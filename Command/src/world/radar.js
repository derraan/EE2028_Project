/**
 * HUD contact radar (2D canvas). Shows *simulated* contacts from the world,
 * centred on the drone, field (+z) at the top. Styled after the concept's
 * "Contact detected" screen: ice-blue rings, red contacts, sweeping beam.
 * A firmware RD-03D lock (stats.radar) adds the ±60° sensor cone, a bearing
 * line and a reticle on the locked zombie.
 */
export function createRadar(canvas) {
  const ctx = canvas.getContext('2d');
  const RANGE = 80;
  let sweep = 0;
  const memory = new Map(); // contact index → last-lit alpha
  let size = 0;

  function fit() {
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const css = canvas.clientWidth || 220;
    if (size !== css * ratio) {
      size = css * ratio;
      canvas.width = canvas.height = size;
    }
  }

  return {
    draw(dt, stats, { alarm = false, active = true } = {}) {
      fit();
      const w = size;
      const c = w / 2;
      const r = c * 0.92;
      ctx.clearRect(0, 0, w, w);
      ctx.save();
      ctx.translate(c, c);

      // face + rings
      const bg = ctx.createRadialGradient(0, 0, 0, 0, 0, r);
      bg.addColorStop(0, alarm ? 'rgba(80,10,20,0.55)' : 'rgba(18,30,52,0.55)');
      bg.addColorStop(1, 'rgba(4,8,16,0.75)');
      ctx.fillStyle = bg;
      ctx.beginPath();
      ctx.arc(0, 0, r, 0, Math.PI * 2);
      ctx.fill();
      ctx.strokeStyle = alarm ? 'rgba(255,90,100,0.35)' : 'rgba(140,190,255,0.28)';
      ctx.lineWidth = Math.max(1, w / 260);
      for (let i = 1; i <= 4; i++) {
        ctx.beginPath();
        ctx.arc(0, 0, (r * i) / 4, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.beginPath();
      ctx.moveTo(-r, 0);
      ctx.lineTo(r, 0);
      ctx.moveTo(0, -r);
      ctx.lineTo(0, r);
      ctx.stroke();

      // lab marker (behind the drone, −z → bottom)
      const sx = (x) => ((x - stats.drone.x) / RANGE) * r;
      const sy = (z) => (-(z - stats.drone.z) / RANGE) * r;
      ctx.fillStyle = 'rgba(184,168,255,0.85)';
      const lx = sx(0);
      const ly = sy(-20);
      if (Math.hypot(lx, ly) < r) {
        ctx.fillRect(lx - w * 0.04, ly - w * 0.015, w * 0.08, w * 0.03);
      }

      // sweep
      if (active) sweep = (sweep + dt * 2.2) % (Math.PI * 2);
      const grad = ctx.createConicGradient ? ctx.createConicGradient(sweep - Math.PI / 2 - 0.9, 0, 0) : null;
      if (grad) {
        grad.addColorStop(0, 'rgba(0,0,0,0)');
        grad.addColorStop(0.14, alarm ? 'rgba(255,70,90,0.28)' : 'rgba(95,227,255,0.22)');
        grad.addColorStop(0.145, 'rgba(0,0,0,0)');
        ctx.fillStyle = grad;
        ctx.beginPath();
        ctx.arc(0, 0, r, 0, Math.PI * 2);
        ctx.fill();
      }

      // contacts
      stats.contacts.forEach((ct, i) => {
        const x = sx(ct.x);
        const y = sy(ct.z);
        if (Math.hypot(x, y) > r) return;
        const ang = (Math.atan2(x, -y) + Math.PI * 2) % (Math.PI * 2);
        let diff = (sweep - ang + Math.PI * 2) % (Math.PI * 2);
        let a = memory.get(i) ?? 0.25;
        if (diff < 0.15) a = 1;
        a = Math.max(0.25, a - dt * 0.6);
        memory.set(i, a);
        ctx.fillStyle = `rgba(255,${ct.rising ? 160 : 60},${ct.rising ? 90 : 75},${a})`;
        ctx.shadowColor = 'rgba(255,60,75,0.9)';
        ctx.shadowBlur = w * 0.03 * a;
        ctx.beginPath();
        ctx.arc(x, y, w * 0.018, 0, Math.PI * 2);
        ctx.fill();
        ctx.shadowBlur = 0;
      });

      // RD-03D field of view (boresight = +z = up) and firmware lock
      const fov = (60 * Math.PI) / 180;
      const rr = ((6 * 12) / RANGE) * r; // 6 m × RADAR_WORLD_PER_M
      ctx.strokeStyle = 'rgba(255,90,110,0.22)';
      ctx.setLineDash([w * 0.01, w * 0.015]);
      ctx.beginPath();
      ctx.moveTo(0, 0);
      ctx.arc(0, 0, Math.min(rr, r), -Math.PI / 2 - fov, -Math.PI / 2 + fov);
      ctx.closePath();
      ctx.stroke();
      ctx.setLineDash([]);
      if (stats.radar) {
        const x = sx(stats.radar.x);
        const y = sy(stats.radar.z);
        ctx.strokeStyle = stats.radar.laser ? 'rgba(255,70,90,0.95)' : 'rgba(255,170,90,0.85)';
        ctx.lineWidth = Math.max(1, w / 150);
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(x, y);
        ctx.stroke();
        const k = w * 0.035;
        ctx.beginPath();
        ctx.arc(x, y, k, 0, Math.PI * 2);
        ctx.moveTo(x - k * 1.6, y);
        ctx.lineTo(x - k * 0.6, y);
        ctx.moveTo(x + k * 0.6, y);
        ctx.lineTo(x + k * 1.6, y);
        ctx.moveTo(x, y - k * 1.6);
        ctx.lineTo(x, y - k * 0.6);
        ctx.moveTo(x, y + k * 0.6);
        ctx.lineTo(x, y + k * 1.6);
        ctx.stroke();
      }

      // drone at centre, nose = heading
      ctx.rotate(stats.drone.heading); // heading +y-rot turns +z toward +x = clockwise on screen
      ctx.fillStyle = alarm ? '#ff5a6a' : '#5fe3ff';
      ctx.beginPath();
      ctx.moveTo(0, -w * 0.04);
      ctx.lineTo(w * 0.028, w * 0.03);
      ctx.lineTo(0, w * 0.015);
      ctx.lineTo(-w * 0.028, w * 0.03);
      ctx.closePath();
      ctx.fill();
      ctx.restore();
    },
  };
}
