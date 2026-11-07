/**
 * Host regression for the radar → tracker → laser-aim chain (no HAL).
 * Build + run:  node scripts/test_radar_aim_host.mjs
 *   (or: gcc -std=c99 -Wall -Wextra -IApp/Inc App/Src/rd03d.c
 *        App/Src/radar_track.c App/Src/laser_aim.c
 *        scripts/host/radar_aim_host_test.c -lm -o radar_aim_test)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd03d.h"
#include "radar_track.h"
#include "laser_aim.h"

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
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (double)(tol))

/* ---------------------------------------------------------------- helpers */

static void EncSigned(int v, uint8_t *lo, uint8_t *hi)
{
    unsigned mag = (unsigned)((v < 0) ? -v : v) & 0x7FFFU;
    if (v >= 0 && v != 0) {
        mag |= 0x8000U;
    }
    *lo = (uint8_t)(mag & 0xFFU);
    *hi = (uint8_t)(mag >> 8);
}

typedef struct { int x, y, speed; } T3;

static void BuildFrame(uint8_t f[RD03D_FRAME_LEN], const T3 *t, int n)
{
    int i;
    memset(f, 0, RD03D_FRAME_LEN);
    f[0] = 0xAA; f[1] = 0xFF; f[2] = 0x03; f[3] = 0x00;
    for (i = 0; i < n && i < 3; i++) {
        uint8_t *b = &f[4 + i * 8];
        EncSigned(t[i].x, &b[0], &b[1]);
        EncSigned(t[i].y, &b[2], &b[3]);
        EncSigned(t[i].speed, &b[4], &b[5]);
        b[6] = 0x68; b[7] = 0x01; /* 360 mm resolution */
    }
    f[28] = 0x55; f[29] = 0xCC;
}

static int Feed(RD03D_Parser_t *p, const uint8_t *bytes, int n, RD03D_Frame_t *out)
{
    int frames = 0, i;
    for (i = 0; i < n; i++) {
        frames += RD03D_PushByte(p, bytes[i], out);
    }
    return frames;
}

/* Push one frame of targets through parser + tracker. */
static void Step(RadarTrack_HandleTypeDef *trk, const T3 *t, int n, uint32_t now)
{
    uint8_t f[RD03D_FRAME_LEN];
    RD03D_Parser_t p;
    RD03D_Frame_t fr;
    RD03D_ParserInit(&p);
    BuildFrame(f, t, n);
    if (Feed(&p, f, RD03D_FRAME_LEN, &fr) == 1) {
        RadarTrack_OnFrame(trk, &fr, now);
    }
    RadarTrack_Update(trk, now);
}

/* ---------------------------------------------------------------- parser */

static void TestParser(void)
{
    uint8_t f[RD03D_FRAME_LEN];
    uint8_t stream[3 + RD03D_FRAME_LEN * 2];
    uint8_t cmd[RD03D_CMD_LEN];
    static const uint8_t kMulti[RD03D_CMD_LEN] = {0xFD, 0xFC, 0xFB, 0xFA, 0x02, 0x00,
                                                  0x90, 0x00, 0x04, 0x03, 0x02, 0x01};
    RD03D_Parser_t p;
    RD03D_Frame_t fr;
    T3 t[2] = {{-782, 1713, -16}, {300, 2500, 0}};

    /* Datasheet example bytes: 0E 03 → -782 mm, B1 86 → +1713 mm, 10 00 → -16 cm/s */
    CHECK(RD03D_DecodeSigned(0x0E, 0x03) == -782, "x decode %d", RD03D_DecodeSigned(0x0E, 0x03));
    CHECK(RD03D_DecodeSigned(0xB1, 0x86) == 1713, "y decode %d", RD03D_DecodeSigned(0xB1, 0x86));
    CHECK(RD03D_DecodeSigned(0x10, 0x00) == -16, "speed decode");
    CHECK(RD03D_DecodeSigned(0x00, 0x80) == 0 && RD03D_DecodeSigned(0x00, 0x00) == 0, "both zeros");

    RD03D_ParserInit(&p);
    BuildFrame(f, t, 2);
    stream[0] = 0x12; stream[1] = 0xAA; stream[2] = 0xAA; /* noise + false header start */
    memcpy(&stream[3], f, RD03D_FRAME_LEN);
    memcpy(&stream[3 + RD03D_FRAME_LEN], f, RD03D_FRAME_LEN);
    CHECK(Feed(&p, stream, (int)sizeof(stream), &fr) == 2, "resync then two frames, ok=%lu", (unsigned long)p.frames_ok);
    CHECK(fr.count == 2, "count=%u", fr.count);
    CHECK(fr.target[0].x_mm == -782 && fr.target[0].y_mm == 1713 && fr.target[0].speed_cms == -16, "t0");
    CHECK(fr.target[0].res_mm == 360, "res");
    CHECK(fr.target[1].valid == 1 && fr.target[1].speed_cms == 0, "stationary target stays valid");
    CHECK(fr.target[2].valid == 0, "empty slot invalid");

    /* Sentinel speed marks a placeholder slot */
    {
        T3 s[1] = {{100, 1000, 256}};
        BuildFrame(f, s, 1);
        RD03D_ParserInit(&p);
        Feed(&p, f, RD03D_FRAME_LEN, &fr);
        CHECK(fr.count == 0, "speed sentinel rejected");
    }

    /* Bad footer */
    RD03D_ParserInit(&p);
    BuildFrame(f, t, 1);
    f[29] = 0x00;
    CHECK(Feed(&p, f, RD03D_FRAME_LEN, &fr) == 0 && p.frames_bad == 1, "bad footer dropped");

    RD03D_BuildTrackingModeCmd(cmd, 1U);
    CHECK(memcmp(cmd, kMulti, RD03D_CMD_LEN) == 0, "multi-target command bytes");
    RD03D_BuildTrackingModeCmd(cmd, 0U);
    CHECK(cmd[6] == 0x80 && cmd[7] == 0x00, "single-target command word");
}

/* ---------------------------------------------------------------- tracker */

static void TestTracker(void)
{
    RadarTrack_Config_t cfg;
    RadarTrack_HandleTypeDef trk;
    const RadarTarget_t *tg;
    uint32_t now = 1000;
    int i;
    T3 z = {500, 2000, -20};

    RadarTrack_DefaultConfig(&cfg);
    RadarTrack_Init(&trk, &cfg);
    tg = RadarTrack_Target(&trk);

    Step(&trk, &z, 1, now);
    CHECK(tg->valid && !tg->locked, "tentative after 1 hit");
    CHECK(RadarTrack_PollEvent(&trk) == RADAR_TRACK_EVT_NONE, "no event yet");
    CHECK(RadarTrack_MatrixColumn(&trk) == 0xFF, "no column until locked");
    for (i = 0; i < 2; i++) {
        now += 100;
        Step(&trk, &z, 1, now);
    }
    CHECK(tg->locked, "locked after 3 hits");
    CHECK(RadarTrack_PollEvent(&trk) == RADAR_TRACK_EVT_LOCK, "lock event");
    CHECK(NEAR(tg->azimuth_deg, 14.04, 0.2), "az=%f", tg->azimuth_deg);
    CHECK(NEAR(tg->range_mm, 2061.6, 2.0), "range=%f", tg->range_mm);
    CHECK(RadarTrack_MatrixColumn(&trk) == 4, "col=%u", RadarTrack_MatrixColumn(&trk));

    /* Zombie walks right: the smoothed azimuth follows */
    for (i = 0; i < 15; i++) {
        now += 100;
        z.x += 60;
        Step(&trk, &z, 1, now);
    }
    CHECK(NEAR(tg->azimuth_deg, atan2(z.x, z.y) * 57.2958, 2.0), "follows walker az=%f", tg->azimuth_deg);
    CHECK(tg->vx_mm_s > 300.0f, "velocity estimate vx=%f", tg->vx_mm_s);

    /* Short dropout coasts, long dropout → LOST */
    now += 300;
    Step(&trk, NULL, 0, now);
    CHECK(tg->locked, "coasts through 300 ms dropout");
    now += 500;
    Step(&trk, NULL, 0, now);
    CHECK(!tg->valid && !tg->locked, "lost after timeout");
    CHECK(RadarTrack_PollEvent(&trk) == RADAR_TRACK_EVT_LOST, "lost event");

    /* FOV / range gates */
    {
        T3 wide = {3000, 1000, 10};  /* ~71.6° */
        T3 close = {0, 250, 10};     /* inside min range */
        T3 behind = {200, -800, 10};
        RadarTrack_Reset(&trk);
        (void)RadarTrack_PollEvent(&trk);
        Step(&trk, &wide, 1, now += 100);
        Step(&trk, &close, 1, now += 100);
        Step(&trk, &behind, 1, now += 100);
        CHECK(!tg->valid, "gated returns ignored");
    }

    /* Closer zombie steals the lock; a slightly closer one does not */
    {
        T3 two[2] = {{0, 3000, 5}, {-400, 2700, 5}};
        RadarTrack_Reset(&trk);
        for (i = 0; i < 4; i++) {
            T3 one = {0, 3000, 5};
            Step(&trk, &one, 1, now += 100);
        }
        (void)RadarTrack_PollEvent(&trk);
        Step(&trk, two, 2, now += 100);
        CHECK(tg->locked && NEAR(tg->x_mm, 0, 50), "300 mm closer: keeps lock");
        two[1].y = 1500;
        two[1].x = -800;
        Step(&trk, two, 2, now += 100);
        CHECK(RadarTrack_PollEvent(&trk) == RADAR_TRACK_EVT_LOST, "switch drops the old lock");
        CHECK(tg->valid && !tg->locked && NEAR(tg->x_mm, -800, 1), "re-acquires the nearer zombie");
        for (i = 0; i < 2; i++) {
            Step(&trk, two, 2, now += 100);
        }
        CHECK(tg->locked && tg->azimuth_deg < -25.0f, "nearer zombie locked az=%f", tg->azimuth_deg);
    }

    /* Mirror flips the lateral axis */
    {
        T3 r = {500, 2000, 0};
        cfg.mirror_x = -1;
        RadarTrack_Init(&trk, &cfg);
        Step(&trk, &r, 1, now += 100);
        CHECK(tg->azimuth_deg < -13.0f, "mirror_x=-1 az=%f", tg->azimuth_deg);
    }
}

/* ---------------------------------------------------------------- aim */

static uint16_t s_pulse;
static int s_pulse_writes;
static uint8_t s_laser;
static void SetPulse(void *ctx, uint16_t us) { (void)ctx; s_pulse = us; s_pulse_writes++; }
static void SetLaser(void *ctx, uint8_t on) { (void)ctx; s_laser = on; }

static void TestAim(void)
{
    LaserAim_Config_t cfg;
    LaserAim_HandleTypeDef h;
    Servo_Backend_t sb = {SetPulse, NULL};
    Laser_Backend_t lb = {SetLaser, NULL};
    RadarTarget_t t;
    uint32_t now = 0;
    int i, toggles;
    uint8_t prev;

    LaserAim_DefaultConfig(&cfg);
    s_laser = 1;
    LaserAim_Init(&h, &sb, &lb, &cfg);
    CHECK(s_laser == 0 && s_pulse == 1500, "init: laser off, servo centred");

    /* Pulse mapping + clamps */
    CHECK(LaserAim_DegToPulse(&cfg, 90.0f) == 2400, "clamp max");
    CHECK(LaserAim_DegToPulse(&cfg, 45.0f) == 2000, "45° = 2000 us");
    cfg.direction = -1;
    CHECK(LaserAim_DegToPulse(&cfg, 45.0f) == 1000, "reversed horn");
    cfg.direction = 1;

    /* Parallax: laser 60 mm right of the radar sees a boresight target on its left */
    cfg.offset_x_mm = 60.0f;
    CHECK(NEAR(LaserAim_GoalDeg(&cfg, 0.0f, 1000.0f), -3.43, 0.05), "parallax at 1 m");
    CHECK(NEAR(LaserAim_GoalDeg(&cfg, 0.0f, 5000.0f), -0.69, 0.05), "parallax at 5 m");
    cfg.offset_x_mm = 0.0f;
    LaserAim_Init(&h, &sb, &lb, &cfg);

    memset(&t, 0, sizeof(t));
    t.valid = 1; t.locked = 1;
    t.x_mm = 1154.7f; t.y_mm = 2000.0f; /* 30° right */

    /* Slew: 180 °/s → 18° after 100 ms, laser stays dark until aligned */
    LaserAim_Update(&h, &t, 1, 1, now);
    for (i = 0; i < 5; i++) {
        now += 20;
        LaserAim_Update(&h, &t, 1, 1, now);
    }
    CHECK(NEAR(h.cmd_deg, 18.0, 0.5), "slew-limited cmd=%f", h.cmd_deg);
    CHECK(s_laser == 0, "laser off while slewing");
    for (i = 0; i < 10; i++) {
        now += 20;
        LaserAim_Update(&h, &t, 1, 1, now);
    }
    CHECK(NEAR(h.cmd_deg, 30.0, 0.1) && h.aligned, "aligned cmd=%f", h.cmd_deg);
    CHECK(s_laser == 1, "laser on when locked + aligned in Battle");
    CHECK(s_pulse == LaserAim_DegToPulse(&cfg, 30.0f), "servo pulse at 30°");

    /* Standby: servo tracks but laser off */
    LaserAim_Update(&h, &t, 1, 0, now += 20);
    CHECK(s_laser == 0 && h.state == LASER_AIM_TRACKING, "standby: track, no laser");

    /* Fire strobe toggles only while aimed */
    LaserAim_Update(&h, &t, 1, 1, now += 20);
    LaserAim_Fire(&h, now);
    prev = s_laser;
    toggles = 0;
    for (i = 0; i < 20; i++) {
        now += 10;
        LaserAim_Update(&h, &t, 1, 1, now);
        if (s_laser != prev) {
            toggles++;
            prev = s_laser;
        }
    }
    CHECK(toggles >= 4, "fire strobe toggles=%d", toggles);
    for (i = 0; i < 30; i++) {
        now += 10;
        LaserAim_Update(&h, &t, 1, 1, now);
    }
    CHECK(s_laser == 1 && !h.firing, "steady after strobe");

    /* Out of reach: clamp at limit, laser stays dark */
    t.x_mm = 8000.0f; t.y_mm = 1000.0f; /* ~83° */
    for (i = 0; i < 60; i++) {
        now += 20;
        LaserAim_Update(&h, &t, 1, 1, now);
    }
    CHECK(NEAR(h.cmd_deg, cfg.pan_max_deg, 0.1) && !h.reachable, "clamped at pan limit");
    CHECK(s_laser == 0, "no laser outside reach");

    /* Lost: laser off immediately, hold, then park */
    t.x_mm = 0.0f; t.y_mm = 2000.0f;
    for (i = 0; i < 60; i++) {
        now += 20;
        LaserAim_Update(&h, &t, 1, 1, now);
    }
    CHECK(s_laser == 1, "re-aimed at boresight");
    t.locked = 0; t.valid = 0;
    LaserAim_Update(&h, &t, 1, 1, now += 20);
    CHECK(s_laser == 0 && h.state == LASER_AIM_HOLDING, "lost → laser off, holding");
    now += cfg.hold_ms;
    LaserAim_Update(&h, &t, 1, 1, now);
    CHECK(h.state == LASER_AIM_PARKED, "parked after hold");

    /* Safe forces everything off; strobe cannot light it */
    t.locked = 1; t.valid = 1;
    LaserAim_Update(&h, &t, 0, 1, now += 20);
    LaserAim_Fire(&h, now);
    LaserAim_Update(&h, &t, 0, 1, now += 10);
    CHECK(s_laser == 0 && h.state == LASER_AIM_SAFE, "mode gate blocks laser + fire");
    LaserAim_Safe(&h);
    CHECK(s_laser == 0 && s_pulse == 1500, "safe: off + neutral");
}

int main(void)
{
    TestParser();
    TestTracker();
    TestAim();
    if (s_fail != 0) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("OK - RD-03D parser, tracker and laser aim passed\n");
    return 0;
}
