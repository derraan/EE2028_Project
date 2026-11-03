/**
 * VESPER world renderer — software-projected canvas scene (from the
 * vesper-3d-concept prototype). Coordinates are fictional scene units.
 *
 * Rendering is a pure function of a view object built in main.js from the
 * shared parser state, so live UART, fixture replay and the virtual board all
 * look identical. The scene owns only camera state.
 *
 * view = {
 *   mode,        // STANDBY_MODE | BATTLE_MODE | THE_LAST_OF_2028 | TERMINATED
 *   down,        // drone overturned / grounded
 *   ended,       // mission terminated
 *   secured,     // a contact was neutralised (lab lights up)
 *   contact,     // hostile contact on the perimeter
 *   shot,        // seconds of laser beam remaining
 *   scan,        // seconds of telemetry sweep ring remaining
 * }
 */
export function createScene(canvas) {
  const context = canvas.getContext('2d');
  let width = 1;
  let height = 1;
  let orbit = 0.6;
  let forward = false;
  let drag = null;
  const faces = [];

  canvas.addEventListener('pointerdown', (event) => {
    drag = event.clientX;
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', (event) => {
    if (drag === null) return;
    orbit += (event.clientX - drag) * 0.008;
    drag = event.clientX;
  });
  const release = () => {
    drag = null;
  };
  canvas.addEventListener('pointerup', release);
  canvas.addEventListener('pointercancel', release);
  canvas.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowLeft') orbit -= 0.08;
    else if (event.key === 'ArrowRight') orbit += 0.08;
    else return;
    event.preventDefault();
  });

  function point(x, y, z) {
    const camera = forward ? [0, 5, 3] : [Math.sin(orbit) * 27, 15, Math.cos(orbit) * 27];
    const target = forward ? [0, 5, -20] : [0, 3, 0];
    const dz = target[2] - camera[2];
    const dx = target[0] - camera[0];
    const yaw = Math.atan2(dx, dz);
    const pitch = Math.atan2(target[1] - camera[1], Math.hypot(dx, dz));
    const rx = x - camera[0];
    const ry = y - camera[1];
    const rz = z - camera[2];
    const lateral = rx * Math.cos(yaw) - rz * Math.sin(yaw);
    const depth = rx * Math.sin(yaw) + rz * Math.cos(yaw);
    const vertical = ry * Math.cos(pitch) - depth * Math.sin(pitch);
    const distance = ry * Math.sin(pitch) + depth * Math.cos(pitch);
    if (distance < 0.5) return null;
    const scale = (Math.min(width, height) * 1.1) / distance;
    return { x: width / 2 + lateral * scale, y: height * 0.57 - vertical * scale, distance };
  }

  function polygon(vertices, color) {
    const projected = vertices.map((vertex) => point(...vertex));
    if (projected.some((vertex) => !vertex)) return;
    faces.push({
      vertices: projected,
      color,
      depth: projected.reduce((sum, vertex) => sum + vertex.distance, 0) / projected.length,
    });
  }

  function box(x, y, z, w, h, d, color) {
    const v = [
      [x - w, y, z - d], [x + w, y, z - d], [x + w, y, z + d], [x - w, y, z + d],
      [x - w, y + h, z - d], [x + w, y + h, z - d], [x + w, y + h, z + d], [x - w, y + h, z + d],
    ];
    for (const indices of [[0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7], [4, 5, 6, 7]]) {
      polygon(indices.map((index) => v[index]), color);
    }
  }

  function resize() {
    const rect = canvas.getBoundingClientRect();
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.round(rect.width * ratio);
    const h = Math.round(rect.height * ratio);
    if (canvas.width !== w || canvas.height !== h) {
      width = rect.width;
      height = rect.height;
      canvas.width = w;
      canvas.height = h;
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
    }
  }

  function draw(view, clock) {
    resize();
    const emergency = view.mode === 'THE_LAST_OF_2028';

    const sky = context.createLinearGradient(0, 0, 0, height);
    sky.addColorStop(0, '#080b20');
    sky.addColorStop(0.55, emergency ? '#5b3448' : '#534b68');
    sky.addColorStop(1, emergency ? '#c0786b' : '#b68b7b');
    context.fillStyle = sky;
    context.fillRect(0, 0, width, height);
    const halo = context.createRadialGradient(width * 0.76, height * 0.24, 8, width * 0.76, height * 0.24, height * 0.3);
    halo.addColorStop(0, '#ffe9ca66');
    halo.addColorStop(1, '#efd3a000');
    context.fillStyle = halo;
    context.fillRect(0, 0, width, height);
    context.fillStyle = '#e8d3b8';
    context.beginPath();
    context.arc(width * 0.76, height * 0.24, height * 0.1, 0, Math.PI * 2);
    context.fill();
    context.fillStyle = '#292638';
    context.beginPath();
    context.arc(width * 0.78, height * 0.22, height * 0.095, 0, Math.PI * 2);
    context.fill();
    for (let i = 0; i < 70; i++) {
      context.fillStyle = 'rgba(221,233,220,' + (0.1 + 0.15 * Math.sin(clock + i)) + ')';
      context.fillRect((i * 137.7) % width, (i * 43.3 + clock * (2 + (i % 3))) % height, 1.5, 1.5);
    }

    faces.length = 0;
    for (let x = -60; x < 60; x += 8) {
      for (let z = -60; z < 60; z += 8) {
        polygon([[x, -1, z], [x + 8, -1, z], [x + 8, -1, z + 8], [x, -1, z + 8]], (x + z) % 16 === 0 ? '#243232' : '#293635');
      }
    }
    for (let i = 0; i < 24; i++) {
      const angle = i * 2.4;
      const radius = 32 + (i % 5) * 4;
      const x = Math.sin(angle) * radius;
      const z = Math.cos(angle) * radius;
      const buildingHeight = 8 + (i % 7) * 3;
      box(x, -1, z, 2.6, buildingHeight, 2.8, '#344045');
      for (let level = 2; level < buildingHeight; level += 3) {
        box(x, -1 + level, z - 2.82, 1.9, 0.12, 0.03, i % 3 ? '#6f7772' : '#c9a786');
      }
      if (i % 4 === 0) box(x, buildingHeight + 4 + Math.sin(clock * 0.4 + i), z, 2.6, 1, 2.8, '#556063');
    }
    // NUS E4-03-07 laboratory
    box(0, -1, -15, 5, 1, 5, '#475452');
    box(0, 0, -17, 5, 6, 2, view.secured ? '#a9c79b' : '#65706a');
    box(0, 2, -14.8, 4, 0.25, 0.1, '#c2ec9e');
    box(0, 6, -17, 0.3, 4, 0.3, '#76877d');
    box(-7, -1, -19, 3, 4, 3, '#3c4c49');

    // Drone
    const down = view.down;
    const y = down ? 0 : 4 + Math.sin(clock * 2) * 0.15;
    if (!forward) {
      box(0, y, 0, 1.7, down ? 2.6 : 0.8, 1.2, '#97aaa1');
      const blink = emergency ? Math.floor(clock * 4) % 2 === 0 : view.mode === 'BATTLE_MODE' ? Math.floor(clock * 2) % 2 === 0 : true;
      for (const x of [-3, 3]) {
        for (const z of [-2, 2]) {
          box(x / 2, y + 0.1, z / 2, 1.1, 0.2, 0.3, '#596e68');
          box(x, y, z, 0.8, 0.25, 0.8, '#354a47');
          box(x, y + 0.28, z, 0.6, 0.05, 0.6, view.ended ? '#263332' : blink ? '#bbeb9d' : '#4d6b58');
        }
      }
      box(0, y + 0.15, -1.6, 0.3, 0.3, 0.9, view.mode === 'BATTLE_MODE' ? '#e5c593' : '#54675f');
    }

    // Hostile contact
    if (view.contact) {
      const sway = Math.sin(clock * 3) * 0.15;
      box(5 + sway, -0.8, -9, 0.55, 1.7, 0.35, '#6a5856');
      box(5 + sway, 0.9, -9, 0.75, 1.3, 0.4, '#876657');
      box(5 + sway, 2.2, -9, 0.42, 0.72, 0.38, '#aa846c');
    }

    faces.sort((a, b) => b.depth - a.depth);
    for (const face of faces) {
      context.beginPath();
      face.vertices.forEach((vertex, index) => (index ? context.lineTo(vertex.x, vertex.y) : context.moveTo(vertex.x, vertex.y)));
      context.closePath();
      context.fillStyle = face.color;
      context.fill();
      const shade = context.createLinearGradient(0, face.vertices[0].y, width, height);
      shade.addColorStop(0, '#f1d5b925');
      shade.addColorStop(1, '#02081265');
      context.fillStyle = shade;
      context.fill();
      context.fillStyle = 'rgba(110,102,125,' + Math.min(0.65, face.depth / 140) + ')';
      context.fill();
      context.strokeStyle = '#c4d3bf12';
      context.stroke();
    }

    if (view.scan > 0) {
      const p = point(0, 0, 0);
      if (p) {
        context.strokeStyle = '#c2ec9e88';
        context.beginPath();
        context.ellipse(p.x, p.y, (1 - view.scan) * 360, (1 - view.scan) * 90, 0, 0, Math.PI * 2);
        context.stroke();
      }
    }
    if (view.shot > 0) {
      const start = point(0, y, -2);
      const end = point(5, 4, -9);
      if (start && end) {
        context.strokeStyle = '#dcffb7';
        context.lineWidth = 4;
        context.beginPath();
        context.moveTo(start.x, start.y);
        context.lineTo(end.x, end.y);
        context.stroke();
        context.lineWidth = 1;
      }
    }
    const vignette = context.createRadialGradient(width / 2, height / 2, height * 0.2, width / 2, height / 2, width * 0.65);
    vignette.addColorStop(0, '#03081000');
    vignette.addColorStop(1, emergency ? '#3a0810dd' : '#030810cc');
    context.fillStyle = vignette;
    context.fillRect(0, 0, width, height);
    if (forward) {
      context.strokeStyle = '#c2ec9e';
      context.strokeRect(width / 2 - 12, height / 2 - 12, 24, 24);
    }
  }

  return {
    draw,
    toggleCamera() {
      forward = !forward;
      return forward;
    },
    get forward() {
      return forward;
    },
  };
}
