/**
 * Offline regression: fixture → parser → expected dashboard fields.
 * Run: node scripts/test_uart_parser.mjs
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const parserUrl = pathToFileURL(path.join(root, 'Command/src/parser.js')).href;
const { createParser } = await import(parserUrl);

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function replayFile(rel) {
  const text = fs.readFileSync(path.join(root, rel), 'utf8');
  const p = createParser();
  p.markLink('replay');
  for (const line of text.split(/\r?\n/)) p.pushLine(line);
  return p.state;
}

const sample = replayFile('Command/fixtures/mission_uart_sample.log');
assert(sample.missionEnded === true, 'sample should TERMINATE');
assert(sample.mode === 'TERMINATED', `mode=${sample.mode}`);
assert(sample.lastSelfDestructReason === 'timeout', `reason=${sample.lastSelfDestructReason}`);
assert(sample.charge === 5, `charge after last fire path expected 5, got ${sample.charge}`);
assert(sample.telemetry.T === 26.5, `T=${sample.telemetry.T}`);
assert(sample.telemetry.Accel[2] === 9.7, `Accel Z=${sample.telemetry.Accel[2]}`);
assert(sample.telemetry.units.P === 'hPa', 'units preserved');
assert(sample.rawLog.some((l) => /Cheerio!/.test(l)), 'Cheerio kept in raw log');
assert(sample.events.some((e) => /TERMINATE/.test(e.text)), 'TERMINATE in events');
assert(sample.fireSeq === 2, `fireSeq=${sample.fireSeq}`);
assert(sample.rescueSeq === 1, `rescueSeq=${sample.rescueSeq}`);
assert(sample.attackSeq >= 2, `attackSeq=${sample.attackSeq}`);
assert(sample.selfDestructing === false && sample.countdownMs === null, 'TERMINATE clears self-destruct + countdown');
assert(sample.lineCount === sample.rawLog.length, `lineCount=${sample.lineCount}`);

// T/A/G trip prose + rescue attempt prose
{
  const p = createParser();
  p.pushLine('Critical T/A/G threshold violated -> Entering BATTLE MODE. (Laser Charge: 0/10)');
  assert(p.state.mode === 'BATTLE_MODE' && p.state.tripSeq === 1, 'T/A/G trip parsed');
  p.pushLine('EVT MODE=THE_LAST_OF_2028');
  p.pushLine('EVT COUNTDOWN remaining_ms=10000');
  assert(p.state.countdownAt != null, 'countdown timestamped');
  p.pushLine('Rescue attempt 3/5 (Double Press detected)...');
  assert(p.state.rescueAttempts === 3, `rescueAttempts=${p.state.rescueAttempts}`);
  p.pushLine('EVT MODE=BATTLE_MODE');
  assert(p.state.countdownMs === null && p.state.rescueAttempts === 0, 'leaving Last clears countdown/attempts');
}

const early = replayFile('Command/fixtures/mission_timeout_early.log');
assert(early.missionEnded === true, 'early fixture terminates');
assert(early.lastSelfDestructReason === 'rescue_limit', `early reason=${early.lastSelfDestructReason}`);

// Chunked feed (simulates UART)
const p2 = createParser();
p2.markLink('uart');
p2.pushChunk('EVT MODE=BATTLE');
p2.pushChunk('_MODE\r\nEVT FIRE char');
p2.pushChunk('ge=2/10\n');
assert(p2.state.mode === 'BATTLE_MODE', `chunk mode=${p2.state.mode}`);
assert(p2.state.charge === 2 && p2.state.armed === false, `chunk charge=${p2.state.charge}`);

// Disconnect ≠ terminate
p2.markDisconnect();
assert(p2.state.link === 'severed', 'disconnect severs link');
assert(p2.state.missionEnded === false, 'disconnect must not end mission');

// EVT RADAR (enhancement #5)
{
  const p = createParser();
  p.pushLine('EVT MODE=BATTLE_MODE');
  p.pushLine('EVT RADAR az=-12.4 range_mm=1830 lock=1 pan=-11.9 laser=1 n=2');
  const r = p.state.radar;
  assert(r.lock && r.az === -12.4 && r.rangeMm === 1830 && r.pan === -11.9 && r.laser && r.contacts === 2, `radar parsed ${JSON.stringify(r)}`);
  assert(p.state.radarAz === -12.4 && p.state.radarLockSeq === 1, 'legacy radarAz + lock seq');
  p.pushLine('EVT RADAR az=-10.0 range_mm=1800 lock=1 pan=-10.2 laser=0 n=1');
  assert(p.state.radarLockSeq === 1 && !p.state.radar.laser, 'track update is not a new lock');
  p.pushLine('EVT RADAR lost');
  assert(!p.state.radar.lock && p.state.radar.az === null && p.state.radarAz === null, 'lost clears');
  p.pushLine('EVT RADAR az=5.0 range_mm=900 lock=1 pan=4.0 laser=1 n=1');
  assert(p.state.radarLockSeq === 2, 'relock counts');
  p.pushLine('EVT MODE=THE_LAST_OF_2028');
  assert(!p.state.radar.lock, 'Last of EE2028 clears radar');
  p.pushLine('EVT RADAR az=3'); // old harness shape still parses (degrees)
  assert(p.state.radar.az === 3 && p.state.radar.lock, 'bare az tolerated');
}

console.log('OK — UART/EVT parser fixtures passed');
