# EE2028 Project — Spaceship Drone

STM32L4 skeleton for the EE2028 spaceship drone state machine.

## Modes

| State | Description |
|-------|-------------|
| `STANDBY_MODE` | LED on; telemetry; double-press or T/A/G thresholds → Battle |
| `BATTLE_MODE` | LED 1 Hz; laser charge/fire; upside-down → The Last of EE2028 |
| `THE_LAST_OF_EE2028` | LED 2 Hz; attack alerts; double-press rescue or self-destruct |
| `SELF_DESTRUCT_*` | Noble sacrifice → `Cheerio!` → permanent halt |

## Button enhancement (button only)

Dual-edge EXTI + GPIO duration windows, debounce, and how gestures change mode/laser/rescue flow: **[docs/Button_Input_Enhancement.md](docs/Button_Input_Enhancement.md)**.

## Noted behaviors (in `Core/Src/main.c`)

1. **Push button (rising + falling + GPIO):** see button enhancement doc above.
2. **Self-destruct (whichever first):** In `THE_LAST_OF_EE2028` — (a) **10 s** wall-clock without a successful rescue, or (b) **5** failed rescue doubles while still upside down. Attack alerts print once per second but are **not** counted for explode.
3. **Laser fire (full-auto hold):** In `BATTLE_MODE`, hold **> 500 ms** to fire. While held and charge ≥ 5/10, shots continue every 500 ms (−5 each). Short single press (after 500 ms window, no second click) recharges +3 when charge &lt; 5/10.

## Layout

```
Core/
  Inc/drone.h   — states, thresholds, prototypes
  Src/main.c    — main loop, mode handlers, PB EXTI callback
```

## Next steps

- Wire CubeMX-generated HAL init (`SystemClock_Config`, GPIO, UART, EXTI)
- Implement sensor reads (T, P, H, A, G, M) and UART messaging
- Fill in laser charge / self-destruct timing logic
