/******************************************************************************
 * @file  : main.c
 * @brief : Main program body
 * (c) EE2028 Teaching Team
 ******************************************************************************/

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_accelero.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_tsensor.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_gyro.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_hsensor.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_magneto.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_psensor.h"
#include <stdio.h>
#include <stdbool.h>

extern void initialise_monitor_handles(void); /* for semi-hosting support (printf) */

/* Define thresholds */
#define THRESHOLD_A 20.0f
#define THRESHOLD_T 50.0f
#define THRESHOLD_G 150.0f
#define THRESHOLD_H 30.0f /* Minimum humidity threshold (desert check) */
#define THRESHOLD_M 1000.0f
#define THRESHOLD_P 900.0f

/* Define states */
typedef enum {
	STANDBY_MODE,
	BATTLE_MODE,
	LAST_OF_2028,
	SELF_DESTRUCT_TRIGGERED,
	SELF_DESTRUCT_DEAD
} DroneState_t;

DroneState_t current_state = STANDBY_MODE;

/* Push button & tracking variables */
volatile uint32_t last_pb_press_time = 0;
bool button_was_pressed = false;
uint32_t press_start_tick = 0;
bool hold_action_triggered = false;
float current_accel_z = 0.0f;
int last_of_2028_rescue_attempts = 0;

/* Laser Gun Energy Tracking (Initialized to 10/10 full charge, remembers last charge) */
int laser_charge = 10;

/* Timers & State Trackers */
uint32_t tick_1s_telemetry = 0;
uint32_t tick_1s_attack_msg = 0;
uint32_t led_blink_tick = 0;
uint32_t last_of_2028_entry_tick = 0;
uint32_t noble_sacrifice_tick = 0;
bool sacrifice_sent = false;
bool cheerio_sent = false;

/* Function prototype for threshold checks */
void Check_And_Log_Thresholds(float t, float p, float h, float a[3], float g[3], float m[3]);

int main(void)
{
	initialise_monitor_handles(); /* for semi-hosting support (printf) */

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* Peripheral and Board Initializations using BSP functions */
	BSP_LED_Init(LED2);
	BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

	BSP_ACCELERO_Init();
	BSP_TSENSOR_Init();
	BSP_GYRO_Init();
	BSP_HSENSOR_Init();
	BSP_MAGNETO_Init();
	BSP_PSENSOR_Init();

	printf("Drone initialized: Entering STANDBY MODE (Laser Charge: %d/10)\n", laser_charge);

	while (1)
	{
		/* --- 1. Read All Sensors --- */
		float accel_data[3];
		int16_t accel_data_i16[3] = { 0 };
		BSP_ACCELERO_AccGetXYZ(accel_data_i16);
		accel_data[0] = (float)accel_data_i16[0] * (9.8f / 1000.0f);
		accel_data[1] = (float)accel_data_i16[1] * (9.8f / 1000.0f);
		accel_data[2] = (float)accel_data_i16[2] * (9.8f / 1000.0f);
		current_accel_z = accel_data[2]; /* Save for ISR check */

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

		/* --- Global Check for "The Last of 2028" (Upside Down) --- */
		if ((current_state == STANDBY_MODE || current_state == BATTLE_MODE) && accel_data[2] < -4.0f)
		{
			current_state = LAST_OF_2028;
			last_of_2028_entry_tick = HAL_GetTick();
			led_blink_tick = HAL_GetTick(); /* Reset blink timer for 2 Hz */
			last_of_2028_rescue_attempts = 0; /* Reset attempt counter on entry */
			printf("Drone is upside down! Entering LAST OF 2028.\r\n");
		}

		/* --- 2. State Machine Handling --- */
		switch (current_state)
		{
			case STANDBY_MODE:
				/* LED always ON in Standby mode */
				BSP_LED_On(LED2);
				hold_action_triggered = false;

				/* Check if T, A, or G exceed thresholds -> Triggers direct transition to Battle Mode */
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
						printf("Critical T/A/G threshold violated -> Entering BATTLE MODE. (Laser Charge: %d/10)\r\n", laser_charge);
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
							printf("Telemetry -> T:%.1f °C, P:%.1f hPa, H:%.1f %% | Accel:[%.1f, %.1f, %.1f] m/s^2 | Gyro:[%.1f, %.1f, %.1f] dps | Magneto:[%.1f, %.1f, %.1f] mgauss\r\n",
								temp_data, press_data, humid_data,
								accel_data[0], accel_data[1], accel_data[2],
								gyro_data[0], gyro_data[1], gyro_data[2],
								magneto_data[0], magneto_data[1], magneto_data[2]);
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

				/* --- Button Hold & Continuous Firing Logic --- */
				if (BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET) /* Active-low push button check */
				{
					if (!button_was_pressed) {
						button_was_pressed = true;
						press_start_tick = HAL_GetTick();
						hold_action_triggered = false;
					}
					else if (!hold_action_triggered && (HAL_GetTick() - press_start_tick >= 500))
					{
						/* Button has been held down for > 500ms */
						hold_action_triggered = true;

						if (laser_charge < 5) {
							printf("Laser charge too low (%d/10) to fire! Please single-press to recharge the laser gun.\r\n", laser_charge);
						} else {
							/* Fires continuously while held until charge drops below 5/10 */
							while (laser_charge >= 5 && BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET) {
								laser_charge -= 5;
								printf("Laser gun fired continuously! PEW PEW! (Remaining charge: %d/10)\r\n", laser_charge);
								HAL_Delay(500); /* Pace continuous shots */
							}

							/* If charge dropped below 5 while holding, immediately remind to recharge without needing another press */
							if (laser_charge < 5 && BSP_PB_GetState(BUTTON_USER) == GPIO_PIN_RESET) {
								printf("Laser charge too low (%d/10) to fire! Please single-press to recharge the laser gun.\r\n", laser_charge);
							}
						}
					}
				}
				else {
					button_was_pressed = false;
				}
				break;

			case LAST_OF_2028:
				/* LED blinking at 2 Hz (toggle every 250ms) */
				if ((HAL_GetTick() - led_blink_tick) >= 250) {
					led_blink_tick = HAL_GetTick();
					BSP_LED_Toggle(LED2);
				}

				/* Sends "Drone was attacked" message every 1s */
				if ((HAL_GetTick() - tick_1s_attack_msg) >= 1000) {
					tick_1s_attack_msg = HAL_GetTick();
					printf("Drone was attacked!\r\n");
				}
				break;

			case SELF_DESTRUCT_TRIGGERED:
				if (!sacrifice_sent) {
					printf("Drone has made a noble sacrifice for mankind...\r\n");
					noble_sacrifice_tick = HAL_GetTick();
					sacrifice_sent = true;
				}

				/* Wait 1s after noble sacrifice -> Send "Cheerio!" */
				if (sacrifice_sent && !cheerio_sent && (HAL_GetTick() - noble_sacrifice_tick) >= 1000) {
					printf("Cheerio!\r\n");
					cheerio_sent = true;
					current_state = SELF_DESTRUCT_DEAD;
				}
				break;

			case SELF_DESTRUCT_DEAD:
				BSP_LED_Off(LED2); /* LED OFF permanently */
				while (1) {
					/* Permanent self-destruct halt */
				}
				break;
		}

		HAL_Delay(10); /* Small loop tick delay */
	}
}

/* --- EXTI Interrupt Callback for Push Button (USER_BUTTON) --- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == USER_BUTTON_PIN)
	{
		uint32_t current_time = HAL_GetTick();
		uint32_t time_since_last = current_time - last_pb_press_time;

		/* Debounce filter: Ignore switch bounce (< 100ms) */
		if (time_since_last < 100) {
			return;
		}

		if (current_state == STANDBY_MODE)
		{
			/* Double press window: second press arrives within 500ms following first press */
			if (time_since_last <= 500)
			{
				current_state = BATTLE_MODE;
				led_blink_tick = HAL_GetTick();
				printf("PB Double Press (Standby) -> Entering BATTLE MODE. (Laser Charge: %d/10)\r\n", laser_charge);
				last_pb_press_time = 0;
			}
			else
			{
				last_pb_press_time = current_time;
			}
		}
		else if (current_state == BATTLE_MODE)
		{
			/* Double press window: second press arrives within 500ms following first press returns to Standby */
			if (time_since_last <= 500)
			{
				current_state = STANDBY_MODE;
				BSP_LED_On(LED2);
				printf("PB Double Press (Battle) -> Returning to STANDBY MODE. (Laser Charge: %d/10)\r\n", laser_charge);
				last_pb_press_time = 0;
			}
			else
			{
				last_pb_press_time = current_time;

				/* Single press / recharge action (press arrived > 500ms after the last one) */
				if (!hold_action_triggered)
				{
					if (laser_charge < 5)
					{
						laser_charge += 3;
						if (laser_charge > 10) laser_charge = 10;
						printf("Laser recharging... Current charge: %d/10\r\n", laser_charge);
					}
					else
					{
						printf("Laser charge is already sufficient (>=5/10). Hold button >500ms to fire! Or double-press within 500ms to return to standby mode\r\n");
					}
				}
			}
		}
		else if (current_state == LAST_OF_2028)
		{
			/* Rescue double press window: second press arrives within 500ms following first press counts as 1 rescue attempt */
			if (time_since_last <= 500)
			{
				last_of_2028_rescue_attempts++;
				printf("Rescue attempt %d/5 (Double Press detected)...\r\n", last_of_2028_rescue_attempts);

				if (current_accel_z < -4.0f)
				{
					if (last_of_2028_rescue_attempts >= 5)
					{
						printf("5 rescue attempts failed while upside down! Drone self-destructing...\r\n");
						current_state = SELF_DESTRUCT_TRIGGERED;
						last_of_2028_rescue_attempts = 0;
					}
					else
					{
						printf("Drone is still upside down! Flip upright and double press to rescue.\r\n");
					}
				}
				else
				{
					current_state = BATTLE_MODE;
					led_blink_tick = HAL_GetTick();
					printf("PB Rescue Action: Successfully reverted to Battle Mode.\r\n");
					last_of_2028_rescue_attempts = 0;
				}
				last_pb_press_time = 0;
			}
			else
			{
				last_pb_press_time = current_time;
			}
		}
	}
}

/* --- Threshold Warning Logging Function --- */
void Check_And_Log_Thresholds(float t, float p, float h, float a[3], float g[3], float m[3])
{
	if (t > THRESHOLD_T) {
		printf("T:%.2f C, exceeds threshold of %.2f C.\r\n", t, THRESHOLD_T);
	}
	if (p < THRESHOLD_P) {
		printf("P:%.2f hPa, drops below threshold of %.2f hPa.\r\n", p, THRESHOLD_P);
	}
	if (h < THRESHOLD_H) {
		printf("H:%.2f %%, drops below threshold of %.2f %%.\r\n", h, THRESHOLD_H);
	}
	if (a[0] > THRESHOLD_A) { printf("Accel X:%.2f m/s^2, exceeds threshold of %.2f m/s^2.\r\n", a[0], THRESHOLD_A); }
	if (a[1] > THRESHOLD_A) { printf("Accel Y:%.2f m/s^2, exceeds threshold of %.2f m/s^2.\r\n", a[1], THRESHOLD_A); }
	if (a[2] > THRESHOLD_A) { printf("Accel Z:%.2f m/s^2, exceeds threshold of %.2f m/s^2.\r\n", a[2], THRESHOLD_A); }

	if (g[0] > THRESHOLD_G) { printf("Gyro X:%.2f dps, exceeds threshold of %.2f dps.\r\n", g[0], THRESHOLD_G); }
	if (g[1] > THRESHOLD_G) { printf("Gyro Y:%.2f dps, exceeds threshold of %.2f dps.\r\n", g[1], THRESHOLD_G); }
	if (g[2] > THRESHOLD_G) { printf("Gyro Z:%.2f dps, exceeds threshold of %.2f dps.\r\n", g[2], THRESHOLD_G); }

	if (m[0] > THRESHOLD_M) { printf("Magneto X:%.2f mgauss, exceeds threshold of %.2f mgauss.\r\n", m[0], THRESHOLD_M); }
	if (m[1] > THRESHOLD_M) { printf("Magneto Y:%.2f mgauss, exceeds threshold of %.2f mgauss.\r\n", m[1], THRESHOLD_M); }
	if (m[2] > THRESHOLD_M) { printf("Magneto Z:%.2f mgauss, exceeds threshold of %.2f mgauss.\r\n", m[2], THRESHOLD_M); }
}
