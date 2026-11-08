# Hardware Pinout — B-L4S5I-IOT01A

The single reference for every pin this firmware uses. Checked on 2026-11-08 against the code (`App/Src/enhancements_hw.c`, `App/Src/telemetry_uart.c`, the board BSP `stm32l4s5i_iot01.h`) and the CubeIDE project's `Core/Inc/main.h` labels.

- **Debug build** (baseline): on-board parts only (§1).
- **Enhanced build** (`EE_ENHANCEMENTS=1 EE_RADAR_AIM=1`): on-board parts + the Arduino-header modules (§2).

Every module is optional. If one is missing the firmware skips it and the drone still runs.

---

## 1. On-board (all builds)

| Function | MCU pin | Peripheral | Notes |
|---|---|---|---|
| Mode LED (LED2, green) | **PB14** | GPIO out | Standby steady, Battle 1 Hz, The Last of EE2028 2 Hz, off after self-destruct |
| USER button (B1, blue) | **PC13** | GPIO in, EXTI15_10 | Active-LOW. Single / double press / hold-to-fire |
| On-board sensors | **PB10** SCL / **PB11** SDA | I2C2 (BSP) | HTS221 (T, H), LPS22HB (P), LSM6DSL (accel, gyro), LIS3MDL (mag) |
| Telemetry → VESPER / Tera Term | **PB6** TX / **PB7** RX | USART1, AF7 | ST-LINK Virtual COM, **115200 8N1** (`telemetry_uart.c`) |
| Programming / debug | SWD | ST-LINK | Flash + semihosting (CubeIDE Console only while debugging) |

No wiring needed: these are on the board. Connect one USB cable to the **ST-LINK** port.

---

## 2. Arduino header (Enhanced build)

```
            Arduino header (top view, board USB at the top)
  D15 ── PB8  I2C1 SCL ─┐ LED matrix (HT16K33, 0x70)
  D14 ── PB9  I2C1 SDA ─┘
  D7  ── PA4  laser cannon (via transistor, active-HIGH)
  D5  ── PB4  pan servo PWM (TIM3_CH1, 50 Hz)
  D4  ── PA3  CAP_EXPLODE relay control (HIGH at self-destruct)
  D3  ── PB0  IR sensor in (active-LOW)
  D2  ── PD14 buzzer (active)
  D1  ── PA0  UART4 TX → RD-03D RX
  D0  ── PA1  UART4 RX ← RD-03D TX
```

| Board pin | MCU pin | Peripheral | Dir | Module | Boot state | Behaviour |
|---|---|---|---|---|---|---|
| **D0** | PA1 | UART4_RX (AF8) | in | RD-03D **TX** | — | 256000 8N1, radar frames ~10 Hz |
| **D1** | PA0 | UART4_TX (AF8) | out | RD-03D **RX** | idle HIGH | Multi-target command at boot, +1 s, +3 s |
| **D2** | PD14 | GPIO out | out | Grove buzzer (active) | **LOW** | ON/OFF rhythms (`docs/Buzzer_Design.md`) |
| **D3** | PB0 | GPIO in, pull-up | in | Grove IR reflective | — | Active-LOW: detect → log + chirp (no mode change) |
| **D4** | PA3 | GPIO out | out | Relay module **IN** (`CAP_EXPLODE`) | **LOW** | **HIGH** once at self-destruct, latched until reset (`docs/Relay_Design.md`) |
| **D5** | PB4 | TIM3_CH1 (AF2) | out | Pan servo **signal** | 1500 µs, then self-test sweep | 50 Hz, 1 µs steps. Tracks the radar lock |
| **D7** | PA4 | GPIO out, pull-down | out | Laser module via transistor | **LOW (off)** | Lit only for a 1 s **giant-cannon** beam (Battle + lock + 10/10 shot) |
| **D14** | PB9 | I2C1_SDA (AF4, open-drain) | i/o | Grove LED matrix **SDA** | — | ~100 kHz, address 0x70 |
| **D15** | PB8 | I2C1_SCL (AF4, open-drain) | out | Grove LED matrix **SCL** | — | |

**Not used by this firmware:** D6, D8–D13, A0–A5. Check `Core/Inc/main.h` before reusing one; some are shared with on-board parts.

---

## 3. Power and wiring rules

| Module | Power | Ground | Must-know |
|---|---|---|---|
| RD-03D radar | **5 V** | GND | Logic is 3.3 V, safe for D0/D1 directly |
| Pan servo | **Own 5 V supply** | **Common GND** with the board | Never power from the board's 3V3; stall current can reset the board |
| Laser module | 5 V or 3.3 V (per module) | via transistor | **Do not drive from D7 directly.** D7 → ~1 kΩ → NPN base (e.g. 2N2222) or logic-level N-MOSFET gate; laser − to collector/drain, emitter/source to GND |
| Relay | Relay **module** VCC | GND | D4 → module IN. Never a bare coil on the pin. Switch only an instructor-approved low-voltage load |
| Grove matrix, buzzer, IR | **3V3** (recommended) | GND | Powering Grove from 3V3 keeps the matrix's I2C pull-ups at 3.3 V |

Share GND between the board and every module.

**Eye safety:** the laser lights only during a cannon beam, is forced LOW at boot, on The Last of EE2028, on self-destruct, and during the servo self-test. Use a low-power (Class 1/2) module and never aim at eyes.

---

## 4. Safe states at a glance

| Event | Laser D7 | Relay D4 | Buzzer D2 | Servo D5 |
|---|---|---|---|---|
| Power-up | OFF | LOW | silent | centre, then one sweep (laser OFF) |
| Standby / Battle | OFF except cannon beam | LOW | cues | tracks the radar lock, else centre |
| The Last of EE2028 | OFF | LOW | countdown beeps | parked centre |
| Self-destruct | OFF | **HIGH (latched)** | final burst → silent | parked centre |
| Rescue before 0 | OFF | stays LOW | "saved" tone | resumes tracking |

---

## 5. Changing a pin

Pins are defined at the top of the matching block in `App/Src/enhancements_hw.c` (`BUZZER_Pin`, `IR_Pin`, `CAP_EXPLODE_Pin`, `LASER_Pin`, matrix I2C1 in `Enhancements_HW_MatrixI2C()`, UART4 in `Radar_UartInit()`, TIM3 in `Servo_TimInit()`). The telemetry UART is in `App/Src/telemetry_uart.c` (board BSP `COM1`). Update this document in the same commit.

> **RD-03E is not supported** on D0/D1. The parser is for RD-03D frames and needs azimuth. See Task 6a in `docs/Darren's_Tasks.md`.
