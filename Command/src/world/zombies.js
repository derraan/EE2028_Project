/**
 * The infected — lab-coat researchers turned by spores from orbit.
 * Pure fiction: they spawn, walk, claw and die only when the world tells
 * them to (meteor impacts, confirmed EVT FIRE, rescue shockwave, self-destruct).
 */
import * as THREE from 'three';
import { LAB_CENTER, PERIMETER_RADIUS } from './environment.js';

const MAX = 40;

export function createHorde(scene) {
  // Faint self-glow keeps the infected readable against the night (spore bioluminescence).
  const skin = new THREE.MeshStandardMaterial({ color: 0x4f5c4b, roughness: 0.95, emissive: 0x12300e, emissiveIntensity: 0.7 });
  const coats = [0xa9aeb5, 0x7b8088, 0x3b3f46, 0x4a3438].map((c) => new THREE.MeshStandardMaterial({ color: c, roughness: 0.9, emissive: c, emissiveIntensity: 0.12 }));
  const pants = new THREE.MeshStandardMaterial({ color: 0x151a22, roughness: 1 });
  const eye = new THREE.MeshBasicMaterial({ color: new THREE.Color(1.2, 4.5, 1.0) });
  const spore = new THREE.MeshBasicMaterial({ color: new THREE.Color(0.8, 3, 0.6) });

  const torsoGeo = new THREE.BoxGeometry(0.72, 0.95, 0.42);
  torsoGeo.translate(0, 0.47, 0);
  const coatTailGeo = new THREE.BoxGeometry(0.76, 0.5, 0.46);
  const headGeo = new THREE.BoxGeometry(0.42, 0.46, 0.42);
  headGeo.translate(0, 0.23, 0);
  const armGeo = new THREE.BoxGeometry(0.18, 0.82, 0.18);
  armGeo.translate(0, -0.38, 0);
  const legGeo = new THREE.BoxGeometry(0.22, 0.92, 0.22);
  legGeo.translate(0, -0.46, 0);
  const eyeGeo = new THREE.BoxGeometry(0.11, 0.07, 0.02);
  const sporeGeo = new THREE.IcosahedronGeometry(0.07, 0);

  const shadowTex = (() => {
    const c = document.createElement('canvas');
    c.width = c.height = 64;
    const g = c.getContext('2d');
    const grd = g.createRadialGradient(32, 32, 0, 32, 32, 32);
    grd.addColorStop(0, 'rgba(0,0,0,0.65)');
    grd.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = grd;
    g.fillRect(0, 0, 64, 64);
    return new THREE.CanvasTexture(c);
  })();
  const shadowGeo = new THREE.PlaneGeometry(1.4, 1.4);
  shadowGeo.rotateX(-Math.PI / 2);
  const shadowMat = new THREE.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false });

  function build(i) {
    const g = new THREE.Group();
    const hips = new THREE.Group();
    hips.position.y = 0.95;
    g.add(hips);
    const coat = coats[i % coats.length];
    const torso = new THREE.Mesh(torsoGeo, coat);
    hips.add(torso);
    const tail = new THREE.Mesh(coatTailGeo, coat);
    tail.position.y = -0.1;
    hips.add(tail);
    const neck = new THREE.Group();
    neck.position.y = 0.98;
    hips.add(neck);
    const head = new THREE.Mesh(headGeo, skin);
    neck.add(head);
    for (const x of [-0.1, 0.1]) {
      const e = new THREE.Mesh(eyeGeo, eye);
      e.position.set(x, 0.27, 0.215);
      neck.add(e);
    }
    const arms = [];
    for (const x of [-0.46, 0.46]) {
      const shoulder = new THREE.Group();
      shoulder.position.set(x, 0.85, 0);
      const arm = new THREE.Mesh(armGeo, skin);
      shoulder.add(arm);
      hips.add(shoulder);
      arms.push(shoulder);
    }
    const legs = [];
    for (const x of [-0.16, 0.16]) {
      const hip = new THREE.Group();
      hip.position.set(x, 0, 0);
      const leg = new THREE.Mesh(legGeo, pants);
      hip.add(leg);
      hips.add(hip);
      legs.push(hip);
    }
    // glowing spore growths
    for (let k = 0; k < 3; k++) {
      const s = new THREE.Mesh(sporeGeo, spore);
      s.position.set((Math.random() - 0.5) * 0.6, 0.3 + Math.random() * 0.6, 0.22);
      hips.add(s);
    }
    const shadow = new THREE.Mesh(shadowGeo, shadowMat);
    shadow.position.y = 0.02;
    g.add(shadow);
    g.visible = false;
    scene.add(g);
    return {
      g, hips, neck, arms, legs, shadow,
      alive: false, state: 'idle', t: 0,
      pos: new THREE.Vector3(), vel: new THREE.Vector3(),
      speed: 1, phase: Math.random() * 6, slot: new THREE.Vector3(),
      lurch: 0,
    };
  }

  const pool = Array.from({ length: MAX }, (_, i) => build(i));
  const tmp = new THREE.Vector3();
  let target = 'lab'; // 'lab' | 'drone'
  const dronePos = new THREE.Vector3();

  function slotFor(z) {
    // A point on the holo-fence arc, roughly in line with where the zombie is.
    const a = THREE.MathUtils.clamp(Math.atan2(z.pos.x - LAB_CENTER.x, (z.pos.z - LAB_CENTER.z) * 1.55), -1.25, 1.25);
    z.slot.set(LAB_CENTER.x + Math.sin(a) * PERIMETER_RADIUS * 1.55 * 1.04, 0, LAB_CENTER.z + Math.cos(a) * PERIMETER_RADIUS * 1.08);
  }

  return {
    get alive() {
      return pool.filter((z) => z.alive && z.state !== 'die');
    },
    /** Zombies currently clawing at the lab fence. */
    attackingLab() {
      let n = 0;
      for (const z of pool) if (z.alive && z.state === 'claw' && target === 'lab') n++;
      return n;
    },
    count() {
      let n = 0;
      for (const z of pool) if (z.alive && z.state !== 'die') n++;
      return n;
    },

    /** Spawn `n` zombies clawing out of the ground around `center`. */
    spawn(center, n, { delay = 0.2 } = {}) {
      let made = 0;
      for (const z of pool) {
        if (made >= n) break;
        if (z.alive) continue;
        z.alive = true;
        z.state = 'rise';
        z.t = -made * delay;
        // Random bearing and random distance from `center`; wide scatter so
        // they arrive staggered, not as one clump.
        const a = Math.random() * Math.PI * 2;
        const r = 2 + Math.random() * 6;
        z.pos.set(center.x + Math.cos(a) * r, 0, center.z + Math.sin(a) * r);
        z.speed = 4.2 + Math.random() * 4.0; // long approach: cover 70-130 units in ~30-40 s
        z.lurch = Math.random();
        z.g.position.copy(z.pos).setY(-2.2);
        z.g.rotation.set(0, Math.atan2(LAB_CENTER.x - z.pos.x, LAB_CENTER.z - z.pos.z), 0);
        z.g.visible = true;
        slotFor(z);
        made++;
      }
      return made;
    },

    /** Pick the infected that matters most: closest to the drone (or lab). */
    priorityTarget(from) {
      let best = null;
      let bestD = Infinity;
      for (const z of pool) {
        if (!z.alive || z.state === 'die' || z.state === 'rise') continue;
        const d = z.pos.distanceTo(from) + z.pos.distanceTo(LAB_CENTER) * 0.6;
        if (d < bestD) {
          bestD = d;
          best = z;
        }
      }
      return best;
    },

    /** Living (non-dying, above-ground) zombie nearest to `point` within `maxDist`. */
    nearestTo(point, maxDist = Infinity) {
      let best = null;
      let bestD = maxDist;
      for (const z of pool) {
        if (!z.alive || z.state === 'die' || z.state === 'rise') continue;
        const d = Math.hypot(z.pos.x - point.x, z.pos.z - point.z);
        if (d < bestD) {
          bestD = d;
          best = z;
        }
      }
      return best;
    },

    /** Drag a zombie toward a firmware-reported radar position (live UART / replay). */
    follow(z, point, dt) {
      if (!z || !z.alive || z.state === 'die') return;
      const d = Math.hypot(z.pos.x - point.x, z.pos.z - point.z);
      const k = d > 6 ? 1 : 1 - Math.exp(-dt * 3);
      z.pos.x += (point.x - z.pos.x) * k;
      z.pos.z += (point.z - z.pos.z) * k;
      z.g.position.x = z.pos.x;
      z.g.position.z = z.pos.z;
    },

    torsoPosition(z, out = new THREE.Vector3()) {
      return out.copy(z.pos).setY(1.35);
    },

    headPosition(z, out = new THREE.Vector3()) {
      return out.copy(z.pos).setY(1.9);
    },

    kill(z, from) {
      if (!z || !z.alive || z.state === 'die') return false;
      z.state = 'die';
      z.t = 0;
      tmp.subVectors(z.pos, from || LAB_CENTER).setY(0).normalize();
      z.g.rotation.y = Math.atan2(-tmp.x, -tmp.z);
      return true;
    },

    /** Kill everything within `radius` of `center` (rescue shockwave / blast). */
    blast(center, radius, onKill) {
      let n = 0;
      for (const z of pool) {
        if (!z.alive || z.state === 'die') continue;
        if (z.pos.distanceTo(center) <= radius) {
          this.kill(z, center);
          onKill?.(z);
          n++;
        }
      }
      return n;
    },

    setTarget(mode, drone) {
      target = mode;
      if (drone) dronePos.copy(drone);
    },

    clear() {
      for (const z of pool) {
        z.alive = false;
        z.g.visible = false;
      }
    },

    /** Contacts for the radar, in world xz. */
    contacts() {
      const out = [];
      for (const z of pool) if (z.alive && z.state !== 'die') out.push({ x: z.pos.x, z: z.pos.z, rising: z.state === 'rise' });
      return out;
    },

    update(dt, t, { onClaw, onDeath } = {}) {
      const living = pool.filter((z) => z.alive);
      for (const z of living) {
        z.t += dt;
        z.phase += dt * z.speed * 4.2;
        const walkSwing = Math.sin(z.phase);

        if (z.state === 'rise') {
          if (z.t < 0) continue;
          const k = Math.min(1, z.t / 1.3);
          z.g.position.set(z.pos.x, -2.2 + (1 - (1 - k) ** 3) * 2.2, z.pos.z);
          z.arms.forEach((a, i) => (a.rotation.x = -2.6 + Math.sin(z.t * 9 + i) * 0.3));
          z.hips.rotation.x = 0.4 * (1 - k);
          if (k >= 1) z.state = 'walk';
          continue;
        }

        if (z.state === 'die') {
          const k = Math.min(1, z.t / 0.9);
          z.hips.rotation.x = -k * 1.45;
          z.hips.position.y = 0.95 - k * 0.7;
          z.g.scale.setScalar(1 - Math.max(0, z.t - 0.6) * 1.6);
          if (z.t > 0.62 && !z.dissolved) {
            z.dissolved = true;
            onDeath?.(z.pos.clone().setY(0.8));
          }
          if (z.t > 1.2) {
            z.alive = false;
            z.dissolved = false;
            z.g.visible = false;
            z.g.scale.setScalar(1);
            z.hips.rotation.set(0, 0, 0);
            z.hips.position.y = 0.95;
          }
          continue;
        }

        // Steering
        const goal = target === 'drone' ? dronePos : z.slot;
        tmp.subVectors(goal, z.pos).setY(0);
        const dist = tmp.length();
        const reach = target === 'drone' ? 2.4 + z.lurch * 1.2 : 0.6;
        const moving = dist > reach;
        if (moving) {
          tmp.normalize().multiplyScalar(z.speed * (0.75 + 0.25 * Math.max(0, walkSwing)));
          // separation
          for (const o of living) {
            if (o === z || o.state !== 'walk' && o.state !== 'claw') continue;
            const dx = z.pos.x - o.pos.x;
            const dz = z.pos.z - o.pos.z;
            const d2 = dx * dx + dz * dz;
            if (d2 > 0.0001 && d2 < 1.4) {
              tmp.x += (dx / d2) * 0.6;
              tmp.z += (dz / d2) * 0.6;
            }
          }
          z.pos.addScaledVector(tmp, dt);
          z.state = 'walk';
        } else {
          if (z.state !== 'claw') z.t = 0;
          z.state = 'claw';
        }

        z.g.position.set(z.pos.x, Math.abs(walkSwing) * 0.06, z.pos.z);
        const face = Math.atan2(goal.x - z.pos.x, goal.z - z.pos.z);
        let dh = face - z.g.rotation.y;
        dh = Math.atan2(Math.sin(dh), Math.cos(dh));
        z.g.rotation.y += dh * Math.min(1, dt * 4);

        if (z.state === 'walk') {
          z.legs[0].rotation.x = walkSwing * 0.55;
          z.legs[1].rotation.x = -walkSwing * 0.55;
          z.arms[0].rotation.x = -1.45 + Math.sin(z.phase * 0.5) * 0.15;
          z.arms[1].rotation.x = -1.35 + Math.cos(z.phase * 0.5) * 0.15;
          z.hips.rotation.x = 0.18 + Math.abs(walkSwing) * 0.06;
          z.hips.rotation.z = Math.sin(z.phase * 0.5) * 0.08 * (0.5 + z.lurch);
          z.neck.rotation.z = Math.sin(z.phase * 0.33 + z.lurch * 6) * 0.35;
        } else {
          const c = Math.sin(z.t * 7 + z.lurch * 6);
          z.arms[0].rotation.x = -1.9 + c * 0.6;
          z.arms[1].rotation.x = -1.9 - c * 0.6;
          z.legs[0].rotation.x = 0;
          z.legs[1].rotation.x = 0;
          z.hips.rotation.x = 0.35 + c * 0.05;
          if (Math.random() < dt * 0.6) onClaw?.(z);
        }
      }
    },
  };
}
