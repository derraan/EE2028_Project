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

/*
 * Canonical mode enum shared by the baseline state machine and the
 * enhancements adapter. PR #3 gameplay (hold-to-fire, sensor Battle entry,
 * 10 s / 5-failed-rescue self-destruct) lives in Core/Src/main.c.
 */
typedef enum {
    STANDBY_MODE = 0,
    BATTLE_MODE,
    THE_LAST_OF_2028,
    SELF_DESTRUCT_TRIGGERED,
    SELF_DESTRUCT_DEAD
} DroneState_t;

#ifndef RESCUE_TIMEOUT_MS
#define RESCUE_TIMEOUT_MS 10000U
#endif

#endif /* DRONE_H */
