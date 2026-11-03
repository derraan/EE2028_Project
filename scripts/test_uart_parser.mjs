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

console.log('OK — UART/EVT parser fixtures passed');
