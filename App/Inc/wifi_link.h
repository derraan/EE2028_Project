/**
 * @file wifi_link.h
 * @brief Wi-Fi telemetry session over the ISM43362: join the hotspot, run a
 *        TCP server, stream queued telemetry lines to one client.
 *
 * HAL-free and non-blocking (drives ism43362.c one step per poll).
 *
 *   OFF → RESET → JOIN (C1/C2/C3/C0) → GETIP (C?) → SERVER (P0..P5)
 *       → LISTEN (MR + P? every 500 ms) → CLIENT (S3 batches ≤ every 150 ms)
 *   Any failure → BACKOFF 5 s → RESET. Client gone (send fails) → LISTEN.
 *
 * Lines are queued only while a client is connected (older lines are stale);
 * WifiLink_TakeNewClient() lets main.c resend a state snapshot on connect.
 */
#ifndef WIFI_LINK_H
#define WIFI_LINK_H

#include <stdint.h>
#include "ism43362.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_LINK_QUEUE_BYTES   2048U
#define WIFI_LINK_BATCH_BYTES   1000U
#define WIFI_LINK_LISTEN_MS     500U
#define WIFI_LINK_FLUSH_MS      150U
#define WIFI_LINK_BACKOFF_MS    5000U
#define WIFI_LINK_JOIN_MS       20000U
#define WIFI_LINK_CMD_MS        3000U

typedef enum {
    WIFI_LINK_OFF = 0,
    WIFI_LINK_RESET,
    WIFI_LINK_JOIN,
    WIFI_LINK_GETIP,
    WIFI_LINK_SERVER,
    WIFI_LINK_LISTEN,
    WIFI_LINK_CLIENT,
    WIFI_LINK_BACKOFF
} WifiLinkState_t;

typedef struct {
    const char *ssid;
    const char *password;
    uint16_t port;
} WifiLink_Config_t;

typedef struct {
    ISM_HandleTypeDef ism;
    WifiLink_Config_t cfg;
    WifiLinkState_t state;
    uint8_t step;            /* index into the current command sequence */
    uint8_t busy;            /* an ISM transaction is in flight */
    uint8_t listen_sub;      /* 0 = MR next, 1 = P? next */
    uint8_t send_sub;        /* 0 = P0, 1 = S2, 2 = S3 */
    uint32_t t_ms;           /* timer for backoff / listen / flush cadence */
    char ip[16];
    char peer[16];
    uint8_t new_client;
    char status[96];
    uint8_t status_ready;

    uint8_t q[WIFI_LINK_QUEUE_BYTES];
    uint16_t q_head;
    uint16_t q_tail;
    uint16_t q_used;
    uint32_t dropped_lines;
    uint16_t batch_len;
    uint8_t batch[WIFI_LINK_BATCH_BYTES];
    uint32_t bytes_sent;
} WifiLink_HandleTypeDef;

void WifiLink_Init(WifiLink_HandleTypeDef *h, const ISM_Backend_t *io,
                   const WifiLink_Config_t *cfg, uint32_t now_ms);
void WifiLink_Poll(WifiLink_HandleTypeDef *h, uint32_t now_ms);

/** Queue one telemetry line (CRLF added). Ignored unless a client is connected. */
void WifiLink_Enqueue(WifiLink_HandleTypeDef *h, const char *line);

/** 1 exactly once per newly connected client. */
uint8_t WifiLink_TakeNewClient(WifiLink_HandleTypeDef *h);

/** Human-readable status change (joined, IP, client, errors), or NULL. */
const char *WifiLink_TakeStatus(WifiLink_HandleTypeDef *h);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_LINK_H */
