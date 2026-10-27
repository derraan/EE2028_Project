#ifndef HT16K33_H
#define HT16K33_H

#include "stm32l4xx_hal.h"

#define HT16K33_I2C_ADDR_DEFAULT  (0x70U << 1)
#define HT16K33_I2C_TIMEOUT       100U

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t addr;
} HT16K33_HandleTypeDef;

HAL_StatusTypeDef HT16K33_Init(HT16K33_HandleTypeDef *hdev,
                               I2C_HandleTypeDef *hi2c,
                               uint16_t addr);
HAL_StatusTypeDef HT16K33_SetBrightness(HT16K33_HandleTypeDef *hdev,
                                        uint8_t level);
HAL_StatusTypeDef HT16K33_SetBlinkRate(HT16K33_HandleTypeDef *hdev,
                                       uint8_t rate);
HAL_StatusTypeDef HT16K33_Clear(HT16K33_HandleTypeDef *hdev);
HAL_StatusTypeDef HT16K33_WriteFrame(HT16K33_HandleTypeDef *hdev,
                                     const uint8_t frame[8]);

void HT16K33_Test_Init(HT16K33_HandleTypeDef *hdev);
uint8_t HT16K33_Test_Update(uint32_t now_ms);

#endif /* HT16K33_H */
