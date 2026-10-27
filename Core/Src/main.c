#include "drone.h"

DroneState_t current_state = STANDBY_MODE;

/* Global variables for sensors, energy, and push button tracking */
float T, P, H, A, G, M;
float laser_charge = 0.0f; /* Scale 0.0 to 1.0 (0/10 to 10/10) */
uint32_t last_pb_press_time = 0;
uint8_t pb_press_count = 0;
uint8_t upside_down_detected = 0;

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_UART_Init();

    /* Default entry message */
    /* UART_Send("Entering STANDBY MODE\r\n"); */

    while (1) {
        Read_Sensors();

        switch (current_state) {
            case STANDBY_MODE:
                Handle_Standby_Mode();
                break;

            case BATTLE_MODE:
                Handle_Battle_Mode();
                break;

            case LAST_OF_2028:
                Handle_Last_Of_2028_Mode();
                break;
        }

        HAL_Delay(100); /* Main loop tick */
    }
}

/* --- STANDBY MODE LOGIC --- */
void Handle_Standby_Mode(void) {
    /* LED always ON */
    /* HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET); */

    /* Jump to Battle Mode if T/A/G exceed thresholds */
    if (T > THRESHOLD_T && A > THRESHOLD_A && G > THRESHOLD_G) {
        current_state = BATTLE_MODE;
        /* UART_Send("Entering BATTLE MODE\r\n"); */
        return;
    }

    /* Periodic 1s telemetry or threshold violation logs */
    /* (Implement a 1s non-blocking timer or tick check here) */
}

/* --- BATTLE MODE LOGIC --- */
void Handle_Battle_Mode(void) {
    /* LED blinking at 1 Hz */

    /* Gyroscope upside-down -> LAST_OF_2028 */
    if (G_is_upside_down()) {
        current_state = LAST_OF_2028;
        return;
    }

    /* Core laser charging logic */
    if (laser_charge < 0.5f) {
        /* "Recharge!" if < 5/10 */
        /* Single press adds 3/10 charge */
    } else {
        /* Fire laser gun and relay charge level */
        /* laser_charge -= 0.5f; // Cost per fire */
    }
}

/* --- LAST OF 2028 & SELF-DESTRUCT LOGIC --- */
void Handle_Last_Of_2028_Mode(void) {
    /* LED blinking at 2 Hz */

    /* Send "Drone was attacked" message every 1s */

    /* Rescue: double press within 10s -> revert to Battle Mode */

    /* If 10s timeout expires without rescue:
     * 1. Send "Drone has made a noble sacrifice for mankind..."
     * 2. Wait 1s -> Send "Cheerio!"
     * 3. Execute SELF-DESTRUCT and sever communication
     */
}

/* --- EXTI Interrupt for Push Button --- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == PB_Pin) {
        uint32_t current_time = HAL_GetTick();

        /* Double press vs single press within 0.5s window */
        if ((current_time - last_pb_press_time) < 500) {
            pb_press_count++;
        } else {
            pb_press_count = 1;
        }
        last_pb_press_time = current_time;

        if (pb_press_count == 2) {
            if (current_state == STANDBY_MODE) {
                current_state = BATTLE_MODE; /* Double press enters Battle Mode */
            } else if (current_state == LAST_OF_2028) {
                current_state = BATTLE_MODE; /* Rescue action */
            }
            pb_press_count = 0;
        }
    }
}

void Read_Sensors(void) {
    /* Read T, P, H, A, G, M from I2C/SPI sensors */
}

void Send_Telemetry(void) {
    /* Periodic UART telemetry stub */
}

void Send_Threshold_Warnings(void) {
    /* Threshold violation log stub */
}

uint8_t G_is_upside_down(void) {
    /* Return 1 when gyro/accel indicates inverted orientation */
    return upside_down_detected;
}

/* CubeMX / board stubs — replace with generated init code */
void SystemClock_Config(void) {
}

void MX_GPIO_Init(void) {
}

void MX_UART_Init(void) {
}
