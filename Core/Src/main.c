/******************************************************************************
 * @file  : main.c
 * @brief : Main program body
 * (c) EE2028 Teaching Team
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
#else
#define EE_EVENT(evt) ((void)0)
#endif

/* CubeIDE console debug only (printf) — mission / VESPER traffic uses Telemetry_* over UART. */
extern void initialise_monitor_handles(void); /* semihosting printf */

/* Define thresholds */
#define THRESHOLD_A 20.0f
#define THRESHOLD_T 50.0f
#define THRESHOLD_G 150.0f
#define THRESHOLD_H 30.0f /* Minimum humidity threshold (desert check) */
#define THRESHOLD_M 1000.0f
#define THRESHOLD_P 900.0f

/* Push-button timing (USER button is active-low) */
#define PB_DEBOUNCE_MS           100U
#define PB_DOUBLE_PRESS_MS       500U
#define PB_HOLD_MS               500U
#define PB_FIRE_PERIOD_MS        500U
#define ATTACK_MSG_PERIOD_MS     1000U
#ifndef RESCUE_TIMEOUT_MS
#define RESCUE_TIMEOUT_MS        10000U /* 10s without rescue → self-destruct */
#endif
#define RESCUE_ATTEMPT_LIMIT     5

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

/* Laser Gun Energy Tracking (Initialized to 10/10 full charge, remembers last charge) */
int laser_charge = 10;

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
#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
static void Fill_DroneStatus(DroneStatus_t *out);
static void Enhancements_Tick(void);
#endif

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
#endif
		Enhancements_Init(&ee_cfg);
	}
#endif

	Telemetry_Printf("Drone initialized: Entering STANDBY MODE (Laser Charge: %d/10)", laser_charge);
	Telemetry_EvtMode(EVT_MODE_STANDBY);
	Telemetry_EvtCharge((uint8_t)laser_charge);

	while (1)
	{
		/* --- 1. Read All Sensors --- */
		float accel_data[3];
		int16_t accel_data_i16[3] = { 0 };
		BSP_ACCELERO_AccGetXYZ(accel_data_i16);
		accel_data[0] = (float)accel_data_i16[0] * (9.8f / 1000.0f);
		accel_data[1] = (float)accel_data_i16[1] * (9.8f / 1000.0f);
		accel_data[2] = (float)accel_data_i16[2] * (9.8f / 1000.0f);
		current_accel_z = accel_data[2];

		float gyro_data[3];
		BSP_GYRO_GetXYZ(gyro_data);
		gyro_data[0] = gyro_data[0] / 1000.0f;
		gyro_data[1] = gyro_data[1] / 1000.0f;
		gyro_data[2] = gyro_data[2] / 1000.0f;

		float magneto_data[3];
		int16_t magneto_data_i16[3] = { 0 };
		BSP_MAGNETO_GetXYZ(magneto_data_i16);
		magneto_data[0] = (float)magneto_data_i16[0];
		magneto_data[1] = (float)magneto_data_i16[1];
		magneto_data[2] = (float)magneto_data_i16[2];

		float temp_data = BSP_TSENSOR_ReadTemp();
		float press_data = BSP_PSENSOR_ReadPressure();
		float humid_data = BSP_HSENSOR_ReadHumidity();

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
						(magneto_data[0] > THRESHOLD_M || magneto_data[1] > THRESHOLD_M || magneto_data[2] > THRESHOLD_M);

					if (tag_exceeded) {
						current_state = BATTLE_MODE;
						led_blink_tick = HAL_GetTick();
						PB_CancelPendingSingle();
						Telemetry_Printf("Critical T/A/G threshold violated -> Entering BATTLE MODE. (Laser Charge: %d/10)", laser_charge);
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
					if (laser_charge < 5) {
						if (!pb_low_charge_warned) {
							Telemetry_Printf("Laser charge too low (%d/10) to fire! Please single-press to recharge the laser gun.", laser_charge);
							pb_low_charge_warned = true;
							EE_EVENT(DRONE_EVENT_LASER_DENIED);
						}
					}
					else if ((HAL_GetTick() - pb_last_fire_tick) >= PB_FIRE_PERIOD_MS) {
						pb_last_fire_tick = HAL_GetTick();
						laser_charge -= 5;
						Telemetry_Printf("Laser gun fired continuously! PEW PEW! (Remaining charge: %d/10)", laser_charge);
						Telemetry_EvtFire((uint8_t)laser_charge); /* charge AFTER the shot */
						EE_EVENT(DRONE_EVENT_LASER_FIRED);
						if (laser_charge < 5) {
							Telemetry_Printf("Laser charge too low (%d/10) to fire! Please single-press to recharge the laser gun.", laser_charge);
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
				/* Bounded final-effects phase before permanent halt. */
				{
					DroneStatus_t st;
					uint32_t now;
					Fill_DroneStatus(&st);
					do {
						now = HAL_GetTick();
						Fill_DroneStatus(&st);
						Enhancements_Update(&st, now);
						HAL_Delay(10);
					} while (Enhancements_FinalEffectsDone(now) == 0U);
					Enhancements_Shutdown();
				}
#endif
				while (1) {
					/* Permanent self-destruct halt */
				}
				break;
		}

#if defined(EE_ENHANCEMENTS) && (EE_ENHANCEMENTS != 0)
		Enhancements_Tick();
#endif
		HAL_Delay(10);
	}
}

/* --- EXTI: stamp rising/falling edges (GPIO level classified in Process_PushButton) --- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
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
			/* Short press completed (rising after falling within hold window) */
			if (pb_last_short_press_tick != 0 &&
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
		Telemetry_Printf("PB Double Press (Standby) -> Entering BATTLE MODE. (Laser Charge: %d/10)", laser_charge);
		Telemetry_EvtMode(EVT_MODE_BATTLE);
	}
	else if (current_state == BATTLE_MODE) {
		current_state = STANDBY_MODE;
		BSP_LED_On(LED2);
		Telemetry_Printf("PB Double Press (Battle) -> Returning to STANDBY MODE. (Laser Charge: %d/10)", laser_charge);
		Telemetry_EvtMode(EVT_MODE_STANDBY);
	}
	else if (current_state == THE_LAST_OF_2028) {
		the_last_of_2028_rescue_attempts++;
		Telemetry_Printf("Rescue attempt %d/5 (Double Press detected)...", the_last_of_2028_rescue_attempts);

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

	if (laser_charge < 5) {
		laser_charge += 3;
		if (laser_charge > 10) {
			laser_charge = 10;
		}
		Telemetry_Printf("Laser recharging... Current charge: %d/10", laser_charge);
		Telemetry_EvtCharge((uint8_t)laser_charge);
		EE_EVENT(DRONE_EVENT_LASER_RECHARGED);
	}
	else {
		Telemetry_Printf("Laser charge is already sufficient (>=5/10). Hold button >500ms to fire! Or double-press within 500ms to return to standby mode");
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
static void Fill_DroneStatus(DroneStatus_t *out)
{
	uint32_t now;
	uint32_t elapsed;

	if (out == NULL) {
		return;
	}

	now = HAL_GetTick();
	out->mode = current_state;
	out->charge = (laser_charge < 0) ? 0U :
	              (laser_charge > 10) ? 10U : (uint8_t)laser_charge;
	/* Armed = Battle with enough charge to fire (PR #3 hold-to-fire rule). */
	out->armed = ((current_state == BATTLE_MODE) && (laser_charge >= 5)) ? 1U : 0U;
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
	Fill_DroneStatus(&st);
	Enhancements_Update(&st, HAL_GetTick());
}
#endif

/* --- Threshold Warning Logging Function (mission UART) --- */
void Check_And_Log_Thresholds(float t, float p, float h, float a[3], float g[3], float m[3])
{
	if (t > THRESHOLD_T) {
		Telemetry_Printf("T:%.2f C, exceeds threshold of %.2f C.", (double)t, (double)THRESHOLD_T);
	}
	if (p < THRESHOLD_P) {
		Telemetry_Printf("P:%.2f hPa, drops below threshold of %.2f hPa.", (double)p, (double)THRESHOLD_P);
	}
	if (h < THRESHOLD_H) {
		Telemetry_Printf("H:%.2f %%, drops below threshold of %.2f %%.", (double)h, (double)THRESHOLD_H);
	}
	if (a[0] > THRESHOLD_A) {
		Telemetry_Printf("Accel X:%.2f m/s^2, exceeds threshold of %.2f m/s^2.", (double)a[0], (double)THRESHOLD_A);
	}
	if (a[1] > THRESHOLD_A) {
		Telemetry_Printf("Accel Y:%.2f m/s^2, exceeds threshold of %.2f m/s^2.", (double)a[1], (double)THRESHOLD_A);
	}
	if (a[2] > THRESHOLD_A) {
		Telemetry_Printf("Accel Z:%.2f m/s^2, exceeds threshold of %.2f m/s^2.", (double)a[2], (double)THRESHOLD_A);
	}

	if (g[0] > THRESHOLD_G) {
		Telemetry_Printf("Gyro X:%.2f dps, exceeds threshold of %.2f dps.", (double)g[0], (double)THRESHOLD_G);
	}
	if (g[1] > THRESHOLD_G) {
		Telemetry_Printf("Gyro Y:%.2f dps, exceeds threshold of %.2f dps.", (double)g[1], (double)THRESHOLD_G);
	}
	if (g[2] > THRESHOLD_G) {
		Telemetry_Printf("Gyro Z:%.2f dps, exceeds threshold of %.2f dps.", (double)g[2], (double)THRESHOLD_G);
	}

	if (m[0] > THRESHOLD_M) {
		Telemetry_Printf("Magneto X:%.2f mgauss, exceeds threshold of %.2f mgauss.", (double)m[0], (double)THRESHOLD_M);
	}
	if (m[1] > THRESHOLD_M) {
		Telemetry_Printf("Magneto Y:%.2f mgauss, exceeds threshold of %.2f mgauss.", (double)m[1], (double)THRESHOLD_M);
	}
	if (m[2] > THRESHOLD_M) {
		Telemetry_Printf("Magneto Z:%.2f mgauss, exceeds threshold of %.2f mgauss.", (double)m[2], (double)THRESHOLD_M);
	}
}
