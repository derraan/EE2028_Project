# Assignment 2 — Enhancements

High-impact extras for the spaceship drone, biased toward **hardware we already own** first, then stretch builds. Basics (T/P/H/A/G/M, three modes, PB, UART) stay in firmware; this file tracks beyond-basic ideas and **as-built status**.

**As-built (merged):** enhancements #1–#4 are largely implemented under `App/` + `Command/`, gated by `-DEE_ENHANCEMENTS=1` for hardware effects. Stretch #5–#7 remain aspirational.

**Button-only input enhancement** (dual-edge EXTI, GPIO duration windows, debounce, mode/laser/rescue gesture map): see [`Button_Input_Enhancement.md`](Button_Input_Enhancement.md). That doc is scoped to the USER button only — not matrix/IR/buzzer.

---

## Owned hardware

| Module | Interface | Role |
|--------|-----------|------|
| **Grove Red LED Matrix** (8×8, HT16K33) | I2C | On-drone HUD: mode glyph, charge bar, FX |
| **Infrared Reflective Sensor** | GPIO (digital / analog) | Close-range threat / “hostile” detect |
| **Grove Buzzer** | GPIO or TIM PWM | Fire tone, alarms, explode siren |

**Wiring:** Matrix on a free I2C bus (scan address; avoid clash with MEMS). IR → polled GPIO or EXTI. Buzzer → GPIO or PWM — check if your SKU is active (on/off) or passive (needs PWM). Confirm 3.3 V OK for your boards.

---

## Recommended enhancements (build these first)

### 1. Matrix overall status display — **implemented**
| | |
|--|--|
| **What** | One 8×8 display presents every main feature: charge state, armed state, radar azimuth, mode changes and self-destruct countdown |
| **Charging** | Fill a 0–8 column progress bar. Once armed (charge ≥ 5/10 in Battle), the bar itself blinks until charge drops below 5 (the rest of the matrix stays dark) |
| **Radar** | Draw a moving vertical line whose horizontal position represents the locked RD-03D target's azimuth, left to right (Battle). Standby shows the same lock as a 180° dial needle. Fed live by the adapter (`RadarTrack_MatrixColumn`) |
| **Mode text** | Scroll `BATTLE MODE` once when Battle begins. Scroll `THE LAST OF EE2028` repeatedly for the whole THE_LAST_OF_2028 mode |
| **Countdown** | Live path: one 10 s drone clock via `rescue_remaining_ms` / `StatusDisplay_SetRescueRemainingMs`. Harness may still use a longer standalone sequence. At `0`, explosion frame then blank |
| **Hooks** | `status_display` + `Enhancements_Update` from `DroneStatus_t` snapshots |
| **Effort** | Medium (HT16K33 I2C, scrolling text/font, bitmaps and display arbitration) |

#### Display priority

Only one image fits on an 8×8 matrix, so use this priority:

1. Explosion frame / shutdown
2. THE_LAST_OF_2028 countdown and repeating text
3. One-time `BATTLE MODE` transition text
4. Locked radar target (line in Battle, dial in Standby; the line blinks when armed)
5. Armed flashing
6. Charging progress

Mode text temporarily interrupts the normal screen. Radar tracking resumes after `BATTLE MODE` finishes scrolling. A locked zombie outranks the armed flash and charge bar because it is the most actionable thing on the HUD; with no lock the armed flash / charge bar show as before. In THE_LAST_OF_2028, the repeating warning and countdown override radar and charging: the live 10 s window scrolls `THE LAST OF EE2028` until 5 s remain, then flashes the digits 5…0.

### 2. IR threat / “zombie” detect — **as shipped (observe-only)**
| | |
|--|--|
| **What (shipped)** | Object/hand over IR → UART warning + buzzer chirp only |
| **What (stretch, not wired)** | Standby→Battle, or auto-fire if charged — explicitly out of scope for the frozen adapter |
| **Why** | Interactive cue; good stand-in until mmWave is ready |
| **Hooks** | `IR_Threat_Poll` inside `Enhancements_Update`; D3/PB0 active-LOW |
| **Effort** | Low–medium |

### 3. Richer THE_LAST_OF_2028 / capacitor explode — **implemented**
| | |
|--|--|
| **What** | Start a 10-second countdown on entry. Scroll `THE LAST OF EE2028` repeatedly, accelerate the buzzer beeps as `0` approaches, then show the explosion at `0` |
| **At zero** | Set `CAP_EXPLODE_Pin` **HIGH** to activate a relay-controlled safe effect; send sacrifice + `Cheerio!`, show the explosion frame, sound the final buzzer burst, blank the matrix and stop normal telemetry |
| **Rescue** | A valid upright double-press before `0` cancels the countdown, keeps `CAP_EXPLODE_Pin` LOW and returns to Battle Mode |
| **Hooks** | Baseline: `Enter_The_Last_Of_2028()` + inline `case THE_LAST_OF_2028` in `main.c`. Effects: `Enhancements_Update` / `DRONE_EVENT_SELF_DESTRUCT_STARTED`. GPIO defaults LOW |
| **Effort** | Low–medium |

The GPIO drives the relay module's control input, not a relay coil directly. The relay must switch only a safe, instructor-approved low-voltage simulation load. Do not rupture, short or overcharge a physical capacitor.

### 4. Simple Command Center (UART) — **implemented (VESPER *The Last Signal*)**
| | |
|--|--|
| **What** | Localhost Three.js game + HUD: NUS E4-03-07 lab defence, mode / sensors / charge / events; **virtual board**, fixture replay, paste log, Web Serial |
| **Why** | Markers see state even if they miss the matrix; turns UART into a playable demo without changing firmware authority |
| **Hooks** | `telemetry_evt.*` + [`Command/`](../Command/README.md); contract in [`Telemetry_EVT_Contract.md`](Telemetry_EVT_Contract.md); design in [`Command_Center_Design.md`](Command_Center_Design.md) |
| **Effort** | Medium–high (parser + virtual board + 3D world); offline-friendly (`node Command/serve.mjs`, vendored three.js) |

---

## Advanced enhancements

### 5. mmWave radar projectile detection + 2‑DOF auto-aim laser
| | |
|--|--|
| **What** | RD-03D mmWave radar detects and localises (azimuth) the nearest person / "zombie"; a pan servo aims a low-power laser pointer at their body (implemented as azimuth-only. Tilt was dropped because the RD-03D reports no elevation) |
| **Why** | Strong wow feature; real sensing + actuation beyond IR |
| **Hardware** | Ai-Thinker **RD-03D** on UART4 (D0/D1, 256000 baud), **1× hobby servo** on TIM3_CH1 (D5), Class 1 laser module on D7. Radar fixed; servo pans the laser only |
| **Hooks** | Detection only (no mode / fire authority); tracker → parallax-corrected pan angle + matrix column; laser lights in Battle when locked + aligned; `EVT FIRE` strobes it; `EVT RADAR` to VESPER. See `docs/Radar_Aim_Design.md` |
| **Effort** | High (driver + coordinate mapping + mechanical mount) |
| **Safety** | Low-power laser / LED only; lab eye-safety rules; no high-energy beam |

#### 5a. Giants + giant cannon (game dynamic on top of #5)

| | |
|--|--|
| **Story** | The RD-03D detects **giants**: every radar-locked person is a giant 10–20× the size of a zombie. The physical red laser pointer is the drone's **giant cannon**, far bigger than the laser gun |
| **Fire rule** | Battle + giant locked + charge **10/10** → hold-to-fire fires the **cannon** (spends all 10, `EVT CANNON`) instead of the gun. Otherwise hold-to-fire is the normal gun (−5, `EVT FIRE`), which only kills zombies |
| **Recharge** | With a giant locked, single press keeps adding +3 past 5 up to 10/10 ("Cannon charging…"). With no giant the baseline rule (only below 5) is unchanged |
| **Giant HP** | 2 cannon hits (firmware counts on the current lock): `EVT GIANT hit=1/2`, then `EVT GIANT down`. Lock lost → next giant starts at full HP |
| **Physical laser** | Off normally; lights for a **1 s beam** per cannon shot on the locked giant (servo aimed). No aim dot, and gun shots never light it |
| **Feedback** | Buzzer: charge-up + blast (hit) or blast + fanfare (down). Matrix: thick flashing beam on the giant's column (hit) or a burst (down). VESPER: giant rises at the lock, thick red beam, stagger / topple, Giants counter |
| **Scope** | Enhanced build only (`EE_ENHANCEMENTS=1`). The Debug baseline keeps the assignment rules exactly |

### 6. Wi‑Fi / BLE telemetry
| | |
|--|--|
| **What** | Same mission data as #4, over on-kit **Wi‑Fi** and/or **BLE** to a phone or laptop Command Center |
| **Why** | Wireless “mission control”; no USB tether during demo |
| **Hardware** | B-L4S5I-IOT01A Wi‑Fi (ISM43362) / BLE (board of record); B-U585I-IOT02A is an alternate kit family |
| **Hooks** | Reuse telemetry framing from `Send_Telemetry()`; send mode, T/P/H/A/G/M, charge, IR/mmWave events, aim angles |
| **Effort** | Medium–high (stack + reconnect + keep UART as fallback) |

---

## Final enhancement

### 7. Drone UAV
| | |
|--|--|
| **What** | Put the system on (or beside) a real **UAV**: either STM32 as companion computer talking to a flight controller, or a small quad carrying the Discovery for a staged “spaceship drone” demo |
| **Why** | Capstone story — the platform is actually a drone |
| **Approaches** | (A) Companion UART/MAVLink to Pixhawk-class FC — STM32 does modes / sensors / aim, FC flies; (B) Tiny toy quad with Discovery strapped for static/slow demo; (C) Ground “UAV lite” rover if flight is blocked by lab rules |
| **Hooks** | Arm/disarm tied to Standby vs Battle; THE_LAST_OF_2028 → RTL or land; explode = motors cut + telemetry silence (safe fail) |
| **Effort** | Very high (power, weight, crash risk, flight time) |
| **Note** | Ship **#1–#4** solid first; treat UAV as the **final** stretch after mmWave aim + wireless telemetry |

---

## Light optional extras

| Idea | Notes | Effort |
|------|--------|--------|
| **IR as soft “projectile”** | In Battle, IR trip while charged = fire (same as #1–2) | Low |
| **Shake / impact hit** | On-kit accel spike → THE_LAST_OF_2028 | Low |

---

## Parked (unless everything above is done)

- Real kinetic launcher / solenoid / Nerf  
- Camera vision, voice command, GPS mission, multi-drone swarm, SD black-box  

---

## Demo flow

**Core (#1–#4):**  
1. Standby — matrix idle; UART heartbeat / VESPER fixture or Web Serial  
2. IR wave → threat chirp + log (no mode change); double-press or T/A/G → Battle  
3. Matrix scrolls `BATTLE MODE` once, then returns to the active status screen
4. PB charge → bar fills → armed matrix flashes → **hold-to-fire**
5. Flip → matrix repeatedly scrolls `THE LAST OF EE2028`; 10-second countdown begins
6. Rescue before `0`, or hear accelerating beeps → explosion frame at `0` → silence

**Advanced (#5–#6):**  
7. mmWave target moves → matrix azimuth line follows → 2‑DOF servos aim laser
8. Phone/laptop gets live Wi‑Fi or BLE telemetry

**Final (#7):**  
9. UAV armed in Battle; modes drive companion behaviour; safe land / cut on THE_LAST_OF_2028 or explode

---

## Discovery kit map (basics + hooks)

Pin-level detail for everything below: [`docs/Hardware_Pinout.md`](Hardware_Pinout.md).

Board of record: **B-L4S5I-IOT01A** (PR #3 BSP `B-L4S5I-IOT01` / `stm32l4s5i_iot01*`).

| Variable / feature | Hardware |
|--------------------|----------|
| `T`, `H` | HTS221 |
| `P` | LPS22HB (U5: LPS22HH) |
| `A`, `G` | LSM6DSL (U5: ISM330DHCX) — upside-down → THE_LAST_OF_2028 |
| `M` | LIS3MDL (U5: IIS2MDCTR) |
| Mode LED / PB | On-kit LD + B1 (EXTI) |
| UART Command Center (#4) | ST-LINK Virtual COM |
| Wi‑Fi / BLE (#6) | On-kit radio modules |
| HUD / IR / buzzer (#1–#3) | **Owned Grove trio** |
| mmWave aim + giant cannon (#5) | RD-03D on D0/D1 + 1 pan servo (D5) + laser (D7) |
| UAV (#7) | FC + airframe **or** companion link to Discovery |

---

## Two-week feasibility assessment

**Team assumption:** 2 students working part-time for fewer than 2 weeks.

### Honest verdict

The **entire #1–#7 scope is not realistically achievable and demo-stable** in that time. The basic firmware plus **#1–#4** is realistic. One advanced feature may be attempted only after the core demo is stable.

- **High feasibility:** basic state machine and sensors; matrix HUD; IR trigger; buzzer effects; THE_LAST_OF_2028 sequence; UART telemetry.
- **Medium feasibility:** Wi‑Fi **or** BLE telemetry, provided the board support package and examples work quickly.
- **Low feasibility:** mmWave tracking plus calibrated 2‑DOF auto-aim. The radar driver, target coordinates, servo mapping, mechanics and power supply create several integration risks.
- **Very low feasibility:** a safe, reliable flying UAV integration. Flight-controller communication, mounting, weight, power, testing and safety can consume the whole project window.

### Recommended delivery scope

1. **Commit to:** basics + enhancements **#1–#4**.
2. **Choose one stretch:** preferably **#6 Wi‑Fi/BLE telemetry**.
3. **Prototype only if ahead:** #5 mmWave detection first; add servo aiming only after detection is reliable.
4. **Future-work/demo prop:** #7 UAV. Do not make project success depend on flight.

### 14-day timeline

- **Days 1–3 — Student A:** state machine, PB timing, UART and sensor reads.
- **Days 1–3 — Student B:** verify matrix, IR and buzzer separately with minimal test programs.
- **Days 4–6 — Both:** integrate matrix HUD, charge/fire effects and IR threat trigger.
- **Days 7–8 — Both:** implement and test THE_LAST_OF_2028 rescue/timeout/explode behavior.
- **Days 9–10 — Student A:** telemetry and Command Center output.
- **Days 9–10 — Student B:** wiring cleanup, non-blocking timing and hardware reliability tests.
- **Days 11–12 — Both:** end-to-end integration, edge cases and repeatable demo script.
- **Days 13–14 — Both:** report/video, regression testing and contingency buffer.

If #1–#4 are stable by **Day 9**, use the remaining development time for Wi‑Fi/BLE. Stop adding features after **Day 11** so the final days remain available for debugging and presentation work.

### Scope gates

- **Gate 1 (end Day 3):** all basic sensors, PB and UART work independently.
- **Gate 2 (end Day 6):** Grove modules work together without blocking the main loop.
- **Gate 3 (end Day 9):** complete wired demo is repeatable.
- **Gate 4 (Day 11):** freeze features; unfinished advanced work moves to future work.

---

## Checklist

- [x] I2C bus / address for matrix: I2C1 on D15/D14 (PB8/PB9), 0x70, brought up by `Enhancements_HW_MatrixI2C()` (default `EE_MATRIX_I2C`)  
- [x] IR pin + active level (D3/PB0 active-LOW)  
- [x] Buzzer type (active ON/OFF on D2/PD14)  
- [x] Ship **#1–#4** (source + adapter + VESPER; on-board verify pending Cube)  
- [ ] Then **#5** mmWave + pan-tilt servos (if parts available)  
- [ ] Then **#6** Wi‑Fi / BLE telemetry  
- [ ] **#7** UAV last — only after the above are demo-stable  
- [ ] Leave parked ideas parked  

## Links

- [ST sensor step-by-step (L4 / U5 IoT)](https://wiki.st.com/stm32mcu/wiki/STM32StepByStep:Step4_Sensors_usage)  
- [Grove Red LED Matrix (HT16K33)](https://wiki.seeedstudio.com/Grove-Red_LED_Matrix_w_Driver/)  
