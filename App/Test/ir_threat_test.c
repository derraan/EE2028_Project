#include "ir_threat_test.h"
#include "buzzer_test.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} IrGpioCtx_t;

static UART_HandleTypeDef *s_huart = NULL;
static IR_HandleTypeDef s_ir;
static IrGpioCtx_t s_ir_ctx;
static uint8_t s_prev_present = 0U;
static uint8_t s_sim_pending = 0U;

static void UartPrint(const char *msg)
{
    if ((s_huart != NULL) && (msg != NULL)) {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
    }
}

static uint8_t IrRaw_Read(void *ctx)
{
    GPIO_PinState pin_state;
    IrGpioCtx_t *gpio = (IrGpioCtx_t *)ctx;

    if (gpio == NULL) {
        return 1U;
    }

    pin_state = HAL_GPIO_ReadPin(gpio->port, gpio->pin);
    return (uint8_t)((pin_state == GPIO_PIN_SET) ? 1U : 0U);
}

static void IrInput_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = ARD_D3_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ARD_D3_GPIO_Port, &gpio);
}

static void PrintStatus(void)
{
    char line[96];
    uint8_t raw = IR_ReadRaw(&s_ir);
    uint8_t present = IR_IsPresent(&s_ir);
    uint8_t armed = s_ir.armed;
    int len;

    len = snprintf(line,
                   sizeof(line),
                   "ir: raw=%u present=%u armed=%u active_level=%u\r\n",
                   raw,
                   present,
                   armed,
                   s_ir.cfg.active_level);
    if (len > 0) {
        UartPrint(line);
    }
}

void IR_Test_Init(UART_HandleTypeDef *huart)
{
    IR_Backend_t backend;
    IR_Config_t cfg;

    s_huart = huart;
    s_ir_ctx.port = ARD_D3_GPIO_Port;
    s_ir_ctx.pin = ARD_D3_Pin;
    s_prev_present = 0U;
    s_sim_pending = 0U;

    IrInput_Init();

    backend.read_raw = IrRaw_Read;
    backend.ctx = &s_ir_ctx;

    cfg.active_level = 0U;
    cfg.debounce_ms = 30U;
    cfg.rearm_ms = 300U;
    IR_Init(&s_ir, &backend, &cfg);
    s_prev_present = IR_IsPresent(&s_ir);
}

void IR_Test_PrintMenu(void)
{
    UartPrint("--- IR Threat ---\r\n");
    UartPrint("i : print IR status\r\n");
    UartPrint("I : simulate IR threat event\r\n");
}

uint8_t IR_Test_HandleKey(uint8_t ch, uint32_t now_ms)
{
    (void)now_ms;

    switch (ch) {
        case 'i':
            PrintStatus();
            return 1U;
        case 'I':
            s_sim_pending = 1U;
            UartPrint("ir: simulated threat queued\r\n");
            return 1U;
        default:
            return 0U;
    }
}

void IR_Test_Update(uint32_t now_ms)
{
    uint8_t present;

    IR_Update(&s_ir, now_ms);

    present = IR_IsPresent(&s_ir);
    if (present != s_prev_present) {
        if (present != 0U) {
            UartPrint("EVT IR=1\r\n");
        } else {
            UartPrint("EVT IR=0\r\n");
        }
        s_prev_present = present;
    }

    if ((IR_PollThreat(&s_ir) != 0U) || (s_sim_pending != 0U)) {
        s_sim_pending = 0U;
        UartPrint("EVT THREAT source=IR\r\n");
        (void)Buzzer_Test_HandleKey((uint8_t)'c', now_ms);
    }
}
