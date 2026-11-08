/**
 * End-to-end: fake Wi-Fi board (TCP, real firmware telemetry bytes)
 *   → Command/serve.mjs bridge (/wifi/stream SSE) → VESPER parser.
 * Also checks reconnect after the board drops, /wifi/scan, and input checks.
 * Run: node scripts/test_wifi_bridge.mjs
 */
import { execFileSync, spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cc = process.env.CC || 'gcc';
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'wifi-bridge-'));
const exe = path.join(tmp, process.platform === 'win32' ? 'telemetry_stream.exe' : 'telemetry_stream');
execFileSync(cc, ['-std=c99', '-O1', `-I${path.join(root, 'App/Inc')}`,
  path.join(root, 'App/Src/telemetry_evt.c'), path.join(root, 'scripts/host/telemetry_stream_host.c'), '-o', exe]);
const wire = execFileSync(exe); // exact CRLF bytes the board sends
const wireLines = wire.toString('utf8').split('\r\n').filter((l) => l.length);

const { createParser } = await import(pathToFileURL(path.join(root, 'Command/src/parser.js')).href);
const assert = (c, m) => { if (!c) throw new Error(m); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- fake board: first connection gets the mission in random chunks, later ones a short line
let connections = 0;
const boardSockets = new Set();
const board = net.createServer(async (s) => {
  connections += 1;
  boardSockets.add(s);
  s.on('close', () => boardSockets.delete(s));
  s.on('error', () => {});
  if (connections === 1) {
    let i = 0;
    let seed = 7;
    while (i < wire.length && !s.destroyed) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      const n = 1 + (seed % 60); // TCP segments split lines and the UTF-8 °
      s.write(wire.subarray(i, i + n));
      i += n;
      await sleep(1);
    }
  } else {
    s.write('EVT MODE=BATTLE_MODE\r\n');
  }
});
await new Promise((r) => board.listen(0, '127.0.0.1', r));
const boardPort = board.address().port;

// ---- real serve.mjs on a free port
const httpPort = await new Promise((r) => { const t = net.createServer(); t.listen(0, '127.0.0.1', () => { const p = t.address().port; t.close(() => r(p)); }); });
const srv = spawn(process.execPath, [path.join(root, 'Command/serve.mjs'), String(httpPort)], { stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((r, j) => { srv.stdout.on('data', (d) => /VESPER/.test(String(d)) && r()); srv.on('exit', () => j(new Error('serve.mjs exited'))); });
const base = `http://127.0.0.1:${httpPort}`;

try {
  // ---- stream
  const p = createParser();
  p.markLink('wifi');
  const got = [];
  const status = [];
  const ac = new AbortController();
  const resp = await fetch(`${base}/wifi/stream?host=127.0.0.1&port=${boardPort}`, { signal: ac.signal });
  assert(resp.ok && /text\/event-stream/.test(resp.headers.get('content-type')), 'SSE response');
  const reader = resp.body.getReader();
  const dec = new TextDecoder();
  let buf = '';
  let reconnected = false;
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline && !reconnected) {
    const { value, done } = await reader.read();
    if (done) break;
    buf += dec.decode(value, { stream: true });
    let k;
    while ((k = buf.indexOf('\n\n')) >= 0) {
      const block = buf.slice(0, k);
      buf = buf.slice(k + 2);
      const ev = /^event: (.*)$/m.exec(block)?.[1];
      const data = /^data: (.*)$/m.exec(block)?.[1];
      if (data == null) continue;
      if (ev === 'status') {
        status.push(data);
        continue;
      }
      got.push(data);
      p.pushLine(data);
      if (data === 'Cheerio!' && connections === 1) {
        for (const s of boardSockets) s.destroy(); // board drops off Wi-Fi
      }
      if (connections >= 2 && data === 'EVT MODE=BATTLE_MODE' && got.length > wireLines.length) reconnected = true;
    }
  }
  ac.abort();

  const mission = got.slice(0, wireLines.length);
  assert(mission.length === wireLines.length, `got ${mission.length} of ${wireLines.length} lines`);
  mission.forEach((l, i) => assert(l === wireLines[i], `line ${i} differs: "${l}" vs "${wireLines[i]}"`));
  assert(mission.some((l) => l.includes('°C')), 'UTF-8 °C survives TCP chunking');
  assert(p.state.missionEnded && p.state.mode === 'TERMINATED', `parser ended the mission, mode=${p.state.mode}`);
  assert(p.state.telemetry.T === 26.4, `sensor line parsed, T=${p.state.telemetry.T}`);
  assert(status.some((s) => s.startsWith('connected')), `status connected, got ${status.join(' | ')}`);
  assert(status.includes('closed'), 'status closed when the board drops');
  assert(reconnected, 'bridge reconnected and kept streaming');

  // ---- scan
  const scan = await (await fetch(`${base}/wifi/scan?subnet=127.0.0&port=${boardPort}`)).json();
  assert(scan.found.includes('127.0.0.1'), `scan finds the board, got ${JSON.stringify(scan.found)}`);

  // ---- input validation
  assert((await fetch(`${base}/wifi/stream?host=bad%20host`)).status === 400, 'bad host rejected');
  assert((await fetch(`${base}/wifi/scan?subnet=1.2`)).status === 400, 'bad subnet rejected');
  assert((await fetch(`${base}/index.html`)).ok, 'static files still served');

  console.log(`OK — Wi-Fi bridge: ${mission.length} lines byte-identical, reconnect, scan (${scan.found.length} found)`);
} finally {
  srv.kill();
  board.close();
  for (const s of boardSockets) s.destroy();
}
