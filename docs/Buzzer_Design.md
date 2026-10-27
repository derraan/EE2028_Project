# Buzzer Design

Task 3 implementation for the Grove buzzer enhancement.

**Status:** Task 3 is **complete and hardware-verified** (active buzzer on D2; rhythm patterns confirmed).

## Scope

- Non-blocking pattern engine driven by `Buzzer_Update(now_ms)`.
- Enum + switch-case based mode handling.
- Hardware-neutral tone backend callback.
- **Hardware confirmed: active Grove buzzer on `ARD_D2` (`PD14`)** — fixed pitch; all effects are **ON/OFF rhythms** (no PWM / pitch control).

## Files

- `App/Inc/buzzer.h`
- `App/Src/buzzer.c`
- `App/Inc/buzzer_test.h`
- `App/Test/buzzer_test.c`

Lab integration copies are synced to:

- `C:/Users/darre/STM32CubeIDE/workspace_1.19.0/EE2028_Project_Test/Core/Inc`
- `C:/Users/darre/STM32CubeIDE/workspace_1.19.0/EE2028_Project_Test/Core/Src`

## Public API

```c
void Buzzer_Init(Buzzer_HandleTypeDef *h, const Buzzer_Backend_t *backend);
void Buzzer_Play(Buzzer_HandleTypeDef *h, BuzzerPattern_t pattern);
void Buzzer_SetCountdown(Buzzer_HandleTypeDef *h, uint8_t value_0_10);
void Buzzer_Stop(Buzzer_HandleTypeDef *h);
void Buzzer_Reset(Buzzer_HandleTypeDef *h);
void Buzzer_Update(Buzzer_HandleTypeDef *h, uint32_t now_ms);
```

## Pattern enum

`BuzzerPattern_t`:

1. `BUZZER_PATTERN_SILENT`
2. `BUZZER_PATTERN_THREAT_CHIRP`
3. `BUZZER_PATTERN_LASER_FIRE`
4. `BUZZER_PATTERN_LAST_WARNING` (2 Hz loop)
5. `BUZZER_PATTERN_COUNTDOWN` (accelerating loop)
6. `BUZZER_PATTERN_FINAL_BURST` (one-shot, then latch silent)

## Active-buzzer design (rhythm only)

An active buzzer has its own oscillator. GPIO HIGH = fixed tone; GPIO LOW = silence.  
`freq_hz` in step tables is kept for API compatibility but is **always 0 / ignored** by the current GPIO backend.

| Pattern | Rhythm |
|---------|--------|
| Threat chirp | Double tap: `ON 60` → `OFF 70` → `ON 60` |
| Laser fire | “Pew”: three stutters then hold — `ON 40` / `OFF 30` ×3 → `ON 180` |
| LAST warning | 2 Hz loop: `ON 250` → `OFF 250` |
| Countdown | Beep `ON 60`; gap `60 + value×80` ms (`value` = 10..0, faster near 0) |
| Final burst | Accelerating pulses → long `ON 250`, then latched silence |

## Arbitration and state model

- Looping base pattern is tracked separately (`SILENT`, `LAST_WARNING`, `COUNTDOWN`).
- One-shot patterns (`THREAT_CHIRP`, `LASER_FIRE`, `FINAL_BURST`) start only when their priority is not lower than the currently active pattern.
- When a one-shot ends, controller resumes the looping base pattern.
- `FINAL_BURST` sets a latched-silent flag when done; output remains off until `Buzzer_Reset`.
- `Buzzer_Stop` immediately forces silent (used for rescue behavior).

## Hardware backend

Backend interface:

```c
typedef struct {
    void (*set_tone)(void *ctx, uint8_t on, uint16_t freq_hz);
    void *ctx;
} Buzzer_Backend_t;
```

Current test backend:

- Re-inits `PD14` (`ARD_D2`) as GPIO output push-pull, default LOW.
- Uses `HAL_GPIO_WritePin` for on/off (active module).
- PWM / pitch path deferred; not needed with this SKU.

## UART test controls

Buzzer controls are integrated into the existing status-display menu and forwarded to `Buzzer_Test_HandleKey`:

- `t`: tone probe ON for 1 s
- `c`: threat chirp (double tap)
- `w`: LAST warning loop
- `k`: countdown 10..0 demo
- `F`: laser fire “pew” rhythm
- `X`: final burst
- `Z`: stop/silent

Uppercase `F/X/Z` avoid collisions with matrix keys `f/x/z`.

## Build / flash

- Sources listed in CubeIDE `Debug/Core/Src/subdir.mk` and `objects.list`.
- Flash from CubeIDE, then: `.\scripts\serial_monitor.ps1` (COM6 @ 115200).
