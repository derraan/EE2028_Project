/**
 * @file laser_aim.h
 * @brief Pan-servo laser pointer that follows a RadarTrack target (azimuth only).
 *
 * Geometry: the radar is FIXED; one hobby servo pans only the laser. The
 * laser pivot sits at (offset_x_mm, offset_y_mm) in the radar frame, so the
 * pan goal is the parallax-corrected bearing
 *     goal = atan2(target_x - offset_x, target_y - offset_y)
 * which matters at short range (a 6 cm lever arm is ~3.4° at 1 m).
 *
 * Pure C (stdint + math). Hardware lives behind two backends:
 *   servo: set_pulse_us(500..2500) at 50 Hz;   laser: set_output(on).
 *
 * Laser rules (enforced here, not by callers):
 *   - Laser is ON only when allowed by mode, a target is LOCKED, the servo has
 *     slewed to within aligned_tol_deg of the goal, and the goal is reachable
 *     inside the pan limits.
 *   - LaserAim_Fire() plays a short strobe on the current line of aim, but
 *     only while the same "allowed" gate holds (never a blind flash).
 *   - LaserAim_Safe() forces laser OFF and parks the servo at neutral.
 */
#ifndef LASER_AIM_H
#define LASER_AIM_H

#include <stdint.h>
#include "radar_track.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*set_pulse_us)(void *ctx, uint16_t pulse_us);
    void *ctx;
} Servo_Backend_t;

typedef struct {
    void (*set_output)(void *ctx, uint8_t on);
    void *ctx;
} Laser_Backend_t;

typedef struct {
    uint16_t pulse_center_us;   /* pulse that points the laser along radar boresight */
    uint16_t pulse_min_us;      /* hard electrical limits of the servo */
    uint16_t pulse_max_us;
    float us_per_deg;           /* ~11.1 for a 180° / 2000 µs servo */
    int8_t direction;           /* +1 / -1: which way +azimuth turns the horn */
    float pan_min_deg;          /* mechanical limits (bracket / cable) */
    float pan_max_deg;
    float max_slew_dps;         /* rate limit on the commanded angle */
    float offset_x_mm;          /* laser pivot position in the radar frame */
    float offset_y_mm;
    float aligned_tol_deg;      /* |cmd - goal| below this → laser may light */
    uint16_t lead_ms;           /* aim ahead of a walking zombie (0 = off) */
    uint16_t hold_ms;           /* after LOST, hold the last angle this long… */
    uint16_t fire_strobe_ms;    /* FIRE strobe duration */
    uint16_t fire_strobe_half_ms; /* strobe on/off half-period */
} LaserAim_Config_t;

typedef enum {
    LASER_AIM_PARKED = 0,   /* neutral, no target */
    LASER_AIM_TRACKING,     /* following a locked target */
    LASER_AIM_HOLDING,      /* target lost, holding last angle */
    LASER_AIM_SAFE          /* forced safe (Last of EE2028 / self-destruct) */
} LaserAimState_t;

typedef struct {
    LaserAim_Config_t cfg;
    Servo_Backend_t servo;
    Laser_Backend_t laser;

    LaserAimState_t state;
    float cmd_deg;          /* rate-limited angle sent to the servo */
    float goal_deg;         /* parallax-corrected angle we want */
    uint16_t pulse_us;      /* last pulse written */
    uint8_t reachable;      /* goal within pan limits */
    uint8_t aligned;        /* |cmd - goal| <= tol */
    uint8_t laser_on;       /* last laser output written */
    uint8_t allow_laser;    /* mode gate from the last Update */
    uint32_t last_ms;
    uint32_t lost_ms;
    uint32_t fire_until_ms;
    uint32_t fire_start_ms;
    uint8_t firing;
    uint8_t started;
} LaserAim_HandleTypeDef;

void LaserAim_DefaultConfig(LaserAim_Config_t *cfg);

/** Parks the servo at neutral and writes laser OFF immediately. */
void LaserAim_Init(LaserAim_HandleTypeDef *h,
                   const Servo_Backend_t *servo,
                   const Laser_Backend_t *laser,
                   const LaserAim_Config_t *cfg);

/**
 * Run one control tick.
 * @param tgt          tracker output (NULL = no target)
 * @param allow_track  mode lets the servo follow targets (Standby + Battle)
 * @param allow_laser  mode lets the laser light (Battle only)
 */
void LaserAim_Update(LaserAim_HandleTypeDef *h, const RadarTarget_t *tgt,
                     uint8_t allow_track, uint8_t allow_laser, uint32_t now_ms);

/** Firmware accepted a shot (DRONE_EVENT_LASER_FIRED): strobe if aimed. */
void LaserAim_Fire(LaserAim_HandleTypeDef *h, uint32_t now_ms);

/** Laser OFF, servo to neutral, stay safe until the next allowed Update. */
void LaserAim_Safe(LaserAim_HandleTypeDef *h);

/** Parallax-corrected pan angle (deg) for a target at (x, y) mm. */
float LaserAim_GoalDeg(const LaserAim_Config_t *cfg, float x_mm, float y_mm);

/** Angle → servo pulse with direction, centre and electrical clamps. */
uint16_t LaserAim_DegToPulse(const LaserAim_Config_t *cfg, float deg);

#ifdef __cplusplus
}
#endif

#endif /* LASER_AIM_H */
