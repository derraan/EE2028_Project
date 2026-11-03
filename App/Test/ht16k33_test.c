#include "ht16k33.h"

typedef enum {
    HT16K33_TEST_PIXEL_SWEEP = 0,
    HT16K33_TEST_ROW_SWEEP,
    HT16K33_TEST_COL_SWEEP,
    HT16K33_TEST_COMPLETE
} HT16K33_TestPhase_t;

static HT16K33_HandleTypeDef *s_test_dev = NULL;
static HT16K33_TestPhase_t s_phase = HT16K33_TEST_COMPLETE;
static uint8_t s_index = 0U;
static uint32_t s_last_step_ms = 0U;
static uint8_t s_started = 0U;

static void HT16K33_FrameClear(uint8_t frame[8])
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        frame[i] = 0U;
    }
}

void HT16K33_Test_Init(HT16K33_HandleTypeDef *hdev)
{
    s_test_dev = hdev;
    s_phase = HT16K33_TEST_PIXEL_SWEEP;
    s_index = 0U;
    s_last_step_ms = 0U;
    s_started = 0U;
}

uint8_t HT16K33_Test_Update(uint32_t now_ms)
{
    uint8_t frame[8];
    const uint32_t step_interval_ms = 120U;
    uint8_t row;
    uint8_t col;

    if ((s_test_dev == NULL) || (s_phase == HT16K33_TEST_COMPLETE)) {
        return 1U;
    }

    if (s_started != 0U) {
        if ((now_ms - s_last_step_ms) < step_interval_ms) {
            return 0U;
        }
    } else {
        s_started = 1U;
    }

    s_last_step_ms = now_ms;
    HT16K33_FrameClear(frame);

    if (s_phase == HT16K33_TEST_PIXEL_SWEEP) {
        row = (uint8_t)(s_index / 8U);
        col = (uint8_t)(s_index % 8U);
        frame[row] = (uint8_t)(1U << col);
        (void)HT16K33_WriteFrame(s_test_dev, frame);

        s_index++;
        if (s_index >= 64U) {
            s_phase = HT16K33_TEST_ROW_SWEEP;
            s_index = 0U;
        }
        return 0U;
    }

    if (s_phase == HT16K33_TEST_ROW_SWEEP) {
        frame[s_index] = 0xFFU;
        (void)HT16K33_WriteFrame(s_test_dev, frame);

        s_index++;
        if (s_index >= 8U) {
            s_phase = HT16K33_TEST_COL_SWEEP;
            s_index = 0U;
        }
        return 0U;
    }

    col = s_index;
    for (row = 0U; row < 8U; row++) {
        frame[row] = (uint8_t)(1U << col);
    }
    (void)HT16K33_WriteFrame(s_test_dev, frame);

    s_index++;
    if (s_index >= 8U) {
        (void)HT16K33_Clear(s_test_dev);
        s_phase = HT16K33_TEST_COMPLETE;
        return 1U;
    }

    return 0U;
}
