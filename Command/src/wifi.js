/**
 * Wi-Fi board transport → same parser as Web Serial and fixture replay.
 *
 * The board's ISM43362 streams the UART telemetry bytes over TCP; the browser
 * can't open raw TCP, so `node Command/serve.mjs` bridges it to Server-Sent
 * Events (/wifi/stream). One SSE message = one telemetry line.
 * Board disconnect = link loss (not EVT TERMINATE); the bridge retries.
 */
import { parser, notify, resetMission } from './state.js';

let es = null;
const LAST_HOST_KEY = 'vesper.wifiHost';

export function lastWifiHost() {
  try {
    return localStorage.getItem(LAST_HOST_KEY) || '';
  } catch {
    return '';
  }
}

/**
 * @param {string} host board IPv4 (shown on UART at boot: "Wi-Fi: connected … IP …")
 * @param {(status: string) => void} [onStatus] connecting / connected / closed / error …
 */
export function connectWifi(host, onStatus = () => {}) {
  disconnectWifi();
  if (!/^(?:\d{1,3}\.){3}\d{1,3}$|^[A-Za-z0-9.-]+$/.test(host)) throw new Error('Enter the board IP, e.g. 172.20.10.5');
  try {
    localStorage.setItem(LAST_HOST_KEY, host);
  } catch {
    /* private mode: fine */
  }
  resetMission('idle');
  es = new EventSource(`/wifi/stream?host=${encodeURIComponent(host)}`);
  es.addEventListener('status', (e) => {
    const s = String(e.data);
    if (s.startsWith('connected')) parser.markLink('wifi');
    else if (s === 'closed' || s.startsWith('error')) parser.markDisconnect();
    onStatus(s);
    notify();
  });
  es.onmessage = (e) => {
    parser.pushLine(String(e.data));
    notify();
  };
  es.onerror = () => {
    // serve.mjs not reachable (EventSource retries by itself)
    onStatus('bridge unreachable — is `node Command/serve.mjs` running?');
  };
}

export function disconnectWifi() {
  if (!es) return;
  es.close();
  es = null;
  parser.markDisconnect();
  notify();
}

/** Ask the bridge to scan the laptop's networks for a board (TCP 5028 open). */
export async function scanWifi() {
  const r = await fetch('/wifi/scan', { cache: 'no-store' });
  if (!r.ok) throw new Error(`scan failed (${r.status})`);
  return r.json();
}
