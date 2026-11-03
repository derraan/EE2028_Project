# Telemetry + EVT wire contract

Shared UART stream for Tera Term and VESPER. One message per line (`\r\n` or `\n`).

**Transport target:** USART → ST-LINK Virtual COM @ **115200 8N1** (replacing PR #3 semihosting `printf`). Until Cube USART retarget lands, `Telemetry_WriteLine()` defaults to `printf`.

Firmware helpers: [`App/Inc/telemetry_evt.h`](../App/Inc/telemetry_evt.h), [`App/Src/telemetry_evt.c`](../App/Src/telemetry_evt.c).

---

## 1) Human-readable sensor line (PR #3 layout — keep)

```text
Telemetry -> T:26.4 °C, P:1012.5 hPa, H:55.1 % | Accel:[0.0, 0.1, 9.8] m/s^2 | Gyro:[0.0, 0.0, 0.2] dps | Magneto:[31.0, 42.0, 18.0] mgauss
```

| Field | Unit | Axes |
|-------|------|------|
| T | °C | scalar |
| P | hPa | scalar |
| H | % | scalar |
| Accel | m/s^2 | X, Y, Z |
| Gyro | dps | X, Y, Z |
| Magneto | mgauss | X, Y, Z |

Threshold violation lines from PR #3 (e.g. `T:40.10 C, exceeds threshold…`) remain valid; VESPER keeps them in the raw log and threshold panel. When all readings recover, firmware may emit `EVT THRESHOLD_OK` so the dashboard clears a sticky warning.

---

## 2) Machine-readable `EVT` lines

| Line | When | Notes |
|------|------|--------|
| `EVT MODE=STANDBY_MODE` | Enter Standby | |
| `EVT MODE=BATTLE_MODE` | Enter Battle | |
| `EVT MODE=THE_LAST_OF_2028` | Enter Last of 2028 | Preferred tag for new EVT (see §3) |
| `EVT CHARGE=n/10` | After a successful recharge | `n` is charge **after** the add |
| `EVT FIRE charge=n/10` | After a accepted shot | `n` is charge **after** the shot |
| `EVT COUNTDOWN remaining_ms=N` | During Last of 2028 | From firmware rescue deadline |
| `EVT SELF_DESTRUCT reason=…` | Self-destruct starts | Tokens: `timeout`, `rescue_limit`, `early` |
| `EVT TERMINATE` | Mission end | **Immediately before** `Cheerio!` |
| `EVT ATTACK` | Optional | Pairs with `Drone was attacked!` prose |
| `EVT RESCUE` | Optional | Pairs with successful rescue prose |
| `EVT THRESHOLD_OK` | Optional | Clear sticky threshold warning |

Example stream (plan shape):

```text
Telemetry -> T:26.4 °C, P:1012.5 hPa, H:55.1 % | Accel:[0.0, 0.1, 9.8] m/s^2 | Gyro:[0.0, 0.0, 0.2] dps | Magneto:[31.0, 42.0, 18.0] mgauss
EVT MODE=BATTLE_MODE
EVT CHARGE=6/10
EVT FIRE charge=1/10
EVT MODE=THE_LAST_OF_2028
EVT COUNTDOWN remaining_ms=9000
EVT SELF_DESTRUCT reason=timeout
EVT TERMINATE
Cheerio!
```

---

## 3) Mode naming note

| Layer | Tag |
|-------|-----|
| Firmware enum (integration) | `LAST_OF_2028` |
| **EVT `MODE=` value (emit this)** | `THE_LAST_OF_2028` |
| PR #3 human printf today | `THE LAST OF EE2028` / enum `THE_LAST_OF_EE2028` |

Until the adapter sibling settles gameplay naming, VESPER accepts both `THE_LAST_OF_2028` and legacy `THE_LAST_OF_EE2028` / prose containing `LAST OF` as Last-of-2028. **New firmware EVT emitters use `THE_LAST_OF_2028`.**

---

## 4) Parser rules (live UART + fixture replay)

1. Buffer chunks until `\n` (strip trailing `\r`).
2. Parse sensor lines and known `EVT` messages into structured state.
3. Keep unrecognized text in the raw log (do not drop).
4. Preserve units and vector axes in parsed telemetry.
5. Cable / Web Serial disconnect → link loss (`severed`), not mission end.
6. `EVT TERMINATE` → mission end; still read through following lines (`Cheerio!`).
7. Same parser for live UART and `Command/fixtures/*.log` replay.

---

## 5) What this work does **not** own

Matrix / buzzer / IR / relay adapter wiring (sibling Integration Adapter plan). Drone gameplay rules stay in the baseline state machine.
