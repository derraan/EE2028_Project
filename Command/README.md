# VESPER Command Center

Localhost dashboard for EE2028 UART telemetry + `EVT` lines.

## Quick start (fixture replay — no board)

```bash
# from repo root
npx --yes serve Command
```

Open the printed URL (typically `http://localhost:3000`). Use the top-right toolbar:

| Button | Effect |
|--------|--------|
| **Replay fixture** | Feeds `fixtures/mission_uart_sample.log` through the same parser as live UART |
| **Replay early SD** | `fixtures/mission_timeout_early.log` (rescue-limit self-destruct) |
| **Connect serial** | Chrome/Edge Web Serial @ 115200 8N1 → ST-LINK VCP |
| **Disconnect** | Link loss (`severed`) — **not** mission `TERMINATE` |
| **Reset state** | Clear dashboard state |

Offline parser check:

```bash
node scripts/test_uart_parser.mjs
```

## Layout

```text
Command/
  index.html          # VESPER UI + data-cc-* hooks
  src/parser.js       # shared line parser (live + replay)
  src/state.js        # state + subscribers
  src/bindings.js     # data-cc-* DOM updates
  src/replay.js       # fixture feeder
  src/serial.js       # Web Serial transport
  src/main.js         # toolbar + wire-up
  fixtures/*.log      # recorded / synthetic missions
  bridge/             # optional Node bridge stub (Web Serial preferred)
```

## Contract

See [`docs/Telemetry_EVT_Contract.md`](../docs/Telemetry_EVT_Contract.md). Firmware emitters live in `App/Src/telemetry_evt.c`.

## Capture a real mission later

See [`scripts/capture_uart_mission.md`](../scripts/capture_uart_mission.md). Save as `fixtures/mission_uart_capture.log` and replay the same way.
