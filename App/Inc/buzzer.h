#ifndef BUZZER_H
#define BUZZER_H

#include "stm32l4xx_hal.h"

typedef enum {
    BUZZER_PATTERN_SILENT = 0,
    BUZZER_PATTERN_THREAT_CHIRP,
    BUZZER_PATTERN_LASER_FIRE,
    BUZZER_PATTERN_LAST_WARNING,
    BUZZER_PATTERN_COUNTDOWN,
    BUZZER_PATTERN_FINAL_BURST,
    BUZZER_PATTERN_COUNT
} BuzzerPattern_t;

typedef struct {
    uint8_t on;
    uint16_t freq_hz;
    uint16_t duration_ms;
} BuzzerStep_t;

typedef struct {
    void (*set_tone)(void *ctx, uint8_t on, uint16_t freq_hz);
    void *ctx;
} Buzzer_Backend_t;

typedef struct {
    Buzzer_Backend_t backend;
    BuzzerPattern_t active_pattern;
    BuzzerPattern_t base_pattern;
    uint8_t step_index;
    uint8_t latched_silent;
    uint8_t output_on;
    uint16_t output_freq_hz;
    uint8_t countdown_value;
    uint8_t dirty;
    uint32_t step_start_ms;
} Buzzer_HandleTypeDef;

void Buzzer_Init(Buzzer_HandleTypeDef *h, const Buzzer_Backend_t *backend);
void Buzzer_Play(Buzzer_HandleTypeDef *h, BuzzerPattern_t pattern);
void Buzzer_SetCountdown(Buzzer_HandleTypeDef *h, uint8_t value_0_10);
void Buzzer_Stop(Buzzer_HandleTypeDef *h);
void Buzzer_Reset(Buzzer_HandleTypeDef *h);
void Buzzer_Update(Buzzer_HandleTypeDef *h, uint32_t now_ms);

#endif /* BUZZER_H */
