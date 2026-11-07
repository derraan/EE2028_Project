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

static uint8_t IR_ReadPin(void *ctx)
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

/*
 * The lab project has no CubeMX MX_GPIO_Init, so nothing else turns these port
 * clocks on (GPIOD in particular: without this the buzzer on PD14 stays silent
 * because writes to an unclocked port are ignored). Enabling twice is harmless.
 */
static void GpioClockEnable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    } else {
        /* other ports unused by the enhancements */
    }
}

static void InitOutputLow(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio;

    if (port == NULL) {
        return;
    }
    GpioClockEnable(port);
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
    GpioClockEnable(port);
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
    s_ir_backend.read_raw = IR_ReadPin;
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

/* ----------------------------------------------------------------------
 * Matrix I2C bus: I2C1 on Arduino D15 (PB8, SCL) / D14 (PB9, SDA), AF4 —
 * the bus the HT16K33 was hardware-verified on. Self-contained like the radar
 * UART because the lab project has no MX_I2C1_Init. The on-board sensors stay
 * on the BSP's I2C2 (PB10/PB11); nothing here touches that bus.
 * ---------------------------------------------------------------------- */
static I2C_HandleTypeDef s_matrix_i2c;

/* Standard-mode (~100 kHz) TIMINGR for the given I2C kernel clock. Computed
 * because the lab firmware runs on the 4 MHz MSI reset clock, where the
 * CubeMX value (0x30A175AB, made for a PLL clock) would be far too slow. */
static uint32_t MatrixI2C_Timing(uint32_t i2c_clk_hz)
{
    uint32_t presc;
    uint32_t f;
    uint32_t scll;
    uint32_t sclh;
    uint32_t scldel;
    uint32_t sdadel;

    presc = (i2c_clk_hz + 3999999U) / 4000000U; /* aim for ≤4 MHz tick */
    presc = (presc == 0U) ? 0U : (presc - 1U);
    if (presc > 15U) {
        presc = 15U;
    }
    f = i2c_clk_hz / (presc + 1U);

    scll = (f / 200000U);            /* 5.0 µs low  (min 4.7) */
    sclh = (f * 21U) / 5000000U;     /* 4.2 µs high (min 4.0) */
    scldel = f / 1000000U;           /* 1.0 µs data setup     */
    sdadel = f / 2000000U;           /* 0.5 µs data hold      */

    scll = (scll == 0U) ? 0U : (scll - 1U);
    sclh = (sclh == 0U) ? 0U : (sclh - 1U);
    scldel = (scldel == 0U) ? 0U : (scldel - 1U);
    if (scll > 255U) { scll = 255U; }
    if (sclh > 255U) { sclh = 255U; }
    if (scldel > 15U) { scldel = 15U; }
    if (sdadel > 15U) { sdadel = 15U; }

    return (presc << 28) | (scldel << 20) | (sdadel << 16) | (sclh << 8) | scll;
}

I2C_HandleTypeDef *Enhancements_HW_MatrixI2C(void)
{
    GPIO_InitTypeDef gpio;

    if (s_matrix_i2c.Instance != NULL) {
        return &s_matrix_i2c; /* already up */
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_PCLK1);
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9; /* PB8 = SCL (D15), PB9 = SDA (D14) */
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;            /* Grove board has its own pull-ups too */
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    (void)memset(&s_matrix_i2c, 0, sizeof(s_matrix_i2c));
    s_matrix_i2c.Instance = I2C1;
    s_matrix_i2c.Init.Timing = MatrixI2C_Timing(HAL_RCC_GetPCLK1Freq());
    s_matrix_i2c.Init.OwnAddress1 = 0U;
    s_matrix_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_matrix_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_matrix_i2c.Init.OwnAddress2 = 0U;
    s_matrix_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_matrix_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_matrix_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&s_matrix_i2c) != HAL_OK) {
        s_matrix_i2c.Instance = NULL;
        return NULL;
    }
    (void)HAL_I2CEx_ConfigAnalogFilter(&s_matrix_i2c, I2C_ANALOGFILTER_ENABLE);
    (void)HAL_I2CEx_ConfigDigitalFilter(&s_matrix_i2c, 0U);
    return &s_matrix_i2c;
}

/* ======================================================================
 * Enhancement #5 — RD-03D radar (UART4) + pan servo (TIM3_CH1) + laser.
 * Self-contained peripheral bring-up: the lab project initialises the board
 * through BSP calls only, so there are no MX_UART4 / MX_TIM3 handles.
 * ====================================================================== */
#if defined(EE_RADAR_AIM) && (EE_RADAR_AIM != 0)

/* Geometry / calibration knobs — override with -D or in Cube main.h. */
#ifndef EE_RADAR_MIRROR_X
#define EE_RADAR_MIRROR_X      1      /* -1 if "walk right" gives negative azimuth */
#endif
#ifndef EE_RADAR_FOV_HALF_DEG
#define EE_RADAR_FOV_HALF_DEG  60.0f
#endif
#ifndef EE_RADAR_MAX_RANGE_MM
#define EE_RADAR_MAX_RANGE_MM  6000U
#endif
#ifndef EE_SERVO_CENTER_US
#define EE_SERVO_CENTER_US     1500U  /* trim so the laser hits radar boresight */
#endif
#ifndef EE_SERVO_DIRECTION
#define EE_SERVO_DIRECTION     1      /* -1 if +azimuth swings the horn left */
#endif
#ifndef EE_SERVO_PAN_LIMIT_DEG
#define EE_SERVO_PAN_LIMIT_DEG 70.0f
#endif
#ifndef EE_LASER_OFFSET_X_MM
#define EE_LASER_OFFSET_X_MM   0.0f   /* laser pivot right of the radar centre */
#endif
#ifndef EE_LASER_OFFSET_Y_MM
#define EE_LASER_OFFSET_Y_MM   0.0f   /* laser pivot ahead of the radar face */
#endif
#ifndef EE_LASER_ACTIVE_HIGH
#define EE_LASER_ACTIVE_HIGH   1
#endif

#ifndef EE_RADAR_RX_RING
#define EE_RADAR_RX_RING       512U   /* power of two */
#endif
#ifndef EE_RADAR_IRQ_PRIO
#define EE_RADAR_IRQ_PRIO      5U
#endif
#ifndef EE_RADAR_OWN_IRQ
#define EE_RADAR_OWN_IRQ       1      /* 0 if Cube already defines UART4_IRQHandler */
#endif

#ifndef LASER_Pin
#define LASER_Pin              GPIO_PIN_4   /* ARD_D7 / PA4 */
#define LASER_GPIO_Port        GPIOA
#endif

static UART_HandleTypeDef s_radar_uart;
static TIM_HandleTypeDef s_servo_tim;
static uint8_t s_radar_ready;
static uint8_t s_servo_ready;
static uint8_t s_rx_ring[EE_RADAR_RX_RING];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_rx_count;
static volatile uint32_t s_rx_dropped;
static volatile uint32_t s_rx_errors;
static GpioPin_t s_laser_pin;

static RD03D_Transport_t s_radar_io;
static RadarTrack_Config_t s_radar_cfg;
static Servo_Backend_t s_servo_backend;
static Laser_Backend_t s_laser_backend;
static LaserAim_Config_t s_aim_cfg;

void Enhancements_HW_RadarIrq(void)
{
    USART_TypeDef *u = s_radar_uart.Instance;

    if (u == NULL) {
        return;
    }
    if ((u->ISR & (UART_FLAG_ORE | UART_FLAG_NE | UART_FLAG_FE | UART_FLAG_PE)) != 0U) {
        u->ICR = UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF;
        s_rx_errors++;
    }
    while ((u->ISR & UART_FLAG_RXNE) != 0U) {
        uint8_t b = (uint8_t)(u->RDR & 0xFFU);
        uint16_t next = (uint16_t)((s_rx_head + 1U) & (EE_RADAR_RX_RING - 1U));
        if (next == s_rx_tail) {
            s_rx_dropped++; /* parser resyncs on the next header */
        } else {
            s_rx_ring[s_rx_head] = b;
            s_rx_head = next;
        }
        s_rx_count++;
    }
}

#if EE_RADAR_OWN_IRQ
void UART4_IRQHandler(void)
{
    Enhancements_HW_RadarIrq();
}
#endif

void Enhancements_HW_RadarStats(uint32_t *rx, uint32_t *dropped, uint32_t *errors)
{
    if (rx != NULL) {
        *rx = s_rx_count;
    }
    if (dropped != NULL) {
        *dropped = s_rx_dropped;
    }
    if (errors != NULL) {
        *errors = s_rx_errors;
    }
}

static uint16_t Radar_Read(void *ctx, uint8_t *buf, uint16_t max)
{
    uint16_t n = 0U;
    (void)ctx;
    while ((n < max) && (s_rx_tail != s_rx_head)) {
        buf[n++] = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) & (EE_RADAR_RX_RING - 1U));
    }
    return n;
}

static void Radar_Write(void *ctx, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    if ((s_radar_ready == 0U) || (buf == NULL) || (len == 0U)) {
        return;
    }
    (void)HAL_UART_Transmit(&s_radar_uart, (uint8_t *)buf, len, 50U);
}

static void Servo_SetPulse(void *ctx, uint16_t pulse_us)
{
    (void)ctx;
    if (s_servo_ready == 0U) {
        return;
    }
    /* Timer runs at 1 MHz, so compare value = pulse width in µs. */
    __HAL_TIM_SET_COMPARE(&s_servo_tim, TIM_CHANNEL_1, pulse_us);
}

static void Laser_SetOutput(void *ctx, uint8_t on)
{
    GpioPin_t *p = (GpioPin_t *)ctx;
    uint8_t level = (uint8_t)(((on != 0U) == (EE_LASER_ACTIVE_HIGH != 0)) ? 1U : 0U);
    if ((p == NULL) || (p->port == NULL)) {
        return;
    }
    HAL_GPIO_WritePin(p->port, p->pin, (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static HAL_StatusTypeDef Radar_UartInit(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1; /* PA0 = UART4_TX (D1), PA1 = UART4_RX (D0) */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gpio);

    (void)memset(&s_radar_uart, 0, sizeof(s_radar_uart));
    s_radar_uart.Instance = UART4;
    s_radar_uart.Init.BaudRate = RD03D_BAUD;
    s_radar_uart.Init.WordLength = UART_WORDLENGTH_8B;
    s_radar_uart.Init.StopBits = UART_STOPBITS_1;
    s_radar_uart.Init.Parity = UART_PARITY_NONE;
    s_radar_uart.Init.Mode = UART_MODE_TX_RX;
    s_radar_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    /* At the 4 MHz MSI reset clock, 16x oversampling gives 250000 baud
     * (-2.3 %); 8x gives 258065 (+0.8 %). Prefer 8x on slow clocks. */
    s_radar_uart.Init.OverSampling = (HAL_RCC_GetPCLK1Freq() < 32000000U)
                                         ? UART_OVERSAMPLING_8
                                         : UART_OVERSAMPLING_16;
    s_radar_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
#if defined(USART_PRESC_PRESCALER)
    s_radar_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
#endif
    s_radar_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&s_radar_uart) != HAL_OK) {
        s_radar_uart.Instance = NULL;
        return HAL_ERROR;
    }

    /* 8-byte RX FIFO: at the 4 MHz reset clock a byte arrives every ~156
     * cycles, so the FIFO absorbs ISR latency (SysTick, EXTI, I2C waits)
     * instead of overrunning. RXNE now means "FIFO not empty"; the ISR
     * drains everything available per entry. */
    (void)HAL_UARTEx_SetRxFifoThreshold(&s_radar_uart, UART_RXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_EnableFifoMode(&s_radar_uart);

    s_rx_head = 0U;
    s_rx_tail = 0U;
    __HAL_UART_ENABLE_IT(&s_radar_uart, UART_IT_RXNE);
    HAL_NVIC_SetPriority(UART4_IRQn, EE_RADAR_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    return HAL_OK;
}

static HAL_StatusTypeDef Servo_TimInit(void)
{
    GPIO_InitTypeDef gpio;
    TIM_OC_InitTypeDef oc;
    uint32_t timclk;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_4; /* PB4 = TIM3_CH1 (D5) */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* APB1 timer clock is 2×PCLK1 whenever the APB1 prescaler is not 1. */
    timclk = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1_2) != 0U) {
        timclk *= 2U;
    }

    (void)memset(&s_servo_tim, 0, sizeof(s_servo_tim));
    s_servo_tim.Instance = TIM3;
    s_servo_tim.Init.Prescaler = (timclk / 1000000U) - 1U; /* 1 tick = 1 µs */
    s_servo_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_servo_tim.Init.Period = 20000U - 1U;                  /* 20 ms = 50 Hz */
    s_servo_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_servo_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_servo_tim) != HAL_OK) {
        return HAL_ERROR;
    }

    (void)memset(&oc, 0, sizeof(oc));
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = EE_SERVO_CENTER_US;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_servo_tim, &oc, TIM_CHANNEL_1) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_TIM_PWM_Start(&s_servo_tim, TIM_CHANNEL_1) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

int Enhancements_HW_SetupRadarAim(Enhancements_Config_t *cfg)
{
    int err = 0;

    if (cfg == NULL) {
        return -1;
    }

    /* Laser first: latch the OFF level, then make the pin an output. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    s_laser_pin.port = LASER_GPIO_Port;
    s_laser_pin.pin = LASER_Pin;
    Laser_SetOutput(&s_laser_pin, 0U);
    {
        GPIO_InitTypeDef gpio;
        gpio.Pin = LASER_Pin;
        gpio.Mode = GPIO_MODE_OUTPUT_PP;
        gpio.Pull = (EE_LASER_ACTIVE_HIGH != 0) ? GPIO_PULLDOWN : GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(LASER_GPIO_Port, &gpio);
    }
    Laser_SetOutput(&s_laser_pin, 0U);
    s_laser_backend.set_output = Laser_SetOutput;
    s_laser_backend.ctx = &s_laser_pin;
    cfg->laser = &s_laser_backend;

    s_servo_ready = (uint8_t)((Servo_TimInit() == HAL_OK) ? 1U : 0U);
    if (s_servo_ready != 0U) {
        s_servo_backend.set_pulse_us = Servo_SetPulse;
        s_servo_backend.ctx = NULL;
        cfg->servo = &s_servo_backend;
    } else {
        cfg->servo = NULL;
        err |= 2;
    }

    s_radar_ready = (uint8_t)((Radar_UartInit() == HAL_OK) ? 1U : 0U);
    if (s_radar_ready != 0U) {
        s_radar_io.read = Radar_Read;
        s_radar_io.write = Radar_Write;
        s_radar_io.ctx = NULL;
        cfg->radar = &s_radar_io;
    } else {
        cfg->radar = NULL;
        err |= 1;
    }

    RadarTrack_DefaultConfig(&s_radar_cfg);
    s_radar_cfg.mirror_x = (int8_t)EE_RADAR_MIRROR_X;
    s_radar_cfg.fov_half_deg = EE_RADAR_FOV_HALF_DEG;
    s_radar_cfg.max_range_mm = (uint16_t)EE_RADAR_MAX_RANGE_MM;
    cfg->radar_cfg = &s_radar_cfg;

    LaserAim_DefaultConfig(&s_aim_cfg);
    s_aim_cfg.pulse_center_us = (uint16_t)EE_SERVO_CENTER_US;
    s_aim_cfg.direction = (int8_t)EE_SERVO_DIRECTION;
    s_aim_cfg.pan_min_deg = -(EE_SERVO_PAN_LIMIT_DEG);
    s_aim_cfg.pan_max_deg = EE_SERVO_PAN_LIMIT_DEG;
    s_aim_cfg.offset_x_mm = EE_LASER_OFFSET_X_MM;
    s_aim_cfg.offset_y_mm = EE_LASER_OFFSET_Y_MM;
    cfg->aim_cfg = &s_aim_cfg;

    return err;
}

#endif /* EE_RADAR_AIM */
