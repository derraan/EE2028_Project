/* Dependency-free perspective renderer. All coordinates use simulated metres. */
(() => {
  'use strict';
  const canvas = document.querySelector('#flightCanvas');
  const context = canvas.getContext('2d');
  const ui = id => document.getElementById(id);
  const keys = new Set();
  let state, lastTime = performance.now(), scanUntil = 0, shotUntil = 0;
  let width = 1, height = 1;
  const beacon = { x: 0, y: 35, z: 850 };

  function report(message) { ui('flightEvent').textContent = message; }
  function reset() {
    state = { x: 0, y: 42, z: 0, yaw: 0, speed: 0, charge: 5,
      mode: 'STANDBY', rescue: 0, scanned: false, complete: false };
    scanUntil = shotUntil = 0;
    keys.clear();
    ui('objective').textContent = 'Find the Nacre beacon';
    ui('flightHint').textContent = 'Launch, approach the beacon, then scan within 180 m.';
    report('Ready on the landing pad. Flight and sensor values in this view are simulated.');
  }
  function distance() {
    return Math.hypot(state.x - beacon.x, state.y - beacon.y, state.z - beacon.z);
  }
  function action(name) {
    if (name === 'reset') { reset(); return; }
    if (state.mode === 'TERMINATED') return;
    if (name === 'rescue' && state.mode === 'THE_LAST_OF_EE2028') {
      state.mode = 'BATTLE'; state.rescue = 0;
      report('Rescue confirmed. Battle mode restored.'); return;
    }
    if (state.mode === 'THE_LAST_OF_EE2028') return;
    if (name === 'launch') {
      state.speed = state.speed ? 0 : 55;
      report(state.speed ? 'Flight engaged. Follow the green beacon.' : 'Position hold engaged.');
    }
    if (name === 'scan') {
      scanUntil = performance.now() + 1600;
      if (distance() < 180) {
        state.scanned = true; state.mode = 'BATTLE';
        ui('objective').textContent = 'Beacon acquired. Test the laser';
        ui('flightHint').textContent = 'Fire with at least 5/10 charge to complete the encounter.';
        report('Scan confirmed. Nacre beacon acquired. Battle mode active.');
      } else report('Scan complete. Beacon beyond 180 m acquisition range.');
    }
    if (name === 'charge') {
      state.charge = Math.min(10, state.charge + 3);
      report('Laser recharged to ' + state.charge + '/10.');
    }
    if (name === 'fire') {
      if (state.mode !== 'BATTLE') { report('Enter Battle mode by scanning the beacon.'); return; }
      if (state.charge < 5) { report('Insufficient charge. Press C to recharge.'); return; }
      state.charge -= 5; shotUntil = performance.now() + 180;
      if (state.scanned && distance() < 180) {
        state.complete = true; state.speed = 0;
        ui('objective').textContent = 'Nacre contact established';
        ui('flightHint').textContent = 'Mission complete. Explore, test distress, or restart.';
      }
      report('Laser fired. Remaining charge ' + state.charge + '/10.');
    }
    if (name === 'distress') {
      state.mode = 'THE_LAST_OF_EE2028'; state.rescue = 10; state.speed = 0;
      report('Drone attacked. Press R or Rescue within 10 seconds.');
    }
  }
  for (const name of ['launch', 'scan', 'charge', 'fire', 'distress', 'rescue', 'reset']) {
    ui(name + 'Flight').onclick = () => action(name);
  }
  function active() { return ui('flight').classList.contains('active'); }
  document.addEventListener('keydown', event => {
    if (!active() || /INPUT|TEXTAREA|BUTTON/.test(event.target.tagName)) return;
    const key = event.key.toLowerCase();
    if (['arrowup','arrowdown','arrowleft','arrowright',' '].includes(key)) event.preventDefault();
    keys.add(key);
    if (!event.repeat) {
      const actions = { ' ': 'scan', f: 'fire', c: 'charge', r: 'rescue' };
      if (actions[key]) action(actions[key]);
    }
  });
  document.addEventListener('keyup', event => keys.delete(event.key.toLowerCase()));
  window.addEventListener('blur', () => keys.clear());
  let pointer = null;
  canvas.onpointerdown = event => {
    canvas.focus(); canvas.setPointerCapture(event.pointerId);
    pointer = { x: event.clientX, y: event.clientY };
  };
  canvas.onpointermove = event => {
    if (!pointer || state.mode === 'TERMINATED' || state.rescue) return;
    state.yaw += (event.clientX - pointer.x) * 0.003;
    state.y = Math.max(12, Math.min(170, state.y - (event.clientY - pointer.y) * 0.2));
    pointer = { x: event.clientX, y: event.clientY };
  };
  canvas.onpointerup = canvas.onpointercancel = () => { pointer = null; };
  function project(point) {
    const dx = point.x - state.x, dz = point.z - state.z;
    const depth = dz * Math.cos(state.yaw) + dx * Math.sin(state.yaw);
    if (depth < 2) return null;
    const lateral = dx * Math.cos(state.yaw) - dz * Math.sin(state.yaw);
    const scale = Math.min(width, height) * 0.95 / depth;
    return { x: width / 2 + lateral * scale, y: height * 0.48 - (point.y - state.y) * scale, depth };
  }
  function terrain(x, z) {
    const corridor = 85 + 18 * Math.sin(z * 0.009);
    return Math.max(0, Math.abs(x) - corridor) * 0.8
      + (Math.sin(x * 0.047 + z * 0.025) + Math.cos(z * 0.044)) * Math.min(12, Math.abs(x) / 10);
  }
  function draw(now) {
    const sky = context.createLinearGradient(0, 0, 0, height);
    sky.addColorStop(0, '#10222e'); sky.addColorStop(0.55, '#658784'); sky.addColorStop(1, '#132225');
    context.fillStyle = sky; context.fillRect(0, 0, width, height);
    context.fillStyle = '#c7d8bb'; context.beginPath();
    context.arc(width * 0.78, height * 0.23, height * 0.055, 0, Math.PI * 2); context.fill();
    const faces = [];
    const base = Math.floor(state.z / 45) * 45;
    for (let z = base - 180; z < base + 1600; z += 45) {
      for (let x = -500; x < 500; x += 50) {
        const points = [[x,z],[x+50,z],[x+50,z+45],[x,z+45]].map(([px,pz]) =>
          project({x:px,y:terrain(px,pz),z:pz}));
        if (points.some(point => !point)) continue;
        faces.push({points, depth:points.reduce((sum,p) => sum+p.depth,0)/4,
          shade: Math.round(23 + 7 * Math.sin(x * 0.1 + z * 0.03))});
      }
    }
    faces.sort((a,b) => b.depth-a.depth);
    for (const face of faces) {
      context.beginPath();
      face.points.forEach((point,index) => index ? context.lineTo(point.x,point.y) : context.moveTo(point.x,point.y));
      context.closePath();
      context.fillStyle = 'hsl(165 16% ' + face.shade + '%)';
      context.fill();
      context.strokeStyle = scanUntil > now ? '#a8e69255' : '#88b8a512';
      context.stroke();
    }
    const target = project(beacon);
    if (target) {
      const radius = Math.max(6, Math.min(90, 1300 / target.depth));
      context.strokeStyle = '#ccff9e'; context.lineWidth = 2;
      context.strokeRect(target.x-radius,target.y-radius,radius*2,radius*2);
      context.fillStyle = '#dcffc7'; context.font = '12px monospace';
      context.fillText('NACRE / ' + Math.round(distance()) + ' m',target.x+radius+8,target.y);
    }
    if (scanUntil > now) {
      context.strokeStyle = '#c7ff9d88'; context.lineWidth = 2;
      context.beginPath(); context.arc(width/2,height/2,(1600-scanUntil+now)*width/1800,0,Math.PI*2); context.stroke();
    }
    if (shotUntil > now) {
      context.strokeStyle = '#e2ffc3'; context.lineWidth = 4;
      for (const x of [width*0.18,width*0.82]) {
        context.beginPath();context.moveTo(x,height);context.lineTo(width/2,height/2);context.stroke();
      }
    }
    context.fillStyle = '#091318';
    context.beginPath();context.moveTo(0,height);context.lineTo(0,height*0.83);
    context.lineTo(width*0.23,height*0.94);context.lineTo(width*0.77,height*0.94);
    context.lineTo(width,height*0.83);context.lineTo(width,height);context.fill();
    if (state.rescue || state.mode === 'TERMINATED') {
      context.fillStyle = '#9f241f55';context.fillRect(0,0,width,height);
      context.fillStyle = '#ffe2ca';context.textAlign = 'center';context.font = 'bold 28px monospace';
      context.fillText(state.rescue ? 'RESCUE ' + state.rescue.toFixed(1) + 's' : 'SIGNAL TERMINATED',width/2,height*0.65);
      context.textAlign = 'left';
    }
  }
  function frame(now) {
    const dt = Math.min((now-lastTime)/1000,0.1); lastTime = now;
    if (active()) {
      const rect = canvas.getBoundingClientRect();
      const ratio = Math.min(devicePixelRatio || 1,2);
      if (canvas.width !== Math.round(rect.width*ratio) || canvas.height !== Math.round(rect.height*ratio)) {
        width=rect.width;height=rect.height;canvas.width=Math.round(width*ratio);canvas.height=Math.round(height*ratio);
        context.setTransform(ratio,0,0,ratio,0,0);
      }
      if (state.rescue) {
        state.rescue = Math.max(0,state.rescue-dt);
        if (!state.rescue) { state.mode='TERMINATED';report('Rescue window expired. Cheerio! Restart to fly again.'); }
      } else if (state.mode !== 'TERMINATED') {
        state.yaw += ((keys.has('arrowright')?1:0)-(keys.has('arrowleft')?1:0))*dt;
        state.speed = Math.max(0,Math.min(100,state.speed+((keys.has('w')?1:0)-(keys.has('s')?1:0))*dt*30));
        state.y = Math.max(12,Math.min(170,state.y+((keys.has('arrowup')?1:0)-(keys.has('arrowdown')?1:0))*dt*25));
        state.x += Math.sin(state.yaw)*state.speed*dt;state.z += Math.cos(state.yaw)*state.speed*dt;
        if (state.y < terrain(state.x,state.z)+8 || Math.abs(state.x)>450) {
          state.speed=0;state.x=Math.max(-440,Math.min(440,state.x));
          state.y=Math.max(state.y,terrain(state.x,state.z)+10);
          report('Terrain proximity. Position hold engaged; steer toward the canyon.');
        }
      }
      draw(now);
      ui('flightReadout').textContent = Math.round(state.speed)+' m/s · ALT '+Math.round(state.y)+' m · CHARGE '+state.charge+'/10';
      ui('missionMode').textContent = state.mode;
    } else keys.clear();
    requestAnimationFrame(frame);
  }
  reset(); requestAnimationFrame(frame);
})();
