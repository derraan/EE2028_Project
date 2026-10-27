#ifndef STATUS_DISPLAY_H
#define STATUS_DISPLAY_H

#include "ht16k33.h"

typedef enum {
    STATUS_SCREEN_BLANK = 0,
    STATUS_SCREEN_RADAR,
    STATUS_SCREEN_CHARGE,
    STATUS_SCREEN_ARMED,
    STATUS_SCREEN_BATTLE_TEXT,
    STATUS_SCREEN_LAST_2028,
    STATUS_SCREEN_EXPLOSION
} StatusScreen_t;

typedef enum {
    STATUS_RADAR_LINE = 0,
    STATUS_RADAR_DIAL
} StatusRadarStyle_t;

typedef struct {
    HT16K33_HandleTypeDef *dev;
    uint8_t charge;
    uint8_t azimuth;
    uint8_t radar_style;   /* StatusRadarStyle_t */
    uint8_t armed;

    uint8_t battle_active;
    uint32_t battle_start_ms;

    uint8_t last_active;
    uint8_t countdown_on;
    uint8_t countdown_val;
    uint32_t countdown_start_ms;

    uint8_t explosion_active;
    uint32_t explosion_start_ms;
    uint8_t terminated;

    uint16_t scroll_col;
    uint32_t last_step_ms;
    StatusScreen_t last_drawn;
    uint8_t dirty;
} StatusDisplay_HandleTypeDef;

void StatusDisplay_Init(StatusDisplay_HandleTypeDef *h, HT16K33_HandleTypeDef *dev);
void StatusDisplay_SetCharge(StatusDisplay_HandleTypeDef *h, uint8_t charge_0_10);
void StatusDisplay_SetArmed(StatusDisplay_HandleTypeDef *h, uint8_t armed);
void StatusDisplay_SetAzimuth(StatusDisplay_HandleTypeDef *h, uint8_t col_0_7);
void StatusDisplay_SetRadarStyle(StatusDisplay_HandleTypeDef *h, StatusRadarStyle_t style);

void StatusDisplay_EnterBattle(StatusDisplay_HandleTypeDef *h);
void StatusDisplay_EnterLastOf2028(StatusDisplay_HandleTypeDef *h);
void StatusDisplay_Rescue(StatusDisplay_HandleTypeDef *h);
void StatusDisplay_TriggerExplosion(StatusDisplay_HandleTypeDef *h);
void StatusDisplay_Reset(StatusDisplay_HandleTypeDef *h);

HAL_StatusTypeDef StatusDisplay_Update(StatusDisplay_HandleTypeDef *h, uint32_t now_ms);

void StatusDisplay_Test_Init(StatusDisplay_HandleTypeDef *h,
                             HT16K33_HandleTypeDef *dev,
                             UART_HandleTypeDef *huart);
void StatusDisplay_Test_Update(uint32_t now_ms);

#endif /* STATUS_DISPLAY_H */
