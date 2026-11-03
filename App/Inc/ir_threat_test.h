#ifndef IR_THREAT_TEST_H
#define IR_THREAT_TEST_H

#include "ir_threat.h"

void IR_Test_Init(UART_HandleTypeDef *huart);
void IR_Test_PrintMenu(void);
uint8_t IR_Test_HandleKey(uint8_t ch, uint32_t now_ms);
void IR_Test_Update(uint32_t now_ms);

#endif /* IR_THREAT_TEST_H */
