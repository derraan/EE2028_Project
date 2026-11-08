/**
 * Virtual RD-03D + tracker + pan/laser — a browser stand-in for the firmware's
 * enhancement #5 chain:
 *   App/Src/radar_track.c   (nearest-threat tracker)
 *   App/Src/laser_aim.c     (pan slew + laser gate)
 *   App/Src/enhancements.c  (Radar_Update: mode gates + EVT RADAR cadence,
 *                            Cannon_OnFired: giant HP + cannon beam window)
 *
 * Giant cannon: the locked target is a giant (2 hits). The physical laser is
 * the cannon and lights only during a cannon beam window — gun shots (EVT FIRE)
 * no longer strobe it.
 *
 * Like sim.js it never touches the dashboard: it prints the exact EVT RADAR
 * lines the firmware prints and the shared parser does the rest.
 * Keep RADAR / AIM constants and the update order in sync with the C code.
 */

export const RADAR = Object.freeze({
  FRAME_PERIOD_MS: 100, // RD-03D reports ~10 Hz
  MAX_TARGETS: 3,
  MIN_RANGE_MM: 400,
  MAX_RANGE_MM: 6000,
  FOV_HALF_DEG: 60,
  CONFIRM_FRAMES: 3,
  LOST_TIMEOUT_MS: 700,
  GATE_MM: 800,
  SWITCH_MARGIN_MM: 600,
  SMOOTH_ALPHA: 0.5,
  EVT_PERIOD_MS: 250,
});

/** Giant cannon — mirrors EE_GIANT_HP / EE_CANNON_BEAM_MS in enhancements.h. */
export const GIANT = Object.freeze({
  HP: 2,
  BEAM_MS: 1000,
});

export const AIM = Object.freeze({
  PAN_LIMIT_DEG: 70,
  MAX_SLEW_DPS: 180,
  ALIGNED_TOL_DEG: 3,
  HOLD_MS: 1200,
  FIRE_STROBE_MS: 300,
  FIRE_STROBE_HALF_MS: 40,
  MAX_DT_S: 0.2,
});

const DEG = 180 / Math.PI;
const tenths = (v) => {
  const t = Math.max(-32767, Math.min(32767, v * 10));
  return Math.trunc(t < 0 ? t - 0.5 : t + 0.5);
};
const fmtTenths = (t) => `${t < 0 ? '-' : ''}${Math.trunc(Math.abs(t) / 10)}.${Math.abs(t) % 10}`;

/** Same line Telemetry_EvtRadar() writes. */
export function formatRadarLine({ az, range, lock, pan, laser, n }) {
  const r = Math.max(0, Math.min(65535, Math.trunc(range)));
  return `EVT RADAR az=${fmtTenths(tenths(az))} range_mm=${r} lock=${lock ? 1 : 0} pan=${fmtTenths(tenths(pan))} laser=${laser ? 1 : 0} n=${n}`;
}

function createTracker() {
  const t = { valid: false, locked: false, x: 0, y: 0, vx: 0, vy: 0, az: 0, range: 0, contacts: 0, lastHit: 0 };
  let hits = 0;
  let lockPending = false;
  let lostPending = false;

  const derive = () => {
    t.range = Math.hypot(t.x, t.y);
    t.az = Math.atan2(t.x, t.y) * DEG;
  };
  const countHit = (now) => {
    t.lastHit = now;
    hits = Math.min(255, hits + 1);
    if (!t.locked && hits >= RADAR.CONFIRM_FRAMES) {
      t.locked = true;
      lockPending = true;
    }
  };
  const drop = () => {
    if (t.locked) {
      lostPending = true;
      lockPending = false;
    }
    t.valid = t.locked = false;
    t.vx = t.vy = 0;
    hits = 0;
  };
  const start = (c, now) => {
    t.valid = true;
    t.locked = false;
    t.x = c.x;
    t.y = c.y;
    t.vx = t.vy = 0;
    hits = 0;
    derive();
    countHit(now);
  };

  return {
    t,
    reset() {
      drop(); // queues LOST if a target was locked (RadarTrack_Reset keeps it)
      lockPending = false;
      t.contacts = 0;
    },
    onFrame(targets, now) {
      const cand = [];
      let nearest = -1;
      for (const raw of targets) {
        if (raw.y <= 0 || (raw.x === 0 && raw.y === 0)) continue;
        const c = { x: raw.x, y: raw.y, range: Math.hypot(raw.x, raw.y), az: Math.atan2(raw.x, raw.y) * DEG };
        if (c.range < RADAR.MIN_RANGE_MM || c.range > RADAR.MAX_RANGE_MM || Math.abs(c.az) > RADAR.FOV_HALF_DEG) continue;
        if (nearest < 0 || c.range < cand[nearest].range) nearest = cand.length;
        cand.push(c);
      }
      t.contacts = cand.length;
      if (!cand.length) return;
      if (!t.valid) return start(cand[nearest], now);

      const dt = Math.min(0.5, (now - t.lastHit) / 1000);
      const px = t.x + t.vx * dt;
      const py = t.y + t.vy * dt;
      let assoc = -1;
      let best = 0;
      cand.forEach((c, i) => {
        const d = Math.hypot(c.x - px, c.y - py);
        if (assoc < 0 || d < best) {
          assoc = i;
          best = d;
        }
      });
      if (best > RADAR.GATE_MM) {
        if (!t.locked) start(cand[nearest], now);
        return;
      }
      if (nearest !== assoc && cand[nearest].range + RADAR.SWITCH_MARGIN_MM < cand[assoc].range) {
        drop();
        start(cand[nearest], now);
        return;
      }
      const c = cand[assoc];
      const a = RADAR.SMOOTH_ALPHA;
      const b = (a * a) / (2 - a);
      const rx = c.x - px;
      const ry = c.y - py;
      t.x = px + a * rx;
      t.y = py + a * ry;
      if (dt > 0.001) {
        t.vx += (b * rx) / dt;
        t.vy += (b * ry) / dt;
      }
      derive();
      countHit(now);
    },
    update(now) {
      if (t.valid && now - t.lastHit >= RADAR.LOST_TIMEOUT_MS) {
        drop();
        t.contacts = 0;
      }
    },
    poll() {
      if (lostPending) {
        lostPending = false;
        return 'LOST';
      }
      if (lockPending) {
        lockPending = false;
        return 'LOCK';
      }
      return null;
    },
  };
}

function createAim() {
  const a = { state: 'PARKED', cmd: 0, goal: 0, reachable: true, aligned: false, laser: false, firing: false, fireStart: 0, fireUntil: 0, lost: 0, last: null };
  return {
    a,
    safe() {
      a.laser = false;
      a.firing = false;
      a.goal = a.cmd = 0;
      a.aligned = false;
      a.state = 'SAFE';
    },
    fire(now) {
      a.firing = true;
      a.fireStart = now;
      a.fireUntil = now + AIM.FIRE_STROBE_MS;
    },
    update(tgt, allowTrack, allowLaser, now) {
      const dt = a.last === null ? 0 : Math.min(AIM.MAX_DT_S, (now - a.last) / 1000);
      a.last = now;
      if (!allowTrack) {
        a.state = 'SAFE';
        a.goal = 0;
        a.reachable = true;
      } else if (tgt.valid && tgt.locked) {
        const g = Math.atan2(tgt.x, tgt.y) * DEG;
        a.reachable = g >= -AIM.PAN_LIMIT_DEG && g <= AIM.PAN_LIMIT_DEG;
        a.goal = Math.max(-AIM.PAN_LIMIT_DEG, Math.min(AIM.PAN_LIMIT_DEG, g));
        a.state = 'TRACKING';
      } else if (a.state === 'TRACKING') {
        a.state = 'HOLDING';
        a.lost = now;
      } else if (a.state === 'HOLDING') {
        if (now - a.lost >= AIM.HOLD_MS) {
          a.state = 'PARKED';
          a.goal = 0;
          a.reachable = true;
        }
      } else {
        a.state = 'PARKED';
        a.goal = 0;
        a.reachable = true;
      }
      const err = a.goal - a.cmd;
      const step = AIM.MAX_SLEW_DPS * dt;
      a.cmd = err > step ? a.cmd + step : err < -step ? a.cmd - step : a.goal;
      a.aligned = Math.abs(a.goal - a.cmd) <= AIM.ALIGNED_TOL_DEG;
      const aimOk = allowLaser && a.state === 'TRACKING' && a.reachable && a.aligned;
      let on = aimOk;
      if (a.firing) {
        if (now < a.fireUntil) on = aimOk && (Math.floor((now - a.fireStart) / AIM.FIRE_STROBE_HALF_MS) & 1) === 0;
        else a.firing = false;
      }
      a.laser = on;
    },
  };
}

/**
 * @param {(line: string) => void} emit
 */
export function createRadarMirror(emit) {
  let tracker;
  let aim;
  let contacts = [];
  let nextFrame = 0;
  let evtAt = 0;
  // Giant cannon (Cannon_OnFired / Radar_Update in enhancements.c)
  let giantHits = 0;
  let beamOn = false;
  let beamUntil = 0;
  let downPending = false;
  let terminal = false;

  function reset(now = 0) {
    tracker = createTracker();
    aim = createAim();
    contacts = [];
    nextFrame = now;
    evtAt = now;
    giantHits = 0;
    beamOn = false;
    beamUntil = 0;
    downPending = false;
    terminal = false;
  }
  reset();

  function emitTrack() {
    const t = tracker.t;
    emit(formatRadarLine({ az: t.az, range: t.range, lock: t.locked, pan: aim.a.cmd, laser: aim.a.laser, n: t.contacts }));
  }

  return {
    reset,
    /** Returns in radar mm (x lateral +right, y forward). The module reports the 3 strongest; we keep the 3 nearest. */
    setContacts(list) {
      contacts = (list || [])
        .map((c) => ({ x: Math.round(c.x), y: Math.round(c.y) }))
        .sort((p, q) => Math.hypot(p.x, p.y) - Math.hypot(q.x, q.y))
        .slice(0, RADAR.MAX_TARGETS);
    },
    /** Enhancements_CannonReady(): a giant is locked and self-destruct has not started. */
    cannonReady() {
      return tracker.t.locked && !terminal;
    },
    /** Cannon_OnFired(): main.c already spent the full charge. Prints the same EVT GIANT lines. */
    cannon(now) {
      if (!tracker.t.locked) {
        emit('Giant cannon missed - the target moved out of lock.');
        emit('EVT GIANT miss');
        return;
      }
      beamOn = true;
      beamUntil = now + GIANT.BEAM_MS;
      giantHits += 1;
      if (giantHits >= GIANT.HP) {
        giantHits = 0;
        downPending = true;
        emit('GIANT DOWN! The giant collapses.');
        emit('EVT GIANT down');
      } else {
        emit(`Giant hit! (${giantHits}/${GIANT.HP}) - recharge to 50/50 to finish it.`);
        emit(`EVT GIANT hit=${giantHits}/${GIANT.HP}`);
      }
    },
    /** Mirror of Radar_Update(mode, now) in enhancements.c. */
    tick(now, mode, isTerminal) {
      terminal = Boolean(isTerminal);
      const allowTrack = (mode === 'STANDBY_MODE' || mode === 'BATTLE_MODE') && !terminal;
      // Cannon beam window; after "giant down" drop the lock → next lock is a fresh giant.
      if (beamOn && (now >= beamUntil || !allowTrack)) {
        beamOn = false;
        if (downPending) {
          downPending = false;
          tracker.reset();
        }
      }
      const allowLaser = mode === 'BATTLE_MODE' && allowTrack && beamOn;
      while (now >= nextFrame) {
        if (allowTrack) tracker.onFrame(contacts, nextFrame);
        nextFrame += RADAR.FRAME_PERIOD_MS;
      }
      if (!allowTrack) tracker.reset();
      tracker.update(now);
      aim.update(tracker.t, allowTrack, allowLaser, now);
      let evt;
      while ((evt = tracker.poll())) {
        giantHits = 0; // new or lost lock: a different giant
        if (evt === 'LOCK') {
          emitTrack();
          evtAt = now;
        } else emit('EVT RADAR lost');
      }
      if (tracker.t.locked && now - evtAt >= RADAR.EVT_PERIOD_MS) {
        evtAt = now;
        emitTrack();
      }
    },
    get snapshot() {
      return { ...tracker.t, pan: aim.a.cmd, laser: aim.a.laser, aim: aim.a.state, giantHits, beamOn };
    },
  };
}
