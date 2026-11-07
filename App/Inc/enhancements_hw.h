/**
 * @file enhancements_hw.h
 * @brief Board backends for matrix / buzzer / IR / relay (Cube pin aliases).
 *
 * Board of record B-L4S5I-IOT01A Arduino pins (lab Cube main.h / .ioc):
 *   Matrix HT16K33  — I2C1 Arduino D15/D14 (PB8 SCL / PB9 SDA), addr 0x70,
 *                     brought up by Enhancements_HW_MatrixI2C()
 *   Buzzer          — D2  (ARD_D2 / PD14)
 *   IR reflective   — D3  (ARD_D3 / PB0), active-LOW
 *   CAP_EXPLODE     — D4  (ARD_D4 / PA3)
 *   RD-03D radar    — D0/D1 (PA1 UART4_RX ← radar TX, PA0 UART4_TX → radar RX), 256000 8N1
 *   Pan servo PWM   — D5  (ARD_D5 / PB4, TIM3_CH1 AF2), 50 Hz
 *   Laser module    — D7  (ARD_D7 / PA4) via NPN/MOSFET low-side switch, active-HIGH
 *   (radar + servo + laser only with -DEE_RADAR_AIM=1)
 *
 * Call after Cube/HAL GPIO + I2C init. Safe if handles/pins are unavailable:
 * backends become no-ops and the adapter still runs.
 */
#ifndef ENHANCEMENTS_HW_H
#define ENHANCEMENTS_HW_H

#include "enhancements.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise GPIO backends and fill cfg with injectable backends.
 * hi2c may be NULL to skip matrix bring-up.
 * Returns 0 on success (cfg ready); non-zero if cfg is NULL.
 */
int Enhancements_HW_Setup(Enhancements_Config_t *cfg, I2C_HandleTypeDef *hi2c);

#if defined(EE_RADAR_AIM) && (EE_RADAR_AIM != 0)
/**
 * Bring up UART4 (RD-03D), TIM3_CH1 PWM (pan servo) and the laser GPIO, and
 * add their backends to cfg (call after Enhancements_HW_Setup, before
 * Enhancements_Init). Configures the peripherals itself because the lab
 * project uses BSP init only (no MX_* calls). Laser is driven LOW first.
 * Returns 0 on success; non-zero bit mask of failed parts (1 UART, 2 TIM).
 */
int Enhancements_HW_SetupRadarAim(Enhancements_Config_t *cfg);

/**
 * Boot bench check for the pan servo on D5 (no radar needed): blocking ~3.5 s
 * sweep centre → left → centre → right → centre (±45°). The laser is held OFF
 * throughout. Call after Enhancements_HW_SetupRadarAim, before Enhancements_Init.
 */
void Enhancements_HW_ServoSelfTest(void);

/** UART4 RX interrupt body — call it yourself if EE_RADAR_OWN_IRQ=0. */
void Enhancements_HW_RadarIrq(void);

/** RX diagnostics: bytes received, bytes dropped (ring full), UART errors. */
void Enhancements_HW_RadarStats(uint32_t *rx, uint32_t *dropped, uint32_t *errors);
#endif

/** Matrix device handle used when I2C was provided (else NULL). */
HT16K33_HandleTypeDef *Enhancements_HW_Matrix(void);

/**
 * Bring up I2C1 on Arduino D15/D14 (PB8 SCL / PB9 SDA) for the HT16K33 and
 * return its handle; NULL if HAL_I2C_Init fails. Idempotent. Default source
 * of EE_MATRIX_I2C in main.c — pass the result to Enhancements_HW_Setup.
 * A missing / unpowered matrix simply NACKs and the adapter runs without it.
 */
I2C_HandleTypeDef *Enhancements_HW_MatrixI2C(void);

#ifdef __cplusplus
}
#endif

#endif /* ENHANCEMENTS_HW_H */
