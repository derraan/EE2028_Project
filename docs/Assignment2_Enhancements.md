# Assignment 2 — Enhancement Ideas & Hardware Notes

Notes for possible **beyond-basic** features on the EE2028 spaceship drone, plus STM32 Discovery / expansion hardware that could support them.

> Status legend: `idea` · `planned` · `owned` · `maybe` · `stretch` · `hardware-heavy`

---

## 0. Hardware we already have

Acquired Grove modules (pair with STM32 IoT Discovery + Grove cables / base shield or jumper wiring):

| Module | Typical interface | Role on the drone |
|--------|-------------------|-------------------|
| **Grove Red LED Matrix** (8×8 + HT16K33) | I2C (addr often `0x70`–`0x77`) | On-drone HUD: mode icon, laser charge bar, “attacked” / explode animations |
| **Infrared Reflective Sensor** | GPIO digital (or analog, depending on board) | Close-range object / “hostile” / soft projectile detect when IR reflects off a surface |
| **Grove Buzzer** | GPIO or TIM PWM | Laser fire “pew”, LAST_OF_2028 alarm, capacitor-explode siren |

### How these three compose a coherent enhancement set

| Story beat | LED Matrix | IR Reflective | Buzzer |
|------------|------------|---------------|--------|
| Standby | Solid “S” / idle eyes | Idle | Off |
| Threat / zombie near | Warning glyph | Edge / object → high | Short chirp |
| Battle + charge | Fill bar 0–8 columns | Optional auto-fire if IR trips while charged | Charge blip / fire tone |
| Fire / “kill” | Flash blast frame | — | PWM tone burst |
| LAST_OF_2028 | Blink “!” / skull | — | 2 Hz alarm |
| Capacitor explode | Full-matrix strobe then blank | — | Rising pitch → silence |
| Comms severed | All LEDs off | — | Off |

**Wiring notes (STM32):** Matrix on an free I2C bus (watch address clash with on-board MEMS — scan bus). IR digital out → EXTI or polled GPIO. Buzzer → GPIO toggle or TIM PWM (better tones). Confirm 3.3 V compatibility; many Grove parts accept 3.3 V / 5 V — check your exact SKUs.

---

## 1. Enhancement brainstorm

### 1.1 mmWave radar — projectile / threat detection
| Field | Notes |
|-------|--------|
| Status | `idea` (not owned — IR is the near-term substitute) |
| Goal | Detect incoming “projectiles” or nearby moving objects without contact sensors |
| Near-term substitute | **IR Reflective Sensor**: treat edge/object pass as “projectile inbound” or “hostile close” |
| How it fits | New threat input → jump Standby→Battle, or trigger dodge / shield / laser fire |
| Software hooks | `Read_Sensors()` + new flag `threat_detected`; optional Battle-mode auto-fire |
| Challenges | True mmWave still needs a separate module; IR is short-range and surface-dependent |
| Demo idea | Sweep a white card / hand past IR → UART `"Projectile inbound!"` + matrix flash + buzzer |

### 1.2 Command Center dashboard
| Field | Notes |
|-------|--------|
| Status | `planned` (PC UART) + on-drone HUD via **LED Matrix** |
| Goal | Live “mission control” view of drone state, T/P/H/A/G/M, laser charge, warnings |
| How it fits | Host PC UI over UART / USB VCP; matrix is the **local** Command HUD |
| Software hooks | Structured telemetry from `Send_Telemetry()`; matrix redraw on mode/charge change |
| Challenges | Protocol + UI time; keep matrix glyphs simple (8×8) |
| Demo idea | Serial dashboard + matrix charge bar staying in sync |

### 1.3 Projectile propulsion (shooting / “killing” device)
| Field | Notes |
|-------|--------|
| Status | `owned` path — **buzzer + matrix** simulated kill (no real launcher needed) |
| Goal | Simulated “laser / projectile” actuator when Battle charge ≥ threshold |
| Soft options (ours) | Buzzer “pew”, matrix blast frame, UART fire event |
| Hard options | Solenoid / Nerf — skip unless course allows |
| Software hooks | Existing `laser_charge` fire path in `Handle_Battle_Mode()` |
| Challenges | Keep it clearly **simulated** for lab safety |
| Demo idea | PB charge → matrix bar fills → fire → buzzer + blast glyph + charge drop |

### 1.4 Zombie / hostile detection
| Field | Notes |
|-------|--------|
| Status | `planned` with **IR Reflective Sensor** |
| Goal | Detect “hostile” / object in zone without ToF/mmWave |
| Sensing options | **IR reflective** (owned); later ToF / PIR / mmWave if needed |
| How it fits | IR trip in Standby → Battle; or narrate as zombie / boarding attempt |
| Challenges | Needs a reflective target (paper, hand, line); calibrate threshold / polarity |
| Demo idea | Hand over IR → matrix “Z” + `"Zombie signature detected"` → Battle + chirp |

### 1.5 New enhancement — open slots
Use these as placeholders while deciding the final pitch:

| ID | Candidate | Why it might score well |
|----|-----------|-------------------------|
| 5a | **Voice / tone command** (mic + simple energy detect) | Uses on-kit MEMS mics; “voice of Command Center” |
| 5b | **Formation / swarm sim** (2 boards UART/BLE) | Dual-drone story without flying hardware |
| 5c | **Shield / capacitor bank UI** | Ties to charge + explode mode; clear state machine |
| 5d | **GPS / mock navigation waypoints** | Only if GNSS expansion available; else fake path from magnetometer heading |
| 5e | **Black-box SD / flash flight log** | Post-mission dump after self-destruct |
| 5f | **Gesture control** (ToF / IMU flick) | Enter Battle without PB double-press |

*(Pick 1–2 strong demos rather than many half-finished ones.)*

### 1.6 Capacitor explode mode
| Field | Notes |
|-------|--------|
| Status | `planned` — strong fit for **matrix + buzzer** |
| Goal | Dramatic alternate to / extension of self-destruct: charge bank overload → “detonate” |
| How it fits | After LAST_OF_2028 timeout, or deliberate Battle overcharge (`laser_charge` → 1.0 then overload) |
| Effects | Matrix full-strobe then blank; rising buzzer; UART sacrifice lines; cut telemetry |
| Software hooks | New state e.g. `CAPACITOR_OVERLOAD` or branch inside `Handle_Last_Of_2028_Mode()` |
| Challenges | Keep purely **simulated** (no real high-energy discharge) |
| Demo idea | Fail rescue → matrix fills → explode animation + siren → silence (“Cheerio!”) |

### 1.7 Maybe: actual drone (UAV)
| Field | Notes |
|-------|--------|
| Status | `stretch` / `hardware-heavy` |
| Goal | Mount Discovery (or Nucleo) on a small quad / use companion FC |
| Reality check | Flight stack, ESCs, LiPo, crash risk, marking time — usually too large for A2 alone |
| Lighter variants | Ground “rover” chassis; gimbal-only pan; static “drone” prop + LED props spinning |
| Recommendation | Treat **true flight** as stretch; ship solid modes + 1–2 sensor enhancements first |

---

## 2. Suggested priority (for marking / demo)

Given owned Grove parts, lean into **matrix HUD + IR threat + buzzer FX** first; mmWave / real UAV stay optional.

| Priority | Enhancement | Hardware used | Effort | Wow factor |
|----------|-------------|----------------|--------|------------|
| P0 | On-drone HUD + laser charge / mode glyphs | LED Matrix | Medium | High |
| P0 | Simulated laser fire + alarms | Buzzer (+ matrix) | Low | High |
| P1 | Capacitor explode / rich self-destruct | Matrix + buzzer | Low–Med | High |
| P1 | Zombie / projectile-close detect | IR Reflective | Low–Med | Medium–High |
| P2 | PC Command Center (UART telemetry UI) | ST-LINK VCP | Medium | High |
| P3 | True mmWave projectile radar | *(not owned)* | High | High |
| P3 | Actual flying drone | *(stretch)* | Very high | High (risk) |

---

## 3. STM32 Discovery / kit hardware for drone features

Course boards are typically **STM32L4 IoT Discovery** class (e.g. **B-L475E-IOT01A**) or newer **B-U585I-IOT02A**. On-board MEMS map cleanly to your skeleton variables **T, P, H, A, G, M**.

### 3.1 On-board sensors (B-L475E-IOT01A — common EE kit)

| Sensor | Part | Maps to | Drone / enhancement use |
|--------|------|---------|-------------------------|
| Temperature + humidity | **HTS221** | `T`, `H` | Environment telemetry; Standby threshold → Battle |
| Pressure / barometer | **LPS22HB** | `P` | Altitude proxy; cabin pressure warnings |
| Accel + gyro (IMU) | **LSM6DSL** | `A`, `G` | Upside-down → `LAST_OF_2028`; shake/impact “hit” |
| Magnetometer | **LIS3MDL** | `M` | Heading / “navigation”; compass on dashboard |
| User LED(s) | On-kit LD | — | Mode: solid / 1 Hz / 2 Hz / explode strobe |
| User push-button | On-kit B1 | PB EXTI | Double-press Battle entry / rescue |
| UART (ST-LINK VCP) | USB Virtual COM | Telemetry | Command Center serial link |
| Wi‑Fi (ISM43362) | On-kit module | Optional | Wireless dashboard / cloud-ish demo |
| Bluetooth | On-kit module | Optional | Phone Command Center |
| MEMS microphones | On-kit (if present / U5 kit) | Optional | “Zombie moan” / voice energy detect |

### 3.2 On-board extras (B-U585I-IOT02A — if available)

| Sensor | Part | Enhancement fit |
|--------|------|-----------------|
| IMU | **ISM330DHCX** | Same as A/G; often better motion features |
| Magnetometer | **IIS2MDCTR** | Heading `M` |
| Pressure | **LPS22HH** | `P` |
| Humidity / temp | **HTS221** | `H`, `T` |
| Time-of-Flight + gesture | **VL53L5CX** (multizone) | Zombie proximity, gesture Battle entry, “projectile close” proxy |
| Ambient light | **VEML6030 / VEML3235** | Day/night ops mode; laser “visibility” flair |
| Dual MEMS mics | **MP23DB01HP** | Acoustic event / command cue |
| Wi‑Fi + BLE | On-kit | Dashboard without USB cable |

### 3.3 Owned Grove add-ons (use these first)

| Hardware | Interface | Use for | Driver sketch |
|----------|-----------|---------|---------------|
| **Grove Red LED Matrix w/Driver** | I2C (HT16K33) | Mode HUD, charge 0–8 bars, fire/explode frames | Port Seeed HT16K33 patterns to HAL I2C |
| **Infrared Reflective Sensor** | Digital GPIO (EXTI) or ADC | Zombie / projectile-close / line or card “shot” detect | Debounce + polarity check in `Read_Sensors()` |
| **Grove Buzzer** | GPIO or TIM PWM | Fire, alarm, explode siren | `HAL_TIM_PWM` preferred over busy-wait beeps |

### 3.4 Other Discovery / expansion options (not owned)

| Hardware | Interface | Use for |
|----------|-----------|---------|
| **X-NUCLEO-53L0A1 / 53L1A1** (VL53 ToF) | I2C | Longer-range proximity if IR is too short |
| **X-NUCLEO-IKS01A3** (motion MEMS pack) | I2C | Extra IMU / mag / pressure if base board is bare Nucleo |
| **X-NUCLEO-GFX01M2** or similar display | SPI | Richer HUD than 8×8 (optional; matrix already covers basics) |
| **X-NUCLEO-IDW01M1 / IDB05A1** Wi‑Fi / BLE | UART/SPI | Wireless Command Center |
| **mmWave module** (e.g. IWR / RD-03 / HB100-class DIY) | UART/GPIO | True projectile / motion radar |
| **PIR motion module** | GPIO | Wider-area “hostile entered zone” |
| **Servo + PWM** | TIM PWM | Turret aim (optional with buzzer fire) |
| **Relay / MOSFET board** | GPIO | *Simulated* kill switch / “comms cut” (safe loads only) |
| **Camera module** (DCMI / SPI cam) | Parallel/SPI | Stretch: vision zombie detect |
| **GNSS (X-NUCLEO-GNSS1A1)** | UART | Mock mission map / geofence |
| **Motor driver + rover chassis** | GPIO/PWM | “Actual drone” lite — ground vehicle |
| **Quad FC + UART MAVLink** (Pixhawk-class) | UART | True UAV companion — stretch only |

### 3.5 Feature → hardware quick map

| Feature | Preferred hardware |
|---------|-------------------|
| Basic T/P/H/A/G/M telemetry | IoT Discovery on-board MEMS |
| Upside-down / attack | LSM6DSL / ISM330 gyro-accel |
| Laser charge + fire FX | **LED Matrix** + **Grove Buzzer** + PB |
| Local Command HUD | **Grove Red LED Matrix** |
| PC Command Center | ST-LINK VCP UART; optional Wi‑Fi/BLE |
| Zombie / hostile / soft projectile | **IR Reflective Sensor** (ToF/mmWave later) |
| Capacitor explode mode | **LED Matrix** + **Buzzer** + UART lines |
| Actual drone | Rover chassis **or** separate flight controller (stretch) |

---

## 4. Open decisions

- [x] Soft projectile / kill FX: **buzzer + matrix** (owned)
- [x] Hostile / close-threat sensing: **IR reflective** (owned); mmWave deferred
- [ ] Confirm course board: **B-L475E-IOT01A** vs **B-U585I-IOT02A** vs Nucleo + Grove base
- [ ] Which I2C bus / pins for HT16K33 (avoid clash with MEMS)
- [ ] IR module: digital vs analog version + EXTI pin choice
- [ ] Buzzer: active (on/off) vs passive (needs PWM) — check the exact Grove SKU
- [ ] Dashboard: serial-only vs custom UI (matrix already covers local HUD)
- [ ] Final shortlist (suggested): **Matrix HUD + IR threat + Buzzer fire/explode** (+ UART Command Center if time)

---

## 5. Links

- [STM32 Step-by-step: sensors (L4 / U5 IoT kits)](https://wiki.st.com/stm32mcu/wiki/STM32StepByStep:Step4_Sensors_usage)
- B-L475E-IOT01A: HTS221, LPS22HB, LSM6DSL, LIS3MDL
- B-U585I-IOT02A: HTS221, LPS22HH, ISM330DHCX, IIS2MDCTR, VL53L5CX, mics, Wi‑Fi/BLE
- [Grove Red LED Matrix w/Driver (HT16K33)](https://wiki.seeedstudio.com/Grove-Red_LED_Matrix_w_Driver/)
- [Seeed HT16K33 matrix driver (GitHub)](https://github.com/Seeed-Studio/Grove_LED_Matrix_Driver_HT16K33)
