#ifndef RELAY_EFFECT_H
#define RELAY_EFFECT_H

#include "stm32l4xx_hal.h"

typedef struct {
    void (*set_output)(void *ctx, uint8_t high);
    void *ctx;
} Relay_Backend_t;

typedef struct {
    /* 0 = latch HIGH until Reset; >0 = pulse then auto-LOW */
    uint16_t pulse_ms;
} Relay_Config_t;

typedef struct {
    Relay_Backend_t backend;
    Relay_Config_t cfg;
    uint8_t active;
    uint8_t fired;          /* one-shot guard: Trigger once until Reset */
    uint32_t active_since_ms;
} Relay_HandleTypeDef;

void RelayEffect_Init(Relay_HandleTypeDef *h,
                      const Relay_Backend_t *backend,
                      const Relay_Config_t *cfg);
void RelayEffect_Trigger(Relay_HandleTypeDef *h, uint32_t now_ms);
void RelayEffect_Reset(Relay_HandleTypeDef *h);
void RelayEffect_Update(Relay_HandleTypeDef *h, uint32_t now_ms);
uint8_t RelayEffect_IsActive(const Relay_HandleTypeDef *h);

#endif /* RELAY_EFFECT_H */
