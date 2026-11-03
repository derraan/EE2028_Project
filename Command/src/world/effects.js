/**
 * Pooled visual effects: particles, laser beams, spore meteors, shockwave
 * rings, flashes. Nothing here changes game or drone state; callers decide
 * when an effect plays (from parsed firmware events or world fiction).
 */
import * as THREE from 'three';

const PARTICLE_VERT = /* glsl */ `
  attribute float aSize;
  attribute float aAlpha;
  attribute vec3 aColor;
  varying float vAlpha;
  varying vec3 vColor;
  void main() {
    vAlpha = aAlpha;
    vColor = aColor;
    vec4 mv = modelViewMatrix * vec4(position, 1.0);
    gl_PointSize = aSize * (300.0 / -mv.z);
    gl_Position = projectionMatrix * mv;
  }
`;
const PARTICLE_FRAG = /* glsl */ `
  varying float vAlpha;
  varying vec3 vColor;
  void main() {
    vec2 c = gl_PointCoord - 0.5;
    float d = length(c);
    if (d > 0.5) discard;
    float a = smoothstep(0.5, 0.0, d) * vAlpha;
    gl_FragColor = vec4(vColor * a, a);
  }
`;

function createParticles(scene, capacity = 3000) {
  const positions = new Float32Array(capacity * 3);
  const colors = new Float32Array(capacity * 3);
  const sizes = new Float32Array(capacity);
  const alphas = new Float32Array(capacity);
  const vel = new Float32Array(capacity * 3);
  const life = new Float32Array(capacity);
  const maxLife = new Float32Array(capacity);
  const gravity = new Float32Array(capacity);
  const baseSize = new Float32Array(capacity);
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  geo.setAttribute('aColor', new THREE.BufferAttribute(colors, 3));
  geo.setAttribute('aSize', new THREE.BufferAttribute(sizes, 1));
  geo.setAttribute('aAlpha', new THREE.BufferAttribute(alphas, 1));
  const mat = new THREE.ShaderMaterial({
    vertexShader: PARTICLE_VERT,
    fragmentShader: PARTICLE_FRAG,
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  const points = new THREE.Points(geo, mat);
  points.frustumCulled = false;
  scene.add(points);
  let cursor = 0;
  const color = new THREE.Color();

  return {
    burst(origin, { count = 30, color: c = 0xff4455, speed = 6, life: l = 0.9, gravity: g = -6, size = 0.5, spread = 1, up = 0.4, intensity = 2 } = {}) {
      color.set(c).multiplyScalar(intensity);
      for (let n = 0; n < count; n++) {
        const i = cursor;
        cursor = (cursor + 1) % capacity;
        positions[i * 3] = origin.x + (Math.random() - 0.5) * 0.3;
        positions[i * 3 + 1] = origin.y + (Math.random() - 0.5) * 0.3;
        positions[i * 3 + 2] = origin.z + (Math.random() - 0.5) * 0.3;
        const theta = Math.random() * Math.PI * 2;
        const phi = Math.acos(2 * Math.random() - 1);
        const s = speed * (0.35 + Math.random() * 0.65);
        vel[i * 3] = Math.sin(phi) * Math.cos(theta) * s * spread;
        vel[i * 3 + 1] = Math.abs(Math.cos(phi)) * s * up + Math.cos(phi) * s * (1 - up);
        vel[i * 3 + 2] = Math.sin(phi) * Math.sin(theta) * s * spread;
        colors[i * 3] = color.r;
        colors[i * 3 + 1] = color.g;
        colors[i * 3 + 2] = color.b;
        life[i] = maxLife[i] = l * (0.6 + Math.random() * 0.6);
        gravity[i] = g;
        baseSize[i] = size * (0.5 + Math.random());
      }
    },
    update(dt) {
      for (let i = 0; i < capacity; i++) {
        if (life[i] <= 0) {
          if (alphas[i] !== 0) alphas[i] = 0;
          continue;
        }
        life[i] -= dt;
        const k = Math.max(0, life[i] / maxLife[i]);
        vel[i * 3 + 1] += gravity[i] * dt;
        vel[i * 3] *= 1 - dt * 1.4;
        vel[i * 3 + 2] *= 1 - dt * 1.4;
        positions[i * 3] += vel[i * 3] * dt;
        positions[i * 3 + 1] = Math.max(0.05, positions[i * 3 + 1] + vel[i * 3 + 1] * dt);
        positions[i * 3 + 2] += vel[i * 3 + 2] * dt;
        alphas[i] = k;
        sizes[i] = baseSize[i] * (0.4 + k * 0.6);
      }
      geo.attributes.position.needsUpdate = true;
      geo.attributes.aAlpha.needsUpdate = true;
      geo.attributes.aSize.needsUpdate = true;
      geo.attributes.aColor.needsUpdate = true;
    },
  };
}

function glowTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  const grd = g.createRadialGradient(64, 64, 0, 64, 64, 64);
  grd.addColorStop(0, 'rgba(255,255,255,1)');
  grd.addColorStop(0.25, 'rgba(255,255,255,0.6)');
  grd.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = grd;
  g.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

export function createEffects(scene) {
  const particles = createParticles(scene);
  const glow = glowTexture();
  const tmp = new THREE.Vector3();
  const up = new THREE.Vector3(0, 1, 0);

  // ---- laser beams
  const beamGeo = new THREE.CylinderGeometry(1, 1, 1, 10, 1, true);
  beamGeo.translate(0, 0.5, 0);
  const beams = Array.from({ length: 6 }, () => {
    const core = new THREE.Mesh(beamGeo, new THREE.MeshBasicMaterial({ color: new THREE.Color(6, 1.6, 1.8), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
    const outer = new THREE.Mesh(beamGeo, new THREE.MeshBasicMaterial({ color: new THREE.Color(2.5, 0.15, 0.3), transparent: true, opacity: 0.5, depthWrite: false, blending: THREE.AdditiveBlending }));
    const g = new THREE.Group();
    g.add(core, outer);
    g.visible = false;
    scene.add(g);
    return { g, core, outer, life: 0 };
  });

  // ---- flashes (sprite + point light)
  const flashes = Array.from({ length: 6 }, () => {
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: glow, color: 0xffffff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
    sprite.visible = false;
    scene.add(sprite);
    return { sprite, life: 0, max: 1, size: 1 };
  });
  const flashLights = Array.from({ length: 2 }, () => {
    const l = new THREE.PointLight(0xff4040, 0, 30, 1.7);
    scene.add(l);
    return { l, life: 0, max: 1, peak: 0 };
  });

  // ---- rings (shockwaves / scans)
  const ringGeo = new THREE.RingGeometry(0.85, 1, 64);
  ringGeo.rotateX(-Math.PI / 2);
  const rings = Array.from({ length: 8 }, () => {
    const m = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ color: 0xffffff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide }));
    m.visible = false;
    scene.add(m);
    return { m, life: 0, max: 1, radius: 1, color: new THREE.Color() };
  });

  // ---- crater glows
  const craterGeo = new THREE.CircleGeometry(1, 32);
  craterGeo.rotateX(-Math.PI / 2);
  const craters = Array.from({ length: 8 }, () => {
    const m = new THREE.Mesh(craterGeo, new THREE.MeshBasicMaterial({ map: glow, color: new THREE.Color(0.6, 2.2, 0.7), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
    m.visible = false;
    scene.add(m);
    return { m, life: 0, max: 1 };
  });

  // ---- meteors (spore pods falling from orbit)
  const trailGeo = new THREE.CylinderGeometry(0.05, 0.9, 1, 8, 1, true);
  trailGeo.translate(0, 0.5, 0);
  const meteors = [];
  function makeMeteor() {
    const head = new THREE.Sprite(new THREE.SpriteMaterial({ map: glow, color: new THREE.Color(2.5, 3, 1.6), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending }));
    const trail = new THREE.Mesh(trailGeo, new THREE.MeshBasicMaterial({ color: new THREE.Color(0.9, 2.2, 0.8), transparent: true, opacity: 0.55, depthWrite: false, blending: THREE.AdditiveBlending }));
    const g = new THREE.Group();
    g.add(head, trail);
    g.visible = false;
    scene.add(g);
    return { g, head, trail, from: new THREE.Vector3(), to: new THREE.Vector3(), t: 0, dur: 1, active: false, onImpact: null, ambient: false };
  }
  for (let i = 0; i < 6; i++) meteors.push(makeMeteor());

  function setBeam(beam, from, to, radius) {
    tmp.subVectors(to, from);
    const len = tmp.length();
    beam.g.position.copy(from);
    beam.g.quaternion.setFromUnitVectors(up, tmp.normalize());
    beam.core.scale.set(radius * 0.35, len, radius * 0.35);
    beam.outer.scale.set(radius, len, radius);
  }

  function flash(position, { color = 0xff4050, size = 6, life = 0.35, light = 40 } = {}) {
    const f = flashes.find((x) => x.life <= 0) || flashes[0];
    f.sprite.position.copy(position);
    f.sprite.material.color.set(color).multiplyScalar(2);
    f.size = size;
    f.life = f.max = life;
    f.sprite.visible = true;
    if (light > 0) {
      const fl = flashLights.find((x) => x.life <= 0) || flashLights[0];
      fl.l.position.copy(position);
      fl.l.color.set(color);
      fl.peak = light;
      fl.life = fl.max = life * 1.4;
    }
  }

  function ring(position, { color = 0x5fe3ff, radius = 8, life = 0.9, y = 0.06, strength = 2.2 } = {}) {
    const r = rings.find((x) => x.life <= 0) || rings[0];
    r.m.position.set(position.x, y, position.z);
    r.color.set(color).multiplyScalar(strength);
    r.m.material.color.copy(r.color);
    r.radius = radius;
    r.life = r.max = life;
    r.m.visible = true;
  }

  function crater(position, life = 14) {
    const c = craters.find((x) => x.life <= 0) || craters[0];
    c.m.position.set(position.x, 0.04, position.z);
    c.m.scale.setScalar(3.2);
    c.life = c.max = life;
    c.m.visible = true;
  }

  return {
    particles,
    flash,
    ring,

    laser(from, to) {
      const b = beams.find((x) => x.life <= 0) || beams[0];
      b.life = 0.28;
      b.g.visible = true;
      setBeam(b, from, to, 0.22);
      b.from = from.clone();
      b.to = to.clone();
      flash(from, { color: 0xff5060, size: 2.2, life: 0.18, light: 25 });
      flash(to, { color: 0xff6a70, size: 4, life: 0.3, light: 45 });
      particles.burst(to, { count: 40, color: 0xff5a4a, speed: 7, life: 0.7, size: 0.45, gravity: -9 });
    },

    disintegrate(position) {
      particles.burst(position, { count: 55, color: 0x8dff6a, speed: 4.5, life: 1.2, size: 0.55, gravity: 2.5, up: 0.8, intensity: 1.6 });
      particles.burst(position, { count: 25, color: 0xff5a4a, speed: 6, life: 0.6, size: 0.35, gravity: -8 });
    },

    sparks(position, color = 0xffb35a) {
      particles.burst(position, { count: 18, color, speed: 6, life: 0.45, size: 0.3, gravity: -14 });
    },

    explosion(position, scale = 1) {
      flash(position, { color: 0xff7040, size: 22 * scale, life: 0.9, light: 140 * scale });
      ring(position, { color: 0xff6040, radius: 26 * scale, life: 1.2 });
      ring(position, { color: 0xffd090, radius: 14 * scale, life: 0.7 });
      particles.burst(position, { count: 260, color: 0xff7a3a, speed: 16 * scale, life: 1.6, size: 0.9, gravity: -6, up: 0.6 });
      particles.burst(position, { count: 120, color: 0xffe0a0, speed: 9 * scale, life: 0.8, size: 0.6, gravity: -2 });
      particles.burst(position, { count: 90, color: 0x5fe3ff, speed: 12 * scale, life: 1.1, size: 0.4, gravity: -10 });
    },

    scan(position) {
      ring(position, { color: 0x5fe3ff, radius: 9, life: 1.0, y: 0.08, strength: 0.45 });
    },

    /**
     * Drop a spore meteor from orbit onto `to`. `onImpact` fires on touchdown.
     * Ambient meteors streak across the sky and never land.
     */
    meteor(to, { onImpact = null, ambient = false, duration = 1.6 } = {}) {
      const m = meteors.find((x) => !x.active);
      if (!m) return false;
      m.active = true;
      m.ambient = ambient;
      m.onImpact = onImpact;
      m.t = 0;
      m.dur = duration;
      m.to.copy(to);
      if (ambient) {
        const a = Math.random() * Math.PI * 2;
        m.from.set(Math.cos(a) * 300, 260 + Math.random() * 80, -200 + Math.sin(a) * 200);
        m.to.set(m.from.x + (Math.random() - 0.5) * 400, 120 + Math.random() * 60, m.from.z + (Math.random() - 0.5) * 200);
      } else {
        m.from.set(to.x + 60 + Math.random() * 60, 180, to.z - 90 - Math.random() * 60);
      }
      m.g.visible = true;
      return true;
    },

    update(dt) {
      particles.update(dt);
      for (const b of beams) {
        if (b.life <= 0) continue;
        b.life -= dt;
        const k = Math.max(0, b.life / 0.28);
        b.core.material.opacity = k;
        b.outer.material.opacity = 0.5 * k;
        setBeam(b, b.from, b.to, 0.22 * (0.5 + k));
        if (b.life <= 0) b.g.visible = false;
      }
      for (const f of flashes) {
        if (f.life <= 0) continue;
        f.life -= dt;
        const k = Math.max(0, f.life / f.max);
        f.sprite.scale.setScalar(f.size * (1.4 - k * 0.4));
        f.sprite.material.opacity = k;
        if (f.life <= 0) f.sprite.visible = false;
      }
      for (const fl of flashLights) {
        if (fl.life <= 0) {
          fl.l.intensity = 0;
          continue;
        }
        fl.life -= dt;
        fl.l.intensity = fl.peak * Math.max(0, fl.life / fl.max) ** 2;
      }
      for (const r of rings) {
        if (r.life <= 0) continue;
        r.life -= dt;
        const k = Math.max(0, r.life / r.max);
        r.m.scale.setScalar(0.5 + (1 - k) * r.radius);
        r.m.material.opacity = k;
        if (r.life <= 0) r.m.visible = false;
      }
      for (const c of craters) {
        if (c.life <= 0) continue;
        c.life -= dt;
        const k = Math.max(0, c.life / c.max);
        c.m.material.opacity = k * (0.75 + Math.sin(c.life * 6) * 0.15);
        if (c.life <= 0) c.m.visible = false;
      }
      for (const m of meteors) {
        if (!m.active) continue;
        m.t += dt;
        const k = Math.min(1, m.t / m.dur);
        const e = m.ambient ? k : k * k;
        tmp.lerpVectors(m.from, m.to, e);
        m.head.position.copy(tmp);
        m.head.scale.setScalar(m.ambient ? 3 : 5);
        // trail points back toward the origin
        const dir = new THREE.Vector3().subVectors(m.from, tmp);
        const len = Math.min(dir.length(), m.ambient ? 40 : 28);
        m.trail.position.copy(tmp);
        m.trail.quaternion.setFromUnitVectors(up, dir.normalize());
        m.trail.scale.set(m.ambient ? 0.6 : 1, len, m.ambient ? 0.6 : 1);
        m.trail.material.opacity = m.ambient ? 0.35 * (1 - k) + 0.1 : 0.55;
        if (k >= 1) {
          m.active = false;
          m.g.visible = false;
          if (!m.ambient) {
            flash(m.to.clone().setY(1.5), { color: 0x9dff7a, size: 18, life: 0.7, light: 90 });
            ring(m.to, { color: 0x9dff7a, radius: 14, life: 1.1 });
            crater(m.to);
            particles.burst(m.to.clone().setY(0.5), { count: 160, color: 0x9dff7a, speed: 12, life: 1.4, size: 0.7, gravity: -9, up: 0.8 });
            particles.burst(m.to.clone().setY(0.5), { count: 80, color: 0xff7a4a, speed: 8, life: 1, size: 0.5, gravity: -12 });
            m.onImpact?.(m.to.clone());
          }
        }
      }
    },
  };
}
