/**
 * VESPER live-binding entry — fixture replay + Web Serial share one parser.
 */
import { subscribe, getState, resetMission, notify } from './state.js';
import { bindDashboard } from './bindings.js';
import { replayFixture, stopReplay } from './replay.js';
import { connectSerial, disconnectSerial, webSerialSupported } from './serial.js';

subscribe((s) => bindDashboard(s));
resetMission('idle');
notify();

function toast(msg) {
  const el = document.getElementById('toast');
  if (!el) return;
  el.textContent = msg;
  el.style.display = 'block';
  clearTimeout(toast._t);
  toast._t = setTimeout(() => {
    el.style.display = 'none';
  }, 3200);
}

function ensureToolbar() {
  let bar = document.getElementById('cc-toolbar');
  if (bar) return bar;
  bar = document.createElement('div');
  bar.id = 'cc-toolbar';
  bar.style.cssText =
    'position:fixed;top:12px;right:12px;z-index:20;display:flex;gap:8px;flex-wrap:wrap;max-width:min(520px,96vw)';
  bar.innerHTML = `
    <button type="button" id="cc-replay" title="Replay Command/fixtures/mission_uart_sample.log">Replay fixture</button>
    <button type="button" id="cc-replay-early" title="Replay early self-destruct fixture">Replay early SD</button>
    <button type="button" id="cc-serial">${webSerialSupported() ? 'Connect serial' : 'Serial N/A'}</button>
    <button type="button" id="cc-disconnect">Disconnect</button>
    <button type="button" id="cc-reset">Reset state</button>
  `;
  document.body.appendChild(bar);
  return bar;
}

ensureToolbar();

document.getElementById('cc-replay')?.addEventListener('click', async () => {
  stopReplay();
  try {
    toast('Replaying mission_uart_sample.log…');
    await replayFixture('mission_uart_sample.log', {
      intervalMs: 100,
      onDone: () => toast(`Replay done · mode=${getState().mode}`),
    });
  } catch (e) {
    toast(String(e.message || e));
  }
});

document.getElementById('cc-replay-early')?.addEventListener('click', async () => {
  stopReplay();
  try {
    toast('Replaying mission_timeout_early.log…');
    await replayFixture('mission_timeout_early.log', {
      intervalMs: 80,
      onDone: () => toast(`Replay done · mode=${getState().mode}`),
    });
  } catch (e) {
    toast(String(e.message || e));
  }
});

document.getElementById('cc-serial')?.addEventListener('click', async () => {
  try {
    stopReplay();
    await connectSerial(115200);
    toast('Web Serial connected @ 115200');
  } catch (e) {
    toast(String(e.message || e));
  }
});

document.getElementById('cc-disconnect')?.addEventListener('click', async () => {
  stopReplay();
  await disconnectSerial();
  toast('Disconnected (link loss, not TERMINATE)');
});

document.getElementById('cc-reset')?.addEventListener('click', () => {
  stopReplay();
  resetMission('idle');
  notify();
  toast('Dashboard state reset');
});

bindDashboard(getState());
