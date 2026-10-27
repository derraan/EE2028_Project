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
- Radar azimuth display and future radar parser interface (Task 6 stretch)
- RD-03E ranging + registration prototype (Task 6a)
- Command Center web dashboard (HTML mockups → wired live data; UART / Wi‑Fi/BLE)
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

## Task 4 — IR threat adapter ✅ completed and hardware-verified

Create:

- `App/Inc/ir_threat.h`
- `App/Src/ir_threat.c`
- `App/Inc/ir_threat_test.h`
- `App/Test/ir_threat_test.c`

Tasks:

- [x] Record module SKU, supply voltage, output type and active polarity.
- [x] Define a raw GPIO/ADC read callback.
- [x] Filter/debounce detections without blocking.
- [x] Emit one threat event per object encounter rather than one event every loop.
- [x] Add re-arm logic after the object leaves.
- [x] Do not let IR behavior alter compulsory PB or sensor behavior.

Hardware notes:

- Grove Infrared Reflective Sensor on **D3** (`ARD_D3_Pin` / `ARD_D3_GPIO_Port`, PB0 mapping on Discovery).
- Digital mode, input pull-up in test harness.
- Active level confirmed working with default **active-LOW** (`active_level=0`).
- Baseline timing defaults: `debounce_ms=30`, `rearm_ms=300`.

Deliverable: hand/card approach creates one clean threat event. **Done** (bench-verified on D3; `EVT IR=1` / threat chirp confirmed). See `docs/IR_Design.md`.

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

**Keep this task.** It is the stretch path for **azimuth / 2‑DOF aim** (matrix column + pan/tilt). Build the mapping logic with simulated targets even if the real module cannot provide XY.

Tasks:

- [ ] Define a `RadarTarget` structure containing valid flag, azimuth, elevation, range and timestamp.
- [ ] Convert test azimuth values into matrix columns 0–7.
- [ ] Convert bounded azimuth/elevation into pan/tilt command angles.
- [ ] Add clamping, neutral position, stale-target timeout and maximum slew rate.
- [ ] Generate fake moving targets to exercise the display and aiming flow.
- [ ] Keep parser/transport separate so the eventual radar module can be swapped.

Do **not** assume every mmWave module reports angle. The **RD-03E** we have is primarily a **ranging / human presence** module and may **not** provide usable XY tracking — use **Task 6a** for the hardware-backed first prototype.

Deliverable: simulated target sweeps left→right on the matrix and produces bounded servo commands.

## Task 6a — RD-03E ranging + separate peace-sign registration (preliminary)

**Hardware:** Ai-Thinker **RD-03E** (24 GHz).  
**First prototype goal:** range a single person and optionally **register** them when a peace sign is confirmed by a **separate** vision path — not by assuming the RD-03E can recognise ✌️.

### Important limits (do not overclaim)

- RD-03E docs list **separate firmware builds**: precision **ranging** (e.g. Build 2268) vs **gesture** (e.g. Build 2269). They are not assumed to run usefully at the same time until tested.
- Gesture firmware documents a **supported gesture class** (e.g. arm-wave style bathroom control). **Held peace-sign (✌️) recognition is unverified** and must **not** be assumed from the radar.
- RD-03E reports **target status + distance**, not a reliable identity link between “person gesturing” and “person ranged”.
- First prototype: **one person** in the sensing area only.
- Firmware is oriented toward **human** detection; validate people and static obstacles separately.
- Documented human ranging starts around **0.3 m** minimum — begin tests **above** that.

References:

- [RD-03E docs hub](https://docs.ai-thinker.com/en/Rd-03E/)
- [Precision ranging user manual (PDF)](https://aithinker-static.oss-cn-shenzhen.aliyuncs.com/docs/media/Radar/Guide/Rd-03E%E7%B2%BE%E5%87%86%E6%B5%8B%E8%B7%9D%E7%94%A8%E6%88%B7%E6%89%8B%E5%86%8C%E8%8B%B1%E6%96%87%E7%89%88.pdf)

### Basic plan

1. **Connect RD-03E → STM32 UART**
  - Use **precision-ranging firmware**.  
  - Default serial (per ranging manual): **256000 8N1**.  
  - Parse frames: `AA AA | status(1) | distance_cm LE(2) | 55 55`.  
  - Status: `0x00` none · `0x01` moving · `0x02` micro-motion.  
  - Expose a small driver API, e.g. `RD03E_Update()` → `{ present, status, distance_cm, age_ms }`.
2. **Validate ranging first**
  - One person at known distances (start **> 0.3 m**).  
  - Log status + distance over UART / Command Center `EVT`.  
  - Separately test chairs/walls/other obstacles — do not treat every reflection as a valid “person”.  
  - Freeze a working ranging parser before adding registration logic.
3. **Peace-sign detection = separate function**
  - Use a **camera + vision-capable module/processor** (choice depends on board/expansion).  
  - Vision side sends a simple event to the STM32, e.g. `PEACE_DETECTED` (GPIO pulse, short UART line, or I2C flag).  
  - Until that hardware exists, stub `PEACE_DETECTED` from a test key / mock.  
  - Do **not** claim RD-03E recognises ✌️ unless independently proven on **gesture firmware** after flash + test.
4. **Registration rule**
  - Register only when **both** are true within a short coincidence window:  
    - fresh valid radar detection (status ≠ none, distance in configured interval), and  
    - `PEACE_DETECTED` received.
  - Store `{ registered, distance_cm, timestamp }`.  
  - **One** registered target for the first prototype.
5. **Indicate + timeout**
  - LED (or matrix glyph) = registered.  
  - UART / Command Center: log measured distance on register.  
  - Clear registration after a configurable period with **no** valid target (and optionally clear on explicit command).

### RD-03E-only alternate (if no camera)

- Flash / test **gesture firmware** and use **only gestures the manual documents and you personally verify**.  
- Replace peace-sign registration with that verified gesture event.  
- Re-verify whether ranging still works on that build; if not, treat gesture-only and ranging-only as **two demo modes**, not one simultaneous feature.

### Files (suggested)

- `App/Inc/rd03e.h` / `App/Src/rd03e.c` — ranging UART parser  
- `App/Inc/target_register.h` / `App/Src/target_register.c` — coincidence + timeout logic  
- `App/Inc/peace_stub.h` — mock / external `PEACE_DETECTED` input  
- Later: vision companion docs (not on RD-03E)

### Tasks

- [ ] Confirm RD-03E wiring (5 V / level shifting as required) and which STM32 UART is free.
- [ ] Flash / confirm **precision-ranging** firmware; capture raw frames at 256000.
- [ ] Implement non-blocking frame parser (header/trailer sync, LE distance, status).
- [ ] Bench-test known human distances (> 0.3 m) and log results.
- [ ] Bench-test non-human obstacles; document false-positive behaviour.
- [ ] Add registration state machine: peace stub ∧ valid range → LED + distance log.
- [ ] Add stale-target timeout / clear.
- [ ] Emit Command Center tags, e.g. `EVT RADAR dist=120 status=1`, `EVT REGISTER dist=120`, `EVT REGISTER clear`.
- [ ] Keep Task 6 azimuth/servo simulation separate until a true angle source exists.
- [ ] (Optional) Evaluate gesture firmware only after ranging path is solid; do not assume ✌️.

Deliverable: single-person RD-03E ranging on STM32, plus a registration path that depends on an **external/stubbed** peace event — with explicit docs that ✌️ is **not** claimed from RD-03E.

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

## Task 8 — Command Center web dashboard (proposed design)

Maps to enhancement **#4** (UART Command Center) and stretch **#6** (Wi‑Fi / BLE) in `Assignment2_Enhancements.md`. The matrix is the **on-drone HUD**; this is the **PC mission-control view**.

**Delivery shape:** a **web dashboard**, not a standalone desktop app. Design starts from the imported HTML mockup, then wires live EE2028 data into its panels. Mandatory UART telemetry must keep working; wireless is an add-on, never a replacement.

Full design notes: `[docs/Command_Center_Design.md](Command_Center_Design.md)`.

### Mockup imported


| Item        | Location                                                                          |
| ----------- | --------------------------------------------------------------------------------- |
| HTML mockup | `Command/index.html`                                                              |
| Branding    | VESPER / Expedition control (fictional UI theme)                                  |
| Views       | `01 Navigation` · `02 Drone systems` · `03 Captain view`                          |
| Status      | Static interactive prototype; fiction telemetry only — **not yet wired** to STM32 |


Open locally by serving the folder (recommended) or opening the file in a browser:

```bash
# from repo root
npx --yes serve Command
```

### Goal

Show mode, sensors, charge, threats and critical events so markers can follow the demo even if they miss the 8×8 matrix.

### Development / deploy model


| Stage        | What                                                          |
| ------------ | ------------------------------------------------------------- |
| Design       | ✅ HTML mockup lives in `Command/`                             |
| Annotate     | Add `data-cc-*` hooks on existing nodes (keep VESPER styling) |
| Wire         | Bind those nodes to a shared JS state model                   |
| Local deploy | Serve `Command/` from a tiny static server + optional bridge  |
| Live data    | UART (or later Wi‑Fi) → parser → WebSocket / SSE → page       |
| Demo         | Browser on the lab PC; Tera Term for raw line debug only      |


Preferred layout:

```text
Command/
  index.html          # imported mockup (current)
  src/                # bindings + state (to add)
  bridge/             # UART/Wi-Fi → WebSocket (to add)
  fixtures/           # recorded mission logs for mock mode (to add)
```

Do **not** require Electron or a packaged standalone GUI.

### Wire mockup → EE2028 features

Reuse the existing three tabs. Map fiction panels to real drone data; keep unused fiction UI as decorative unless it has a clear EE2028 meaning.


| Mockup view / node          | Existing id / region         | EE2028 feature to wire                       |
| --------------------------- | ---------------------------- | -------------------------------------------- |
| Header uplink               | `.online`                    | Link state: UART / Wi‑Fi / severed           |
| Header clock                | `#clock`                     | Host clock or mission uptime                 |
| Captain · mode              | `#mode`, `#flightState`      | `STANDBY` / `BATTLE` / `LAST_OF_2028`        |
| Systems · energy bar        | `.bar i` under ENERGY CORE   | Laser charge 0–10 (width %)                  |
| Systems · energy %          | Quantum cell `<strong>`      | Charge text `n/10` + armed cue               |
| Systems · temp              | `#temp`                      | Temperature `T` (°C)                         |
| Systems · devices row       | `#devices`                   | Rebuild cards for `P H A G M` (+ units)      |
| Systems · event stream      | `#events`                    | Mission event log (`EVT …`)                  |
| Systems · diagnostics       | `#diagnostics`               | Threshold summary / ALL OK vs violations     |
| Captain · advisory          | `#warningText`               | Attack / IR threat / threshold alert text    |
| Captain · HUD label         | `.hudtext` / `#flightState`  | Mode + countdown when in LAST_OF_2028        |
| Navigation · hazards        | `#hazards`, notice panels    | Optional IR / radar threat cue               |
| Navigation · route / marker | `#routePath`, `#droneMarker` | Optional radar azimuth / aim story (stretch) |


Suggested `data-cc-*` hooks to add on top of current markup (do not rip out VESPER chrome):

```html
<span class="online" data-cc-link>UPLINK ESTABLISHED</span>
<span id="clock" data-cc-uptime></span>
<h2 id="mode" data-cc-mode>Manual standby</h2>
<strong data-cc-charge>0/10</strong>
<div class="bar"><i data-cc-charge-bar style="width:0%"></i></div>
<span id="temp" data-cc-t>--</span>
<div id="devices" data-cc-sensors></div>
<p id="diagnostics" data-cc-thresholds>ALL OK</p>
<p id="events" data-cc-event-log></p>
<p id="warningText" data-cc-alert></p>
<span data-cc-armed>NO</span>
<span data-cc-ir>CLEAR</span>
<span data-cc-radar-az>--</span>
<span data-cc-countdown></span>
```

### Visual rules

- Mode colours: Standby = steady, Battle = mint / 1 Hz cue, LAST_OF_2028 = orange warning / 2 Hz alarm (toggle CSS classes from JS).
- Threshold violation: update `#diagnostics` and highlight sensor cards until all recover.
- LAST_OF_2028: show countdown in captain HUD; at 0 mark terminated, freeze `#events`, set uplink to severed.
- Prefer browser deploy; keep fiction map/autopilot demo only if it does not fight live telemetry.

### UART message contract (bridge parser)

Keep compulsory telemetry format compatible with the assignment. Add tagged event lines for the dashboard:

```text
# Mode (once on transition)
Entering STANDBY MODE
Entering BATTLE MODE

# 1 Hz telemetry (units required)
T: 26.40 C, P: 101.25 kPa, H: 55.10 %RH, A: 0.98 g, G: 2.10 dps, M: 42.00 uT

# Threshold example (replaces telemetry while any out of range)
G:12.50 dps, exceeds threshold of 8.00 dps.

# Enhancement events (parseable tags)
EVT MODE=LAST_OF_2028
EVT ATTACK
EVT CHARGE=6/10
EVT FIRE charge=1/10
EVT IR=1
EVT RADAR az=3
EVT COUNTDOWN=7
EVT RESCUE
EVT TERMINATE
```

Rules:

- Friend owns mandatory telemetry / threshold lines.
- Darren owns `EVT ...` tags, the bridge parser and wiring `Command/`.
- Bridge / page must tolerate unknown lines without crashing.
- After `EVT TERMINATE`, stop expecting further data (comms severed).

### Implementation plan

Tasks:

- [x] Import HTML mockup into the repo (`Command/index.html`).
- [ ] Agree message tags with friend (do not break compulsory formats).
- [ ] Add `data-cc-*` hooks to the existing VESPER nodes listed above.
- [ ] Split interactive prototype JS from live-binding JS (`Command/src/`).
- [ ] Implement JS state model + fixture/mock feeder.
- [ ] Rebuild `#devices` cards for real `T P H A G M` (or bind dedicated spans).
- [ ] Map energy core UI to laser charge / armed / fire events.
- [ ] Map `#events` + `#warningText` to attack / IR / countdown / terminate.
- [ ] Implement UART bridge (115200 8N1) → WebSocket/SSE → page.
- [ ] Support offline replay for demo without the board.
- [ ] Keep UART path working if Wi‑Fi/BLE is added later (same payload → same web UI).

### Stretch: Wi‑Fi / BLE

- Same `Command/` dashboard and same `EVT` / telemetry payload.
- Bridge transport switches; browser UI stays identical.
- UART remains the fallback Command Center link.

Deliverable: `Command/index.html` mockup wired into a **browser-deployed** Command Center that tracks a live or recorded EE2028 mission. See `docs/Command_Center_Design.md`.

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
6. RD-03E ranging (Task 6a) before azimuth/servo stretch (Task 6)
7. Radar/servos simulation (Task 6)
8. Command Center web dashboard (HTML mockups + UART bridge)
9. Wi-Fi/BLE into the same web UI (same payload)

After each step, rerun the entire mandatory acceptance checklist. Revert or disable an enhancement if it breaks compulsory behavior.

---

## Recommended order for Darren

1. Agree on interfaces and branch ownership with your friend.
2. ~~Implement HT16K33 matrix driver.~~ ✅
3. ~~Implement pure bitmap/font/status-display logic (Task 2).~~ ✅
4. ~~Implement non-blocking buzzer patterns (Task 3).~~ ✅
5. ~~Implement IR hardware smoke tests in isolated test code (Task 4).~~ ✅
6. RD-03E ranging parser + single-person registration prototype (**Task 6a — next**).
7. Keep Task 6 azimuth/servo simulation as stretch (do not assume RD-03E XY).
8. ~~Import Command Center HTML mockup.~~ ✅ (`Command/index.html`)
9. Annotate `data-cc-*` hooks and draft UART/`EVT` tags with friend (**Task 8**).
10. Wait for the mandatory baseline integration checkpoint.
11. Merge and bind matrix first; add other enhancements one by one.
12. Wire the `Command/` web bridge last (needs stable UART lines; mock/fixture mode can proceed earlier).

## Definition of done

Darren's parallel work is ready to merge when:

- It does not modify CubeMX-generated basic files.
- Every module has a small, documented API.
- No module calls `HAL_Delay()`.
- Fake events can demonstrate every enhancement flow.
- Missing hardware returns an error without blocking the main application.
- The mandatory project can compile with enhancements disabled.
- Integration can be performed one module at a time.

