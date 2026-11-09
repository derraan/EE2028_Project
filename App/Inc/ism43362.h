/**
 * @file ism43362.h
 * @brief Non-blocking AT engine for the on-board Inventek ISM43362 Wi-Fi
 *        module (SPI3 on the B-L4S5I-IOT01A).
 *
 * HAL-free: the SPI / GPIO / timing come from an injected backend, so the
 * engine runs unchanged in the host test (scripts/test_wifi_host.mjs).
 *
 * Every ISM_Poll() does at most one bounded step (a short SPI slice or one
 * pin check) and returns. Waits are states with HAL_GetTick() deadlines —
 * no HAL_Delay, no spin-waits (project timing rule).
 *
 * SPI protocol (same as ST's es_wifi_io.c reference driver):
 *  - module raises DATA-READY (DRDY) when it can take a command;
 *  - host selects (NSS low), waits ~15 µs, sends the command as 16-bit words
 *    (odd length padded with '\n'), deselects;
 *  - DRDY rises again when the reply is ready; host selects, waits ~15 µs,
 *    reads 16-bit words while DRDY stays high, deselects;
 *  - reply ends with "\r\nOK\r\n> " (or contains "\r\nERROR"); trailing
 *    0x15 bytes are padding.
 */
#ifndef ISM43362_H
#define ISM43362_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ISM_TX_MAX        1100U  /* command + one telemetry batch */
#define ISM_RX_MAX        512U
#define ISM_SLICE_WORDS   64U    /* max 16-bit words moved per poll */

typedef struct {
    uint8_t (*drdy)(void *ctx);                 /* DATA-READY pin level */
    void (*arm_rise)(void *ctx);                /* clear + arm the rising-edge latch */
    uint8_t (*rose)(void *ctx);                 /* 1 once DRDY rose since arm */
    void (*select)(void *ctx, uint8_t on);      /* 1 = NSS low */
    void (*reset)(void *ctx, uint8_t asserted); /* 1 = hold module in reset */
    /* Full-duplex transfer of `words` 16-bit frames (bounded, polled). rx may be NULL. */
    void (*xfer)(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t words);
    uint32_t (*now_us)(void *ctx);              /* free-running µs counter */
    void *ctx;
} ISM_Backend_t;

typedef enum {
    ISM_IDLE = 0,
    ISM_BUSY,
    ISM_OK,
    ISM_ERROR,    /* reply had ERROR or no OK */
    ISM_TIMEOUT   /* DRDY never came */
} ISM_Result_t;

typedef struct {
    ISM_Backend_t io;
    uint8_t phase;
    ISM_Result_t result;
    uint32_t t0_ms;
    uint32_t deadline_ms;
    uint32_t t_us;
    uint8_t tx[ISM_TX_MAX];
    uint16_t tx_len;    /* bytes, always even */
    uint16_t tx_pos;
    char rx[ISM_RX_MAX + 2U];
    uint16_t rx_len;
    uint32_t words_moved; /* diagnostics: words transferred in the last poll */
} ISM_HandleTypeDef;

void ISM_Init(ISM_HandleTypeDef *h, const ISM_Backend_t *io);

/** Hardware reset + wait for the "> " prompt (about 0.5 s, non-blocking). */
void ISM_StartReset(ISM_HandleTypeDef *h, uint32_t now_ms);

/**
 * Send `cmd` (must end in '\r') plus optional binary `payload` in the same
 * transfer, then collect the reply. For a payload, `cmd` must have an even
 * length (e.g. "S3=0042\r"). Returns 0 if the command does not fit.
 */
uint8_t ISM_StartCmd(ISM_HandleTypeDef *h, const char *cmd,
                     const uint8_t *payload, uint16_t payload_len,
                     uint32_t timeout_ms, uint32_t now_ms);

/** Advance one bounded step. ISM_BUSY until the transaction finishes. */
ISM_Result_t ISM_Poll(ISM_HandleTypeDef *h, uint32_t now_ms);

/** NUL-terminated reply of the last finished transaction (padding removed). */
const char *ISM_Reply(const ISM_HandleTypeDef *h);

#ifdef __cplusplus
}
#endif

#endif /* ISM43362_H */
