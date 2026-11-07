/**
 * @file laser_aim.c
 * @brief Pan servo + laser gate (see laser_aim.h for the rules).
 */
#include "laser_aim.h"
#include <math.h>
#include <string.h>

#define LA_RAD2DEG 57.29577951f
#define LA_MAX_DT_S 0.2f

void LaserAim_DefaultConfig(LaserAim_Config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->pulse_center_us = 1500U;
    cfg->pulse_min_us = 600U;
    cfg->pulse_max_us = 2400U;
    cfg->us_per_deg = 2000.0f / 180.0f;
    cfg->direction = 1;
    cfg->pan_min_deg = -70.0f;
    cfg->pan_max_deg = 70.0f;
    cfg->max_slew_dps = 180.0f;
    cfg->offset_x_mm = 0.0f;
    cfg->offset_y_mm = 0.0f;
    cfg->aligned_tol_deg = 3.0f;
    cfg->lead_ms = 0U;
    cfg->hold_ms = 1200U;
    cfg->fire_strobe_ms = 300U;
    cfg->fire_strobe_half_ms = 40U;
}

float LaserAim_GoalDeg(const LaserAim_Config_t *cfg, float x_mm, float y_mm)
{
    float dx = x_mm;
    float dy = y_mm;

    if (cfg != NULL) {
        dx -= cfg->offset_x_mm;
        dy -= cfg->offset_y_mm;
    }
    if ((dx == 0.0f) && (dy == 0.0f)) {
        return 0.0f;
    }
    return atan2f(dx, dy) * LA_RAD2DEG;
}

uint16_t LaserAim_DegToPulse(const LaserAim_Config_t *cfg, float deg)
{
    float us;

    if (cfg == NULL) {
        return 1500U;
    }
    us = (float)cfg->pulse_center_us +
         ((float)((cfg->direction < 0) ? -1 : 1) * deg * cfg->us_per_deg);
    if (us < (float)cfg->pulse_min_us) {
        us = (float)cfg->pulse_min_us;
    }
    if (us > (float)cfg->pulse_max_us) {
        us = (float)cfg->pulse_max_us;
    }
    return (uint16_t)(us + 0.5f);
}

static void WritePulse(LaserAim_HandleTypeDef *h, uint8_t force)
{
    uint16_t us = LaserAim_DegToPulse(&h->cfg, h->cmd_deg);
    if ((force != 0U) || (us != h->pulse_us)) {
        h->pulse_us = us;
        if (h->servo.set_pulse_us != NULL) {
            h->servo.set_pulse_us(h->servo.ctx, us);
        }
    }
}

static void WriteLaser(LaserAim_HandleTypeDef *h, uint8_t on, uint8_t force)
{
    on = (on != 0U) ? 1U : 0U;
    if ((force != 0U) || (on != h->laser_on)) {
        h->laser_on = on;
        if (h->laser.set_output != NULL) {
            h->laser.set_output(h->laser.ctx, on);
        }
    }
}

void LaserAim_Init(LaserAim_HandleTypeDef *h,
                   const Servo_Backend_t *servo,
                   const Laser_Backend_t *laser,
                   const LaserAim_Config_t *cfg)
{
    if (h == NULL) {
        return;
    }
    (void)memset(h, 0, sizeof(*h));
    if (cfg != NULL) {
        h->cfg = *cfg;
    } else {
        LaserAim_DefaultConfig(&h->cfg);
    }
    if (h->cfg.pan_min_deg > h->cfg.pan_max_deg) {
        float t = h->cfg.pan_min_deg;
        h->cfg.pan_min_deg = h->cfg.pan_max_deg;
        h->cfg.pan_max_deg = t;
    }
    if (servo != NULL) {
        h->servo = *servo;
    }
    if (laser != NULL) {
        h->laser = *laser;
    }
    h->state = LASER_AIM_PARKED;
    h->reachable = 1U;
    WriteLaser(h, 0U, 1U); /* laser OFF before the servo moves */
    WritePulse(h, 1U);
}

void LaserAim_Safe(LaserAim_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    WriteLaser(h, 0U, 1U);
    h->firing = 0U;
    h->goal_deg = 0.0f;
    h->cmd_deg = 0.0f;
    h->aligned = 0U;
    h->state = LASER_AIM_SAFE;
    WritePulse(h, 0U);
}

void LaserAim_Fire(LaserAim_HandleTypeDef *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    /* The strobe still obeys the aim gate in Update — no blind flashes. */
    h->firing = 1U;
    h->fire_start_ms = now_ms;
    h->fire_until_ms = now_ms + h->cfg.fire_strobe_ms;
}

void LaserAim_Update(LaserAim_HandleTypeDef *h, const RadarTarget_t *tgt,
                     uint8_t allow_track, uint8_t allow_laser, uint32_t now_ms)
{
    float dt = 0.0f;
    float step;
    float err;
    uint8_t aim_ok;
    uint8_t on;

    if (h == NULL) {
        return;
    }
    if (h->started != 0U) {
        dt = (float)(uint32_t)(now_ms - h->last_ms) / 1000.0f;
        if (dt > LA_MAX_DT_S) {
            dt = LA_MAX_DT_S;
        }
    }
    h->started = 1U;
    h->last_ms = now_ms;
    h->allow_laser = (allow_laser != 0U) ? 1U : 0U;

    /* --- choose the goal --- */
    if (allow_track == 0U) {
        h->state = LASER_AIM_SAFE;
        h->goal_deg = 0.0f;
        h->reachable = 1U;
    } else if ((tgt != NULL) && (tgt->valid != 0U) && (tgt->locked != 0U)) {
        float lead_s = (float)h->cfg.lead_ms / 1000.0f;
        float g = LaserAim_GoalDeg(&h->cfg,
                                   tgt->x_mm + (tgt->vx_mm_s * lead_s),
                                   tgt->y_mm + (tgt->vy_mm_s * lead_s));
        h->reachable = (uint8_t)(((g >= h->cfg.pan_min_deg) && (g <= h->cfg.pan_max_deg)) ? 1U : 0U);
        if (g < h->cfg.pan_min_deg) {
            g = h->cfg.pan_min_deg;
        }
        if (g > h->cfg.pan_max_deg) {
            g = h->cfg.pan_max_deg;
        }
        h->goal_deg = g;
        h->state = LASER_AIM_TRACKING;
    } else if (h->state == LASER_AIM_TRACKING) {
        h->state = LASER_AIM_HOLDING;
        h->lost_ms = now_ms;
    } else if (h->state == LASER_AIM_HOLDING) {
        if ((uint32_t)(now_ms - h->lost_ms) >= h->cfg.hold_ms) {
            h->state = LASER_AIM_PARKED;
            h->goal_deg = 0.0f;
            h->reachable = 1U;
        }
    } else {
        h->state = LASER_AIM_PARKED;
        h->goal_deg = 0.0f;
        h->reachable = 1U;
    }

    /* --- rate-limited slew --- */
    err = h->goal_deg - h->cmd_deg;
    step = h->cfg.max_slew_dps * dt;
    if (h->cfg.max_slew_dps <= 0.0f) {
        h->cmd_deg = h->goal_deg;
    } else if (err > step) {
        h->cmd_deg += step;
    } else if (err < -step) {
        h->cmd_deg -= step;
    } else {
        h->cmd_deg = h->goal_deg;
    }
    WritePulse(h, 0U);
    h->aligned = (uint8_t)((fabsf(h->goal_deg - h->cmd_deg) <= h->cfg.aligned_tol_deg) ? 1U : 0U);

    /* --- laser gate --- */
    aim_ok = (uint8_t)(((h->allow_laser != 0U) && (h->state == LASER_AIM_TRACKING) &&
                        (h->reachable != 0U) && (h->aligned != 0U))
                           ? 1U
                           : 0U);
    on = aim_ok;
    if (h->firing != 0U) {
        if ((int32_t)(now_ms - h->fire_until_ms) < 0) {
            uint32_t half = (h->cfg.fire_strobe_half_ms == 0U) ? 1U : h->cfg.fire_strobe_half_ms;
            uint32_t phase = (uint32_t)(now_ms - h->fire_start_ms) / half;
            on = (uint8_t)((aim_ok != 0U) && ((phase & 1U) == 0U));
        } else {
            h->firing = 0U;
        }
    }
    WriteLaser(h, on, 0U);
}
