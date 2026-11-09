# Telemetry + EVT wire contract

Unified **mission UART** stream for Tera Term and VESPER. One message per line (`\r\n` or `\n`).

## Dual sinks (do not merge)

| Path | API | Transport | Audience |
|------|-----|-----------|----------|
| **Mission UART** | `Telemetry_WriteLine` / `Telemetry_Printf` / `Telemetry_SendSensors` / `Telemetry_Evt*` | USART1 (PB6/PB7) → ST-LINK VCP @ **115200 8N1** | Tera Term + VESPER |
| **Mission Wi-Fi** | same lines (fed by `Telemetry_WriteLine`) | On-board ISM43362 → TCP server **port 5028** → `node Command/serve.mjs` bridge → SSE → VESPER | VESPER ("Live board (Wi-Fi)") |
| **Debug** | plain `printf` | Semihosting → CubeIDE Console (only while a debugger is attached) | Developer only |

On the board, [`App/Src/telemetry_uart.c`](../App/Src/telemetry_uart.c) provides the strong `Telemetry_WriteLine` (blocking `HAL_UART_Transmit`, `<line>\r\n`); `main.c` calls `Telemetry_UartInit()` right after `HAL_Init()`. The weak `printf` fallback in `telemetry_evt.c` is only used by host tests. Never send mission/EVT text through raw `printf`.

Regression: `node scripts/test_vesper_ingest.mjs` builds the real `telemetry_evt.c` on the host, emits a full mission in `main.c` order, and feeds the exact CRLF byte stream to VESPER's parser in random chunks.

### Wi-Fi transport

- **Same bytes as UART.** `Telemetry_WriteLine` queues each `<line>\r\n` for the Wi-Fi client too (`App/Src/telemetry_wifi.c`). Lines are only queued while a client is connected; older lines are stale.
- **Board = TCP server** on port 5028 (one client). It announces itself on UART: `Wi-Fi: connected to <ssid>, IP a.b.c.d, telemetry TCP port 5028`.
- **Snapshot on connect.** When a client connects, `main.c` resends `EVT MODE=…`, `EVT CHARGE=n/50` and, in The Last of EE2028, `EVT COUNTDOWN`, so a late-joining VESPER shows the current state.
- **Bridge.** Browsers cannot open TCP, so `Command/bridge/wifi_bridge.mjs` (inside `serve.mjs`) serves `GET /wifi/stream?host=<ip>` (one SSE `data:` per line, `event: status` for connecting / connected / closed / error, retry every 2 s) and `GET /wifi/scan` (finds hosts with 5028 open on the laptop's /24 networks). It forwards raw lines only; parsing stays in `parser.js`.
- **Non-blocking firmware.** `ism43362.c` (SPI AT engine) and `wifi_link.c` (join → IP → server → listen → batched `S3` sends every ≥150 ms) advance one bounded step per main-loop pass: no `HAL_Delay`, no spin-waits.
- **Enable** by creating the git-ignored `App/Inc/wifi_config.h` from `wifi_config.example.h`. Without it, Wi-Fi is off and UART is unchanged.

Regression: `node scripts/test_wifi_host.mjs` (fake ISM43362 at the SPI byte level: join, server, exact stream, batching, reconnect, wrong password, dead module) and `node scripts/test_wifi_bridge.mjs` (fake TCP board → real `serve.mjs` → SSE → parser, byte-identical lines, reconnect, scan).

Firmware helpers: [`App/Inc/telemetry_evt.h`](../App/Inc/telemetry_evt.h), [`App/Src/telemetry_evt.c`](../App/Src/telemetry_evt.c).

---

## 1) Human-readable sensor line (PR #3 layout — keep)

**Precision:** every floating-point field on the `Telemetry ->` sensor line (and threshold / heading floats from `main.c`) prints at **exactly two decimal places** (`%.2f` / `.toFixed(2)`). Do not regress to `%.1f`.

**Magnetometer:** print **magnitude only** (`sqrt(x²+y²+z²)` mgauss). Do not emit the XYZ vector on this line.

```text
Telemetry -> T:26.40 °C, P:1012.50 hPa, H:55.10 % | Accel:[0.00, 0.10, 9.80] m/s^2 | Gyro:[0.00, 0.00, 0.20] dps | Magneto:55.22 mgauss
```

| Field | Unit | Axes |
|-------|------|------|
| T | °C | scalar |
| P | hPa | scalar |
| H | % | scalar |
| Accel | m/s^2 | X, Y, Z |
| Gyro | dps | X, Y, Z |
| Magneto | mgauss | magnitude only (not XYZ) |

Threshold violation lines from PR #3 (e.g. `T:40.10 C, exceeds threshold…`) remain valid; VESPER keeps them in the raw log and threshold panel. When all readings recover, firmware may emit `EVT THRESHOLD_OK` so the dashboard clears a sticky warning.

---

## 2) Machine-readable `EVT` lines

> **Charge scale:** the official laser charge range is **0–50** (starts 50/50, +3 per recharge, firing needs ≥25, −5 per shot, cannon needs ≥10 and spends 10 per shot, so a giant (2 hits) costs 20). Legacy `n/10` logs are still accepted by the VESPER parser and rescaled ×5.

| Line | When | Notes |
|------|------|--------|
| `EVT MODE=STANDBY_MODE` | Enter Standby | |
| `EVT MODE=BATTLE_MODE` | Enter Battle | |
| `EVT MODE=THE_LAST_OF_2028` | Enter Last of 2028 | Preferred tag for new EVT (see §3) |
| `EVT CHARGE=n/50` | After a successful recharge | `n` is charge **after** the add |
| `EVT FIRE charge=n/50` | After a accepted shot | `n` is charge **after** the shot |
| `EVT COUNTDOWN remaining_ms=N` | During Last of 2028 | From firmware rescue deadline |
| `EVT SELF_DESTRUCT reason=…` | Self-destruct starts | Tokens: `timeout`, `rescue_limit`, `early` |
| `EVT TERMINATE` | Mission end | **Immediately before** `Cheerio!` |
| `EVT ATTACK` | Optional | Pairs with `Drone was attacked!` prose |
| `EVT RESCUE` | Optional | Pairs with successful rescue prose |
| `EVT THRESHOLD_OK` | Optional | Clear sticky threshold warning |
| `EVT RADAR az=… range_mm=… lock=1 pan=… laser=0\|1 n=…` | RD-03D lock, then every 250 ms while locked | Enhancement #5. `az`/`pan` in degrees (one decimal, + = right of boresight). See `docs/Radar_Aim_Design.md` |
| `EVT RADAR lost` | Locked track dropped (timeout, leaving Standby/Battle, or after a giant is destroyed) | Clears the lock in VESPER |
| `EVT CANNON charge=n/50` | Giant cannon fired: Battle + radar lock + charge ≥10, hold-to-fire (`main.c`) | Charge **after** the shot; separate from `EVT FIRE` (gun) |
| `EVT GIANT hit=1/2` | First cannon hit on the locked giant (adapter) | Giant HP = 2 |
| `EVT GIANT down` | Second hit: giant destroyed. Lock is dropped when the beam ends, so the same person re-locks as a new giant | |
| `EVT GIANT miss` | Cannon fired but the lock was lost first | |

Example stream (plan shape):

```text
Telemetry -> T:26.40 °C, P:1012.50 hPa, H:55.10 % | Accel:[0.00, 0.10, 9.80] m/s^2 | Gyro:[0.00, 0.00, 0.20] dps | Magneto:55.22 mgauss
EVT MODE=BATTLE_MODE
EVT CHARGE=30/50
EVT FIRE charge=25/50
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

Wired from the enhancements adapter when built with `EE_ENHANCEMENTS=1 EE_RADAR_AIM=1`: `EVT RADAR …` / `EVT RADAR lost` (`Radar_Update` in `enhancements.c`), `EVT GIANT …` (`Cannon_OnFired`). `EVT CANNON` comes from `main.c`, which owns the charge.

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
