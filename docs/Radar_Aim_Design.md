# Radar zombie detection + laser aim (enhancement #5)

An Ai-Thinker **RD-03D** 24 GHz mmWave radar detects people ("zombies" in the VESPER story) and localises them in **azimuth**. A single **pan servo** turns a low-power **laser pointer** onto the nearest one's body.

**Giant cannon (current behaviour):** the locked person is a *giant* and the laser is the *giant cannon*. The servo tracks the lock in Standby and Battle, but the laser stays **off** until a cannon shot (Battle + lock + hold-to-fire with ≥10 charge, 10 per shot). It then lights steadily for `EE_CANNON_BEAM_MS` (1 s) while aimed. Gun shots (`EVT FIRE`) no longer strobe it. The adapter implements this by opening `allow_laser` only during the beam window; `laser_aim.c` is unchanged. Giant HP, `EVT GIANT` and the recharge rule: `docs/Assignment2_Enhancements.md` §5a.

The radar is **detection + aiming only**. It never changes the drone mode, adds or spends charge, or fires. Hold-to-fire, the +3 charge rule, rescue, and the 10 s / 5-rescue self-destruct all stay in `Core/Src/main.c`. This is the same authority rule IR follows.

```
RD-03D ──UART4 256000──► rd03d.c ──frames──► radar_track.c ──target──► laser_aim.c ──► TIM3_CH1 servo
 (fixed)                 (parser)            (nearest threat,           (parallax, slew,   └► D7 laser
                                              confirm, timeout)          gate, strobe)
                                                     │
                       enhancements.c Radar_Update ──┴─► EVT RADAR … ──► VESPER (parser → 3D world)
                                                     └─► matrix azimuth column 0–7
```

## Hardware

| Part | Board pin | Notes |
|------|-----------|-------|
| RD-03D TX → | **D0 / PA1** (UART4_RX, AF8) | 3.3 V logic. Radar VCC is **5 V** |
| RD-03D RX ← | **D1 / PA0** (UART4_TX, AF8) | Sends the multi-target command at boot |
| Pan servo signal | **D5 / PB4** (TIM3_CH1, AF2) | 50 Hz, 1 µs resolution. Power the servo from its **own 5 V** supply with a common GND, not the board's 3V3 |
| Laser module | **D7 / PA4** → NPN/MOSFET low-side switch | Active-HIGH by default (`EE_LASER_ACTIVE_HIGH`). Configured as a pulled-down output that is driven LOW **before** the servo or radar start |

D2/D3/D4 stay with the buzzer, IR, and CAP_EXPLODE. Check D0/D1/D5/D7 against your Cube `main.h` labels. If they clash, change the pins at the top of the radar block in `App/Src/enhancements_hw.c`.

### Mounting (azimuth only)

- The radar is **fixed**, with its boresight along the drone's forward axis. The laser sits on the servo horn. Measure the laser pivot's position relative to the radar centre and set `EE_LASER_OFFSET_X_MM` (right +) and `EE_LASER_OFFSET_Y_MM` (forward +). The firmware then aims at `atan2(x − offx, y − offy)`. A 60 mm lever arm is worth 3.4° at 1 m.
- The laser beam is **horizontal at chest height** and is never tilted up. The RD-03D gives no elevation, so the mount height is what keeps the dot on the torso.

### Laser safety

Use a **Class 1 / ≤1 mW** pointer module only, and follow lab eye-safety rules. The firmware lights it only when **all** of these are true:

- the mode is BATTLE,
- the target is LOCKED (3 confirmed frames),
- the servo is within 3° of the goal,
- the goal is inside the pan limits.

The laser goes off immediately when the target is lost, on The Last of EE2028, on self-destruct, and on `Enhancements_Shutdown()`. The FIRE strobe obeys the same gate, so it never flashes blind.

## RD-03D protocol (as implemented in `App/Src/rd03d.c`)

- **256000 8N1**. The module reports a 30-byte frame at about 10 Hz: `AA FF 03 00 | 3 × 8-byte target | 55 CC`.
- Each target is `x LE16 | y LE16 | speed LE16 | resolution LE16`. x, y, and speed are **sign-magnitude with bit 15 = positive** (`0E 03` → −782 mm, `B1 86` → +1713 mm). Units are mm and cm/s. An all-zero slot is empty, and speeds of ±248/±256 are placeholder slots.
- Command frames are `FD FC FB FA | len LE16 | cmd LE16 | 04 03 02 01`. Single-target is `0x0080` and multi-target is `0x0090`. Boot sends **multi-target**, so a nearer zombie can take over the lock.
- Azimuth is `atan2(x, y)`: 0° is boresight and + is toward +x. If walking to your right gives a negative azimuth, build with `-DEE_RADAR_MIRROR_X=-1`.

Reference: ESPHome's `rd03d` component (frame/sign/sentinel handling) and the Ai-Thinker RD-03D docs.

## Tracker (`radar_track.c`)

| Rule | Default |
|------|---------|
| Range gate | 0.4–6.0 m (`EE_RADAR_MAX_RANGE_MM`) |
| FOV gate | ±60° (`EE_RADAR_FOV_HALF_DEG`) |
| Pick | Nearest valid return |
| Association gate | 0.8 m around the predicted position (alpha-beta, α = 0.5) |
| Steal lock | A new return ≥ 0.6 m closer than the current one drops the lock and re-confirms |
| Confirm | 3 consecutive associated frames → **LOCK** |
| Lost | 700 ms without a hit → **LOST** (a locked track coasts through shorter drop-outs) |
| Matrix | ±60° mapped to columns 0–7 while locked (lowest display priority, unchanged) |

## Aim (`laser_aim.c`)

| Parameter | Default | Override |
|-----------|---------|----------|
| Centre pulse | 1500 µs | `EE_SERVO_CENTER_US` (trim so the dot hits boresight) |
| Scale | 11.1 µs/° (2000 µs over 180°), clamped 600–2400 µs | — |
| Direction | +1 | `EE_SERVO_DIRECTION=-1` if +azimuth swings the horn left |
| Pan limits | ±70° (goal clamped, laser off when out of reach) | `EE_SERVO_PAN_LIMIT_DEG` |
| Slew limit | 180 °/s | — |
| Aligned tolerance | 3° | — |
| After LOST | Hold angle 1.2 s, then park at 0° | — |
| FIRE strobe | 300 ms, 40 ms on/off | — |

Mode gates (`Radar_Update` in `enhancements.c`):

| Mode | Tracks + servo follows | Laser may light | EVT RADAR |
|------|------------------------|-----------------|-----------|
| STANDBY | yes | no | yes |
| BATTLE | yes | yes (gate above) | yes |
| THE_LAST_OF_2028 | no: track dropped, servo neutral | no | `lost` once |
| Self-destruct / dead | no (`LaserAim_Safe`) | no | — |

## Telemetry

```text
EVT RADAR az=-12.4 range_mm=1830 lock=1 pan=-11.9 laser=1 n=2
EVT RADAR lost
```

The firmware emits a line on lock, then every 250 ms while locked (`EE_RADAR_EVT_PERIOD_MS`), plus `lost` when the track drops. Angles are fixed-point tenths, so no float `printf` is needed. `n` is the number of gated contacts in the last frame. A lock in Battle also plays the IR threat chirp.

## Build

1. Add the new sources to the Cube build: `App/Src/rd03d.c`, `App/Src/radar_track.c`, `App/Src/laser_aim.c`. `enhancements*.c` and `telemetry_evt.c` already exist.
2. Define `EE_ENHANCEMENTS=1` **and** `EE_RADAR_AIM=1`. HAL needs `HAL_UART_MODULE_ENABLED` and `HAL_TIM_MODULE_ENABLED`, which the default L4 `hal_conf` has.
3. `Enhancements_HW_SetupRadarAim()` configures UART4, TIM3, and the GPIO itself, because the lab project uses BSP init and has no `MX_*` handles. It defines `UART4_IRQHandler`. If CubeMX also generates one, build with `-DEE_RADAR_OWN_IRQ=0` and call `Enhancements_HW_RadarIrq()` from yours.
4. Clock note: the project runs on the 4 MHz MSI reset clock. UART4 then uses 8× oversampling (258 065 baud, +0.8 %), and the RX ISR has about 150 cycles per byte. If frames drop, check `Enhancements_HW_RadarStats()`. Raising SYSCLK fixes it.

## Bring-up checklist

1. **Laser off at boot**: check D7 LOW, both with and without the radar connected.
2. **Frames**: the Enhanced build prints `Radar link: rx=… B, frames ok=… bad=…, dropped=…, uart_err=…` every 5 s on the telemetry COM port (VESPER log / Tera Term). Healthy: `rx` grows ~1500 B and `ok` ~50 per line, `bad`/`dropped`/`uart_err` stay near 0. `rx=0` → radar TX not reaching D0 (wiring, 5 V power, GND). `rx` grows but `ok=0` → wrong module or baud (an RD-03E sends `AA AA …` frames). UART4 runs with its 8-byte RX FIFO enabled, and the multi-target command is resent at +1 s and +3 s in case the radar was still booting.
3. **Sign**: stand 2 m ahead and step right. `EVT RADAR az` should be > 0. Otherwise set `EE_RADAR_MIRROR_X=-1`.
4. **Servo direction and trim**: with no target, adjust `EE_SERVO_CENTER_US` until the laser points along radar boresight. Flip `EE_SERVO_DIRECTION` if it tracks the wrong way.
5. **Parallax**: measure the laser offset, then check at 1 m and 4 m that the dot stays on the chest.
6. **Authority regression**: rerun the mandatory checklist. The radar must not change modes, charge, or fire.

## Tests

| Command | Covers |
|---------|--------|
| `node scripts/test_radar_aim_host.mjs` | C parser (datasheet bytes, resync, bad footer, sentinels, command bytes), tracker (confirm, follow, coast/lost, gates, lock steal, mirror), and aim (slew, parallax, clamp, laser gate, strobe, hold/park, safe). Uses host `gcc` |
| `node scripts/test_virtual_board.mjs` | JS mirror (`Command/src/radar_mirror.js`) through the shared parser: Standby tracks with the laser off, Battle lights it, FIRE leaves hold-to-fire unchanged, lost, gates, lock steal, Last of EE2028 safe state, nothing after `Cheerio!` |
| `node scripts/test_uart_parser.mjs` | `EVT RADAR` parsing, lock sequence, `lost`, mode clears |

## VESPER

- **Live UART / replay**: on `EVT RADAR lock=1`, the world places (or claws up) a zombie at the reported azimuth and range. 1 m maps to 4 world units, with boresight along the field (+z). The turret aims at its torso, and the red pointer beam shows only while `laser=1`. A FIRE shot hits that zombie.
- **Simulation**: the world's zombies stand in front of the *virtual* RD-03D (`world.radarContacts()` → `board.setRadarContacts()`). `radar_mirror.js` prints the same `EVT RADAR` lines the firmware would, so the lock still only shows up through the parser.
- **HUD**: the radar card shows the ±60° sensor cone, a bearing line, and a reticle. The tag reads `RD-03D LOCK`, and the status line shows azimuth, range, and laser state.

Keep `radar_mirror.js` in sync with `radar_track.c`, `laser_aim.c`, and `Radar_Update()`, the same way `sim.js` mirrors `main.c`.
