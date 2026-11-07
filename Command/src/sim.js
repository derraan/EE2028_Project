/**
 * Virtual board — a browser stand-in for Core/Src/main.c.
 *
 * It does not drive the dashboard directly. It prints the same UART lines the
 * firmware prints (prose + EVT) and hands them to `emit`, which main.js feeds
 * into the shared parser. Simulation, fixture replay and Web Serial therefore
 * all render through one path, and the game can never show something the
 * firmware would not have reported.
 *
 * Rules mirrored from main.c (keep in sync when the firmware changes):
 *  - PB: 500 ms double-press window, hold ≥500 ms latches full-auto fire.
 *  - Standby: telemetry every 1 s; T/A/G trip → Battle; single press ignored.
 *  - Battle: single press +3 charge (cap 10) only while charge < 5;
 *    hold fires every 500 ms, −5 per shot, needs ≥5/10.
 *  - Upside-down (accel z < −4) in Standby/Battle → THE_LAST_OF_2028.
 *  - THE_LAST_OF_2028: ATTACK + COUNTDOWN every 1 s; upright double-press
 *    rescues to Battle; 5 failed upside-down rescues or 10 s → self-destruct.
 *  - Self-destruct: sacrifice line, then 1 s later EVT TERMINATE + Cheerio!
 *
 * Enhancement #5 (RD-03D radar + pan laser) runs after the state machine each
 * tick, exactly like Enhancements_Tick() in main.c — see radar_mirror.js.
 * Its contacts are whatever the caller places in front of the virtual radar.
 */
import { createRadarMirror } from './radar_mirror.js';

export const SIM = Object.freeze({
  DOUBLE_PRESS_MS: 500,
  HOLD_MS: 500,
  FIRE_PERIOD_MS: 500,
  TELEMETRY_PERIOD_MS: 1000,
  ATTACK_PERIOD_MS: 1000,
  RESCUE_TIMEOUT_MS: 10000,
  RESCUE_ATTEMPT_LIMIT: 5,
  TERMINATE_DELAY_MS: 1000,
});

const STANDBY = 'STANDBY_MODE';
const BATTLE = 'BATTLE_MODE';
const LAST = 'THE_LAST_OF_2028';
const SD_TRIGGERED = 'SELF_DESTRUCT_TRIGGERED';
const SD_DEAD = 'SELF_DESTRUCT_DEAD';

const f1 = (n) => (Math.abs(n) < 0.05 ? 0 : n).toFixed(1);

/**
 * @param {(line: string) => void} emit receives each UART line
 */
export function createVirtualBoard(emit) {
  let s;
  const radar = createRadarMirror(emit);

  function reset(now = 0) {
    radar.reset(now);
    s = {
      state: STANDBY,
      charge: 0,
      upsideDown: false,
      tripPending: false,
      // PB
      down: false,
      downAt: 0,
      holdLatched: false,
      lastFireAt: -Infinity,
      lowChargeWarned: false,
      lastShortReleaseAt: null,
      singleDeadline: null,
      // timers
      telemetryAt: now,
      attackAt: 0,
      lastEntryAt: 0,
      rescueAttempts: 0,
      sacrificeAt: null,
      started: false,
    };
  }

  function sensors(now) {
    const w = now / 1000;
    const jitter = (k, a) => Math.sin(w * (0.7 + k * 0.31) + k) * a;
    return {
      t: 26.4 + jitter(1, 0.3),
      p: 1012.5 + jitter(2, 0.4),
      h: 55.0 + jitter(3, 0.8),
      a: [jitter(4, 0.1), jitter(5, 0.1), (s.upsideDown ? -9.8 : 9.8) + jitter(6, 0.05)],
      g: [jitter(7, 0.3), jitter(8, 0.3), jitter(9, 0.3)],
      m: [31 + jitter(10, 1.5), 42 + jitter(11, 1.5), 18 + jitter(12, 1.5)],
    };
  }

  function start(now) {
    if (s.started) return;
    s.started = true;
    s.telemetryAt = now;
    emit(`Drone initialized: Entering STANDBY MODE (Laser Charge: ${s.charge}/10)`);
    emit('EVT MODE=STANDBY_MODE');
    emit(`EVT CHARGE=${s.charge}/10`);
  }

  function enterLast(now) {
    s.state = LAST;
    s.lastEntryAt = now;
    s.attackAt = now;
    s.rescueAttempts = 0;
    s.singleDeadline = null;
    s.lastShortReleaseAt = null;
    emit('Drone is upside down! Entering THE LAST OF EE2028.');
    emit('EVT MODE=THE_LAST_OF_2028');
    emit(`EVT COUNTDOWN remaining_ms=${SIM.RESCUE_TIMEOUT_MS}`);
  }

  function selfDestruct(reason) {
    if (s.state === SD_TRIGGERED || s.state === SD_DEAD) return;
    emit(`${reason}! Drone self-destructing...`);
    emit(`EVT SELF_DESTRUCT reason=${/rescue attempts/.test(reason) ? 'rescue_limit' : 'timeout'}`);
    s.state = SD_TRIGGERED;
    s.sacrificeAt = null;
    s.singleDeadline = null;
  }

  function onDouble(now) {
    if (s.state === STANDBY) {
      s.state = BATTLE;
      emit(`PB Double Press (Standby) -> Entering BATTLE MODE. (Laser Charge: ${s.charge}/10)`);
      emit('EVT MODE=BATTLE_MODE');
    } else if (s.state === BATTLE) {
      s.state = STANDBY;
      s.telemetryAt = now;
      emit(`PB Double Press (Battle) -> Returning to STANDBY MODE. (Laser Charge: ${s.charge}/10)`);
      emit('EVT MODE=STANDBY_MODE');
    } else if (s.state === LAST) {
      s.rescueAttempts += 1;
      emit(`Rescue attempt ${s.rescueAttempts}/5 (Double Press detected)...`);
      if (s.upsideDown) {
        if (s.rescueAttempts >= SIM.RESCUE_ATTEMPT_LIMIT) {
          selfDestruct('5 rescue attempts failed while upside down');
        } else {
          emit('Drone is still upside down! Flip upright and double press to rescue.');
        }
      } else {
        s.state = BATTLE;
        s.rescueAttempts = 0;
        emit('PB Rescue Action: Successfully reverted to Battle Mode.');
        emit('EVT RESCUE');
        emit('EVT MODE=BATTLE_MODE');
      }
    }
  }

  function onSingle() {
    if (s.state !== BATTLE) return;
    if (s.charge < 5) {
      s.charge = Math.min(10, s.charge + 3);
      emit(`Laser recharging... Current charge: ${s.charge}/10`);
      emit(`EVT CHARGE=${s.charge}/10`);
    } else {
      emit(
        'Laser charge is already sufficient (>=5/10). Hold button >500ms to fire! Or double-press within 500ms to return to standby mode'
      );
    }
  }

  function tick(now) {
    if (!s.started) start(now);

    // Pending single press resolves once the double-press window closes.
    if (s.singleDeadline !== null && now >= s.singleDeadline) {
      s.singleDeadline = null;
      s.lastShortReleaseAt = null;
      onSingle();
    }

    // Hold latch.
    if (s.down && !s.holdLatched && now - s.downAt >= SIM.HOLD_MS) {
      s.holdLatched = true;
      s.singleDeadline = null;
      s.lastShortReleaseAt = null;
      s.lowChargeWarned = false;
    }

    switch (s.state) {
      case STANDBY: {
        if (s.upsideDown) {
          enterLast(now);
          break;
        }
        if (s.tripPending) {
          s.tripPending = false;
          s.state = BATTLE;
          s.singleDeadline = null;
          emit(`Critical T/A/G threshold violated -> Entering BATTLE MODE. (Laser Charge: ${s.charge}/10)`);
          emit('EVT MODE=BATTLE_MODE');
          break;
        }
        if (now - s.telemetryAt >= SIM.TELEMETRY_PERIOD_MS) {
          s.telemetryAt = now;
          const v = sensors(now);
          emit(
            `Telemetry -> T:${f1(v.t)} °C, P:${f1(v.p)} hPa, H:${f1(v.h)} % | ` +
              `Accel:[${v.a.map(f1).join(', ')}] m/s^2 | Gyro:[${v.g.map(f1).join(', ')}] dps | ` +
              `Magneto:[${v.m.map(f1).join(', ')}] mgauss`
          );
        }
        break;
      }
      case BATTLE: {
        s.tripPending = false; // T/A/G only matters in Standby
        if (s.upsideDown) {
          enterLast(now);
          break;
        }
        if (s.holdLatched && s.down) {
          if (s.charge < 5) {
            if (!s.lowChargeWarned) {
              emit(`Laser charge too low (${s.charge}/10) to fire! Please single-press to recharge the laser gun.`);
              s.lowChargeWarned = true;
            }
          } else if (now - s.lastFireAt >= SIM.FIRE_PERIOD_MS) {
            s.lastFireAt = now;
            s.charge -= 5;
            emit(`Laser gun fired continuously! PEW PEW! (Remaining charge: ${s.charge}/10)`);
            emit(`EVT FIRE charge=${s.charge}/10`);
            radar.fire(now); // Enhancements_OnEvent(DRONE_EVENT_LASER_FIRED)
            if (s.charge < 5) {
              emit(`Laser charge too low (${s.charge}/10) to fire! Please single-press to recharge the laser gun.`);
              s.lowChargeWarned = true;
            }
          }
        }
        break;
      }
      case LAST: {
        if (now - s.attackAt >= SIM.ATTACK_PERIOD_MS) {
          s.attackAt = now;
          const elapsed = now - s.lastEntryAt;
          const remaining = Math.max(0, SIM.RESCUE_TIMEOUT_MS - elapsed);
          emit('Drone was attacked!');
          emit('EVT ATTACK');
          emit(`EVT COUNTDOWN remaining_ms=${Math.round(remaining)}`);
        }
        if (now - s.lastEntryAt >= SIM.RESCUE_TIMEOUT_MS) {
          selfDestruct('10s elapsed without rescue');
        }
        break;
      }
      case SD_TRIGGERED: {
        if (s.sacrificeAt === null) {
          s.sacrificeAt = now;
          emit('Drone has made a noble sacrifice for mankind...');
        } else if (now - s.sacrificeAt >= SIM.TERMINATE_DELAY_MS) {
          emit('EVT TERMINATE');
          emit('Cheerio!');
          s.state = SD_DEAD;
        }
        break;
      }
      default:
        break;
    }

    // Enhancements_Tick(): radar → tracker → pan/laser (silent once halted).
    if (s.state !== SD_DEAD) radar.tick(now, s.state, s.state === SD_TRIGGERED);
  }

  function pressDown(now) {
    if (s.state === SD_TRIGGERED || s.state === SD_DEAD || s.down) return;
    tick(now);
    s.down = true;
    s.downAt = now;
    s.holdLatched = false;
  }

  function pressUp(now) {
    if (!s.down) return;
    tick(now);
    s.down = false;
    if (s.holdLatched) {
      s.holdLatched = false;
      return; // a hold is never a single/double press
    }
    if (s.state === SD_TRIGGERED || s.state === SD_DEAD) return;
    if (s.lastShortReleaseAt !== null && now - s.lastShortReleaseAt <= SIM.DOUBLE_PRESS_MS) {
      s.lastShortReleaseAt = null;
      s.singleDeadline = null;
      onDouble(now);
    } else {
      s.lastShortReleaseAt = now;
      s.singleDeadline = now + SIM.DOUBLE_PRESS_MS;
    }
  }

  reset();

  return {
    reset,
    tick,
    pressDown,
    pressUp,
    /** Flip the virtual board (accel z sign). */
    setUpsideDown(flag) {
      s.upsideDown = Boolean(flag);
    },
    /** A sudden motion spike — trips T/A/G in Standby like a real disturbance. */
    disturb() {
      if (s.state === STANDBY) s.tripPending = true;
    },
    /** What stands in front of the virtual RD-03D: [{x, y}] in mm (x right, y forward). */
    setRadarContacts(list) {
      radar.setContacts(list);
    },
    get snapshot() {
      return {
        state: s.state,
        charge: s.charge,
        upsideDown: s.upsideDown,
        holding: s.down && s.holdLatched,
        rescueAttempts: s.rescueAttempts,
        halted: s.state === SD_DEAD,
        radar: radar.snapshot,
      };
    },
  };
}
