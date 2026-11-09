/**
 * Nuclear blast — plays when the countdown runs out and the drone
 * self-destructs (EVT SELF_DESTRUCT → mission terminated). White-out flash,
 * fireball, ground shockwave and a rising mushroom cloud centred on the lab.
 */
import * as THREE from 'three';

const easeOut = (k) => 1 - (1 - Math.min(1, Math.max(0, k))) ** 3;

export function createNuke(scene, host, center) {
  const root = new THREE.Group();
  root.position.copy(center).setY(0);
  root.visible = false;
  scene.add(root);

  const mat = (r, g, b) => new THREE.MeshBasicMaterial({ color: new THREE.Color(r, g, b), transparent: true, depthWrite: false, fog: false });
  const fireMat = mat(6, 2.4, 0.5);
  const capMat = mat(3.2, 1.0, 0.25);
  const stemMat = mat(1.6, 0.5, 0.15);
  const waveMat = mat(4, 3, 1.6);
  waveMat.side = THREE.DoubleSide;

  const fire = new THREE.Mesh(new THREE.SphereGeometry(1, 32, 20), fireMat);
  const cap = new THREE.Mesh(new THREE.SphereGeometry(1, 28, 16), capMat);
  const skirt = new THREE.Mesh(new THREE.TorusGeometry(1, 0.35, 12, 36), capMat);
  skirt.rotation.x = Math.PI / 2;
  const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.55, 1, 1, 20, 1, true), stemMat);
  stem.geometry.translate(0, 0.5, 0);
  const wave = new THREE.Mesh(new THREE.RingGeometry(0.93, 1, 96), waveMat);
  wave.rotation.x = -Math.PI / 2;
  wave.position.y = 0.4;
  const dome = new THREE.Mesh(new THREE.SphereGeometry(1, 32, 16, 0, Math.PI * 2, 0, Math.PI / 2), mat(2.5, 1.5, 0.7));
  root.add(fire, cap, skirt, stem, wave, dome);

  const light = new THREE.PointLight(0xffa860, 0, 900, 1.2);
  light.position.y = 30;
  root.add(light);

  const flashEl = document.createElement('div');
  flashEl.style.cssText = 'position:absolute;inset:0;background:#fff;opacity:0;pointer-events:none;z-index:5';
  host.appendChild(flashEl);

  let t = -1;

  return {
    get active() {
      return t >= 0;
    },
    start() {
      t = 0;
      root.visible = true;
    },
    clear() {
      t = -1;
      root.visible = false;
      flashEl.style.opacity = '0';
      light.intensity = 0;
    },
    /** @returns camera-shake strength 0..1 */
    update(dt) {
      if (t < 0) return 0;
      t += dt;
      const fade = (a, b) => 1 - Math.min(1, Math.max(0, (t - a) / (b - a)));

      const fr = 120 * easeOut(t / 3.2);
      fire.scale.setScalar(Math.max(0.01, fr));
      fire.position.y = fr * 0.35 + easeOut(t / 14) * 40;
      fireMat.opacity = fade(3, 12) * 0.95;

      const dr = 260 * easeOut(t / 5);
      dome.scale.set(Math.max(0.01, dr), Math.max(0.01, dr * 0.55), Math.max(0.01, dr));
      dome.material.opacity = fade(0.5, 4.5) * 0.5;

      const wr = 900 * easeOut(t / 9);
      wave.scale.setScalar(Math.max(0.01, wr));
      waveMat.opacity = fade(5, 10) * 0.9;

      const rise = easeOut((t - 0.8) / 9);
      const stemH = 220 * rise;
      stem.scale.set(18 + rise * 10, Math.max(0.01, stemH), 18 + rise * 10);
      stemMat.opacity = Math.min(1, t / 1.5) * (0.55 + 0.45 * fade(18, 40));
      const capR = 80 * easeOut((t - 2) / 8);
      cap.scale.set(Math.max(0.01, capR), Math.max(0.01, capR * 0.6), Math.max(0.01, capR));
      cap.position.y = stemH + capR * 0.25;
      skirt.scale.setScalar(Math.max(0.01, capR * 0.95));
      skirt.position.y = stemH - capR * 0.05;
      const cool = fade(4, 30);
      capMat.color.setRGB(0.6 + 2.6 * cool, 0.35 + 0.65 * cool, 0.3 + 0 * cool);
      capMat.opacity = Math.min(1, t / 2) * (0.6 + 0.4 * fade(20, 50));

      light.intensity = 4000 * fade(0, 9);
      flashEl.style.opacity = String(Math.min(1, fade(0, 3.2) * 1.4 * Math.min(1, t / 0.12)));
      return fade(0, 5);
    },
  };
}
