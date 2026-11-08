/******************************************************************************
 * @file  : main.c
 * @brief : Main program body
 * (c) EE2028 Teaching Team
 *
 * Basic-requirements drone logic (heading, 0..50 laser charge, thresholds)
 * plus the VESPER telemetry / enhancements hooks.
 *
 * TIMING RULE: this file (and everything under Core/ and App/) never calls
 * HAL_Delay(). All timing is non-blocking HAL_GetTick() deltas.
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "drone.h"
#include "telemetry_evt.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_accelero.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_tsensor.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_gyro.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_hsensor.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_magneto.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_psensor.h"
#include <stdbool.h>
#include <string.h>
#include <math.h> /* heading filter: needs -lm */
/* #include <stdio.h> — add back only for CubeIDE-console debug printf */

#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
#include "enhancements.h"
#include "enhancements_hw.h"
/* HT16K33 matrix bus. Default: the adapter brings up I2C1 on D15/D14 itself
 * (the lab project has no MX_I2C1_Init). Override with -DEE_MATRIX_I2C=NULL
 * to skip the matrix, or point it at a CubeMX handle (e.g. &hi2c1). */
#ifndef EE_MATRIX_I2C
#define EE_MATRIX_I2C Enhancements_HW_MatrixI2C()
#endif
/* One-shot drone actions the HUD / buzzer react to (no-op without enhancements). */
#define EE_EVENT(evt) Enhancements_OnEvent((evt), HAL_GetTick())
/* Giant cannon available: the radar has a giant locked. */
#define EE_CANNON_READY() (Enhancements_CannonReady() != 0U)
#else
#define EE_EVENT(evt) ((void)0)
#define EE_CANNON_READY() (0)
#endif

/* CubeIDE console debug only (printf) — mission / VESPER traffic uses Telemetry_* over UART. */
extern void initialise_monitor_handles(void); /* semihosting printf */

/* Define thresholds */
#define THRESHOLD_A 20.00f    /* m/s^2 */
#define THRESHOLD_T 50.00f    /* deg C */
#define THRESHOLD_G 1000.00f  /* dps */
#define THRESHOLD_H 30.00f    /* Minimum humidity threshold (%) */
#define THRESHOLD_M 10000.00f /* mgauss, on the calibrated vector magnitude */
#define THRESHOLD_P 900.00f   /* hPa */

/* ===================== USER SETTINGS ===================== */
#define MAG_CAL_MODE   0
#define MAG_OFFSET_X   -190.5f
#define MAG_OFFSET_Y   -1883.0f
#define MAG_OFFSET_Z   -137.5f
#define MAG_SCALE_X    0.789f
#define MAG_SCALE_Y    0.787f
#define MAG_SCALE_Z    2.165f
#define MAG_MAP        0
#define YAW_GYRO_SIGN  (-1.0f)
#define YAW_DEBUG      0
/* ========================================================= */

/* Yaw filter tuning */
#define YAW_TAU_FAST_S      0.15f
#define YAW_TAU_SLOW_S      1.5f
#define YAW_STILL_DPS       2.0f
#define YAW_BOOT_FAST_MS    2000U
#define YAW_BIG_ERR_DEG     30.0f
#define HEADING_LPF_TAU_S   0.05f
#define GYRO_BIAS_TRACK_S   20.0f

/* Push-button timing (USER button is active-low) */
#define PB_DEBOUNCE_MS           100U
#define PB_DOUBLE_PRESS_MS       500U
#define PB_HOLD_MS               500U
#define PB_FIRE_PERIOD_MS        500U
#define ATTACK_MSG_PERIOD_MS     1000U

/* Laser gun energy (official 0..50 scale; telemetry reports the same scale) */
#define LASER_CHARGE_MAX   50
#define LASER_CHARGE_STEP   3
#define LASER_FIRE_MIN     25
#define LASER_SHOT_COST     5
/* Giant cannon: each shot costs 10 charge and needs at least 10. A giant takes
 * 2 hits, so killing one costs 2 x 10 = 20 charge. */
#define CANNON_CHARGE_COST 10

_Static_assert(LASER_CHARGE_MAX <= 255, "charge is cast to uint8_t for telemetry");
_Static_assert(LASER_FIRE_MIN >= LASER_SHOT_COST, "a shot must never take charge below 0");

#ifndef RESCUE_TIMEOUT_MS
#define RESCUE_TIMEOUT_MS        10000U /* 10s without rescue → self-destruct */
#endif
#define RESCUE_ATTEMPT_LIMIT     5

#ifndef EE_SERVO_SELFTEST
#define EE_SERVO_SELFTEST 0 /* 1 = run the non-blocking D5 servo sweep after boot */
#endif

/* DroneState_t comes from drone.h (shared with the enhancements adapter). */
DroneState_t current_state = STANDBY_MODE;

/*
 * Push button: EXTI captures rising+falling edges; main loop reads GPIO level
 * to confirm press/hold/release and measures the falling→rising duration window.
 * Configure USER button EXTI as rising AND falling (or poll still recovers release).
 */
volatile uint32_t pb_last_edge_tick = 0;
volatile bool pb_edge_pending = false;
volatile uint32_t pb_edge_tick = 0;

bool pb_is_down = false;
uint32_t pb_down_tick = 0;
uint32_t pb_last_press_duration_ms = 0;
bool pb_hold_latched = false;
bool pb_low_charge_warned = false;
uint32_t pb_last_fire_tick = 0;

bool pb_single_armed = false;
uint32_t pb_single_deadline_tick = 0;
uint32_t pb_last_short_press_tick = 0;

float current_accel_z = 0.0f;
int the_last_of_2028_rescue_attempts = 0;

/* Laser Gun Energy Tracking (starts full, remembers last charge) */
int laser_charge = LASER_CHARGE_MAX;

static int Laser_Clamp(int c)
{
    if (c < 0) return 0;
    if (c > LASER_CHARGE_MAX) return LASER_CHARGE_MAX;
    return c;
}

static void Laser_AddCharge(void)
{
    laser_charge = Laser_Clamp(laser_charge + LASER_CHARGE_STEP);
}

static void Laser_UseShot(void)
{
    laser_charge = Laser_Clamp(laser_charge - LASER_SHOT_COST);
}

/* Timers & State Trackers */
uint32_t tick_1s_telemetry = 0;
uint32_t tick_1s_attack_msg = 0;
uint32_t led_blink_tick = 0;
uint32_t the_last_of_2028_entry_tick = 0;
uint32_t noble_sacrifice_tick = 0;
bool sacrifice_sent = false;
bool cheerio_sent = false;

/* Prototypes */
void Check_And_Log_Thresholds(float t, float p, float h, float a[3], float g[3], float m[3]);
void Process_PushButton(void);
void PB_OnDoublePress(void);
void PB_OnSinglePress(void);
void PB_CancelPendingSingle(void);
void Enter_The_Last_Of_2028(void);
void Enter_Self_Destruct(const char *reason);
static void Telemetry_SendSnapshot(void);
#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
static void Fill_DroneStatus(DroneStatus_t *out);
static void Enhancements_Tick(void);
#endif

/* Heading support */
static float filtered_yaw_deg = 0.0f;
static bool  yaw_initialized  = false;
static float gyro_bias[3]     = { 0.0f, 0.0f, 0.0f };
static float g_roll_deg = 0.0f, g_pitch_deg = 0.0f;
static int   g_yaw_deg = 0;
static float g_mag_yaw_deg = 0.0f;

/* Averages a few gyro samples at boot — keep the board still until the loop starts. */
static void Gyro_CalibrateBias(void)
{
    float g[3];
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 5; i++) {
        BSP_GYRO_GetXYZ(g);
        sum[0] += g[0] / 1000.0f;
        sum[1] += g[1] / 1000.0f;
        sum[2] += g[2] / 1000.0f;
    }
    gyro_bias[0] = sum[0] / 5.0f;
    gyro_bias[1] = sum[1] / 5.0f;
    gyro_bias[2] = sum[2] / 5.0f;
}

static const char *Heading_Cardinal(int yaw)
{
    static const char *names[8] = { "North", "North-East", "East", "South-East",
                                   "South", "South-West", "West", "North-West" };
    return names[((yaw + 22) / 45) % 8];
}

static void Mag_ToBodyFrame(const float c[3], float *x, float *y, float *z)
{
#if   MAG_MAP == 0
    *x =  c[0]; *y =  c[1]; *z =  c[2];
#elif MAG_MAP == 1
    *x =  c[1]; *y = -c[0]; *z =  c[2];
#elif MAG_MAP == 2
    *x = -c[0]; *y = -c[1]; *z =  c[2];
#elif MAG_MAP == 3
    *x = -c[1]; *y =  c[0]; *z =  c[2];
#elif MAG_MAP == 4
    *x =  c[0]; *y = -c[1]; *z = -c[2];
#elif MAG_MAP == 5
    *x = -c[1]; *y = -c[0]; *z = -c[2];
#elif MAG_MAP == 6
    *x = -c[0]; *y =  c[1]; *z = -c[2];
#else
    *x =  c[1]; *y =  c[0]; *z = -c[2];
#endif
}

static void Heading_Update(const float a[3], const float m[3], const float g[3], float dt)
{
    static float af[3];
    static float mf[3];
    static bool  filt_init  = false;
    static uint32_t start_tick = 0;

    const float m_norm = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    if (!filt_init) {
        if (m_norm < 50.0f) return;
        for (int i = 0; i < 3; i++) {
            af[i] = a[i];
            mf[i] = m[i];
        }
        filt_init = true;
        start_tick = HAL_GetTick();
    } else {
        const float lk = dt / (HEADING_LPF_TAU_S + dt);
        for (int i = 0; i < 3; i++) {
            af[i] += lk * (a[i] - af[i]);
            mf[i] += lk * (m[i] - mf[i]);
        }
    }

    float mx, my, mz;
    Mag_ToBodyFrame(mf, &mx, &my, &mz);

    float ax = af[0], ay = af[1], az = af[2];
    const float n = sqrtf(ax * ax + ay * ay + az * az);
    if (n < 0.0001f) return;
    ax /= n; ay /= n; az /= n;

    const float dot     = mx * ax + my * ay + mz * az;
    const float north_x = mx - dot * ax;
    const float east_x  = my * az - mz * ay;

    float mag_yaw = atan2f(east_x, north_x) * (180.0f / 3.14159265f);
    if (mag_yaw < 0.0f) mag_yaw += 360.0f;
    g_mag_yaw_deg = mag_yaw;

    const float gx = g[0] - gyro_bias[0];
    const float gy = g[1] - gyro_bias[1];
    const float gz = g[2] - gyro_bias[2];
    const float g_mag = sqrtf(gx * gx + gy * gy + gz * gz);
    const bool  still = (g_mag < YAW_STILL_DPS);

    if (!yaw_initialized) {
        filtered_yaw_deg = mag_yaw;
        yaw_initialized = true;
    } else {
        float y = filtered_yaw_deg + YAW_GYRO_SIGN * gz * dt;
        y = fmodf(y, 360.0f);
        if (y < 0.0f) y += 360.0f;

        float diff = mag_yaw - y;
        if (diff < -180.0f) diff += 360.0f;
        if (diff >  180.0f) diff -= 360.0f;

        const bool booting = ((HAL_GetTick() - start_tick) < YAW_BOOT_FAST_MS);
        const bool fast = still || booting || (fabsf(diff) > YAW_BIG_ERR_DEG);
        const float tau = fast ? YAW_TAU_FAST_S : YAW_TAU_SLOW_S;

        const float k = dt / (tau + dt);
        y += k * diff;
        y = fmodf(y, 360.0f);
        if (y < 0.0f) y += 360.0f;
        filtered_yaw_deg = y;
    }

    if (still) {
        const float bk = dt / (GYRO_BIAS_TRACK_S + dt);
        gyro_bias[0] += bk * (g[0] - gyro_bias[0]);
        gyro_bias[1] += bk * (g[1] - gyro_bias[1]);
        gyro_bias[2] += bk * (g[2] - gyro_bias[2]);
    }

    g_roll_deg  = atan2f(ay, az) * (180.0f / 3.14159265f);
    g_pitch_deg = atan2f(-ax, sqrtf(ay * ay + az * az)) * (180.0f / 3.14159265f);
    int yi = (int)roundf(filtered_yaw_deg);
    if (yi >= 360) yi -= 360;
    g_yaw_deg = yi;
}

int main(void)
{
    /* Semihosting (CubeIDE console printf) only when a debugger is attached:
     * without one the semihosting call faults, so the board could not run from
     * plain USB power. Mission telemetry does not need it (UART below). */
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U) {
        initialise_monitor_handles();
    }

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Mission telemetry → ST-LINK Virtual COM (USART1 115200 8N1) for Tera Term / VESPER. */
    Telemetry_UartInit();
    /* Same telemetry over Wi-Fi (ISM43362 TCP server) when App/Inc/wifi_config.h
     * exists. Starts the join in the background; Telemetry_WifiPoll() drives it. */
    Telemetry_WifiInit(HAL_GetTick());

    /* Peripheral and Board Initializations using BSP functions */
    BSP_LED_Init(LED2);
    /* Prefer BOTH edges so rising (release) and falling (press) each generate EXTI.
     * If the BSP only enables one edge, Process_PushButton() still tracks level via GPIO. */
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

    BSP_ACCELERO_Init();
    BSP_TSENSOR_Init();
    BSP_GYRO_Init();
    BSP_HSENSOR_Init();
    BSP_MAGNETO_Init();
    BSP_PSENSOR_Init();

#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
    {
        Enhancements_Config_t ee_cfg;
        (void)Enhancements_HW_Setup(&ee_cfg, EE_MATRIX_I2C);
#if defined(EE_RADAR_AIM) && (EE_RADAR_AIM != 0)
        /* RD-03D on UART4 (D0/D1), pan servo on TIM3_CH1 (D5), laser on D7.
         * Detection + aiming only: modes, charge and hold-to-fire stay here. */
        if (Enhancements_HW_SetupRadarAim(&ee_cfg) != 0) {
            Telemetry_Printf("Radar/laser aim hardware init incomplete - continuing without it.");
        }
#if EE_SERVO_SELFTEST
        /* Bench check: arms a non-blocking servo sweep on D5 (laser stays OFF);
         * it is stepped from Enhancements_Tick(). */
        Enhancements_HW_ServoSelfTest();
#endif
#endif
        Enhancements_Init(&ee_cfg);
    }
#endif

    Telemetry_Printf("Drone initialized: Entering STANDBY MODE (Laser Charge: %d/%d)",
                     laser_charge, LASER_CHARGE_MAX);
    Telemetry_EvtMode(EVT_MODE_STANDBY);
    Telemetry_EvtCharge((uint8_t)laser_charge);

    Gyro_CalibrateBias();

    uint32_t last_tick = HAL_GetTick();

    while (1)
    {
        uint32_t current_tick = HAL_GetTick();

        /* Wi-Fi telemetry: one bounded step; on a new client resend the state. */
        Telemetry_WifiPoll(current_tick);
        if (Telemetry_WifiTakeNewClient() != 0U) {
            Telemetry_SendSnapshot();
        }

        float dt = (float)(current_tick - last_tick) / 1000.0f;
        if (dt <= 0.0f) dt = 0.01f;
        if (dt > 0.5f) dt = 0.5f;
        last_tick = current_tick;

        laser_charge = Laser_Clamp(laser_charge);

        /* --- 1. Read All Sensors --- */
        float accel_data[3];
        int16_t accel_data_i16[3] = { 0 };
        BSP_ACCELERO_AccGetXYZ(accel_data_i16);
        accel_data[0] = (float)accel_data_i16[0] * (9.81f / 1000.0f);
        accel_data[1] = (float)accel_data_i16[1] * (9.81f / 1000.0f);
        accel_data[2] = (float)accel_data_i16[2] * (9.81f / 1000.0f);
        current_accel_z = accel_data[2];

        float gyro_data[3];
        BSP_GYRO_GetXYZ(gyro_data);
        gyro_data[0] = gyro_data[0] / 1000.0f;
        gyro_data[1] = gyro_data[1] / 1000.0f;
        gyro_data[2] = gyro_data[2] / 1000.0f;

        float magneto_data[3];
        int16_t magneto_data_i16[3] = { 0 };
        BSP_MAGNETO_GetXYZ(magneto_data_i16);
        magneto_data[0] = ((float)magneto_data_i16[0] - MAG_OFFSET_X) * MAG_SCALE_X;
        magneto_data[1] = ((float)magneto_data_i16[1] - MAG_OFFSET_Y) * MAG_SCALE_Y;
        magneto_data[2] = ((float)magneto_data_i16[2] - MAG_OFFSET_Z) * MAG_SCALE_Z;

        float temp_data = BSP_TSENSOR_ReadTemp();
        float press_data = BSP_PSENSOR_ReadPressure();
        float humid_data = BSP_HSENSOR_ReadHumidity();

        const float mag_mag = sqrtf(magneto_data[0] * magneto_data[0] +
                                    magneto_data[1] * magneto_data[1] +
                                    magneto_data[2] * magneto_data[2]);

        Heading_Update(accel_data, magneto_data, gyro_data, dt);

        /* --- Global Check for "The Last of EE2028" (Upside Down) --- */
        if ((current_state == STANDBY_MODE || current_state == BATTLE_MODE) && accel_data[2] < -4.0f)
        {
            Enter_The_Last_Of_2028();
        }

        /* --- Push button: EXTI edges + GPIO level hold/release --- */
        Process_PushButton();

        /* --- 2. State Machine Handling --- */
        switch (current_state)
        {
            case STANDBY_MODE:
                BSP_LED_On(LED2);

                {
                    bool tag_exceeded = (temp_data > THRESHOLD_T) ||
                        (accel_data[0] > THRESHOLD_A || accel_data[1] > THRESHOLD_A || accel_data[2] > THRESHOLD_A) ||
                        (gyro_data[0] > THRESHOLD_G || gyro_data[1] > THRESHOLD_G || gyro_data[2] > THRESHOLD_G);

                    bool any_exceeded = tag_exceeded ||
                        (press_data < THRESHOLD_P) ||
                        (humid_data < THRESHOLD_H) ||
                        (mag_mag > THRESHOLD_M);

                    if (tag_exceeded) {
                        Check_And_Log_Thresholds(temp_data, press_data, humid_data, accel_data, gyro_data, magneto_data);
                        current_state = BATTLE_MODE;
                        led_blink_tick = HAL_GetTick();
                        PB_CancelPendingSingle();
                        Telemetry_Printf("Critical T/A/G threshold violated -> Entering BATTLE MODE. (Laser Charge: %d/%d)",
                                         laser_charge, LASER_CHARGE_MAX);
                        Telemetry_EvtMode(EVT_MODE_BATTLE);
                    }
                    else if (any_exceeded) {
                        if ((HAL_GetTick() - tick_1s_telemetry) >= 1000) {
                            tick_1s_telemetry = HAL_GetTick();
                            Check_And_Log_Thresholds(temp_data, press_data, humid_data, accel_data, gyro_data, magneto_data);
                        }
                    }
                    else {
                        if ((HAL_GetTick() - tick_1s_telemetry) >= 1000) {
                            tick_1s_telemetry = HAL_GetTick();
                            Telemetry_SendSensors(temp_data, press_data, humid_data,
                                accel_data, gyro_data, magneto_data);
                            Telemetry_Printf("Heading -> Magneto Mag:%.2f mgauss | Roll:%.2f deg, Pitch:%.2f deg, Yaw:%d deg (%s)",
                                             (double)mag_mag, (double)g_roll_deg, (double)g_pitch_deg,
                                             g_yaw_deg, Heading_Cardinal(g_yaw_deg));
                        }
                    }
                }
                break;

            case BATTLE_MODE:
                /* LED blinking at 1 Hz (toggle every 500ms) */
                if ((HAL_GetTick() - led_blink_tick) >= 500) {
                    led_blink_tick = HAL_GetTick();
                    BSP_LED_Toggle(LED2);
                }

                /* Full-auto fire while held: GPIO still low after hold latch */
                if (pb_hold_latched && pb_is_down &&
                    (BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET))
                {
                    /* Giant cannon: a radar-locked giant + at least 10 charge → the hold fires
                     * the cannon (-10 per shot) instead of the laser gun. */
                    if ((laser_charge >= CANNON_CHARGE_COST) && EE_CANNON_READY()) {
                        if ((HAL_GetTick() - pb_last_fire_tick) >= PB_FIRE_PERIOD_MS) {
                            pb_last_fire_tick = HAL_GetTick();
                            laser_charge = Laser_Clamp(laser_charge - CANNON_CHARGE_COST);
                            Telemetry_Printf("GIANT CANNON FIRED! KA-BOOM! (Remaining charge: %d/%d)",
                                             laser_charge, LASER_CHARGE_MAX);
                            Telemetry_EvtCannon((uint8_t)laser_charge);
                            EE_EVENT(DRONE_EVENT_CANNON_FIRED);
                            pb_low_charge_warned = true; /* cannon shots do not trigger the gun's "too low" nag */
                        }
                    }
                    else if (laser_charge < LASER_FIRE_MIN) {
                        if (!pb_low_charge_warned) {
                            Telemetry_Printf("Laser charge too low (%d/%d) to fire! Need at least %d. Please single-press to recharge the laser gun.",
                                             laser_charge, LASER_CHARGE_MAX, LASER_FIRE_MIN);
                            pb_low_charge_warned = true;
                            EE_EVENT(DRONE_EVENT_LASER_DENIED);
                        }
                    }
                    else if ((HAL_GetTick() - pb_last_fire_tick) >= PB_FIRE_PERIOD_MS) {
                        pb_last_fire_tick = HAL_GetTick();
                        Laser_UseShot();
                        Telemetry_Printf("Laser gun fired continuously! PEW PEW! (Remaining charge: %d/%d)",
                                         laser_charge, LASER_CHARGE_MAX);
                        Telemetry_EvtFire((uint8_t)laser_charge); /* charge AFTER the shot */
                        EE_EVENT(DRONE_EVENT_LASER_FIRED);
                        if (laser_charge < LASER_FIRE_MIN) {
                            Telemetry_Printf("Laser charge too low (%d/%d) to fire! Need at least %d. Please single-press to recharge the laser gun.",
                                             laser_charge, LASER_CHARGE_MAX, LASER_FIRE_MIN);
                            pb_low_charge_warned = true;
                        }
                    }
                }
                break;

            case THE_LAST_OF_2028:
                /* LED blinking at 2 Hz (toggle every 250ms) */
                if ((HAL_GetTick() - led_blink_tick) >= 250) {
                    led_blink_tick = HAL_GetTick();
                    BSP_LED_Toggle(LED2);
                }

                /* Alert once per second (informational only — does not trigger self-destruct) */
                if ((HAL_GetTick() - tick_1s_attack_msg) >= ATTACK_MSG_PERIOD_MS) {
                    uint32_t elapsed;
                    uint32_t remaining;
                    tick_1s_attack_msg = HAL_GetTick();
                    Telemetry_Printf("Drone was attacked!");
                    Telemetry_EvtAttack();
                    elapsed = HAL_GetTick() - the_last_of_2028_entry_tick;
                    remaining = (elapsed >= RESCUE_TIMEOUT_MS) ? 0U : (RESCUE_TIMEOUT_MS - elapsed);
                    Telemetry_EvtCountdown(remaining);
                }

                /* Self-destruct: 10s without rescue, or 5 failed upside-down rescues (whichever earlier) */
                if ((HAL_GetTick() - the_last_of_2028_entry_tick) >= RESCUE_TIMEOUT_MS) {
                    Enter_Self_Destruct("10s elapsed without rescue");
                }
                break;

            case SELF_DESTRUCT_TRIGGERED:
                if (!sacrifice_sent) {
                    Telemetry_Printf("Drone has made a noble sacrifice for mankind...");
                    noble_sacrifice_tick = HAL_GetTick();
                    sacrifice_sent = true;
                }

                if (sacrifice_sent && !cheerio_sent && (HAL_GetTick() - noble_sacrifice_tick) >= 1000) {
                    Telemetry_EvtTerminate(); /* immediately before Cheerio! */
                    Telemetry_Printf("Cheerio!");
                    cheerio_sent = true;
                    current_state = SELF_DESTRUCT_DEAD;
                }
                break;

            case SELF_DESTRUCT_DEAD:
                BSP_LED_Off(LED2);
#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
                /* Bounded final-effects phase before permanent halt (tick-driven, no blocking delay). */
                {
                    DroneStatus_t st;
                    uint32_t now;
                    Fill_DroneStatus(&st);
                    do {
                        now = HAL_GetTick();
                        Fill_DroneStatus(&st);
                        Enhancements_Update(&st, now);
                        Telemetry_WifiPoll(now); /* flush TERMINATE / Cheerio! */
                    } while (Enhancements_FinalEffectsDone(now) == 0U);
                    Enhancements_Shutdown();
                }
#endif
                while (1) {
                    /* Permanent self-destruct halt: only finish sending queued
                     * Wi-Fi telemetry (TERMINATE / Cheerio!). */
                    Telemetry_WifiPoll(HAL_GetTick());
                }
                break;
        }

#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
        Enhancements_Tick();
#endif
    }
}

/* A Wi-Fi client joined mid-mission: resend what VESPER needs to show the
 * current state (mode, charge, rescue countdown). Same EVT lines as usual. */
static void Telemetry_SendSnapshot(void)
{
    switch (current_state) {
        case STANDBY_MODE:
            Telemetry_EvtMode(EVT_MODE_STANDBY);
            break;
        case BATTLE_MODE:
            Telemetry_EvtMode(EVT_MODE_BATTLE);
            break;
        case THE_LAST_OF_2028: {
            uint32_t elapsed = HAL_GetTick() - the_last_of_2028_entry_tick;
            Telemetry_EvtMode(EVT_MODE_THE_LAST_OF_2028);
            Telemetry_EvtCountdown((elapsed >= RESCUE_TIMEOUT_MS) ? 0U : (RESCUE_TIMEOUT_MS - elapsed));
            break;
        }
        default:
            return; /* self-destruct in progress: TERMINATE follows */
    }
    Telemetry_EvtCharge((uint8_t)Laser_Clamp(laser_charge));
}

/* --- EXTI: stamp rising/falling edges (GPIO level classified in Process_PushButton) --- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_1) {
        Telemetry_WifiDrdyIsr(); /* ISM43362 DATA-READY (PE1 / EXTI1) */
        return;
    }
    if (GPIO_Pin != USER_BUTTON_PIN) {
        return;
    }

    uint32_t now = HAL_GetTick();
    if ((now - pb_last_edge_tick) < PB_DEBOUNCE_MS) {
        return;
    }
    pb_last_edge_tick = now;
    pb_edge_tick = now;
    pb_edge_pending = true;
}

/*
 * Pair EXTI edge stamps with live GPIO level:
 *  - falling / GPIO low  -> press start (start duration window)
 *  - rising  / GPIO high -> press end   (duration = release - press)
 * Hold: still low after PB_HOLD_MS. Short press: released before PB_HOLD_MS.
 * Battle below full charge: a short press recharges immediately; at full charge
 * short presses use the double-press window (double = back to Standby).
 */
void Process_PushButton(void)
{
    const bool pressed = (BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET);
    const uint32_t now = HAL_GetTick();

    /* Optional: consume EXTI edge stamp (confirms an edge occurred; GPIO decides which) */
    if (pb_edge_pending) {
        pb_edge_pending = false;
        (void)pb_edge_tick;
    }

    /* Press start: GPIO low */
    if (pressed && !pb_is_down) {
        pb_is_down = true;
        pb_down_tick = now;
        pb_hold_latched = false;
        pb_low_charge_warned = false;
        pb_last_fire_tick = 0;
    }

    /* Hold crossed while still down (GPIO still pressed) */
    if (pb_is_down && pressed && !pb_hold_latched &&
        (now - pb_down_tick) >= PB_HOLD_MS)
    {
        pb_hold_latched = true;
        PB_CancelPendingSingle();
        pb_last_short_press_tick = 0;
        /* Fire immediately on hold latch, then every PB_FIRE_PERIOD_MS */
        pb_last_fire_tick = now - PB_FIRE_PERIOD_MS;
        /* Battle fire runs in state machine; other modes ignore hold */
    }

    /* Release: GPIO high — measure falling→rising window */
    if (!pressed && pb_is_down) {
        pb_last_press_duration_ms = now - pb_down_tick;
        pb_is_down = false;

        if (pb_hold_latched) {
            /* Hold/release pair: do not treat as a click for double-press */
            pb_hold_latched = false;
        }
        else if (pb_last_press_duration_ms < PB_HOLD_MS) {
            if (current_state == BATTLE_MODE && laser_charge < LASER_CHARGE_MAX) {
                /* Charging: act on every short press right away */
                PB_CancelPendingSingle();
                pb_last_short_press_tick = 0;
                PB_OnSinglePress();
            }
            else if (pb_last_short_press_tick != 0 &&
                     (now - pb_last_short_press_tick) <= PB_DOUBLE_PRESS_MS)
            {
                PB_CancelPendingSingle();
                pb_last_short_press_tick = 0;
                PB_OnDoublePress();
            }
            else {
                pb_last_short_press_tick = now;
                pb_single_armed = true;
                pb_single_deadline_tick = now + PB_DOUBLE_PRESS_MS;
            }
        }
    }

    /* Deferred single press: no second short press within double-press window */
    if (pb_single_armed && (int32_t)(now - pb_single_deadline_tick) >= 0) {
        pb_single_armed = false;
        pb_last_short_press_tick = 0;
        PB_OnSinglePress();
    }
}

void PB_CancelPendingSingle(void)
{
    pb_single_armed = false;
}

void PB_OnDoublePress(void)
{
    if (current_state == STANDBY_MODE) {
        current_state = BATTLE_MODE;
        led_blink_tick = HAL_GetTick();
        Telemetry_Printf("PB Double Press (Standby) -> Entering BATTLE MODE. (Laser Charge: %d/%d)",
                         laser_charge, LASER_CHARGE_MAX);
        Telemetry_EvtMode(EVT_MODE_BATTLE);
    }
    else if (current_state == BATTLE_MODE) {
        if (laser_charge >= LASER_CHARGE_MAX) {
            current_state = STANDBY_MODE;
            BSP_LED_On(LED2);
            Telemetry_Printf("PB Double Press (Battle) -> Returning to STANDBY MODE. (Laser Charge: %d/%d)",
                             laser_charge, LASER_CHARGE_MAX);
            Telemetry_EvtMode(EVT_MODE_STANDBY);
        }
        else {
            Telemetry_Printf("Laser must be fully charged (%d/%d) before returning to standby mode! Current charge: %d/%d",
                             LASER_CHARGE_MAX, LASER_CHARGE_MAX, laser_charge, LASER_CHARGE_MAX);
        }
    }
    else if (current_state == THE_LAST_OF_2028) {
        the_last_of_2028_rescue_attempts++;
        Telemetry_Printf("Rescue attempt %d/%d (Double Press detected)...",
                         the_last_of_2028_rescue_attempts, RESCUE_ATTEMPT_LIMIT);

        if (current_accel_z < -4.0f) {
            if (the_last_of_2028_rescue_attempts >= RESCUE_ATTEMPT_LIMIT) {
                Enter_Self_Destruct("5 rescue attempts failed while upside down");
            }
            else {
                Telemetry_Printf("Drone is still upside down! Flip upright and double press to rescue.");
                EE_EVENT(DRONE_EVENT_RESCUE_FAILED);
            }
        }
        else {
            current_state = BATTLE_MODE;
            led_blink_tick = HAL_GetTick();
            the_last_of_2028_rescue_attempts = 0;
            Telemetry_Printf("PB Rescue Action: Successfully reverted to Battle Mode.");
            Telemetry_EvtRescue();
            Telemetry_EvtMode(EVT_MODE_BATTLE);
        }
    }
}

void PB_OnSinglePress(void)
{
    if (current_state != BATTLE_MODE) {
        return; /* Standby / THE_LAST_OF_2028: single presses ignored */
    }
    if (laser_charge >= LASER_CHARGE_MAX) {
        return; /* already full */
    }

    const bool was_ready = (laser_charge >= LASER_FIRE_MIN);
    Laser_AddCharge();

    Telemetry_Printf("Laser recharging... Current charge: %d/%d", laser_charge, LASER_CHARGE_MAX);
    Telemetry_EvtCharge((uint8_t)laser_charge);
    EE_EVENT(DRONE_EVENT_LASER_RECHARGED);

    if (!was_ready && laser_charge >= LASER_FIRE_MIN) {
        Telemetry_Printf("Laser ready to fire (%d/%d). Hold button >500ms to fire, or keep charging up to %d to return to Standby Mode.",
                         laser_charge, LASER_CHARGE_MAX, LASER_CHARGE_MAX);
    }
    if (laser_charge >= CANNON_CHARGE_COST && EE_CANNON_READY()) {
        Telemetry_Printf("GIANT CANNON READY (%d/%d), hold to fire!", laser_charge, LASER_CHARGE_MAX);
    }
}

void Enter_The_Last_Of_2028(void)
{
    current_state = THE_LAST_OF_2028;
    the_last_of_2028_entry_tick = HAL_GetTick();
    tick_1s_attack_msg = HAL_GetTick();
    led_blink_tick = HAL_GetTick();
    the_last_of_2028_rescue_attempts = 0;
    PB_CancelPendingSingle();
    pb_last_short_press_tick = 0;
    Telemetry_Printf("Drone is upside down! Entering THE LAST OF EE2028.");
    Telemetry_EvtMode(EVT_MODE_THE_LAST_OF_2028);
    Telemetry_EvtCountdown(RESCUE_TIMEOUT_MS);
}

void Enter_Self_Destruct(const char *reason)
{
    const char *reason_token = EVT_REASON_TIMEOUT;

    if (current_state == SELF_DESTRUCT_TRIGGERED || current_state == SELF_DESTRUCT_DEAD) {
        return;
    }
    Telemetry_Printf("%s! Drone self-destructing...", reason);
    /* Map human reason → EVT token (avoid matching "without rescue" as rescue_limit). */
    if (reason != NULL && strstr(reason, "rescue attempts") != NULL) {
        reason_token = EVT_REASON_RESCUE_LIMIT;
    }
    Telemetry_EvtSelfDestruct(reason_token);
    current_state = SELF_DESTRUCT_TRIGGERED;
    sacrifice_sent = false;
    cheerio_sent = false;
    the_last_of_2028_rescue_attempts = 0;
    PB_CancelPendingSingle();
    /* Both timeout and fifth failed rescue converge here — emit once. */
    EE_EVENT(DRONE_EVENT_SELF_DESTRUCT_STARTED);
}

#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
/* The matrix / buzzer adapter keeps its own 0..10 charge bar; scale 0..50 down to it. */
#define STATUS_CHARGE_SCALE 10U
static void Fill_DroneStatus(DroneStatus_t *out)
{
    uint32_t now;
    uint32_t elapsed;

    if (out == NULL) {
        return;
    }

    now = HAL_GetTick();
    out->mode = current_state;
    out->charge = (uint8_t)(((uint32_t)Laser_Clamp(laser_charge) * STATUS_CHARGE_SCALE) / LASER_CHARGE_MAX);
    /* Armed = Battle with enough charge to fire. */
    out->armed = ((current_state == BATTLE_MODE) && (laser_charge >= LASER_FIRE_MIN)) ? 1U : 0U;
    out->rescue_remaining_ms = 0U;

    if (current_state == THE_LAST_OF_2028) {
        elapsed = now - the_last_of_2028_entry_tick;
        if (elapsed >= RESCUE_TIMEOUT_MS) {
            out->rescue_remaining_ms = 0U;
        } else {
            out->rescue_remaining_ms = RESCUE_TIMEOUT_MS - elapsed;
        }
    }
}

static void Enhancements_Tick(void)
{
    DroneStatus_t st;

#if defined(EE_RADAR_AIM) && (EE_RADAR_AIM != 0) && EE_SERVO_SELFTEST
    /* While the bench sweep runs it owns the servo: do not let the aim loop fight it. */
    if (Enhancements_HW_ServoSelfTestStep(HAL_GetTick()) != 0U) {
        return;
    }
#endif
    Fill_DroneStatus(&st);
    Enhancements_Update(&st, HAL_GetTick());

#if defined(EE_RADAR_AIM) && (EE_RADAR_AIM != 0)
    /* RD-03D link health every 5 s (bring-up): rx should climb ~300 B/s with
     * ok frames ~10/s; bad/drop/err should stay near 0. rx=0 → check wiring. */
    {
        static uint32_t radar_link_tick = 0;
        if ((HAL_GetTick() - radar_link_tick) >= 5000U) {
            uint32_t rx, dropped, errors, ok, bad;
            radar_link_tick = HAL_GetTick();
            Enhancements_HW_RadarStats(&rx, &dropped, &errors);
            Enhancements_RadarFrameStats(&ok, &bad);
            Telemetry_Printf("Radar link: rx=%lu B, frames ok=%lu bad=%lu, dropped=%lu, uart_err=%lu",
                (unsigned long)rx, (unsigned long)ok, (unsigned long)bad,
                (unsigned long)dropped, (unsigned long)errors);
        }
    }
#endif
}
#endif

/* --- Threshold Warning Logging Function (mission UART) --- */
void Check_And_Log_Thresholds(float t, float p, float h, float a[3], float g[3], float m[3])
{
    if (t > THRESHOLD_T) {
        Telemetry_Printf("Temperature has exceeded the threshold of %.2f C.", (double)THRESHOLD_T);
    }
    if (p < THRESHOLD_P) {
        Telemetry_Printf("P:%.2f hPa is lower than the threshold of %.2f hPa.", (double)p, (double)THRESHOLD_P);
        Telemetry_Printf("Too high! Stall alert!");
    }
    if (h < THRESHOLD_H) {
        Telemetry_Printf("H:%.2f %% is lower than the threshold of %.2f %%.", (double)h, (double)THRESHOLD_H);
        Telemetry_Printf("Too dry for inhabitation!");
    }
    if (a[0] > THRESHOLD_A) {
        Telemetry_Printf("Accel X has exceeded the threshold of %.2f m/s^2.", (double)THRESHOLD_A);
    }
    if (a[1] > THRESHOLD_A) {
        Telemetry_Printf("Accel Y has exceeded the threshold of %.2f m/s^2.", (double)THRESHOLD_A);
    }
    if (a[2] > THRESHOLD_A) {
        Telemetry_Printf("Accel Z has exceeded the threshold of %.2f m/s^2.", (double)THRESHOLD_A);
    }
    if (g[0] > THRESHOLD_G) {
        Telemetry_Printf("Angular velocity X has exceeded the threshold of %.2f dps.", (double)THRESHOLD_G);
    }
    if (g[1] > THRESHOLD_G) {
        Telemetry_Printf("Angular velocity Y has exceeded the threshold of %.2f dps.", (double)THRESHOLD_G);
    }
    if (g[2] > THRESHOLD_G) {
        Telemetry_Printf("Angular velocity Z has exceeded the threshold of %.2f dps.", (double)THRESHOLD_G);
    }

    float mag_mag = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    if (mag_mag > THRESHOLD_M) {
        Telemetry_Printf("Magnetometer Magnitude:%.2f mgauss, exceeds threshold of %.2f mgauss.", (double)mag_mag, (double)THRESHOLD_M);
        Telemetry_Printf("Possible EM interference detected!");
    }
}
