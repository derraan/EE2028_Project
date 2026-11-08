/**
 * @file telemetry_uart.c
 * @brief Mission telemetry → ST-LINK Virtual COM port (USART1, PB6/PB7) @ 115200 8N1.
 *
 * Every Telemetry_* line (mission prose, sensor lines, EVT tags) is sent here,
 * so Tera Term and VESPER (Chrome/Edge Web Serial) see the same stream.
 * This strong Telemetry_WriteLine replaces the weak printf default in
 * telemetry_evt.c. Call Telemetry_UartInit() once, right after HAL_Init().
 */
#include "telemetry_evt.h"
#include "stm32l4xx_hal.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01.h"

#include <string.h>

static UART_HandleTypeDef s_vcp;
static uint8_t s_ready;

void Telemetry_UartInit(void)
{
	(void)memset(&s_vcp, 0, sizeof(s_vcp));
	s_vcp.Init.BaudRate = 115200U;
	s_vcp.Init.WordLength = UART_WORDLENGTH_8B;
	s_vcp.Init.StopBits = UART_STOPBITS_1;
	s_vcp.Init.Parity = UART_PARITY_NONE;
	s_vcp.Init.Mode = UART_MODE_TX_RX;
	s_vcp.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	s_vcp.Init.OverSampling = UART_OVERSAMPLING_16;
	s_vcp.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	s_vcp.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

	/* Board BSP: clocks + PB6 (TX) / PB7 (RX) AF7 + HAL_UART_Init on USART1. */
	BSP_COM_Init(COM1, &s_vcp);
	s_ready = (uint8_t)((s_vcp.gState == HAL_UART_STATE_READY) ? 1U : 0U);
}

void Telemetry_WriteLine(const char *line)
{
	static const uint8_t crlf[2] = {'\r', '\n'};

	if (line == NULL) {
		return;
	}
	/* Same line to the Wi-Fi TCP client, if one is connected (queued, non-blocking). */
	Telemetry_WifiEnqueue(line);
	if (s_ready == 0U) {
		return;
	}
	/* Blocking is fine: ~9 ms for a 100-byte line at 115200, a few lines/s. */
	(void)HAL_UART_Transmit(&s_vcp, (uint8_t *)line, (uint16_t)strlen(line), 100U);
	(void)HAL_UART_Transmit(&s_vcp, (uint8_t *)crlf, 2U, 10U);
}
