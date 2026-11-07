/**
 * Minimal host stand-in for stm32l4xx_hal.h — just enough for the HAL-light
 * App modules (ht16k33, status_display, buzzer, ir_threat, relay_effect,
 * telemetry_evt, enhancements) to compile and run on a PC.
 *
 * HAL_I2C_Master_Transmit records the last HT16K33 frame so tests can read
 * what the matrix would show. Used by scripts/host/hud_buzzer_host_test.c.
 */
#ifndef HOST_STUB_STM32L4XX_HAL_H
#define HOST_STUB_STM32L4XX_HAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    HAL_OK = 0x00,
    HAL_ERROR = 0x01,
    HAL_BUSY = 0x02,
    HAL_TIMEOUT = 0x03
} HAL_StatusTypeDef;

typedef struct {
    int unused;
} I2C_HandleTypeDef;

typedef struct {
    int unused;
} UART_HandleTypeDef;

/* Implemented by the test: captures transmitted bytes. */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                          uint8_t *data, uint16_t size,
                                          uint32_t timeout);

#endif /* HOST_STUB_STM32L4XX_HAL_H */
