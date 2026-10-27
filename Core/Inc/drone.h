#ifndef DRONE_H
#define DRONE_H

#include "stm32l4xx_hal.h"

/* Placeholder pin / threshold defines — replace with CubeMX / board values */
#ifndef PB_Pin
#define PB_Pin GPIO_PIN_13
#endif

#ifndef THRESHOLD_T
#define THRESHOLD_T 40.0f
#endif
#ifndef THRESHOLD_A
#define THRESHOLD_A 2.0f
#endif
#ifndef THRESHOLD_G
#define THRESHOLD_G 1.5f
#endif

typedef enum {
    STANDBY_MODE,
    BATTLE_MODE,
    TEST_OF_EE2028
} DroneState_t;

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_UART_Init(void);
void Read_Sensors(void);
void Send_Telemetry(void);
void Send_Threshold_Warnings(void);
void Handle_Standby_Mode(void);
void Handle_Battle_Mode(void);
void Handle_EE2028_Mode(void);
uint8_t G_is_upside_down(void);

#endif /* DRONE_H */
