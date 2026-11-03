/**
 * VESPER environment — NUS E4-03-07 under a blood moon, year 6202.
 * Night Singapore at ground level, open space overhead: stars, nebula,
 * a ringed planet and the orbital ring. Everything here is static scenery
 * plus a few ambient animations; game logic lives elsewhere.
 */
import * as THREE from 'three';

export const LAB_CENTER = new THREE.Vector3(0, 0, -20);
export const PERIMETER_RADIUS = 15; // holo-fence arc around the lab front
export const PAD_POSITION = new THREE.Vector3(0, 0, -3);

export function canvasTexture(width, height, draw, { srgb = true } = {}) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  draw(canvas.getContext('2d'), width, height);
  const texture = new THREE.CanvasTexture(canvas);
  if (srgb) texture.colorSpace = THREE.SRGBColorSpace;
  texture.anisotropy = 4;
  return texture;
}

/** Deterministic random so the city looks the same every load. */
export function seeded(seed = 2028) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 4294967296;
  };
}

const SKY_VERT = /* glsl */ `
  varying vec3 vDir;
  void main() {
    vDir = normalize(position);
    vec4 p = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
    gl_Position = p.xyww;
  }
`;

const SKY_FRAG = /* glsl */ `
  uniform float uTime;
  uniform float uAlarm;
  varying vec3 vDir;
  float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
  float noise(vec3 x) {
    vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x), mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x), mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
  }
  float fbm(vec3 p) { float v = 0.0; float a = 0.5; for (int i = 0; i < 5; i++) { v += a * noise(p); p *= 2.03; a *= 0.5; } return v; }
  void main() {
    vec3 d = normalize(vDir);
    float h = d.y;
    vec3 zenith = vec3(0.006, 0.009, 0.03);
    vec3 mid = vec3(0.05, 0.03, 0.10);
    vec3 horizon = vec3(0.26, 0.05, 0.09);
    vec3 col = mix(horizon, mid, smoothstep(-0.02, 0.22, h));
    col = mix(col, zenith, smoothstep(0.22, 0.85, h));
    // Nebula band — magenta and teal dust
    float n = fbm(d * 2.6 + vec3(0.0, 0.0, uTime * 0.004));
    float n2 = fbm(d * 5.5 + 3.1);
    vec3 neb = mix(vec3(0.42, 0.05, 0.26), vec3(0.04, 0.22, 0.34), n2);
    float band = exp(-pow((d.x * 0.55 + d.y - 0.55) * 2.2, 2.0));
    col += neb * pow(max(n - 0.42, 0.0), 1.6) * 2.4 * smoothstep(0.04, 0.4, h) * (0.35 + band);
    // Stars
    vec3 sp = d * 380.0;
    vec3 id = floor(sp);
    float s = hash(id);
    float star = step(0.9972, s) * smoothstep(0.42, 0.0, length(fract(sp) - 0.5));
    float twinkle = 0.55 + 0.45 * sin(uTime * (1.5 + s * 3.0) + s * 90.0);
    vec3 starCol = mix(vec3(1.0, 0.85, 0.8), vec3(0.75, 0.88, 1.0), hash(id + 7.0));
    col += starCol * star * twinkle * smoothstep(0.03, 0.25, h) * 2.2;
    // City glow on the horizon
    col += vec3(0.55, 0.06, 0.10) * pow(1.0 - abs(h), 14.0) * 0.55;
    col = mix(col, col * vec3(1.5, 0.55, 0.55) + vec3(0.06, 0.0, 0.01), uAlarm * 0.65);
    gl_FragColor = vec4(col, 1.0);
  }
`;

/**
 * The procedural sky (nebula fbm + stars) is far too expensive to evaluate per
 * pixel every frame on integrated GPUs, so it is rendered once into a cube map
 * and used as the scene background.
 */
function buildSky(scene, renderer) {
  const uniforms = { uTime: { value: 0 }, uAlarm: { value: 0 } };
  const sky = new THREE.Mesh(
    new THREE.SphereGeometry(900, 48, 32),
    new THREE.ShaderMaterial({ uniforms, vertexShader: SKY_VERT, fragmentShader: SKY_FRAG, side: THREE.BackSide, depthWrite: false, fog: false })
  );
  const bakeScene = new THREE.Scene();
  bakeScene.add(sky);
  const cubeTarget = new THREE.WebGLCubeRenderTarget(1024, { type: THREE.HalfFloatType, generateMipmaps: false });
  const cubeCamera = new THREE.CubeCamera(1, 1000, cubeTarget);
  cubeCamera.update(renderer, bakeScene);
  scene.background = cubeTarget.texture;
  sky.geometry.dispose();
  sky.material.dispose();
  return uniforms;
}

function buildMoon(scene) {
  const moonTex = canvasTexture(512, 512, (g, w, h) => {
    const r = w * 0.42;
    const cx = w / 2;
    const cy = h / 2;
    const base = g.createRadialGradient(cx - r * 0.3, cy - r * 0.3, r * 0.1, cx, cy, r);
    base.addColorStop(0, '#ff9a86');
    base.addColorStop(0.55, '#d8443f');
    base.addColorStop(1, '#5c1018');
    g.fillStyle = base;
    g.beginPath();
    g.arc(cx, cy, r, 0, Math.PI * 2);
    g.fill();
    const rnd = seeded(77);
    g.save();
    g.beginPath();
    g.arc(cx, cy, r, 0, Math.PI * 2);
    g.clip();
    for (let i = 0; i < 70; i++) {
      const a = rnd() * Math.PI * 2;
      const d = Math.sqrt(rnd()) * r;
      const cr = 4 + rnd() * rnd() * 46;
      g.fillStyle = `rgba(70,8,16,${0.12 + rnd() * 0.25})`;
      g.beginPath();
      g.arc(cx + Math.cos(a) * d, cy + Math.sin(a) * d, cr, 0, Math.PI * 2);
      g.fill();
      g.strokeStyle = `rgba(255,170,150,${0.06 + rnd() * 0.12})`;
      g.lineWidth = 1.5;
      g.stroke();
    }
    // terminator shadow
    const shade = g.createLinearGradient(cx - r, cy, cx + r, cy);
    shade.addColorStop(0, 'rgba(0,0,0,0)');
    shade.addColorStop(0.7, 'rgba(10,0,6,0.15)');
    shade.addColorStop(1, 'rgba(10,0,6,0.65)');
    g.fillStyle = shade;
    g.fillRect(0, 0, w, h);
    g.restore();
  });
  const haloTex = canvasTexture(256, 256, (g, w, h) => {
    const grd = g.createRadialGradient(w / 2, h / 2, 0, w / 2, h / 2, w / 2);
    grd.addColorStop(0, 'rgba(255,90,80,0.55)');
    grd.addColorStop(0.35, 'rgba(200,30,50,0.18)');
    grd.addColorStop(1, 'rgba(120,0,30,0)');
    g.fillStyle = grd;
    g.fillRect(0, 0, w, h);
  });
  const position = new THREE.Vector3(-90, 95, -430);
  const moon = new THREE.Sprite(new THREE.SpriteMaterial({ map: moonTex, fog: false, depthWrite: false, color: new THREE.Color(1.4, 1.1, 1.1) }));
  moon.scale.setScalar(150);
  moon.position.copy(position);
  const halo = new THREE.Sprite(new THREE.SpriteMaterial({ map: haloTex, fog: false, depthWrite: false, blending: THREE.AdditiveBlending, transparent: true }));
  halo.scale.setScalar(430);
  halo.position.copy(position).multiplyScalar(1.01);
  moon.renderOrder = -8;
  halo.renderOrder = -9;
  scene.add(halo, moon);
  return { moon, halo, position };
}

function buildPlanetAndRing(scene) {
  const group = new THREE.Group();
  const planetTex = canvasTexture(512, 256, (g, w, h) => {
    const rnd = seeded(9);
    for (let y = 0; y < h; y++) {
      const t = y / h;
      const band = Math.sin(t * 38 + Math.sin(t * 9) * 2) * 0.5 + 0.5;
      const r = 40 + band * 50 + rnd() * 8;
      const gg = 70 + band * 60;
      const b = 120 + band * 70;
      g.fillStyle = `rgb(${r | 0},${gg | 0},${b | 0})`;
      g.fillRect(0, y, w, 1);
    }
  });
  const planet = new THREE.Mesh(
    new THREE.SphereGeometry(38, 48, 32),
    new THREE.MeshStandardMaterial({ map: planetTex, roughness: 1, metalness: 0, emissive: new THREE.Color(0x0b1830), fog: false })
  );
  const ringTex = canvasTexture(512, 8, (g, w) => {
    for (let x = 0; x < w; x++) {
      const t = x / w;
      const a = (0.25 + 0.5 * Math.abs(Math.sin(t * 40))) * Math.sin(t * Math.PI);
      g.fillStyle = `rgba(170,200,255,${a * 0.55})`;
      g.fillRect(x, 0, 1, 8);
    }
  });
  const ringGeo = new THREE.RingGeometry(48, 82, 96, 1);
  // map u along the radius
  const pos = ringGeo.attributes.position;
  const uv = ringGeo.attributes.uv;
  const v = new THREE.Vector3();
  for (let i = 0; i < pos.count; i++) {
    v.fromBufferAttribute(pos, i);
    uv.setXY(i, (v.length() - 48) / 34, 0.5);
  }
  const ring = new THREE.Mesh(
    ringGeo,
    new THREE.MeshBasicMaterial({ map: ringTex, transparent: true, side: THREE.DoubleSide, depthWrite: false, fog: false })
  );
  ring.rotation.x = -Math.PI / 2.35;
  ring.rotation.y = 0.35;
  group.add(planet, ring);
  group.position.set(230, 120, -520);
  group.rotation.z = 0.25;
  scene.add(group);

  // Orbital ring of 6202 — a thin luminous arc around the planet's sky.
  const orbital = new THREE.Mesh(
    new THREE.TorusGeometry(700, 0.9, 6, 320),
    new THREE.MeshBasicMaterial({ color: new THREE.Color(0.35, 0.75, 1.0), transparent: true, opacity: 0.45, fog: false })
  );
  orbital.rotation.set(Math.PI / 2.6, 0.2, 0.4);
  orbital.position.set(0, -120, -100);
  scene.add(orbital);

  // Stations riding the ring
  const stationGeo = new THREE.SphereGeometry(2.6, 8, 6);
  const stations = [];
  for (let i = 0; i < 9; i++) {
    const m = new THREE.Mesh(stationGeo, new THREE.MeshBasicMaterial({ color: i % 3 ? 0x9fd8ff : 0xff5a6a, fog: false }));
    orbital.add(m);
    stations.push({ mesh: m, phase: (i / 9) * Math.PI * 2, speed: 0.004 + (i % 4) * 0.0015 });
  }
  return { group, planet, ring, orbital, stations };
}

function windowTexture(seed, lit = 0.28) {
  return canvasTexture(128, 256, (g, w, h) => {
    const rnd = seeded(seed);
    g.fillStyle = '#000';
    g.fillRect(0, 0, w, h);
    const cols = 8;
    const rows = 28;
    const cw = w / cols;
    const rh = h / rows;
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        if (rnd() > lit) continue;
        const warm = rnd() < 0.35;
        const k = 0.35 + rnd() * 0.65;
        g.fillStyle = warm ? `rgba(255,${(150 * k) | 0},${(120 * k) | 0},${k})` : `rgba(${(140 * k) | 0},${(190 * k) | 0},255,${k})`;
        g.fillRect(c * cw + 3, r * rh + 2, cw - 6, rh - 4);
      }
    }
  });
}

function buildSkyline(scene) {
  const rnd = seeded(6202);
  const geometry = new THREE.BoxGeometry(1, 1, 1);
  geometry.translate(0, 0.5, 0);
  const materials = [11, 23, 37].map(
    (seed) =>
      new THREE.MeshStandardMaterial({
        color: 0x0a0e18,
        roughness: 0.85,
        metalness: 0.3,
        emissive: 0xffffff,
        emissiveMap: windowTexture(seed, 0.22 + (seed % 3) * 0.05),
        emissiveIntensity: 1.6,
      })
  );
  const meshes = materials.map((m) => new THREE.InstancedMesh(geometry, m, 70));
  const counts = [0, 0, 0];
  const beacons = [];
  const dummy = new THREE.Object3D();
  for (let i = 0; i < 200; i++) {
    // Mostly behind the lab (−z) and to the flanks, leaving the field (+z) open.
    const angle = -Math.PI / 2 + (rnd() - 0.5) * Math.PI * 1.55;
    const radius = 110 + rnd() * 170;
    const x = Math.cos(angle) * radius;
    const z = Math.sin(angle) * radius - 20;
    const tall = rnd() < 0.18;
    const height = tall ? 45 + rnd() * 45 : 12 + rnd() * 30;
    const width = 7 + rnd() * 14;
    const depth = 7 + rnd() * 14;
    const k = i % 3;
    if (counts[k] >= 70) continue;
    dummy.position.set(x, -0.5, z);
    dummy.rotation.y = rnd() * Math.PI;
    dummy.scale.set(width, height, depth);
    dummy.updateMatrix();
    meshes[k].setMatrixAt(counts[k]++, dummy.matrix);
    if (height > 40) beacons.push(new THREE.Vector3(x, height + 1, z));
  }
  meshes.forEach((m, k) => {
    m.count = counts[k];
    m.instanceMatrix.needsUpdate = true;
    scene.add(m);
  });

  // Red aviation lights — blink together like the concept skyline.
  const beaconGeo = new THREE.BufferGeometry().setFromPoints(beacons);
  const beaconMat = new THREE.PointsMaterial({
    color: new THREE.Color(3, 0.3, 0.35),
    size: 2.6,
    sizeAttenuation: true,
    transparent: true,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  const beaconPoints = new THREE.Points(beaconGeo, beaconMat);
  scene.add(beaconPoints);
  return { beaconMat };
}

function signTexture() {
  return canvasTexture(1024, 256, (g, w, h) => {
    g.clearRect(0, 0, w, h);
    g.font = '600 150px Bahnschrift, "DIN Alternate", "Segoe UI", sans-serif';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.shadowColor = 'rgba(190,170,255,1)';
    g.shadowBlur = 28;
    g.fillStyle = '#f2ecff';
    g.fillText('NUS   E4-03-07', w / 2, h / 2 + 6);
  });
}

function labFacadeTexture() {
  return canvasTexture(1024, 384, (g, w, h) => {
    const rnd = seeded(4307);
    g.fillStyle = '#05060a';
    g.fillRect(0, 0, w, h);
    const cols = 14;
    const rows = 3;
    const cw = w / cols;
    const rh = h / rows;
    for (let r = 0; r < rows; r++) {
      for (let c = 0; c < cols; c++) {
        const red = rnd() < 0.62;
        const k = 0.35 + rnd() * 0.65;
        const grd = g.createLinearGradient(0, r * rh, 0, (r + 1) * rh);
        if (red) {
          grd.addColorStop(0, `rgba(255,${(40 * k) | 0},${(60 * k) | 0},${0.9 * k})`);
          grd.addColorStop(1, `rgba(120,10,25,${0.5 * k})`);
        } else {
          grd.addColorStop(0, `rgba(90,140,220,${0.55 * k})`);
          grd.addColorStop(1, `rgba(20,40,90,${0.3 * k})`);
        }
        g.fillStyle = grd;
        g.fillRect(c * cw + 5, r * rh + 6, cw - 10, rh - 12);
        // silhouettes of lab equipment
        if (rnd() < 0.35) {
          g.fillStyle = 'rgba(0,0,0,0.6)';
          g.fillRect(c * cw + 10 + rnd() * 20, (r + 1) * rh - 40, 18 + rnd() * 26, 34);
        }
      }
    }
  });
}

function buildLab(scene) {
  const lab = new THREE.Group();
  lab.position.copy(LAB_CENTER);
  const concrete = new THREE.MeshStandardMaterial({ color: 0x1b2230, roughness: 0.92, metalness: 0.1 });
  const darkMetal = new THREE.MeshStandardMaterial({ color: 0x0e131c, roughness: 0.5, metalness: 0.7 });
  const neonRed = new THREE.MeshBasicMaterial({ color: new THREE.Color(4, 0.35, 0.45) });
  const neonCyan = new THREE.MeshBasicMaterial({ color: new THREE.Color(0.4, 2.2, 3.2) });

  const body = new THREE.Mesh(new THREE.BoxGeometry(36, 13, 14), concrete);
  body.position.y = 6.5;
  lab.add(body);

  const upper = new THREE.Mesh(new THREE.BoxGeometry(30, 4, 11), concrete);
  upper.position.set(-2, 15, -1);
  lab.add(upper);

  const facadeMat = new THREE.MeshStandardMaterial({
    color: 0x05070c,
    roughness: 0.15,
    metalness: 0.6,
    emissive: 0xffffff,
    emissiveMap: labFacadeTexture(),
    emissiveIntensity: 2.2,
  });
  const facade = new THREE.Mesh(new THREE.PlaneGeometry(33, 9.5), facadeMat);
  facade.position.set(0, 5.6, 7.02);
  lab.add(facade);

  // Mullions
  for (let i = -7; i <= 7; i++) {
    const m = new THREE.Mesh(new THREE.BoxGeometry(0.22, 9.6, 0.3), darkMetal);
    m.position.set(i * 2.36, 5.6, 7.1);
    lab.add(m);
  }
  const slab = new THREE.Mesh(new THREE.BoxGeometry(38, 0.6, 16), darkMetal);
  slab.position.set(0, 13.2, 0.6);
  lab.add(slab);

  // Neon roofline + entrance strips (red, like the concept art)
  const strip = (w, x, y, z, mat = neonRed) => {
    const s = new THREE.Mesh(new THREE.BoxGeometry(w, 0.14, 0.14), mat);
    s.position.set(x, y, z);
    lab.add(s);
    return s;
  };
  strip(37.6, 0, 12.85, 8.65);
  strip(14, 9, 0.9, 7.3);
  strip(8, -12, 0.9, 7.3, neonCyan);
  strip(29.6, -2, 17.05, 4.55);

  // Sign
  const sign = new THREE.Mesh(
    new THREE.PlaneGeometry(14, 3.5),
    new THREE.MeshBasicMaterial({ map: signTexture(), transparent: true, color: new THREE.Color(2.2, 2.0, 2.6), depthWrite: false })
  );
  sign.position.set(6, 15.2, 4.6);
  lab.add(sign);

  // Rooftop antenna array + dish
  for (const [x, hgt] of [[-12, 7], [-9, 5], [11, 9]]) {
    const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.18, hgt, 6), darkMetal);
    mast.position.set(x, 17 + hgt / 2, -2);
    lab.add(mast);
    const tip = new THREE.Mesh(new THREE.SphereGeometry(0.28, 8, 6), neonRed);
    tip.position.set(x, 17 + hgt, -2);
    lab.add(tip);
  }
  const dish = new THREE.Mesh(new THREE.SphereGeometry(2.2, 20, 10, 0, Math.PI * 2, 0, Math.PI / 2.6), darkMetal);
  dish.rotation.x = -0.9;
  dish.position.set(-4, 18.2, -2);
  lab.add(dish);

  // Side annex
  const annex = new THREE.Mesh(new THREE.BoxGeometry(12, 7, 12), concrete);
  annex.position.set(-25, 3.5, -2);
  lab.add(annex);

  // Glow spilling onto the plaza
  // One wide red spill instead of several lights — each real light costs every lit pixel.
  const spill = new THREE.PointLight(0xff2a44, 45, 30, 1.4);
  spill.position.set(2, 7, 7.5);
  lab.add(spill);

  scene.add(lab);
  return { lab, facadeMat };
}

function buildGround(scene) {
  const tex = canvasTexture(
    512,
    512,
    (g, w, h) => {
      const rnd = seeded(3);
      g.fillStyle = '#0b0e15';
      g.fillRect(0, 0, w, h);
      for (let i = 0; i < 9000; i++) {
        const v = 10 + rnd() * 18;
        g.fillStyle = `rgb(${v},${v + 2},${v + 8})`;
        g.fillRect(rnd() * w, rnd() * h, 2, 2);
      }
      g.strokeStyle = 'rgba(90,120,170,0.08)';
      g.lineWidth = 2;
      for (let i = 0; i <= 8; i++) {
        g.beginPath();
        g.moveTo((i * w) / 8, 0);
        g.lineTo((i * w) / 8, h);
        g.moveTo(0, (i * h) / 8);
        g.lineTo(w, (i * h) / 8);
        g.stroke();
      }
    }
  );
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(60, 60);
  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(1200, 1200),
    new THREE.MeshStandardMaterial({ map: tex, roughness: 0.5, metalness: 0.3, color: 0x4a5368 })
  );
  ground.rotation.x = -Math.PI / 2;
  scene.add(ground);

  // Plaza in front of the lab
  const plaza = new THREE.Mesh(
    new THREE.PlaneGeometry(44, 22),
    new THREE.MeshStandardMaterial({ color: 0x141a26, roughness: 0.62, metalness: 0.3 })
  );
  plaza.rotation.x = -Math.PI / 2;
  plaza.position.set(0, 0.01, -4);
  scene.add(plaza);

  // Drone landing pad
  const padTex = canvasTexture(256, 256, (g, w, h) => {
    g.clearRect(0, 0, w, h);
    g.strokeStyle = 'rgba(110,220,255,0.95)';
    g.lineWidth = 6;
    g.beginPath();
    g.arc(w / 2, h / 2, w * 0.44, 0, Math.PI * 2);
    g.stroke();
    g.lineWidth = 2;
    g.setLineDash([10, 10]);
    g.beginPath();
    g.arc(w / 2, h / 2, w * 0.34, 0, Math.PI * 2);
    g.stroke();
    g.setLineDash([]);
    g.font = '700 70px Bahnschrift, "Segoe UI", sans-serif';
    g.fillStyle = 'rgba(110,220,255,0.9)';
    g.textAlign = 'center';
    g.textBaseline = 'middle';
    g.fillText('V', w / 2, h / 2 + 4);
  });
  const pad = new THREE.Mesh(
    new THREE.PlaneGeometry(6.5, 6.5),
    new THREE.MeshBasicMaterial({ map: padTex, transparent: true, color: new THREE.Color(0.9, 0.9, 0.9), depthWrite: false })
  );
  pad.rotation.x = -Math.PI / 2;
  pad.position.copy(PAD_POSITION).setY(0.03);
  scene.add(pad);
  return { pad };
}

function buildPerimeter(scene) {
  const group = new THREE.Group();
  const postMat = new THREE.MeshStandardMaterial({ color: 0x10151f, roughness: 0.4, metalness: 0.8 });
  const capMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(3.5, 0.3, 0.4) });
  const fieldTex = canvasTexture(256, 64, (g, w, h) => {
    const grd = g.createLinearGradient(0, h, 0, 0);
    grd.addColorStop(0, 'rgba(255,60,80,0.55)');
    grd.addColorStop(1, 'rgba(255,60,80,0)');
    g.fillStyle = grd;
    g.fillRect(0, 0, w, h);
    g.strokeStyle = 'rgba(255,140,150,0.35)';
    for (let x = 0; x < w; x += 16) {
      g.beginPath();
      g.moveTo(x, h);
      g.lineTo(x + 32, 0);
      g.stroke();
    }
  });
  fieldTex.wrapS = THREE.RepeatWrapping;
  const fieldMat = new THREE.MeshBasicMaterial({
    map: fieldTex,
    transparent: true,
    opacity: 0.45,
    side: THREE.DoubleSide,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  const segments = 18;
  const start = -Math.PI * 0.42;
  const end = Math.PI * 0.42;
  let prev = null;
  for (let i = 0; i <= segments; i++) {
    const a = start + ((end - start) * i) / segments;
    const x = LAB_CENTER.x + Math.sin(a) * PERIMETER_RADIUS * 1.55;
    const z = LAB_CENTER.z + Math.cos(a) * PERIMETER_RADIUS;
    const post = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.16, 2.2, 6), postMat);
    post.position.set(x, 1.1, z);
    const cap = new THREE.Mesh(new THREE.SphereGeometry(0.14, 6, 4), capMat);
    cap.position.set(x, 2.25, z);
    group.add(post, cap);
    if (prev) {
      const mid = new THREE.Vector3((prev.x + x) / 2, 1.05, (prev.z + z) / 2);
      const len = Math.hypot(x - prev.x, z - prev.z);
      const panel = new THREE.Mesh(new THREE.PlaneGeometry(len, 2), fieldMat);
      panel.position.copy(mid);
      panel.rotation.y = -Math.atan2(z - prev.z, x - prev.x);
      group.add(panel);
    }
    prev = { x, z };
  }
  scene.add(group);
  return { fieldTex, fieldMat };
}

function buildTrees(scene) {
  const rnd = seeded(808);
  const trunkGeo = new THREE.CylinderGeometry(0.18, 0.32, 1, 6);
  trunkGeo.translate(0, 0.5, 0);
  const crownGeo = new THREE.IcosahedronGeometry(1, 1);
  const trunkMat = new THREE.MeshStandardMaterial({ color: 0x07090c, roughness: 1 });
  const crownMat = new THREE.MeshStandardMaterial({ color: 0x0a1414, roughness: 1, emissive: 0x16050a, emissiveIntensity: 0.6 });
  const n = 46;
  const trunks = new THREE.InstancedMesh(trunkGeo, trunkMat, n);
  const crowns = new THREE.InstancedMesh(crownGeo, crownMat, n * 3);
  const d = new THREE.Object3D();
  let c = 0;
  for (let i = 0; i < n; i++) {
    const side = i % 2 ? 1 : -1;
    const x = side * (26 + rnd() * 40);
    const z = -40 + rnd() * 90;
    const h = 5 + rnd() * 6;
    d.position.set(x, 0, z);
    d.rotation.set(0, rnd() * 6, 0);
    d.scale.set(1, h, 1);
    d.updateMatrix();
    trunks.setMatrixAt(i, d.matrix);
    for (let k = 0; k < 3; k++) {
      const s = 1.8 + rnd() * 2.2;
      d.position.set(x + (rnd() - 0.5) * 3, h + (rnd() - 0.3) * 2, z + (rnd() - 0.5) * 3);
      d.scale.set(s, s * 0.8, s);
      d.updateMatrix();
      crowns.setMatrixAt(c++, d.matrix);
    }
  }
  crowns.count = c;
  scene.add(trunks, crowns);
}

function buildLamps(scene) {
  const poleMat = new THREE.MeshStandardMaterial({ color: 0x111720, metalness: 0.7, roughness: 0.4 });
  const headMat = new THREE.MeshBasicMaterial({ color: new THREE.Color(1.4, 2.4, 3.2) });
  for (const [x, z] of [[-16, 6], [16, 6], [-24, 14], [24, 14]]) {
    const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.14, 7, 6), poleMat);
    pole.position.set(x, 3.5, z);
    const head = new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.15, 0.35), headMat);
    head.position.set(x + (x < 0 ? 0.5 : -0.5), 7, z);
    scene.add(pole, head);
  }
}

function buildEmbers(scene) {
  const n = 500;
  const rnd = seeded(99);
  const positions = new Float32Array(n * 3);
  const speeds = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    positions[i * 3] = (rnd() - 0.5) * 120;
    positions[i * 3 + 1] = rnd() * 30;
    positions[i * 3 + 2] = (rnd() - 0.5) * 120;
    speeds[i] = 0.3 + rnd() * 1.1;
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  const mat = new THREE.PointsMaterial({
    color: new THREE.Color(2.4, 0.55, 0.35),
    size: 0.16,
    transparent: true,
    opacity: 0.8,
    depthWrite: false,
    blending: THREE.AdditiveBlending,
  });
  const points = new THREE.Points(geo, mat);
  scene.add(points);
  return {
    update(dt, t) {
      for (let i = 0; i < n; i++) {
        positions[i * 3 + 1] += speeds[i] * dt;
        positions[i * 3] += Math.sin(t * 0.6 + i) * 0.4 * dt;
        if (positions[i * 3 + 1] > 30) positions[i * 3 + 1] = 0;
      }
      geo.attributes.position.needsUpdate = true;
    },
  };
}

function buildGroundFog(scene) {
  const tex = canvasTexture(256, 256, (g, w, h) => {
    const rnd = seeded(5);
    g.clearRect(0, 0, w, h);
    for (let i = 0; i < 60; i++) {
      const x = rnd() * w;
      const y = rnd() * h;
      const r = 20 + rnd() * 60;
      const grd = g.createRadialGradient(x, y, 0, x, y, r);
      grd.addColorStop(0, 'rgba(120,90,150,0.07)');
      grd.addColorStop(1, 'rgba(120,90,150,0)');
      g.fillStyle = grd;
      g.fillRect(0, 0, w, h);
    }
  });
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  const sheets = [];
  for (let i = 0; i < 2; i++) {
    const m = new THREE.Mesh(
      new THREE.PlaneGeometry(220, 220),
      new THREE.MeshBasicMaterial({ map: tex.clone(), transparent: true, opacity: 0.22, depthWrite: false, fog: true })
    );
    m.material.map.repeat.set(2 + i, 2 + i);
    m.material.map.wrapS = m.material.map.wrapT = THREE.RepeatWrapping;
    m.material.map.needsUpdate = true;
    m.rotation.x = -Math.PI / 2;
    m.position.y = 0.25 + i * 0.5;
    scene.add(m);
    sheets.push(m);
  }
  return {
    update(dt) {
      sheets.forEach((s, i) => {
        s.material.map.offset.x += dt * 0.004 * (i + 1);
        s.material.map.offset.y += dt * 0.002 * (i % 2 ? -1 : 1);
      });
    },
  };
}

export function buildEnvironment(scene, renderer) {
  const fogCalm = new THREE.Color(0x120b1d);
  const fogAlarm = new THREE.Color(0x2a0710);
  scene.fog = new THREE.FogExp2(fogCalm.getHex(), 0.0105);

  const hemi = new THREE.HemisphereLight(0x2c3466, 0x0c0406, 0.75);
  scene.add(hemi);
  const moonLight = new THREE.DirectionalLight(0xff6a6a, 1.1);
  moonLight.position.set(-120, 150, -420).normalize().multiplyScalar(100);
  scene.add(moonLight);
  const fill = new THREE.DirectionalLight(0x5f7dff, 0.45);
  fill.position.set(60, 40, 80);
  scene.add(fill);

  const sky = buildSky(scene, renderer);
  const moon = buildMoon(scene);
  const space = buildPlanetAndRing(scene);
  const skyline = buildSkyline(scene);
  const lab = buildLab(scene);
  const ground = buildGround(scene);
  const perimeter = buildPerimeter(scene);
  buildTrees(scene);
  buildLamps(scene);
  const embers = buildEmbers(scene);
  const groundFog = buildGroundFog(scene);

  let alarm = 0;
  let alarmNow = 0;
  return {
    moonPosition: moon.position,
    setAlarm(level) {
      alarm = level;
    },
    update(dt, t) {
      alarmNow += (alarm - alarmNow) * Math.min(1, dt * 3);
      const a = alarmNow;
      scene.fog.color.copy(fogCalm).lerp(fogAlarm, a);
      scene.backgroundIntensity = 1 + a * 0.35;
      skyline.beaconMat.opacity = Math.sin(t * Math.PI) > 0 ? 1 : 0.15;
      perimeter.fieldTex.offset.x -= dt * 0.25;
      perimeter.fieldMat.opacity = 0.35 + a * 0.4 + Math.sin(t * 6) * 0.05 * a;
      lab.facadeMat.emissiveIntensity = 2.2 + a * (Math.sin(t * 12.5) > 0 ? 1.5 : -0.8);
      space.planet.rotation.y += dt * 0.01;
      space.stations.forEach((s) => {
        const ang = s.phase + t * s.speed;
        s.mesh.position.set(Math.cos(ang) * 700, Math.sin(ang) * 700, 0);
      });
      moon.halo.material.opacity = 0.85 + Math.sin(t * 0.5) * 0.1 + a * 0.3;
      embers.update(dt, t);
      groundFog.update(dt);
    },
  };
}
