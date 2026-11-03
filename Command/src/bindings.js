/**
 * Push parser state into data-cc-* DOM hooks (and legacy ids from the mockup).
 */
const MODE_LABEL = {
  STANDBY_MODE: 'STANDBY',
  BATTLE_MODE: 'BATTLE',
  THE_LAST_OF_2028: 'THE_LAST_OF_2028',
  TERMINATED: 'TERMINATED',
};

function $(sel, root = document) {
  return root.querySelector(sel);
}

function setText(el, text) {
  if (el) el.textContent = text;
}

function fmtVec(v, unit) {
  if (!v || v[0] == null) return `-- ${unit}`;
  return `[${v.map((n) => Number(n).toFixed(1)).join(', ')}] ${unit}`;
}

export function bindDashboard(state) {
  const linkEl = $('[data-cc-link]') || $('.online');
  if (linkEl) {
    if (state.missionEnded) {
      linkEl.textContent = 'MISSION TERMINATED';
      linkEl.style.color = 'var(--orange)';
    } else if (state.link === 'severed') {
      linkEl.textContent = 'LINK LOST';
      linkEl.style.color = 'var(--orange)';
    } else if (state.link === 'uart' || state.link === 'replay') {
      linkEl.textContent = state.link === 'uart' ? 'UPLINK ESTABLISHED' : 'FIXTURE REPLAY';
      linkEl.style.color = 'var(--mint)';
    } else {
      linkEl.textContent = 'UPLINK IDLE';
      linkEl.style.color = 'var(--muted)';
    }
  }

  const modeLabel = MODE_LABEL[state.mode] || state.mode;
  setText($('[data-cc-mode]') || $('#mode'), modeLabel);
  const flight = $('[data-cc-flight-state]') || $('#flightState');
  if (flight) {
    if (state.countdownMs != null && state.mode === 'THE_LAST_OF_2028') {
      flight.textContent = `COUNTDOWN ${(state.countdownMs / 1000).toFixed(1)}s`;
    } else {
      flight.textContent = modeLabel;
    }
  }

  setText($('[data-cc-countdown]'), state.countdownMs == null ? '--' : `${state.countdownMs} ms`);

  const charge = state.charge;
  setText($('[data-cc-charge]'), `${charge}/10`);
  const bar = $('[data-cc-charge-bar]');
  if (bar) bar.style.width = `${charge * 10}%`;
  // Energy core strong in mockup (no data-cc yet on some builds)
  const energyStrong = document.querySelector('.panel.padded strong[style]');
  if (energyStrong && energyStrong.closest('.panel')) {
    const eyebrow = energyStrong.closest('.panel')?.querySelector('.eyebrow');
    if (eyebrow && /ENERGY CORE/i.test(eyebrow.textContent || '')) {
      energyStrong.innerHTML = `${charge}<span style="font-size:20px">/10</span>`;
      const bi = energyStrong.parentElement?.querySelector('.bar i');
      if (bi) bi.style.width = `${charge * 10}%`;
    }
  }
  setText($('[data-cc-armed]'), state.armed ? 'YES' : 'NO');

  const t = state.telemetry.T;
  setText($('[data-cc-t]') || $('#temp'), t == null ? '-- °C' : `${Number(t).toFixed(1)} °C`);

  const sensorsHost = $('[data-cc-sensors]') || $('#devices');
  if (sensorsHost && sensorsHost.hasAttribute('data-cc-sensors')) {
    const tel = state.telemetry;
    const cards = [
      ['TEMPERATURE', t == null ? '--' : Number(t).toFixed(1), tel.units.T],
      ['PRESSURE', tel.P == null ? '--' : Number(tel.P).toFixed(1), tel.units.P],
      ['HUMIDITY', tel.H == null ? '--' : Number(tel.H).toFixed(1), tel.units.H],
      ['ACCEL', fmtVec(tel.Accel, tel.units.Accel), 'X Y Z'],
      ['GYRO', fmtVec(tel.Gyro, tel.units.Gyro), 'X Y Z'],
      ['MAGNETO', fmtVec(tel.Magneto, tel.units.Magneto), 'X Y Z'],
    ];
    sensorsHost.innerHTML = cards
      .map(
        ([title, value, sub]) =>
          `<div class="panel device"><div class="eyebrow">${title}</div><strong style="font-size:18px">${value}</strong><div class="small">${sub}</div></div>`
      )
      .join('');
  }

  const thresh = $('[data-cc-thresholds]') || $('#diagnostics');
  if (thresh) {
    thresh.textContent = state.thresholds.ok
      ? 'ALL OK — sensors within thresholds'
      : state.thresholds.lines.slice(-3).join(' · ') || 'THRESHOLD FAULT';
  }

  const log = $('[data-cc-event-log]') || $('#events');
  if (log) {
    log.innerHTML = state.events
      .slice(0, 12)
      .map((e) => `[EVT] ${e.text}`)
      .join('<br>') || '[SYS] awaiting telemetry';
  }

  const alert = $('[data-cc-alert]') || $('#warningText');
  if (alert) {
    if (state.missionEnded) {
      alert.textContent = `Mission terminated${state.lastSelfDestructReason ? ` (${state.lastSelfDestructReason})` : ''}.`;
    } else if (state.mode === 'THE_LAST_OF_2028') {
      alert.textContent = 'Drone under attack — rescue window open. Double-press when upright.';
    } else if (!state.thresholds.ok) {
      alert.textContent = state.thresholds.lines.slice(-1)[0] || 'Sensor threshold violation.';
    } else if (state.link === 'severed') {
      alert.textContent = 'UART link lost. Reconnect serial or replay a fixture.';
    }
  }

  setText($('[data-cc-ir]'), state.ir ? 'HIT' : 'CLEAR');
  setText($('[data-cc-radar-az]'), state.radarAz == null ? '--' : String(state.radarAz));
}
