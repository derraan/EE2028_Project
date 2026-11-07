# Next Steps

This checklist turns the current skeleton into a buildable, testable STM32CubeIDE project.

## Assumption

**Board of record:** **B-L4S5I-IOT01A** (STM32L4S5VITx Discovery kit for IoT node). PR #3 / Lin baseline BSP paths (`Drivers/BSP/B-L4S5I-IOT01`, `stm32l4s5i_iot01*`) match this kit. Regenerate peripherals from the B-L4S5I-IOT01A Cube board selector; do not assume a B-L475E-IOT01A project tree.

## Mandatory-first rule

Everything in the assignment's basic specification is a **minimum requirement**. Do not spend project time on the Grove matrix, IR sensor, buzzer effects, relay, mmWave, servos, Wi-Fi/BLE or UAV until the mandatory acceptance checklist below passes on hardware.

### Basic-task acceptance checklist

- [ ] Power-on enters `STANDBY_MODE` and sends `Entering STANDBY MODE` once.
- [ ] All six sensors are sampled every second: temperature, pressure, humidity, accelerometer, gyroscope and magnetometer.
- [ ] UART telemetry includes values **and physical units**; no raw register values are displayed.
- [ ] Vector sensors report a meaningful axis, all three axes or a computed physical magnitude.
- [ ] Each sensor has explainable high and/or low thresholds in physical units.
- [ ] If **any** sensor is abnormal, normal telemetry pauses and all current threshold violations are reported once per second.
- [ ] Normal telemetry resumes only when **all** readings return to their normal ranges.
- [ ] Pushbutton input is interrupt-driven.
- [ ] Two presses less than 0.5 seconds apart are classified as a double press.
- [ ] One press is classified as a single press only after the 0.5-second double-press window expires.
- [ ] Standby single press is ignored; Standby double press enters Battle.
- [ ] Standby LED remains continuously ON.
- [ ] Entering Battle sends `Entering BATTLE MODE` immediately and once.
- [ ] Battle LED blinks at 1 Hz.
- [ ] Battle single press adds exactly 3/10 charge, with charge represented precisely enough to avoid float-comparison errors.
- [ ] At charge ≥5/10, hold PB >500 ms to fire (full-auto while held; −5 charge per shot every 500 ms).
- [ ] At charge <5/10, the laser stays inactive (hold reminds to recharge; single press adds +3).
- [ ] Battle double press returns to Standby when THE_LAST_OF_2028 is not active.
- [ ] Upside-down detection enters THE_LAST_OF_2028.
- [ ] THE_LAST_OF_2028 LED blinks at 2 Hz.
- [ ] `Drone Was Attacked!` is sent once per second while THE_LAST_OF_2028 is active.
- [ ] A double press while upright clears THE_LAST_OF_2028 and returns to Battle.
- [ ] Single presses during THE_LAST_OF_2028 are ignored.
- [ ] Five failed rescue double presses while still upside down enter self-destruct → sacrifice → `Cheerio!` → permanent halt.
- [ ] After 10 s wall-clock without rescue (attack alerts are informational only), enter self-destruct → sacrifice → `Cheerio!` → permanent halt. Whichever of 10 s / 5 failed attempts comes first wins.
- [ ] The terminated state cannot accidentally resume telemetry or process normal controls.
- [ ] All of the above runs together repeatedly without blocking timing failures.

**Enhancement gate:** only start enhancement integration after this checklist passes in one continuous demonstration.

## What is missing now

Application source for the mandatory baseline, enhancements adapter, telemetry EVT helpers and VESPER *The Last Signal* game **is already in this repository** (`Core/Src/main.c`, `App/`, `Command/`). What is **not** in this checkout — and still blocks a firmware build — is the STM32CubeIDE project glue:

- An STM32CubeMX `.ioc` file or complete STM32CubeIDE project
- Startup code, linker script, CMSIS/HAL drivers or interrupt files
- Real clock, GPIO, UART, I2C or timer initialization
- Peripheral handles such as `hi2c2`, `huart1` and timer handles
- Board Support Package (BSP) or sensor component drivers under `Drivers/`
- A documented CubeIDE import / build / flash procedure against this tree

### Software status vs hardware verify

| Area | In source | On-board verify |
|------|-----------|-----------------|
| Modes, dual-edge PB, hold-to-fire, charge, rescue / self-destruct | Yes (`main.c`) | Pending Cube + flash |
| Six-sensor reads + units + threshold logging | Yes (BSP call sites) | Pending BSP link |
| Mission UART via `Telemetry_*` (prose + sensors + EVT) → USART1 ST-LINK VCP (`telemetry_uart.c`) | Yes | Pending VESPER live-board check |
| Matrix / buzzer / IR / relay via `Enhancements_*` | Yes (`-DEE_ENHANCEMENTS=1`) | Pending Cube + wiring |
| VESPER *The Last Signal* (3D game + HUD, virtual board, fixtures, Web Serial) | Yes (`Command/`) | Live stream pending USART |

Frozen gameplay (do not “fix” from older assignment wording without a focused patch):

- Battle fire is **hold-to-fire** (≥500 ms, charge ≥5/10), not auto-fire on charge alone.
- Standby → Battle on **double-press or T/A/G** threshold trip (PR #3).
- Self-destruct at **10 s** without rescue **or** **5** failed upside-down rescues.
- IR is observe-only (chirp + log); no mode/fire authority.

When the Cube project lands, keep generated `SystemClock_Config()` / `MX_*_Init()` and drop competing handwritten copies. Link existing `App/` sources rather than rewriting drivers from scratch.

---

## Phase 1 — Create the real CubeIDE project

- [ ] In STM32CubeIDE, create a project using **Board Selector → B-L4S5I-IOT01A**.
- [ ] Save the `.ioc` inside this repository.
- [ ] Allow CubeMX to generate startup code, linker script, HAL/CMSIS drivers and `stm32l4xx_it.c`.
- [ ] Move the existing application logic into CubeMX `USER CODE` sections or separate application modules so regeneration does not erase it.
- [ ] Add `drone.c` / `drone.h` rather than leaving all application logic in generated `main.c`.
- [ ] Build and flash a minimal LED-blink image before adding sensors.
- [ ] Commit the generated baseline once it builds cleanly.

## Phase 2 — Configure board peripherals

### Clock and debug

- [ ] Generate a valid system clock configuration for the STM32L4S5.
- [ ] Keep SWD enabled for ST-LINK programming and debugging.
- [ ] Verify `HAL_GetTick()` advances correctly; all later behavior depends on it.

### LEDs and push button

For this board, the user LEDs are normally **PA5 (LD1)** and **PB14 (LD2)**. The blue user button is **PC13**, active-low.

- [ ] Configure one LED for mode indication.
- [ ] Configure PC13 as GPIO EXTI with the correct edge for an active-low press.
- [ ] Enable the corresponding EXTI interrupt in NVIC.
- [ ] Replace the placeholder `PB_Pin` fallback with CubeMX-generated labels.
- [ ] Add button debounce (about 30–50 ms) separately from the 500 ms double-press window.
- [ ] Keep the ISR short: record an event/timestamp and process state changes in the main loop.

### UART telemetry

The ST-LINK Virtual COM connection uses **USART1**, normally PB6 TX and PB7 RX.

- [ ] Configure USART1 for **115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**.
- [ ] Generate and use `UART_HandleTypeDef huart1`.
- [ ] Implement a bounded `UART_Send()` helper using `HAL_UART_Transmit()` initially.
- [ ] Print a startup banner and each mode transition exactly once.
- [ ] Verify output in a serial terminal before implementing the dashboard.

### I2C sensor bus

The on-board environmental and motion sensors use **I2C2**, normally PB10 SCL and PB11 SDA.

- [ ] Configure I2C2 in Fast Mode (400 kHz).
- [ ] Generate and use `I2C_HandleTypeDef hi2c2`.
- [ ] Run an I2C address scan as a hardware smoke test.
- [ ] Be consistent about 7-bit addresses versus the shifted address expected by STM32 HAL calls.
- [ ] Check whether the Grove matrix shares I2C2 or uses another exposed I2C bus.
- [ ] Confirm the matrix's HT16K33 address with a scan before writing its driver.

### Timers and output GPIO

- [ ] Allocate a timer channel for passive-buzzer PWM, or a normal GPIO if the buzzer is active.
- [ ] Allocate two PWM channels for the future pan/tilt servos; do not power servos from an MCU GPIO.
- [ ] Configure `CAP_EXPLODE_Pin` as push-pull output, default **LOW**.
- [ ] Ensure startup and reset cannot glitch the relay input HIGH.
- [ ] Drive only the control input of a suitable relay module and use a safe low-voltage simulation load.

---

## Phase 3 — Add board sensor drivers

Use the STM32CubeL4 BSP (`Drivers/BSP/B-L4S5I-IOT01`, `stm32l4s5i_iot01*`) and required component drivers, or add the equivalent X-CUBE-MEMS1 components:

- **HTS221** — temperature and humidity (`T`, `H`)
- **LPS22HB** — pressure (`P`)
- **LSM6DSL** — acceleration and angular rate (`A`, `G`)
- **LIS3MDL** — magnetic field (`M`)

Tasks:

- [ ] Add only the required BSP/component source and include paths.
- [ ] Initialize each sensor and check every returned status code.
- [ ] Read the sensor identity (`WHO_AM_I`) before trusting measurements.
- [ ] Select explicit output data rates and full-scale ranges.
- [ ] Define what scalar values `A`, `G` and `M` mean; the devices return three axes.
- [ ] Keep raw axis values in a sensor structure rather than losing information in globals.
- [ ] Convert raw readings into documented units: °C, %RH, hPa, g, dps and gauss/µT.
- [ ] If reporting vector magnitude, compute `sqrt(x*x + y*y + z*z)`; do not sum raw components.
- [ ] Produce one complete six-sensor telemetry sample at least every second, as required.
- [ ] Faster internal IMU sampling is allowed, but it must not replace the one-second complete report.
- [ ] Detect and report stale data, bus errors and initialization failures.

### Orientation and thresholds

- [ ] Implement reliable upside-down detection. Physically, orientation comes from accelerometer gravity direction; use gyro data to confirm rotation if required by the assessment wording.
- [ ] Calibrate the board's upright and inverted Z-axis signs.
- [ ] Require the inverted condition to persist briefly to reject vibration.
- [ ] Replace placeholder `THRESHOLD_T`, `THRESHOLD_A` and `THRESHOLD_G` values with tested values and correct units.
- [ ] Define sensible high and/or low thresholds for **all six sensor quantities**, not only T/A/G.
- [ ] Track a violation bitmask so multiple simultaneous violations can all be printed.
- [ ] Fix the current `T && A && G` condition: mandatory monitoring treats **any** out-of-range reading as a violation.
- [ ] Add hysteresis or cooldown to prevent repeated mode changes near a threshold.

---

## Phase 4 — Replace blocking skeleton behavior

The current `HAL_Delay(100)` and unimplemented comments are insufficient for simultaneous display, telemetry, buttons, countdown and radar.

- [ ] Replace the fixed-delay loop with cooperative tasks driven by `HAL_GetTick()`.
- [ ] Use separate schedules for sensor sampling, telemetry, LEDs, matrix animation and buzzer.
- [ ] Add a single `Set_Drone_State()` function that records entry time and runs one-time transition actions.
- [ ] Mark variables shared with interrupts as `volatile` or pass events through flags.
- [ ] Make the EXTI callback record raw presses only; classify single versus double presses outside the ISR.
- [ ] Delay single-press dispatch until the 0.5-second double-press window has expired.
- [ ] Store laser energy as integer tenths (`0`–`10`) rather than float where practical.
- [ ] Use unsigned tick subtraction so rollover remains safe.
- [ ] Add a default branch or recovery path for an invalid state.

Suggested update periods:

- Button/event processing: every loop
- IMU/orientation: 20–50 ms
- IR input: 10–20 ms with filtering
- Matrix animation: 50–100 ms
- Environmental sensors: 500–1000 ms
- Telemetry: 1000 ms

---

## Phase 5 — Finish the three modes

### STANDBY_MODE

- [ ] Turn the selected status LED on continuously.
- [ ] Send `Entering STANDBY MODE` once on power-up and on every later entry.
- [ ] Read all six sensors and publish one complete telemetry record every second.
- [ ] Include units and converted physical values, not raw sensor registers.
- [ ] If any threshold is violated, pause the normal telemetry line.
- [ ] Send every active threshold violation once per second until all readings are normal.
- [ ] Resume normal telemetry only when every reading is in range.
- [ ] Ignore a resolved single press.
- [ ] Enter Battle on a valid double press.
- [ ] Do not use a threshold violation as a mode switch unless it is clearly labelled as an optional behavior and cannot break the required flow.

### BATTLE_MODE

- [ ] Send `Entering BATTLE MODE` immediately and once on entry.
- [ ] Blink the board LED at 1 Hz without blocking.
- [ ] Implement single-press charging: add exactly 3/10 and cap at 10/10.
- [ ] Hold PB ≥500 ms in Battle with charge ≥5/10 to fire full-auto (−5 charge per shot every 500 ms).
- [ ] Keep the laser inactive when energy is below 5/10 (hold reminds to recharge).
- [ ] Handle Battle double press by returning immediately to Standby when THE_LAST_OF_2028 is not active.
- [ ] Show charge progress on the matrix (enhancement path).
- [ ] Flash the matrix while armed: Battle and charge ≥5/10 (enhancement path).
- [ ] Play the fire tone and emit `EVT FIRE` after firing.
- [ ] Enter THE_LAST_OF_2028 when the persistent upside-down condition is detected.

### THE_LAST_OF_2028

- [ ] Record entry time and start one 10-second rescue window.
- [ ] Blink the board LED at 2 Hz.
- [ ] Repeatedly scroll `THE LAST OF EE2028` on the matrix.
- [ ] Send `Drone Was Attacked!` once per second.
- [ ] Ignore resolved single presses.
- [ ] Accelerate buzzer beeps as the countdown approaches zero.
- [ ] On a valid rescue double press, cancel all countdown outputs and return to Battle.
- [ ] At 10 seconds without rescue, send the final termination message.
- [ ] Enter a terminal state that stops UART and any Wi-Fi/BLE communication and no longer runs normal mode logic.
- [ ] As an enhancement layered after the mandatory shutdown behavior, set `CAP_EXPLODE_Pin` HIGH, show the explosion frame and sound the final buzzer effect.
- [ ] Latch the final state so the relay event and final messages are not retriggered every loop.
- [ ] Define whether the relay remains HIGH or returns LOW after a tested pulse duration.

---

## Phase 6 — Enhancements (source present; gate = on-board mandatory pass)

Optional enhancement **modules and the shared adapter already live under `App/`**. Preserve a working mandatory baseline before enabling `-DEE_ENHANCEMENTS=1` on hardware. Do not rewrite drivers from scratch — bind and verify the existing APIs.

### Grove modules and matrix controller

### Red LED matrix

- [x] HT16K33 HAL-I2C driver (`App/Src/ht16k33.c`): oscillator on, display on, brightness, clear and frame write.
- [x] Compact 8×8 font, charge bar, armed flash, radar line, countdown digits and explosion (`status_display.c`).
- [x] Display priority arbitration (explosion → Last → Battle banner → locked radar → armed → charge).
- [x] Non-blocking scroll / flash driven by `HAL_GetTick()` / injected `now_ms`.
- [x] Live matrix bus: I2C1 D15/D14 brought up by `Enhancements_HW_MatrixI2C()`.
- [ ] Verify row/column orientation on the live I2C1 bus.
- [x] Live path uses one 10 s `rescue_remaining_ms` clock: banner, then digits for the last 5 s (host test).

### IR reflective sensor

- [x] Debounce adapter + test harness (`ir_threat.*`).
- [x] Live policy: detect → chirp + log only (no mode/fire authority).
- [ ] Confirm module SKU, polarity and distance on the wired D3/PB0 pin.

### Buzzer

- [x] Active Grove buzzer patterns (chirp / fire / countdown / final burst) on D2/PD14.
- [ ] Confirm patterns cannot starve PB rescue or sensor ticks on live hardware.

### Relay

- [x] `CAP_EXPLODE` adapter on D4/PA3; triggered by `DRONE_EVENT_SELF_DESTRUCT_STARTED`.
- [ ] Power-cycle verify: pin always starts LOW.

---

## Phase 7 — Advanced features

Do these only after the wired #1–#4 demo is stable.

### mmWave and 2-DOF aim

- [ ] Select a radar that actually reports target position/angle; presence-only radar cannot provide azimuth.
- [ ] Confirm its voltage levels, protocol, update rate and coordinate format.
- [ ] Parse frames with timeout/checksum handling.
- [ ] Map azimuth to the matrix's eight columns.
- [ ] Calibrate radar coordinates to pan/tilt servo angles.
- [ ] Use an external servo supply with common ground and adequate decoupling.
- [ ] Add angle limits, slew-rate limits and a neutral fail-safe position.
- [ ] Use only a low-power, instructor-approved laser or an LED substitute.

### Wi-Fi/BLE telemetry

- [ ] Choose **one** transport first: Wi-Fi or BLE.
- [ ] Reuse one telemetry message format across UART and wireless.
- [ ] Add connection state, timeout and reconnect behavior.
- [ ] Keep UART as the debugging/fallback channel.

### UAV

- [ ] Treat the STM32 board as a companion controller; use a dedicated flight controller for stabilization.
- [ ] Define a safe UART/MAVLink interface and fail-safe behavior.
- [ ] Verify weight, power, mounting and radio rules.
- [ ] Make THE_LAST_OF_2028 request a controlled land/return action—not an unsafe in-flight motor cut.
- [ ] Do not make flight a dependency of the graded core demo.

---

## Phase 8 — Verification and delivery

- [ ] Build with warnings enabled and resolve all new warnings.
- [ ] Verify every item in the mandatory basic-task acceptance checklist before testing enhancements.
- [ ] Test every state transition individually.
- [ ] Test button bounce, slow/fast double presses and presses across tick boundaries.
- [ ] Verify a Battle double press returns to Standby without accidentally charging.
- [ ] Verify a single Battle press is dispatched only after 0.5 seconds and adds exactly 3/10.
- [ ] Test charge sequences such as 0→3→6→fire→1 and 2→5→fire→0.
- [ ] Confirm all six telemetry values and their units appear every second in normal Standby.
- [ ] Force each sensor above and/or below its threshold and confirm normal telemetry pauses.
- [ ] Force multiple simultaneous violations and confirm every violation is printed.
- [ ] Restore all readings and confirm normal telemetry resumes.
- [ ] Test I2C device missing, UART disconnected and sensor read failures.
- [ ] Test rescue at 9.9 seconds and timeout at 10 seconds.
- [ ] Confirm no UART or wireless data is sent after terminal shutdown.
- [ ] Power-cycle repeatedly and confirm relay output always starts LOW.
- [ ] Run the complete demo at least five times without resetting manually.
- [ ] Record the pin map, I2C addresses, timer channels and power connections.
- [ ] Update the README with CubeIDE import/build/flash instructions.

## Phase 9 — Submission and presentation

- [ ] Export the complete STM32CubeIDE project using **File → Export → Archive File**.
- [ ] Produce one project `.zip`; do not combine it with the slides.
- [ ] Follow the Canvas filename template using the group number and both matriculation numbers, for example `Assignment2_12_A0213331Z_A0010101Z_Code.zip`.
- [ ] Submit slides separately as `.ppt`, `.pptx`, `.pdf`, `.key` or `.html` using the required naming format.
- [ ] Include system flowchart(s).
- [ ] Explain overall logic and important functions.
- [ ] Clearly separate compulsory implementation from enhancements.
- [ ] Document problems encountered and how they were solved.
- [ ] Add an appendix declaring joint and individual contributions for both students.
- [ ] Prepare and rehearse a 20-minute presentation and live demonstration.
- [ ] Verify the actual Canvas deadline for the assigned lab group.

## Immediate next five actions

1. Generate and commit the STM32CubeIDE `.ioc` baseline for **B-L4S5I-IOT01A**; link `App/` + `Core/` sources.
2. Prove LED + button EXTI + USART1 Virtual COM (`telemetry_uart.c`, always built). Keep CubeIDE `printf` for debug only.
3. Configure I2C2 / BSP and prove all on-board sensor identities.
4. Run the **mandatory basic-task acceptance checklist** on hardware (software already written).
5. Enable `-DEE_ENHANCEMENTS=1`, wire D2/D3/D4 + matrix I2C, then attach VESPER Web Serial.

## References

- [UM2708 — B-L4S5I-IOT01A Discovery kit user manual](https://www.st.com/resource/en/user_manual/um2708-discovery-kit-for-iot-node-multichannel-communication-with-stm32l4-series-stmicroelectronics.pdf)
- [ST motion-sensing setup (I2C2 / LSM6DSL on STM32L4 IoT node)](https://wiki.st.com/stm32mcu/wiki/AI:How_to_perform_motion_sensing_on_STM32L4_IoTnode)
- [X-CUBE-MEMS1](https://www.st.com/en/embedded-software/x-cube-mems1.html)
- Assignment brief: `S1AY2627 Assignment 2: The Last of EE2028` (local PDF supplied by the team)
