#include "buzzer_test.h"
#include "main.h"
#include <string.h>

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} GpioToneCtx_t;

static UART_HandleTypeDef *s_huart = NULL;
static Buzzer_HandleTypeDef s_buzzer;
static GpioToneCtx_t s_tone_ctx;
static uint8_t s_manual_tone_active = 0U;
static uint32_t s_manual_tone_until_ms = 0U;
static uint8_t s_auto_countdown = 0U;
static uint8_t s_auto_countdown_val = 10U;
static uint32_t s_auto_step_ms = 0U;

static void UartPrint(const char *msg)
{
    if ((s_huart != NULL) && (msg != NULL)) {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
    }
}

static void ToneBackend_Set(void *ctx, uint8_t on, uint16_t freq_hz)
{
    GpioToneCtx_t *gpio = (GpioToneCtx_t *)ctx;
    GPIO_PinState state = (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    (void)freq_hz;

    if (gpio == NULL) {
        return;
    }
    HAL_GPIO_WritePin(gpio->port, gpio->pin, state);
}

static void BuzzerGpio_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(ARD_D2_GPIO_Port, ARD_D2_Pin, GPIO_PIN_RESET);

    gpio.Pin = ARD_D2_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ARD_D2_GPIO_Port, &gpio);
}

static void ManualTone_Start(uint32_t now_ms)
{
    s_manual_tone_active = 1U;
    s_manual_tone_until_ms = now_ms + 1000U;
    Buzzer_Stop(&s_buzzer);
    ToneBackend_Set(&s_tone_ctx, 1U, 0U);
}

void Buzzer_Test_Init(UART_HandleTypeDef *huart)
{
    Buzzer_Backend_t backend;

    s_huart = huart;
    s_tone_ctx.port = ARD_D2_GPIO_Port;
    s_tone_ctx.pin = ARD_D2_Pin;
    s_manual_tone_active = 0U;
    s_auto_countdown = 0U;
    s_auto_countdown_val = 10U;

    BuzzerGpio_Init();

    backend.set_tone = ToneBackend_Set;
    backend.ctx = &s_tone_ctx;
    Buzzer_Init(&s_buzzer, &backend);
}

void Buzzer_Test_PrintMenu(void)
{
    UartPrint("t : tone probe (ON for 1s)\r\n");
    UartPrint("c : threat chirp\r\n");
    UartPrint("f : laser fire tone\r\n");
    UartPrint("w : LAST_OF_2028 warning (2Hz)\r\n");
    UartPrint("k : countdown beeps 10..0\r\n");
    UartPrint("x : final burst + latched silence\r\n");
    UartPrint("z : stop buzzer / rescue\r\n");
}

uint8_t Buzzer_Test_HandleKey(uint8_t ch, uint32_t now_ms)
{
    switch (ch) {
        case 't':
        case 'T':
            ManualTone_Start(now_ms);
            UartPrint("buzzer: tone probe 1s\r\n");
            return 1U;
        case 'c':
        case 'C':
            s_auto_countdown = 0U;
            Buzzer_Play(&s_buzzer, BUZZER_PATTERN_THREAT_CHIRP);
            UartPrint("buzzer: threat chirp\r\n");
            return 1U;
        case 'f':
        case 'F':
            s_auto_countdown = 0U;
            Buzzer_Play(&s_buzzer, BUZZER_PATTERN_LASER_FIRE);
            UartPrint("buzzer: laser fire\r\n");
            return 1U;
        case 'w':
        case 'W':
            s_auto_countdown = 0U;
            Buzzer_Play(&s_buzzer, BUZZER_PATTERN_LAST_WARNING);
            UartPrint("buzzer: warning 2Hz\r\n");
            return 1U;
        case 'k':
        case 'K':
            s_auto_countdown = 1U;
            s_auto_countdown_val = 10U;
            s_auto_step_ms = now_ms;
            Buzzer_SetCountdown(&s_buzzer, s_auto_countdown_val);
            Buzzer_Play(&s_buzzer, BUZZER_PATTERN_COUNTDOWN);
            UartPrint("buzzer: countdown 10..0\r\n");
            return 1U;
        case 'x':
        case 'X':
            s_auto_countdown = 0U;
            Buzzer_Play(&s_buzzer, BUZZER_PATTERN_FINAL_BURST);
            UartPrint("buzzer: final burst\r\n");
            return 1U;
        case 'z':
        case 'Z':
            s_auto_countdown = 0U;
            Buzzer_Stop(&s_buzzer);
            UartPrint("buzzer: stop\r\n");
            return 1U;
        default:
            return 0U;
    }
}

void Buzzer_Test_Update(uint32_t now_ms)
{
    if (s_manual_tone_active != 0U) {
        if ((int32_t)(now_ms - s_manual_tone_until_ms) >= 0) {
            s_manual_tone_active = 0U;
            ToneBackend_Set(&s_tone_ctx, 0U, 0U);
        }
        return;
    }

    if (s_auto_countdown != 0U) {
        if ((now_ms - s_auto_step_ms) >= 1000U) {
            s_auto_step_ms = now_ms;
            if (s_auto_countdown_val > 0U) {
                s_auto_countdown_val--;
                Buzzer_SetCountdown(&s_buzzer, s_auto_countdown_val);
            } else {
                s_auto_countdown = 0U;
            }
        }
    }

    Buzzer_Update(&s_buzzer, now_ms);
}
