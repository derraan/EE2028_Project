# EE2028 Project — Spaceship Drone

STM32L4 skeleton for the EE2028 spaceship drone state machine.

## Modes

| State | Description |
|-------|-------------|
| `STANDBY_MODE` | LED on; telemetry; double-press or T/A/G thresholds → Battle |
| `BATTLE_MODE` | LED 1 Hz; laser charge/fire; upside-down → Test of EE2028 |
| `TEST_OF_EE2028` | LED 2 Hz; attack alerts; double-press rescue or self-destruct |

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
