# Command Center Dashboard Design

Web mission-control UI for EE2028 Assignment 2 enhancements **#4** (UART Command Center) and stretch **#6** (Wi‑Fi / BLE).

**Status:** VESPER is now *The Last Signal*, a Three.js space-zombie game (blood moon over NUS E4-03-07, spore meteors, the infected, VESPER-07 drone) with a full game HUD, driven by one parser. See [Command/README.md](../Command/README.md) for the event → world mapping. Sources: in-browser **virtual board** (`sim.js`, mirrors `main.c`), fixture replay, pasted UART, Web Serial — all **implemented** under `Command/src/`. Live board stream waits on Cube USART retarget (same parser). Optional Node bridge remains a stub — Web Serial is the preferred live path.

Sources:

1. [docs/Assignment2_Enhancements.md](Assignment2_Enhancements.md) — enhancement #4 / #6
2. [docs/Darren's_Tasks.md](Darren's_Tasks.md) — Task 8
3. As-built UI: [Command/index.html](../Command/index.html) + [Command/src/](../Command/src/) + [Command/fixtures/](../Command/fixtures/)
4. Wire contract: [Telemetry_EVT_Contract.md](Telemetry_EVT_Contract.md)
5. Quick start: [Command/README.md](../Command/README.md)
6. Concept microsite (art direction): [vesper-concept.html](vesper-concept.html)

The 8×8 matrix remains the **on-drone HUD** ([LED_Matrix_Design.md](LED_Matrix_Design.md)). This document is the **PC browser dashboard**.

---

## 1) Goal

Give markers a clear live view of:

- Drone mode (`STANDBY` / `BATTLE` / `THE_LAST_OF_2028`)
- Six-sensor telemetry with units
- Threshold status
- Laser charge / armed / fire
- IR threat and optional radar azimuth
- Event log through rescue or terminal shutdown

Mandatory UART telemetry must keep working. Wireless is an add-on, never a UART replacement.

---

## 2) Delivery shape

| Decision | Choice |
|----------|--------|
| UI type | **Web dashboard** (browser) |
| Not | Standalone Electron / desktop app |
| Design input | Importable HTML mockup |
| **Deploy target** | **Localhost on the lab/demo PC** |
| Live serial | Prefer **Chrome Web Serial** to ST-LINK VCP; optional Node/Python bridge |
| Debug fallback | Tera Term / raw serial lines (close it before Web Serial opens the same COM port) |

### Should it be on localhost?

**Yes.** For this assignment, the Command Center should run as a **local web app on the PC connected to the board**:

| Option | Verdict | Why |
|--------|---------|-----|
| `http://localhost:…` static site | **Recommended** | Works offline in the lab, no cloud account, secure enough for Web Serial, easy demo |
| `file://` open HTML directly | Avoid | Breaks ES modules / some browser APIs; flaky paths |
| Public cloud host (Vercel, GitHub Pages, etc.) | Not for live UART | Page is remote; serial is on the lab PC. Cloud is only useful for a **static mock** showcase |
| STM32 serves the HTML over Wi‑Fi | Stretch only | Cool, but UART is still mandatory; more firmware risk |
| Packaged desktop app | Out of scope | Extra packaging with no grading benefit |

**Demo flow:** board USB → ST-LINK Virtual COM → browser on **that same PC** at `http://localhost:3000` (or similar).

### Recommended implementation path

Three layers share one JS state model (as-built):

1. **UI** — `Command/index.html` + `bindings.js` (HUD) + `src/world/` (Three.js world, radar, audio) served on localhost  
2. **Parser / state** — `parser.js` / `state.js` turn UART/`EVT` lines into `{ mode, telemetry, charge, events… }`  
3. **Transport**
   - **Phase 0 (done):** virtual board `sim.js` prints firmware-identical UART lines into the same parser  
   - **Phase A (done):** fixture replay via `replay.js` + `Command/fixtures/*.log`, or pasted logs  
   - **Phase B (UI done; board pending):** **Web Serial API** in Chrome/Edge (`serial.js`)  
   - **Phase C (optional stub):** `Command/bridge/` if Web Serial is blocked, or for Wi‑Fi/BLE fan-in  

Parser also accepts legacy mode aliases (`THE_LAST_OF_EE2028`, `LAST_OF_2028`, `TEST_OF_2028`) and maps them to canonical `THE_LAST_OF_2028`.

```text
                    ┌─────────────────────────────┐
   sim.js / *.log → │  Command/ (localhost:3000)  │
                    │  parse → state → HUD+scene  │
   ST-LINK COM  ──→ │  Web Serial (preferred)     │
   (or bridge WS)   └─────────────────────────────┘
```

**Why Web Serial first:** one process to start (`serve Command`), no Python dependency, same machine as the board.  
**Why keep a bridge option:** Firefox lacks Web Serial; a bridge also helps later if Wi‑Fi packets and UART need merging.

### Lab deploy checklist

1. Flash firmware; confirm Tera Term sees telemetry.  
2. **Close Tera Term** (only one app can own the COM port).  
3. From repo root: `node Command/serve.mjs` (or `npx --yes serve Command`)  
4. Open Chrome → `http://localhost:3000`  
5. Source **Live board (Web Serial)** → **Connect board** → choose ST-LINK VCP @ **115200 8N1**  
6. Dashboard updates; raw lines can still be logged in a debug panel if needed  

### Deploy stages

| Stage | What |
|-------|------|
| Design | ✅ Mockup in `Command/index.html` |
| Annotate | Add `data-cc-*` hooks on existing nodes |
| Mock wire | Fixture / replay feeder on localhost (no board) |
| Live wire | Web Serial (preferred) or localhost bridge → same bindings |
| Demo | Browser on the lab PC at `http://localhost:…` |

---

## 3) Repository layout

```text
Command/
  index.html          # VESPER game page
  styles/vesper.css   # dashboard styles
  serve.mjs           # zero-dependency static server
  vendor/three/       # vendored three.js r169 (MIT)
  src/
    main.js           # sources → parser → HUD + scene, frame loop
    bindings.js       # HUD DOM updates from parser state
    world/            # Three.js world: environment, drone, zombies, effects, radar, audio
    sim.js            # virtual board mirroring Core/Src/main.c
    parser.js         # UART / fixture line parser
    state.js          # shared mission state
    serial.js         # Web Serial transport
    replay.js         # fixture replay
  fixtures/           # recorded / synthetic mission logs
  bridge/README.md    # optional Node bridge stub (Web Serial preferred)
```

Serve the UI:

```bash
# from repo root (preferred)
node Command/serve.mjs
# or: npx --yes serve Command
```

Then open the printed local URL (typically `http://localhost:3000`).

---

## 4) Game page (as-built, `Command/index.html`)

| Area | Content |
|------|---------|
| Scene | Canvas world: drone, NUS E4-03-07 lab, ruined Singapore street, hostile contact; orbit (drag / arrows) or forward camera |
| Objective card | Story text derived from parser state (scout → defend → The Last of EE2028 → ending) |
| Overlays | Emergency countdown (interpolated between 1 Hz `EVT COUNTDOWN`), self-destruct, mission-record ending |
| Instruments | Mode + board LED pattern, laser charge bars + armed, signal source |
| Panels | Virtual-board controls, recorded missions, paste log, live board; sensors; raw UART transmission log |

Authority rule: scene effects (beam, contact cleared, lab lit, overlays) fire only from parsed events (`fireSeq`, `tripSeq`, mode/countdown). Fiction is the world, never the drone state.

> §4 *Mockup overview* (below, historical) through §6 describe the earlier "Expedition control" mockup and its `data-cc-*` hooks, superseded by the game page.

### Historical: mockup overview

| Item | Detail |
|------|--------|
| Title / brand | **VESPER** — Extraterrestrial Operations |
| Theme | Dark panel UI (`#090e11` / mint accent `#b9f587` / orange warning `#eeaa70`) |
| Shell | Header + left icon rail + main content |
| Tabs | `01 Navigation` · `02 Drone systems` · `03 Captain view` |
| Prototype script | Inline JS: tab switching, fake autopilot, diagnostics, toast |

Current behaviour is **fiction telemetry only**. Autopilot, map route, lidar cards and throttle demo are visual/prototype affordances until replaced or demoted by live EE2028 bindings.

### View map

| Tab | Purpose in mockup | EE2028 use |
|-----|-------------------|------------|
| Navigation | Sector map, corridor, ETA, uplink notice | Optional threat/radar story; decorative until stretch |
| Drone systems | Schematic, device cards, energy core, diagnostics, event stream | **Primary** telemetry / charge / events |
| Captain view | Pilot HUD, mode, thrust, advisory | **Primary** mode / THE_LAST_OF_2028 / alerts |

---

## 5) Existing DOM anchors

Useful ids / regions already in the mockup:

| Anchor | Role today | Planned EE2028 bind |
|--------|------------|---------------------|
| `.online` | “UPLINK ESTABLISHED” | Link: UART / Wi‑Fi / severed |
| `#clock` | UTC clock | Host clock or mission uptime |
| `#mode` | “Manual standby” | `STANDBY` / `BATTLE` / `THE_LAST_OF_2028` |
| `#flightState` | HUD flight state text | Same mode (or countdown overlay) |
| ENERGY CORE `%` + `.bar i` | Fake 87% quantum cell | Laser charge `n/50` + bar width |
| `#temp` | Core temperature | Sensor `T` (°C) |
| `#devices` | LIDAR / bio / optics cards | Rebuild for `P H A G M` (and optionally keep `T` here) |
| `#diagnostics` | Diagnostic prose | Threshold summary |
| `#events` | Event stream HTML | Mission event log |
| `#warningText` | Pilot advisory | Attack / IR / threshold alert |
| `#hazards`, notice panels | Ion field fiction | Optional IR / radar cue |
| `#droneMarker`, `#routePath` | Autopilot path | Optional radar azimuth story |
| `#toast` | Transient notices | Soft UI feedback only |

---

## 6) `data-cc-*` binding contract

Add attributes **without** discarding VESPER styling. Prefer hooks on existing nodes.

```html
<span class="online" data-cc-link>UPLINK ESTABLISHED</span>
<span id="clock" data-cc-uptime></span>

<h2 id="mode" data-cc-mode>STANDBY</h2>
<div id="flightState" data-cc-flight-state>STANDBY</div>

<strong data-cc-charge>0/50</strong>
<i data-cc-charge-bar style="width:0%"></i>
<span data-cc-armed>NO</span>

<span id="temp" data-cc-t>--</span>
<div id="devices" data-cc-sensors></div>
<p id="diagnostics" data-cc-thresholds>ALL OK</p>

<p id="events" data-cc-event-log></p>
<p id="warningText" data-cc-alert></p>

<span data-cc-ir>CLEAR</span>
<span data-cc-radar-az>--</span>
<span data-cc-countdown></span>
```

### Field → source

| Bind | Source |
|------|--------|
| `data-cc-link` | Bridge connection state |
| `data-cc-uptime` | Host timer |
| `data-cc-mode` / `data-cc-flight-state` | Mode enter lines / `EVT MODE=` |
| `data-cc-charge` / `data-cc-charge-bar` / `data-cc-armed` | `EVT CHARGE=` / fire rules (≥25/50 armed) |
| `data-cc-t` … sensors | Mandatory 1 Hz telemetry |
| `data-cc-thresholds` | Threshold violation lines or ALL OK |
| `data-cc-event-log` | Mode + `EVT` history (newest first) |
| `data-cc-alert` | Attack / IR / advisory |
| `data-cc-ir` / `data-cc-radar-az` | Enhancement adapters |
| `data-cc-countdown` | `EVT COUNTDOWN=` during THE_LAST_OF_2028 |

---

## 7) Visual / UX rules

- **Standby:** steady mint / calm header.
- **Battle:** emphasize weapons/charge; optional 1 Hz pulse on mode badge.
- **THE_LAST_OF_2028:** orange warning path; 2 Hz cue; countdown visible in captain HUD.
- **Threshold trip:** `#diagnostics` shows violation text; sensor cards highlight until all recover.
- **Terminate:** uplink → severed; freeze `#events`; stop accepting new live frames.
- Keep fiction map/autopilot only if it does not override or contradict live state.

---

## 8) UART / event message contract

Friend owns compulsory telemetry and threshold formats. Darren owns `EVT` tags and the web parser.

### Compulsory-style lines (must remain compatible)

```text
Entering STANDBY MODE
Entering BATTLE MODE

T: 26.40 C, P: 101.25 kPa, H: 55.10 %RH, A: 0.98 g, G: 2.10 dps, M: 42.00 uT

G:12.50 dps, exceeds threshold of 8.00 dps.
```

### Enhancement event tags

Canonical contract: [`Telemetry_EVT_Contract.md`](Telemetry_EVT_Contract.md). Implemented parser: `Command/src/parser.js`.

```text
EVT MODE=STANDBY_MODE | BATTLE_MODE | THE_LAST_OF_2028
EVT ATTACK
EVT CHARGE=30/50
EVT FIRE charge=25/50
EVT IR=1
EVT RADAR az=3
EVT COUNTDOWN remaining_ms=9000
EVT RESCUE
EVT SELF_DESTRUCT reason=timeout|rescue_limit|early
EVT THRESHOLD_OK
EVT TERMINATE
```

Parser rules:

- Buffer to newline; keep unrecognized text in the raw log.
- Tolerant whitespace / casing where practical; accept legacy `THE_LAST_OF_EE2028`.
- Cable disconnect = link loss; `EVT TERMINATE` = mission end (still read `Cheerio!`).
- Same parser for live UART and `Command/fixtures/*.log` replay.

---

## 9) Runtime architecture

**Preferred (localhost + Web Serial):**

```text
STM32 firmware
    │  USART1 @ 115200 8N1 → ST-LINK VCP
    ▼
Chrome on localhost:3000
    Web Serial read → parse → state → data-cc-* nodes
```

**Fallback / Wi‑Fi merge (optional bridge):**

```text
STM32 UART or Wi‑Fi
    ▼
Command/bridge/  (localhost)  parse → WebSocket/SSE
    ▼
Same Command/ page bindings
```

Offline path:

```text
Command/fixtures/*.log  →  mock feeder  →  same JS bindings
```

Suggested browser state object:

```js
{
  link: "uart" | "wifi" | "ble" | "severed",
  mode: "STANDBY" | "BATTLE" | "THE_LAST_OF_2028" | "TERMINATED",
  uptimeMs: 0,
  telemetry: { T, P, H, A, G, M, units: {...} },
  thresholds: { ok: true, lines: [] },
  charge: 0,          // 0..50
  armed: false,
  ir: false,
  radarAz: null,      // 0..7 or degrees
  countdown: null,    // 10..0
  events: []          // newest first
}
```

---

## 10) Implementation checklist

- [x] Place mockup at `Command/index.html`
- [x] Agree `EVT` tags with teammate (no breakage of compulsory UART) — see Telemetry_EVT_Contract.md
- [x] Add `data-cc-*` attributes to the nodes in §6
- [x] Extract live-binding JS into `Command/src/` (leave decorative prototype JS isolated)
- [x] Implement state model + fixture replay
- [x] Rebuild `#devices` for real sensors (or dedicated spans)
- [x] Wire energy core UI to charge / armed / fire
- [x] Wire `#events`, `#warningText`, countdown, terminate
- [ ] Implement UART bridge → WebSocket/SSE (optional; Web Serial preferred — stub in `Command/bridge/`)
- [x] Demo offline replay without the board
- [ ] Keep UART path when adding Wi‑Fi/BLE

---

## 11) Stretch: Wi‑Fi / BLE

- Reuse the same HTML and state model.
- Only the bridge transport changes.
- UART remains the grading / fallback Command Center link.

---

## 12) Non-goals (for this dashboard)

- Replacing compulsory Tera Term–visible UART output
- Controlling flight-critical mandatory behaviour from the browser
- Shipping a packaged desktop app
- Making Navigation-tab fiction telemetry appear to be real sensor truth

---

## 13) Definition of done

Task 8 source work is done when:

1. `Command/index.html` opens via a local static server. ✅
2. Fixture mode exercises Standby → Battle → charge/fire → THE_LAST_OF_2028 → rescue/terminate. ✅
3. Web Serial (preferred) or optional bridge updates the same bindings. ✅ UI; ⏳ board USART
4. Compulsory telemetry lines still make sense in a raw serial terminal. ✅ contract + fixtures
5. Docs in this file match the as-built bind hooks. ✅ (this revision)

**Demo-complete** additionally needs a flashed board with `Telemetry_WriteLine` on ST-LINK VCP and one captured `mission_uart_capture.log`.
