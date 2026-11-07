#ifndef BUZZER_H
#define BUZZER_H

#include "stm32l4xx_hal.h"

/*
 * Enum order is not priority order: see Buzzer_PatternPriority() in buzzer.c.
 * New cues are appended so existing values stay stable for the UART harness.
 */
typedef enum {
    BUZZER_PATTERN_SILENT = 0,
    BUZZER_PATTERN_THREAT_CHIRP,   /* one-shot: IR / radar lock double tap   */
    BUZZER_PATTERN_LASER_FIRE,     /* one-shot: "pew" per shot               */
    BUZZER_PATTERN_LAST_WARNING,   /* loop: 2 Hz (harness; live uses COUNTDOWN) */
    BUZZER_PATTERN_COUNTDOWN,      /* loop: gap shrinks as value → 0         */
    BUZZER_PATTERN_FINAL_BURST,    /* one-shot: then latched silent          */
    BUZZER_PATTERN_BATTLE_ENTRY,   /* one-shot: accelerating triple beep     */
    BUZZER_PATTERN_RECHARGE,       /* one-shot: short blip per +3 charge     */
    BUZZER_PATTERN_DENIED,         /* one-shot: rough "brrr" — fire refused  */
    BUZZER_PATTERN_RESCUE_OK,      /* one-shot: two blips + long "saved"     */
    BUZZER_PATTERN_RESCUE_FAIL,    /* one-shot: flat error tone, still upside down */
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
