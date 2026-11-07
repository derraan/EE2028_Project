/**
 * Synthesized sound — no asset files. Browsers only allow audio after a
 * user gesture, so nothing plays until `unlock()` is called from a click.
 */
export function createAudio() {
  let ctx = null;
  let master = null;
  let hum = null;
  let muted = false;
  let alarmTimer = 0;

  function ensure() {
    if (ctx) return ctx;
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    ctx = new AC();
    master = ctx.createGain();
    master.gain.value = muted ? 0 : 0.55;
    const comp = ctx.createDynamicsCompressor();
    master.connect(comp).connect(ctx.destination);

    // Ambient reactor hum
    const o1 = ctx.createOscillator();
    const o2 = ctx.createOscillator();
    o1.type = 'sawtooth';
    o2.type = 'sine';
    o1.frequency.value = 46;
    o2.frequency.value = 69.5;
    const lp = ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = 180;
    const g = ctx.createGain();
    g.gain.value = 0.05;
    o1.connect(lp);
    o2.connect(lp);
    lp.connect(g).connect(master);
    o1.start();
    o2.start();
    hum = { g, lp };
    return ctx;
  }

  function noiseBuffer(seconds = 1) {
    const len = Math.floor(ctx.sampleRate * seconds);
    const buf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = buf.getChannelData(0);
    for (let i = 0; i < len; i++) d[i] = Math.random() * 2 - 1;
    return buf;
  }

  function env(gainNode, t0, attack, peak, decay) {
    gainNode.gain.setValueAtTime(0.0001, t0);
    gainNode.gain.exponentialRampToValueAtTime(peak, t0 + attack);
    gainNode.gain.exponentialRampToValueAtTime(0.0001, t0 + attack + decay);
  }

  const api = {
    unlock() {
      if (!ensure()) return;
      if (ctx.state === 'suspended') ctx.resume();
    },
    get muted() {
      return muted;
    },
    setMuted(m) {
      muted = m;
      if (master) master.gain.setTargetAtTime(m ? 0 : 0.55, ctx.currentTime, 0.05);
    },
    laser() {
      if (!ctx) return;
      const t = ctx.currentTime;
      const o = ctx.createOscillator();
      o.type = 'sawtooth';
      o.frequency.setValueAtTime(1800, t);
      o.frequency.exponentialRampToValueAtTime(120, t + 0.28);
      const f = ctx.createBiquadFilter();
      f.type = 'bandpass';
      f.frequency.value = 900;
      f.Q.value = 0.8;
      const g = ctx.createGain();
      env(g, t, 0.005, 0.35, 0.3);
      o.connect(f).connect(g).connect(master);
      o.start(t);
      o.stop(t + 0.35);
    },
    /** Giant cannon: rising charge whine into a heavy blast. */
    cannon() {
      if (!ctx) return;
      const t = ctx.currentTime;
      const o = ctx.createOscillator();
      o.type = 'sawtooth';
      o.frequency.setValueAtTime(90, t);
      o.frequency.exponentialRampToValueAtTime(1400, t + 0.22);
      const g = ctx.createGain();
      env(g, t, 0.01, 0.25, 0.24);
      o.connect(g).connect(master);
      o.start(t);
      o.stop(t + 0.26);
      setTimeout(() => this.boom(1.4), 200);
    },
    boom(size = 1) {
      if (!ctx) return;
      const t = ctx.currentTime;
      const src = ctx.createBufferSource();
      src.buffer = noiseBuffer(1.6 * size);
      const f = ctx.createBiquadFilter();
      f.type = 'lowpass';
      f.frequency.setValueAtTime(900, t);
      f.frequency.exponentialRampToValueAtTime(60, t + 1.2 * size);
      const g = ctx.createGain();
      env(g, t, 0.01, 0.9 * Math.min(1.2, size), 1.3 * size);
      src.connect(f).connect(g).connect(master);
      src.start(t);
      const sub = ctx.createOscillator();
      sub.frequency.setValueAtTime(80, t);
      sub.frequency.exponentialRampToValueAtTime(28, t + 0.8);
      const sg = ctx.createGain();
      env(sg, t, 0.01, 0.6, 0.9);
      sub.connect(sg).connect(master);
      sub.start(t);
      sub.stop(t + 1);
    },
    whoosh() {
      if (!ctx) return;
      const t = ctx.currentTime;
      const src = ctx.createBufferSource();
      src.buffer = noiseBuffer(1.6);
      const f = ctx.createBiquadFilter();
      f.type = 'bandpass';
      f.Q.value = 2;
      f.frequency.setValueAtTime(300, t);
      f.frequency.exponentialRampToValueAtTime(2400, t + 1.4);
      const g = ctx.createGain();
      g.gain.setValueAtTime(0.0001, t);
      g.gain.exponentialRampToValueAtTime(0.25, t + 1.3);
      g.gain.exponentialRampToValueAtTime(0.0001, t + 1.55);
      src.connect(f).connect(g).connect(master);
      src.start(t);
    },
    groan() {
      if (!ctx) return;
      const t = ctx.currentTime;
      const o = ctx.createOscillator();
      o.type = 'sawtooth';
      const base = 70 + Math.random() * 40;
      o.frequency.setValueAtTime(base, t);
      o.frequency.linearRampToValueAtTime(base * 0.7, t + 0.9);
      const lfo = ctx.createOscillator();
      lfo.frequency.value = 7 + Math.random() * 5;
      const lg = ctx.createGain();
      lg.gain.value = 9;
      lfo.connect(lg).connect(o.frequency);
      const f = ctx.createBiquadFilter();
      f.type = 'bandpass';
      f.frequency.value = 420;
      f.Q.value = 4;
      const g = ctx.createGain();
      env(g, t, 0.15, 0.12, 0.8);
      o.connect(f).connect(g).connect(master);
      o.start(t);
      lfo.start(t);
      o.stop(t + 1.1);
      lfo.stop(t + 1.1);
    },
    blip(freq = 880, dur = 0.08, type = 'square', vol = 0.12) {
      if (!ctx) return;
      const t = ctx.currentTime;
      const o = ctx.createOscillator();
      o.type = type;
      o.frequency.value = freq;
      const g = ctx.createGain();
      env(g, t, 0.003, vol, dur);
      o.connect(g).connect(master);
      o.start(t);
      o.stop(t + dur + 0.05);
    },
    rescue() {
      if (!ctx) return;
      [523, 784, 1046].forEach((f, i) => setTimeout(() => api.blip(f, 0.16, 'triangle', 0.18), i * 90));
    },
    /** Call every frame with the current mode for ambient layers. */
    update(dt, { alarm = false, battle = false } = {}) {
      if (!ctx || !hum) return;
      hum.lp.frequency.setTargetAtTime(battle ? 420 : alarm ? 600 : 180, ctx.currentTime, 0.3);
      hum.g.gain.setTargetAtTime(battle ? 0.07 : 0.05, ctx.currentTime, 0.3);
      if (alarm) {
        alarmTimer -= dt;
        if (alarmTimer <= 0) {
          alarmTimer = 0.5;
          api.blip(960, 0.12, 'square', 0.1);
          setTimeout(() => api.blip(720, 0.12, 'square', 0.08), 250);
        }
      } else alarmTimer = 0;
    },
  };
  return api;
}
