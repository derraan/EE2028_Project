# Darren's Tasks — Parallel Enhancement Work

## Can this be done in parallel?

**Yes, with boundaries.** The basic firmware is not ready, so enhancement code should initially run against mocks and small hardware tests. Do not build enhancements directly into the current `main.c`; that would create avoidable merge conflicts with the CubeMX project and state-machine work.

Your friend owns the mandatory baseline described in `Next_Steps.md`. Darren owns self-contained enhancement drivers, display flows and integration adapters. Enhancements are merged only after the mandatory acceptance gate passes.

---

## Git workflow

- [x] Create a branch such as `darren/enhancements`.
- [ ] Ask your friend to use a different branch, such as `basic/mandatory-firmware`.
- [ ] Do not both edit generated `Core/Src/main.c`, `Core/Inc/main.h`, `.ioc` or interrupt files.
- [ ] Commit each driver separately so it can be reviewed or cherry-picked.
- [ ] Regularly merge/rebase from `main` after your friend's stable checkpoints.
- [ ] Merge the mandatory baseline first; integrate enhancement modules afterward.
- [ ] Never overwrite CubeMX-generated files when resolving conflicts.

## Ownership boundary

### Friend: mandatory baseline

- CubeMX `.ioc` and generated project
- Clock, GPIO, EXTI, I2C2, USART1 and timer setup
- Six on-board sensor drivers and physical-unit conversion
- Required state machine, PB classification, LEDs and UART behavior
- Mandatory telemetry, thresholds, laser charging/firing and termination
- Final definitions of board pins and peripheral handles

### Darren: enhancements

- HT16K33 matrix driver and display controller
- Matrix fonts, bitmaps and animation flows
- Grove buzzer pattern controller
- Grove IR sensor adapter/test
- Relay simulation output adapter
- Radar azimuth display and future radar parser interface
- Enhancement integration specification and tests

### Shared only at integration

- Mode/event API
- Sensor/telemetry data structure
- Pin/peripheral binding file
- Final scheduler calls

---

## Define the integration contract first

Write enhancement modules against an application-facing interface, not directly against your friend's globals.

Suggested events:

```c
typedef enum {
    DRONE_EVENT_NONE,
    DRONE_EVENT_ENTER_STANDBY,
    DRONE_EVENT_ENTER_BATTLE,
    DRONE_EVENT_ENTER_LAST_OF_2028,
    DRONE_EVENT_CHARGE_CHANGED,
    DRONE_EVENT_LASER_FIRED,
    DRONE_EVENT_RADAR_AZIMUTH,
    DRONE_EVENT_COUNTDOWN_TICK,
    DRONE_EVENT_RESCUED,
    DRONE_EVENT_TERMINATED
} DroneEvent_t;
```

Suggested enhancement update API:

```c
void Enhancements_Init(void);
void Enhancements_OnEvent(DroneEvent_t event, int32_t value);
void Enhancements_Update(uint32_t now_ms);
```

Rules:

- `Enhancements_OnEvent()` records requested changes and returns quickly.
- `Enhancements_Update()` advances animations and sounds without blocking.
- Enhancement failures must never prevent mandatory mode logic, PB rescue or UART.
- Hardware-specific HAL calls stay inside individual drivers.
- During early development, replace HAL calls with mocks or callbacks.

Confirm this contract with your friend before either side writes integration code.

---

## Task 1 — Matrix driver ✅ completed and verified

Files:

- `App/Inc/ht16k33.h`
- `App/Src/ht16k33.c`
- `App/Test/ht16k33_test.c`

Tasks:

- [x] Define initialization, brightness, clear and 8-byte frame-write APIs.
- [x] Keep the I2C handle/address injectable; do not hard-code `hi2c2`.
- [x] Return status/error values to the caller.
- [x] Keep display frame data independent from HAL so it can be tested on a PC.
- [x] Build a temporary one-pixel / row / column hardware test when the Cube project is available.

Deliverable: a driver that accepts an 8×8 bitmap and sends it to the HT16K33. **Done.**

## Task 2 — Matrix status controller ✅ completed and hardware-verified

Create files such as:

- `App/Inc/status_display.h`
- `App/Src/status_display.c`
- `App/Test/status_display_test.c`

Implement:

- [x] 0–10 charge value mapped to an 8-column progress bar.
- [x] Full-matrix armed flashing.
- [x] Radar azimuth mapped to matrix column 0–7 (plus optional 180° dial style).
- [x] Scrolling `BATTLE MODE` once on mode entry.
- [x] Repeating `LAST OF EE2028` banner text.
- [x] Countdown digits 10→0 with progressive flash toward 0.
- [x] Explosion frame followed by blank display.
- [x] Compact 8×8 font containing only required characters.

Display priority:

1. Explosion / terminal blank
2. LAST_OF_2028 countdown and warning
3. One-time `BATTLE MODE`
4. Armed flash
5. Charge bar
6. Radar azimuth line / dial

All animation must use timestamps passed to `StatusDisplay_Update(now_ms)`—no `HAL_Delay()`.

Deliverable: feed fake events/timestamps and demonstrate every screen sequence. **Done** (UART menu + auto sequence; display confirmed on hardware). See `docs/LED_Matrix_Design.md`.

## Task 3 — Buzzer pattern controller ✅ completed and hardware-verified

Create:

- `App/Inc/buzzer.h`
- `App/Src/buzzer.c`

Tasks:

- [x] Define a hardware-neutral backend callback (`set_tone(ctx, on, freq_hz)`).
- [x] Implement short threat chirp.
- [x] Implement laser-fire tone.
- [x] Implement 2 Hz LAST_OF_2028 warning.
- [x] Implement accelerating countdown beeps.
- [x] Implement final burst and silence.
- [x] Ensure `Buzzer_Update(now_ms)` is non-blocking.
- [x] Active buzzer confirmed: patterns are rhythm (ON/OFF) only; no PWM pitch.

Deliverable: a deterministic timeline of ON/OFF or frequency commands for each effect. **Done** (active buzzer on D2 confirmed; rhythm patterns verified). See `docs/Buzzer_Design.md`.

## Task 4 — IR threat adapter (start after checking SKU)

Create:

- `App/Inc/ir_threat.h`
- `App/Src/ir_threat.c`

Tasks:

- [ ] Record module SKU, supply voltage, output type and active polarity.
- [ ] Define a raw GPIO/ADC read callback.
- [ ] Filter/debounce detections without blocking.
- [ ] Emit one threat event per object encounter rather than one event every loop.
- [ ] Add re-arm logic after the object leaves.
- [ ] Do not let IR behavior alter compulsory PB or sensor behavior.

Deliverable: hand/card approach creates one clean threat event.

## Task 5 — Safe relay simulation adapter

Create:

- `App/Inc/relay_effect.h`
- `App/Src/relay_effect.c`

Tasks:

- [ ] Keep the output LOW during initialization and normal operation.
- [ ] Provide explicit `RelayEffect_Trigger()` and `RelayEffect_Reset()` APIs.
- [ ] Decide with the team whether HIGH is latched or timed.
- [ ] Use the relay module control input only, not a bare relay coil.
- [ ] Test first with an LED or other safe low-voltage load.
- [ ] Do not integrate until mandatory termination behavior works.

Deliverable: countdown-zero event produces one controlled GPIO action with no startup glitch.

## Task 6 — Radar/servo flow without radar hardware

You can build the mapping logic before hardware arrives.

Tasks:

- [ ] Define a `RadarTarget` structure containing valid flag, azimuth, elevation, range and timestamp.
- [ ] Convert test azimuth values into matrix columns 0–7.
- [ ] Convert bounded azimuth/elevation into pan/tilt command angles.
- [ ] Add clamping, neutral position, stale-target timeout and maximum slew rate.
- [ ] Generate fake moving targets to exercise the display and aiming flow.
- [ ] Keep parser/transport separate so the eventual radar module can be swapped.

Do **not** assume every mmWave module reports angle. Select hardware that explicitly provides azimuth/elevation or position data.

Deliverable: simulated target sweeps left→right on the matrix and produces bounded servo commands.

## Task 7 — Host-side flow simulator

This is the safest way to progress while the STM32 baseline is unfinished.

- [ ] Build a small C test harness (or Python model) that injects events and simulated milliseconds.
- [ ] Exercise Standby → Battle message → charging → armed → fire.
- [ ] Exercise radar azimuth while Battle is active.
- [ ] Exercise LAST_OF_2028 warning → countdown → rescue.
- [ ] Exercise LAST_OF_2028 warning → countdown → termination.
- [ ] Verify display priority when events overlap.
- [ ] Capture expected timelines for later on-board regression tests.

Deliverable: repeatable expected output independent of CubeIDE and hardware.

---

## Work that must wait

Do not complete these until your friend's baseline provides stable interfaces:

- Binding modules to real `I2C_HandleTypeDef` / `TIM_HandleTypeDef`
- Choosing final GPIO and timer channels
- Editing the generated main loop or interrupt callback
- Sharing I2C2 with the on-board sensors
- Triggering enhancements from mandatory mode transitions
- Replacing mandatory UART with wireless telemetry
- Connecting relay behavior to terminal shutdown
- UAV integration

---

## Integration checkpoints

### Checkpoint A — contract review

- [ ] Agree on `DroneEvent_t`, event values and update API.
- [ ] Agree on folder layout and naming.
- [ ] Agree that the mandatory implementation never calls blocking enhancement functions.

### Checkpoint B — hardware baseline ready

Your friend's branch must demonstrate:

- [ ] LED, PB EXTI, USART1 and I2C2 working.
- [ ] Six sensors reporting physical values.
- [ ] Mandatory state/PB/laser behavior passing.
- [ ] 10-second rescue and communication shutdown passing.

### Checkpoint C — bind one module at a time

1. Matrix driver only
2. Matrix status controller
3. Buzzer
4. IR sensor
5. Relay simulation
6. Radar/servos
7. Wi-Fi/BLE

After each step, rerun the entire mandatory acceptance checklist. Revert or disable an enhancement if it breaks compulsory behavior.

---

## Recommended order for Darren

1. Agree on interfaces and branch ownership with your friend.
2. ~~Implement HT16K33 matrix driver.~~ ✅
3. ~~Implement pure bitmap/font/status-display logic (Task 2).~~ ✅
4. ~~Implement non-blocking buzzer patterns (Task 3).~~ ✅
5. Implement IR hardware smoke tests in isolated test code (**Task 4 — next**).
6. Build fake radar target and servo-angle mapping.
7. Wait for the mandatory baseline integration checkpoint.
8. Merge and bind matrix first; add other enhancements one by one.

## Definition of done

Darren's parallel work is ready to merge when:

- It does not modify CubeMX-generated basic files.
- Every module has a small, documented API.
- No module calls `HAL_Delay()`.
- Fake events can demonstrate every enhancement flow.
- Missing hardware returns an error without blocking the main application.
- The mandatory project can compile with enhancements disabled.
- Integration can be performed one module at a time.
