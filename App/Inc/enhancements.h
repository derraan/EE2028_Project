/**
 * @file enhancements.h
 * @brief Shared enhancement adapter for live drone + UART bench builds.
 *
 * Live: Core/Src/main.c fills DroneStatus_t and calls OnEvent at shot /
 *       self-destruct points, then Update each loop tick.
 * Bench: App/Test/enhancements_bench.c feeds the same API from UART keys.
 *
 * Compile with -DEE_ENHANCEMENTS=1 to include. Default off keeps PR #3 baseline.
 * Radar + laser aim additionally need -DEE_RADAR_AIM=1 (see enhancements_hw.h).
 */
#ifndef ENHANCEMENTS_H
#define ENHANCEMENTS_H

#include <stdint.h>
#include "drone.h"
#include "ht16k33.h"
#include "status_display.h"
#include "buzzer.h"
#include "ir_threat.h"
#include "relay_effect.h"
#include "rd03d.h"
#include "radar_track.h"
#include "laser_aim.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One-shot actions that cannot be inferred reliably from a status snapshot. */
typedef enum {
    DRONE_EVENT_NONE = 0,
    DRONE_EVENT_LASER_FIRED,
    DRONE_EVENT_SELF_DESTRUCT_STARTED,
    DRONE_EVENT_LASER_RECHARGED,   /* Battle single press added +3 charge     */
    DRONE_EVENT_LASER_DENIED,      /* hold-to-fire refused: charge < 5        */
    DRONE_EVENT_RESCUE_FAILED      /* double press while still upside down    */
} DroneEvent_t;
/* Mode entries (Battle, rescue back to Battle, Standby, THE_LAST_OF_2028) are
 * not events: Enhancements_Update sees them in the DroneStatus_t snapshot. */

/**
 * Snapshot of drone decisions for the matrix / buzzer.
 * rescue_remaining_ms is meaningful only in THE_LAST_OF_2028 (drone clock).
 */
typedef struct {
    DroneState_t mode;
    uint8_t charge;               /* 0..10 */
    uint8_t armed;                /* 1 when Battle and charge >= 5 */
    uint32_t rescue_remaining_ms; /* 0 when not in rescue window */
} DroneStatus_t;

/** Optional hardware backends. NULL entries are safe no-ops. */
typedef struct {
    HT16K33_HandleTypeDef *matrix;   /* NULL → skip matrix I²C */
    const Buzzer_Backend_t *buzzer;
    const IR_Backend_t *ir;
    const IR_Config_t *ir_cfg;
    const Relay_Backend_t *relay;
    const Relay_Config_t *relay_cfg;

    /* Enhancement #5 — RD-03D zombie radar + pan-servo laser (azimuth only).
     * radar NULL → no tracking; servo/laser NULL → tracking + EVT only. */
    const RD03D_Transport_t *radar;
    const RadarTrack_Config_t *radar_cfg;  /* NULL → RadarTrack_DefaultConfig */
    const Servo_Backend_t *servo;
    const Laser_Backend_t *laser;
    const LaserAim_Config_t *aim_cfg;      /* NULL → LaserAim_DefaultConfig */
} Enhancements_Config_t;

void Enhancements_Init(const Enhancements_Config_t *cfg);
void Enhancements_OnEvent(DroneEvent_t event, uint32_t now_ms);
void Enhancements_Update(const DroneStatus_t *status, uint32_t now_ms);

/** 1 when explosion / final burst / relay pulse (if any) have finished. */
uint8_t Enhancements_FinalEffectsDone(uint32_t now_ms);

/** Current radar track (never NULL after Init; valid=0 when no zombie). */
const RadarTarget_t *Enhancements_RadarTarget(void);

/** RD-03D frames parsed OK / with a bad footer since boot (bring-up check). */
void Enhancements_RadarFrameStats(uint32_t *frames_ok, uint32_t *frames_bad);

/** Pan / laser controller state (bench + debug). */
const LaserAim_HandleTypeDef *Enhancements_LaserAim(void);

/** Force outputs quiet / relay LOW (call before permanent halt if needed). */
void Enhancements_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ENHANCEMENTS_H */
