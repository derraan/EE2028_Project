/**
 * Giants — the people the RD-03D radar locks onto, shown 10–20× the size of
 * the infected. Only the giant cannon (EVT CANNON → EVT GIANT hit/down) can
 * hurt them; the laser gun's EVT FIRE ignores them.
 *
 * Pure fiction driven by firmware truth: a giant stands where the radar lock
 * is (live board), or strides into the radar cone and is what the virtual
 * RD-03D sees (simulation). Hits and death come only from parsed EVT GIANT.
 */
import * as THREE from 'three';

const POOL = 2; // a dying giant can still be on screen when the next one rises
const BASE_HEIGHT = 1.9; // zombie height in world units

export function createGiants(scene) {
  const skin = new THREE.MeshStandardMaterial({ fog: false, color: 0x3b4636, roughness: 0.95, emissive: 0x1a2a12, emissiveIntensity: 0.6 });
  const hide = new THREE.MeshStandardMaterial({ fog: false, color: 0x2a2320, roughness: 1, emissive: 0x120806, emissiveIntensity: 0.4 });
  const eye = new THREE.MeshBasicMaterial({ fog: false, color: new THREE.Color(5, 0.6, 0.4) });
  const spore = new THREE.MeshBasicMaterial({ color: new THREE.Color(0.5, 1.6, 0.4) });
  const box = (w, h, d, ty = 0) => {
    const g = new THREE.BoxGeometry(w, h, d);
    g.translate(0, ty, 0);
    return g;
  };
  // Unit giant ≈ 1.9 tall (same as a zombie), scaled 10–20× on spawn.
  const torsoGeo = box(0.95, 0.85, 0.6, 0.42);
  const bellyGeo = box(0.85, 0.35, 0.55, -0.05);
  const headGeo = box(0.38, 0.36, 0.38, 0.18);
  const armGeo = box(0.26, 0.95, 0.26, -0.45);
  const fistGeo = box(0.32, 0.26, 0.32, -0.98);
  const legGeo = box(0.32, 0.85, 0.32, -0.42);
  const eyeGeo = box(0.09, 0.05, 0.02);
  const sporeGeo = new THREE.IcosahedronGeometry(0.035, 0); // scaled 10–20× with the giant

  function build() {
    const g = new THREE.Group();
    const hips = new THREE.Group();
    hips.position.y = 0.85;
    g.add(hips);
    hips.add(new THREE.Mesh(torsoGeo, hide));
    hips.add(new THREE.Mesh(bellyGeo, skin));
    const neck = new THREE.Group();
    neck.position.y = 0.86;
    hips.add(neck);
    neck.add(new THREE.Mesh(headGeo, skin));
    for (const x of [-0.08, 0.08]) {
      const e = new THREE.Mesh(eyeGeo, eye);
      e.position.set(x, 0.22, 0.195);
      neck.add(e);
    }
    const arms = [];
    for (const x of [-0.62, 0.62]) {
      const sh = new THREE.Group();
      sh.position.set(x, 0.78, 0);
      sh.add(new THREE.Mesh(armGeo, skin));
      sh.add(new THREE.Mesh(fistGeo, hide));
      hips.add(sh);
      arms.push(sh);
    }
    const legs = [];
    for (const x of [-0.22, 0.22]) {
      const hp = new THREE.Group();
      hp.position.set(x, 0, 0);
      hp.add(new THREE.Mesh(legGeo, hide));
      hips.add(hp);
      legs.push(hp);
    }
    for (let k = 0; k < 6; k++) {
      const s = new THREE.Mesh(sporeGeo, spore);
      s.position.set((Math.random() - 0.5) * 0.8, 0.1 + Math.random() * 0.75, 0.31);
      hips.add(s);
    }
    g.visible = false;
    scene.add(g);
    return {
      g, hips, neck, arms, legs,
      alive: false, state: 'idle', t: 0, scale: 15,
      pos: new THREE.Vector3(), goal: new THREE.Vector3(), phase: Math.random() * 10,
      hitFlash: 0, stagger: 0, hits: 0, face: null, speed: 22,
    };
  }

  const pool = Array.from({ length: POOL }, build);
  let current = null; // the giant bound to the radar lock

  function setFlash(z, k) {
    const c = 0.6 + k * 4;
    skin.emissiveIntensity = c;
    hide.emissiveIntensity = 0.4 + k * 3;
    skin.emissive.setRGB(0.1 + k * 0.9, 0.16 * (1 - k), 0.07 * (1 - k));
  }

  return {
    /** The giant currently bound to the radar lock (alive, not dying), or null. */
    get current() {
      return current && current.alive && current.state !== 'die' ? current : null;
    },
    /** Height in world units of the current giant (0 if none) — camera framing. */
    get height() {
      const z = this.current;
      return z ? z.scale * BASE_HEIGHT : 0;
    },

    /** A giant rises at `point` (world xz), 10–20× zombie size. */
    spawn(point, { scale = 10 + Math.random() * 10, facing = null } = {}) {
      const z = pool.find((p) => !p.alive) || pool[0];
      z.alive = true;
      z.state = 'rise';
      z.t = 0;
      z.hits = 0;
      z.face = null;
      z.speed = 18 + Math.random() * 8; // varied per giant
      z.stagger = 0;
      z.hitFlash = 0;
      z.scale = scale;
      z.pos.set(point.x, 0, point.z);
      z.goal.copy(z.pos);
      z.g.scale.setScalar(scale);
      z.g.position.set(z.pos.x, -scale * BASE_HEIGHT, z.pos.z);
      z.g.rotation.set(0, facing ?? 0, 0);
      z.hips.rotation.set(0, 0, 0);
      z.g.visible = true;
      current = z;
      return z;
    },

    /** Walk the current giant toward a firmware-reported (or scripted) point. */
    follow(point) {
      const z = this.current;
      if (z) z.goal.set(point.x, 0, point.z);
    },

    /** When standing still, turn to face this world point. */
    faceTo(point) {
      const z = this.current;
      if (z) z.face = point.clone();
    },

    /** Chest point for the cannon beam / laser dot (azimuth-only aim → chest). */
    chest(z = this.current, out = new THREE.Vector3()) {
      if (!z) return null;
      return out.copy(z.pos).setY(z.scale * BASE_HEIGHT * 0.62);
    },

    /** EVT GIANT hit=n/2: stagger + flash, keeps standing. */
    hit(hits) {
      const z = this.current;
      if (!z) return null;
      z.hits = hits;
      z.hitFlash = 1;
      z.stagger = 1;
      return z;
    },

    /** EVT GIANT down: topple backwards, then sink away. */
    kill(from) {
      const z = this.current;
      if (!z) return null;
      z.state = 'die';
      z.t = 0;
      z.hitFlash = 1;
      if (from) z.g.rotation.y = Math.atan2(from.x - z.pos.x, from.z - z.pos.z);
      current = null;
      return z;
    },

    /** Lock lost without a kill: the giant lumbers off into the fog. */
    leave() {
      const z = this.current;
      if (!z) return;
      z.state = 'leave';
      z.t = 0;
      current = null;
    },

    /** Giants standing at their goal (the lab) — they smash it. */
    attackingLab(lab, reach) {
      let n = 0;
      for (const z of pool) {
        if (z.alive && z.state === 'walk' && Math.hypot(z.pos.x - lab.x, z.pos.z - lab.z) < reach) n++;
      }
      return n;
    },

    /** Visit every live giant (for a map-wide blast) then remove them all. */
    obliterate(cb) {
      for (const z of pool) if (z.alive) cb?.(z.pos.clone().setY(z.scale * BASE_HEIGHT * 0.5), z.scale);
      this.clear();
    },

    clear() {
      for (const z of pool) {
        z.alive = false;
        z.g.visible = false;
      }
      current = null;
    },

    /** World xz of the live giant (for the virtual radar), or null. */
    contact() {
      const z = this.current;
      return z && z.state !== 'rise' ? { x: z.pos.x, z: z.pos.z } : null;
    },

    update(dt, { onStomp, onFall } = {}) {
      let flash = 0;
      for (const z of pool) {
        if (!z.alive) continue;
        z.t += dt;
        const H = z.scale * BASE_HEIGHT;
        z.hitFlash = Math.max(0, z.hitFlash - dt * 2.2);
        z.stagger = Math.max(0, z.stagger - dt * 1.6);
        flash = Math.max(flash, z.hitFlash);

        if (z.state === 'rise') {
          const k = Math.min(1, z.t / 1.8);
          z.g.position.set(z.pos.x, -H + (1 - (1 - k) ** 3) * H, z.pos.z);
          z.arms.forEach((a, i) => (a.rotation.x = -2.4 + Math.sin(z.t * 4 + i) * 0.3));
          if (k >= 1) {
            z.state = 'walk';
            onStomp?.(z.pos.clone(), z.scale);
          }
          continue;
        }

        if (z.state === 'die') {
          const k = Math.min(1, z.t / 2.2);
          z.hips.rotation.x = -k * k * 1.5; // topple backwards
          z.g.position.y = -Math.max(0, z.t - 2.2) * H * 0.4;
          if (z.t > 1.9 && !z.fell) {
            z.fell = true;
            onFall?.(z.pos.clone(), z.scale);
          }
          if (z.t > 4.5) {
            z.alive = false;
            z.fell = false;
            z.g.visible = false;
          }
          continue;
        }

        if (z.state === 'leave') {
          z.g.position.y -= dt * H * 0.25;
          if (z.t > 4) {
            z.alive = false;
            z.g.visible = false;
          }
          continue;
        }

        // walk toward goal (slow, heavy strides)
        const dx = z.goal.x - z.pos.x;
        const dz = z.goal.z - z.pos.z;
        const d = Math.hypot(dx, dz);
        const speed = z.speed * (1 - z.stagger * 0.9);
        if (d > 0.3) {
          const k = Math.min(1, (speed * dt) / d);
          z.pos.x += dx * k;
          z.pos.z += dz * k;
          z.phase += dt * 2.2 * (z.speed / 12);
          const face = Math.atan2(dx, dz);
          let dh = face - z.g.rotation.y;
          dh = Math.atan2(Math.sin(dh), Math.cos(dh));
          z.g.rotation.y += dh * Math.min(1, dt * 1.5);
        } else {
          z.phase += dt * 0.6;
          if (z.face) {
            let dh = Math.atan2(z.face.x - z.pos.x, z.face.z - z.pos.z) - z.g.rotation.y;
            dh = Math.atan2(Math.sin(dh), Math.cos(dh));
            z.g.rotation.y += dh * Math.min(1, dt * 1.5);
          }
        }
        const swing = Math.sin(z.phase);
        z.g.position.set(z.pos.x, Math.abs(swing) * 0.02 * H, z.pos.z);
        z.legs[0].rotation.x = swing * 0.45;
        z.legs[1].rotation.x = -swing * 0.45;
        z.arms[0].rotation.x = -0.3 - swing * 0.35;
        z.arms[1].rotation.x = -0.3 + swing * 0.35;
        z.hips.rotation.x = 0.1 - z.stagger * 0.5; // reel back when hit
        z.neck.rotation.x = -z.stagger * 0.4;
      }
      setFlash(null, flash);
    },
  };
}
