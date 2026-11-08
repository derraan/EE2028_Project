/**
 * HUD bindings — push shared parser state (and world fiction stats) into the
 * VESPER dashboard.
 *
 * Drone fields (mode, LED, charge, countdown, sensors, log) read only from
 * parser state. Threat fields (infected, kills, waves) are game fiction from
 * the world and are labelled as simulated in the page.
 */

const $ = (id) => document.getElementById(id);

/** Display names. Enum stays THE_LAST_OF_2028; human banner is THE LAST OF EE2028. */
export const MODE_LABEL = {
  STANDBY_MODE: 'STANDBY',
  BATTLE_MODE: 'BATTLE',
  THE_LAST_OF_2028: 'THE LAST OF EE2028',
  TERMINATED: 'TERMINATED',
};

const LINK_LABEL = {
  idle: 'AWAITING SIGNAL',
  sim: 'VIRTUAL BOARD',
  replay: 'UART REPLAY',
  uart: 'UPLINK LIVE',
  severed: 'LINK LOST',
};

const SENSORS = [
  ['T', 'Temperature', (t) => fmt(t.T, t.units.T), (t) => t.T],
  ['P', 'Pressure', (t) => fmt(t.P, t.units.P), (t) => t.P],
  ['H', 'Humidity', (t) => fmt(t.H, t.units.H), (t) => t.H],
  ['A', 'Accel', (t) => vec(t.Accel, t.units.Accel), (t) => mag(t.Accel)],
  ['G', 'Gyro', (t) => vec(t.Gyro, t.units.Gyro), (t) => mag(t.Gyro)],
  ['M', 'Magneto', (t) => vec(t.Magneto, t.units.Magneto), (t) => mag(t.Magneto)],
];
const HISTORY = 40;
const history = Object.fromEntries(SENSORS.map(([k]) => [k, []]));

function fmt(n, unit) {
  return n == null ? '—' : `${Number(n).toFixed(1)} ${unit}`;
}
function vec(v, unit) {
  if (!v || v[0] == null) return '—';
  return `${v.map((n) => Number(n).toFixed(1)).join(', ')} ${unit}`;
}
function mag(v) {
  return v && v[0] != null ? Math.hypot(v[0], v[1], v[2]) : null;
}
function setText(id, text) {
  const el = $(id);
  if (el && el.textContent !== text) el.textContent = text;
}

/** Remaining rescue window in ms, interpolated between 1 Hz COUNTDOWN events. */
export function liveCountdownMs(state, now = Date.now()) {
  if (state.countdownMs == null || state.countdownAt == null) return null;
  return Math.max(0, state.countdownMs - (now - state.countdownAt));
}

function clock(ms) {
  const s = ms / 1000;
  const m = Math.floor(s / 60);
  return `${String(m).padStart(2, '0')}:${(s - m * 60).toFixed(1).padStart(4, '0')}`;
}

function objective(state, world) {
  if (state.missionEnded) return ['Transmission ended', 'Reboot the drone to defend E4-03-07 again.'];
  if (state.selfDestructing) return ['Self-destruct sequence', 'VESPER-07 is making its final sacrifice for mankind.'];
  const infected = world.infected;
  switch (state.mode) {
    case 'THE_LAST_OF_2028':
      return ['Survive: the drone is down', `The infected are swarming it. Flip it upright, then double press PB. Failed rescues ${state.rescueAttempts}/5.`];
    case 'BATTLE_MODE':
      if (infected > 0) {
        return [
          `Defend the lab — ${infected} infected`,
          state.armed ? 'Laser armed. Hold PB ≥ 0.5 s — every shot vaporises the closest threat.' : 'Tap PB to charge the laser (+3 per tap). Firing needs 25/50.',
        ];
      }
      return world.kills > 0
        ? ['Perimeter holding', 'More spore pods are inbound. Keep charge at 25/50 or more, or double press PB to stand down.']
        : ['Weapons free', 'Tap PB to charge. Spore pods are falling on the field.'];
    default:
      if (infected > 0) return ['Contact on the field', `${infected} infected approaching E4-03-07. Double press PB to enter Battle.`];
      return world.kills > 0
        ? ['The lab still stands', 'Telemetry nominal. Watch the sky — the next pod is never far.']
        : ['Scout the perimeter', 'Telemetry streams every second in Standby. Double press PB to arm VESPER-07.'];
  }
}

let renderedLines = -1;
let renderedTelemetryAt;

/** Force the log / sensor panels to redraw on the next bind (after a reset). */
export function invalidateHud() {
  renderedLines = -1;
  renderedTelemetryAt = undefined;
  for (const k of Object.keys(history)) history[k].length = 0;
}

function sparkline(values) {
  const pts = values.filter((v) => v != null);
  if (pts.length < 2) return '';
  const min = Math.min(...pts);
  const max = Math.max(...pts);
  const span = max - min || 1;
  return pts.map((v, i) => `${((i / (pts.length - 1)) * 100).toFixed(1)},${(22 - ((v - min) / span) * 20).toFixed(1)}`).join(' ');
}

/**
 * @param {object} state parser state
 * @param {{infected:number,kills:number,wave:number,nearest:null|{dist:number,bearing:number}}} world fiction stats
 */
export function bindHud(state, world) {
  const now = Date.now();
  const app = $('app');
  const emergency = state.mode === 'THE_LAST_OF_2028' && !state.missionEnded;
  const alarm = emergency || state.selfDestructing;
  if (app.dataset.mode !== state.mode) app.dataset.mode = state.mode;
  const alarmFlag = alarm ? '1' : '0';
  if (app.dataset.alarm !== alarmFlag) app.dataset.alarm = alarmFlag;

  // Link
  setText('connection', state.missionEnded ? 'MISSION TERMINATED' : LINK_LABEL[state.link] || state.link.toUpperCase());
  $('connection').dataset.link = state.missionEnded ? 'ended' : state.link;

  // Mode + board LED (Standby steady, Battle 1 Hz, Last 2 Hz)
  setText('mode', MODE_LABEL[state.mode] || state.mode);
  setText('modeRaw', state.mode);
  const led = state.missionEnded ? 'off' : emergency ? 'fast' : state.mode === 'BATTLE_MODE' ? 'battle' : 'standby';
  const ledClass = `mode-led ${led}`;
  if ($('boardLed').className !== ledClass) $('boardLed').className = ledClass;
  setText('ledPattern', { off: 'LED · OFF', fast: 'LED · 2 HZ', battle: 'LED · 1 HZ', standby: 'LED · STEADY ON' }[led]);
  const az = state.telemetry.Accel?.[2];
  setText('attitude', emergency || state.selfDestructing ? 'Inverted' : az == null ? '—' : az < -4 ? 'Inverted' : 'Upright');

  // Laser core
  $('charge').firstChild.nodeValue = String(state.charge);
  $('chargeFill').style.width = `${(state.charge / 50) * 100}%`;
  $('chargeMeter').setAttribute('aria-valuenow', String(state.charge));
  const armed = $('armed');
  const armedText = state.armed ? (state.mode === 'BATTLE_MODE' ? 'ARMED' : 'CHARGED') : state.mode === 'BATTLE_MODE' ? 'CHARGING' : 'SAFE';
  setText('armed', armedText);
  armed.classList.toggle('on', state.armed && state.mode === 'BATTLE_MODE');

  // Mission
  const [title, hint] = objective(state, world);
  setText('objective', title);
  setText('hint', hint);

  // Threat (fiction)
  setText('infected', String(world.infected));
  setText('kills', String(world.kills));
  setText('wave', String(world.wave));
  // RD-03D lock is firmware-reported (EVT RADAR); the lock is a GIANT. HP and
  // cannon readiness are firmware too (EVT GIANT, charge ≥ 10 in Battle).
  const rd = state.radar;
  const hp = state.giant?.hp ?? 2;
  const hpLeft = Math.max(0, hp - (state.giant?.hits ?? 0));
  const cannonReady = state.mode === 'BATTLE_MODE' && state.charge >= 10;
  const radarLine =
    rd && rd.lock && rd.az != null
      ? `GIANT ${'█'.repeat(hpLeft)}${'░'.repeat(hp - hpLeft)} ${rd.az > 0 ? '+' : ''}${rd.az.toFixed(1)}° · ${
          rd.rangeMm != null ? (rd.rangeMm / 1000).toFixed(1) : '?'
        } m · ${rd.laser ? 'CANNON FIRING' : cannonReady ? 'CANNON READY · HOLD TO FIRE' : state.mode === 'BATTLE_MODE' ? 'CANNON NEEDS 10 CHARGE' : 'TRACKING'}`
      : null;
  setText(
    'nearest',
    radarLine ||
      (world.nearest ? `NEAREST ${world.nearest.dist.toFixed(0)} m · BRG ${String(Math.round(world.nearest.bearing)).padStart(3, '0')}°` : 'NO CONTACTS')
  );
  $('nearest').classList.toggle('lock', Boolean(radarLine));
  setText('radarTag', radarLine ? 'GIANT LOCK' : 'SIMULATED');
  setText('giantKills', String(state.giantKills ?? 0));

  // Emergency overlay
  $('emergency').hidden = !alarm || state.missionEnded;
  if (alarm) {
    const remaining = liveCountdownMs(state, now);
    setText('emergencyLabel', state.selfDestructing ? 'SELF-DESTRUCT' : 'EMERGENCY');
    setText('countdown', state.selfDestructing ? 'CHEERIO' : remaining == null ? '--:--.-' : clock(remaining));
    [...$('rescuePips').children].forEach((pip, i) => pip.classList.toggle('on', i < state.rescueAttempts));
    setText(
      'emergencyHint',
      state.selfDestructing
        ? `Reason: ${String(state.lastSelfDestructReason).replace('_', ' ')}.`
        : 'Flip the drone upright, then double press PB to rescue.'
    );
  }

  // Ending overlay
  $('ending').hidden = !state.missionEnded;
  if (state.missionEnded) {
    const reason = state.lastSelfDestructReason;
    setText('endingText', `VESPER-07's final transmission was recorded at E4-03-07. The laboratory remembers. Cheerio!`);
    setText('endKills', String(world.kills));
    setText('endWaves', String(world.wave));
    setText('endCause', reason === 'rescue_limit' ? 'Rescue limit' : reason === 'timeout' ? '10 s timeout' : reason ? String(reason) : '—');
  }

  // Telemetry
  if (state.telemetryAt) {
    const age = Math.floor((now - state.telemetryAt) / 1000);
    setText('sampleAge', age > 3 ? `STALE ${age}s` : `LIVE ${age}s`);
    $('sampleAge').classList.toggle('stale', age > 3);
  } else {
    setText('sampleAge', 'NO SAMPLE');
  }
  if (renderedTelemetryAt !== state.telemetryAt) {
    renderedTelemetryAt = state.telemetryAt;
    if (state.telemetryAt) {
      for (const [k, , , read] of SENSORS) {
        history[k].push(read(state.telemetry));
        if (history[k].length > HISTORY) history[k].shift();
      }
    }
    $('sensors').replaceChildren(
      ...SENSORS.map(([k, name, text]) => {
        const row = document.createElement('div');
        row.className = 'sensor';
        const label = document.createElement('span');
        label.textContent = name;
        const value = document.createElement('strong');
        value.textContent = text(state.telemetry);
        const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('viewBox', '0 0 100 24');
        svg.setAttribute('preserveAspectRatio', 'none');
        svg.setAttribute('aria-hidden', 'true');
        const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
        line.setAttribute('points', sparkline(history[k]));
        svg.append(line);
        row.append(label, svg, value);
        return row;
      })
    );
  }
  setText('warning', state.thresholds.ok ? 'All sensors within thresholds.' : state.thresholds.lines.slice(-1)[0] || 'Sensor threshold violation.');
  $('warning').classList.toggle('alert', !state.thresholds.ok);

  // UART terminal — raw lines, newest first
  if (renderedLines !== state.lineCount) {
    renderedLines = state.lineCount;
    const items = state.rawLog
      .slice(-120)
      .reverse()
      .map((line) => {
        const li = document.createElement('li');
        if (/^EVT\s/.test(line)) li.className = 'evt';
        else if (/attacked|self-destruct|upside down|Cheerio|threshold/i.test(line)) li.className = 'alert';
        li.textContent = line;
        return li;
      });
    $('log').replaceChildren(...items);
    setText('eventCount', `${state.lineCount} LINES`);
  }
}
