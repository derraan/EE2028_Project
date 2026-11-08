/**
 * @file ism43362.c
 * @brief Non-blocking AT engine for the Inventek ISM43362 (see ism43362.h).
 *
 * Protocol follows ST's es_wifi_io.c / es_wifi.c reference driver for the
 * B-L4S5I-IOT01A (STM32CubeL4 Projects/B-L4S5I-IOT01A/Applications/WiFi),
 * rewritten as a state machine: each wait is a phase with a deadline.
 */
#include "ism43362.h"
#include <string.h>

#define ISM_RESET_LOW_MS     10U   /* RST held low */
#define ISM_RESET_BOOT_MS    500U  /* module boot after RST released */
#define ISM_PROMPT_TIMEOUT   2000U /* prompt must appear within this */
#define ISM_SETUP_US         15U   /* NSS-low → first clock */
#define ISM_PAD              0x15U /* reply padding byte */

#define AT_OK     "\r\nOK\r\n> "

enum {
    P_IDLE = 0,
    /* reset */
    P_RST_LOW,
    P_RST_BOOT,
    P_PROMPT_WAIT,
    P_PROMPT_SETUP,
    P_PROMPT_RX,
    /* command */
    P_CMD_WAIT_READY,
    P_CMD_SETUP,
    P_CMD_TX,
    P_CMD_WAIT_RISE,
    P_CMD_RX_SETUP,
    P_CMD_RX
};

static void Finish(ISM_HandleTypeDef *h, ISM_Result_t r)
{
    h->io.select(h->io.ctx, 0U);
    while ((h->rx_len > 0U) &&
           (((uint8_t)h->rx[h->rx_len - 1U] == ISM_PAD) || (h->rx[h->rx_len - 1U] == '\0'))) {
        h->rx_len--;
    }
    h->rx[h->rx_len] = '\0';
    h->result = r;
    h->phase = P_IDLE;
}

void ISM_Init(ISM_HandleTypeDef *h, const ISM_Backend_t *io)
{
    if ((h == NULL) || (io == NULL)) {
        return;
    }
    (void)memset(h, 0, sizeof(*h));
    h->io = *io;
    h->result = ISM_IDLE;
    h->io.select(h->io.ctx, 0U);
}

void ISM_StartReset(ISM_HandleTypeDef *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    h->io.select(h->io.ctx, 0U);
    h->io.reset(h->io.ctx, 1U);
    h->t0_ms = now_ms;
    h->rx_len = 0U;
    h->result = ISM_BUSY;
    h->phase = P_RST_LOW;
}

uint8_t ISM_StartCmd(ISM_HandleTypeDef *h, const char *cmd,
                     const uint8_t *payload, uint16_t payload_len,
                     uint32_t timeout_ms, uint32_t now_ms)
{
    uint16_t n;

    if ((h == NULL) || (cmd == NULL)) {
        return 0U;
    }
    n = (uint16_t)strlen(cmd);
    if ((payload != NULL) && ((n & 1U) != 0U)) {
        return 0U; /* payload must start on a word boundary */
    }
    if (((uint32_t)n + payload_len + 1U) > ISM_TX_MAX) {
        return 0U;
    }
    (void)memcpy(h->tx, cmd, n);
    if ((payload != NULL) && (payload_len > 0U)) {
        (void)memcpy(&h->tx[n], payload, payload_len);
        n = (uint16_t)(n + payload_len);
    }
    if ((n & 1U) != 0U) {
        h->tx[n++] = '\n'; /* odd length: pad the last word */
    }
    h->tx_len = n;
    h->tx_pos = 0U;
    h->rx_len = 0U;
    h->deadline_ms = now_ms + timeout_ms;
    h->result = ISM_BUSY;
    h->phase = P_CMD_WAIT_READY;
    return 1U;
}

static uint8_t Expired(const ISM_HandleTypeDef *h, uint32_t now_ms)
{
    return (uint8_t)(((int32_t)(now_ms - h->deadline_ms) >= 0) ? 1U : 0U);
}

static uint8_t SetupDone(const ISM_HandleTypeDef *h)
{
    return (uint8_t)(((uint32_t)(h->io.now_us(h->io.ctx) - h->t_us) >= ISM_SETUP_US) ? 1U : 0U);
}

/* Read words while DRDY is high, up to this poll's slice budget.
 * Returns 1 when DRDY fell (reply complete). */
static uint8_t ReadSlice(ISM_HandleTypeDef *h)
{
    static const uint8_t dummy[2] = {'\n', '\n'};
    uint8_t buf[2];

    while (h->words_moved < ISM_SLICE_WORDS) {
        if (h->io.drdy(h->io.ctx) == 0U) {
            return 1U;
        }
        h->io.xfer(h->io.ctx, dummy, buf, 1U);
        h->words_moved++;
        if ((h->rx_len + 2U) <= ISM_RX_MAX) {
            h->rx[h->rx_len++] = (char)buf[0];
            h->rx[h->rx_len++] = (char)buf[1];
        }
    }
    return 0U;
}

/* One phase step: a pin check, a timer check, or one bounded SPI slice. */
static void Step(ISM_HandleTypeDef *h, uint32_t now_ms)
{
    switch (h->phase) {
        case P_RST_LOW:
            if ((uint32_t)(now_ms - h->t0_ms) >= ISM_RESET_LOW_MS) {
                h->io.reset(h->io.ctx, 0U);
                h->t0_ms = now_ms;
                h->phase = P_RST_BOOT;
            }
            break;

        case P_RST_BOOT:
            if ((uint32_t)(now_ms - h->t0_ms) >= ISM_RESET_BOOT_MS) {
                h->deadline_ms = now_ms + ISM_PROMPT_TIMEOUT;
                h->phase = P_PROMPT_WAIT;
            }
            break;

        case P_PROMPT_WAIT:
            if (h->io.drdy(h->io.ctx) != 0U) {
                h->io.select(h->io.ctx, 1U);
                h->t_us = h->io.now_us(h->io.ctx);
                h->phase = P_PROMPT_SETUP;
            } else if (Expired(h, now_ms) != 0U) {
                Finish(h, ISM_TIMEOUT);
            }
            break;

        case P_PROMPT_SETUP:
            if (SetupDone(h) != 0U) {
                h->phase = P_PROMPT_RX;
            }
            break;

        case P_PROMPT_RX:
            if (ReadSlice(h) != 0U) {
                h->rx[h->rx_len] = '\0';
                /* expect 0x15 0x15 '\r' '\n' '>' ' ' */
                Finish(h, ((h->rx_len >= 4U) &&
                           (strstr(h->rx, "\r\n> ") != NULL)) ? ISM_OK : ISM_ERROR);
            }
            break;

        case P_CMD_WAIT_READY:
            if (h->io.drdy(h->io.ctx) != 0U) {
                h->io.arm_rise(h->io.ctx);
                h->io.select(h->io.ctx, 1U);
                h->t_us = h->io.now_us(h->io.ctx);
                h->phase = P_CMD_SETUP;
            } else if (Expired(h, now_ms) != 0U) {
                Finish(h, ISM_TIMEOUT);
            }
            break;

        case P_CMD_SETUP:
            if (SetupDone(h) != 0U) {
                h->phase = P_CMD_TX;
            }
            break;

        case P_CMD_TX: {
            uint16_t left = (uint16_t)((h->tx_len - h->tx_pos) / 2U);
            uint16_t budget = (uint16_t)(ISM_SLICE_WORDS - h->words_moved);
            uint16_t words = (left > budget) ? budget : left;
            if (words > 0U) {
                h->io.xfer(h->io.ctx, &h->tx[h->tx_pos], NULL, words);
                h->words_moved += words;
                h->tx_pos = (uint16_t)(h->tx_pos + (words * 2U));
            }
            if (h->tx_pos >= h->tx_len) {
                h->io.select(h->io.ctx, 0U); /* end of command → module processes */
                h->phase = P_CMD_WAIT_RISE;
            }
            break;
        }

        case P_CMD_WAIT_RISE:
            if (h->io.rose(h->io.ctx) != 0U) {
                h->io.select(h->io.ctx, 1U);
                h->t_us = h->io.now_us(h->io.ctx);
                h->phase = P_CMD_RX_SETUP;
            } else if (Expired(h, now_ms) != 0U) {
                Finish(h, ISM_TIMEOUT);
            }
            break;

        case P_CMD_RX_SETUP:
            if (SetupDone(h) != 0U) {
                h->phase = P_CMD_RX;
            }
            break;

        case P_CMD_RX:
            if (ReadSlice(h) != 0U) {
                h->rx[h->rx_len] = '\0';
                if (strstr(h->rx, AT_OK) != NULL) {
                    Finish(h, ISM_OK);
                } else {
                    Finish(h, ISM_ERROR);
                }
            } else if (Expired(h, now_ms) != 0U) {
                Finish(h, ISM_TIMEOUT); /* DRDY stuck high */
            }
            break;

        case P_IDLE:
        default:
            break;
    }
}

ISM_Result_t ISM_Poll(ISM_HandleTypeDef *h, uint32_t now_ms)
{
    uint8_t i;

    if (h == NULL) {
        return ISM_ERROR;
    }
    h->words_moved = 0U;
    /* Chain instant steps (e.g. DRDY already high → select, reply ready →
     * read) so a slow main loop still finishes a command in few passes. Stop
     * at a real wait, at the end, or once a slice of SPI words has moved. */
    for (i = 0U; i < 6U; i++) {
        uint8_t before = h->phase;
        Step(h, now_ms);
        if ((h->phase == before) || (h->phase == P_IDLE) ||
            (h->words_moved >= ISM_SLICE_WORDS)) {
            break;
        }
    }
    return h->result;
}

const char *ISM_Reply(const ISM_HandleTypeDef *h)
{
    return (h != NULL) ? h->rx : "";
}
