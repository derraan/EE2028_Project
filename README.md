# EE2028 Project — Integration (Adapter)

PR #3 basic drone baseline plus a thin shared enhancements adapter (matrix, buzzer, IR, relay). Telemetry/VESPER is a separate plan and is not required for this milestone.

## Modes (PR #3 baseline — frozen gameplay)

| State | Description |
|-------|-------------|
| `STANDBY_MODE` | LED on; telemetry; double-press or T/A/G thresholds → Battle |
| `BATTLE_MODE` | LED 1 Hz; **hold-to-fire** (≥500 ms, charge ≥5/10, −5 per shot); upside-down → The Last of EE2028 |
| `THE_LAST_OF_2028` | LED 2 Hz; attack alerts; double-press rescue or self-destruct |
| `SELF_DESTRUCT_*` | Noble sacrifice → `Cheerio!` → bounded final effects (if enhancements on) → permanent halt |

Frozen rules (do not “fix” from older checklist wording without a focused patch):

1. **Fire:** hold-to-fire full-auto in Battle (not automatic fire at 5/10).
2. **Armed (display):** Battle and charge ≥ 5/10.
3. **Battle entry:** double-press from Standby, or T/A/G threshold trip (PR #3).
4. **Self-destruct:** 10 s without rescue **or** 5 failed upside-down rescues (whichever first).
5. **IR:** detect + chirp + log only — no mode/fire authority.

Enum name is `THE_LAST_OF_2028`; matrix banner text remains `THE LAST OF EE2028`.

## Layout

```
Core/
  Inc/drone.h          — shared DroneState_t
  Src/main.c           — PR #3 state machine + thin EE hooks
App/
  Inc|Src/             — ht16k33, status_display, buzzer, ir_threat, relay_effect
  Inc|Src/enhancements.*     — shared adapter (live + bench)
  Inc|Src/enhancements_hw.*  — GPIO/I2C backends (D2/D3/D4)
  Test/enhancements_bench.c  — UART bench feeding the same adapter
  Test/*_test.c              — module-level harnesses (matrix/buzzer/IR/relay)
docs/                  — design notes + button enhancement
```

## Build configurations

| Macro | Meaning |
|-------|---------|
| *(default)* | PR #3 baseline only — enhancements compiled out |
| `-DEE_ENHANCEMENTS=1` | Live: adapter updates from drone status/events |
| `-DEE_ENHANCEMENTS=1 -DEE_BENCH=1` | Bench: UART menu drives the same adapter (use `Enhancements_Bench_*`) |

Live call sites (only):

- `Enhancements_OnEvent(DRONE_EVENT_LASER_FIRED, …)` where charge is deducted
- `Enhancements_OnEvent(DRONE_EVENT_SELF_DESTRUCT_STARTED, …)` inside `Enter_Self_Destruct()`
- `Enhancements_Update(&drone_status, now_ms)` each loop tick
- Bounded final-effects wait in `SELF_DESTRUCT_DEAD` before halt

One rescue clock: `rescue_remaining_ms` is derived from `the_last_of_2028_entry_tick` + `RESCUE_TIMEOUT_MS`. The matrix/buzzer consume that remaining time (no second 21 s display countdown in live mode).

## Board / Cube status (honest)

**Board of record:** **B-L4S5I-IOT01A** (STM32L4S5VITx). PR #3 / Lin baseline BSP paths (`Drivers/BSP/B-L4S5I-IOT01`, `stm32l4s5i_iot01*`) match this kit.

| Item | Status |
|------|--------|
| **Baseline source** | Lin Thant PR #3 (`linthant/basictasks`) brought into this worktree |
| **Enhancement modules** | Copied from `darren/enhancements` `App/` |
| **CubeIDE project / HAL / BSP** | **Not in this checkout** — `main.c` still references `B-L4S5I-IOT01` BSP paths and `main.h` |
| **Board target** | **B-L4S5I-IOT01A** — confirmed; BSP folder `B-L4S5I-IOT01` / `stm32l4s5i_iot01*` aligns with PR #3 |
| **Firmware build** | **Blocked here** until a Cube project with Drivers/BSP (and optional I2C handle for the HT16K33) is linked. App modules + adapter are structured to drop into that project |
| **Hardware verify** | Matrix/buzzer/IR/relay were verified on Darren’s bench branch; live integration on-board is pending Cube + wiring |

Arduino connector aliases (from lab Cube `main.h` / B-L4S5I-IOT01A `.ioc`): buzzer **D2 = PD14**, IR **D3 = PB0** (active-LOW), `CAP_EXPLODE` **D4 = PA3**. Prefer Cube-generated `ARD_D*` labels when the project is linked. Pass the matrix I2C handle via `EE_MATRIX_I2C` / `Enhancements_HW_Setup(..., &hi2cN)`; `NULL` skips the matrix safely (relay/buzzer/IR still run).

## Next steps (when Cube is present)

1. Add `App/Inc` + `App/Src` to the CubeIDE include/source paths; define `EE_ENHANCEMENTS=1` for the live config.
2. Wire `EE_MATRIX_I2C` to the HT16K33 I2C handle after `MX_I2Cx_Init`.
3. Confirm USER button EXTI both edges + sensor BSP init as in PR #3.
4. Bench: run `Enhancements_Bench_Init/Update` on Virtual COM to exercise adapter without the full drone loop.
