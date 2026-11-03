# Telemetry + EVT wire contract

Unified **mission UART** stream for Tera Term and VESPER. One message per line (`\r\n` or `\n`).

## Dual sinks (do not merge)

| Path | API | Transport | Audience |
|------|-----|-----------|----------|
| **Mission UART** | `Telemetry_WriteLine` / `Telemetry_Printf` / `Telemetry_SendSensors` / `Telemetry_Evt*` | USART → ST-LINK VCP @ **115200 8N1** (strong override) | Tera Term + VESPER |
| **Debug** | plain `printf` | Semihosting → CubeIDE Console | Developer only |

Until the Cube strong override is linked, the weak `Telemetry_WriteLine` falls back to `printf` so mission lines still appear in CubeIDE. That fallback is temporary — after USART lands, keep `printf` for debug notes only; never send mission/EVT text through raw `printf`.

Enable USART TX: copy [`App/Src/telemetry_uart_hal.c.example`](../App/Src/telemetry_uart_hal.c.example) into the Cube project, set `EE_TELEMETRY_USART=1`.

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

## 3) Mode naming (settled)

| Layer | Tag |
|-------|-----|
| Firmware enum (`DroneState_t`) | `THE_LAST_OF_2028` |
| **EVT `MODE=` value (emit this)** | `THE_LAST_OF_2028` |
| Matrix banner / human printf | `THE LAST OF EE2028` |

VESPER’s parser also accepts **legacy aliases** for old logs only: `THE_LAST_OF_EE2028`, `LAST_OF_2028`, `TEST_OF_2028`, and prose containing `LAST OF`. New firmware must emit `THE_LAST_OF_2028` only.

### Live emitters vs harness-only tags

Wired from `main.c` today: mission prose via `Telemetry_Printf`, sensors via `Telemetry_SendSensors`, and EVT `MODE`, `CHARGE`, `FIRE`, `COUNTDOWN`, `ATTACK`, `RESCUE`, `SELF_DESTRUCT`, `TERMINATE`.

Implemented helper but **not** called from live `main.c` yet: `Telemetry_EvtThresholdOk` (parser clears sticky thresholds on the next good sensor line).

Harness / optional only (not live adapter): `EVT IR=…`, `EVT CAP_EXPLODE=…`, `EVT THREAT=…`. Live IR is prose + chirp; live relay is GPIO via `Enhancements_OnEvent(DRONE_EVENT_SELF_DESTRUCT_STARTED, …)`.

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

Matrix / buzzer / IR / relay hardware timing (see `enhancements.*` and module design docs). Drone gameplay rules stay in the baseline state machine (`Core/Src/main.c`).
