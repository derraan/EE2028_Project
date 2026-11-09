/**
 * Offline regression: VESPER virtual board → shared parser.
 * Checks the browser board follows the frozen Core/Src/main.c rules and that
 * everything it prints parses the same way live UART would.
 * Run: node scripts/test_virtual_board.mjs
 */
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const load = (rel) => import(pathToFileURL(path.join(root, rel)).href);
const { createParser } = await load('Command/src/parser.js');
const { createVirtualBoard, SIM } = await load('Command/src/sim.js');

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function rig() {
  const p = createParser();
  p.markLink('sim');
  const lines = [];
  const board = createVirtualBoard((line) => {
    lines.push(line);
    p.pushLine(line);
  });
  let t = 0;
  board.reset(t);
  board.tick(t);
  const api = {
    p,
    board,
    lines,
    get s() {
      return p.state;
    },
    advance(ms, step = 20) {
      const end = t + ms;
      while (t < end) {
        t = Math.min(end, t + step);
        board.tick(t);
      }
    },
    tap() {
      board.pressDown(t);
      api.advance(60);
      board.pressUp(t);
    },
    single() {
      api.tap();
      api.advance(SIM.DOUBLE_PRESS_MS + 40);
    },
    double() {
      api.tap();
      api.advance(120);
      api.tap();
      api.advance(SIM.DOUBLE_PRESS_MS + 40);
    },
    hold(ms) {
      board.pressDown(t);
      api.advance(ms);
      board.pressUp(t);
      api.advance(40);
    },
  };
  return api;
}

// Boot + Standby telemetry
{
  const r = rig();
  assert(r.s.mode === 'STANDBY_MODE', `boot mode=${r.s.mode}`);
  r.advance(2100);
  assert(r.s.telemetry.T != null && r.s.telemetryAt != null, 'standby telemetry parsed');
  assert(r.s.telemetry.Accel[2] > 9, 'upright accel z positive');
  r.single();
  assert(r.s.mode === 'STANDBY_MODE' && r.s.charge === 50, 'single press ignored in Standby (charge stays 50/50)');
}

// Battle: 0..50 charge, hold-to-fire, immediate recharge, double press only when full
{
  const r = rig();
  assert(r.s.charge === 50, `boot charge is 50/50, got ${r.s.charge}`);
  r.double();
  assert(r.s.mode === 'BATTLE_MODE', `double → battle, got ${r.s.mode}`);
  r.single();
  assert(r.s.charge === 50, 'single press at 50/50 changes nothing');
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.fireSeq === 1 && r.s.charge === 45, `hold fires once → 45/50, got seq=${r.s.fireSeq} charge=${r.s.charge}`);
  assert(r.s.mode === 'BATTLE_MODE', 'hold is not a double press');
  // Keep holding: full-auto down to 20/50 (< 25 cannot fire)
  r.hold(SIM.HOLD_MS + SIM.FIRE_PERIOD_MS * 6);
  assert(r.s.charge === 20, `full-auto stops below 25, got ${r.s.charge}`);
  assert(r.lines.some((l) => /charge too low \(20\/50\).*at least 25/.test(l)), 'low-charge warning printed');
  const shots = r.s.fireSeq;
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.fireSeq === shots, 'cannot fire below 25/50');
  // Recharge: every short press acts at once (+3), armed from 25
  r.tap();
  assert(r.s.charge === 23 && !r.s.armed, `immediate +3 → 23, got ${r.s.charge}`);
  r.tap();
  assert(r.s.charge === 26 && r.s.armed, `26 armed, got ${r.s.charge}`);
  assert(r.lines.some((l) => /Laser ready to fire \(26\/50\)/.test(l)), 'ready-to-fire hint printed');
  assert(r.s.fireSeq === shots, 'reaching 25 must not auto-fire');
  while (r.s.charge < 50) r.tap();
  assert(r.s.charge === 50, `charges up to the cap, got ${r.s.charge}`);
  r.double();
  assert(r.s.mode === 'STANDBY_MODE', 'double at 50/50 → standby');
}

// Full-auto: holding long fires repeatedly while charge allows
{
  const r = rig();
  r.double();
  r.hold(SIM.HOLD_MS + SIM.FIRE_PERIOD_MS * 3 + 100);
  assert(r.s.fireSeq >= 3 && r.s.charge === 50 - 5 * r.s.fireSeq, `full-auto fires repeatedly, seq=${r.s.fireSeq} charge=${r.s.charge}`);
}

// T/A/G disturbance in Standby → Battle
{
  const r = rig();
  r.board.disturb();
  r.advance(40);
  assert(r.s.mode === 'BATTLE_MODE' && r.s.tripSeq === 1, `trip → battle, got ${r.s.mode}`);
}

// Overturn → The Last, rescue when upright
{
  const r = rig();
  r.double();
  r.board.setUpsideDown(true);
  r.advance(40);
  assert(r.s.mode === 'THE_LAST_OF_2028', `upside-down → last, got ${r.s.mode}`);
  assert(r.s.countdownMs === SIM.RESCUE_TIMEOUT_MS, `countdown starts at 10 s, got ${r.s.countdownMs}`);
  r.advance(2100);
  assert(r.s.attackSeq === 2, `attack each second, got ${r.s.attackSeq}`);
  r.double();
  assert(r.s.mode === 'THE_LAST_OF_2028' && r.s.rescueAttempts === 1, 'rescue fails while upside down');
  r.board.setUpsideDown(false);
  r.double();
  assert(r.s.mode === 'BATTLE_MODE' && r.s.rescueSeq === 1, `upright rescue → battle, got ${r.s.mode}`);
  assert(r.s.countdownMs === null && r.s.rescueAttempts === 0, 'rescue clears countdown + attempts');
}

// Timeout self-destruct
{
  const r = rig();
  r.board.setUpsideDown(true);
  r.advance(SIM.RESCUE_TIMEOUT_MS + SIM.TERMINATE_DELAY_MS + 200);
  assert(r.s.missionEnded && r.s.lastSelfDestructReason === 'timeout', `timeout SD, got ${r.s.lastSelfDestructReason}`);
  assert(r.lines.at(-1) === 'Cheerio!' && r.lines.at(-2) === 'EVT TERMINATE', 'TERMINATE immediately before Cheerio!');
  const n = r.lines.length;
  r.double();
  r.advance(2000);
  assert(r.lines.length === n, 'halted board prints nothing');
}

// Five failed rescues self-destruct
{
  const r = rig();
  r.board.setUpsideDown(true);
  r.advance(40);
  for (let i = 0; i < 5; i++) r.double();
  r.advance(SIM.TERMINATE_DELAY_MS + 200);
  assert(r.s.missionEnded && r.s.lastSelfDestructReason === 'rescue_limit', `rescue-limit SD, got ${r.s.lastSelfDestructReason}`);
}

// ---------------------------------------------------------------- radar (enhancement #5)
const { RADAR, AIM, formatRadarLine } = await load('Command/src/radar_mirror.js');
const radarLines = (r) => r.lines.filter((l) => l.startsWith('EVT RADAR'));

// Wire format matches Telemetry_EvtRadar() (fixed-point tenths, no float printf)
assert(
  formatRadarLine({ az: -12.44, range: 1830.6, lock: true, pan: -0.04, laser: false, n: 2 }) ===
    'EVT RADAR az=-12.4 range_mm=1830 lock=1 pan=0.0 laser=0 n=2',
  'radar line format'
);
assert(formatRadarLine({ az: 0.05, range: 500, lock: 1, pan: -7.25, laser: 1, n: 1 }).includes('az=0.1 ') , 'round half away from zero');

// Standby: confirm → lock, servo tracks, laser stays off
{
  const r = rig();
  r.board.setRadarContacts([{ x: 500, y: 2000 }]);
  r.advance(RADAR.FRAME_PERIOD_MS * (RADAR.CONFIRM_FRAMES - 1) - 20);
  assert(!r.s.radar.lock, 'no lock before confirm frames');
  r.advance(200);
  assert(r.s.radar.lock && r.s.radarLockSeq === 1, `standby lock, got ${JSON.stringify(r.s.radar)}`);
  assert(Math.abs(r.s.radar.az - 14.0) < 0.3, `az≈14°, got ${r.s.radar.az}`);
  assert(Math.abs(r.s.radar.rangeMm - 2061) <= 2, `range≈2061, got ${r.s.radar.rangeMm}`);
  r.advance(1000);
  assert(!r.s.radar.laser, 'standby never lights the laser');
  assert(Math.abs(r.s.radar.pan - 14.0) < 0.5, `servo tracks in standby, pan=${r.s.radar.pan}`);
  const n = radarLines(r).length;
  r.advance(1000);
  const per = radarLines(r).length - n;
  assert(per >= 3 && per <= 5, `EVT RADAR ~4 Hz while locked, got ${per}/s`);
  assert(r.s.mode === 'STANDBY_MODE' && r.s.fireSeq === 0, 'radar has no mode / fire authority');
}

// Battle: a locked giant is tracked but the laser (= giant cannon) stays dark;
// gun shots never light it; lost → EVT RADAR lost
{
  const r = rig();
  r.double();
  r.hold(SIM.HOLD_MS + 100); // 50 → 45: no longer a full charge
  r.board.setRadarContacts([{ x: -1200, y: 2100 }]); // ~-29.7°
  r.advance(400);
  assert(r.s.radar.lock, 'battle lock');
  r.advance(500);
  assert(Math.abs(r.s.radar.pan - r.s.radar.az) < AIM.ALIGNED_TOL_DEG, `servo on target, ${JSON.stringify(r.s.radar)}`);
  assert(!r.s.radar.laser, 'no aim dot: laser only lights for a cannon beam');
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.fireSeq === 1 && r.s.cannonSeq === 1 && r.s.charge === 35, `locked giant + ≥10 charge: hold is the cannon (−10), got cannon=${r.s.cannonSeq} fire=${r.s.fireSeq} charge=${r.s.charge}`);
  r.board.setRadarContacts([]);
  r.advance(RADAR.LOST_TIMEOUT_MS + 100);
  assert(!r.s.radar.lock && !r.s.radar.laser && r.lines.includes('EVT RADAR lost'), 'lost clears lock + laser');
  assert(r.s.radarLockSeq === 1, 'one lock so far');
}

// Giant cannon: 10 charge per shot (minimum 10), 2 hits destroy the giant (20 charge),
// the lock drops and the same person re-locks as a fresh giant
{
  const { GIANT } = await load('Command/src/radar_mirror.js');
  const r = rig();
  r.double();
  r.board.setRadarContacts([{ x: 0, y: 2500 }]);
  r.advance(500);
  assert(r.s.radar.lock, 'giant locked');
  assert(r.s.charge === 50, `full charge with a giant locked, got ${r.s.charge}`);

  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.cannonSeq === 1 && r.s.fireSeq === 0, `hold + lock fires the cannon, not the gun (cannon=${r.s.cannonSeq} fire=${r.s.fireSeq})`);
  assert(r.s.charge === 40, `one cannon shot costs 10, got ${r.s.charge}`);
  assert(r.lines.includes('EVT CANNON charge=40/50') && r.lines.includes('EVT GIANT hit=1/2'), 'cannon + first hit reported');
  assert(r.s.giant.hits === 1 && r.s.giantKills === 0, 'giant survives the first hit');
  r.advance(RADAR.EVT_PERIOD_MS + 20);
  assert(r.s.radar.laser, 'cannon beam lights the laser on the giant');
  r.advance(GIANT.BEAM_MS);
  assert(!r.board.snapshot.radar.laser, 'beam ends after GIANT.BEAM_MS');

  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.charge === 30, `second shot costs another 10, got ${r.s.charge}`);
  assert(r.lines.includes('EVT GIANT down') && r.s.giantKills === 1 && r.s.giant.hits === 0, 'second hit destroys the giant');
  const locks = r.s.radarLockSeq;
  r.advance(GIANT.BEAM_MS + 600);
  assert(r.lines.filter((l) => l === 'EVT RADAR lost').length >= 1, 'lock dropped after the giant falls');
  assert(r.s.radarLockSeq === locks + 1, `same person re-locks as a new giant, locks ${locks} → ${r.s.radarLockSeq}`);
}

// Cannon minimum: below 10 charge the giant cannot be fired (and below 25 the gun is refused too)
{
  const r = rig();
  r.double();
  r.hold(SIM.HOLD_MS + SIM.FIRE_PERIOD_MS * 8); // no lock: gun drains 50 → 20
  assert(r.s.charge === 20, `gun drained to 20, got ${r.s.charge}`);
  r.board.setRadarContacts([{ x: 0, y: 2500 }]);
  r.advance(500);
  r.hold(SIM.HOLD_MS + SIM.FIRE_PERIOD_MS * 3); // cannon: 20 → 10 → 0, then refused
  assert(r.s.cannonSeq === 2 && r.s.charge === 0, `cannon fires at 20 and 10, stops at 0, got cannon=${r.s.cannonSeq} charge=${r.s.charge}`);
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.cannonSeq === 2, 'no cannon shot with 0 charge');
  r.tap();
  r.tap();
  r.tap();
  assert(r.s.charge === 9 && !r.lines.some((l) => /GIANT CANNON READY/.test(l)), `9 charge: not ready yet, got ${r.s.charge}`);
  r.tap();
  assert(r.s.charge === 12 && r.lines.some((l) => /GIANT CANNON READY \(12\/50\)/.test(l)), 'cannon-ready prompt at ≥10 with a lock');
}

// No giant: the cannon is unavailable; the hold is the plain gun even at full charge.
{
  const r = rig();
  r.double();
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.cannonSeq === 0 && r.s.fireSeq === 1 && r.s.charge === 45, `no giant: hold fires the gun, got cannon=${r.s.cannonSeq} charge=${r.s.charge}`);
}

// Out of FOV / too close / too far are ignored; a nearer zombie steals the lock
{
  const r = rig();
  r.board.setRadarContacts([{ x: 3000, y: 1000 }, { x: 0, y: 250 }, { x: 0, y: 7000 }]);
  r.advance(1000);
  assert(!r.s.radar.lock && radarLines(r).length === 0, 'gated contacts produce no EVT RADAR');
  r.board.setRadarContacts([{ x: 0, y: 4000 }]);
  r.advance(500);
  assert(r.s.radar.lock && Math.abs(r.s.radar.az) < 1, 'far zombie locked');
  r.board.setRadarContacts([{ x: 0, y: 4000 }, { x: 1500, y: 1500 }]);
  r.advance(600);
  assert(r.s.radarLockSeq === 2 && r.s.radar.az > 40, `nearer zombie re-locked az=${r.s.radar.az}`);
}

// Overturn drops the track and parks the servo; nothing after Cheerio!
{
  const r = rig();
  r.double();
  r.board.setRadarContacts([{ x: 0, y: 2000 }]);
  r.advance(500);
  assert(r.s.radar.lock, 'locked before overturn');
  r.board.setUpsideDown(true);
  r.advance(60);
  assert(r.s.mode === 'THE_LAST_OF_2028' && !r.s.radar.lock, 'Last of EE2028 clears radar');
  assert(r.board.snapshot.radar.aim === 'SAFE' && !r.board.snapshot.radar.laser, 'servo safe, laser off');
  r.advance(SIM.RESCUE_TIMEOUT_MS + SIM.TERMINATE_DELAY_MS + 400);
  assert(r.lines.at(-1) === 'Cheerio!', 'radar prints nothing after Cheerio!');
}

console.log('OK — virtual board follows firmware rules through the shared parser');
