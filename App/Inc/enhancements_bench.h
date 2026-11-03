#ifndef ENHANCEMENTS_BENCH_H
#define ENHANCEMENTS_BENCH_H

#include "stm32l4xx_hal.h"

void Enhancements_Bench_Init(UART_HandleTypeDef *huart, I2C_HandleTypeDef *hi2c);
void Enhancements_Bench_Update(uint32_t now_ms);

#endif /* ENHANCEMENTS_BENCH_H */
