/**
 * Wi-Fi telemetry bridge for VESPER (used by Command/serve.mjs).
 *
 * The board (ISM43362) runs a TCP server that streams exactly the UART
 * telemetry bytes ("<line>\r\n"). Browsers cannot open raw TCP, so:
 *
 *   GET /wifi/stream?host=172.20.10.5[&port=5028]
 *       → text/event-stream. One SSE `data:` event per telemetry line, plus
 *         `event: status` (connecting | connected | closed | error). Retries
 *         every 2 s while the browser stays attached.
 *   GET /wifi/scan[?port=5028][&subnet=172.20.10]
 *       → JSON { port, subnets, found: ["172.20.10.5", ...] }: hosts on the
 *         laptop's own /24 networks (or `subnet`) with the port open.
 *
 * Forwards raw lines only — all parsing stays in Command/src/parser.js.
 */
import net from 'node:net';
import os from 'node:os';

export const WIFI_PORT = 5028;
const RETRY_MS = 2000;
const PING_MS = 15000;
const SCAN_TIMEOUT_MS = 300;
const SCAN_PARALLEL = 64;

const HOST_RE = /^(?:\d{1,3}\.){3}\d{1,3}$|^[A-Za-z0-9.-]{1,253}$/;

function intParam(v, def, min, max) {
  const n = Number(v ?? def);
  return Number.isInteger(n) && n >= min && n <= max ? n : null;
}

function sse(res, { event, data }) {
  if (res.writableEnded) return;
  if (event) res.write(`event: ${event}\n`);
  res.write(`data: ${String(data).replace(/[\r\n]/g, ' ')}\n\n`);
}

function stream(req, res, url) {
  const host = url.searchParams.get('host') || '';
  const port = intParam(url.searchParams.get('port'), WIFI_PORT, 1, 65535);
  if (!HOST_RE.test(host) || port === null) {
    res.writeHead(400, { 'content-type': 'text/plain' }).end('host=<board IPv4> required');
    return;
  }
  res.writeHead(200, {
    'content-type': 'text/event-stream; charset=utf-8',
    'cache-control': 'no-cache',
    connection: 'keep-alive',
  });
  res.write('retry: 2000\n\n');

  let sock = null;
  let timer = null;
  let open = true;
  const ping = setInterval(() => res.write(': ping\n\n'), PING_MS);

  const connect = () => {
    if (!open) return;
    sse(res, { event: 'status', data: `connecting ${host}:${port}` });
    let buf = '';
    sock = net.connect({ host, port });
    sock.setEncoding('utf8'); // keeps multi-byte chars (°C) whole across chunks
    sock.setKeepAlive(true, 5000);
    sock.on('connect', () => sse(res, { event: 'status', data: `connected ${host}:${port}` }));
    sock.on('data', (chunk) => {
      buf += chunk;
      let nl;
      while ((nl = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, nl).replace(/\r$/, '');
        buf = buf.slice(nl + 1);
        if (line.length) sse(res, { data: line });
      }
      if (buf.length > 4096) buf = ''; // no newline in 4 KB: not telemetry
    });
    sock.on('error', (err) => sse(res, { event: 'status', data: `error ${err.code || err.message}` }));
    sock.on('close', () => {
      sse(res, { event: 'status', data: 'closed' });
      sock = null;
      if (open) timer = setTimeout(connect, RETRY_MS);
    });
  };

  req.on('close', () => {
    open = false;
    clearInterval(ping);
    clearTimeout(timer);
    sock?.destroy();
  });
  connect();
}

function probe(host, port) {
  return new Promise((resolve) => {
    const s = net.connect({ host, port });
    const done = (ok) => {
      s.destroy();
      resolve(ok);
    };
    s.setTimeout(SCAN_TIMEOUT_MS, () => done(false));
    s.on('connect', () => done(true));
    s.on('error', () => done(false));
  });
}

function localSubnets() {
  const out = new Set();
  for (const list of Object.values(os.networkInterfaces())) {
    for (const a of list || []) {
      if (a.family === 'IPv4' && !a.internal) out.add(a.address.split('.').slice(0, 3).join('.'));
    }
  }
  return [...out];
}

async function scan(res, url) {
  const port = intParam(url.searchParams.get('port'), WIFI_PORT, 1, 65535);
  const only = url.searchParams.get('subnet');
  if (port === null || (only && !/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(only))) {
    res.writeHead(400, { 'content-type': 'text/plain' }).end('bad port/subnet');
    return;
  }
  const subnets = only ? [only] : localSubnets();
  const self = new Set(Object.values(os.networkInterfaces()).flat().map((a) => a?.address));
  const hosts = subnets.flatMap((s) => Array.from({ length: 254 }, (_, i) => `${s}.${i + 1}`)).filter((h) => !self.has(h) || only);
  const found = [];
  for (let i = 0; i < hosts.length; i += SCAN_PARALLEL) {
    const batch = hosts.slice(i, i + SCAN_PARALLEL);
    const ok = await Promise.all(batch.map((h) => probe(h, port)));
    batch.forEach((h, k) => ok[k] && found.push(h));
  }
  res.writeHead(200, { 'content-type': 'application/json', 'cache-control': 'no-cache' });
  res.end(JSON.stringify({ port, subnets, found }));
}

/** Returns true if the request was a /wifi/* route (handled here). */
export function handleWifi(req, res) {
  let url;
  try {
    url = new URL(req.url, 'http://x');
  } catch {
    return false;
  }
  if (url.pathname === '/wifi/stream') {
    stream(req, res, url);
    return true;
  }
  if (url.pathname === '/wifi/scan') {
    scan(res, url).catch((e) => {
      if (!res.headersSent) res.writeHead(500, { 'content-type': 'text/plain' });
      res.end(String(e));
    });
    return true;
  }
  return false;
}
