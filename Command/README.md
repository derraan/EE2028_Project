# VESPER — The Last of EE2028

Browser game + mission dashboard for the EE2028 drone. Year 6202: the drone guards the last laboratory at NUS E4-03-07. The 3D world is fiction; **drone state is only what the firmware reports** over UART.

## Quick start (no board)

```bash
# from repo root — ES modules need a local server (file:// will not work)
npx --yes serve Command
```

Open the printed URL (typically `http://localhost:3000`) and pick a **signal source**:

| Source | What drives the drone |
|--------|-----------------------|
| **Virtual board** (default) | `src/sim.js` — a browser copy of `Core/Src/main.c` that prints the firmware's exact UART lines. **PB** button (or Space): tap = press, hold ≥0.5 s = hold-to-fire. **Zombie arrives** = T/A/G disturbance. **Flip upside down / upright** = accel Z. |
| **Recorded mission** | `fixtures/*.log` replayed line by line |
| **Paste UART log** | Any Tera Term capture pasted into the page |
| **Live board** | Chrome/Edge Web Serial @ 115200 8N1 → ST-LINK VCP. Disconnect = link loss, **not** `TERMINATE`. |

Every source goes through the **same** `src/parser.js`, so a mission looks identical whether it came from the board, a log, or the virtual board.

## Tests

```bash
node scripts/test_uart_parser.mjs     # fixtures → parser
node scripts/test_virtual_board.mjs   # virtual board follows the frozen firmware rules
```

## Layout

```text
Command/
  index.html          # VESPER page (scene, HUD, source panels)
  flight.css          # styles
  src/main.js         # wires sources → parser → HUD + scene; frame loop
  src/parser.js       # one line parser for live UART, replay and virtual board
  src/state.js        # shared parser instance
  src/bindings.js     # HUD: parser state → DOM (mode, LED, charge, countdown, sensors, log)
  src/scene.js        # canvas world renderer (pure function of a view object)
  src/sim.js          # virtual board — mirrors Core/Src/main.c, emits UART lines
  src/replay.js       # fixture feeder
  src/serial.js       # Web Serial transport
  fixtures/*.log      # recorded / synthetic missions
  bridge/             # optional Node bridge stub (Web Serial preferred)
```

## Rules the game must keep

- The page never decides that the drone fired, was rescued, or self-destructed. Effects (laser beam, contact cleared, emergency overlay, ending) are triggered by parsed events only.
- `sim.js` must stay in step with `Core/Src/main.c` (hold-to-fire, +3 charge only below 5/10, 10 s / 5-rescue self-destruct). Change both together and extend `test_virtual_board.mjs`.
- Naming: enum `THE_LAST_OF_2028`; on-screen banner `THE LAST OF EE2028`.

## Contract

See [`docs/Telemetry_EVT_Contract.md`](../docs/Telemetry_EVT_Contract.md). Firmware emitters live in `App/Src/telemetry_evt.c`.

## Capture a real mission later

See [`scripts/capture_uart_mission.md`](../scripts/capture_uart_mission.md). Save as `fixtures/mission_uart_capture.log` and replay it, or paste it into **Paste UART log**.
