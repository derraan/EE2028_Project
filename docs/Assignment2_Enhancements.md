# Assignment 2 — Enhancements

High-impact extras for the spaceship drone, biased toward **hardware we already own** first, then stretch builds. Basics (T/P/H/A/G/M, three modes, PB, UART) stay in firmware; this file is only for beyond-basic ideas.

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

### 1. Matrix overall status display
| | |
|--|--|
| **What** | One 8×8 display presents every main feature: charge state, armed state, radar azimuth, mode changes and self-destruct countdown |
| **Charging** | Fill a 0–8 column progress bar. Once fully charged / armed, flash the full matrix until fired |
| **Radar** | Draw a moving vertical line whose horizontal position represents target azimuth from left to right |
| **Mode text** | Scroll `BATTLE MODE` once when Battle begins. Scroll `LAST OF 2028` repeatedly for the whole LAST_OF_2028 mode |
| **Countdown** | During the final 10 seconds, show `9` through `0` (or a shrinking bar). At `0`, display an explosion frame and then blank the matrix |
| **Hooks** | A single non-blocking display controller selects the highest-priority screen and redraws from `HAL_GetTick()` |
| **Effort** | Medium (HT16K33 I2C, scrolling text/font, bitmaps and display arbitration) |

#### Display priority

Only one image fits on an 8×8 matrix, so use this priority:

1. Explosion frame / shutdown
2. LAST_OF_2028 countdown and repeating text
3. One-time `BATTLE MODE` transition text
4. Armed flashing
5. Charging progress
6. Radar azimuth line

Mode text temporarily interrupts the normal screen. Radar tracking resumes after `BATTLE MODE` finishes scrolling. In LAST_OF_2028, the repeating warning and countdown override radar and charging.

### 2. IR threat / “zombie” detect
| | |
|--|--|
| **What** | Object/hand over IR → UART warning + chirp; Standby→Battle, or auto-fire if charged |
| **Why** | Interactive; good stand-in until mmWave is ready |
| **Hooks** | Flag in `Read_Sensors()`; debounce + check IR polarity |
| **Effort** | Low–medium |

### 3. Richer LAST_OF_2028 / capacitor explode
| | |
|--|--|
| **What** | Start a 10-second countdown on entry. Scroll `LAST OF 2028` repeatedly, accelerate the buzzer beeps as `0` approaches, then show the explosion at `0` |
| **At zero** | Set `CAP_EXPLODE_Pin` **HIGH** to activate a relay-controlled safe effect; send sacrifice + `Cheerio!`, show the explosion frame, sound the final buzzer burst, blank the matrix and stop normal telemetry |
| **Rescue** | A valid double-press before `0` cancels the countdown, keeps `CAP_EXPLODE_Pin` LOW and returns to Battle Mode |
| **Hooks** | Non-blocking countdown and beep interval inside `Handle_Last_Of_2028_Mode()` using `HAL_GetTick()`; GPIO defaults LOW and changes HIGH only at `0` |
| **Effort** | Low–medium |

The GPIO drives the relay module's control input, not a relay coil directly. The relay must switch only a safe, instructor-approved low-voltage simulation load. Do not rupture, short or overcharge a physical capacitor.

### 4. Simple Command Center (UART)
| | |
|--|--|
| **What** | Clear serial lines or a tiny host script: mode, sensors, charge, events (threat / fire / explode) |
| **Why** | Markers see state even if they miss the matrix; pairs with HUD |
| **Hooks** | `Send_Telemetry()` + event prints (not a full web app) |
| **Effort** | Low if text-only; medium if a small dashboard UI |

---

## Advanced enhancements

### 5. mmWave radar projectile detection + 2‑DOF auto-aim laser
| | |
|--|--|
| **What** | mmWave module detects incoming / nearby “projectiles” or targets; two servos pan (**yaw / lateral**) and tilt (**pitch / elevation**) to aim a laser pointer (or LED “laser”) at the track |
| **Why** | Strong wow feature; real sensing + actuation beyond IR |
| **Hardware** | mmWave module (UART/GPIO, e.g. RD-03 / similar), **2× hobby servos** on a pan-tilt bracket, laser diode / bright LED, TIM PWM for both servos |
| **Hooks** | Threat flag → Battle; aim loop updates servo angles from radar; map target azimuth to both pan servo angle and the matrix's eight horizontal positions; fire when aligned + `laser_charge` ready |
| **Effort** | High (driver + coordinate mapping + mechanical mount) |
| **Safety** | Low-power laser / LED only; lab eye-safety rules; no high-energy beam |

### 6. Wi‑Fi / BLE telemetry
| | |
|--|--|
| **What** | Same mission data as #4, over on-kit **Wi‑Fi** and/or **BLE** to a phone or laptop Command Center |
| **Why** | Wireless “mission control”; no USB tether during demo |
| **Hardware** | B-L475E-IOT01A Wi‑Fi (ISM43362) / BLE, or B-U585I-IOT02A Wi‑Fi + BLE |
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
| **Hooks** | Arm/disarm tied to Standby vs Battle; LAST_OF_2028 → RTL or land; explode = motors cut + telemetry silence (safe fail) |
| **Effort** | Very high (power, weight, crash risk, flight time) |
| **Note** | Ship **#1–#4** solid first; treat UAV as the **final** stretch after mmWave aim + wireless telemetry |

---

## Light optional extras

| Idea | Notes | Effort |
|------|--------|--------|
| **IR as soft “projectile”** | In Battle, IR trip while charged = fire (same as #1–2) | Low |
| **Shake / impact hit** | On-kit accel spike → LAST_OF_2028 | Low |

---

## Parked (unless everything above is done)

- Real kinetic launcher / solenoid / Nerf  
- Camera vision, voice command, GPS mission, multi-drone swarm, SD black-box  

---

## Demo flow

**Core (#1–#4):**  
1. Standby — matrix idle; UART heartbeat  
2. IR wave → threat + chirp → Battle  
3. Matrix scrolls `BATTLE MODE` once, then returns to the active status screen
4. PB charge → bar fills → armed matrix flashes → fire
5. Flip → matrix repeatedly scrolls `LAST OF 2028`; 10-second countdown begins
6. Rescue before `0`, or hear accelerating beeps → explosion frame at `0` → silence

**Advanced (#5–#6):**  
7. mmWave target moves → matrix azimuth line follows → 2‑DOF servos aim laser
8. Phone/laptop gets live Wi‑Fi or BLE telemetry

**Final (#7):**  
9. UAV armed in Battle; modes drive companion behaviour; safe land / cut on LAST_OF_2028 or explode

---

## Discovery kit map (basics + hooks)

Typical course board: **B-L475E-IOT01A** (or **B-U585I-IOT02A**).

| Variable / feature | Hardware |
|--------------------|----------|
| `T`, `H` | HTS221 |
| `P` | LPS22HB (U5: LPS22HH) |
| `A`, `G` | LSM6DSL (U5: ISM330DHCX) — upside-down → LAST_OF_2028 |
| `M` | LIS3MDL (U5: IIS2MDCTR) |
| Mode LED / PB | On-kit LD + B1 (EXTI) |
| UART Command Center (#4) | ST-LINK Virtual COM |
| Wi‑Fi / BLE (#6) | On-kit radio modules |
| HUD / IR / buzzer (#1–#3) | **Owned Grove trio** |
| mmWave + 2‑DOF aim (#5) | External radar + 2 servos + laser/LED |
| UAV (#7) | FC + airframe **or** companion link to Discovery |

---

## Two-week feasibility assessment

**Team assumption:** 2 students working part-time for fewer than 2 weeks.

### Honest verdict

The **entire #1–#7 scope is not realistically achievable and demo-stable** in that time. The basic firmware plus **#1–#4** is realistic. One advanced feature may be attempted only after the core demo is stable.

- **High feasibility:** basic state machine and sensors; matrix HUD; IR trigger; buzzer effects; LAST_OF_2028 sequence; UART telemetry.
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
- **Days 7–8 — Both:** implement and test LAST_OF_2028 rescue/timeout/explode behavior.
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

- [ ] I2C bus / address for matrix  
- [ ] IR pin + active level  
- [ ] Buzzer type (active vs passive)  
- [ ] Ship **#1–#4**  
- [ ] Then **#5** mmWave + pan-tilt servos (if parts available)  
- [ ] Then **#6** Wi‑Fi / BLE telemetry  
- [ ] **#7** UAV last — only after the above are demo-stable  
- [ ] Leave parked ideas parked  

## Links

- [ST sensor step-by-step (L4 / U5 IoT)](https://wiki.st.com/stm32mcu/wiki/STM32StepByStep:Step4_Sensors_usage)  
- [Grove Red LED Matrix (HT16K33)](https://wiki.seeedstudio.com/Grove-Red_LED_Matrix_w_Driver/)  
