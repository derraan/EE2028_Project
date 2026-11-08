# VESPER — The Last Signal

A space-zombie survival game driven by the EE2028 drone. Year 6202: spore pods fall from orbit onto Singapore and the infected converge on **NUS E4-03-07**, humanity's last laboratory. One defence drone, VESPER-07, remains, and its controller is the STM32 in your hands.

The 3D world (meteors, zombies, kills, waves) is fiction. **Drone state is only what the firmware reports** over UART: mode, charge, fire, rescue and self-destruct all come from parsed lines.

## Run it

```bash
# from repo root (zero dependencies, works offline in the lab)
node Command/serve.mjs            # → http://localhost:3000
# or: npx --yes serve Command
```

ES modules need `http://`; opening `index.html` from disk will not work. Three.js r169 is vendored under `vendor/three/` (MIT), so no CDN or internet is required.

Pick a **signal source** in the control deck:

| Source | What drives the drone |
|--------|-----------------------|
| **Virtual board** (default) | `src/sim.js`, a browser copy of `Core/Src/main.c` that prints the firmware's exact UART lines. **PB** (or Space): tap = press, hold ≥ 0.5 s = hold-to-fire. **Flip** = accel Z sign. **Disturbance** = T/A/G trip. |
| **Recorded mission** | `fixtures/*.log` replayed line by line |
| **Paste UART log** | Any Tera Term capture |
| **Live board** | Chrome/Edge Web Serial @ 115200 8N1 → ST-LINK VCP. Disconnect = link loss, **not** `TERMINATE`. |
| **Live board (Wi-Fi)** | Board's ISM43362 TCP :5028 → `serve.mjs` `/wifi/stream` (SSE) → same parser. **Find board** scans the hotspot; board drop = link loss, bridge retries every 2 s. |

Every source goes through the **same** `src/parser.js`.

## How firmware events become the game

| Parsed from UART | In the world |
|------------------|--------------|
| `EVT MODE=STANDBY_MODE` | Drone patrols the pad, cyan running lights, steady LED |
| `EVT MODE=BATTLE_MODE` | Drone climbs, red searchlight, turret tracks the nearest infected, 1 Hz nav lights |
| `Critical T/A/G threshold violated` | A spore meteor strikes the field and a wave rises |
| `EVT FIRE charge=n/50` | Laser gun vaporises the priority infected (one per FIRE). Giants shrug it off |
| `EVT RADAR … lock=1` | A **giant** (10–20× a zombie) rises where the radar lock is; the camera swings to a side view |
| `EVT CANNON` | Thick red giant-cannon beam from the drone to the giant's chest |
| `EVT GIANT hit=1/2` / `down` | Giant staggers / topples and explodes; *Giants* counter increments |
| `EVT MODE=THE_LAST_OF_2028` | Drone crashes inverted, alarm sky, infected swarm it; emergency strip with interpolated countdown |
| `EVT ATTACK` | Sparks and impact on the downed drone |
| `EVT RESCUE` | Drone rights itself; shockwave clears nearby infected |
| `EVT SELF_DESTRUCT` / `EVT TERMINATE` | Self-destruct blast, mission record screen |
| `Telemetry -> …` | Scan pulse; telemetry card + sparklines |

Fiction never feeds back: zombies can't flip the drone or change its charge. In Battle the world also drops extra pods on a timer, so there is always something to shoot.

## Controls

- **Mouse drag / arrow keys**: orbit the camera · **wheel**: zoom · **Drone cam**: chase view
- **Sound**: synthesized with WebAudio (no assets). It's off until you click *Enter command* or the Sound chip.

## Tests

```bash
node scripts/test_uart_parser.mjs     # fixtures → parser
node scripts/test_virtual_board.mjs   # virtual board follows the frozen firmware rules
```

Dev URL flags: `?play` skips the intro, `?debug` exposes `window.vesper`, `?nohud` hides the dashboard, `?fixedq` disables adaptive resolution.

## Layout

```text
Command/
  index.html            # page shell: viewport, HUD cards, control deck, overlays
  serve.mjs             # zero-dependency static server
  styles/vesper.css     # dashboard look (concept: dark glass, "//" red marks)
  src/main.js           # sources → parser → world / HUD / radar / audio; frame loop
  src/parser.js         # one line parser for live UART, replay and virtual board
  src/state.js          # shared parser instance
  src/bindings.js       # HUD: parser state (+ fiction stats) → DOM
  src/sim.js            # virtual board mirroring Core/Src/main.c
  src/replay.js         # fixture feeder
  src/serial.js         # Web Serial transport
  src/world/world.js    # renderer, bloom, camera rig, event → spectacle director
  src/world/environment.js  # lab, skyline, blood moon, ringed planet, orbital ring, fog
  src/world/drone.js    # VESPER-07 model + mode poses
  src/world/zombies.js  # the infected: spawn, walk, swarm, die
  src/world/effects.js  # particles, lasers, meteors, shockwaves, flashes
  src/world/radar.js    # HUD radar (simulated contacts)
  src/world/audio.js    # WebAudio SFX
  vendor/three/         # three.js r169 + bloom post-processing (MIT)
  fixtures/*.log        # recorded / synthetic missions
  bridge/               # optional Node bridge stub (Web Serial preferred)
```

## Performance

Tuned for integrated GPUs: the procedural sky is baked once into a cube map, bloom runs at half resolution, there's no MSAA, there are few real lights and no backdrop blur over the canvas, and adaptive resolution (0.6–1.25×) holds the frame rate.

## Rules the game must keep

- The page never decides that the drone fired, was rescued or self-destructed. Effects are triggered by parsed events only.
- `sim.js` must stay in step with `Core/Src/main.c` (hold-to-fire, 0–50 charge: +3 per press, fire at ≥25, double press to Standby only at 50/50, 10 s / 5-rescue self-destruct). Change both together and extend `test_virtual_board.mjs`.
- Naming: enum `THE_LAST_OF_2028`; on-screen banner `THE LAST OF EE2028`.

## Contract

See [`docs/Telemetry_EVT_Contract.md`](../docs/Telemetry_EVT_Contract.md). Firmware emitters live in `App/Src/telemetry_evt.c`.
