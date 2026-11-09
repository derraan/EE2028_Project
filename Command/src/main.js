/**
 * VESPER entry — one parser, four signal sources, one world.
 *
 *   virtual board (sim.js) ─┐
 *   fixture replay ─────────┤
 *   pasted UART log ────────┼─► parser.js state ─► bindings.js (HUD)
 *   Web Serial (live) ──────┘                   └► world/ (3D) + radar + audio
 *
 * The world turns *new parsed events* into spectacle (laser on EVT FIRE,
 * meteor wave on a T/A/G trip, shockwave on EVT RESCUE, blast on TERMINATE).
 * Zombies, kills and waves are fiction and never feed back into drone state.
 * In simulation the zombies stand in front of the *virtual* RD-03D (like a
 * person in front of the real one); the lock and laser still only appear once
 * the board prints EVT RADAR.
 */
import { parser, getState, resetMission, notify } from './state.js';
import { bindHud, invalidateHud } from './bindings.js';
import { createVirtualBoard, SIM } from './sim.js';
import { replayFixture, stopReplay } from './replay.js';
import { connectSerial, disconnectSerial, webSerialSupported } from './serial.js';
import { connectWifi, disconnectWifi, scanWifi, lastWifiHost } from './wifi.js';
import { createWorld } from './world/world.js';
import { createRadar } from './world/radar.js';
import { createAudio } from './world/audio.js';

const $ = (id) => document.getElementById(id);

const audio = createAudio();
const params = new URLSearchParams(location.search);
const world = createWorld($('viewport'), { audio, adaptive: !params.has('fixedq') });
const radar = createRadar($('radar'));
const board = createVirtualBoard((line) => parser.pushLine(line));

let source = 'simulation';
let clock = 0;
let previous = performance.now();

// ---------------------------------------------------------------- events

/** Last-seen parser counters; diffs become one-shot world events. */
let seen = null;
function syncSeen(state) {
  seen = {
    fireSeq: state.fireSeq,
    tripSeq: state.tripSeq,
    attackSeq: state.attackSeq,
    rescueSeq: state.rescueSeq,
    radarLockSeq: state.radarLockSeq,
    cannonSeq: state.cannonSeq,
    giantHitSeq: state.giantHitSeq,
    giantDownSeq: state.giantDownSeq,
    telemetryAt: state.telemetryAt,
    missionEnded: state.missionEnded,
  };
}

function takeEvents(state) {
  if (!seen || state.fireSeq < seen.fireSeq || state.tripSeq < seen.tripSeq || state.attackSeq < seen.attackSeq || state.radarLockSeq < seen.radarLockSeq) {
    syncSeen(state); // a reset swapped counters back to 0 — don't replay old effects
    world.reset();
  }
  const ev = {
    fired: state.fireSeq - seen.fireSeq,
    tripped: state.tripSeq - seen.tripSeq,
    attacked: state.attackSeq - seen.attackSeq,
    rescued: state.rescueSeq - seen.rescueSeq,
    radarLocked: state.radarLockSeq - seen.radarLockSeq,
    cannon: state.cannonSeq - seen.cannonSeq,
    giantHit: state.giantHitSeq - seen.giantHitSeq,
    giantDown: state.giantDownSeq - seen.giantDownSeq,
    telemetry: Boolean(state.telemetryAt) && state.telemetryAt !== seen.telemetryAt,
    terminated: state.missionEnded && !seen.missionEnded,
  };
  syncSeen(state);
  return ev;
}

function toast(message) {
  const el = $('toast');
  el.textContent = message;
  el.hidden = false;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => {
    el.hidden = true;
  }, 3000);
}

// ---------------------------------------------------------------- sources

const PANELS = { simulation: 'simulationControls', replay: 'replayControls', paste: 'pastePanel', serial: 'serialControls', wifi: 'wifiControls' };
const FRESHNESS = {
  simulation: "Browser board prints the firmware's UART lines",
  replay: 'Recorded fixture · no live connection',
  paste: 'Pasted log · no live connection',
  serial: webSerialSupported() ? 'Live UART from the B-L4S5I-IOT01A' : 'Web Serial needs Chrome or Edge',
  wifi: 'Live telemetry over Wi-Fi (ISM43362 → serve.mjs bridge)',
};

async function startSource(next) {
  stopReplay();
  if (source === 'serial' && next !== 'serial') await disconnectSerial();
  if (source === 'wifi' && next !== 'wifi') disconnectWifi();
  source = next;
  $('source').value = next;
  for (const [key, id] of Object.entries(PANELS)) $(id).hidden = key !== source;
  $('freshness').textContent = FRESHNESS[source];
  resetMission(source === 'simulation' ? 'sim' : 'idle');
  if (source === 'simulation') {
    board.reset(performance.now());
    board.tick(performance.now());
  }
  syncSeen(getState());
  world.reset();
  invalidateHud();
  notify();
}

$('source').addEventListener('change', (event) => startSource(event.target.value));
const restart = () => {
  startSource(source);
  toast('Drone rebooted. Mission reset.');
};
$('restart').addEventListener('click', restart);
$('endingRestart').addEventListener('click', restart);

// ---------------------------------------------------------------- virtual board controls

const pb = $('pb');
let pbHeldAt = null;
function pbDown(event) {
  if (source !== 'simulation' || pb.disabled) return;
  event?.preventDefault?.();
  audio.unlock();
  pbHeldAt = performance.now();
  pb.classList.add('down');
  board.pressDown(pbHeldAt);
}
function pbUp() {
  if (source !== 'simulation' || pbHeldAt === null) return;
  pbHeldAt = null;
  pb.classList.remove('down');
  board.pressUp(performance.now());
}
pb.addEventListener('pointerdown', (event) => {
  pb.setPointerCapture(event.pointerId);
  pbDown(event);
});
pb.addEventListener('pointerup', pbUp);
pb.addEventListener('pointercancel', pbUp);
pb.addEventListener('keydown', (event) => {
  if (event.key === 'Enter') event.preventDefault(); // Space is the PB key
});
window.addEventListener('keydown', (event) => {
  if (event.code !== 'Space' || event.repeat || source !== 'simulation') return;
  const t = event.target;
  if (t instanceof HTMLTextAreaElement || t instanceof HTMLSelectElement || t instanceof HTMLInputElement) return;
  if (!$('intro').hidden) return;
  event.preventDefault();
  pbDown();
});
window.addEventListener('keyup', (event) => {
  if (event.code !== 'Space' || source !== 'simulation') return;
  event.preventDefault();
  pbUp();
});

$('threat').addEventListener('click', () => {
  audio.unlock();
  board.disturb();
  // In Battle the firmware ignores T/A/G — the world still drops a pod so there is something to defend against.
  if (board.snapshot.state === 'BATTLE_MODE') world.launchWave(4);
});
$('overturn').addEventListener('click', () => {
  audio.unlock();
  board.setUpsideDown(!board.snapshot.upsideDown);
});

// ---------------------------------------------------------------- recorded / pasted / live

async function playFixture(name) {
  try {
    toast(`Replaying ${name}…`);
    await replayFixture(name, { intervalMs: 380, onDone: () => toast(`Replay finished · ${getState().mode}`) });
  } catch (error) {
    toast(String(error.message || error));
  }
}
$('replaySample').addEventListener('click', () => playFixture('mission_uart_sample.log'));
$('replayEarly').addEventListener('click', () => playFixture('mission_timeout_early.log'));
$('replayStop').addEventListener('click', () => stopReplay());

$('ingest').addEventListener('click', () => {
  const lines = $('uart').value.split(/\r?\n/).filter((line) => line.trim());
  resetMission('replay');
  invalidateHud();
  for (const line of lines) parser.pushLine(line);
  notify();
  $('parseStatus').textContent = `${lines.length} lines read through the shared parser.`;
});

if (!webSerialSupported()) {
  $('serialConnect').disabled = true;
  $('serialHint').textContent = 'Web Serial is unavailable in this browser. Use Chrome or Edge, or replay a recorded mission.';
}
$('serialConnect').addEventListener('click', async () => {
  try {
    await connectSerial(115200);
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

// Wi-Fi board (ISM43362 TCP → serve.mjs SSE bridge)
$('wifiHost').value = lastWifiHost();
const wifiHint = (text) => {
  $('wifiHint').textContent = text;
};
$('wifiConnect').addEventListener('click', () => {
  try {
    connectWifi($('wifiHost').value.trim(), (s) => {
      if (s.startsWith('connected')) {
        wifiHint(`Streaming from ${s.slice(10)}`);
        toast('Board connected over Wi-Fi');
        invalidateHud();
      } else if (s.startsWith('connecting')) wifiHint(`Connecting to ${s.slice(11)}…`);
      else if (s === 'closed') wifiHint('Board dropped off Wi-Fi — retrying every 2 s…');
      else if (s.startsWith('error')) wifiHint(`Board unreachable (${s.slice(6)}) — same hotspot? retrying…`);
      else wifiHint(s);
    });
  } catch (error) {
    toast(String(error.message || error));
  }
});
$('wifiDisconnect').addEventListener('click', () => {
  disconnectWifi();
  wifiHint('Disconnected (link loss, not TERMINATE)');
});
$('wifiScan').addEventListener('click', async () => {
  $('wifiScan').disabled = true;
  wifiHint('Scanning this laptop’s networks for a board on TCP 5028…');
  try {
    const r = await scanWifi();
    if (r.found.length) {
      $('wifiHost').value = r.found[0];
      wifiHint(`Found ${r.found.join(', ')} on ${r.subnets.join(', ')}.x — press Connect.`);
    } else {
      wifiHint(`No board on ${r.subnets.map((s) => `${s}.x`).join(', ') || 'any network'} — same hotspot? Wi-Fi joined (see UART)?`);
    }
  } catch (error) {
    wifiHint(`Scan needs node Command/serve.mjs (${error.message || error})`);
  } finally {
    $('wifiScan').disabled = false;
  }
});

// ---------------------------------------------------------------- chrome

$('camera').addEventListener('click', () => {
  const mode = world.toggleCamera();
  $('camera').textContent = mode === 'drone' ? 'Orbit cam' : 'Drone cam';
});
function setSound(on) {
  audio.unlock();
  audio.setMuted(!on);
  $('audio').setAttribute('aria-pressed', String(on));
  $('audio').textContent = on ? 'Sound on' : 'Sound off';
}
$('audio').addEventListener('click', () => setSound(audio.muted));
$('enter').addEventListener('click', () => {
  $('intro').hidden = true;
  setSound(true);
  $('viewport').querySelector('canvas')?.focus({ preventScroll: true });
});
audio.setMuted(true);

function updateSimControls(state) {
  const snap = board.snapshot;
  const halted = snap.state === 'SELF_DESTRUCT_TRIGGERED' || snap.halted;
  pb.disabled = halted;
  pb.classList.toggle('holding', snap.holding);
  const holdMs = pbHeldAt === null ? 0 : performance.now() - pbHeldAt;
  const ring = Math.min(100, (holdMs / SIM.HOLD_MS) * 100);
  pb.querySelector('circle').style.strokeDasharray = `${pbHeldAt === null ? 0 : ring} 100`;
  const action =
    snap.state === 'THE_LAST_OF_2028'
      ? snap.upsideDown
        ? 'Flip upright first'
        : 'Double tap to rescue'
      : snap.state === 'BATTLE_MODE'
        ? state.armed
          ? 'Hold to fire'
          : 'Tap to charge'
        : halted
          ? 'No response'
          : 'Double tap for Battle';
  $('pbAction').textContent = action;
  $('threat').disabled = halted;
  $('overturn').disabled = halted;
  $('overturn').textContent = snap.upsideDown ? 'Flip upright' : 'Flip upside down';
}

function updateClock() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, '0');
  $('clock').textContent = `6202.${p(d.getMonth() + 1)}.${p(d.getDate())} // ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

// ---------------------------------------------------------------- loop

let clockSecond = -1;
function frame(now) {
  const dt = Math.min(0.1, (now - previous) / 1000);
  previous = now;
  clock += dt;

  if (source === 'simulation') {
    board.setRadarContacts(world.radarContacts());
    board.tick(now);
  }
  const state = getState();
  const ev = takeEvents(state);
  const stats = world.update(
    dt,
    clock,
    {
      mode: state.mode,
      ended: state.missionEnded,
      selfDestructing: state.selfDestructing,
      radar: state.radar,
      simRadar: source === 'simulation',
      giantHits: state.giant.hits,
      telemetry: state.telemetry,
    },
    ev
  );
  radar.draw(dt, stats, { alarm: state.mode === 'THE_LAST_OF_2028' || state.selfDestructing, active: !state.missionEnded });
  if (source === 'simulation') updateSimControls(state);
  bindHud(state, stats);
  const second = Math.floor(Date.now() / 1000);
  if (second !== clockSecond) {
    clockSecond = second;
    updateClock();
  }
  requestAnimationFrame(frame);
}

// Dev hooks: ?debug exposes internals on window.vesper, ?nohud hides the dashboard for clean captures.
if (params.has('debug')) window.vesper = { world, board, parser, getState, startSource };
if (params.has('nohud')) document.body.classList.add('nohud');
if (params.has('play')) $('intro').hidden = true;

startSource('simulation');
requestAnimationFrame(frame);
