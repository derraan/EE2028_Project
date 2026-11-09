/**
 * @file telemetry_wifi.c
 * @brief Mission telemetry over the on-board ISM43362 Wi-Fi (TCP server).
 *
 * Every Telemetry_WriteLine() line is also queued here and streamed to one
 * TCP client on port WIFI_TCP_PORT (default 5028): the exact bytes the
 * ST-LINK COM port carries. `node Command/serve.mjs` bridges that TCP stream
 * to VESPER (Server-Sent Events) — see docs/Telemetry_EVT_Contract.md.
 *
 * Enabled only when App/Inc/wifi_config.h exists (git-ignored; copy
 * wifi_config.example.h). Fully non-blocking: Telemetry_WifiPoll() advances
 * the session one bounded step per main-loop pass. No HAL_Delay.
 *
 * Module wiring (internal, not on the Arduino header; labels from main.h):
 *   SPI3 SCK PC10 / MISO PC11 / MOSI PC12 (AF6), CSN PE0, DRDY PE1 (EXTI1),
 *   RST PE8, WAKEUP PB13 (held low). Pin setup follows ST's SPI_WIFI_MspInit
 *   (STM32CubeL4 Projects/B-L4S5I-IOT01A/Applications/WiFi/Common/Src/es_wifi_io.c).
 */
#include "telemetry_evt.h"
#include "stm32l4xx_hal.h"
#include "wifi_link.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("wifi_config.h")
#include "wifi_config.h"
#define TELEMETRY_WIFI_ENABLED 1
#endif
#endif

#ifndef TELEMETRY_WIFI_ENABLED
#define TELEMETRY_WIFI_ENABLED 0
#endif

#if TELEMETRY_WIFI_ENABLED

#ifndef WIFI_TCP_PORT
#define WIFI_TCP_PORT 5028U
#endif
#ifndef WIFI_IRQ_PRIO
#define WIFI_IRQ_PRIO 6U
#endif

static SPI_HandleTypeDef s_spi;
static WifiLink_HandleTypeDef s_link;
static volatile uint8_t s_rise_armed;
static volatile uint8_t s_rose;
static uint8_t s_new_client;
static uint32_t s_cycles_per_us = 4U;

/* --------------------------------------------------------- backend */

static uint8_t Io_Drdy(void *ctx)
{
    (void)ctx;
    return (uint8_t)((HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_1) == GPIO_PIN_SET) ? 1U : 0U);
}

static void Io_ArmRise(void *ctx)
{
    (void)ctx;
    s_rose = 0U;
    s_rise_armed = 1U;
}

static uint8_t Io_Rose(void *ctx)
{
    (void)ctx;
    return s_rose;
}

static void Io_Select(void *ctx, uint8_t on)
{
    (void)ctx;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void Io_Reset(void *ctx, uint8_t asserted)
{
    (void)ctx;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, (asserted != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void Io_Xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t words)
{
    (void)ctx;
    /* 16-bit frames; bounded by the engine's slice size (≤ 64 words). */
    if (rx != NULL) {
        (void)HAL_SPI_TransmitReceive(&s_spi, (uint8_t *)tx, rx, words, 10U);
    } else {
        (void)HAL_SPI_Transmit(&s_spi, (uint8_t *)tx, words, 10U);
    }
}

static uint32_t Io_NowUs(void *ctx)
{
    (void)ctx;
    return DWT->CYCCNT / s_cycles_per_us;
}

/* --------------------------------------------------------- hardware */

static uint32_t SpiPrescaler(uint32_t pclk)
{
    /* smallest divider keeping SCK ≤ 10 MHz (module max 20 MHz) */
    static const uint32_t presc[] = {
        SPI_BAUDRATEPRESCALER_2, SPI_BAUDRATEPRESCALER_4, SPI_BAUDRATEPRESCALER_8,
        SPI_BAUDRATEPRESCALER_16, SPI_BAUDRATEPRESCALER_32, SPI_BAUDRATEPRESCALER_64,
        SPI_BAUDRATEPRESCALER_128, SPI_BAUDRATEPRESCALER_256};
    uint32_t i;
    for (i = 0U; i < 8U; i++) {
        if ((pclk >> (i + 1U)) <= 10000000U) {
            return presc[i];
        }
    }
    return SPI_BAUDRATEPRESCALER_256;
}

static uint8_t Hw_Init(void)
{
    GPIO_InitTypeDef g;

    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    (void)memset(&g, 0, sizeof(g));
    /* WAKEUP PB13 low */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_13;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    /* DRDY PE1: rising-edge interrupt */
    g.Pin = GPIO_PIN_1;
    g.Mode = GPIO_MODE_IT_RISING;
    HAL_GPIO_Init(GPIOE, &g);
    /* RST PE8 (start in reset) */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_8;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOE, &g);
    /* NSS PE0 high (deselected) */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_0;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(GPIOE, &g);
    /* SPI3 SCK PC10, MISO PC11 (pull-up), MOSI PC12 */
    g.Mode = GPIO_MODE_AF_PP;
    g.Alternate = GPIO_AF6_SPI3;
    g.Pin = GPIO_PIN_10 | GPIO_PIN_12;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_11;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &g);

    (void)memset(&s_spi, 0, sizeof(s_spi));
    s_spi.Instance = SPI3;
    s_spi.Init.Mode = SPI_MODE_MASTER;
    s_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize = SPI_DATASIZE_16BIT;
    s_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_spi.Init.NSS = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SpiPrescaler(HAL_RCC_GetPCLK1Freq());
    s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    if (HAL_SPI_Init(&s_spi) != HAL_OK) {
        return 0U;
    }

    /* µs clock for the 15 µs NSS setup time (DWT cycle counter). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_cycles_per_us = (SystemCoreClock / 1000000U);
    if (s_cycles_per_us == 0U) {
        s_cycles_per_us = 1U;
    }

    HAL_NVIC_SetPriority(EXTI1_IRQn, WIFI_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
    return 1U;
}

void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1); /* → HAL_GPIO_EXTI_Callback → Telemetry_WifiDrdyIsr */
}

/* --------------------------------------------------------- public */

void Telemetry_WifiDrdyIsr(void)
{
    if (s_rise_armed != 0U) {
        s_rose = 1U;
        s_rise_armed = 0U;
    }
}

void Telemetry_WifiInit(uint32_t now_ms)
{
    static const ISM_Backend_t io = {
        Io_Drdy, Io_ArmRise, Io_Rose, Io_Select, Io_Reset, Io_Xfer, Io_NowUs, NULL};
    WifiLink_Config_t cfg;

    if (Hw_Init() == 0U) {
        Telemetry_Printf("Wi-Fi: SPI3 init failed - telemetry stays on UART only.");
        return;
    }
    cfg.ssid = WIFI_SSID;
    cfg.password = WIFI_PASSWORD;
    cfg.port = (uint16_t)WIFI_TCP_PORT;
    WifiLink_Init(&s_link, &io, &cfg, now_ms);
    Telemetry_Printf("Wi-Fi: joining \"%s\" (telemetry TCP port %u)...", WIFI_SSID,
                     (unsigned)WIFI_TCP_PORT);
}

void Telemetry_WifiPoll(uint32_t now_ms)
{
    const char *msg;

    WifiLink_Poll(&s_link, now_ms);
    if (WifiLink_TakeNewClient(&s_link) != 0U) {
        s_new_client = 1U;
    }
    msg = WifiLink_TakeStatus(&s_link);
    if (msg != NULL) {
        Telemetry_Printf("%s", msg); /* reaches UART, and Wi-Fi once a client is on */
    }
}

void Telemetry_WifiEnqueue(const char *line)
{
    WifiLink_Enqueue(&s_link, line);
}

uint8_t Telemetry_WifiTakeNewClient(void)
{
    uint8_t v = s_new_client;
    s_new_client = 0U;
    return v;
}

#else /* !TELEMETRY_WIFI_ENABLED */

void Telemetry_WifiDrdyIsr(void) {}

void Telemetry_WifiInit(uint32_t now_ms)
{
    (void)now_ms;
    Telemetry_Printf("Wi-Fi telemetry off: create App/Inc/wifi_config.h (copy wifi_config.example.h).");
}

void Telemetry_WifiPoll(uint32_t now_ms)
{
    (void)now_ms;
}

void Telemetry_WifiEnqueue(const char *line)
{
    (void)line;
}

uint8_t Telemetry_WifiTakeNewClient(void)
{
    return 0U;
}

#endif /* TELEMETRY_WIFI_ENABLED */
