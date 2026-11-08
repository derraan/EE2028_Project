# EE2028 Spaceship Drone

Firmware and mission-control UI for Assignment 2 — *The Last of EE2028*.

The Discovery board runs a three-mode drone state machine (sensors, laser charge/fire, rescue / self-destruct). Optional Grove hardware (LED matrix, buzzer, IR, relay) rides on a thin adapter. A local browser dashboard (**VESPER**) shows the same UART stream markers would read in Tera Term.

**Board of record:** ST **B-L4S5I-IOT01A**.

---

## What you can do today vs later

| Without a Cube project | With Cube + board |
|------------------------|-------------------|
| Read this summary and the design docs | Flash baseline or enhanced firmware |
| Replay a mission in VESPER from fixtures | Live UART / Web Serial into VESPER |
| Run the offline UART parser smoke test | Wire Grove modules on D2/D3/D4 + matrix I2C |

This git tree has application source (`Core/`, `App/`, `Command/`) but **does not** contain the STM32CubeIDE project, HAL, or BSP. You cannot build/flash from this checkout alone until those are linked. Details: [docs/Next_Steps.md](docs/Next_Steps.md).

---

## How a mission works

```text
STANDBY ──double-press or T/A/G──► BATTLE ──upside-down──► THE LAST OF EE2028
   ▲                                  │                         │
   └────────── double-press ──────────┘         rescue ─────────┤
                                                                ▼
                                              self-destruct → Cheerio! → halt
```

1. **Standby** — status LED on; telemetry every second; ignore a single press.
2. **Battle** — LED blinks 1 Hz. Laser charge is the official **0–50** scale (starts 50/50). Each short press adds **+3** at once (cap 50). **Hold** the button ≥500 ms with charge ≥25/50 to fire full-auto (−5 per shot). Double-press returns to Standby, but only at 50/50.
3. **The Last of EE2028** — board flipped upside-down. LED blinks 2 Hz; attack lines at 1 Hz. Upright double-press **rescues** back to Battle. Otherwise self-destruct after **10 seconds** or **5 failed** upside-down rescues.
4. **Self-destruct** — sacrifice message, `Cheerio!`, then permanent halt. With enhancements on, matrix / buzzer / relay play final effects first.

**Naming:** code enum is `THE_LAST_OF_2028`; matrix and human banners say `THE LAST OF EE2028`.

### Rules we treat as frozen

Do not “fix” these from older checklist wording without a focused patch:

- Fire is **hold-to-fire**, not auto-fire as soon as charge hits 25/50.
- Armed (for the matrix) means Battle **and** charge ≥ 25/50 (the matrix itself still draws a 0–10 bar; `main.c` scales 0–50 down).
- Battle also opens on **T / A / G** threshold trips (PR #3 behaviour).
- IR detects and chirps only — it does **not** change mode or fire the laser.

---

## What’s in the program

### Mandatory baseline — `Core/`

| Piece | Role |
|-------|------|
| Modes + LEDs | Standby / Battle / Last / self-destruct |
| USER button | Dual-edge EXTI, debounce, single / double / hold |
| Sensors | T, P, H, A, G, M (BSP paths for B-L4S5I-IOT01A) |
| Laser energy | Charge in tenths; hold-to-fire |
| UART lines | Human telemetry + `EVT …` machine tags |

### Enhancements — `App/` (optional, `-DEE_ENHANCEMENTS=1`)

| Module | Pin / bus | Behaviour |
|--------|-----------|-----------|
| 8×8 matrix HUD | I2C1 D15/D14 (PB8/PB9), brought up by the adapter (`EE_MATRIX_I2C=NULL` skips) | Charge bar, armed flash, radar lock (dial / line), `BATTLE MODE` / `THE LAST OF EE2028` banner, countdown digits, explosion |
| Buzzer | D2 = PD14 | Battle entry, recharge / denied, fire, threat chirp, accelerating countdown, rescue OK / fail, final burst |
| IR sensor | D3 = PB0 (active-LOW) | Detect → log + chirp |
| Relay `CAP_EXPLODE` | D4 = PA3 | HIGH on self-destruct start (safe sim load only) |
| RD-03D radar | D0 = PA1 (RX) / D1 = PA0 (TX), UART4 256000 | Giant lock → matrix, servo, `EVT RADAR` |
| Pan servo | D5 = PB4, TIM3_CH1 50 Hz | Tracks the lock; one self-test sweep at boot |
| Laser (giant cannon) | D7 = PA4, via transistor | Lit only for a 1 s cannon beam |
| Telemetry | ST-LINK VCP (USART1 PB6/PB7, 115200) | Tera Term / VESPER |

Full pinout incl. on-board LED / button / sensors, power rules and safe boot states: [docs/Hardware_Pinout.md](docs/Hardware_Pinout.md).

The shared adapter (`enhancements.c` / `enhancements_hw.c`) watches a `DroneStatus_t` snapshot each tick and handles two one-shot events: laser fired, self-destruct started. Live mode uses **one** 10 s rescue clock for matrix and buzzer.

Module harnesses and a UART bench live under `App/Test/`.

### VESPER Command Center — `Command/`

Browser game + dashboard (*The Last Signal* / *The Last of EE2028*, NUS E4-03-07 in year 6202): Three.js lab defence scene (spore meteors, infected, VESPER-07), full HUD, mode / sensors / charge / countdown / raw UART log. One parser for the **virtual board**, **fixture replay**, **pasted logs** and **Web Serial** @ 115200 8N1. Fiction never feeds back into drone state.

---

## Bring-up guide

### A. Try VESPER with no hardware (now)

```bash
# from repo root (zero dependencies, offline-friendly)
node Command/serve.mjs
```

Open the URL (usually `http://localhost:3000`). The default **Virtual board** source behaves like the firmware (PB tap / hold, flip, disturbance); **Recorded mission** replays `Command/fixtures/*.log`.

Optional parser check:

```bash
node scripts/test_uart_parser.mjs
node scripts/test_virtual_board.mjs
```

More UI detail: [Command/README.md](Command/README.md).

### B. Link a CubeIDE project (required to flash)

**Step-by-step guide: [docs/CubeIDE_Setup.md](docs/CubeIDE_Setup.md).** It copies the Assignment 2 project, links this repo's `Core/Src/main.c`, `Core/Inc/drone.h` and `App/` into it, and adds Debug (baseline) and Enhanced build configurations. It also covers building, flashing and troubleshooting. Summary:

1. In STM32CubeIDE, create or open a **B-L4S5I-IOT01A** project; keep the `.ioc` in (or next to) this repo.
2. Add `App/Inc` + `App/Src` (and `Core/` app files) to include paths and the build.
3. Confirm USER button EXTI on **both edges**, USART1 @ **115200 8N1** (ST-LINK VCP), I2C2 for on-board sensors.
4. Mission telemetry goes to the ST-LINK Virtual COM port (USART1 @ 115200 8N1) via `App/Src/telemetry_uart.c` — connect VESPER (Chrome/Edge **Connect board**) or Tera Term to that COM port. CubeIDE `printf` is for debug only.
5. Build, flash, prove LED + button + one telemetry line before enabling extras.

Full checklist: [docs/Next_Steps.md](docs/Next_Steps.md).

### C. Run the mandatory demo on the board

Power on → Standby telemetry → double-press into Battle → charge with single presses → **hold** to fire → flip for The Last of EE2028 → rescue **or** wait for timeout → confirm halt after `Cheerio!`.

Walk the acceptance list in Next Steps before treating the demo as done.

### D. Turn on Grove enhancements

1. Wire: buzzer **D2**, IR **D3**, relay control **D4** (relay switches only an instructor-approved low-voltage load). Matrix on the Arduino I2C pins **D15 (SCL) / D14 (SDA)**, address 0x70.
2. Build with `-DEE_ENHANCEMENTS=1` (the CubeIDE **Enhanced** configuration).
3. The adapter brings up I2C1 for the matrix itself. To use a CubeMX handle instead, define `EE_MATRIX_I2C` as `&hi2c1`; define it as `NULL` to skip the matrix.
4. Re-run the mandatory flow; confirm matrix banners, chirps, and relay only at self-destruct.

**Bench without the full drone loop:** `-DEE_ENHANCEMENTS=1 -DEE_BENCH=1` and `Enhancements_Bench_Init` / `Update` on Virtual COM.

### E. Live VESPER with the board

1. Close Tera Term / other apps holding the COM port.
2. Serve `Command/` as in step A; choose **Live board (Web Serial)** → **Connect board** (Chrome or Edge).
3. To save a real mission log for replay later: [scripts/capture_uart_mission.md](scripts/capture_uart_mission.md).

---

## Build macros

| Macro | Meaning |
|-------|---------|
| *(default)* | Baseline only — enhancements compiled out |
| `-DEE_ENHANCEMENTS=1` | Live adapter from drone status / events |
| `-DEE_ENHANCEMENTS=1 -DEE_BENCH=1` | UART menu drives the same adapter |

---

## Repository map

```text
Core/          Drone state machine (main.c) + drone.h
App/           Grove drivers, enhancements adapter, telemetry EVT, tests
Command/       VESPER game + HUD, virtual board, fixtures, Web Serial
docs/          Design notes, contracts, acceptance plan, concept microsite
scripts/       Serial monitor, UART capture notes, parser / sim smoke tests
```

---

## Status (honest)

| Item | State |
|------|--------|
| Baseline + enhancement **source** | On `main` |
| VESPER *The Last Signal* game + HUD | Ready (`Command/`; virtual board / fixtures / paste / Web Serial) |
| CubeIDE / HAL / BSP in this checkout | **Missing** — firmware build blocked here |
| On-board verify of full live stack | Pending Cube + wiring |
| mmWave aim, Wi‑Fi/BLE, UAV | Stretch / docs only |

---

## Docs

| Doc | When to open it |
|-----|-----------------|
| [docs/Next_Steps.md](docs/Next_Steps.md) | Cube bring-up + mandatory acceptance |
| [docs/Telemetry_EVT_Contract.md](docs/Telemetry_EVT_Contract.md) | UART / `EVT` line formats |
| [docs/Assignment2_Enhancements.md](docs/Assignment2_Enhancements.md) | Enhancement backlog #1–#7 |
| [docs/Button_Input_Enhancement.md](docs/Button_Input_Enhancement.md) | Push-button classifier |
| [docs/Timing_and_Interrupts.md](docs/Timing_and_Interrupts.md) | Non-blocking `HAL_GetTick()` timing, interrupt inventory (no `HAL_Delay`) |
| [docs/LED_Matrix_Design.md](docs/LED_Matrix_Design.md) | Matrix HUD |
| [docs/Buzzer_Design.md](docs/Buzzer_Design.md) | Buzzer patterns |
| [docs/IR_Design.md](docs/IR_Design.md) | IR threat |
| [docs/Relay_Design.md](docs/Relay_Design.md) | CAP_EXPLODE relay |
| [docs/Command_Center_Design.md](docs/Command_Center_Design.md) | VESPER game + dashboard design |
| [Command/README.md](Command/README.md) | Run VESPER + event → world map |
| [docs/vesper-concept.html](docs/vesper-concept.html) | Art-direction / mixed-reality concept microsite |
| [docs/Darren's_Tasks.md](docs/Darren's_Tasks.md) | Work split / task status |
