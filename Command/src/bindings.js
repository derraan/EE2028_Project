/**
 * HUD bindings — push shared parser state into the VESPER page.
 *
 * Everything here reads `state` from parser.js. Nothing in the HUD decides
 * that the drone fired, rescued or self-destructed; it only shows what the
 * firmware (or the virtual board, which prints the same lines) reported.
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
  uart: 'UPLINK ESTABLISHED',
  severed: 'LINK LOST',
};

const SENSOR_ROWS = [
  ['Temperature', (t) => fmt(t.T, t.units.T)],
  ['Pressure', (t) => fmt(t.P, t.units.P)],
  ['Humidity', (t) => fmt(t.H, t.units.H)],
  ['Acceleration', (t) => vec(t.Accel, t.units.Accel)],
  ['Angular rate', (t) => vec(t.Gyro, t.units.Gyro)],
  ['Magnetic field', (t) => vec(t.Magneto, t.units.Magneto)],
];

function fmt(n, unit) {
  return n == null ? '—' : `${Number(n).toFixed(1)} ${unit}`;
}

function vec(v, unit) {
  if (!v || v[0] == null) return '—';
  return `[${v.map((n) => Number(n).toFixed(1)).join(', ')}] ${unit}`;
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

function objective(state, scene) {
  if (state.missionEnded) return ['Transmission ended', 'Reset the mission to fly again.'];
  if (state.selfDestructing) return ['Self-destruct sequence', 'The drone is making its final sacrifice.'];
  switch (state.mode) {
    case 'THE_LAST_OF_2028':
      return [
        'The Last of EE2028',
        `Flip the drone upright, then double press PB to rescue. Failed rescues: ${state.rescueAttempts}/5.`,
      ];
    case 'BATTLE_MODE':
      if (scene.contact) {
        return [
          'Defend the laboratory',
          state.armed
            ? 'Armed. Hold PB ≥0.5 s to fire full-auto (−5 per shot).'
            : 'Tap PB to charge (+3). Fire needs ≥5/10.',
        ];
      }
      return scene.secured
        ? ['Perimeter secured', 'Double press PB to stand down to Standby.']
        : ['Hold the perimeter', 'Tap PB to charge. Double press to return to Standby.'];
    default:
      return scene.secured
        ? ['The lab is still standing', 'Telemetry nominal. Keep watch over E4-03-07.']
        : ['Scout for a human habitat', 'Telemetry streams every second. Double press PB to enter Battle.'];
  }
}

let renderedLines = -1;
let renderedTelemetryAt = undefined;

/** Force the log / sensor panels to redraw on the next bind (after a reset). */
export function invalidateHud() {
  renderedLines = -1;
  renderedTelemetryAt = undefined;
}

/**
 * @param {object} state parser state
 * @param {{ contact: boolean, secured: boolean, source: string, board?: object }} scene
 */
export function bindHud(state, scene) {
  const now = Date.now();
  const emergency = state.mode === 'THE_LAST_OF_2028' && !state.missionEnded;

  // Link
  const link = state.missionEnded ? 'MISSION TERMINATED' : LINK_LABEL[state.link] || state.link.toUpperCase();
  setText('connection', link);
  $('connection').dataset.link = state.missionEnded ? 'ended' : state.link;

  // Mode + board LED (Standby steady, Battle 1 Hz, Last 2 Hz)
  setText('mode', MODE_LABEL[state.mode] || state.mode);
  const led = state.missionEnded ? 'off' : emergency ? 'fast' : state.mode === 'BATTLE_MODE' ? 'battle' : 'standby';
  if ($('boardLed').className !== led) $('boardLed').className = led;
  setText(
    'ledPattern',
    { off: 'LED · off', fast: 'LED · 2 Hz', battle: 'LED · 1 Hz', standby: 'LED · steady on' }[led]
  );

  // Charge
  setText('charge', `${state.charge} / 10`);
  const bars = $('chargeBars');
  if (bars.children.length !== 10) {
    bars.replaceChildren(...Array.from({ length: 10 }, () => document.createElement('i')));
  }
  [...bars.children].forEach((bar, i) => bar.classList.toggle('on', i < state.charge));
  setText('armed', state.armed ? (state.mode === 'BATTLE_MODE' ? 'Armed · hold PB to fire' : 'Charged ≥5/10') : 'Not armed · needs ≥5/10');

  // Objective
  const [title, hint] = objective(state, scene);
  setText('objective', title);
  setText('hint', hint);
  setText('coordinates', scene.contact ? 'HOSTILE CONTACT / SIMULATED' : scene.secured ? 'PERIMETER / SECURED' : 'PERIMETER / CLEAR');

  // Emergency overlay
  $('emergency').hidden = !emergency && !state.selfDestructing;
  const remaining = liveCountdownMs(state, now);
  setText(
    'countdown',
    state.selfDestructing ? 'SELF-DESTRUCT' : remaining == null ? 'AWAITING EVENT' : (remaining / 1000).toFixed(1)
  );
  setText(
    'emergencyHint',
    state.selfDestructing
      ? `Reason: ${state.lastSelfDestructReason}.`
      : `Flip the drone upright, then double press PB to rescue. Failed rescues ${state.rescueAttempts}/5.`
  );

  // Ending overlay
  $('ending').hidden = !state.missionEnded;
  if (state.missionEnded) {
    setText('endingLabel', 'MISSION RECORD CLOSED');
    setText('endingTitle', 'The last signal.');
    setText(
      'endingText',
      `The drone’s final transmission has been recorded at E4-03-07${
        state.lastSelfDestructReason ? ` (${state.lastSelfDestructReason.replace('_', ' ')})` : ''
      }. Cheerio!`
    );
  }

  // Sample age (Standby-only telemetry → stale is normal in Battle)
  if (state.telemetryAt) {
    const age = Math.floor((now - state.telemetryAt) / 1000);
    setText('sampleAge', `Last sample ${age}s ago${age > 3 ? ' · STALE' : ''}`);
  } else {
    setText('sampleAge', 'No sample');
  }

  // Sensors
  if (renderedTelemetryAt !== state.telemetryAt) {
    renderedTelemetryAt = state.telemetryAt;
    $('sensors').replaceChildren(
      ...SENSOR_ROWS.map(([name, read]) => {
        const card = document.createElement('div');
        const label = document.createElement('span');
        const value = document.createElement('strong');
        label.textContent = name;
        value.textContent = read(state.telemetry);
        card.append(label, value);
        return card;
      })
    );
  }
  setText(
    'warning',
    state.thresholds.ok ? 'No active warning reported.' : state.thresholds.lines.slice(-1)[0] || 'Sensor threshold violation.'
  );
  $('warning').classList.toggle('alert', !state.thresholds.ok);

  // Transmission log — the raw UART lines, newest first.
  if (renderedLines !== state.lineCount) {
    renderedLines = state.lineCount;
    const items = state.rawLog
      .slice(-80)
      .reverse()
      .map((line) => {
        const li = document.createElement('li');
        if (/^EVT\s/.test(line)) li.className = 'evt';
        li.textContent = line;
        return li;
      });
    $('log').replaceChildren(...items);
    setText('eventCount', `${state.lineCount} LINES`);
  }
}
