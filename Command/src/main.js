/**
 * VESPER entry — one parser, four signal sources, one renderer.
 *
 *   virtual board (sim.js) ─┐
 *   fixture replay ─────────┤
 *   pasted UART log ────────┼─► parser.js state ─► bindings.js (HUD) + scene.js (world)
 *   Web Serial (live) ──────┘
 */
import { parser, getState, resetMission, notify } from './state.js';
import { bindHud, invalidateHud } from './bindings.js';
import { createScene } from './scene.js';
import { createVirtualBoard } from './sim.js';
import { replayFixture, stopReplay } from './replay.js';
import { connectSerial, disconnectSerial, webSerialSupported } from './serial.js';

const $ = (id) => document.getElementById(id);

const scene = createScene($('world'));
const board = createVirtualBoard((line) => parser.pushLine(line));

let source = 'simulation';
let clock = 0;
let previous = performance.now();

/** Scene-only effects derived from parser events (never the other way round). */
const fx = { contact: false, secured: false, shot: 0, scan: 0, seen: null };

function resetFx(state) {
  fx.contact = false;
  fx.secured = false;
  fx.shot = 0;
  fx.scan = 0;
  fx.seen = {
    fireSeq: state.fireSeq,
    tripSeq: state.tripSeq,
    telemetryAt: state.telemetryAt,
  };
}

/** Turn new parser events into visual effects. Runs every frame; cheap. */
function absorbEvents(state) {
  const seen = fx.seen;
  // A reset swaps counters back to 0 — resync instead of replaying effects.
  if (state.fireSeq < seen.fireSeq || state.tripSeq < seen.tripSeq) resetFx(state);
  if (state.tripSeq > seen.tripSeq) fx.contact = true; // T/A/G disturbance = something hit the perimeter
  if (state.fireSeq > seen.fireSeq) {
    fx.shot = 0.3;
    if (fx.contact) {
      fx.contact = false;
      fx.secured = true;
    }
  }
  if (state.telemetryAt !== seen.telemetryAt && state.telemetryAt) fx.scan = 1;
  seen.fireSeq = state.fireSeq;
  seen.tripSeq = state.tripSeq;
  seen.telemetryAt = state.telemetryAt;
}

function toast(message) {
  const el = $('toast');
  el.textContent = message;
  el.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => {
    el.hidden = true;
  }, 3200);
}

// ---------------------------------------------------------------- sources

const PANELS = {
  simulation: 'simulationControls',
  replay: 'replayControls',
  paste: 'pastePanel',
  serial: 'serialControls',
};

const FRESHNESS = {
  simulation: 'Browser board emits the same UART lines as the firmware',
  replay: 'Recorded fixture · no live connection',
  paste: 'Pasted log · no live connection',
  serial: webSerialSupported() ? 'Live UART from the B-L4S5I-IOT01A' : 'Web Serial needs Chrome or Edge',
};

async function startSource(next) {
  stopReplay();
  if (source === 'serial' && next !== 'serial') await disconnectSerial();
  source = next;
  for (const [key, id] of Object.entries(PANELS)) $(id).hidden = key !== source;
  $('freshness').textContent = FRESHNESS[source];
  resetMission(source === 'simulation' ? 'sim' : 'idle');
  if (source === 'simulation') {
    board.reset(performance.now());
    board.tick(performance.now());
  }
  resetFx(getState());
  invalidateHud();
  notify();
}

$('source').addEventListener('change', (event) => startSource(event.target.value));
$('restart').addEventListener('click', () => {
  startSource(source);
  toast('Mission reset.');
});

// Virtual board controls
const pb = $('pb');
const pbDown = (event) => {
  if (source !== 'simulation') return;
  event?.preventDefault?.();
  board.pressDown(performance.now());
};
const pbUp = () => {
  if (source !== 'simulation') return;
  board.pressUp(performance.now());
};
pb.addEventListener('pointerdown', (event) => {
  pb.setPointerCapture(event.pointerId);
  pbDown(event);
});
pb.addEventListener('pointerup', pbUp);
pb.addEventListener('pointercancel', pbUp);
pb.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') event.preventDefault(); // Space is handled globally
});
window.addEventListener('keydown', (event) => {
  if (event.code !== 'Space' || event.repeat || source !== 'simulation') return;
  if (event.target instanceof HTMLTextAreaElement || event.target instanceof HTMLSelectElement) return;
  event.preventDefault();
  board.pressDown(performance.now());
});
window.addEventListener('keyup', (event) => {
  if (event.code !== 'Space' || source !== 'simulation') return;
  event.preventDefault();
  board.pressUp(performance.now());
});

$('threat').addEventListener('click', () => {
  fx.contact = true;
  board.disturb();
});
$('overturn').addEventListener('click', () => {
  board.setUpsideDown(!board.snapshot.upsideDown);
});

// Recorded missions
async function playFixture(name, intervalMs) {
  try {
    toast(`Replaying ${name}…`);
    resetFx(getState());
    await replayFixture(name, {
      intervalMs,
      onDone: () => toast(`Replay finished · ${getState().mode}`),
    });
  } catch (error) {
    toast(String(error.message || error));
  }
}
$('replaySample').addEventListener('click', () => playFixture('mission_uart_sample.log', 220));
$('replayEarly').addEventListener('click', () => playFixture('mission_timeout_early.log', 220));
$('replayStop').addEventListener('click', () => stopReplay());

// Pasted UART
$('ingest').addEventListener('click', () => {
  const lines = $('uart').value.split(/\r?\n/).filter((line) => line.trim());
  resetMission('replay');
  resetFx(getState());
  invalidateHud();
  for (const line of lines) parser.pushLine(line);
  notify();
  $('parseStatus').textContent = `${lines.length} lines read through the shared parser.`;
});

// Live board
if (!webSerialSupported()) {
  $('serialConnect').disabled = true;
  $('serialHint').textContent = 'Web Serial is unavailable in this browser. Use Chrome or Edge, or replay a recorded mission.';
}
$('serialConnect').addEventListener('click', async () => {
  try {
    await connectSerial(115200);
    resetFx(getState());
    invalidateHud();
    toast('Board connected @ 115200');
  } catch (error) {
    toast(String(error.message || error));
  }
});
$('serialDisconnect').addEventListener('click', async () => {
  await disconnectSerial();
  toast('Disconnected (link loss, not TERMINATE)');
});

$('camera').addEventListener('click', () => {
  const forward = scene.toggleCamera();
  $('camera').textContent = forward ? 'Orbit camera' : 'Forward camera';
  $('sceneLabel').textContent = forward ? 'FORWARD OPTICS · SIMULATED' : 'ORBIT VIEW · DRAG TO EXPLORE';
});

// ---------------------------------------------------------------- loop

function updateSimControls(state) {
  const snap = board.snapshot;
  const halted = snap.state === 'SELF_DESTRUCT_TRIGGERED' || snap.halted;
  pb.disabled = halted;
  pb.classList.toggle('holding', snap.holding);
  pb.textContent =
    snap.state === 'THE_LAST_OF_2028'
      ? 'PB · Double tap to rescue'
      : snap.state === 'BATTLE_MODE'
        ? state.armed
          ? 'PB · Hold to fire'
          : 'PB · Tap to charge'
        : halted
          ? 'PB · No response'
          : 'PB · Double tap for Battle';
  $('threat').disabled = halted || fx.contact || snap.state === 'THE_LAST_OF_2028';
  $('overturn').disabled = halted;
  $('overturn').textContent = snap.upsideDown ? 'Flip upright' : 'Flip upside down';
}

function frame(now) {
  const dt = Math.min(0.1, (now - previous) / 1000);
  previous = now;
  clock += dt;

  if (source === 'simulation') board.tick(now);

  const state = getState();
  absorbEvents(state);
  fx.shot = Math.max(0, fx.shot - dt);
  fx.scan = Math.max(0, fx.scan - dt);

  if (source === 'simulation') updateSimControls(state);
  bindHud(state, { contact: fx.contact, secured: fx.secured, source });
  scene.draw(
    {
      mode: state.mode,
      down: state.mode === 'THE_LAST_OF_2028' || state.missionEnded,
      ended: state.missionEnded,
      secured: fx.secured,
      contact: fx.contact,
      shot: fx.shot,
      scan: fx.scan,
    },
    clock
  );
  requestAnimationFrame(frame);
}

startSource('simulation');
requestAnimationFrame(frame);
