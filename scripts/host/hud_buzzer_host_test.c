/**
 * Host regression for the matrix HUD + buzzer integration (no HAL).
 *
 * Drives the real enhancements adapter (App/Src/enhancements.c) with the real
 * status display, HT16K33 driver and buzzer engine, exactly as main.c does:
 * a DroneStatus_t snapshot every loop tick plus one-shot DroneEvent_t calls.
 * The stub HAL captures every matrix frame; a fake backend records the buzzer.
 *
 * Run: node scripts/test_hud_buzzer_host.mjs
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhancements.h"

static int s_fail;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
            s_fail++;                                                      \
        }                                                                  \
    } while (0)

/* ------------------------------------------------------------ fake matrix */

static I2C_HandleTypeDef s_i2c;
static uint8_t s_frame[8];
static uint32_t s_frames_written;

static uint8_t Rev8(uint8_t v)
{
    uint8_t r = 0U;
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        r = (uint8_t)((r << 1) | ((v >> i) & 1U));
    }
    return r;
}

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t addr,
                                          uint8_t *data, uint16_t size,
                                          uint32_t timeout)
{
    uint8_t row;
    (void)hi2c;
    (void)addr;
    (void)timeout;
    if ((size == 17U) && (data[0] == 0x00U)) {
        for (row = 0U; row < 8U; row++) {
            s_frame[row] = Rev8(data[1U + row * 2U]); /* undo driver bit-reverse */
        }
        s_frames_written++;
    }
    return HAL_OK;
}

static int FrameIs(uint8_t value)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        if (s_frame[i] != value) {
            return 0;
        }
    }
    return 1;
}

/* Only logical column `col` lit, every row. */
static int FrameIsColumn(uint8_t col)
{
    return FrameIs((uint8_t)(1U << col));
}

/* ------------------------------------------------------------ fake buzzer */

#define MAX_EDGES 4096
static uint32_t s_edge_t[MAX_EDGES];
static uint8_t s_edge_on[MAX_EDGES];
static int s_edges;
static uint8_t s_buzz_on;

static void FakeTone(void *ctx, uint8_t on, uint16_t freq)
{
    extern uint32_t g_now;
    (void)ctx;
    (void)freq;
    on = (on != 0U) ? 1U : 0U;
    if ((on != s_buzz_on) && (s_edges < MAX_EDGES)) {
        s_edge_t[s_edges] = g_now;
        s_edge_on[s_edges] = on;
        s_edges++;
    }
    s_buzz_on = on;
}

/* Rising edges in [t0, t1). */
static int Beeps(uint32_t t0, uint32_t t1)
{
    int i;
    int n = 0;
    for (i = 0; i < s_edges; i++) {
        if ((s_edge_on[i] != 0U) && (s_edge_t[i] >= t0) && (s_edge_t[i] < t1)) {
            n++;
        }
    }
    return n;
}

/* Milliseconds the buzzer was ON within [t0, t1). */
static uint32_t OnTime(uint32_t t0, uint32_t t1)
{
    int i;
    uint32_t total = 0U;
    uint8_t on = 0U;
    uint32_t since = t0;
    for (i = 0; i < s_edges; i++) {
        uint32_t t = s_edge_t[i];
        if (t >= t1) {
            break;
        }
        if (t >= t0) {
            if (on != 0U) {
                total += t - since;
            }
            since = t;
        }
        on = s_edge_on[i];
    }
    if (on != 0U) {
        total += t1 - since;
    }
    return total;
}

/* ------------------------------------------------------------ drone sim */

uint32_t g_now = 1000U;
static DroneStatus_t s_st;
static uint32_t s_last_entry;

static void Tick(uint32_t ms, uint32_t step)
{
    uint32_t end = g_now + ms;
    while (g_now < end) {
        g_now += step;
        if (s_st.mode == THE_LAST_OF_2028) {
            uint32_t el = g_now - s_last_entry;
            s_st.rescue_remaining_ms = (el >= RESCUE_TIMEOUT_MS) ? 0U : (RESCUE_TIMEOUT_MS - el);
        } else {
            s_st.rescue_remaining_ms = 0U;
        }
        s_st.armed = (uint8_t)(((s_st.mode == BATTLE_MODE) && (s_st.charge >= 5U)) ? 1U : 0U);
        Enhancements_Update(&s_st, g_now);
    }
}

static void Boot(void)
{
    static HT16K33_HandleTypeDef dev;
    static Buzzer_Backend_t buzz;
    Enhancements_Config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memset(s_frame, 0, sizeof(s_frame));
    s_edges = 0;
    s_buzz_on = 0U;
    g_now = 1000U;

    if (HT16K33_Init(&dev, &s_i2c, HT16K33_I2C_ADDR_DEFAULT) != HAL_OK) {
        CHECK(0, "HT16K33_Init failed on stub bus");
    }
    buzz.set_tone = FakeTone;
    buzz.ctx = NULL;
    cfg.matrix = &dev;
    cfg.buzzer = &buzz;
    Enhancements_Init(&cfg);

    memset(&s_st, 0, sizeof(s_st));
    s_st.mode = STANDBY_MODE;
    s_st.charge = 10U;
}

/* ------------------------------------------------------------ scenarios */

static void Test_StandbyShowsChargeNotDead(void)
{
    Boot();
    Tick(300U, 10U);
    CHECK(FrameIs(0xFFU), "standby charge 10 should be a full static bar");
    CHECK(OnTime(1000U, g_now) == 0U, "standby must be silent");
}

static void Test_BattleEntryCueAndHud(void)
{
    uint32_t t0;
    int i;
    int saw_on = 0;
    int saw_off = 0;

    Boot();
    Tick(100U, 10U);
    t0 = g_now;
    s_st.mode = BATTLE_MODE;
    Tick(700U, 10U);
    CHECK(Beeps(t0, t0 + 700U) == 3, "battle entry should be 3 beeps, got %d", Beeps(t0, t0 + 700U));
    CHECK(OnTime(t0, t0 + 700U) >= 280U && OnTime(t0, t0 + 700U) <= 330U,
          "battle entry on-time ~300 ms, got %u", (unsigned)OnTime(t0, t0 + 700U));

    /* BATTLE MODE scroll (~3.6 s), then armed full-matrix flash. */
    Tick(4000U, 10U);
    for (i = 0; i < 20; i++) {
        Tick(10U, 10U);
        if (FrameIs(0xFFU)) {
            saw_on = 1;
        }
        if (FrameIs(0x00U)) {
            saw_off = 1;
        }
    }
    CHECK(saw_on && saw_off, "armed with no target should flash the full matrix");
}

static void Test_RechargeAndDenied(void)
{
    uint32_t t0;

    Boot();
    s_st.mode = BATTLE_MODE;
    s_st.charge = 2U;
    Tick(1000U, 10U); /* let battle entry finish */

    t0 = g_now;
    Enhancements_OnEvent(DRONE_EVENT_LASER_DENIED, g_now);
    Tick(400U, 10U);
    CHECK(Beeps(t0, g_now) >= 4, "denied should stutter (>=4 pulses), got %d", Beeps(t0, g_now));

    t0 = g_now;
    s_st.charge = 5U;
    Enhancements_OnEvent(DRONE_EVENT_LASER_RECHARGED, g_now);
    Tick(300U, 10U);
    CHECK(Beeps(t0, g_now) == 1, "recharge should be one blip, got %d", Beeps(t0, g_now));
    CHECK(OnTime(t0, g_now) <= 50U, "recharge blip should be short, got %u", (unsigned)OnTime(t0, g_now));
}

static void Test_CountdownBeepsAccelerate(uint32_t step)
{
    uint32_t t0;
    int early;
    int late;

    Boot();
    s_st.mode = BATTLE_MODE;
    Tick(1000U, step);
    s_st.mode = THE_LAST_OF_2028;
    s_last_entry = g_now;
    t0 = g_now;
    Tick(9900U, step);

    early = Beeps(t0, t0 + 3000U);
    late = Beeps(t0 + 6900U, t0 + 9900U);
    /* Regression: SetCountdown every tick used to hold the buzzer ON. */
    CHECK(OnTime(t0, t0 + 3000U) < 1000U,
          "[%ums loop] countdown must beep, not drone (on %u ms of 3000)",
          (unsigned)step, (unsigned)OnTime(t0, t0 + 3000U));
    CHECK(early >= 3, "[%ums loop] expected >=3 beeps in first 3 s, got %d", (unsigned)step, early);
    CHECK(late > early, "[%ums loop] beeps should accelerate: first 3 s %d, last 3 s %d",
          (unsigned)step, early, late);
}

static void Test_LastBannerThenDigits(void)
{
    uint8_t first[8];
    int changed = 0;
    int saw_digit = 0;
    int i;

    Boot();
    s_st.mode = BATTLE_MODE;
    Tick(1000U, 10U);
    s_st.mode = THE_LAST_OF_2028;
    s_last_entry = g_now;
    Tick(500U, 10U);
    memcpy(first, s_frame, 8);
    Tick(500U, 10U);
    changed = (memcmp(first, s_frame, 8) != 0);
    CHECK(changed, "live LAST should scroll the banner first (frame did not move)");

    /* Jump to 4.x s remaining: digit '4' or '5' flashing, not scrolling text. */
    Tick(5300U, 10U);
    for (i = 0; i < 60; i++) {
        Tick(10U, 10U);
        /* '4' glyph col 4 at x=1+3 is 0x7F → rows 0..6 lit at bit 4 */
        if (((s_frame[0] & (1U << 4)) != 0U) && ((s_frame[6] & (1U << 4)) != 0U) &&
            ((s_frame[7] == 0U))) {
            saw_digit = 1;
        }
    }
    CHECK(saw_digit, "last 5 s should show the countdown digit");
}

static void Test_RescueFailThenRescued(void)
{
    uint32_t t0;

    Boot();
    s_st.mode = BATTLE_MODE;
    Tick(1000U, 10U);
    s_st.mode = THE_LAST_OF_2028;
    s_last_entry = g_now;
    Tick(1500U, 10U);

    t0 = g_now;
    Enhancements_OnEvent(DRONE_EVENT_RESCUE_FAILED, g_now);
    Tick(320U, 10U);
    CHECK(OnTime(t0, t0 + 300U) >= 280U, "rescue-fail tone should hold ~300 ms over the countdown, got %u",
          (unsigned)OnTime(t0, t0 + 300U));
    Tick(2000U, 10U);
    CHECK(Beeps(t0 + 400U, g_now) >= 1, "countdown should resume after the fail tone");

    t0 = g_now;
    s_st.mode = BATTLE_MODE; /* upright double press */
    Tick(800U, 10U);
    CHECK(Beeps(t0, t0 + 800U) == 3, "rescue OK should be 3 pulses, got %d", Beeps(t0, t0 + 800U));
    CHECK(OnTime(t0, t0 + 800U) >= 500U, "rescue OK ends in a long tone, on %u ms",
          (unsigned)OnTime(t0, t0 + 800U));
    Tick(2000U, 10U);
    CHECK(Beeps(t0 + 800U, g_now) == 0, "no countdown beeps after rescue");
}

static void Test_SelfDestructEffects(void)
{
    Boot();
    s_st.mode = BATTLE_MODE;
    Tick(1000U, 10U);
    s_st.mode = THE_LAST_OF_2028;
    s_last_entry = g_now;
    Tick(10000U, 10U);
    s_st.mode = SELF_DESTRUCT_TRIGGERED;
    Enhancements_OnEvent(DRONE_EVENT_SELF_DESTRUCT_STARTED, g_now);
    Tick(100U, 10U);
    CHECK(!FrameIs(0x00U), "explosion frames should show first");
    Tick(1600U, 10U);
    CHECK(Enhancements_FinalEffectsDone(g_now) == 1U, "final effects should finish");
    CHECK(FrameIs(0x00U), "matrix blank after explosion");
    CHECK(s_buzz_on == 0U, "buzzer silent after final burst");
}

/* Status display directly: radar priority + styles (the adapter only feeds
 * azimuth from a live RD-03D lock). */
static void Test_RadarPriority(void)
{
    static HT16K33_HandleTypeDef dev;
    StatusDisplay_HandleTypeDef h;
    uint32_t t = 5000U;
    int saw_line = 0;
    int saw_blank = 0;
    int saw_full = 0;
    int i;

    (void)HT16K33_Init(&dev, &s_i2c, HT16K33_I2C_ADDR_DEFAULT);
    StatusDisplay_Init(&h, &dev);
    StatusDisplay_SetCharge(&h, 10U);
    StatusDisplay_SetArmed(&h, 1U);
    StatusDisplay_SetAzimuth(&h, 5U); /* locked zombie at column 5 */
    for (i = 0; i < 40; i++) {
        t += 10U;
        (void)StatusDisplay_Update(&h, t);
        saw_line |= FrameIsColumn(5U);
        saw_blank |= FrameIs(0x00U);
        saw_full |= FrameIs(0xFFU);
    }
    CHECK(saw_line && saw_blank, "locked + armed: target line should blink");
    CHECK(!saw_full, "locked target must beat the armed full flash");

    StatusDisplay_SetArmed(&h, 0U);
    t += 10U;
    (void)StatusDisplay_Update(&h, t);
    CHECK(FrameIsColumn(5U), "locked, not armed: steady line beats the charge bar");

    StatusDisplay_SetRadarStyle(&h, STATUS_RADAR_DIAL);
    t += 10U;
    (void)StatusDisplay_Update(&h, t);
    CHECK(((s_frame[7] & 0x18U) == 0x18U) && !FrameIsColumn(5U), "standby dial: pivot at bottom centre");

    StatusDisplay_SetAzimuth(&h, 0xFFU); /* lock lost */
    t += 10U;
    (void)StatusDisplay_Update(&h, t);
    CHECK(FrameIs(0xFFU), "no target: back to the charge bar");
}

int main(void)
{
    Test_StandbyShowsChargeNotDead();
    Test_BattleEntryCueAndHud();
    Test_RechargeAndDenied();
    Test_CountdownBeepsAccelerate(10U);
    Test_CountdownBeepsAccelerate(35U); /* slower loop: sensor reads + delay */
    Test_LastBannerThenDigits();
    Test_RescueFailThenRescued();
    Test_SelfDestructEffects();
    Test_RadarPriority();

    if (s_fail != 0) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("OK - matrix HUD + buzzer integration passed\n");
    return 0;
}
