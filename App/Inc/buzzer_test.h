#ifndef BUZZER_TEST_H
#define BUZZER_TEST_H

#include "buzzer.h"

void Buzzer_Test_Init(UART_HandleTypeDef *huart);
void Buzzer_Test_PrintMenu(void);
uint8_t Buzzer_Test_HandleKey(uint8_t ch, uint32_t now_ms);
void Buzzer_Test_Update(uint32_t now_ms);

#endif /* BUZZER_TEST_H */
