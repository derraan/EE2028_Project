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

### 1. Matrix HUD + laser charge / fire
| | |
|--|--|
| **What** | Mode icon (Standby / Battle / LAST_OF_2028) + 0–8 column charge bar; fire = blast frame + buzzer |
| **Why** | Demo works without a PC; uses owned matrix + buzzer |
| **Hooks** | Redraw on mode / `laser_charge` change; fire path in `Handle_Battle_Mode()` |
| **Effort** | Medium (HT16K33 I2C + a few bitmaps) |

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
| **What** | No rescue in 10 s → matrix strobe → blank, rising buzzer, sacrifice + `Cheerio!`, cut telemetry |
| **Why** | Memorable ending; almost no new hardware |
| **Hooks** | Timing inside `Handle_Last_Of_2028_Mode()` |
| **Effort** | Low–medium |

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
| **Hooks** | Threat flag → Battle; aim loop updates servo angles from radar (or coarse sector); fire when aligned + `laser_charge` ready |
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
3. PB charge → matrix bar → fire (buzzer + flash)  
4. Flip → LAST_OF_2028; rescue or explode  

**Advanced (#5–#6):**  
5. mmWave tracks target → 2‑DOF servos aim laser → auto/assisted fire  
6. Phone/laptop gets live Wi‑Fi or BLE telemetry  

**Final (#7):**  
7. UAV armed in Battle; modes drive companion behaviour; safe land / cut on LAST_OF_2028 or explode  

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
