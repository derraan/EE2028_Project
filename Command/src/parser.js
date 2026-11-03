/**
 * One ingestion parser for live UART chunks and recorded mission fixtures.
 * Buffer to newline → parse sensor + known EVT; unrecognized → raw log.
 */

const SENSOR_RE =
  /^Telemetry\s*->\s*T:([-\d.]+)\s*°?\s*C,\s*P:([-\d.]+)\s*hPa,\s*H:([-\d.]+)\s*%\s*\|\s*Accel:\[([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\]\s*m\/s\^2\s*\|\s*Gyro:\[([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\]\s*dps\s*\|\s*Magneto:\[([-\d.]+),\s*([-\d.]+),\s*([-\d.]+)\]\s*mgauss/i;

/** EVT NAME  |  EVT NAME=value  |  EVT NAME key=value … */
const EVT_RE = /^EVT\s+(\w+)\s*(.*)$/i;
const KV_RE = /(\w+)=([^\s]+)/g;

const MODE_ALIASES = {
  STANDBY_MODE: 'STANDBY_MODE',
  STANDBY: 'STANDBY_MODE',
  BATTLE_MODE: 'BATTLE_MODE',
  BATTLE: 'BATTLE_MODE',
  THE_LAST_OF_2028: 'THE_LAST_OF_2028',
  THE_LAST_OF_EE2028: 'THE_LAST_OF_2028',
  LAST_OF_2028: 'THE_LAST_OF_2028',
  TEST_OF_2028: 'THE_LAST_OF_2028',
};

export const UNITS = {
  T: '°C',
  P: 'hPa',
  H: '%',
  Accel: 'm/s^2',
  Gyro: 'dps',
  Magneto: 'mgauss',
};

export function createEmptyState() {
  return {
    link: 'idle', // idle | sim | uart | replay | severed
    mode: 'STANDBY_MODE',
    missionEnded: false,
    uptimeMs: 0,
    telemetry: {
      T: null,
      P: null,
      H: null,
      Accel: [null, null, null],
      Gyro: [null, null, null],
      Magneto: [null, null, null],
      units: { ...UNITS },
    },
    thresholds: { ok: true, lines: [] },
    charge: 0,
    armed: false,
    countdownMs: null,
    /** Date.now() when countdownMs was last reported (for smooth local interpolation). */
    countdownAt: null,
    lastSelfDestructReason: null,
    selfDestructing: false,
    rescueAttempts: 0,
    /** Monotonic counters so views can react to discrete events without diffing logs. */
    fireSeq: 0,
    attackSeq: 0,
    tripSeq: 0,
    rescueSeq: 0,
    /** Date.now() of the last parsed Telemetry -> line. */
    telemetryAt: null,
    ir: false,
    radarAz: null,
    events: [],
    rawLog: [],
    /** Total non-empty lines ingested (rawLog is capped; this is not). */
    lineCount: 0,
  };
}

function pushEvent(state, text) {
  state.events.unshift({ t: Date.now(), text });
  if (state.events.length > 80) state.events.length = 80;
}

function parseKv(rest) {
  const out = {};
  if (!rest) return out;
  let m;
  KV_RE.lastIndex = 0;
  while ((m = KV_RE.exec(rest)) !== null) {
    out[m[1].toLowerCase()] = m[2];
  }
  // Bare value forms: EVT CHARGE=6/10 already covered; EVT COUNTDOWN=7 legacy
  if (Object.keys(out).length === 0 && rest.includes('=')) {
    const [k, v] = rest.split('=', 2);
    out[k.trim().toLowerCase()] = v.trim();
  }
  return out;
}

function parseChargeToken(tok) {
  if (!tok) return null;
  const m = String(tok).match(/(\d+)\s*\/\s*10/);
  return m ? Math.min(10, Math.max(0, Number(m[1]))) : null;
}

function normalizeMode(tag) {
  if (!tag) return null;
  const key = String(tag).trim().toUpperCase().replace(/\s+/g, '_');
  return MODE_ALIASES[key] || key;
}

function applyCharge(state, n) {
  if (n == null || Number.isNaN(n)) return;
  state.charge = n;
  state.armed = n >= 5;
}

/**
 * Parse one complete line (no trailing newline). Mutates state. Returns kind.
 */
export function parseLine(state, line) {
  const raw = String(line ?? '').replace(/\r$/, '');
  const trimmed = raw.trim();
  if (!trimmed || trimmed.startsWith('#')) {
    return { kind: 'skip' };
  }

  state.rawLog.push(raw);
  state.lineCount += 1;
  if (state.rawLog.length > 500) state.rawLog.shift();

  if (state.missionEnded) {
    // Still ingest for raw log / Cheerio!; do not change mission fields.
    pushEvent(state, raw);
    return { kind: 'post_terminate', text: raw };
  }

  const sensor = SENSOR_RE.exec(trimmed);
  if (sensor) {
    state.telemetry.T = Number(sensor[1]);
    state.telemetry.P = Number(sensor[2]);
    state.telemetry.H = Number(sensor[3]);
    state.telemetry.Accel = [Number(sensor[4]), Number(sensor[5]), Number(sensor[6])];
    state.telemetry.Gyro = [Number(sensor[7]), Number(sensor[8]), Number(sensor[9])];
    state.telemetry.Magneto = [Number(sensor[10]), Number(sensor[11]), Number(sensor[12])];
    state.telemetryAt = Date.now();
    // Resuming normal telemetry implies thresholds cleared unless sticky warn set.
    if (!state.thresholds.ok && state.thresholds.lines.length) {
      state.thresholds.ok = true;
      state.thresholds.lines = [];
    }
    return { kind: 'sensor', telemetry: state.telemetry };
  }

  if (/exceeds threshold|drops below threshold/i.test(trimmed)) {
    state.thresholds.ok = false;
    state.thresholds.lines.push(trimmed);
    if (state.thresholds.lines.length > 24) state.thresholds.lines.shift();
    pushEvent(state, trimmed);
    return { kind: 'threshold', text: trimmed };
  }

  const evt = EVT_RE.exec(trimmed);
  if (evt) {
    const name = evt[1].toUpperCase();
    let rest = (evt[2] || '').trim();
    // Normalize "EVT FOO=bar" (no space) into parseable kv / bare value.
    if (rest.startsWith('=')) rest = rest.slice(1).trim();
    const kv = parseKv(rest.includes('=') ? rest : '');
    const bare = !rest.includes('=') ? rest : null;
    switch (name) {
      case 'MODE': {
        const mode = normalizeMode(kv.mode || bare || rest);
        if (mode) state.mode = mode;
        if (state.mode !== 'THE_LAST_OF_2028') {
          state.countdownMs = null;
          state.countdownAt = null;
          state.rescueAttempts = 0;
        }
        pushEvent(state, `MODE ${state.mode}`);
        return { kind: 'evt', name, mode: state.mode };
      }
      case 'CHARGE': {
        const n = parseChargeToken(kv.charge || bare || rest || Object.values(kv)[0]);
        applyCharge(state, n);
        pushEvent(state, `CHARGE ${state.charge}/10`);
        return { kind: 'evt', name, charge: state.charge };
      }
      case 'FIRE': {
        const n = parseChargeToken(kv.charge || bare || rest);
        applyCharge(state, n);
        state.fireSeq += 1;
        pushEvent(state, `FIRE → ${state.charge}/10`);
        return { kind: 'evt', name, charge: state.charge };
      }
      case 'COUNTDOWN': {
        let ms = null;
        if (kv.remaining_ms != null) ms = Number(kv.remaining_ms);
        else if (kv.countdown != null) ms = Number(kv.countdown) * 1000;
        else if (bare && /^\d+$/.test(bare)) ms = Number(bare) * 1000;
        state.countdownMs = Number.isFinite(ms) ? ms : null;
        state.countdownAt = state.countdownMs == null ? null : Date.now();
        pushEvent(state, `COUNTDOWN ${state.countdownMs ?? '?'} ms`);
        return { kind: 'evt', name, remaining_ms: state.countdownMs };
      }
      case 'SELF_DESTRUCT': {
        state.lastSelfDestructReason = kv.reason || bare || 'unknown';
        state.selfDestructing = true;
        state.countdownMs = 0;
        state.countdownAt = Date.now();
        pushEvent(state, `SELF_DESTRUCT ${state.lastSelfDestructReason}`);
        return { kind: 'evt', name, reason: state.lastSelfDestructReason };
      }
      case 'TERMINATE': {
        state.missionEnded = true;
        state.mode = 'TERMINATED';
        state.selfDestructing = false;
        state.countdownMs = null;
        state.countdownAt = null;
        pushEvent(state, 'TERMINATE');
        return { kind: 'evt', name };
      }
      case 'THRESHOLD_OK': {
        state.thresholds.ok = true;
        state.thresholds.lines = [];
        pushEvent(state, 'THRESHOLD_OK');
        return { kind: 'evt', name };
      }
      case 'RESCUE': {
        state.countdownMs = null;
        state.countdownAt = null;
        state.rescueAttempts = 0;
        state.rescueSeq += 1;
        pushEvent(state, 'RESCUE');
        return { kind: 'evt', name };
      }
      case 'ATTACK': {
        state.attackSeq += 1;
        pushEvent(state, 'ATTACK');
        return { kind: 'evt', name };
      }
      case 'IR': {
        state.ir = kv.ir === '1' || kv.ir === 'true';
        pushEvent(state, `IR ${state.ir ? 1 : 0}`);
        return { kind: 'evt', name, ir: state.ir };
      }
      case 'RADAR': {
        state.radarAz = kv.az != null ? Number(kv.az) : null;
        pushEvent(state, `RADAR az=${state.radarAz}`);
        return { kind: 'evt', name, az: state.radarAz };
      }
      default:
        pushEvent(state, trimmed);
        return { kind: 'evt_unknown', name, text: trimmed };
    }
  }

  // Human prose fallbacks (PR #3) — do not require EVT to track mode when present.
  if (/Critical T\/A\/G threshold violated/i.test(trimmed)) {
    state.tripSeq += 1;
    state.mode = 'BATTLE_MODE';
    pushEvent(state, trimmed);
    return { kind: 'prose', mode: state.mode, trip: true };
  }
  const attempt = /^Rescue attempt (\d+)\s*\/\s*5/i.exec(trimmed);
  if (attempt) {
    state.rescueAttempts = Number(attempt[1]);
    pushEvent(state, trimmed);
    return { kind: 'prose', rescueAttempts: state.rescueAttempts };
  }
  if (/Entering BATTLE MODE/i.test(trimmed) || /-> Entering BATTLE MODE/i.test(trimmed)) {
    state.mode = 'BATTLE_MODE';
    pushEvent(state, trimmed);
    return { kind: 'prose', mode: state.mode };
  }
  if (/Returning to STANDBY MODE|Entering STANDBY MODE/i.test(trimmed)) {
    state.mode = 'STANDBY_MODE';
    pushEvent(state, trimmed);
    return { kind: 'prose', mode: state.mode };
  }
  if (/THE LAST OF EE2028|THE_LAST_OF_2028|LAST OF 2028/i.test(trimmed)) {
    state.mode = 'THE_LAST_OF_2028';
    pushEvent(state, trimmed);
    return { kind: 'prose', mode: state.mode };
  }
  if (/Successfully reverted to Battle Mode/i.test(trimmed)) {
    state.mode = 'BATTLE_MODE';
    state.countdownMs = null;
    state.countdownAt = null;
    pushEvent(state, trimmed);
    return { kind: 'prose', mode: state.mode };
  }
  if (/^Cheerio!/i.test(trimmed)) {
    pushEvent(state, trimmed);
    return { kind: 'cheerio' };
  }

  pushEvent(state, trimmed);
  return { kind: 'raw', text: trimmed };
}

/**
 * Incremental chunk feeder (Web Serial / bridge).
 */
export function createLineBuffer(onLine) {
  let buf = '';
  return {
    push(chunk) {
      buf += String(chunk ?? '').replace(/\r\n/g, '\n').replace(/\r/g, '\n');
      let idx;
      while ((idx = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, idx);
        buf = buf.slice(idx + 1);
        onLine(line);
      }
    },
    flush() {
      if (buf.length) {
        const line = buf;
        buf = '';
        onLine(line);
      }
    },
    reset() {
      buf = '';
    },
  };
}

export function createParser(initial) {
  const state = initial || createEmptyState();
  const buffer = createLineBuffer((line) => parseLine(state, line));
  return {
    state,
    pushChunk(chunk) {
      buffer.push(chunk);
      return state;
    },
    pushLine(line) {
      parseLine(state, line);
      return state;
    },
    reset() {
      Object.assign(state, createEmptyState());
      buffer.reset();
      return state;
    },
    markLink(link) {
      state.link = link;
      return state;
    },
    markDisconnect() {
      // Cable loss ≠ mission terminate
      state.link = 'severed';
      return state;
    },
  };
}
