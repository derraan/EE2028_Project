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
  assert(r.s.mode === 'STANDBY_MODE' && r.s.charge === 0, 'single press ignored in Standby');
}

// Battle: charge, hold-to-fire, no auto-fire
{
  const r = rig();
  r.double();
  assert(r.s.mode === 'BATTLE_MODE', `double → battle, got ${r.s.mode}`);
  r.single();
  assert(r.s.charge === 3 && !r.s.armed, `charge 3, got ${r.s.charge}`);
  r.single();
  assert(r.s.charge === 6 && r.s.armed, `charge 6 armed, got ${r.s.charge}`);
  assert(r.s.fireSeq === 0, 'reaching 5/10 must not auto-fire');
  r.single();
  assert(r.s.charge === 6, 'single press at ≥5 does not add charge');
  assert(r.lines.some((l) => /already sufficient/.test(l)), 'sufficient-charge hint printed');
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.fireSeq === 1 && r.s.charge === 1, `hold fires once → 1/10, got seq=${r.s.fireSeq} charge=${r.s.charge}`);
  assert(r.s.mode === 'BATTLE_MODE', 'hold is not a double press');
  r.hold(SIM.HOLD_MS + 100);
  assert(r.s.fireSeq === 1, 'cannot fire below 5/10');
  assert(r.lines.some((l) => /charge too low \(1\/10\)/.test(l)), 'low-charge warning printed');
  r.double();
  assert(r.s.mode === 'STANDBY_MODE', 'double → standby');
}

// Full-auto: holding long fires repeatedly while charge allows
{
  const r = rig();
  r.double();
  for (let i = 0; i < 4; i++) r.single(); // 3,6 then "sufficient" x2 → 6
  r.single();
  r.hold(SIM.HOLD_MS + SIM.FIRE_PERIOD_MS * 3);
  assert(r.s.fireSeq === 1 && r.s.charge === 1, `6/10 allows exactly one shot, got ${r.s.fireSeq}`);
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

console.log('OK — virtual board follows firmware rules through the shared parser');
