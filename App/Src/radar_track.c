/**
 * @file radar_track.c
 * @brief Nearest-threat tracker with confirm / gate / stale timeout.
 */
#include "radar_track.h"
#include <math.h>
#include <string.h>

#define RT_RAD2DEG 57.29577951f
#define RT_MAX_DT_S 0.5f

typedef struct {
    float x;
    float y;
    float range;
    float az;
    int16_t speed;
} Candidate_t;

void RadarTrack_DefaultConfig(RadarTrack_Config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    cfg->min_range_mm = 400U;
    cfg->max_range_mm = 6000U;
    cfg->fov_half_deg = 60.0f;
    cfg->confirm_frames = 3U;
    cfg->lost_timeout_ms = 700U;
    cfg->gate_mm = 800U;
    cfg->switch_margin_mm = 600U;
    cfg->smooth_alpha = 0.5f;
    cfg->mirror_x = 1;
}

void RadarTrack_Init(RadarTrack_HandleTypeDef *h, const RadarTrack_Config_t *cfg)
{
    if (h == NULL) {
        return;
    }
    (void)memset(h, 0, sizeof(*h));
    if (cfg != NULL) {
        h->cfg = *cfg;
    } else {
        RadarTrack_DefaultConfig(&h->cfg);
    }
    if ((h->cfg.smooth_alpha <= 0.0f) || (h->cfg.smooth_alpha > 1.0f)) {
        h->cfg.smooth_alpha = 0.5f;
    }
    if (h->cfg.confirm_frames == 0U) {
        h->cfg.confirm_frames = 1U;
    }
    h->cfg.mirror_x = (h->cfg.mirror_x < 0) ? (int8_t)-1 : (int8_t)1;
}

static void DropTrack(RadarTrack_HandleTypeDef *h)
{
    if (h->tgt.locked != 0U) {
        h->evt_lost_pending = 1U;
        h->evt_lock_pending = 0U;
    }
    h->tgt.valid = 0U;
    h->tgt.locked = 0U;
    h->tgt.vx_mm_s = 0.0f;
    h->tgt.vy_mm_s = 0.0f;
    h->hits = 0U;
}

void RadarTrack_Reset(RadarTrack_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    /* DropTrack queues LOST if a target was locked — keep it, so callers
     * (mode gate, giant down) report EVT RADAR lost. Only a pending LOCK is
     * cancelled. */
    DropTrack(h);
    h->evt_lock_pending = 0U;
    h->tgt.contacts = 0U;
}

static void Derive(RadarTarget_t *t)
{
    t->range_mm = sqrtf((t->x_mm * t->x_mm) + (t->y_mm * t->y_mm));
    t->azimuth_deg = atan2f(t->x_mm, t->y_mm) * RT_RAD2DEG;
}

static void CountHit(RadarTrack_HandleTypeDef *h, uint32_t now_ms)
{
    h->tgt.last_hit_ms = now_ms;
    if (h->hits < 255U) {
        h->hits++;
    }
    if ((h->tgt.locked == 0U) && (h->hits >= h->cfg.confirm_frames)) {
        h->tgt.locked = 1U;
        h->tgt.locked_since_ms = now_ms;
        h->evt_lock_pending = 1U;
    }
}

static void StartTrack(RadarTrack_HandleTypeDef *h, const Candidate_t *c, uint32_t now_ms)
{
    h->tgt.valid = 1U;
    h->tgt.locked = 0U;
    h->tgt.x_mm = c->x;
    h->tgt.y_mm = c->y;
    h->tgt.vx_mm_s = 0.0f;
    h->tgt.vy_mm_s = 0.0f;
    h->tgt.speed_cms = c->speed;
    h->hits = 0U;
    Derive(&h->tgt);
    CountHit(h, now_ms);
}

void RadarTrack_OnFrame(RadarTrack_HandleTypeDef *h, const RD03D_Frame_t *frame, uint32_t now_ms)
{
    Candidate_t cand[RD03D_MAX_TARGETS];
    uint8_t n = 0U;
    uint8_t i;
    int8_t nearest = -1;

    if ((h == NULL) || (frame == NULL)) {
        return;
    }
    h->frames++;

    for (i = 0U; i < RD03D_MAX_TARGETS; i++) {
        const RD03D_Target_t *t = &frame->target[i];
        Candidate_t c;

        if ((t->valid == 0U) || (t->y_mm <= 0)) {
            continue;
        }
        c.x = (float)t->x_mm * (float)h->cfg.mirror_x;
        c.y = (float)t->y_mm;
        c.range = sqrtf((c.x * c.x) + (c.y * c.y));
        c.az = atan2f(c.x, c.y) * RT_RAD2DEG;
        c.speed = t->speed_cms;
        if ((c.range < (float)h->cfg.min_range_mm) ||
            (c.range > (float)h->cfg.max_range_mm) ||
            (fabsf(c.az) > h->cfg.fov_half_deg)) {
            continue;
        }
        cand[n] = c;
        if ((nearest < 0) || (c.range < cand[(uint8_t)nearest].range)) {
            nearest = (int8_t)n;
        }
        n++;
    }
    h->tgt.contacts = n;

    if (n == 0U) {
        return; /* coast; RadarTrack_Update handles the stale timeout */
    }

    if (h->tgt.valid == 0U) {
        StartTrack(h, &cand[(uint8_t)nearest], now_ms);
        return;
    }

    {
        float dt = (float)(uint32_t)(now_ms - h->tgt.last_hit_ms) / 1000.0f;
        float px;
        float py;
        int8_t assoc = -1;
        float best = 0.0f;

        if (dt > RT_MAX_DT_S) {
            dt = RT_MAX_DT_S;
        }
        px = h->tgt.x_mm + (h->tgt.vx_mm_s * dt);
        py = h->tgt.y_mm + (h->tgt.vy_mm_s * dt);

        for (i = 0U; i < n; i++) {
            float dx = cand[i].x - px;
            float dy = cand[i].y - py;
            float d = sqrtf((dx * dx) + (dy * dy));
            if ((assoc < 0) || (d < best)) {
                assoc = (int8_t)i;
                best = d;
            }
        }

        if (best > (float)h->cfg.gate_mm) {
            /* Nothing near our zombie. A tentative track is abandoned at
             * once; a locked one coasts until lost_timeout_ms. */
            if (h->tgt.locked == 0U) {
                StartTrack(h, &cand[(uint8_t)nearest], now_ms);
            }
            return;
        }

        /* A clearly closer zombie steals the lock (re-confirmed from scratch). */
        if ((nearest != assoc) &&
            ((cand[(uint8_t)nearest].range + (float)h->cfg.switch_margin_mm) <
             cand[(uint8_t)assoc].range)) {
            DropTrack(h);
            StartTrack(h, &cand[(uint8_t)nearest], now_ms);
            return;
        }

        {
            const Candidate_t *c = &cand[(uint8_t)assoc];
            float a = h->cfg.smooth_alpha;
            float b = (a * a) / (2.0f - a); /* critically damped alpha-beta */
            float rx = c->x - px;
            float ry = c->y - py;

            h->tgt.x_mm = px + (a * rx);
            h->tgt.y_mm = py + (a * ry);
            if (dt > 0.001f) {
                h->tgt.vx_mm_s += (b * rx) / dt;
                h->tgt.vy_mm_s += (b * ry) / dt;
            }
            h->tgt.speed_cms = c->speed;
            Derive(&h->tgt);
            CountHit(h, now_ms);
        }
    }
}

void RadarTrack_Update(RadarTrack_HandleTypeDef *h, uint32_t now_ms)
{
    if ((h == NULL) || (h->tgt.valid == 0U)) {
        return;
    }
    if ((uint32_t)(now_ms - h->tgt.last_hit_ms) >= h->cfg.lost_timeout_ms) {
        DropTrack(h);
        h->tgt.contacts = 0U;
    }
}

RadarTrackEvent_t RadarTrack_PollEvent(RadarTrack_HandleTypeDef *h)
{
    if (h == NULL) {
        return RADAR_TRACK_EVT_NONE;
    }
    if (h->evt_lost_pending != 0U) {
        h->evt_lost_pending = 0U;
        return RADAR_TRACK_EVT_LOST;
    }
    if (h->evt_lock_pending != 0U) {
        h->evt_lock_pending = 0U;
        return RADAR_TRACK_EVT_LOCK;
    }
    return RADAR_TRACK_EVT_NONE;
}

const RadarTarget_t *RadarTrack_Target(const RadarTrack_HandleTypeDef *h)
{
    return (h != NULL) ? &h->tgt : NULL;
}

uint8_t RadarTrack_MatrixColumn(const RadarTrack_HandleTypeDef *h)
{
    float span;
    float k;
    int col;

    if ((h == NULL) || (h->tgt.locked == 0U) || (h->cfg.fov_half_deg <= 0.0f)) {
        return 0xFFU;
    }
    span = 2.0f * h->cfg.fov_half_deg;
    k = (h->tgt.azimuth_deg + h->cfg.fov_half_deg) / span; /* 0..1 */
    col = (int)(k * 8.0f);
    if (col < 0) {
        col = 0;
    }
    if (col > 7) {
        col = 7;
    }
    return (uint8_t)col;
}
