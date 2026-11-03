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
2. **Failed-rescue self-destruct:** In `THE_LAST_OF_EE2028`, each double short-press while still upside down (`accel_z < -4`) counts as one rescue attempt. After **5** failed attempts → self-destruct → `Cheerio!`.
3. **10 s / 10 attack timeout:** While in `THE_LAST_OF_EE2028`, `Drone was attacked!` prints once per second. After **10** messages (≈10 s) with no successful rescue → self-destruct → `Cheerio!`.
4. **Laser fire (full-auto hold):** In `BATTLE_MODE`, hold **> 500 ms** to fire. While held and charge ≥ 5/10, shots continue every 500 ms (−5 each). Short single press (after 500 ms window, no second click) recharges +3 when charge &lt; 5/10.

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
