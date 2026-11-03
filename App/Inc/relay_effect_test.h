#ifndef RELAY_EFFECT_TEST_H
#define RELAY_EFFECT_TEST_H

#include "relay_effect.h"

void Relay_Test_Init(UART_HandleTypeDef *huart);
void Relay_Test_PrintMenu(void);
uint8_t Relay_Test_HandleKey(uint8_t ch, uint32_t now_ms);
void Relay_Test_Update(uint32_t now_ms);
void Relay_Test_OnExplosion(uint32_t now_ms);
void Relay_Test_OnRescue(void);

#endif /* RELAY_EFFECT_TEST_H */
