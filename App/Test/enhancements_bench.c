/**
 * @file enhancements_bench.c
 * @brief UART bench harness that drives the shared Enhancements adapter.
 *
 * Build with -DEE_ENHANCEMENTS=1 -DEE_BENCH=1 and call Enhancements_Bench_Init /
 * Enhancements_Bench_Update from a Cube main (or status-display test shell)
 * instead of the live drone state machine.
 *
 * Keys feed simulated DroneStatus_t / DroneEvent_t — same adapter as live.
 */
#include "enhancements_bench.h"
#include "enhancements.h"
#include "enhancements_hw.h"
#include <stdio.h>
#include <string.h>

#ifndef EE_BENCH_RESCUE_MS
#define EE_BENCH_RESCUE_MS 10000U
#endif

static UART_HandleTypeDef *s_huart;
static DroneStatus_t s_status;
static uint32_t s_rescue_deadline_ms;
static uint8_t s_rescue_armed;

static void UartPrint(const char *msg)
{
    if ((s_huart == NULL) || (msg == NULL)) {
        return;
    }
    (void)HAL_UART_Transmit(s_huart, (uint8_t *)msg, (uint16_t)strlen(msg), 100U);
}

static void PrintMenu(void)
{
    UartPrint("\r\n--- Enhancements Bench (shared adapter) ---\r\n");
    UartPrint("1 : STANDBY\r\n");
    UartPrint("2 : BATTLE (charge 10, armed)\r\n");
    UartPrint("3 : THE_LAST_OF_2028 (10 s drone clock)\r\n");
    UartPrint("z : rescue -> BATTLE\r\n");
    UartPrint("f : fire shot event (-5 charge)\r\n");
    UartPrint("+/- : charge up/down\r\n");
    UartPrint("x : SELF_DESTRUCT_STARTED event\r\n");
    UartPrint("h : menu\r\n");
}

static void EnterLast(uint32_t now_ms)
{
    s_status.mode = THE_LAST_OF_2028;
    s_status.armed = 0U;
    s_rescue_deadline_ms = now_ms + EE_BENCH_RESCUE_MS;
    s_rescue_armed = 1U;
    s_status.rescue_remaining_ms = EE_BENCH_RESCUE_MS;
    UartPrint("bench: THE_LAST_OF_2028 (10s)\r\n");
}

void Enhancements_Bench_Init(UART_HandleTypeDef *huart, I2C_HandleTypeDef *hi2c)
{
    Enhancements_Config_t cfg;

    s_huart = huart;
    s_status.mode = STANDBY_MODE;
    s_status.charge = 10U;
    s_status.armed = 0U;
    s_status.rescue_remaining_ms = 0U;
    s_rescue_armed = 0U;

    (void)Enhancements_HW_Setup(&cfg, hi2c);
    Enhancements_Init(&cfg);
    PrintMenu();
}

void Enhancements_Bench_Update(uint32_t now_ms)
{
    uint8_t ch;

    if (s_rescue_armed != 0U) {
        if ((int32_t)(now_ms - s_rescue_deadline_ms) >= 0) {
            s_status.rescue_remaining_ms = 0U;
            s_status.mode = SELF_DESTRUCT_TRIGGERED;
            s_rescue_armed = 0U;
            Enhancements_OnEvent(DRONE_EVENT_SELF_DESTRUCT_STARTED, now_ms);
            UartPrint("bench: rescue timeout -> SELF_DESTRUCT\r\n");
        } else {
            s_status.rescue_remaining_ms = s_rescue_deadline_ms - now_ms;
        }
    }

    if ((s_huart != NULL) && (HAL_UART_Receive(s_huart, &ch, 1U, 0U) == HAL_OK)) {
        switch (ch) {
            case '1':
                s_status.mode = STANDBY_MODE;
                s_status.armed = 0U;
                s_status.rescue_remaining_ms = 0U;
                s_rescue_armed = 0U;
                UartPrint("bench: STANDBY\r\n");
                break;
            case '2':
                s_status.mode = BATTLE_MODE;
                s_status.armed = (s_status.charge >= 5U) ? 1U : 0U;
                s_status.rescue_remaining_ms = 0U;
                s_rescue_armed = 0U;
                UartPrint("bench: BATTLE\r\n");
                break;
            case '3':
            case 'l':
                EnterLast(now_ms);
                break;
            case 'z':
                if (s_status.mode == THE_LAST_OF_2028) {
                    s_status.mode = BATTLE_MODE;
                    s_status.armed = (s_status.charge >= 5U) ? 1U : 0U;
                    s_status.rescue_remaining_ms = 0U;
                    s_rescue_armed = 0U;
                    UartPrint("bench: rescued -> BATTLE\r\n");
                }
                break;
            case 'f':
            case 'F':
                if (s_status.charge >= 5U) {
                    s_status.charge = (uint8_t)(s_status.charge - 5U);
                    s_status.armed = (s_status.charge >= 5U) ? 1U : 0U;
                    Enhancements_OnEvent(DRONE_EVENT_LASER_FIRED, now_ms);
                    UartPrint("bench: LASER_FIRED\r\n");
                } else {
                    UartPrint("bench: charge too low\r\n");
                }
                break;
            case '+':
                if (s_status.charge < 10U) {
                    s_status.charge++;
                }
                s_status.armed = ((s_status.mode == BATTLE_MODE) &&
                                  (s_status.charge >= 5U))
                                     ? 1U
                                     : 0U;
                break;
            case '-':
                if (s_status.charge > 0U) {
                    s_status.charge--;
                }
                s_status.armed = ((s_status.mode == BATTLE_MODE) &&
                                  (s_status.charge >= 5U))
                                     ? 1U
                                     : 0U;
                break;
            case 'x':
                s_status.mode = SELF_DESTRUCT_TRIGGERED;
                s_rescue_armed = 0U;
                s_status.rescue_remaining_ms = 0U;
                Enhancements_OnEvent(DRONE_EVENT_SELF_DESTRUCT_STARTED, now_ms);
                UartPrint("bench: SELF_DESTRUCT_STARTED\r\n");
                break;
            case 'h':
            case 'H':
                PrintMenu();
                break;
            default:
                break;
        }
    }

    Enhancements_Update(&s_status, now_ms);
}
