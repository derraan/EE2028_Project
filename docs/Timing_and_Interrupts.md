# Timing and interrupts

How `Core/Src/main.c` keeps time without blocking, and exactly which interrupts the firmware uses.

**Hard rule:** no code under `Core/` or `App/` calls `HAL_Delay()`. Every wait is a `HAL_GetTick()` comparison. Check with `git grep -n HAL_Delay -- Core App` (only a comment in `main.c` that states the rule matches).

---

## 1. The time base: SysTick → `HAL_GetTick()`

| Piece | Where | What it does |
|-------|-------|--------------|
| `HAL_Init()` | `main()` | Configures SysTick for a 1 ms period (`HAL_InitTick`) at `TICK_INT_PRIORITY` |
| `SysTick_Handler()` | Cube's `Core/Src/stm32l4xx_it.c` (not in this repo) | Calls `HAL_IncTick()`: a free-running `uint32_t` millisecond counter |
| `HAL_GetTick()` | everywhere | Reads that counter; never waits |

`TICK_INT_PRIORITY` is 0 in the lab project's `stm32l4xx_hal_conf.h`, so SysTick is the highest-priority user interrupt and the tick keeps advancing even while another ISR runs.

### The non-blocking pattern

Every periodic or timed action is a stored timestamp plus an unsigned subtraction:

```c
if ((HAL_GetTick() - led_blink_tick) >= 500) {   /* due? */
    led_blink_tick = HAL_GetTick();              /* re-arm */
    BSP_LED_Toggle(LED2);
}
```

Unsigned subtraction stays correct when the 32-bit counter wraps (about every 49.7 days). Deadlines in the future use the signed form `(int32_t)(now - deadline) >= 0` (single-press window, servo self-test).

The main loop is a single cooperative super-loop with no delay at the bottom. Each pass it reads sensors, updates the heading filter, runs `Process_PushButton()`, runs one state-machine step, then `Enhancements_Tick()`. It spins as fast as the sensor reads allow. Each task decides for itself whether it is due.

### Timers in `main.c`

| Timer | Variable | Period / window |
|-------|----------|-----------------|
| Loop `dt` (heading filter) | `last_tick` | measured each pass, clamped to 0.01 s … 0.5 s |
| Standby telemetry / threshold log | `tick_1s_telemetry` | 1000 ms |
| Battle LED blink | `led_blink_tick` | 500 ms (1 Hz) |
| Last-of-2028 LED blink | `led_blink_tick` | 250 ms (2 Hz) |
| "Drone was attacked" + `EVT COUNTDOWN` | `tick_1s_attack_msg` | `ATTACK_MSG_PERIOD_MS` = 1000 ms |
| Rescue deadline | `the_last_of_2028_entry_tick` | `RESCUE_TIMEOUT_MS` = 10 000 ms |
| Sacrifice → `Cheerio!` | `noble_sacrifice_tick` | 1000 ms |
| Button hold | `pb_down_tick` | `PB_HOLD_MS` = 500 ms |
| Double-press window | `pb_last_short_press_tick`, `pb_single_deadline_tick` | `PB_DOUBLE_PRESS_MS` = 500 ms |
| Full-auto fire spacing | `pb_last_fire_tick` | `PB_FIRE_PERIOD_MS` = 500 ms |
| Radar link stats line | `radar_link_tick` | 5000 ms (radar build only) |
| Yaw filter boot window | `start_tick` | `YAW_BOOT_FAST_MS` = 2000 ms |

### Timing in `App/` modules

Every module takes the time as an argument and keeps its own state machine; none sleeps:

- `StatusDisplay_Update(now_ms)`, `Buzzer_Update(now_ms)`, `RelayEffect_Update(now_ms)` and the IR/radar/aim controllers are all stepped from `Enhancements_Update(&status, now_ms)`, which `Enhancements_Tick()` calls once per loop pass.
- After self-destruct, `SELF_DESTRUCT_DEAD` spins `Enhancements_Update()` until `Enhancements_FinalEffectsDone(now)` says the matrix, buzzer and relay effects are over. That is a bounded busy-wait on the tick, not a delay.
- The optional servo bench sweep (`-DEE_SERVO_SELFTEST=1`) is a state machine, `Enhancements_HW_ServoSelfTest()` to arm and `Enhancements_HW_ServoSelfTestStep(now)` once per pass. It holds, glides in 25 small steps and holds again by comparing ticks. While it runs, `Enhancements_Tick()` skips the aim update so the two do not fight over the servo.

### What still blocks (honest list)

Non-blocking here means "no scheduled waits". A few hardware transfers are still polled and finish on their own:

| Call | Where | Worst-case block |
|------|-------|------------------|
| `BSP_*_Get/Read` (I2C2 sensors) | `main.c` loop | a few hundred µs each, tens of ms if the bus faults |
| `HAL_UART_Transmit` (USART1, ST-LINK VCP) | `App/Src/telemetry_uart.c` | about 9 ms for a 100-byte line at 115200 baud (the call waits for the bytes to leave); HAL timeout 100 ms |
| `HAL_I2C_Master_Transmit` (HT16K33 matrix) | `App/Src/ht16k33.c` | well under 1 ms per frame |
| `HAL_UART_Transmit` (radar commands) | `App/Src/enhancements_hw.c` | 50 ms timeout, only on the rare resend |

These are bounded by the HAL timeouts and use the SysTick counter themselves, which is why SysTick must keep its high priority.

---

## 2. Interrupts

### Inventory

Up to **4 interrupt requests**, served by **4 IRQ handlers** plus **1 shared application callback** (`HAL_GPIO_EXTI_Callback`):

| # | Source | Vector / handler | Defined in | Priority (lower = more urgent) | When |
|---|--------|------------------|-----------|-------------------------------|------|
| 1 | SysTick (1 kHz) | `SysTick_Handler()` → `HAL_IncTick()` | Cube `stm32l4xx_it.c` | 0 (`TICK_INT_PRIORITY`) | always |
| 2 | USER button, EXTI line 13 (PC13, both edges) | `EXTI15_10_IRQHandler()` → `HAL_GPIO_EXTI_IRQHandler(BUTTON_EXTI13_Pin)` → **`HAL_GPIO_EXTI_Callback()`** | handler: Cube `stm32l4xx_it.c`; callback: `Core/Src/main.c` | 15 (set by `BSP_PB_Init`) | always |
| 3 | UART4 RX (RD-03D radar) | `UART4_IRQHandler()` → `Enhancements_HW_RadarIrq()` | `App/Src/enhancements_hw.c` | 5 (`EE_RADAR_IRQ_PRIO`) | only with `-DEE_RADAR_AIM=1` |
| 4 | Wi-Fi DRDY, EXTI line 1 (PE1, rising) | `EXTI1_IRQHandler()` → `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1)` → **`HAL_GPIO_EXTI_Callback()`** → `Telemetry_WifiDrdyIsr()` | handler + ISR body: `App/Src/telemetry_wifi.c`; pin dispatch: `Core/Src/main.c` | 6 (`WIFI_IRQ_PRIO`) | only when `App/Inc/wifi_config.h` enables Wi-Fi |

Baseline build: **2** (SysTick, button). Radar build: **3**. Radar + Wi-Fi: **4**.

Notes:

- `HAL_GPIO_EXTI_Callback` is shared: `GPIO_PIN_1` → Wi-Fi DRDY; `USER_BUTTON_PIN` → button edge stamp; any other pin returns immediately.
- `EXTI15_10_IRQHandler` in the lab project also forwards lines 10, 11, 12, 14 and 15 to the HAL (other on-board pins). Those lines are not enabled by our code, so they never fire. `EXTI9_5_IRQHandler` is likewise unused.
- Set `-DEE_RADAR_OWN_IRQ=0` if Cube already defines `UART4_IRQHandler`, and call `Enhancements_HW_RadarIrq()` from it yourself.
- Everything else is polled: USART1 telemetry TX, I2C sensors and matrix, TIM3 PWM for the servo (no timer interrupt), GPIO outputs for buzzer, relay and laser, and the IR input.
- Priority grouping is `NVIC_PRIORITYGROUP_4` (16 preemption levels). SysTick (0) can preempt UART4 (5) and Wi-Fi DRDY (6), which can preempt the button (15). The button interrupt is the least urgent on purpose.

### Handler 1: SysTick

`HAL_IncTick()` only. This is the one place the millisecond counter changes.

### Handler 2: button edge (`HAL_GPIO_EXTI_Callback`, `USER_BUTTON_PIN`)

```c
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_1) {
        Telemetry_WifiDrdyIsr(); /* Wi-Fi path — see Handler 4 */
        return;
    }
    if (GPIO_Pin != USER_BUTTON_PIN) return;
    uint32_t now = HAL_GetTick();
    pb_raw_edge = true;                /* every edge, even debounce-rejected */
    pb_raw_edge_tick = now;
    if ((now - pb_last_edge_tick) < PB_DEBOUNCE_MS) return;   /* 100 ms */
    pb_last_edge_tick = now;
    pb_edge_tick = now;
    pb_edge_pending = true;
}
```

- Runs in a few instructions: no printing, no mode change, no I2C or UART.
- It stamps a debounced edge. `Process_PushButton()` **must consume** that stamp: a new press starts only when `pb_edge_pending` was set and GPIO is low. The one exception is a re-press inside 100 ms of the last accepted edge: debounce rejects it, but `pb_raw_edge` still records it, and the press starts once the line has been quiet for 100 ms. Hold keeps sampling level while already down. Release prefers the EXTI timestamp. It can fall back to GPIO level, but only after the same 100 ms quiet period, so bounce cannot fake a release.
- The shared variables `pb_last_edge_tick`, `pb_edge_pending` and `pb_edge_tick` are `volatile`. Clearing the flag without using it (pure GPIO polling) is not allowed.
- Interrupt requests handled: one per accepted edge, so 2 per click (press + release), at most one per 100 ms because of the debounce.

### Handler 3: radar RX (`Enhancements_HW_RadarIrq`)

- UART4 at 256000 baud with the 8-byte RX FIFO enabled (threshold 1/8), so one interrupt drains everything that has arrived (`while (ISR & RXNE)`).
- Each byte goes into a 512-byte single-producer/single-consumer ring buffer (`s_rx_ring`, `s_rx_head` written only by the ISR, `s_rx_tail` read/advanced only by the main loop; both `volatile`).
- Ring full → byte dropped and counted in `s_rx_dropped`; the frame parser resynchronises on the next header.
- Overrun / noise / framing / parity errors are cleared and counted in `s_rx_errors`.
- The main loop never touches UART4 registers for receive; `Enhancements_Tick()` parses the ring and, every 5 s, prints `Radar link: rx=… dropped=… uart_err=…`.
- Request rate: the RD-03D sends about 300 bytes/s (~10 frames/s), so the ISR fires on the order of tens of times per second.

### Handler 4: Wi-Fi DRDY (`EXTI1_IRQHandler` → `Telemetry_WifiDrdyIsr`)

- Enabled only when `Telemetry_WifiInit()` succeeds (`App/Inc/wifi_config.h` present).
- `EXTI1_IRQHandler` in `telemetry_wifi.c` calls `HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1)`.
- `HAL_GPIO_EXTI_Callback` dispatches pin 1 to `Telemetry_WifiDrdyIsr()`, which only sets a volatile “data ready” flag for the non-blocking SPI/AT state machine in `Telemetry_WifiPoll()`.
- No SPI transfers and no telemetry prints inside the ISR.

### Rules for adding interrupts

1. Keep the ISR to flags, counters and ring-buffer writes.
2. Mark every variable shared with the main loop `volatile` and give it exactly one writer.
3. Never call `Telemetry_Printf`, I2C or `HAL_UART_Transmit` from an ISR.
4. Use `HAL_GetTick()` inside an ISR only for timestamps, never to wait.
5. Do not add `HAL_Delay()`; add a tick-driven state machine instead.
