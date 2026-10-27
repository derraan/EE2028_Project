#include "ht16k33.h"

#define HT16K33_CMD_OSC_ON         0x21U
#define HT16K33_CMD_DISPLAY_ON     0x81U
#define HT16K33_CMD_BRIGHTNESS_MIN 0xE0U
#define HT16K33_MAX_BRIGHTNESS     15U
#define HT16K33_MAX_BLINK_RATE     3U

static HAL_StatusTypeDef HT16K33_WriteCommand(HT16K33_HandleTypeDef *hdev,
                                              uint8_t cmd)
{
    if ((hdev == NULL) || (hdev->hi2c == NULL)) {
        return HAL_ERROR;
    }

    return HAL_I2C_Master_Transmit(hdev->hi2c,
                                   hdev->addr,
                                   &cmd,
                                   1U,
                                   HT16K33_I2C_TIMEOUT);
}

HAL_StatusTypeDef HT16K33_SetBrightness(HT16K33_HandleTypeDef *hdev,
                                        uint8_t level)
{
    if (level > HT16K33_MAX_BRIGHTNESS) {
        level = HT16K33_MAX_BRIGHTNESS;
    }

    return HT16K33_WriteCommand(hdev,
                                (uint8_t)(HT16K33_CMD_BRIGHTNESS_MIN | level));
}

HAL_StatusTypeDef HT16K33_SetBlinkRate(HT16K33_HandleTypeDef *hdev,
                                       uint8_t rate)
{
    if (rate > HT16K33_MAX_BLINK_RATE) {
        rate = HT16K33_MAX_BLINK_RATE;
    }

    return HT16K33_WriteCommand(hdev,
                                (uint8_t)(HT16K33_CMD_DISPLAY_ON | (rate << 1)));
}

/* Grove/Adafruit 8x8 packs wire column 0 to COM bit 7 — reverse for logical left. */
static uint8_t HT16K33_ReverseBits(uint8_t value)
{
    value = (uint8_t)(((value & 0xF0U) >> 4) | ((value & 0x0FU) << 4));
    value = (uint8_t)(((value & 0xCCU) >> 2) | ((value & 0x33U) << 2));
    value = (uint8_t)(((value & 0xAAU) >> 1) | ((value & 0x55U) << 1));
    return value;
}

HAL_StatusTypeDef HT16K33_WriteFrame(HT16K33_HandleTypeDef *hdev,
                                     const uint8_t frame[8])
{
    uint8_t tx[17];
    uint8_t row;

    if ((hdev == NULL) || (hdev->hi2c == NULL) || (frame == NULL)) {
        return HAL_ERROR;
    }

    tx[0] = 0x00U;
    for (row = 0U; row < 8U; row++) {
        tx[1U + (row * 2U)] = HT16K33_ReverseBits(frame[row]);
        tx[2U + (row * 2U)] = 0x00U;
    }

    return HAL_I2C_Master_Transmit(hdev->hi2c,
                                   hdev->addr,
                                   tx,
                                   sizeof(tx),
                                   HT16K33_I2C_TIMEOUT);
}

HAL_StatusTypeDef HT16K33_Clear(HT16K33_HandleTypeDef *hdev)
{
    const uint8_t frame[8] = {0U};
    return HT16K33_WriteFrame(hdev, frame);
}

HAL_StatusTypeDef HT16K33_Init(HT16K33_HandleTypeDef *hdev,
                               I2C_HandleTypeDef *hi2c,
                               uint16_t addr)
{
    HAL_StatusTypeDef status;

    if ((hdev == NULL) || (hi2c == NULL)) {
        return HAL_ERROR;
    }

    hdev->hi2c = hi2c;
    hdev->addr = (addr == 0U) ? HT16K33_I2C_ADDR_DEFAULT : addr;

    status = HT16K33_WriteCommand(hdev, HT16K33_CMD_OSC_ON);
    if (status != HAL_OK) {
        return status;
    }

    status = HT16K33_WriteCommand(hdev, HT16K33_CMD_DISPLAY_ON);
    if (status != HAL_OK) {
        return status;
    }

    status = HT16K33_SetBrightness(hdev, HT16K33_MAX_BRIGHTNESS);
    if (status != HAL_OK) {
        return status;
    }

    return HT16K33_Clear(hdev);
}
