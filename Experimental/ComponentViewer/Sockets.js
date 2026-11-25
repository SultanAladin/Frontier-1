// Sockets: attachment points that can move. A socket is a scene component whose children ride on a *joint frame*;
// the socket kind decides how that frame may move and what drives it. Pure data and maths (no DOM, no three.js), so
// the same step runs in the editor, in Simulate and in the headless checks.
//
//   fixed    rigid mount (the classic static socket)
//   hinge    rotates about one axis between two angles               doors, flaps, steering knuckles
//   slider   travels along one axis between two stops                seat rails, pistons, lifts
//   ball     swings inside a cone around the axis                     tow balls, mirrors, antennae
//   spring   slider with stiffness and damping, reacts to load/bumps  suspension, bumpers
//   spin     turns continuously (fixed rpm or from vehicle speed)     wheels, fans, turrets
//   tether   children hang on a length and swing as a pendulum        cables, lamps, tow straps
//   dock     connects and disconnects: snaps inside a radius, breaks  trailers, wings, tool heads
//            above a force, only accepts children carrying its key
//
// Joint frame = rotation, then translation in the rotated frame: child = R · (offset + p). Quaternions are [x, y, z, w].

export const SOCKET_KINDS = [
  { id: 'fixed', label: 'Fixed', color: '#d8dce4', icon: 'sockFixed', note: 'Rigid mount. Children ride along exactly.' },
  { id: 'hinge', label: 'Hinge', color: '#4d9bff', icon: 'sockHinge', note: 'Rotates about one axis between two angles. Doors, flaps, steering.' },
  { id: 'slider', label: 'Slider', color: '#29c7c2', icon: 'sockSlider', note: 'Travels along one axis between two stops. Rails, pistons, lifts.' },
  { id: 'ball', label: 'Ball', color: '#a77bff', icon: 'sockBall', note: 'Swings inside a cone around its axis. Tow balls, mirrors, antennae.' },
  { id: 'spring', label: 'Spring', color: '#59d15c', icon: 'sockSpring', note: 'A slider with stiffness and damping that reacts to load and road bumps.' },
  { id: 'spin', label: 'Spin', color: '#f7cc40', icon: 'sockSpin', note: 'Turns continuously at a fixed rpm or with the vehicle speed.' },
  { id: 'tether', label: 'Tether', color: '#ff8a4d', icon: 'sockTether', note: 'Children hang on a length and swing like a pendulum. Cables, lamps, straps.' },
  { id: 'dock', label: 'Dock', color: '#ff5e8a', icon: 'sockDock', note: 'Connects and disconnects. Snaps inside a radius, breaks above a force, accepts only children tagged with its key.' },
];
export const SOCKET_KIND = Object.fromEntries(SOCKET_KINDS.map((k) => [k.id, k]));

export const DOCK_COLORS = { connected: '#46d267', seeking: '#ffb238', rejected: '#f04d3d' };

// Defaults for every socket parameter (all kinds share one prop bag, so switching kind keeps your numbers).
export function socketDefaults(kind = 'fixed') {
  return {
    kind, axis: 'Y', drive: 'hold', pose: 0.5, period: 3,
    angleMin: -45, angleMax: 45,               // hinge, degrees
    travelMin: 0, travelMax: 0.3,              // slider, metres
    cone: 30,                                  // ball, degrees
    springHz: 2.2, damping: 0.35, springTravel: 0.12, bumpSpacing: 6, loadGain: 1, // spring
    rpm: 120, spinSource: 'rpm', spinRadius: 0.5, // spin
    length: 0.5, swing: 20, tetherDamping: 0.15, // tether
    key: '', snapRadius: 0.25, breakForce: 1500, connected: true, // dock
  };
}

// Which props each kind shows in Details, in order.
export const SOCKET_FIELDS = {
  fixed: [],
  hinge: ['axis', 'angleMin', 'angleMax', 'drive', 'pose', 'period'],
  slider: ['axis', 'travelMin', 'travelMax', 'drive', 'pose', 'period'],
  ball: ['axis', 'cone', 'drive', 'pose', 'period'],
  spring: ['axis', 'springHz', 'damping', 'springTravel', 'pose', 'bumpSpacing', 'loadGain'],
  spin: ['axis', 'spinSource', 'rpm', 'spinRadius'],
  tether: ['axis', 'length', 'swing', 'tetherDamping'],
  dock: ['axis', 'key', 'snapRadius', 'breakForce', 'drive', 'connected'],
};

export const DRIVES = {
  hinge: [['hold', 'Hold at pose'], ['sweep', 'Sweep between limits']],
  slider: [['hold', 'Hold at pose'], ['sweep', 'Sweep between stops']],
  ball: [['hold', 'Hold at pose'], ['sweep', 'Orbit the cone']],
  dock: [['hold', 'Hold (Connect / Release)'], ['pull', 'Pull test: load until it breaks'], ['aero', 'Aero load from Vehicle Movement']],
};

const AXES = { X: [1, 0, 0], Y: [0, 1, 0], Z: [0, 0, 1] };
// Reference direction u for each axis (angle 0) and v = axis × u, so rotating by θ about the axis takes u to
// u·cosθ + v·sinθ.
const BASIS = { X: { u: [0, 1, 0], v: [0, 0, 1] }, Y: { u: [1, 0, 0], v: [0, 0, -1] }, Z: { u: [1, 0, 0], v: [0, 1, 0] } };
export const axisVector = (axis) => AXES[axis] || AXES.Y;
export const axisBasis = (axis) => BASIS[axis] || BASIS.Y;

const DEG = Math.PI / 180;
const G = 9.81;
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const lerp = (a, b, t) => a + (b - a) * t;
const quatAxis = ([x, y, z], angle) => { const s = Math.sin(angle / 2); return [x * s, y * s, z * s, Math.cos(angle / 2)]; };
export const quatMul = (a, b) => [
  a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
];
export function rotateVector(q, v) {
  const [x, y, z, w] = q;
  const ix = w * v[0] + y * v[2] - z * v[1], iy = w * v[1] + z * v[0] - x * v[2], iz = w * v[2] + x * v[1] - y * v[0], iw = -x * v[0] - y * v[1] - z * v[2];
  return [ix * w + iw * -x + iy * -z - iz * -y, iy * w + iw * -y + iz * -x - ix * -z, iz * w + iw * -z + ix * -y - iy * -x];
}

export function createSocketState() {
  return { t: 0, angle: 0, travel: 0, x: 0, v: 0, spin: 0, theta: 0, omega: 0, thetaInit: false, phi: 0, bumpCell: null,
    dock: { phase: null, gap: 0, load: 0, wait: 0, ripple: 0 } };
}

// Advance one socket by dt. input: { simulate, accel (m/s², along the vehicle), speed (m/s), distance (m),
// along (socket position along the vehicle, m), downforce (N), accepts (dock key matches its children) }.
// Returns the events raised this step: [{ type: 'connected' | 'released' | 'rejected', load }].
export function stepSocket(state, p, dt, input = {}) {
  const events = [];
  const sim = !!input.simulate;
  state.t += dt;
  const sweep = sim && p.drive === 'sweep';
  const wave = 0.5 - 0.5 * Math.cos((2 * Math.PI * state.t) / Math.max(0.2, p.period));
  switch (p.kind) {
    case 'hinge': state.angle = lerp(p.angleMin, p.angleMax, sweep ? wave : clamp(p.pose, 0, 1)); break;
    case 'slider': state.travel = lerp(p.travelMin, p.travelMax, sweep ? wave : clamp(p.pose, 0, 1)); break;
    case 'ball': {
      state.theta = p.cone * DEG * (sweep ? 1 : clamp(p.pose, 0, 1));
      if (sweep) state.phi = (2 * Math.PI * state.t) / Math.max(0.2, p.period);
      else state.phi = 0;
      break;
    }
    case 'spring': {
      // x'' = −ω²(x − rest) − 2ζω x' + load ; rest from the pose (0 = full droop, 1 = full bump), clamped to the travel.
      const w = 2 * Math.PI * p.springHz, rest = lerp(-p.springTravel, p.springTravel, clamp(p.pose, 0, 1)) * 0.5;
      const load = sim ? (input.accel || 0) * p.loadGain * -Math.sign(input.along || 0) * 0.6 : 0;
      // Road bumps: one every bumpSpacing metres of travel, met by each spring when it reaches that spot.
      if (sim && p.bumpSpacing > 0) {
        const cell = Math.floor(((input.distance ?? state.t) + (input.along || 0)) / p.bumpSpacing);
        if (state.bumpCell !== null && cell !== state.bumpCell) state.v += 0.9;
        state.bumpCell = cell;
      }
      const sub = Math.max(1, Math.ceil(dt / (1 / 240)));
      for (let i = 0; i < sub; i++) {
        const h = dt / sub;
        const a = -w * w * (state.x - rest) - 2 * p.damping * w * state.v + load;
        state.v += a * h; state.x += state.v * h;
        if (state.x > p.springTravel) { state.x = p.springTravel; state.v = Math.min(0, state.v) * -0.3; }
        if (state.x < -p.springTravel) { state.x = -p.springTravel; state.v = Math.max(0, state.v) * -0.3; }
      }
      break;
    }
    case 'spin': {
      const rate = p.spinSource === 'vehicle' ? (input.speed || 0) / Math.max(0.05, p.spinRadius) : (p.rpm * 2 * Math.PI) / 60;
      state.rate = rate;
      if (sim || p.spinSource === 'rpm') state.spin = (state.spin + rate * dt) % (2 * Math.PI);
      break;
    }
    case 'tether': {
      // Pendulum in the plane of travel (θ > 0 = trailing backwards): θ'' = −(g/L) sinθ − cθ' + (a/L) cosθ, where the
      // vehicle's forward acceleration a appears as a backward pseudo-force.
      if (!state.thetaInit) { state.theta = p.swing * DEG; state.omega = 0; state.thetaInit = true; }
      const L = Math.max(0.02, p.length), accel = sim ? (input.accel || 0) : 0;
      const sub = Math.max(1, Math.ceil(dt / (1 / 240)));
      for (let i = 0; i < sub; i++) {
        const h = dt / sub;
        const a = -(G / L) * Math.sin(state.theta) - p.tetherDamping * state.omega + (accel / L) * Math.cos(state.theta);
        state.omega += a * h; state.theta += state.omega * h;
      }
      break;
    }
    case 'dock': stepDock(state.dock, p, dt, input, sim, events); break;
  }
  return events;
}

function stepDock(d, p, dt, input, sim, events) {
  const accepts = input.accepts !== false;
  const out = Math.max(0.05, p.snapRadius) * 1.8;
  if (d.phase === null) { d.phase = p.connected && accepts ? 'connected' : accepts ? 'released' : 'rejected'; d.gap = d.phase === 'connected' ? 0 : out; }
  d.ripple = Math.max(0, d.ripple - dt);
  const connect = () => { d.phase = 'connected'; d.ripple = 0.6; d.load = 0; events.push({ type: 'connected', load: 0 }); };
  const release = (load) => { d.phase = 'released'; d.wait = 0; events.push({ type: 'released', load }); };

  // Load on the joint.
  if (d.phase === 'connected') {
    if (sim && p.drive === 'pull') d.load += p.breakForce * 0.4 * dt;          // ramps to 1.2× in 3 s
    else if (sim && p.drive === 'aero') d.load = input.downforce || 0;
    else d.load = Math.max(0, d.load - p.breakForce * dt);
    if (sim && d.load > p.breakForce) release(d.load);
    else if (!sim && !p.connected) release(0);
  }
  if (d.phase === 'connected') { d.gap += (0 - d.gap) * (1 - Math.exp(-dt * 30)); return; }

  // Free: drift out, wait, come back; inside the snap radius the magnet pulls it home (or it bounces if the key is wrong).
  const wantsBack = sim ? (p.drive === 'pull' || p.drive === 'hold' ? true : (input.downforce || 0) < p.breakForce * 0.5) : p.connected;
  if (d.phase === 'released' || d.phase === 'rejected') {
    if (d.gap < out - 1e-3 && d.wait === 0) d.gap = Math.min(out, d.gap + dt * out * 2.5);
    else { d.wait += dt; if (wantsBack && d.wait > (sim ? 0.9 : 0)) d.phase = 'returning'; }
    return;
  }
  if (d.phase === 'returning') {
    const speed = d.gap < p.snapRadius ? (accepts ? out * 3 * (1 + (p.snapRadius - d.gap) / p.snapRadius * 3) : -out * 2) : out * 0.6;
    d.gap = clamp(d.gap - speed * dt, 0, out);
    if (!accepts && d.gap >= p.snapRadius * 1.05 && speed < 0) { d.phase = 'rejected'; d.wait = 0; events.push({ type: 'rejected', load: 0 }); }
    if (accepts && d.gap <= 1e-3) connect();
  }
}

// The joint frame for the current state.
export function socketPose(p, s) {
  const a = axisVector(p.axis), b = axisBasis(p.axis);
  const id = [0, 0, 0, 1];
  switch (p.kind) {
    case 'hinge': return { rotation: quatAxis(a, s.angle * DEG), offset: [0, 0, 0] };
    case 'slider': return { rotation: id, offset: a.map((c) => c * s.travel) };
    case 'spring': return { rotation: id, offset: a.map((c) => c * s.x) };
    case 'spin': return { rotation: quatAxis(a, s.spin), offset: [0, 0, 0] };
    case 'ball': {
      // Swing by θ about the direction that is φ around the axis from v.
      const dir = [0, 1, 2].map((i) => b.v[i] * Math.cos(s.phi) - b.u[i] * Math.sin(s.phi));
      return { rotation: quatAxis(dir, s.theta), offset: [0, 0, 0] };
    }
    case 'tether': {
      // Hang along −axis; swing about v (θ > 0 swings the end toward −u, i.e. backwards under forward acceleration).
      return { rotation: quatAxis(b.v, s.theta), offset: a.map((c) => -c * p.length) };
    }
    case 'dock': return { rotation: id, offset: a.map((c) => c * s.dock.gap) };
  }
  return { rotation: id, offset: [0, 0, 0] };
}

// One-line live readout for the socket tag.
export function socketReadout(p, s) {
  switch (p.kind) {
    case 'hinge': return `${s.angle.toFixed(0)}°  ·  ${p.angleMin}…${p.angleMax}°`;
    case 'slider': return `${s.travel.toFixed(3)} m  ·  ${p.travelMin}…${p.travelMax} m`;
    case 'ball': return `${(s.theta / DEG).toFixed(0)}° of ${p.cone}°`;
    case 'spring': return `${(s.x * 1000).toFixed(0)} mm  ·  ${p.springHz} Hz ζ ${p.damping}`;
    case 'spin': return `${(((s.rate || 0) * 60) / (2 * Math.PI)).toFixed(0)} rpm`;
    case 'tether': return `${(s.theta / DEG).toFixed(0)}°  ·  ${p.length} m`;
    case 'dock': {
      const d = s.dock;
      if (d.phase === 'connected') return `connected  ·  ${(d.load / 1000).toFixed(2)} / ${(p.breakForce / 1000).toFixed(2)} kN`;
      return `${d.phase || 'released'}  ·  gap ${d.gap.toFixed(2)} m`;
    }
  }
  return 'rigid';
}

// Does a dock accept a child? Empty key accepts anything; otherwise the child's tags must contain the key.
export function dockAccepts(key, tags) {
  const k = String(key || '').trim().toLowerCase();
  if (!k) return true;
  return String(tags || '').toLowerCase().split(',').map((t) => t.trim()).includes(k);
}

// Details UI for each parameter (the viewer builds fields from this).
export const SOCKET_PARAMS = {
  axis: { label: 'Axis', type: 'axis' },
  angleMin: { label: 'Minimum angle', type: 'range', min: -180, max: 180, step: 1, unit: '°' },
  angleMax: { label: 'Maximum angle', type: 'range', min: -180, max: 180, step: 1, unit: '°' },
  travelMin: { label: 'Near stop', type: 'range', min: -2, max: 2, step: 0.01, unit: 'm' },
  travelMax: { label: 'Far stop', type: 'range', min: -2, max: 2, step: 0.01, unit: 'm' },
  cone: { label: 'Swing cone', type: 'range', min: 1, max: 85, step: 1, unit: '°' },
  drive: { label: 'Drive', type: 'drive' },
  pose: { label: 'Pose', type: 'pose' },
  period: { label: 'Cycle time', type: 'range', min: 0.4, max: 12, step: 0.1, unit: 's', note: 'Sweep / orbit, while simulating' },
  springHz: { label: 'Natural frequency', type: 'range', min: 0.5, max: 6, step: 0.05, unit: 'Hz' },
  damping: { label: 'Damping ratio', type: 'range', min: 0.05, max: 1.2, step: 0.01, unit: 'ζ' },
  springTravel: { label: 'Travel (each way)', type: 'range', min: 0.01, max: 0.5, step: 0.005, unit: 'm' },
  bumpSpacing: { label: 'Road bump spacing', type: 'range', min: 0, max: 30, step: 0.5, unit: 'm', note: '0 = smooth road' },
  loadGain: { label: 'Reacts to acceleration', type: 'range', min: 0, max: 3, step: 0.05, unit: '×' },
  spinSource: { label: 'Speed from', type: 'select', options: [['rpm', 'Fixed rpm'], ['vehicle', 'Vehicle speed']] },
  rpm: { label: 'Rate', type: 'range', min: -3000, max: 3000, step: 10, unit: 'rpm' },
  spinRadius: { label: 'Rolling radius', type: 'range', min: 0.05, max: 1.5, step: 0.005, unit: 'm', note: 'Used with vehicle speed' },
  length: { label: 'Length', type: 'range', min: 0.05, max: 5, step: 0.01, unit: 'm' },
  swing: { label: 'Starting swing', type: 'range', min: -80, max: 80, step: 1, unit: '°' },
  tetherDamping: { label: 'Air drag', type: 'range', min: 0, max: 2, step: 0.01, unit: '/s' },
  key: { label: 'Key', type: 'text', placeholder: 'any child' },
  snapRadius: { label: 'Snap radius', type: 'range', min: 0.05, max: 2, step: 0.01, unit: 'm' },
  breakForce: { label: 'Break force', type: 'range', min: 100, max: 20000, step: 50, unit: 'N' },
  connected: { label: 'Connected', type: 'bool' },
};

// Value the pose slider stands for, for its label.
export function poseLabel(p, x) {
  switch (p.kind) {
    case 'hinge': return `${(p.angleMin + (p.angleMax - p.angleMin) * x).toFixed(0)}°`;
    case 'slider': return `${(p.travelMin + (p.travelMax - p.travelMin) * x).toFixed(2)} m`;
    case 'ball': return `${(p.cone * x).toFixed(0)}° tilt`;
    case 'spring': return `${(((x - 0.5) * p.springTravel) * 1000).toFixed(0)} mm preload`;
  }
  return x.toFixed(2);
}
