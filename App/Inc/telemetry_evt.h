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
 * Low-level line writer (appends \\r\\n). Default weak impl uses printf
 * (semihosting) so mission lines still appear in CubeIDE until a strong
 * USART override is linked — see App/Src/telemetry_uart_hal.c.example.
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

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_EVT_H */
