/**
 * End-to-end: real firmware telemetry bytes → VESPER parser.
 *
 * Builds scripts/host/telemetry_stream_host.c against the real
 * App/Src/telemetry_evt.c (host C compiler: gcc on PATH, or CC=...), captures
 * the exact "<line>\r\n" byte stream the board sends on the ST-LINK COM port,
 * then feeds it to Command/src/parser.js in random-sized chunks through a
 * streaming TextDecoder — the same path Command/src/serial.js uses for Web Serial.
 *
 * Run: node scripts/test_vesper_ingest.mjs
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cc = process.env.CC || 'gcc';
const exe = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'vesper-ingest-')),
  process.platform === 'win32' ? 'telemetry_stream.exe' : 'telemetry_stream');

execFileSync(cc, ['-std=c99', '-Wall', '-Wextra', '-O1',
  `-I${path.join(root, 'App/Inc')}`,
  path.join(root, 'App/Src/telemetry_evt.c'),
  path.join(root, 'scripts/host/telemetry_stream_host.c'),
  '-o', exe], { stdio: 'inherit' });
const wire = execFileSync(exe); // Buffer: exact bytes, CRLF framed

const { createParser } = await import(pathToFileURL(path.join(root, 'Command/src/parser.js')).href);

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

function ingest(bytes, seed) {
  const p = createParser();
  p.markLink('uart');
  const decoder = new TextDecoder();
  let rnd = seed;
  let i = 0;
  while (i < bytes.length) {
    rnd = (rnd * 1103515245 + 12345) & 0x7fffffff;
    const n = 1 + (rnd % 40); // UART chunks split lines and UTF-8 (°) anywhere
    p.pushChunk(decoder.decode(bytes.subarray(i, i + n), { stream: true }));
    i += n;
  }
  return p.state;
}

assert(wire.includes(Buffer.from('\r\n')), 'firmware lines must be CRLF framed');
assert(wire.toString('utf8').includes('°C'), 'sensor line carries UTF-8 °C');

for (const seed of [1, 7, 42, 1234]) {
  const s = ingest(wire, seed);
  const tag = `[seed ${seed}]`;
  assert(s.telemetry.T === 26.4 && s.telemetry.P === 1008.2 && s.telemetry.H === 55,
    `${tag} sensor T/P/H parsed, got ${s.telemetry.T}/${s.telemetry.P}/${s.telemetry.H}`);
  assert(s.telemetry.Accel[2] === 9.8 && s.telemetry.Magneto[2] === 380,
    `${tag} sensor vectors parsed`);
  assert(s.fireSeq === 2, `${tag} two shots, fireSeq=${s.fireSeq}`);
  assert(s.charge === 3, `${tag} charge after recharge = 3, got ${s.charge}`);
  assert(s.radarLockSeq === 2, `${tag} two radar locks (zombie era + giant), got ${s.radarLockSeq}`);
  assert(s.cannonSeq === 2 && s.fireSeq === 2, `${tag} cannon shots separate from gun shots, cannon=${s.cannonSeq} fire=${s.fireSeq}`);
  assert(s.giantHitSeq === 1 && s.giantDownSeq === 1 && s.giantKills === 1, `${tag} giant hit then down`);
  assert(s.giant.hits === 0, `${tag} giant HP reset after down/lost`);
  assert(s.radar.lock === false, `${tag} radar lost clears lock`);
  assert(s.rescueSeq === 1, `${tag} one rescue, got ${s.rescueSeq}`);
  assert(s.attackSeq >= 2, `${tag} attacks counted, got ${s.attackSeq}`);
  assert(s.lastSelfDestructReason === 'timeout', `${tag} reason=${s.lastSelfDestructReason}`);
  assert(s.missionEnded === true && s.mode === 'TERMINATED', `${tag} mission ended, mode=${s.mode}`);
  assert(s.rawLog.some((l) => /Cheerio!/.test(l)), `${tag} Cheerio in log`);
  assert(s.rawLog.some((l) => /IR threat detected/.test(l)), `${tag} IR line reaches VESPER log`);
  assert(!s.rawLog.some((l) => /\r/.test(l)), `${tag} no stray CR in parsed lines`);
}

console.log(`OK — VESPER parses the real firmware telemetry stream (${wire.length} bytes, chunked)`);
