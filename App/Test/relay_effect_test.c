#include "relay_effect_test.h"
#include "main.h"
#include <string.h>

/*
 * Simulated capacitor blow-up: CAP_EXPLODE on Arduino D4 (PA3 on B-L4S5I-IOT01A).
 * Drives a relay-module control input HIGH at explosion; stays LOW otherwise.
 * D2 = buzzer (PD14), D3 = IR (PB0) — D4 is the next free Grove/Arduino digital.
 */
#ifndef CAP_EXPLODE_Pin
#define CAP_EXPLODE_Pin       ARD_D4_Pin
#define CAP_EXPLODE_GPIO_Port ARD_D4_GPIO_Port
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} RelayGpioCtx_t;

static UART_HandleTypeDef *s_huart = NULL;
static Relay_HandleTypeDef s_relay;
static RelayGpioCtx_t s_gpio_ctx;

static void UartPrint(const char *msg)
{
    if ((s_huart != NULL) && (msg != NULL)) {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
    }
}

static void RelayBackend_Set(void *ctx, uint8_t high)
{
    RelayGpioCtx_t *gpio = (RelayGpioCtx_t *)ctx;
    GPIO_PinState state = (high != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

    if (gpio == NULL) {
        return;
    }
    HAL_GPIO_WritePin(gpio->port, gpio->pin, state);
}

static void CapExplodeGpio_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Drive LOW before switching to output to avoid a HIGH glitch */
    HAL_GPIO_WritePin(CAP_EXPLODE_GPIO_Port, CAP_EXPLODE_Pin, GPIO_PIN_RESET);

    gpio.Pin = CAP_EXPLODE_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAP_EXPLODE_GPIO_Port, &gpio);
}

void Relay_Test_Init(UART_HandleTypeDef *huart)
{
    Relay_Backend_t backend;
    Relay_Config_t cfg;

    s_huart = huart;
    s_gpio_ctx.port = CAP_EXPLODE_GPIO_Port;
    s_gpio_ctx.pin = CAP_EXPLODE_Pin;

    CapExplodeGpio_Init();

    backend.set_output = RelayBackend_Set;
    backend.ctx = &s_gpio_ctx;

    /* Latch HIGH until Reset — matches "set CAP_EXPLODE_Pin HIGH" at boom */
    cfg.pulse_ms = 0U;
    RelayEffect_Init(&s_relay, &backend, &cfg);
}

void Relay_Test_PrintMenu(void)
{
    UartPrint("--- Cap explode (relay D4) ---\r\n");
    UartPrint("e : trigger CAP_EXPLODE HIGH (latched)\r\n");
    UartPrint("E : reset CAP_EXPLODE LOW\r\n");
}

uint8_t Relay_Test_HandleKey(uint8_t ch, uint32_t now_ms)
{
    switch (ch) {
        case 'e':
            RelayEffect_Trigger(&s_relay, now_ms);
            UartPrint("relay: CAP_EXPLODE HIGH\r\n");
            return 1U;
        case 'E':
            RelayEffect_Reset(&s_relay);
            UartPrint("relay: CAP_EXPLODE LOW\r\n");
            return 1U;
        default:
            return 0U;
    }
}

void Relay_Test_Update(uint32_t now_ms)
{
    RelayEffect_Update(&s_relay, now_ms);
}

void Relay_Test_OnExplosion(uint32_t now_ms)
{
    RelayEffect_Trigger(&s_relay, now_ms);
    UartPrint("EVT CAP_EXPLODE=1\r\n");
}

void Relay_Test_OnRescue(void)
{
    RelayEffect_Reset(&s_relay);
    UartPrint("EVT CAP_EXPLODE=0\r\n");
}
