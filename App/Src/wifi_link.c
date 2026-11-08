/**
 * @file wifi_link.c
 * @brief Wi-Fi telemetry session (see wifi_link.h). AT command sequences
 *        follow ST's es_wifi.c for the ISM43362: ES_WIFI_Connect,
 *        ES_WIFI_GetNetworkSettings, ES_WIFI_StartServerSingleConn,
 *        ES_WIFI_WaitServerConnection, ES_WIFI_SendData,
 *        ES_WIFI_CloseServerConnection.
 */
#include "wifi_link.h"
#include <stdio.h>
#include <string.h>

#define S2_WRITE_TIMEOUT_MS 500U

static void SetStatus(WifiLink_HandleTypeDef *h, const char *msg)
{
    (void)snprintf(h->status, sizeof(h->status), "%s", msg);
    h->status_ready = 1U;
}

static void Enter(WifiLink_HandleTypeDef *h, WifiLinkState_t s, uint32_t now_ms)
{
    h->state = s;
    h->step = 0U;
    h->busy = 0U;
    h->t_ms = now_ms;
}

static void Backoff(WifiLink_HandleTypeDef *h, const char *why, uint32_t now_ms)
{
    char msg[96];
    (void)snprintf(msg, sizeof(msg), "Wi-Fi: %s - retrying in %u s.", why,
                   (unsigned)(WIFI_LINK_BACKOFF_MS / 1000U));
    SetStatus(h, msg);
    Enter(h, WIFI_LINK_BACKOFF, now_ms);
}

static void QueueClear(WifiLink_HandleTypeDef *h)
{
    h->q_head = h->q_tail = h->q_used = 0U;
    h->batch_len = 0U;
}

void WifiLink_Init(WifiLink_HandleTypeDef *h, const ISM_Backend_t *io,
                   const WifiLink_Config_t *cfg, uint32_t now_ms)
{
    if ((h == NULL) || (io == NULL) || (cfg == NULL)) {
        return;
    }
    (void)memset(h, 0, sizeof(*h));
    ISM_Init(&h->ism, io);
    h->cfg = *cfg;
    Enter(h, WIFI_LINK_RESET, now_ms);
}

/* "C?" reply: "\r\n<ssid>,<pw>,<sec>,<dhcp>,<ipver>,<ip>,<mask>,<gw>,..." */
static void ParseIp(const char *reply, char out[16])
{
    const char *p = reply;
    uint8_t field = 0U;
    uint8_t n = 0U;

    out[0] = '\0';
    while ((*p == '\r') || (*p == '\n')) {
        p++;
    }
    while ((*p != '\0') && (*p != '\r')) {
        if (*p == ',') {
            field++;
        } else if ((field == 5U) && (n < 15U)) {
            out[n++] = *p;
        }
        p++;
    }
    out[n] = '\0';
}

/* "P?" reply: "\r\n<proto>,<remote ip>,..." — "0.0.0.0" means no client. */
static uint8_t ParsePeer(const char *reply, char out[16])
{
    const char *p = reply;
    uint8_t n = 0U;

    out[0] = '\0';
    while ((*p == '\r') || (*p == '\n')) {
        p++;
    }
    while ((*p != '\0') && (*p != ',')) {
        p++;
    }
    if (*p != ',') {
        return 0U;
    }
    p++;
    while ((*p != '\0') && (*p != ',') && (n < 15U)) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return (uint8_t)(((n > 0U) && (strcmp(out, "0.0.0.0") != 0)) ? 1U : 0U);
}

static void Cmd(WifiLink_HandleTypeDef *h, const char *cmd, uint32_t timeout_ms, uint32_t now_ms)
{
    h->busy = ISM_StartCmd(&h->ism, cmd, NULL, 0U, timeout_ms, now_ms);
}

/* Next command of a fixed sequence; returns 0 when the sequence is finished. */
static uint8_t JoinCmd(WifiLink_HandleTypeDef *h, uint32_t now_ms)
{
    char cmd[80];
    switch (h->step) {
        case 0: (void)snprintf(cmd, sizeof(cmd), "C1=%s\r", h->cfg.ssid); break;
        case 1: (void)snprintf(cmd, sizeof(cmd), "C2=%s\r", h->cfg.password); break;
        case 2: (void)snprintf(cmd, sizeof(cmd), "C3=3\r"); break; /* WPA2 */
        case 3:
            h->busy = ISM_StartCmd(&h->ism, "C0\r", NULL, 0U, WIFI_LINK_JOIN_MS, now_ms);
            return 1U;
        default: return 0U;
    }
    Cmd(h, cmd, WIFI_LINK_CMD_MS, now_ms);
    return 1U;
}

static uint8_t ServerCmd(WifiLink_HandleTypeDef *h, uint32_t now_ms)
{
    char cmd[24];
    switch (h->step) {
        case 0: (void)snprintf(cmd, sizeof(cmd), "P0=0\r"); break;
        case 1: (void)snprintf(cmd, sizeof(cmd), "PK=1,3000\r"); break; /* TCP keep-alive */
        case 2: (void)snprintf(cmd, sizeof(cmd), "P1=0\r"); break;      /* TCP */
        case 3: (void)snprintf(cmd, sizeof(cmd), "P8=1\r"); break;      /* backlog 1 */
        case 4: (void)snprintf(cmd, sizeof(cmd), "P2=%u\r", (unsigned)h->cfg.port); break;
        case 5: (void)snprintf(cmd, sizeof(cmd), "P5=11\r"); break;     /* start server */
        default: return 0U;
    }
    Cmd(h, cmd, WIFI_LINK_CMD_MS, now_ms);
    return 1U;
}

static void BuildBatch(WifiLink_HandleTypeDef *h)
{
    uint16_t n = (h->q_used > WIFI_LINK_BATCH_BYTES) ? (uint16_t)WIFI_LINK_BATCH_BYTES : h->q_used;
    uint16_t i;
    uint16_t pos = h->q_tail;
    for (i = 0U; i < n; i++) {
        h->batch[i] = h->q[pos];
        pos = (uint16_t)((pos + 1U) % WIFI_LINK_QUEUE_BYTES);
    }
    h->batch_len = n;
}

static void ClientLost(WifiLink_HandleTypeDef *h, uint32_t now_ms)
{
    char msg[64];
    (void)snprintf(msg, sizeof(msg), "Wi-Fi: telemetry client %s disconnected.", h->peer);
    SetStatus(h, msg);
    QueueClear(h);
    /* P5=10: drop the current client, keep the server listening. */
    Enter(h, WIFI_LINK_LISTEN, now_ms);
    h->listen_sub = 2U;
    Cmd(h, "P5=10\r", WIFI_LINK_CMD_MS, now_ms);
}

void WifiLink_Poll(WifiLink_HandleTypeDef *h, uint32_t now_ms)
{
    ISM_Result_t r = ISM_IDLE;

    if ((h == NULL) || (h->state == WIFI_LINK_OFF)) {
        return;
    }
    if (h->busy != 0U) {
        r = ISM_Poll(&h->ism, now_ms);
        if (r == ISM_BUSY) {
            return; /* one bounded step per poll */
        }
        h->busy = 0U;
    }

    switch (h->state) {
        case WIFI_LINK_RESET:
            if (r == ISM_IDLE) {
                ISM_StartReset(&h->ism, now_ms);
                h->busy = 1U;
            } else if (r == ISM_OK) {
                Enter(h, WIFI_LINK_JOIN, now_ms);
                (void)JoinCmd(h, now_ms);
            } else {
                Backoff(h, "module not responding on SPI3", now_ms);
            }
            break;

        case WIFI_LINK_JOIN:
            if (r == ISM_OK) {
                h->step++;
                if (JoinCmd(h, now_ms) == 0U) {
                    Enter(h, WIFI_LINK_GETIP, now_ms);
                    Cmd(h, "C?\r", WIFI_LINK_CMD_MS, now_ms);
                }
            } else if (r != ISM_IDLE) {
                char why[80];
                (void)snprintf(why, sizeof(why), "could not join \"%s\" (name/password, 2.4 GHz WPA2?)",
                               h->cfg.ssid);
                Backoff(h, why, now_ms);
            }
            break;

        case WIFI_LINK_GETIP:
            if (r == ISM_OK) {
                char msg[96];
                ParseIp(ISM_Reply(&h->ism), h->ip);
                (void)snprintf(msg, sizeof(msg), "Wi-Fi: connected to %s, IP %s, telemetry TCP port %u",
                               h->cfg.ssid, h->ip, (unsigned)h->cfg.port);
                SetStatus(h, msg);
                Enter(h, WIFI_LINK_SERVER, now_ms);
                (void)ServerCmd(h, now_ms);
            } else if (r != ISM_IDLE) {
                Backoff(h, "no IP address", now_ms);
            }
            break;

        case WIFI_LINK_SERVER:
            if (r == ISM_OK) {
                h->step++;
                if (ServerCmd(h, now_ms) == 0U) {
                    Enter(h, WIFI_LINK_LISTEN, now_ms);
                    h->listen_sub = 0U;
                }
            } else if (r != ISM_IDLE) {
                Backoff(h, "TCP server did not start", now_ms);
            }
            break;

        case WIFI_LINK_LISTEN:
            if (r == ISM_IDLE) {
                if ((h->listen_sub == 0U) &&
                    ((uint32_t)(now_ms - h->t_ms) >= WIFI_LINK_LISTEN_MS)) {
                    h->t_ms = now_ms;
                    Cmd(h, "MR\r", WIFI_LINK_CMD_MS, now_ms); /* flush async messages */
                }
            } else if (r == ISM_OK) {
                if (h->listen_sub == 0U) {
                    h->listen_sub = 1U;
                    Cmd(h, "P?\r", WIFI_LINK_CMD_MS, now_ms);
                } else if (h->listen_sub == 1U) {
                    h->listen_sub = 0U;
                    if (ParsePeer(ISM_Reply(&h->ism), h->peer) != 0U) {
                        char msg[64];
                        (void)snprintf(msg, sizeof(msg), "Wi-Fi: telemetry client %s connected.", h->peer);
                        SetStatus(h, msg);
                        QueueClear(h);
                        h->new_client = 1U;
                        Enter(h, WIFI_LINK_CLIENT, now_ms);
                        h->send_sub = 0U;
                    }
                } else {
                    h->listen_sub = 0U; /* P5=10 after a lost client */
                }
            } else {
                Backoff(h, "lost the module while listening", now_ms);
            }
            break;

        case WIFI_LINK_CLIENT:
            if (r == ISM_IDLE) {
                char cmd[16];
                if (h->send_sub == 0U) {
                    Cmd(h, "P0=0\r", WIFI_LINK_CMD_MS, now_ms);
                } else if (h->send_sub == 1U) {
                    (void)snprintf(cmd, sizeof(cmd), "S2=%u\r", (unsigned)S2_WRITE_TIMEOUT_MS);
                    Cmd(h, cmd, WIFI_LINK_CMD_MS, now_ms);
                } else if ((h->q_used > 0U) &&
                           ((uint32_t)(now_ms - h->t_ms) >= WIFI_LINK_FLUSH_MS)) {
                    h->t_ms = now_ms;
                    BuildBatch(h);
                    (void)snprintf(cmd, sizeof(cmd), "S3=%04u\r", (unsigned)h->batch_len);
                    h->busy = ISM_StartCmd(&h->ism, cmd, h->batch, h->batch_len,
                                           WIFI_LINK_CMD_MS, now_ms);
                }
            } else if (r == ISM_OK) {
                if (h->send_sub < 2U) {
                    h->send_sub++;
                } else if (strstr(ISM_Reply(&h->ism), "-1\r\n") != NULL) {
                    ClientLost(h, now_ms); /* module reports the socket is gone */
                } else {
                    h->q_tail = (uint16_t)((h->q_tail + h->batch_len) % WIFI_LINK_QUEUE_BYTES);
                    h->q_used = (uint16_t)(h->q_used - h->batch_len);
                    h->bytes_sent += h->batch_len;
                    h->batch_len = 0U;
                }
            } else {
                ClientLost(h, now_ms);
            }
            break;

        case WIFI_LINK_BACKOFF:
            if ((uint32_t)(now_ms - h->t_ms) >= WIFI_LINK_BACKOFF_MS) {
                Enter(h, WIFI_LINK_RESET, now_ms);
            }
            break;

        case WIFI_LINK_OFF:
        default:
            break;
    }
}

void WifiLink_Enqueue(WifiLink_HandleTypeDef *h, const char *line)
{
    uint16_t len;
    uint16_t i;
    uint16_t need;

    if ((h == NULL) || (line == NULL) || (h->state != WIFI_LINK_CLIENT)) {
        return;
    }
    len = (uint16_t)strlen(line);
    need = (uint16_t)(len + 2U);
    if (need > (uint16_t)(WIFI_LINK_QUEUE_BYTES - h->q_used)) {
        h->dropped_lines++; /* full: keep what is queued (a batch may be in flight) */
        return;
    }
    for (i = 0U; i < need; i++) {
        uint8_t b = (i < len) ? (uint8_t)line[i] : ((i == len) ? (uint8_t)'\r' : (uint8_t)'\n');
        h->q[h->q_head] = b;
        h->q_head = (uint16_t)((h->q_head + 1U) % WIFI_LINK_QUEUE_BYTES);
    }
    h->q_used = (uint16_t)(h->q_used + need);
}

uint8_t WifiLink_TakeNewClient(WifiLink_HandleTypeDef *h)
{
    uint8_t v;
    if (h == NULL) {
        return 0U;
    }
    v = h->new_client;
    h->new_client = 0U;
    return v;
}

const char *WifiLink_TakeStatus(WifiLink_HandleTypeDef *h)
{
    if ((h == NULL) || (h->status_ready == 0U)) {
        return NULL;
    }
    h->status_ready = 0U;
    return h->status;
}
