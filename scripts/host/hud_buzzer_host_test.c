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

/* Test output goes to stderr: stdout carries the firmware's telemetry lines
 * (telemetry_evt.c's host fallback prints them), redirected to a log file. */
static int s_fail;
#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);           \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
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

static void FeedRadar(void);

static void Tick(uint32_t ms, uint32_t step)
{
    uint32_t end = g_now + ms;
    while (g_now < end) {
        g_now += step;
        FeedRadar();
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

/* ------------------------------------------------------------ fake RD-03D + laser */

static uint8_t s_rx[4096];
static uint16_t s_rx_head, s_rx_tail;
static int s_giant_present;     /* a person stands in front of the radar */
static uint32_t s_next_frame_ms;
static uint8_t s_laser_on;

static uint16_t FakeRadarRead(void *ctx, uint8_t *buf, uint16_t max)
{
    uint16_t n = 0U;
    (void)ctx;
    while ((n < max) && (s_rx_tail != s_rx_head)) {
        buf[n++] = s_rx[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % sizeof(s_rx));
    }
    return n;
}

static void FakeRadarWrite(void *ctx, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
}

static void FakeLaser(void *ctx, uint8_t on)
{
    (void)ctx;
    s_laser_on = (on != 0U) ? 1U : 0U;
}

/* RD-03D target encoding: sign bit set = positive. */
static void EncSigned(int v, uint8_t *lo, uint8_t *hi)
{
    unsigned mag = (unsigned)((v < 0) ? -v : v) & 0x7FFFU;
    if (v > 0) {
        mag |= 0x8000U;
    }
    *lo = (uint8_t)(mag & 0xFFU);
    *hi = (uint8_t)(mag >> 8);
}

/* One 30-byte frame with a single target at (x, y) mm, ~10 Hz like the module. */
static void PushRadarFrame(int x_mm, int y_mm)
{
    uint8_t f[RD03D_FRAME_LEN];
    uint16_t i;
    memset(f, 0, sizeof(f));
    f[0] = 0xAA; f[1] = 0xFF; f[2] = 0x03; f[3] = 0x00;
    EncSigned(x_mm, &f[4], &f[5]);
    EncSigned(y_mm, &f[6], &f[7]);
    f[10] = 0x68; f[11] = 0x01;
    f[28] = 0x55; f[29] = 0xCC;
    for (i = 0U; i < RD03D_FRAME_LEN; i++) {
        s_rx[s_rx_head] = f[i];
        s_rx_head = (uint16_t)((s_rx_head + 1U) % sizeof(s_rx));
    }
}

static void FeedRadar(void)
{
    while ((int32_t)(g_now - s_next_frame_ms) >= 0) {
        if (s_giant_present) {
            PushRadarFrame(0, 2000); /* 2 m straight ahead */
        }
        s_next_frame_ms += 100U;
    }
}

static void Boot(void)
{
    static HT16K33_HandleTypeDef dev;
    static Buzzer_Backend_t buzz;
    static RD03D_Transport_t radar;
    static Laser_Backend_t laser;
    Enhancements_Config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    memset(s_frame, 0, sizeof(s_frame));
    s_edges = 0;
    s_buzz_on = 0U;
    g_now = 1000U;
    s_rx_head = s_rx_tail = 0U;
    s_giant_present = 0;
    s_next_frame_ms = g_now;
    s_laser_on = 0U;

    if (HT16K33_Init(&dev, &s_i2c, HT16K33_I2C_ADDR_DEFAULT) != HAL_OK) {
        CHECK(0, "HT16K33_Init failed on stub bus");
    }
    buzz.set_tone = FakeTone;
    buzz.ctx = NULL;
    radar.read = FakeRadarRead;
    radar.write = FakeRadarWrite;
    radar.ctx = NULL;
    laser.set_output = FakeLaser;
    laser.ctx = NULL;
    cfg.matrix = &dev;
    cfg.buzzer = &buzz;
    cfg.radar = &radar;
    cfg.laser = &laser;
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

    /* BATTLE MODE scroll (~3.6 s), then the armed charge bar blinks. Charge 6
     * = 5 of 8 columns, so a bar flash is distinguishable from a full flash. */
    s_st.charge = 6U;
    Tick(4000U, 10U);
    for (i = 0; i < 20; i++) {
        Tick(10U, 10U);
        if (FrameIs(0x1FU)) {
            saw_on = 1;
        }
        if (FrameIs(0x00U)) {
            saw_off = 1;
        }
        CHECK(!FrameIs(0xFFU), "armed must not flash the whole matrix");
    }
    CHECK(saw_on && saw_off, "armed with no target should blink the charge bar");
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
    CHECK(!saw_full, "locked target must beat the armed charge-bar flash");

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

/* ------------------------------------------------------------ telemetry log */

static const char *s_log_path;
static long s_log_mark;

/* Remember where the log is now; LogCount() then looks only at newer lines. */
static void LogMark(void)
{
    fflush(stdout);
    s_log_mark = ftell(stdout);
}

static int LogCount(const char *needle)
{
    char line[256];
    int n = 0;
    FILE *f;
    fflush(stdout);
    f = fopen(s_log_path, "rb");
    if (f == NULL) {
        return -1;
    }
    (void)fseek(f, s_log_mark, SEEK_SET);
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strstr(line, needle) != NULL) {
            n++;
        }
    }
    fclose(f);
    return n;
}

/* Giant cannon: radar lock = giant, physical laser = cannon beam, 2 hits. */
static void Test_GiantCannon(void)
{
    uint32_t t0;
    int saw_burst = 0;
    int i;

    Boot();
    s_st.mode = BATTLE_MODE;
    s_st.charge = 10U;
    Tick(1000U, 10U);
    CHECK(Enhancements_CannonReady() == 0U, "no giant in front of the radar: cannon not ready");

    LogMark();
    s_giant_present = 1;
    Tick(1200U, 10U);
    CHECK(Enhancements_CannonReady() == 1U, "giant locked: cannon ready");
    CHECK(LogCount("EVT RADAR az=") >= 1 && LogCount("lock=1") >= 1, "lock reported on telemetry");
    CHECK(s_laser_on == 0U, "laser stays OFF while locked (no aim dot; laser = cannon only)");

    /* Gun shots never light the physical laser any more. */
    Enhancements_OnEvent(DRONE_EVENT_LASER_FIRED, g_now);
    Tick(200U, 10U);
    CHECK(s_laser_on == 0U, "laser gun shot does not light the cannon laser");
    Tick(600U, 10U);

    /* Shot 1: beam on the giant for ~1 s, giant wounded 1/2. */
    LogMark();
    t0 = g_now;
    Enhancements_OnEvent(DRONE_EVENT_CANNON_FIRED, g_now);
    Tick(50U, 10U);
    CHECK(s_laser_on == 1U, "cannon beam lights the laser on the locked giant");
    Tick(800U, 10U);
    CHECK(s_laser_on == 1U, "beam still on at 0.85 s");
    Tick(300U, 10U);
    CHECK(s_laser_on == 0U, "beam off after EE_CANNON_BEAM_MS");
    CHECK(LogCount("EVT GIANT hit=1/2") == 1, "first hit reported");
    CHECK(LogCount("EVT GIANT down") == 0, "giant survives the first hit");
    CHECK(OnTime(t0, t0 + 900U) >= 600U, "cannon cue ends in a long blast, on %u ms",
          (unsigned)OnTime(t0, t0 + 900U));
    CHECK(Enhancements_CannonReady() == 1U, "same giant still locked after a hit");
    Tick(1000U, 10U);

    /* Shot 2: giant down; burst on the matrix; lock dropped → new giant. */
    LogMark();
    Enhancements_OnEvent(DRONE_EVENT_CANNON_FIRED, g_now);
    for (i = 0; i < 40; i++) {
        Tick(10U, 10U);
        if ((s_frame[0] == 0x18U) || (s_frame[0] == 0x81U)) {
            saw_burst = 1; /* explosion frames */
        }
    }
    CHECK(saw_burst, "giant down shows the burst on the matrix");
    CHECK(LogCount("EVT GIANT down") == 1, "second hit destroys the giant");
    Tick(800U, 10U);
    CHECK(LogCount("EVT RADAR lost") >= 1, "lock dropped after the giant falls");
    Tick(1000U, 10U);
    CHECK(Enhancements_CannonReady() == 1U, "person still there: re-locked as a NEW giant");

    LogMark();
    Enhancements_OnEvent(DRONE_EVENT_CANNON_FIRED, g_now);
    Tick(100U, 10U);
    CHECK(LogCount("EVT GIANT hit=1/2") == 1, "new giant starts at full HP");
    Tick(1200U, 10U);

    /* Giant walks off: lock lost resets HP; a shot without lock is a miss. */
    s_giant_present = 0;
    Tick(1500U, 10U);
    CHECK(Enhancements_CannonReady() == 0U, "lock lost when the giant leaves");
    LogMark();
    Enhancements_OnEvent(DRONE_EVENT_CANNON_FIRED, g_now);
    Tick(100U, 10U);
    CHECK(LogCount("EVT GIANT miss") == 1, "shot without a lock is a miss");
    CHECK(s_laser_on == 0U, "no beam without a lock");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: hud_buzzer_test <telemetry.log>\n");
        return 2;
    }
    s_log_path = argv[1];
    if (freopen(s_log_path, "w+", stdout) == NULL) {
        fprintf(stderr, "cannot open %s\n", s_log_path);
        return 2;
    }

    Test_StandbyShowsChargeNotDead();
    Test_BattleEntryCueAndHud();
    Test_RechargeAndDenied();
    Test_CountdownBeepsAccelerate(10U);
    Test_CountdownBeepsAccelerate(35U); /* slower loop: sensor reads + delay */
    Test_LastBannerThenDigits();
    Test_RescueFailThenRescued();
    Test_SelfDestructEffects();
    Test_RadarPriority();
    Test_GiantCannon();

    if (s_fail != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_fail);
        return 1;
    }
    fprintf(stderr, "OK - matrix HUD + buzzer + giant cannon integration passed\n");
    return 0;
}
