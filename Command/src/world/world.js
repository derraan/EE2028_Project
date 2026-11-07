/**
 * VESPER world — Three.js scene, camera rig, bloom, and the fiction director.
 *
 * Authority boundary (same as the concept microsite):
 *   - Drone mode, charge, fire, rescue, self-destruct come from parsed UART
 *     events passed into `update()`. The world only *expresses* them.
 *   - Meteors, zombies, kills and waves are game fiction layered on top.
 *     They never feed back into drone state.
 *   - Radar lock (EVT RADAR) is firmware truth: the world places / pins a
 *     zombie at the reported azimuth + range, and draws the laser pointer
 *     only while the firmware says laser=1.
 */
import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { buildEnvironment, LAB_CENTER } from './environment.js';
import { createEffects } from './effects.js';
import { createDrone } from './drone.js';
import { createHorde } from './zombies.js';

const FIELD_RADIUS = [22, 32];
/** World units per real metre for radar ranges (RD-03D 6 m ≈ 24 units of field). */
export const RADAR_WORLD_PER_M = 4;

export function createWorld(host, { audio, adaptive = true } = {}) {
  const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance' });
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  // Integrated GPUs are fill-rate bound: start modest, let adaptive quality move between these bounds.
  const MAX_RATIO = Math.min(window.devicePixelRatio || 1, 1.25);
  const MIN_RATIO = 0.6;
  let pixelRatio = MAX_RATIO;
  renderer.setPixelRatio(pixelRatio);
  host.appendChild(renderer.domElement);
  renderer.domElement.setAttribute('tabindex', '0');
  renderer.domElement.setAttribute('aria-label', 'VESPER 3D world. Drag to orbit, scroll to zoom, arrow keys to turn.');

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(52, 1, 0.1, 2000);

  // HalfFloat keeps HDR for bloom; no MSAA (bloom + fog hide aliasing, and MSAA costs too much on iGPUs).
  const target = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType, samples: 0 });
  const composer = new EffectComposer(renderer, target);
  composer.addPass(new RenderPass(scene, camera));
  const bloom = new UnrealBloomPass(new THREE.Vector2(1, 1), 0.85, 0.55, 0.85);
  const bloomSetSize = bloom.setSize.bind(bloom);
  bloom.setSize = (w, h) => bloomSetSize(Math.max(1, Math.round(w / 2)), Math.max(1, Math.round(h / 2))); // half-res bloom
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  const env = buildEnvironment(scene, renderer);
  const fx = createEffects(scene);
  const drone = createDrone(scene);
  const horde = createHorde(scene);

  // ------------------------------------------------------------ camera rig
  const rig = {
    mode: 'orbit', // orbit | drone
    yaw: 0.22,
    pitch: 0.26,
    dist: 36,
    focus: new THREE.Vector3(0, 4, 0),
    shake: 0,
    idle: 0,
    dragging: false,
    snap: true, // place the camera on its first frame instead of easing in from the origin
  };
  const camGoal = new THREE.Vector3();
  const lookGoal = new THREE.Vector3();
  const lookNow = new THREE.Vector3(0, 4, -6);
  const el = renderer.domElement;
  let last = null;
  el.addEventListener('pointerdown', (e) => {
    rig.dragging = true;
    last = { x: e.clientX, y: e.clientY };
    el.setPointerCapture(e.pointerId);
  });
  el.addEventListener('pointermove', (e) => {
    if (!rig.dragging || !last) return;
    rig.yaw -= (e.clientX - last.x) * 0.005;
    rig.pitch = THREE.MathUtils.clamp(rig.pitch + (e.clientY - last.y) * 0.003, 0.02, 0.85);
    last = { x: e.clientX, y: e.clientY };
    rig.idle = 0;
  });
  const end = () => {
    rig.dragging = false;
    last = null;
  };
  el.addEventListener('pointerup', end);
  el.addEventListener('pointercancel', end);
  el.addEventListener(
    'wheel',
    (e) => {
      e.preventDefault();
      rig.dist = THREE.MathUtils.clamp(rig.dist * (1 + Math.sign(e.deltaY) * 0.08), 14, 80);
      rig.idle = 0;
    },
    { passive: false }
  );
  el.addEventListener('keydown', (e) => {
    const step = { ArrowLeft: [0.08, 0], ArrowRight: [-0.08, 0], ArrowUp: [0, -0.04], ArrowDown: [0, 0.04] }[e.key];
    if (!step) return;
    e.preventDefault();
    rig.yaw += step[0];
    rig.pitch = THREE.MathUtils.clamp(rig.pitch + step[1], 0.02, 0.85);
    rig.idle = 0;
  });

  function resize() {
    const w = host.clientWidth || 1;
    const h = host.clientHeight || 1;
    renderer.setSize(w, h, false);
    composer.setSize(w, h);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(host);
  resize();

  // ------------------------------------------------------------ director
  const game = {
    kills: 0,
    wave: 0,
    sinceWave: 0,
    nextAmbient: 2,
    lastMode: 'STANDBY_MODE',
    ended: false,
    groanTimer: 4,
  };

  function fieldPoint(radius = FIELD_RADIUS) {
    const a = (Math.random() - 0.5) * Math.PI * 0.7;
    const r = radius[0] + Math.random() * (radius[1] - radius[0]);
    return new THREE.Vector3(Math.sin(a) * r, 0, Math.cos(a) * r + 2);
  }

  function launchWave(size, where = fieldPoint()) {
    game.wave += 1;
    game.sinceWave = 0;
    audio?.whoosh();
    fx.meteor(where, {
      duration: 1.5,
      onImpact: (p) => {
        audio?.boom(0.8);
        rig.shake = Math.max(rig.shake, 0.5);
        horde.spawn(p, size);
      },
    });
  }

  // Radar lock binding (see update): the zombie the firmware is aiming at.
  const radarTrack = { zombie: null, point: new THREE.Vector3(), locked: false, laser: false, az: null, rangeM: null, spawnCooldown: 0 };
  const torso = new THREE.Vector3();

  function fireLaser() {
    const from = drone.muzzle().clone();
    const rz = radarTrack.zombie;
    const z = rz && rz.alive && rz.state !== 'die' ? rz : horde.priorityTarget(drone.position);
    drone.recoil();
    audio?.laser();
    rig.shake = Math.max(rig.shake, 0.18);
    if (z) {
      const hit = horde.torsoPosition(z);
      fx.laser(from, hit);
      horde.kill(z, drone.position);
      game.kills += 1;
    } else {
      const ahead = new THREE.Vector3(Math.sin(drone.root.rotation.y), 0, Math.cos(drone.root.rotation.y)).multiplyScalar(18).add(drone.position).setY(0.1);
      fx.laser(from, ahead);
    }
  }

  function reset() {
    horde.clear();
    game.kills = 0;
    game.wave = 0;
    game.sinceWave = 0;
    game.ended = false;
    drone.setVisible(true);
  }

  // ------------------------------------------------------------ stats
  const stats = { kills: 0, wave: 0, infected: 0, nearest: null, contacts: [], drone: { x: 0, z: 0, heading: 0 }, radar: null };

  function computeStats() {
    stats.kills = game.kills;
    stats.wave = game.wave;
    stats.infected = horde.count();
    stats.contacts = horde.contacts();
    stats.drone.x = drone.position.x;
    stats.drone.z = drone.position.z;
    stats.drone.heading = drone.root.rotation.y;
    let best = null;
    for (const c of stats.contacts) {
      const dx = c.x - drone.position.x;
      const dz = c.z - drone.position.z;
      const d = Math.hypot(dx, dz);
      if (!best || d < best.dist) best = { dist: d, bearing: (THREE.MathUtils.radToDeg(Math.atan2(dx, dz)) + 360) % 360 };
    }
    stats.nearest = best;
    stats.radar = radarTrack.locked
      ? { az: radarTrack.az, rangeM: radarTrack.rangeM, laser: radarTrack.laser, x: radarTrack.point.x, z: radarTrack.point.z }
      : null;
    return stats;
  }

  // ------------------------------------------------------------ adaptive quality
  let frameAvg = 16;
  let qualityCooldown = 3;
  function adaptQuality(dt) {
    if (!adaptive) return;
    frameAvg += (dt * 1000 - frameAvg) * 0.05;
    qualityCooldown -= dt;
    if (qualityCooldown > 0) return;
    if (frameAvg > 30 && pixelRatio > MIN_RATIO) {
      pixelRatio = Math.max(MIN_RATIO, pixelRatio - 0.2);
      renderer.setPixelRatio(pixelRatio);
      resize();
      qualityCooldown = 2.5;
    } else if (frameAvg < 14 && pixelRatio < MAX_RATIO) {
      pixelRatio = Math.min(MAX_RATIO, pixelRatio + 0.2);
      renderer.setPixelRatio(pixelRatio);
      resize();
      qualityCooldown = 4;
    }
  }

  // ------------------------------------------------------------ frame
  /**
   * @param {number} dt seconds
   * @param {number} t  seconds since start
   * @param {{mode:string, ended:boolean, selfDestructing:boolean, radar?:object, simRadar?:boolean}} view parser-derived
   *        (radar = parser state.radar; simRadar = contacts came from this world, so don't move zombies)
   * @param {{fired:number, tripped:number, attacked:number, rescued:number, telemetry:boolean, terminated:boolean, radarLocked?:number}} ev new events this frame
   */
  function update(dt, t, view, ev) {
    const mode = view.mode;
    const down = mode === 'THE_LAST_OF_2028' || view.selfDestructing;
    const battle = mode === 'BATTLE_MODE';
    game.sinceWave += dt;

    // --- firmware events → world reactions
    if (ev.tripped) launchWave(4 + Math.min(game.wave, 4));
    for (let i = 0; i < ev.fired; i++) fireLaser();
    if (ev.telemetry && !down && !view.ended) fx.scan(drone.position);
    if (ev.attacked) {
      const p = drone.position.clone().setY(1);
      fx.sparks(p, 0xffb35a);
      fx.flash(p, { color: 0xff5040, size: 5, life: 0.25, light: 30 });
      rig.shake = Math.max(rig.shake, 0.35);
      audio?.blip(140, 0.2, 'sawtooth', 0.2);
      const near = horde.contacts().filter((c) => Math.hypot(c.x - drone.position.x, c.z - drone.position.z) < 9).length;
      if (near < 4) {
        const a = Math.random() * Math.PI * 2;
        horde.spawn(new THREE.Vector3(drone.position.x + Math.cos(a) * 6, 0, drone.position.z + Math.sin(a) * 6), 2, { delay: 0.3 });
      }
    }
    if (ev.rescued) {
      const p = drone.position.clone();
      fx.ring(p, { color: 0x5fe3ff, radius: 16, life: 1 });
      fx.ring(p, { color: 0xffffff, radius: 9, life: 0.6 });
      fx.flash(p.clone().setY(2), { color: 0x5fe3ff, size: 14, life: 0.6, light: 80 });
      game.kills += horde.blast(p, 8, (z) => fx.disintegrate(z.pos.clone().setY(1)));
      rig.shake = Math.max(rig.shake, 0.6);
      audio?.rescue();
    }
    if (ev.terminated && !game.ended) {
      game.ended = true;
      const p = drone.position.clone().setY(1.2);
      fx.explosion(p, 1.5);
      game.kills += horde.blast(p, 18);
      drone.setVisible(false);
      rig.shake = 1.4;
      audio?.boom(1.6);
    }
    if (game.ended && !view.ended) reset(); // a new mission after termination

    // --- fiction director
    if (!view.ended) {
      if (battle && horde.count() < 2 && game.sinceWave > 9) launchWave(3 + Math.min(game.wave, 5));
      if (mode === 'STANDBY_MODE' && horde.count() === 0 && game.sinceWave > 26) launchWave(2, fieldPoint([30, 38]));
    }
    game.nextAmbient -= dt;
    if (game.nextAmbient <= 0) {
      fx.meteor(new THREE.Vector3(), { ambient: true, duration: 2.2 + Math.random() });
      game.nextAmbient = 3 + Math.random() * 5;
    }
    game.groanTimer -= dt;
    if (game.groanTimer <= 0) {
      if (horde.count() > 0) audio?.groan();
      game.groanTimer = 2.5 + Math.random() * 4;
    }

    // --- radar lock (firmware truth) → which zombie the pan laser is on
    const rv = view.radar;
    const locked = Boolean(rv && rv.lock && rv.az != null && rv.rangeMm != null && !down && !view.ended);
    radarTrack.locked = locked;
    radarTrack.laser = locked && Boolean(rv.laser);
    radarTrack.spawnCooldown = Math.max(0, radarTrack.spawnCooldown - dt);
    let radarZombie = null;
    if (locked) {
      const a = THREE.MathUtils.degToRad(rv.az); // radar boresight = world +z, +az → +x
      const r = (rv.rangeMm / 1000) * RADAR_WORLD_PER_M;
      radarTrack.az = rv.az;
      radarTrack.rangeM = rv.rangeMm / 1000;
      radarTrack.point.set(drone.position.x + Math.sin(a) * r, 0, drone.position.z + Math.cos(a) * r);
      radarZombie = horde.nearestTo(radarTrack.point, view.simRadar ? 3 : 7);
      if (!radarZombie && !view.simRadar && radarTrack.spawnCooldown <= 0) {
        // Live board saw someone we have no zombie for — one claws up there.
        horde.spawn(radarTrack.point, 1, { delay: 0 });
        radarTrack.spawnCooldown = 2;
      }
      if (ev.radarLocked) {
        audio?.blip(880, 0.08, 'square', 0.12);
        fx.ring(radarTrack.point, { color: 0xff4050, radius: 3, life: 0.5 });
      }
    }
    radarTrack.zombie = radarZombie;

    // --- actors
    horde.setTarget(down ? 'drone' : 'lab', drone.position);
    const priority = battle && !down ? horde.priorityTarget(drone.position) : null;
    if (radarZombie) drone.track(horde.torsoPosition(radarZombie, torso));
    else drone.track(priority ? horde.headPosition(priority).setY(1.2) : null);
    drone.update(dt, t, mode, { down, ended: view.ended });
    horde.update(dt, t, {
      onDeath: (p) => fx.disintegrate(p),
      onClaw: (z) => {
        if (down) fx.sparks(drone.position.clone().setY(0.9), 0xffd27a);
      },
    });
    if (radarZombie && !view.simRadar) horde.follow(radarZombie, radarTrack.point, dt);
    fx.pointer(
      drone.muzzle().clone(),
      locked ? (radarZombie ? horde.torsoPosition(radarZombie, torso) : torso.copy(radarTrack.point).setY(1.35)) : null,
      { laser: radarTrack.laser && drone.root.visible, dt }
    );
    env.setAlarm(down ? 1 : view.ended ? 0.4 : 0);
    env.update(dt, t);
    fx.update(dt);
    audio?.update(dt, { alarm: down && !view.ended, battle });

    // --- camera
    rig.idle += dt;
    rig.shake = Math.max(0, rig.shake - dt * 1.8);
    if (rig.mode === 'drone' && drone.root.visible) {
      const h = drone.root.rotation.y;
      const back = new THREE.Vector3(-Math.sin(h), 0, -Math.cos(h));
      camGoal.copy(drone.position).addScaledVector(back, 7.5).add(new THREE.Vector3(0, down ? 3.5 : 2.4, 0));
      lookGoal.copy(drone.position).addScaledVector(back, -14).setY(down ? 0.5 : 1.2);
    } else {
      if (!rig.dragging && rig.idle > 6) rig.yaw += dt * 0.025; // slow cinematic drift
      const focus = rig.focus.clone().lerp(drone.position, 0.15);
      camGoal.set(
        focus.x + Math.sin(rig.yaw) * Math.cos(rig.pitch) * rig.dist,
        Math.max(1.5, focus.y + Math.sin(rig.pitch) * rig.dist),
        focus.z + Math.cos(rig.yaw) * Math.cos(rig.pitch) * rig.dist
      );
      lookGoal.copy(focus);
    }
    if (rig.snap) {
      camera.position.copy(camGoal);
      lookNow.copy(lookGoal);
      rig.snap = false;
    }
    camera.position.lerp(camGoal, 1 - Math.exp(-dt * 3));
    lookNow.lerp(lookGoal, 1 - Math.exp(-dt * 4));
    camera.lookAt(lookNow);
    if (rig.shake > 0) {
      const s = rig.shake * rig.shake * 0.5;
      camera.position.x += (Math.random() - 0.5) * s;
      camera.position.y += (Math.random() - 0.5) * s;
    }
    bloom.strength = 0.85 + (down ? 0.25 : 0);

    composer.render(dt);
    adaptQuality(dt);
    game.lastMode = mode;
    return computeStats();
  }

  return {
    update,
    reset,
    toggleCamera() {
      rig.mode = rig.mode === 'orbit' ? 'drone' : 'orbit';
      return rig.mode;
    },
    get cameraMode() {
      return rig.mode;
    },
    /** Optional manual spawn (virtual-board "meteor" control already trips T/A/G). */
    launchWave,
    labCenter: LAB_CENTER,
    /** Drone position + live contacts in radar mm (x right, y = world +z), for the virtual RD-03D. */
    radarContacts() {
      const out = [];
      for (const c of horde.contacts()) {
        if (c.rising) continue;
        out.push({ x: ((c.x - drone.position.x) / RADAR_WORLD_PER_M) * 1000, y: ((c.z - drone.position.z) / RADAR_WORLD_PER_M) * 1000 });
      }
      return out;
    },
    /** Debug access for ?debug sessions. */
    debug: { camera, drone, horde, rig, renderer, scene, composer, bloom },
  };
}
