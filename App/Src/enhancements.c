/**
 * @file enhancements.c
 * @brief Thin adapter: drone status/events → matrix, buzzer, IR chirp, relay.
 */
#include "enhancements.h"
#include <stdio.h>
#include <string.h>

#ifndef EE_FINAL_EFFECTS_MS
#define EE_FINAL_EFFECTS_MS 1500U
#endif

typedef struct {
    uint8_t inited;
    uint8_t matrix_ok;

    HT16K33_HandleTypeDef *matrix_dev;
    StatusDisplay_HandleTypeDef status;
    Buzzer_HandleTypeDef buzzer;
    IR_HandleTypeDef ir;
    Relay_HandleTypeDef relay;

    uint8_t have_buzzer;
    uint8_t have_ir;
    uint8_t have_relay;

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

    if (cfg == NULL) {
        s_ee.inited = 1U;
        return;
    }

    s_ee.matrix_dev = cfg->matrix;
    if (s_ee.matrix_dev != NULL) {
        StatusDisplay_Init(&s_ee.status, s_ee.matrix_dev);
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

    s_ee.inited = 1U;
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
            break;

        case DRONE_EVENT_SELF_DESTRUCT_STARTED:
            if (s_ee.terminal_started == 0U) {
                s_ee.terminal_started = 1U;
                s_ee.terminal_start_ms = now_ms;
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
            if (s_ee.in_last != 0U) {
                /* Rescue: THE_LAST_OF_2028 → BATTLE */
                if (s_ee.matrix_ok != 0U) {
                    StatusDisplay_Rescue(&s_ee.status);
                }
                if (s_ee.have_buzzer != 0U) {
                    Buzzer_Stop(&s_ee.buzzer);
                }
                RelayEffect_Reset(&s_ee.relay);
                s_ee.in_last = 0U;
            } else if (s_ee.matrix_ok != 0U) {
                StatusDisplay_EnterBattle(&s_ee.status);
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
    /* For pulsed relays ensure LOW; latched stays HIGH after boom by design. */
    if ((s_ee.have_relay != 0U) && (s_ee.relay.cfg.pulse_ms != 0U)) {
        RelayEffect_Reset(&s_ee.relay);
    }
}
