/**
 * @file enhancements_hw.h
 * @brief Board backends for matrix / buzzer / IR / relay (Cube pin aliases).
 *
 * Board of record B-L4S5I-IOT01A Arduino pins (lab Cube main.h / .ioc):
 *   Matrix HT16K33  — I2C (inject handle; typically I2C1 Arduino D14/D15 or free bus)
 *   Buzzer          — D2  (ARD_D2 / PD14)
 *   IR reflective   — D3  (ARD_D3 / PB0), active-LOW
 *   CAP_EXPLODE     — D4  (ARD_D4 / PA3)
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

/** Matrix device handle used when I2C was provided (else NULL). */
HT16K33_HandleTypeDef *Enhancements_HW_Matrix(void);

#ifdef __cplusplus
}
#endif

#endif /* ENHANCEMENTS_HW_H */
