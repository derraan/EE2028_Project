/**
 * @file enhancements_hw.c
 * @brief GPIO / I2C backends for the shared enhancements adapter.
 */
#include "enhancements_hw.h"
#include <string.h>

/* Pin aliases — B-L4S5I-IOT01A Arduino D2/D3/D4; override via Cube main.h. */
#ifndef ARD_D2_Pin
#define ARD_D2_Pin        GPIO_PIN_14
#define ARD_D2_GPIO_Port  GPIOD
#endif
#ifndef ARD_D3_Pin
#define ARD_D3_Pin        GPIO_PIN_0
#define ARD_D3_GPIO_Port  GPIOB
#endif
#ifndef ARD_D4_Pin
#define ARD_D4_Pin        GPIO_PIN_3
#define ARD_D4_GPIO_Port  GPIOA
#endif

#ifndef BUZZER_Pin
#define BUZZER_Pin        ARD_D2_Pin
#define BUZZER_GPIO_Port  ARD_D2_GPIO_Port
#endif
#ifndef IR_Pin
#define IR_Pin            ARD_D3_Pin
#define IR_GPIO_Port      ARD_D3_GPIO_Port
#endif
#ifndef CAP_EXPLODE_Pin
#define CAP_EXPLODE_Pin       ARD_D4_Pin
#define CAP_EXPLODE_GPIO_Port ARD_D4_GPIO_Port
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} GpioPin_t;

static HT16K33_HandleTypeDef s_matrix;
static uint8_t s_matrix_ready;
static GpioPin_t s_buzzer_pin;
static GpioPin_t s_ir_pin;
static GpioPin_t s_relay_pin;
static Buzzer_Backend_t s_buzzer_backend;
static IR_Backend_t s_ir_backend;
static IR_Config_t s_ir_cfg;
static Relay_Backend_t s_relay_backend;
static Relay_Config_t s_relay_cfg;

static void Buzzer_SetTone(void *ctx, uint8_t on, uint16_t freq_hz)
{
    GpioPin_t *p = (GpioPin_t *)ctx;
    (void)freq_hz;
    if ((p == NULL) || (p->port == NULL)) {
        return;
    }
    HAL_GPIO_WritePin(p->port, p->pin, (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t IR_ReadRaw(void *ctx)
{
    GpioPin_t *p = (GpioPin_t *)ctx;
    if ((p == NULL) || (p->port == NULL)) {
        return 1U; /* idle / not present for active-LOW sensor */
    }
    return (uint8_t)((HAL_GPIO_ReadPin(p->port, p->pin) == GPIO_PIN_SET) ? 1U : 0U);
}

static void Relay_SetOutput(void *ctx, uint8_t high)
{
    GpioPin_t *p = (GpioPin_t *)ctx;
    if ((p == NULL) || (p->port == NULL)) {
        return;
    }
    HAL_GPIO_WritePin(p->port, p->pin,
                      (high != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void InitOutputLow(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio;

    if (port == NULL) {
        return;
    }
    /* Clock enable is board-specific; Cube MX_GPIO_Init normally enables ports. */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

static void InitInputPullUp(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio;

    if (port == NULL) {
        return;
    }
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(port, &gpio);
}

int Enhancements_HW_Setup(Enhancements_Config_t *cfg, I2C_HandleTypeDef *hi2c)
{
    if (cfg == NULL) {
        return -1;
    }

    (void)memset(cfg, 0, sizeof(*cfg));
    s_matrix_ready = 0U;

    s_buzzer_pin.port = BUZZER_GPIO_Port;
    s_buzzer_pin.pin = BUZZER_Pin;
    InitOutputLow(s_buzzer_pin.port, s_buzzer_pin.pin);
    s_buzzer_backend.set_tone = Buzzer_SetTone;
    s_buzzer_backend.ctx = &s_buzzer_pin;

    s_ir_pin.port = IR_GPIO_Port;
    s_ir_pin.pin = IR_Pin;
    InitInputPullUp(s_ir_pin.port, s_ir_pin.pin);
    s_ir_backend.read_raw = IR_ReadRaw;
    s_ir_backend.ctx = &s_ir_pin;
    s_ir_cfg.active_level = 0U;
    s_ir_cfg.debounce_ms = 30U;
    s_ir_cfg.rearm_ms = 200U;

    s_relay_pin.port = CAP_EXPLODE_GPIO_Port;
    s_relay_pin.pin = CAP_EXPLODE_Pin;
    InitOutputLow(s_relay_pin.port, s_relay_pin.pin);
    s_relay_backend.set_output = Relay_SetOutput;
    s_relay_backend.ctx = &s_relay_pin;
    s_relay_cfg.pulse_ms = 0U; /* latched HIGH at boom */

    cfg->buzzer = &s_buzzer_backend;
    cfg->ir = &s_ir_backend;
    cfg->ir_cfg = &s_ir_cfg;
    cfg->relay = &s_relay_backend;
    cfg->relay_cfg = &s_relay_cfg;

    if (hi2c != NULL) {
        if (HT16K33_Init(&s_matrix, hi2c, HT16K33_I2C_ADDR_DEFAULT) == HAL_OK) {
            s_matrix_ready = 1U;
            cfg->matrix = &s_matrix;
        } else {
            cfg->matrix = NULL; /* display fault: continue without matrix */
        }
    } else {
        cfg->matrix = NULL;
    }

    return 0;
}

HT16K33_HandleTypeDef *Enhancements_HW_Matrix(void)
{
    return (s_matrix_ready != 0U) ? &s_matrix : NULL;
}
