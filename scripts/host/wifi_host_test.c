/**
 * Host regression for the non-blocking Wi-Fi telemetry stack:
 * App/Src/ism43362.c (SPI AT engine) + App/Src/wifi_link.c (session/queue)
 * against a byte-level fake ISM43362 (DRDY handshake, 16-bit frames,
 * 0x15 padding, AT replies, one TCP client that can come and go).
 *
 * Run: node scripts/test_wifi_host.mjs
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wifi_link.h"

static int s_fail;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
            s_fail++;                                                 \
        }                                                             \
    } while (0)

/* ------------------------------------------------------------ fake module */

enum { M_OFF, M_BOOT, M_READY, M_RECV, M_BUSY, M_REPLY, M_DRAINED };

static struct {
    int phase;
    uint32_t until_ms;       /* boot / processing finishes */
    int selected;
    int armed, rose;
    char cmd[1200];
    int cmd_len;
    char out[600];
    int out_len, out_pos;
    char ssid[40], pw[40];
    int alive;               /* 0 = module never answers */
    int client;              /* a TCP client is connected */
    int server_on;
    int port;
    char rx_stream[8192];    /* bytes the client received */
    int rx_len;
    int s3_count, pq_count, c0_count, p510_count;
    uint32_t last_s3_ms, min_s3_gap;
} M;

static uint32_t g_ms;
static uint32_t g_us;

static const char *EXP_SSID = "TestHotspot";
static const char *EXP_PW = "secret123";

static void M_Reply(const char *body, uint32_t latency_ms)
{
    int n = (int)strlen(body);
    memcpy(M.out, body, (size_t)n);
    if (n & 1) {
        M.out[n++] = 0x15; /* pad to whole 16-bit words */
    }
    M.out_len = n;
    M.out_pos = 0;
    M.phase = M_BUSY;
    M.until_ms = g_ms + latency_ms;
}

static void M_Process(void)
{
    char c[1200];
    int n = M.cmd_len;
    memcpy(c, M.cmd, (size_t)n);
    c[n] = '\0';
    if ((n > 0) && (c[n - 1] == '\n') && (strncmp(c, "S3=", 3) != 0)) {
        c[--n] = '\0'; /* odd-length pad (S3 payloads use their explicit length) */
    }

    if (!strncmp(c, "C1=", 3)) {
        sscanf(c + 3, "%39[^\r]", M.ssid);
        M_Reply("\r\nOK\r\n> ", 2);
    } else if (!strncmp(c, "C2=", 3)) {
        sscanf(c + 3, "%39[^\r]", M.pw);
        M_Reply("\r\nOK\r\n> ", 2);
    } else if (!strncmp(c, "C3=", 3)) {
        M_Reply("\r\nOK\r\n> ", 2);
    } else if (!strcmp(c, "C0\r")) {
        M.c0_count++;
        if (!strcmp(M.ssid, EXP_SSID) && !strcmp(M.pw, EXP_PW)) {
            M_Reply("\r\n[JOIN   ] TestHotspot,172.20.10.5,0,0\r\nOK\r\n> ", 900);
        } else {
            M_Reply("\r\n[JOIN   ] TestHotspot\r\nJOIN Failed\r\n> ", 900);
        }
    } else if (!strcmp(c, "C?\r")) {
        M_Reply("\r\nTestHotspot,secret123,3,1,0,172.20.10.5,255.255.255.240,172.20.10.1,172.20.10.1,0.0.0.0,1,0\r\nOK\r\n> ", 3);
    } else if (!strncmp(c, "P2=", 3)) {
        M.port = atoi(c + 3);
        M_Reply("\r\nOK\r\n> ", 2);
    } else if (!strcmp(c, "P5=11\r")) {
        M.server_on = 1;
        M_Reply("\r\nOK\r\n> ", 5);
    } else if (!strcmp(c, "P5=10\r")) {
        M.p510_count++;
        M.client = 0;
        M_Reply("\r\nOK\r\n> ", 3);
    } else if (!strcmp(c, "MR\r")) {
        M_Reply("\r\n[SOMA][EOMA]\r\nOK\r\n> ", 2);
    } else if (!strcmp(c, "P?\r")) {
        M.pq_count++;
        M_Reply(M.client ? "\r\n0,172.20.10.3,5028,172.20.10.5,5028,1,1\r\nOK\r\n> "
                         : "\r\n0,0.0.0.0,5028,172.20.10.5,0,1,0\r\nOK\r\n> ", 2);
    } else if (!strncmp(c, "S3=", 3)) {
        int len = atoi(c + 3);
        const char *payload = memchr(c, '\r', (size_t)n);
        M.s3_count++;
        if (M.s3_count > 1) {
            uint32_t gap = g_ms - M.last_s3_ms;
            if ((M.min_s3_gap == 0) || (gap < M.min_s3_gap)) M.min_s3_gap = gap;
        }
        M.last_s3_ms = g_ms;
        if (M.client && payload && ((payload + 1 + len) <= (c + n + 1))) {
            memcpy(&M.rx_stream[M.rx_len], payload + 1, (size_t)len);
            M.rx_len += len;
            M_Reply("\r\n0042\r\nOK\r\n> ", 4);
        } else {
            M_Reply("\r\n-1\r\nOK\r\n> ", 4);
        }
    } else {
        M_Reply("\r\nOK\r\n> ", 2); /* P0, PK, P1, P8, S2 */
    }
}

static void M_Tick(void)
{
    if ((M.phase == M_BOOT) && ((int32_t)(g_ms - M.until_ms) >= 0)) {
        static const char prompt[] = "\x15\x15\r\n> ";
        memcpy(M.out, prompt, 6);
        M.out_len = 6;
        M.out_pos = 0;
        M.phase = M_REPLY; /* prompt is read like a reply */
    }
    if ((M.phase == M_BUSY) && ((int32_t)(g_ms - M.until_ms) >= 0)) {
        M.phase = M_REPLY;
        if (M.armed) {
            M.rose = 1;
            M.armed = 0;
        }
    }
}

static uint8_t Io_Drdy(void *ctx)
{
    (void)ctx;
    M_Tick();
    if (!M.alive) return 0;
    return (uint8_t)((M.phase == M_READY) || (M.phase == M_RECV) || (M.phase == M_REPLY));
}
static void Io_ArmRise(void *ctx) { (void)ctx; M.rose = 0; M.armed = 1; }
static uint8_t Io_Rose(void *ctx) { (void)ctx; M_Tick(); return (uint8_t)M.rose; }
static void Io_Select(void *ctx, uint8_t on)
{
    (void)ctx;
    if (on && !M.selected) {
        if (M.phase == M_READY) {
            M.phase = M_RECV;
            M.cmd_len = 0;
        }
    } else if (!on && M.selected) {
        if (M.phase == M_RECV && M.cmd_len > 0) {
            M_Process();
        } else if (M.phase == M_DRAINED) {
            M.phase = M_READY;
        }
    }
    M.selected = on;
}
static void Io_Reset(void *ctx, uint8_t asserted)
{
    (void)ctx;
    if (asserted) {
        M.phase = M_OFF;
    } else if (M.alive) {
        M.phase = M_BOOT;
        M.until_ms = g_ms + 300;
    }
}
static uint32_t s_words_max;
static void Io_Xfer(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t words)
{
    (void)ctx;
    if (rx == NULL) {
        if (M.phase == M_RECV) {
            memcpy(&M.cmd[M.cmd_len], tx, (size_t)words * 2u);
            M.cmd_len += words * 2;
        }
        return;
    }
    if (M.phase == M_REPLY) {
        rx[0] = (uint8_t)M.out[M.out_pos];
        rx[1] = (uint8_t)M.out[M.out_pos + 1];
        M.out_pos += 2;
        if (M.out_pos >= M.out_len) {
            M.phase = M_DRAINED; /* DRDY falls: reply finished */
        }
    } else {
        rx[0] = rx[1] = 0x15;
    }
}
static uint32_t Io_NowUs(void *ctx) { (void)ctx; g_us += 7; return g_us; }

/* ------------------------------------------------------------ harness */

static WifiLink_HandleTypeDef L;
static char s_status[16][100];
static int s_nstatus;

static void Boot(int alive, const char *ssid, const char *pw)
{
    static const ISM_Backend_t io = {Io_Drdy, Io_ArmRise, Io_Rose, Io_Select, Io_Reset, Io_Xfer, Io_NowUs, NULL};
    WifiLink_Config_t cfg;
    memset(&M, 0, sizeof(M));
    M.alive = alive;
    M.phase = M_OFF;
    g_ms = 1000;
    g_us = 0;
    s_nstatus = 0;
    s_words_max = 0;
    cfg.ssid = ssid;
    cfg.password = pw;
    cfg.port = 5028;
    WifiLink_Init(&L, &io, &cfg, g_ms);
}

static void Run(uint32_t ms, uint32_t step)
{
    uint32_t end = g_ms + ms;
    while (g_ms < end) {
        const char *s;
        g_ms += step;
        WifiLink_Poll(&L, g_ms);
        if (L.ism.words_moved > s_words_max) s_words_max = L.ism.words_moved;
        s = WifiLink_TakeStatus(&L);
        if (s && s_nstatus < 16) {
            snprintf(s_status[s_nstatus++], sizeof(s_status[0]), "%s", s);
        }
    }
}

static int StatusHas(const char *needle)
{
    int i;
    for (i = 0; i < s_nstatus; i++) {
        if (strstr(s_status[i], needle)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ scenarios */

static void Test_JoinListenStream(uint32_t step)
{
    int pq0, i, new_clients = 0;
    char expect[4096];
    int elen = 0;

    Boot(1, EXP_SSID, EXP_PW);
    Run(3000, step);
    CHECK(StatusHas("connected to TestHotspot, IP 172.20.10.5, telemetry TCP port 5028"),
          "[%u ms] joined + IP announced (got %d statuses: %s)", step, s_nstatus, s_nstatus ? s_status[0] : "-");
    CHECK(M.server_on && M.port == 5028, "[%u ms] TCP server started on 5028", step);
    CHECK(L.state == WIFI_LINK_LISTEN, "[%u ms] listening, state=%d", step, L.state);

    /* lines before a client are not queued (stale) */
    WifiLink_Enqueue(&L, "EVT MODE=STANDBY_MODE");
    CHECK(L.q_used == 0, "no queueing without a client");

    pq0 = M.pq_count;
    Run(5000, step);
    CHECK(M.pq_count - pq0 >= 8 && M.pq_count - pq0 <= 11, "[%u ms] P? about every 500 ms, got %d in 5 s", step, M.pq_count - pq0);

    M.client = 1;
    Run(800, step);
    CHECK(L.state == WIFI_LINK_CLIENT, "[%u ms] client accepted", step);
    CHECK(StatusHas("client 172.20.10.3 connected"), "[%u ms] client status", step);
    new_clients += WifiLink_TakeNewClient(&L);
    new_clients += WifiLink_TakeNewClient(&L);
    CHECK(new_clients == 1, "TakeNewClient exactly once, got %d", new_clients);

    for (i = 0; i < 40; i++) {
        char line[64];
        snprintf(line, sizeof(line), "Telemetry -> line %02d", i);
        WifiLink_Enqueue(&L, line);
        elen += snprintf(&expect[elen], sizeof(expect) - (size_t)elen, "%s\r\n", line);
        Run(25, step);
    }
    Run(1500, step);
    CHECK(M.rx_len == elen && memcmp(M.rx_stream, expect, (size_t)elen) == 0,
          "[%u ms] client received the exact CRLF stream (%d of %d bytes)", step, M.rx_len, elen);
    /* measured when the fake processes S3, one poll after the firmware's timer */
    CHECK(M.min_s3_gap + step >= WIFI_LINK_FLUSH_MS, "[%u ms] batched sends >= %u ms apart, min %u", step,
          (unsigned)WIFI_LINK_FLUSH_MS, (unsigned)M.min_s3_gap);
    CHECK(M.s3_count < 40, "[%u ms] 40 lines went out in %d batches", step, M.s3_count);
    CHECK(s_words_max <= ISM_SLICE_WORDS, "[%u ms] <= %u SPI words per poll, max %u", step,
          (unsigned)ISM_SLICE_WORDS, (unsigned)s_words_max);

    /* client leaves: next send fails → P5=10 → listening → reconnect works */
    M.client = 0;
    WifiLink_Enqueue(&L, "EVT ATTACK");
    Run(1000, step);
    CHECK(StatusHas("disconnected"), "[%u ms] client-gone status", step);
    CHECK(M.p510_count >= 1 && L.state == WIFI_LINK_LISTEN, "[%u ms] dropped client, listening again", step);
    M.client = 1;
    Run(1000, step);
    CHECK(L.state == WIFI_LINK_CLIENT && WifiLink_TakeNewClient(&L) == 1, "[%u ms] second client accepted", step);
}

static void Test_WrongPassword(void)
{
    Boot(1, EXP_SSID, "wrong");
    Run(3000, 1);
    CHECK(StatusHas("could not join \"TestHotspot\""), "wrong password reported");
    CHECK(L.state == WIFI_LINK_BACKOFF, "backing off, state=%d", L.state);
    Run(WIFI_LINK_BACKOFF_MS + 3000, 1);
    CHECK(M.c0_count >= 2, "retried after backoff, C0 count %d", M.c0_count);
}

static void Test_DeadModule(void)
{
    Boot(0, EXP_SSID, EXP_PW);
    Run(4000, 1);
    CHECK(StatusHas("module not responding"), "dead module reported");
    CHECK(L.state == WIFI_LINK_BACKOFF, "dead module → backoff");
}

static void Test_QueueFull(void)
{
    int i;
    Boot(1, EXP_SSID, EXP_PW);
    Run(4000, 1);
    M.client = 1;
    Run(800, 1);
    for (i = 0; i < 200; i++) {
        WifiLink_Enqueue(&L, "Telemetry -> T:26.4 C, P:1008.2 hPa, H:55.0 % | Accel:[0.1, -0.2, 9.8] m/s^2");
    }
    CHECK(L.q_used <= WIFI_LINK_QUEUE_BYTES && L.dropped_lines > 0, "full queue drops lines (used %u, dropped %u)",
          (unsigned)L.q_used, (unsigned)L.dropped_lines);
    Run(3000, 1);
    CHECK(L.q_used == 0, "queue drains, left %u", (unsigned)L.q_used);
}

int main(void)
{
    Test_JoinListenStream(1);   /* fast main loop */
    Test_JoinListenStream(20);  /* slow loop: sensor reads, I2C */
    Test_WrongPassword();
    Test_DeadModule();
    Test_QueueFull();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("OK - non-blocking ISM43362 Wi-Fi link (join, server, stream, reconnect, backoff)\n");
    return 0;
}
