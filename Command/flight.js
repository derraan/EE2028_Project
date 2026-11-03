/* Perspective scene and event visualization. Coordinates are fictional scene units. */
(() => {
  'use strict';
  const ui = id => document.getElementById(id);
  const canvas = ui('world');
  const context = canvas.getContext('2d');
  const sensorNames = ['Temperature', 'Pressure', 'Humidity', 'Acceleration', 'Angular rate', 'Magnetic field'];
  let state, source = 'simulation', orbit = 0.6, forward = false, drag = null;
  let previous = performance.now(), clock = 0, width = 1, height = 1;
  let eventCount = 0, lastSample = 0;
  const samples = new Map();
  const faces = [];
  function log(message) {
    const item = document.createElement('li');
    const time = document.createElement('time');
    time.textContent = new Date().toLocaleTimeString();
    item.append(time, document.createTextNode(message));
    ui('log').prepend(item);
    while (ui('log').children.length > 80) ui('log').lastChild.remove();
    ui('eventCount').textContent = ++eventCount + ' EVENTS';
  }
  function mode(value) {
    state.mode = value;
    if (value !== 'THE_LAST_OF_EE2028') state.remaining = null;
  }
  function reset() {
    state = { mode: source === 'simulation' ? 'STANDBY' : 'UNKNOWN', charge: source === 'simulation' ? 5 : null,
      surveying: false, contact: false, shot: 0, scan: 0, remaining: null, recovered: false, ended: false };
    lastSample = 0; samples.clear(); eventCount = 0; ui('log').replaceChildren();
    ui('ending').hidden = true; ui('warning').textContent = 'No active warning reported.';
    ui('objective').textContent = source === 'simulation' ? 'Recover the last transmission' : 'Awaiting firmware output';
    ui('hint').textContent = source === 'simulation' ? 'Begin the survey to wake the relay.' : 'Paste UART messages below to replay the encounter.';
    ui('connection').textContent = source === 'simulation' ? 'SIMULATION' : 'AWAITING SIGNAL';
    log(source === 'simulation' ? 'Simulation ready. Relay outpost silent.' : 'Replay ready. No live device connection.');
    renderSensors(); updateUI();
  }
  function finish(success) {
    state.ended = true;
    ui('ending').hidden = false;
    ui('endingLabel').textContent = success ? 'TRANSMISSION FRAGMENT 01' : 'MISSION RECORD CLOSED';
    ui('endingTitle').textContent = success ? 'We left the light on.' : 'The last signal.';
    ui('endingText').textContent = success
      ? '“If anyone receives this, the relay still works. Follow the lights beyond the basin. We are waiting.”'
      : 'The drone’s final transmission has been recorded. The outpost falls silent.';
    log(success ? 'Mission complete. Transmission fragment recovered.' : 'Terminal transmission recorded. Cheerio!');
  }
  function distress() {
    mode('THE_LAST_OF_EE2028');
    state.remaining = source === 'simulation' ? 10 : null;
    log('Drone overturned. Rescue window active.');
  }
  function simulate(action) {
    if (source !== 'simulation' || state.ended) return;
    if (state.mode === 'THE_LAST_OF_EE2028') {
      if (action === 'rescue') {
        mode('BATTLE'); log('Rescue confirmed. Drone upright.');
        if (state.recovered) finish(true);
        updateUI();
      }
      return;
    }
    if (action === 'survey') {
      state.surveying = true; state.scan = 2;
      ['24.8 °C','1008.6 hPa','52.0 %','[0.0, 0.0, 9.8] m/s²','[0.0, 0.0, 0.0] °/s','[210, 12, -45] mG'].forEach((value,index) => samples.set(sensorNames[index],value));
      lastSample = performance.now();
      ui('objective').textContent = 'Investigate the approaching contact';
      ui('hint').textContent = 'Introduce a contact to enter Battle mode.';
      log('Simulated survey sample received. Relay located.'); renderSensors();
    }
    if (action === 'threat' && state.surveying) {
      state.contact = true; mode('BATTLE');
      ui('hint').textContent = 'Fire with at least 5/10 charge to secure the relay.';
      log('Simulated proximity contact. Entering Battle mode.');
    }
    if (action === 'recharge' && state.mode === 'BATTLE') {
      state.charge = Math.min(10,state.charge + 3); log('Charge updated to ' + state.charge + '/10.');
    }
    if (action === 'fire' && state.mode === 'BATTLE') {
      if (state.charge < 5) { log('Fire rejected. Insufficient charge.'); return; }
      state.charge -= 5; state.shot = 0.3; log('Laser fired. Remaining charge ' + state.charge + '/10.');
      if (state.contact) {
        state.contact = false; state.recovered = true;
        ui('objective').textContent = 'Relay secured. Restore the drone';
        ui('hint').textContent = 'Recover from the emergency to receive the transmission.';
        distress();
      }
    }
    if (action === 'overturn') distress();
    updateUI();
  }
  function ingest(line) {
    let recognized = false;
    const charge = line.match(/(?:charge|Remaining charge):?\s*(\d+)\/10/i);
    if (charge && Number(charge[1]) <= 10) { state.charge = Number(charge[1]); recognized = true; }
    if (/Entering STANDBY|Returning to STANDBY/i.test(line)) { mode('STANDBY'); recognized = true; }
    if (/Entering BATTLE|reverted to Battle/i.test(line)) { mode('BATTLE'); recognized = true; }
    if (/Entering THE LAST|Entering THE_LAST/i.test(line)) { distress(); recognized = true; }
    if (/Drone was attacked/i.test(line)) { mode('THE_LAST_OF_EE2028'); recognized = true; }
    if (/Laser gun fired/i.test(line)) { state.shot = 0.3; recognized = true; }
    if (/Cheerio!/.test(line)) { mode('TERMINATED'); finish(false); recognized = true; }
    if (/exceeds threshold|drops below threshold/i.test(line)) {
      ui('warning').textContent = line; recognized = true;
    }
    const telemetry = line.match(/Telemetry\s*->\s*T:([\d.+-]+)\s*°?C,\s*P:([\d.+-]+)\s*hPa,\s*H:([\d.+-]+)\s*%\s*\|\s*Accel:\[([^\]]+)\]\s*m\/s\^2\s*\|\s*Gyro:\[([^\]]+)\]\s*dps\s*\|\s*Magneto:\[([^\]]+)\]\s*mgauss/);
    if (telemetry) {
      const values = telemetry.slice(1);
      const numbers = values.flatMap(value => value.split(',').map(Number));
      if (numbers.length === 12 && numbers.every(Number.isFinite)) {
        const formatted = [values[0]+' °C',values[1]+' hPa',values[2]+' %','['+values[3]+'] m/s²','['+values[4]+'] °/s','['+values[5]+'] mG'];
        formatted.forEach((value,index) => samples.set(sensorNames[index],value));
        lastSample = performance.now(); ui('warning').textContent = 'Normal telemetry resumed.'; renderSensors(); recognized = true;
      }
    }
    if (/Rescue attempt|still upside down|charge too low|recharging|charge is already|noble sacrifice|self-destructing/i.test(line)) recognized = true;
    if (recognized) log(line);
    return recognized;
  }
  function renderSensors() {
    ui('sensors').replaceChildren();
    for (const name of sensorNames) {
      const card = document.createElement('div'), label = document.createElement('span'), value = document.createElement('strong');
      label.textContent = name; value.textContent = samples.get(name) || '—';
      card.append(label,value); ui('sensors').append(card);
    }
  }
  function updateUI() {
    ui('mode').textContent = state.mode.replaceAll('_',' ');
    ui('charge').textContent = state.charge === null ? '— / 10' : state.charge + ' / 10';
    ui('chargeBars').replaceChildren(...Array.from({length:10},(_,index) => {
      const bar = document.createElement('i'); bar.className = index < state.charge ? 'on' : ''; return bar;
    }));
    ui('emergency').hidden = state.mode !== 'THE_LAST_OF_EE2028' || state.ended;
    ui('countdown').textContent = state.remaining === null ? 'AWAITING EVENT' : state.remaining.toFixed(1);
    ui('coordinates').textContent = state.contact ? 'CONTACT / SIMULATED' : 'RELAY / ' + (state.recovered ? 'SECURED' : 'SILENT');
    for (const action of ['survey','threat','recharge','fire','overturn','rescue']) {
      const rescueMode = state.mode === 'THE_LAST_OF_EE2028';
      ui(action).disabled = state.ended || (rescueMode ? action !== 'rescue' : action === 'rescue')
        || (action === 'threat' && !state.surveying)
        || (['fire','recharge'].includes(action) && state.mode !== 'BATTLE');
    }
  }
  for (const action of ['survey','threat','recharge','fire','overturn','rescue']) ui(action).onclick = () => simulate(action);
  ui('restart').onclick = reset;
  ui('source').onchange = event => {
    source = event.target.value;
    ui('simulationControls').hidden = source !== 'simulation';
    ui('replayPanel').hidden = source !== 'replay';
    ui('freshness').textContent = source === 'simulation' ? 'Browser-generated events' : 'Pasted log · no live connection';
    reset();
  };
  ui('ingest').onclick = () => {
    const lines = ui('uart').value.split(/\r?\n/).filter(line => line.trim());
    let accepted = 0;
    for (const line of lines) if (ingest(line)) accepted++;
    ui('parseStatus').textContent = accepted + ' recognized / ' + lines.length + ' lines. Unrecognized lines were skipped.';
    if (accepted) ui('connection').textContent = 'UART REPLAY';
    updateUI();
  };
  ui('camera').onclick = () => {
    forward = !forward; ui('camera').textContent = forward ? 'Orbit camera' : 'Forward camera';
  };
  canvas.onpointerdown = event => { drag = event.clientX; canvas.setPointerCapture(event.pointerId); };
  canvas.onpointermove = event => {
    if (drag === null) return;
    orbit += (event.clientX-drag)*0.008; drag = event.clientX;
  };
  canvas.onpointerup = canvas.onpointercancel = () => { drag = null; };
  function point(x,y,z) {
    const camera = forward ? [0,5,3] : [Math.sin(orbit)*27,15,Math.cos(orbit)*27];
    const target = forward ? [0,5,-20] : [0,3,0];
    const dz = target[2]-camera[2], dx = target[0]-camera[0];
    const yaw = Math.atan2(dx,dz), pitch = Math.atan2(target[1]-camera[1],Math.hypot(dx,dz));
    const rx = x-camera[0], ry = y-camera[1], rz = z-camera[2];
    const lateral = rx*Math.cos(yaw)-rz*Math.sin(yaw);
    const depth = rx*Math.sin(yaw)+rz*Math.cos(yaw);
    const vertical = ry*Math.cos(pitch)-depth*Math.sin(pitch);
    const distance = ry*Math.sin(pitch)+depth*Math.cos(pitch);
    if (distance < 0.5) return null;
    const scale = Math.min(width,height)*1.1/distance;
    return {x:width/2+lateral*scale,y:height*0.57-vertical*scale,distance};
  }
  function polygon(vertices,color) {
    const projected = vertices.map(vertex => point(...vertex));
    if (projected.some(vertex => !vertex)) return;
    faces.push({vertices:projected,color,depth:projected.reduce((sum,vertex)=>sum+vertex.distance,0)/projected.length});
  }
  function box(x,y,z,w,h,d,color) {
    const v = [[x-w,y,z-d],[x+w,y,z-d],[x+w,y,z+d],[x-w,y,z+d],
      [x-w,y+h,z-d],[x+w,y+h,z-d],[x+w,y+h,z+d],[x-w,y+h,z+d]];
    for (const indices of [[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7],[4,5,6,7]]) polygon(indices.map(index=>v[index]),color);
  }
  function draw() {
    const sky = context.createLinearGradient(0,0,0,height);
    sky.addColorStop(0,'#0c1a23');sky.addColorStop(1,'#526666');
    context.fillStyle=sky;context.fillRect(0,0,width,height);
    context.fillStyle='#d9d2ab';context.beginPath();context.arc(width*.78,height*.2,22,0,Math.PI*2);context.fill();
    faces.length=0;
    for(let x=-60;x<60;x+=8) for(let z=-60;z<60;z+=8) {
      polygon([[x,-1,z],[x+8,-1,z],[x+8,-1,z+8],[x,-1,z+8]],(x+z)%16===0?'#243232':'#293635');
    }
    for(let i=0;i<24;i++) {
      const angle=i*2.4, radius=32+i%5*4, x=Math.sin(angle)*radius,z=Math.cos(angle)*radius;
      box(x,-1,z,3,4+i%7,3,'#30403f');
    }
    box(0,-1,-15,5,1,5,'#475452');
    box(0,0,-15,1.3,15,1.3,state.recovered?'#a9c79b':'#65706a');
    box(0,10,-15,5,.7,.6,'#6d7770');
    box(-7,-1,-19,3,4,3,'#3c4c49');
    const down=state.mode==='THE_LAST_OF_EE2028'||state.ended&&!state.recovered;
    const y=down?0:4+Math.sin(clock*2)*.15;
    if(!forward) {
      box(0,y,0,1.7,down?2.6:.8,1.2,'#97aaa1');
      for(const x of [-3,3]) for(const z of [-2,2]) {
        box(x/2,y+.1,z/2,1.1,.2,.3,'#596e68');
        box(x,y,z,.8,.25,.8,'#354a47');
        box(x,y+.28,z,.6,.05,.6,state.ended?'#263332':'#bbeb9d');
      }
      box(0,y+.15,-1.6,.3,.3,.9,state.mode==='BATTLE'?'#e5c593':'#54675f');
    }
    if(state.contact) box(5,3+Math.sin(clock*3),-9,1,1.5,1,'#c39273');
    faces.sort((a,b)=>b.depth-a.depth);
    for(const face of faces) {
      context.beginPath();face.vertices.forEach((vertex,index)=>index?context.lineTo(vertex.x,vertex.y):context.moveTo(vertex.x,vertex.y));
      context.closePath();context.fillStyle=face.color;context.fill();context.strokeStyle='#91b3a214';context.stroke();
    }
    if(state.scan>0) {
      const p=point(0,0,0);
      if(p){context.strokeStyle='#c2ec9e88';context.beginPath();context.ellipse(p.x,p.y,(2-state.scan)*180,(2-state.scan)*45,0,0,Math.PI*2);context.stroke();}
    }
    if(state.shot>0) {
      const start=point(0,y,-2),end=point(5,4,-9);
      if(start&&end){context.strokeStyle='#dcffb7';context.lineWidth=4;context.beginPath();context.moveTo(start.x,start.y);context.lineTo(end.x,end.y);context.stroke();context.lineWidth=1;}
    }
    if(forward){context.strokeStyle='#c2ec9e';context.strokeRect(width/2-12,height/2-12,24,24);}
  }
  function frame(now) {
    const dt=Math.min(.1,(now-previous)/1000);previous=now;clock+=dt;
    const rect=canvas.getBoundingClientRect(),ratio=Math.min(devicePixelRatio||1,2);
    if(canvas.width!==Math.round(rect.width*ratio)||canvas.height!==Math.round(rect.height*ratio)){
      width=rect.width;height=rect.height;canvas.width=Math.round(width*ratio);canvas.height=Math.round(height*ratio);context.setTransform(ratio,0,0,ratio,0,0);
    }
    state.shot=Math.max(0,state.shot-dt);state.scan=Math.max(0,state.scan-dt);
    if(state.remaining!==null&&!state.ended){
      state.remaining=Math.max(0,state.remaining-dt);
      if(!state.remaining){mode('TERMINATED');finish(false);}
      updateUI();
    }
    ui('sampleAge').textContent=lastSample?'Last sample '+Math.floor((now-lastSample)/1000)+'s ago'+(now-lastSample>3000?' · STALE':''):'No sample';
    draw();requestAnimationFrame(frame);
  }
  reset();requestAnimationFrame(frame);
})();
