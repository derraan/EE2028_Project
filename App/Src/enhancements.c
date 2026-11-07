/**
 * @file enhancements.c
 * @brief Thin adapter: drone status/events → matrix, buzzer, IR chirp, relay,
 *        RD-03D zombie radar + pan-servo laser.
 */
#include "enhancements.h"
#include "telemetry_evt.h"
#include <stdio.h>
#include <string.h>

#ifndef EE_FINAL_EFFECTS_MS
#define EE_FINAL_EFFECTS_MS 1500U
#endif

/* EVT RADAR cadence while a zombie is locked (UART @115200 has headroom). */
#ifndef EE_RADAR_EVT_PERIOD_MS
#define EE_RADAR_EVT_PERIOD_MS 250U
#endif

/* Bytes drained from the radar transport per tick (30-byte frames, ~10 Hz). */
#ifndef EE_RADAR_RX_CHUNK
#define EE_RADAR_RX_CHUNK 64U
#endif
#ifndef EE_RADAR_RX_MAX_CHUNKS
#define EE_RADAR_RX_MAX_CHUNKS 8U
#endif

typedef struct {
    uint8_t inited;
    uint8_t matrix_ok;

    HT16K33_HandleTypeDef *matrix_dev;
    StatusDisplay_HandleTypeDef status;
    Buzzer_HandleTypeDef buzzer;
    IR_HandleTypeDef ir;
    Relay_HandleTypeDef relay;
    RD03D_Transport_t radar_io;
    RD03D_Parser_t radar_parser;
    RadarTrack_HandleTypeDef track;
    LaserAim_HandleTypeDef aim;

    uint8_t have_buzzer;
    uint8_t have_ir;
    uint8_t have_relay;
    uint8_t have_radar;
    uint8_t last_radar_col;
    uint32_t radar_evt_ms;

    DroneState_t last_mode;
    uint8_t last_charge;
    uint8_t last_armed;
    uint8_t in_last;
    uint8_t terminal_started;
    uint32_t terminal_start_ms;
} Enhancements_Ctx_t;

static Enhancements_Ctx_t s_ee;

static uint8_t CountdownFromRemaining(uint32_t remaining_ms)
{
    uint8_t val;

    if (remaining_ms == 0U) {
        return 0U;
    }
    val = (uint8_t)((remaining_ms + 999U) / 1000U);
    if (val > 10U) {
        val = 10U;
    }
    return val;
}

void Enhancements_Init(const Enhancements_Config_t *cfg)
{
    Buzzer_Backend_t silent_buzzer;
    IR_Backend_t silent_ir;
    IR_Config_t default_ir_cfg;
    Relay_Backend_t silent_relay;
    Relay_Config_t default_relay_cfg;

    (void)memset(&s_ee, 0, sizeof(s_ee));
    s_ee.last_mode = STANDBY_MODE;
    s_ee.last_charge = 0xFFU;
    s_ee.last_radar_col = 0xFFU;
    RadarTrack_Init(&s_ee.track, NULL);
    LaserAim_Init(&s_ee.aim, NULL, NULL, NULL);

    if (cfg == NULL) {
        s_ee.inited = 1U;
        return;
    }

    s_ee.matrix_dev = cfg->matrix;
    if (s_ee.matrix_dev != NULL) {
        StatusDisplay_Init(&s_ee.status, s_ee.matrix_dev);
        /* Drone boots in Standby: radar shown as the 180° dial there. */
        StatusDisplay_SetRadarStyle(&s_ee.status, STATUS_RADAR_DIAL);
        s_ee.matrix_ok = 1U;
    }

    if (cfg->buzzer != NULL) {
        Buzzer_Init(&s_ee.buzzer, cfg->buzzer);
        s_ee.have_buzzer = 1U;
    } else {
        silent_buzzer.set_tone = NULL;
        silent_buzzer.ctx = NULL;
        Buzzer_Init(&s_ee.buzzer, &silent_buzzer);
        s_ee.have_buzzer = 0U;
    }

    default_ir_cfg.active_level = 0U; /* Grove IR: active-LOW */
    default_ir_cfg.debounce_ms = 30U;
    default_ir_cfg.rearm_ms = 200U;
    if (cfg->ir != NULL) {
        IR_Init(&s_ee.ir, cfg->ir,
                (cfg->ir_cfg != NULL) ? cfg->ir_cfg : &default_ir_cfg);
        s_ee.have_ir = 1U;
    } else {
        silent_ir.read_raw = NULL;
        silent_ir.ctx = NULL;
        IR_Init(&s_ee.ir, &silent_ir, &default_ir_cfg);
        s_ee.have_ir = 0U;
    }

    default_relay_cfg.pulse_ms = 0U; /* latch HIGH until Reset (design default) */
    if (cfg->relay != NULL) {
        RelayEffect_Init(&s_ee.relay, cfg->relay,
                         (cfg->relay_cfg != NULL) ? cfg->relay_cfg
                                                 : &default_relay_cfg);
        s_ee.have_relay = 1U;
    } else {
        silent_relay.set_output = NULL;
        silent_relay.ctx = NULL;
        RelayEffect_Init(&s_ee.relay, &silent_relay, &default_relay_cfg);
        s_ee.have_relay = 0U;
    }

    /* Laser OFF + servo neutral first, even if the radar is absent. */
    LaserAim_Init(&s_ee.aim, cfg->servo, cfg->laser, cfg->aim_cfg);
    RadarTrack_Init(&s_ee.track, cfg->radar_cfg);
    RD03D_ParserInit(&s_ee.radar_parser);
    if ((cfg->radar != NULL) && (cfg->radar->read != NULL)) {
        uint8_t cmd[RD03D_CMD_LEN];
        s_ee.radar_io = *cfg->radar;
        s_ee.have_radar = 1U;
        /* Multi-target mode so a nearer zombie can steal the lock. */
        if (s_ee.radar_io.write != NULL) {
            uint8_t len = RD03D_BuildTrackingModeCmd(cmd, 1U);
            s_ee.radar_io.write(s_ee.radar_io.ctx, cmd, len);
        }
    }

    s_ee.inited = 1U;
}

static int16_t ToTenths(float v)
{
    float t = v * 10.0f;
    if (t > 32767.0f) {
        t = 32767.0f;
    }
    if (t < -32767.0f) {
        t = -32767.0f;
    }
    return (int16_t)((t < 0.0f) ? (t - 0.5f) : (t + 0.5f));
}

static void Radar_EmitTrack(void)
{
    const RadarTarget_t *t = &s_ee.track.tgt;
    float range = t->range_mm;
    if (range > 65535.0f) {
        range = 65535.0f;
    }
    Telemetry_EvtRadar(ToTenths(t->azimuth_deg), (uint16_t)range, t->locked,
                       ToTenths(s_ee.aim.cmd_deg), s_ee.aim.laser_on, t->contacts);
}

/**
 * Radar → tracker → pan/laser. Detection and aiming only: no mode change and
 * no fire authority (hold-to-fire + charge stay in main.c).
 */
static void Radar_Update(DroneState_t mode, uint32_t now_ms)
{
    uint8_t allow_track;
    uint8_t allow_laser;
    uint8_t col;
    RadarTrackEvent_t evt;

    allow_track = (uint8_t)((((mode == STANDBY_MODE) || (mode == BATTLE_MODE)) &&
                             (s_ee.terminal_started == 0U))
                                ? 1U
                                : 0U);
    allow_laser = (uint8_t)(((mode == BATTLE_MODE) && (allow_track != 0U)) ? 1U : 0U);

    if (s_ee.have_radar != 0U) {
        uint8_t rx[EE_RADAR_RX_CHUNK];
        uint8_t chunks;
        RD03D_Frame_t frame;

        /* Always drain so the parser stays in sync, even while not tracking. */
        for (chunks = 0U; chunks < EE_RADAR_RX_MAX_CHUNKS; chunks++) {
            uint16_t n = s_ee.radar_io.read(s_ee.radar_io.ctx, rx, (uint16_t)sizeof(rx));
            uint16_t i;
            if (n == 0U) {
                break;
            }
            for (i = 0U; i < n; i++) {
                if ((RD03D_PushByte(&s_ee.radar_parser, rx[i], &frame) != 0U) &&
                    (allow_track != 0U)) {
                    RadarTrack_OnFrame(&s_ee.track, &frame, now_ms);
                }
            }
        }
    }

    if (allow_track == 0U) {
        RadarTrack_Reset(&s_ee.track); /* queues LOST if a zombie was locked */
    }
    RadarTrack_Update(&s_ee.track, now_ms);

    LaserAim_Update(&s_ee.aim, &s_ee.track.tgt, allow_track, allow_laser, now_ms);

    while ((evt = RadarTrack_PollEvent(&s_ee.track)) != RADAR_TRACK_EVT_NONE) {
        if (evt == RADAR_TRACK_EVT_LOCK) {
            Radar_EmitTrack();
            s_ee.radar_evt_ms = now_ms;
            if ((s_ee.have_buzzer != 0U) && (mode == BATTLE_MODE)) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_THREAT_CHIRP);
            }
        } else {
            Telemetry_EvtRadarLost();
        }
    }
    if ((s_ee.track.tgt.locked != 0U) &&
        ((uint32_t)(now_ms - s_ee.radar_evt_ms) >= EE_RADAR_EVT_PERIOD_MS)) {
        s_ee.radar_evt_ms = now_ms;
        Radar_EmitTrack();
    }

    col = RadarTrack_MatrixColumn(&s_ee.track);
    if ((s_ee.matrix_ok != 0U) && (col != s_ee.last_radar_col)) {
        StatusDisplay_SetAzimuth(&s_ee.status, col);
    }
    s_ee.last_radar_col = col;
}

const RadarTarget_t *Enhancements_RadarTarget(void)
{
    return &s_ee.track.tgt;
}

const LaserAim_HandleTypeDef *Enhancements_LaserAim(void)
{
    return &s_ee.aim;
}

void Enhancements_OnEvent(DroneEvent_t event, uint32_t now_ms)
{
    if (s_ee.inited == 0U) {
        return;
    }

    switch (event) {
        case DRONE_EVENT_LASER_FIRED:
            if (s_ee.have_buzzer != 0U) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_LASER_FIRE);
            }
            /* Strobe the pointer on the locked zombie (gated by aim + mode). */
            LaserAim_Fire(&s_ee.aim, now_ms);
            break;

        case DRONE_EVENT_SELF_DESTRUCT_STARTED:
            if (s_ee.terminal_started == 0U) {
                s_ee.terminal_started = 1U;
                s_ee.terminal_start_ms = now_ms;
                LaserAim_Safe(&s_ee.aim);
                if (s_ee.matrix_ok != 0U) {
                    StatusDisplay_TriggerExplosion(&s_ee.status);
                }
                if (s_ee.have_buzzer != 0U) {
                    Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_FINAL_BURST);
                }
                /* Relay fires even if matrix is disconnected */
                RelayEffect_Trigger(&s_ee.relay, now_ms);
            }
            break;

        case DRONE_EVENT_LASER_RECHARGED:
            if (s_ee.have_buzzer != 0U) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_RECHARGE);
            }
            break;

        case DRONE_EVENT_LASER_DENIED:
            if (s_ee.have_buzzer != 0U) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_DENIED);
            }
            break;

        case DRONE_EVENT_RESCUE_FAILED:
            if (s_ee.have_buzzer != 0U) {
                /* Beats the countdown loop, which then resumes. */
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_RESCUE_FAIL);
            }
            break;

        case DRONE_EVENT_NONE:
        default:
            break;
    }
}

void Enhancements_Update(const DroneStatus_t *status, uint32_t now_ms)
{
    DroneState_t mode;
    uint8_t charge;
    uint8_t armed;

    if ((s_ee.inited == 0U) || (status == NULL)) {
        return;
    }

    mode = status->mode;
    charge = status->charge;
    if (charge > 10U) {
        charge = 10U;
    }
    armed = (status->armed != 0U) ? 1U : 0U;

    /* --- Mode transitions observed from snapshot --- */
    if (mode != s_ee.last_mode) {
        if (mode == BATTLE_MODE) {
            if (s_ee.matrix_ok != 0U) {
                StatusDisplay_SetRadarStyle(&s_ee.status, STATUS_RADAR_LINE);
            }
            if (s_ee.in_last != 0U) {
                /* Rescue: THE_LAST_OF_2028 → BATTLE */
                if (s_ee.matrix_ok != 0U) {
                    StatusDisplay_Rescue(&s_ee.status);
                }
                if (s_ee.have_buzzer != 0U) {
                    Buzzer_Stop(&s_ee.buzzer); /* end countdown loop */
                    Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_RESCUE_OK);
                }
                RelayEffect_Reset(&s_ee.relay);
                s_ee.in_last = 0U;
            } else {
                /* Standby → Battle (double press or T/A/G threshold) */
                if (s_ee.matrix_ok != 0U) {
                    StatusDisplay_EnterBattle(&s_ee.status);
                }
                if (s_ee.have_buzzer != 0U) {
                    Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_BATTLE_ENTRY);
                }
            }
        } else if (mode == THE_LAST_OF_2028) {
            s_ee.in_last = 1U;
            if (s_ee.matrix_ok != 0U) {
                StatusDisplay_EnterLastOf2028(&s_ee.status);
            }
            if (s_ee.have_buzzer != 0U) {
                /* Countdown cadence follows drone remaining via SetCountdown. */
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_COUNTDOWN);
            }
        } else if (mode == STANDBY_MODE) {
            s_ee.in_last = 0U;
            if (s_ee.matrix_ok != 0U) {
                StatusDisplay_SetArmed(&s_ee.status, 0U);
                StatusDisplay_SetRadarStyle(&s_ee.status, STATUS_RADAR_DIAL);
            }
            if (s_ee.have_buzzer != 0U) {
                Buzzer_Stop(&s_ee.buzzer);
            }
        }
        s_ee.last_mode = mode;
    }

    /* Charge / armed */
    if (s_ee.matrix_ok != 0U) {
        if (charge != s_ee.last_charge) {
            StatusDisplay_SetCharge(&s_ee.status, charge);
            s_ee.last_charge = charge;
        }
        if (armed != s_ee.last_armed) {
            StatusDisplay_SetArmed(&s_ee.status, armed);
            s_ee.last_armed = armed;
        }
    }

    /* One rescue deadline: matrix + buzzer consume remaining from drone */
    if ((mode == THE_LAST_OF_2028) && (s_ee.terminal_started == 0U)) {
        uint8_t cd = CountdownFromRemaining(status->rescue_remaining_ms);
        if (s_ee.matrix_ok != 0U) {
            StatusDisplay_SetRescueRemainingMs(&s_ee.status,
                                               status->rescue_remaining_ms);
        }
        if (s_ee.have_buzzer != 0U) {
            Buzzer_SetCountdown(&s_ee.buzzer, cd);
            /* Only (re)start countdown if base went silent after a one-shot. */
            if (s_ee.buzzer.base_pattern != BUZZER_PATTERN_COUNTDOWN &&
                s_ee.buzzer.active_pattern != BUZZER_PATTERN_FINAL_BURST) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_COUNTDOWN);
            }
        }
    }

    /* IR: detect + chirp + log only — no mode / fire authority */
    if (s_ee.have_ir != 0U) {
        IR_Update(&s_ee.ir, now_ms);
        if (IR_PollThreat(&s_ee.ir) != 0U) {
            printf("IR threat detected (enhancement only — no mode change).\r\n");
            if (s_ee.have_buzzer != 0U) {
                Buzzer_Play(&s_ee.buzzer, BUZZER_PATTERN_THREAT_CHIRP);
            }
        }
    }

    Radar_Update(mode, now_ms);

    if (s_ee.have_buzzer != 0U) {
        Buzzer_Update(&s_ee.buzzer, now_ms);
    }
    RelayEffect_Update(&s_ee.relay, now_ms);

    if (s_ee.matrix_ok != 0U) {
        /* Failed display must not block gameplay — ignore I²C errors. */
        (void)StatusDisplay_Update(&s_ee.status, now_ms);
    }
}

uint8_t Enhancements_FinalEffectsDone(uint32_t now_ms)
{
    uint8_t matrix_done = 1U;
    uint8_t buzzer_done = 1U;
    uint8_t relay_done = 1U;
    uint8_t time_done;

    if (s_ee.inited == 0U) {
        return 1U;
    }
    if (s_ee.terminal_started == 0U) {
        return 0U;
    }

    time_done = ((uint32_t)(now_ms - s_ee.terminal_start_ms) >= EE_FINAL_EFFECTS_MS)
                    ? 1U
                    : 0U;

    if (s_ee.matrix_ok != 0U) {
        matrix_done = (s_ee.status.terminated != 0U) ? 1U : 0U;
    }
    if (s_ee.have_buzzer != 0U) {
        buzzer_done = (s_ee.buzzer.active_pattern == BUZZER_PATTERN_SILENT ||
                       s_ee.buzzer.latched_silent != 0U)
                          ? 1U
                          : 0U;
    }
    if (s_ee.have_relay != 0U) {
        /* Latched relay (pulse_ms==0) is "done" once triggered; pulsed waits LOW. */
        if (s_ee.relay.cfg.pulse_ms == 0U) {
            relay_done = (s_ee.relay.fired != 0U) ? 1U : 0U;
        } else {
            relay_done = (RelayEffect_IsActive(&s_ee.relay) == 0U) ? 1U : 0U;
        }
    }

    return (uint8_t)((time_done != 0U) && (matrix_done != 0U) &&
                     (buzzer_done != 0U) && (relay_done != 0U));
}

void Enhancements_Shutdown(void)
{
    if (s_ee.inited == 0U) {
        return;
    }
    if (s_ee.have_buzzer != 0U) {
        Buzzer_Stop(&s_ee.buzzer);
    }
    LaserAim_Safe(&s_ee.aim);
    /* For pulsed relays ensure LOW; latched stays HIGH after boom by design. */
    if ((s_ee.have_relay != 0U) && (s_ee.relay.cfg.pulse_ms != 0U)) {
        RelayEffect_Reset(&s_ee.relay);
    }
}
