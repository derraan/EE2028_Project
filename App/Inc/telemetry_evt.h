/**
 * @file telemetry_evt.h
 * @brief Machine-readable EVT lines + PR #3 sensor telemetry over USART/VCP.
 *
 * Human-readable prints stay as-is. EVT lines are one-per-line and share the
 * same UART stream so Tera Term and VESPER both consume them.
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
 * Low-level line writer. Default weak impl uses printf (semihosting / retarget).
 * Override with USART TX to ST-LINK Virtual COM @ 115200 8N1 when Cube is wired.
 */
void Telemetry_WriteLine(const char *line);

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
