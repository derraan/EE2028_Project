/**
 * @file telemetry_evt.h
 * @brief Unified mission UART protocol (human lines + EVT) for USART/VCP.
 *
 * Mission-facing text goes through Telemetry_* so Tera Term and VESPER share
 * one stream. Keep CubeIDE console printf for debug only (semihosting).
 *
 * See docs/Telemetry_EVT_Contract.md for the wire contract.
 */
#ifndef TELEMETRY_EVT_H
#define TELEMETRY_EVT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mode tags emitted in EVT MODE=… (prefer THESE over human prose). */
#define EVT_MODE_STANDBY           "STANDBY_MODE"
#define EVT_MODE_BATTLE            "BATTLE_MODE"
#define EVT_MODE_THE_LAST_OF_2028  "THE_LAST_OF_2028"

/** SELF_DESTRUCT reason tokens. */
#define EVT_REASON_TIMEOUT       "timeout"
#define EVT_REASON_RESCUE_LIMIT  "rescue_limit"
#define EVT_REASON_EARLY         "early"

/**
 * Start USART1 (ST-LINK Virtual COM, PB6/PB7) at 115200 8N1 for telemetry.
 * Call once right after HAL_Init(). Implemented in telemetry_uart.c.
 */
void Telemetry_UartInit(void);

/**
 * Low-level line writer (appends \\r\\n). On the board, telemetry_uart.c sends
 * it to the ST-LINK COM port. The weak default in telemetry_evt.c (printf) is
 * only used by host tests that do not link telemetry_uart.c.
 */
void Telemetry_WriteLine(const char *line);

/**
 * Format one mission line (no trailing newline in fmt) then Telemetry_WriteLine.
 * Use for human-readable UART prose; do not use for CubeIDE-only debug.
 */
void Telemetry_Printf(const char *fmt, ...);

/** PR #3 sensor layout (Standby nominal path). Vectors keep axis order X,Y,Z. */
void Telemetry_SendSensors(float t_c, float p_hpa, float h_pct,
                           const float accel_mps2[3],
                           const float gyro_dps[3],
                           const float magneto_mgauss[3]);

void Telemetry_EvtMode(const char *mode_tag);
void Telemetry_EvtCharge(uint8_t charge_tenths);          /* n/10 after recharge */
void Telemetry_EvtFire(uint8_t charge_after_shot);        /* charge AFTER the shot */
void Telemetry_EvtCountdown(uint32_t remaining_ms);      /* firmware rescue deadline */
void Telemetry_EvtSelfDestruct(const char *reason_token);
void Telemetry_EvtTerminate(void);                       /* immediately before Cheerio! */
void Telemetry_EvtThresholdOk(void);                     /* clear sticky threshold warn */
void Telemetry_EvtRescue(void);                          /* optional; human rescue lines still OK */
void Telemetry_EvtAttack(void);                          /* optional; pairs with attack prose */

/**
 * Radar zombie track (enhancement #5). Angles in tenths of a degree,
 * + = right of boresight. Emits e.g.
 *   EVT RADAR az=-12.4 range_mm=1830 lock=1 pan=-11.9 laser=1 n=2
 */
void Telemetry_EvtRadar(int16_t az_ddeg, uint16_t range_mm, uint8_t locked,
                        int16_t pan_ddeg, uint8_t laser_on, uint8_t contacts);
void Telemetry_EvtRadarLost(void);                       /* EVT RADAR lost */

/**
 * Giant cannon (radar-locked giant, needs 10/10, uses all 10).
 *   EVT CANNON charge=0/10      — main.c, the shot itself (charge AFTER)
 *   EVT GIANT hit=1/2           — adapter, first hit on the locked giant
 *   EVT GIANT down              — adapter, second hit: giant destroyed
 *   EVT GIANT miss              — adapter, lock was lost before the shot landed
 */
void Telemetry_EvtCannon(uint8_t charge_after_shot);
void Telemetry_EvtGiantHit(uint8_t hits, uint8_t hp);
void Telemetry_EvtGiantDown(void);
void Telemetry_EvtGiantMiss(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_EVT_H */
