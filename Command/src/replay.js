/**
 * Fixture / recorded-mission feeder — same parser path as live UART.
 */
import { parser, notify, resetMission } from './state.js';

let timer = null;
let abort = false;

export function stopReplay() {
  abort = true;
  if (timer) {
    clearTimeout(timer);
    timer = null;
  }
}

/**
 * @param {string} text full fixture file contents
 * @param {{ intervalMs?: number, onDone?: () => void }} [opts]
 */
export async function replayText(text, opts = {}) {
  stopReplay();
  abort = false;
  const intervalMs = opts.intervalMs ?? 120;
  resetMission('replay');
  notify();

  const lines = String(text).split(/\r?\n/);
  for (const line of lines) {
    if (abort) break;
    parser.pushLine(line);
    notify();
    await new Promise((r) => {
      timer = setTimeout(r, intervalMs);
    });
  }
  timer = null;
  opts.onDone?.();
}

/**
 * Fetch a fixture under Command/fixtures/ when served from localhost.
 * @param {string} name e.g. mission_uart_sample.log
 */
export async function replayFixture(name, opts = {}) {
  const url = new URL(`../fixtures/${name}`, import.meta.url);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`Fixture fetch failed: ${name} (${res.status})`);
  const text = await res.text();
  return replayText(text, opts);
}
