/**
 * VESPER-07 defence drone — swept-wing silhouette from the concept art,
 * cyan engine pods, red nav lights, underslung laser turret and searchlight.
 *
 * The drone *poses* according to the mode the firmware reports:
 *   STANDBY  — low patrol over the pad, cyan running lights, searchlight dim
 *   BATTLE   — climbs, red searchlight on, nav lights blink 1 Hz, turret tracks
 *   THE LAST — crashes and lies inverted, sparks, 2 Hz red strobe
 *   TERMINATED — hidden (the world plays the self-destruct explosion)
 */
import * as THREE from 'three';
import { PAD_POSITION } from './environment.js';

function wingGeometry() {
  const s = new THREE.Shape();
  s.moveTo(0, 0.9);
  s.lineTo(4.6, -0.4);
  s.lineTo(4.9, -1.0);
  s.lineTo(4.2, -1.05);
  s.lineTo(0.6, -0.55);
  s.lineTo(0, -0.7);
  s.lineTo(0, 0.9);
  const g = new THREE.ExtrudeGeometry(s, { depth: 0.16, bevelEnabled: true, bevelSize: 0.05, bevelThickness: 0.05, bevelSegments: 1 });
  g.rotateX(Math.PI / 2);
  g.translate(0, 0.08, 0);
  return g;
}

export function createDrone(scene) {
  const root = new THREE.Group(); // world position + heading
  const body = new THREE.Group(); // bank / flip / bob
  root.add(body);

  const hull = new THREE.MeshStandardMaterial({ color: 0x161c26, metalness: 0.85, roughness: 0.32 });
  const panel = new THREE.MeshStandardMaterial({ color: 0x232b38, metalness: 0.7, roughness: 0.45 });
  const glass = new THREE.MeshStandardMaterial({ color: 0x05080d, metalness: 1, roughness: 0.05, emissive: 0x0a2a44, emissiveIntensity: 0.6 });
  const cyan = new THREE.MeshBasicMaterial({ color: new THREE.Color(0.5, 2.6, 3.6) });
  const red = new THREE.MeshBasicMaterial({ color: new THREE.Color(4, 0.25, 0.35) });

  // Fuselage
  const fus = new THREE.Mesh(new THREE.CapsuleGeometry(0.62, 2.6, 6, 12), hull);
  fus.rotation.x = Math.PI / 2;
  fus.scale.set(1, 1, 0.62);
  body.add(fus);
  const spine = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.3, 2.6), panel);
  spine.position.set(0, 0.42, -0.2);
  body.add(spine);
  const canopy = new THREE.Mesh(new THREE.SphereGeometry(0.46, 16, 10, 0, Math.PI * 2, 0, Math.PI / 2), glass);
  canopy.scale.set(1, 0.7, 1.7);
  canopy.position.set(0, 0.22, 0.9);
  body.add(canopy);

  // Wings
  const wg = wingGeometry();
  const wingR = new THREE.Mesh(wg, hull);
  wingR.position.set(0.3, 0, 0);
  const wingL = wingR.clone();
  wingL.scale.x = -1;
  wingL.position.x = -0.3;
  body.add(wingR, wingL);

  // Engine pods at the wing tips — cyan exhaust like the concept art
  const pods = [];
  for (const side of [1, -1]) {
    const pod = new THREE.Group();
    pod.position.set(side * 4.75, 0.08, -0.65);
    const shell = new THREE.Mesh(new THREE.CylinderGeometry(0.26, 0.32, 1.7, 12), panel);
    shell.rotation.x = Math.PI / 2;
    const glow = new THREE.Mesh(new THREE.CircleGeometry(0.24, 16), cyan);
    glow.position.z = -0.86;
    glow.rotation.y = Math.PI;
    const streak = new THREE.Mesh(new THREE.BoxGeometry(0.06, 0.06, 1.2), cyan);
    streak.position.set(side * -0.05, 0, 0.25);
    const tipLight = new THREE.Mesh(new THREE.SphereGeometry(0.09, 8, 6), red);
    tipLight.position.set(side * 0.3, 0.05, 0.55);
    pod.add(shell, glow, streak, tipLight);
    body.add(pod);
    pods.push({ pod, glow, tipLight });
  }

  // Tail fins
  for (const side of [1, -1]) {
    const fin = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.7, 0.9), hull);
    fin.position.set(side * 0.38, 0.55, -1.55);
    fin.rotation.z = side * -0.35;
    body.add(fin);
  }

  // Underslung turret + laser emitter
  const turret = new THREE.Group();
  turret.position.set(0, -0.62, 0.55);
  const turretBall = new THREE.Mesh(new THREE.SphereGeometry(0.34, 14, 10), panel);
  const barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.1, 1.0, 10), hull);
  barrel.rotation.x = Math.PI / 2;
  barrel.position.z = 0.5;
  const muzzle = new THREE.Mesh(new THREE.SphereGeometry(0.09, 8, 6), red);
  muzzle.position.z = 1.02;
  turret.add(turretBall, barrel, muzzle);
  body.add(turret);

  // Red nav lights on the fuselage
  const navTop = new THREE.Mesh(new THREE.SphereGeometry(0.1, 8, 6), red);
  navTop.position.set(0, 0.62, -0.9);
  const navBelly = navTop.clone();
  navBelly.position.set(0, -0.48, -0.6);
  body.add(navTop, navBelly);

  // Searchlight cone (volumetric look) + real spot light
  const coneGeo = new THREE.ConeGeometry(2.8, 9, 32, 1, true);
  coneGeo.translate(0, -4.5, 0);
  coneGeo.rotateX(-Math.PI / 2); // tip at the turret, opening along +z (the barrel axis)
  const coneMat = new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(1, 0.12, 0.18) }, uOpacity: { value: 0.0 } },
    vertexShader: /* glsl */ `
      varying float vY; varying vec3 vN; varying vec3 vV;
      void main() {
        vY = position.z / 9.0;
        vec4 mv = modelViewMatrix * vec4(position, 1.0);
        vN = normalize(normalMatrix * normal);
        vV = normalize(-mv.xyz);
        gl_Position = projectionMatrix * mv;
      }`,
    fragmentShader: /* glsl */ `
      uniform vec3 uColor; uniform float uOpacity;
      varying float vY; varying vec3 vN; varying vec3 vV;
      void main() {
        float a = pow(1.0 - vY, 2.0) * uOpacity * 0.6;
        gl_FragColor = vec4(uColor * a, a);
      }`,
    transparent: true,
    depthWrite: false,
    side: THREE.DoubleSide,
    blending: THREE.AdditiveBlending,
  });
  const cone = new THREE.Mesh(coneGeo, coneMat);
  turret.add(cone);
  const spot = new THREE.SpotLight(0xff2a3a, 0, 40, 0.42, 0.6, 1.4);
  spot.position.set(0, 0, 0);
  turret.add(spot);
  turret.add(spot.target);

  // Engine glow onto the ground
  const underGlow = new THREE.PointLight(0x5fe3ff, 8, 10, 2);
  underGlow.position.set(0, -1, 0);
  body.add(underGlow);

  // Blob shadow
  const shadowTex = (() => {
    const c = document.createElement('canvas');
    c.width = c.height = 64;
    const g = c.getContext('2d');
    const grd = g.createRadialGradient(32, 32, 0, 32, 32, 32);
    grd.addColorStop(0, 'rgba(0,0,0,0.7)');
    grd.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = grd;
    g.fillRect(0, 0, 64, 64);
    return new THREE.CanvasTexture(c);
  })();
  const shadow = new THREE.Mesh(new THREE.PlaneGeometry(9, 5), new THREE.MeshBasicMaterial({ map: shadowTex, transparent: true, depthWrite: false }));
  shadow.rotation.x = -Math.PI / 2;
  scene.add(shadow);

  body.scale.setScalar(1.45);
  root.position.copy(PAD_POSITION).setY(45);
  scene.add(root);

  const state = {
    mode: 'STANDBY_MODE',
    down: false,
    flip: 0, // 0 upright → 1 inverted
    alt: 45,
    heading: 0,
    aim: new THREE.Vector3(0, 0, 20),
    hasTarget: false,
    recoil: 0,
    visible: true,
    att: { roll: 0, pitch: 0, yaw: 0 }, // smoothed telemetry attitude (rad)
  };
  const goal = new THREE.Vector3();
  const muzzleWorld = new THREE.Vector3();
  const tmp = new THREE.Vector3();

  function blink(t, hz) {
    return Math.sin(t * Math.PI * 2 * hz) > 0;
  }

  return {
    root,
    get position() {
      return root.position;
    },
    muzzle() {
      muzzle.getWorldPosition(muzzleWorld);
      return muzzleWorld;
    },
    setVisible(v) {
      state.visible = v;
      root.visible = v;
      shadow.visible = v;
    },
    recoil() {
      state.recoil = 1;
    },
    /** Point the turret at a world position (or clear). */
    track(target) {
      if (target) {
        state.aim.copy(target);
        state.hasTarget = true;
      } else state.hasTarget = false;
    },
    update(dt, t, mode, { down = false, ended = false, telemetry = null } = {}) {
      state.mode = mode;
      state.down = down;

      // Where to be
      if (down) {
        goal.set(root.position.x, 0.75, root.position.z);
      } else if (mode === 'BATTLE_MODE') {
        const ax = state.hasTarget ? THREE.MathUtils.clamp(state.aim.x * 0.25, -7, 7) : Math.sin(t * 0.35) * 5;
        goal.set(ax, 70 + Math.sin(t * 1.3) * 1.5, 1.5);
      } else {
        goal.set(PAD_POSITION.x + Math.sin(t * 0.25) * 4, 45 + Math.sin(t * 1.1) * 1.2, PAD_POSITION.z + 3 + Math.cos(t * 0.25) * 2.5);
      }
      const k = down ? 1 - Math.exp(-dt * 3.5) : 1 - Math.exp(-dt * 1.6);
      const prevX = root.position.x;
      root.position.lerp(goal, k);
      const vx = (root.position.x - prevX) / Math.max(dt, 1e-3);

      // Heading: face target in battle, travel direction otherwise
      let desired = 0;
      if (state.hasTarget && !down) {
        tmp.subVectors(state.aim, root.position);
        desired = Math.atan2(tmp.x, tmp.z);
      } else if (!down) {
        desired = Math.sin(t * 0.25) * 0.6;
      } else desired = state.heading;
      let dh = desired - state.heading;
      dh = Math.atan2(Math.sin(dh), Math.cos(dh));
      state.heading += dh * Math.min(1, dt * 2.5);
      root.rotation.y = state.heading;

      // Flip when down (THE_LAST_OF_2028), bank with lateral motion otherwise
      state.flip += ((down ? 1 : 0) - state.flip) * Math.min(1, dt * (down ? 4 : 2.2));
      // Attitude from telemetry: roll/pitch from the accelerometer gravity vector,
      // yaw from the integrated gyro Z (leaky, so gyro drift never wanders off).
      const att = state.att;
      let tRoll = 0;
      let tPitch = 0;
      let yawRate = 0;
      if (telemetry && Array.isArray(telemetry.Accel) && Array.isArray(telemetry.Gyro)) {
        const [ax, ay, az] = telemetry.Accel.map((v) => (Number.isFinite(v) ? v : 0));
        if (Math.hypot(ax, ay, az) > 1) {
          tRoll = Math.atan2(ay, az);
          tPitch = Math.atan2(-ax, Math.hypot(ay, az));
        }
        yawRate = THREE.MathUtils.degToRad(Number.isFinite(telemetry.Gyro[2]) ? telemetry.Gyro[2] : 0);
      }
      const lim = (v) => THREE.MathUtils.clamp(v, -1.0, 1.0);
      att.roll += (lim(tRoll) - att.roll) * Math.min(1, dt * 6);
      att.pitch += (lim(tPitch) - att.pitch) * Math.min(1, dt * 6);
      att.yaw += yawRate * dt;
      att.yaw -= att.yaw * Math.min(1, dt * 0.25);
      att.yaw = THREE.MathUtils.clamp(att.yaw, -Math.PI, Math.PI);
      root.rotation.y = state.heading + (down ? 0 : att.yaw);
      body.rotation.z = state.flip * Math.PI + (down ? 0 : THREE.MathUtils.clamp(-vx * 0.06, -0.35, 0.35) + att.roll);
      body.rotation.x = down ? 0.15 : Math.sin(t * 1.7) * 0.03 - (mode === 'BATTLE_MODE' ? 0.06 : 0) - att.pitch;
      body.position.y = down ? 0 : Math.sin(t * 2.1) * 0.08;

      // Turret aims (local space) and recoils
      if (state.hasTarget && !down) {
        turret.lookAt(state.aim); // world-space; barrel is +z
      } else {
        turret.rotation.set(0.75, 0, 0);
      }
      state.recoil = Math.max(0, state.recoil - dt * 5);
      barrel.position.z = 0.5 - state.recoil * 0.25;

      // Lights by mode — LED semantics: Standby steady, Battle 1 Hz, Last 2 Hz
      const battle = mode === 'BATTLE_MODE' && !down;
      const navOn = ended ? false : down ? blink(t, 2) : battle ? blink(t, 1) : true;
      red.color.setRGB(navOn ? 4 : 0.3, navOn ? 0.25 : 0.02, navOn ? 0.35 : 0.04);
      const engine = ended ? 0 : down ? (blink(t, 6) ? 0.4 : 0.05) : battle ? 1.3 : 0.85;
      cyan.color.setRGB(0.5 * engine, 2.6 * engine, 3.6 * engine);
      underGlow.intensity = down ? 0 : 8 * engine;
      const searchTarget = battle ? 1 : 0;
      coneMat.uniforms.uOpacity.value += (searchTarget * 0.22 - coneMat.uniforms.uOpacity.value) * Math.min(1, dt * 4);
      coneMat.uniforms.uColor.value.set(battle ? 0xff1f30 : 0x3fd8ff);
      spot.color.set(battle ? 0xff2a3a : 0x5fe3ff);
      spot.intensity += ((battle ? 120 : down ? 0 : 30) - spot.intensity) * Math.min(1, dt * 4);
      spot.target.position.set(0, 0, 9);

      // Shadow follows, sharper when low
      shadow.position.set(root.position.x, 0.03, root.position.z);
      shadow.rotation.z = -state.heading;
      const h = Math.max(0.7, root.position.y);
      shadow.scale.setScalar(THREE.MathUtils.clamp(1.4 - h * 0.08, 0.6, 1.3));
      shadow.material.opacity = THREE.MathUtils.clamp(1.1 - h * 0.1, 0.25, 0.9);
    },
  };
}
