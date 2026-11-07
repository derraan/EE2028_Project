/**
 * @file radar_track.h
 * @brief Single-zombie tracker over RD-03D multi-target frames.
 *
 * Pure C (stdint + math). Picks ONE threat (the nearest valid return inside
 * the range / field-of-view gates), keeps it across frames with a distance
 * gate, smooths position, and declares LOCK only after `confirm_frames`
 * consecutive associated hits. A track that receives no hits for
 * `lost_timeout_ms` is dropped (LOST).
 *
 * Coordinate frame (radar at origin, looking along +y):
 *   azimuth_deg = atan2(x, y)  — 0 = boresight, + = toward +x (right when
 *   mirror_x is set correctly; see docs/Radar_Aim_Design.md bring-up).
 *
 * Detection only — no drone mode or fire authority (same rule as IR).
 */
#ifndef RADAR_TRACK_H
#define RADAR_TRACK_H

#include <stdint.h>
#include "rd03d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t min_range_mm;     /* reject returns closer than this (module min ~300) */
    uint16_t max_range_mm;     /* reject returns farther than this */
    float fov_half_deg;        /* reject |azimuth| beyond this */
    uint8_t confirm_frames;    /* consecutive hits before LOCK */
    uint16_t lost_timeout_ms;  /* no hits this long → LOST */
    uint16_t gate_mm;          /* association gate around the predicted position */
    uint16_t switch_margin_mm; /* a new return must be this much closer to steal the lock */
    float smooth_alpha;        /* EMA weight of the new measurement (0..1] */
    int8_t mirror_x;           /* +1 or -1: flips x if the module is mounted inverted */
} RadarTrack_Config_t;

typedef struct {
    uint8_t valid;          /* a track exists (tentative or locked) */
    uint8_t locked;         /* confirmed — safe to aim at */
    float x_mm;             /* smoothed, mirror applied */
    float y_mm;
    float vx_mm_s;          /* smoothed velocity (for optional lead) */
    float vy_mm_s;
    float azimuth_deg;      /* atan2(x, y) of the smoothed position */
    float range_mm;         /* hypot(x, y) */
    int16_t speed_cms;      /* last raw radial speed */
    uint8_t contacts;       /* valid gated returns in the last frame */
    uint32_t last_hit_ms;
    uint32_t locked_since_ms;
} RadarTarget_t;

typedef enum {
    RADAR_TRACK_EVT_NONE = 0,
    RADAR_TRACK_EVT_LOCK,
    RADAR_TRACK_EVT_LOST
} RadarTrackEvent_t;

typedef struct {
    RadarTrack_Config_t cfg;
    RadarTarget_t tgt;
    uint8_t hits;
    uint8_t evt_lock_pending;
    uint8_t evt_lost_pending;
    uint32_t frames;
} RadarTrack_HandleTypeDef;

/** Sensible defaults for a lab room (0.4–6 m, ±60°, 3-frame confirm). */
void RadarTrack_DefaultConfig(RadarTrack_Config_t *cfg);

void RadarTrack_Init(RadarTrack_HandleTypeDef *h, const RadarTrack_Config_t *cfg);
/** Drop any track. Queues RADAR_TRACK_EVT_LOST if a target was locked. */
void RadarTrack_Reset(RadarTrack_HandleTypeDef *h);

/** Feed one decoded frame (call for every frame, including empty ones). */
void RadarTrack_OnFrame(RadarTrack_HandleTypeDef *h, const RD03D_Frame_t *frame, uint32_t now_ms);

/** Age the track (stale timeout). Call every loop tick. */
void RadarTrack_Update(RadarTrack_HandleTypeDef *h, uint32_t now_ms);

/** One pending event per call: LOCK / LOST / NONE (LOST is reported first). */
RadarTrackEvent_t RadarTrack_PollEvent(RadarTrack_HandleTypeDef *h);

const RadarTarget_t *RadarTrack_Target(const RadarTrack_HandleTypeDef *h);

/**
 * Map azimuth onto the 8-column matrix: -fov_half → col 0, +fov_half → col 7.
 * Returns 0xFF when there is no locked target.
 */
uint8_t RadarTrack_MatrixColumn(const RadarTrack_HandleTypeDef *h);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_TRACK_H */
