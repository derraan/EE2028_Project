#include "status_display.h"
#include "buzzer_test.h"
#include "ir_threat_test.h"
#include <string.h>

typedef enum {
    AUTO_IDLE = 0,
    AUTO_CHARGE_RAMP,
    AUTO_ARMED_FLASH,
    AUTO_BATTLE_SCROLL,
    AUTO_RADAR_SWEEP,
    AUTO_LAST_2028,
    AUTO_WAIT_EXPLOSION,
    AUTO_DONE
} StatusAutoState_t;

static StatusDisplay_HandleTypeDef *s_status = NULL;
static UART_HandleTypeDef *s_huart = NULL;
static uint8_t s_radar_cycle = 0xFFU;
static StatusAutoState_t s_auto_state = AUTO_IDLE;
static uint32_t s_auto_step_start = 0U;
static uint8_t s_auto_charge = 0U;
static uint8_t s_auto_azimuth = 0U;

static void UartPrint(const char *msg)
{
    if ((s_huart == NULL) || (msg == NULL)) {
        return;
    }
    (void)HAL_UART_Transmit(s_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
}

static void PrintMenu(void)
{
    UartPrint("\r\n--- Status Display Test Menu ---\r\n");
    UartPrint("+ : charge up (0-10)\r\n");
    UartPrint("- : charge down (0-10)\r\n");
    UartPrint("r : cycle radar azimuth 0..7 then off\r\n");
    UartPrint("d : toggle radar dial (vs line), keep azimuth\r\n");
    UartPrint("f : toggle armed flash\r\n");
    UartPrint("b : BATTLE MODE one-shot scroll\r\n");
    UartPrint("l : LAST OF EE2028 (10s banner, 10..0 flash, boom)\r\n");
    UartPrint("z : rescue from LAST OF 2028\r\n");
    UartPrint("x : trigger explosion now\r\n");
    UartPrint("0 : reset display state\r\n");
    UartPrint("s : run full auto sequence\r\n");
    UartPrint("--- Buzzer ---\r\n");
    Buzzer_Test_PrintMenu();
    IR_Test_PrintMenu();
    UartPrint("note: buzzer uses uppercase F/X/Z to avoid key clash\r\n");
    UartPrint("h : show this menu\r\n");
}

static void AutoSequence_Start(uint32_t now_ms)
{
    s_auto_state = AUTO_CHARGE_RAMP;
    s_auto_step_start = now_ms;
    s_auto_charge = 0U;
    s_auto_azimuth = 0U;
    StatusDisplay_Reset(s_status);
    StatusDisplay_SetCharge(s_status, 0U);
    StatusDisplay_SetArmed(s_status, 0U);
    StatusDisplay_SetAzimuth(s_status, 0xFFU);
    UartPrint("AUTO: started\r\n");
}

static void AutoSequence_Update(uint32_t now_ms)
{
    if ((s_status == NULL) || (s_auto_state == AUTO_IDLE)) {
        return;
    }

    switch (s_auto_state) {
        case AUTO_CHARGE_RAMP:
            if ((now_ms - s_auto_step_start) >= 300U) {
                s_auto_step_start = now_ms;
                if (s_auto_charge < 10U) {
                    s_auto_charge++;
                    StatusDisplay_SetCharge(s_status, s_auto_charge);
                } else {
                    StatusDisplay_SetArmed(s_status, 1U);
                    s_auto_state = AUTO_ARMED_FLASH;
                }
            }
            break;

        case AUTO_ARMED_FLASH:
            if ((now_ms - s_auto_step_start) >= 2000U) {
                s_auto_step_start = now_ms;
                StatusDisplay_EnterBattle(s_status);
                s_auto_state = AUTO_BATTLE_SCROLL;
            }
            break;

        case AUTO_BATTLE_SCROLL:
            if ((now_ms - s_auto_step_start) >= 4000U) {
                s_auto_step_start = now_ms;
                StatusDisplay_SetArmed(s_status, 0U);
                StatusDisplay_SetCharge(s_status, 0U);
                StatusDisplay_SetAzimuth(s_status, 0U);
                s_auto_azimuth = 0U;
                s_auto_state = AUTO_RADAR_SWEEP;
            }
            break;

        case AUTO_RADAR_SWEEP:
            if ((now_ms - s_auto_step_start) >= 220U) {
                s_auto_step_start = now_ms;
                if (s_auto_azimuth < 7U) {
                    s_auto_azimuth++;
                    StatusDisplay_SetAzimuth(s_status, s_auto_azimuth);
                } else {
                    StatusDisplay_EnterLastOf2028(s_status);
                    s_auto_state = AUTO_LAST_2028;
                    s_auto_step_start = now_ms;
                }
            }
            break;

        case AUTO_LAST_2028:
            /* 10 s banner + 11 s (10..0) countdown before explosion */
            if ((now_ms - s_auto_step_start) >= 21500U) {
                s_auto_state = AUTO_WAIT_EXPLOSION;
                s_auto_step_start = now_ms;
            }
            break;

        case AUTO_WAIT_EXPLOSION:
            if ((now_ms - s_auto_step_start) >= 1300U) {
                s_auto_state = AUTO_DONE;
                s_auto_step_start = now_ms;
            }
            break;

        case AUTO_DONE:
            UartPrint("AUTO: done\r\n");
            s_auto_state = AUTO_IDLE;
            break;

        case AUTO_IDLE:
        default:
            break;
    }
}

void StatusDisplay_Test_Init(StatusDisplay_HandleTypeDef *h,
                             HT16K33_HandleTypeDef *dev,
                             UART_HandleTypeDef *huart)
{
    s_status = h;
    s_huart = huart;
    s_radar_cycle = 0xFFU;
    s_auto_state = AUTO_IDLE;
    StatusDisplay_Init(s_status, dev);
    Buzzer_Test_Init(huart);
    IR_Test_Init(huart);
    PrintMenu();
}

void StatusDisplay_Test_Update(uint32_t now_ms)
{
    uint8_t ch;

    if (s_status == NULL) {
        return;
    }

    if ((s_huart != NULL) && (HAL_UART_Receive(s_huart, &ch, 1U, 0U) == HAL_OK)) {
        switch (ch) {
            case '+':
                if (s_status->charge < 10U) {
                    StatusDisplay_SetCharge(s_status, (uint8_t)(s_status->charge + 1U));
                }
                break;
            case '-':
                if (s_status->charge > 0U) {
                    StatusDisplay_SetCharge(s_status, (uint8_t)(s_status->charge - 1U));
                }
                break;
            case 'r':
                if (s_radar_cycle == 0xFFU) {
                    s_radar_cycle = 0U;
                } else if (s_radar_cycle < 7U) {
                    s_radar_cycle++;
                } else {
                    s_radar_cycle = 0xFFU;
                }
                StatusDisplay_SetAzimuth(s_status, s_radar_cycle);
                break;
            case 'd':
            case 'D':
                if (s_status->radar_style == (uint8_t)STATUS_RADAR_DIAL) {
                    StatusDisplay_SetRadarStyle(s_status, STATUS_RADAR_LINE);
                    UartPrint("radar: LINE\r\n");
                } else {
                    StatusDisplay_SetRadarStyle(s_status, STATUS_RADAR_DIAL);
                    if (s_status->azimuth >= 8U) {
                        s_radar_cycle = 0U;
                        StatusDisplay_SetAzimuth(s_status, 0U);
                    }
                    /* Clear higher-priority screens so dial is visible */
                    StatusDisplay_SetCharge(s_status, 0U);
                    StatusDisplay_SetArmed(s_status, 0U);
                    UartPrint("radar: DIAL 180deg (r: L->up->R)\r\n");
                }
                break;
            case 'f':
                StatusDisplay_SetArmed(s_status, (uint8_t)((s_status->armed == 0U) ? 1U : 0U));
                break;
            case 'b':
                StatusDisplay_EnterBattle(s_status);
                break;
            case 'l':
                StatusDisplay_EnterLastOf2028(s_status);
                break;
            case 'z':
                StatusDisplay_Rescue(s_status);
                break;
            case 'x':
                StatusDisplay_TriggerExplosion(s_status);
                break;
            case '0':
                StatusDisplay_Reset(s_status);
                break;
            case 's':
                AutoSequence_Start(now_ms);
                break;
            case 'h':
            case 'H':
                PrintMenu();
                break;
            default:
                if (IR_Test_HandleKey(ch, now_ms) == 0U) {
                    (void)Buzzer_Test_HandleKey(ch, now_ms);
                }
                break;
        }
    }

    AutoSequence_Update(now_ms);
    IR_Test_Update(now_ms);
    Buzzer_Test_Update(now_ms);
    (void)StatusDisplay_Update(s_status, now_ms);
}
