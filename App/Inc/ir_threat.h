#ifndef IR_THREAT_H
#define IR_THREAT_H

#include "stm32l4xx_hal.h"

typedef struct {
    uint8_t (*read_raw)(void *ctx);
    void *ctx;
} IR_Backend_t;

typedef struct {
    uint8_t active_level;
    uint16_t debounce_ms;
    uint16_t rearm_ms;
} IR_Config_t;

typedef struct {
    IR_Backend_t backend;
    IR_Config_t cfg;

    uint8_t present;
    uint8_t armed;
    uint8_t last_raw;
    uint8_t event_pending;

    uint32_t raw_since_ms;
    uint32_t clear_since_ms;
} IR_HandleTypeDef;

void IR_Init(IR_HandleTypeDef *h, const IR_Backend_t *backend, const IR_Config_t *cfg);
void IR_Reset(IR_HandleTypeDef *h);
void IR_Update(IR_HandleTypeDef *h, uint32_t now_ms);
uint8_t IR_PollThreat(IR_HandleTypeDef *h);
uint8_t IR_IsPresent(const IR_HandleTypeDef *h);
uint8_t IR_ReadRaw(const IR_HandleTypeDef *h);

#endif /* IR_THREAT_H */
