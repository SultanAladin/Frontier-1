// Component model for the Component Viewer: a blueprint class is a tree of scene components (each with a transform)
// plus a flat list of actor components (no transform), like an Unreal Blueprint class. Pure data, no DOM.
//
// Vehicle Lab heritage: the ControlVehicle sample and the Vehicle Movement component carry Vehicle Lab's authored
// numbers (sockets, mass, CoM, aero, torque curve, gearing) from Frontier-OLD's VehicleProvingGrounds sheets.

import { socketDefaults, SOCKET_KIND, dockAccepts } from './Sockets.js';

export const COMPONENT_CLASSES = {
  Origin: { label: 'Origin', icon: 'root', scene: true, group: 'Common', props: [] }, // the class's own frame: every scene component hangs from it
  Scene: { label: 'Scene', icon: 'pivot', scene: true, group: 'Common', props: [] },
  // A socket's children ride on its joint frame; the kind (Fixed, Hinge, Slider, Ball, Spring, Spin, Tether, Dock)
  // decides how that frame moves. See Sockets.js.
  Socket: { label: 'Socket', icon: 'sockDock', scene: true, group: 'Sockets', props: [{ key: 'kind', label: 'Socket', type: 'socket', section: 'Socket' }] },
  StaticMesh: {
    label: 'Static Mesh', icon: 'cube', scene: true, group: 'Common', props: [
      { key: 'asset', label: 'Static mesh', type: 'asset', section: 'Static Mesh' },
      { key: 'lod', label: 'Forced LOD', type: 'range', min: -1, max: 12, step: 1, section: 'Static Mesh', note: '−1 = automatic (full detail)' },
      { key: 'castShadow', label: 'Cast shadow', type: 'bool', section: 'Lighting' },
      { key: 'slots', label: 'Material slots', type: 'slots', section: 'Materials' },
      { key: 'collisionPreset', label: 'Collision preset', type: 'select', options: ['Use compiled', 'NoCollision', 'BlockAll', 'OverlapAll'], section: 'Collision' },
      { key: 'simulatePhysics', label: 'Simulate physics', type: 'bool', section: 'Physics' },
      { key: 'mass', label: 'Mass', type: 'range', min: 0, max: 2000, step: 1, unit: 'kg', section: 'Physics' },
    ],
  },
  PointLight: {
    label: 'Point Light', icon: 'bulb', scene: true, group: 'Lights', props: [
      { key: 'intensity', label: 'Intensity', type: 'range', min: 0, max: 50, step: 0.1, unit: 'cd', section: 'Light' },
      { key: 'color', label: 'Light colour', type: 'color', section: 'Light' },
      { key: 'range', label: 'Attenuation radius', type: 'range', min: 0.1, max: 30, step: 0.1, unit: 'm', section: 'Light' },
    ],
  },
  SpotLight: {
    label: 'Spot Light', icon: 'spot', scene: true, group: 'Lights', props: [
      { key: 'intensity', label: 'Intensity', type: 'range', min: 0, max: 80, step: 0.1, unit: 'cd', section: 'Light' },
      { key: 'color', label: 'Light colour', type: 'color', section: 'Light' },
      { key: 'angle', label: 'Outer cone angle', type: 'range', min: 5, max: 80, step: 1, unit: '°', section: 'Light' },
      { key: 'range', label: 'Attenuation radius', type: 'range', min: 0.1, max: 60, step: 0.1, unit: 'm', section: 'Light' },
    ],
  },
  Camera: {
    label: 'Camera', icon: 'camera', scene: true, group: 'Common', props: [
      { key: 'fov', label: 'Field of view', type: 'range', min: 20, max: 120, step: 1, unit: '°', section: 'Camera' },
      { key: 'autoActivate', label: 'Auto activate', type: 'bool', section: 'Camera' },
    ],
  },
  BoxCollision: {
    label: 'Box Collision', icon: 'box', scene: true, group: 'Collision', props: [
      { key: 'extent', label: 'Box extent', type: 'vec3', step: 0.01, unit: 'm', section: 'Shape' },
      { key: 'trigger', label: 'Generate overlap events', type: 'bool', section: 'Collision' },
    ],
  },
  SphereCollision: {
    label: 'Sphere Collision', icon: 'sphere', scene: true, group: 'Collision', props: [
      { key: 'radius', label: 'Sphere radius', type: 'range', min: 0.05, max: 10, step: 0.01, unit: 'm', section: 'Shape' },
      { key: 'trigger', label: 'Generate overlap events', type: 'bool', section: 'Collision' },
    ],
  },
  CapsuleCollision: {
    label: 'Capsule Collision', icon: 'capsule', scene: true, group: 'Collision', props: [
      { key: 'radius', label: 'Capsule radius', type: 'range', min: 0.05, max: 5, step: 0.01, unit: 'm', section: 'Shape' },
      { key: 'halfHeight', label: 'Half height', type: 'range', min: 0.05, max: 5, step: 0.01, unit: 'm', section: 'Shape' },
    ],
  },
  Arrow: {
    label: 'Arrow', icon: 'arrow', scene: true, group: 'Common', props: [
      { key: 'color', label: 'Arrow colour', type: 'color', section: 'Arrow' },
      { key: 'length', label: 'Arrow length', type: 'range', min: 0.1, max: 5, step: 0.05, unit: 'm', section: 'Arrow' },
    ],
  },
  VehicleMovement: {
    label: 'Vehicle Movement', icon: 'vehicle', scene: false, group: 'Movement', props: [
      { key: 'mass', label: 'Vehicle mass', type: 'range', min: 800, max: 1800, step: 1, unit: 'kg', section: 'Chassis & mass' },
      { key: 'comHeight', label: 'Centre of mass height', type: 'range', min: 0.3, max: 0.8, step: 0.005, unit: 'm', section: 'Chassis & mass' },
      { key: 'frontWeight', label: 'Front weight distribution', type: 'range', min: 35, max: 65, step: 0.1, unit: '%', section: 'Chassis & mass' },
      { key: 'dragArea', label: 'Drag area · CdA', type: 'range', min: 0.3, max: 1.2, step: 0.01, unit: 'm²', section: 'Aerodynamics' },
      { key: 'liftArea', label: 'Downforce area · ClA', type: 'range', min: 0, max: 2, step: 0.01, unit: 'm²', section: 'Aerodynamics' },
      { key: 'springHz', label: 'Natural frequency', type: 'range', min: 1, max: 4, step: 0.05, unit: 'Hz', section: 'Suspension' },
      { key: 'dampingRatio', label: 'Damping ratio · ζ', type: 'range', min: 0.15, max: 1.1, step: 0.01, unit: 'ζ', section: 'Suspension' },
      { key: 'maxSteer', label: 'Maximum steering lock', type: 'range', min: 0.35, max: 0.8, step: 0.01, unit: 'rad', section: 'Suspension' },
      { key: 'finalDrive', label: 'Final drive ratio', type: 'range', min: 2.5, max: 5, step: 0.01, unit: ':1', section: 'Powertrain' },
      { key: 'idleRpm', label: 'Idle', type: 'range', min: 500, max: 1500, step: 10, unit: 'rpm', section: 'Powertrain' },
      { key: 'redlineRpm', label: 'Redline', type: 'range', min: 5000, max: 9000, step: 50, unit: 'rpm', section: 'Powertrain' },
      { key: 'tyreRadius', label: 'Tyre radius', type: 'range', min: 0.25, max: 0.7, step: 0.001, unit: 'm', section: 'Wheels' },
      { key: 'wheels', label: 'Wheel components', type: 'wheels', section: 'Wheels' },
    ],
  },
  RotatingMovement: {
    label: 'Rotating Movement', icon: 'rotate', scene: false, group: 'Movement', props: [
      { key: 'rate', label: 'Rotation rate (yaw)', type: 'range', min: -360, max: 360, step: 1, unit: '°/s', section: 'Rotation' },
      { key: 'target', label: 'Updated component', type: 'component', section: 'Rotation' },
    ],
  },
};

export const SCENE_PROPS = [
  { key: 'location', label: 'Location', type: 'vec3', step: 0.01, unit: 'm', section: 'Transform' },
  { key: 'rotation', label: 'Rotation', type: 'vec3', step: 1, unit: '°', section: 'Transform' },
  { key: 'scale', label: 'Scale', type: 'vec3', step: 0.01, unit: '×', section: 'Transform' },
  { key: 'visible', label: 'Visible', type: 'bool', section: 'Rendering' },
  { key: 'hiddenInGame', label: 'Hidden in game', type: 'bool', section: 'Rendering' },
];

export const COMMON_PROPS = [
  { key: 'tags', label: 'Component tags', type: 'text', section: 'Tags' },
];

// Vehicle Lab sheets (Frontier-OLD · VehicleProvingGrounds/Source/VehicleSpecification.js).
export const VEHICLE_SHEET = {
  axleMount: [[1.7274, 1.0475, 0.0914], [1.7274, -1.0475, 0.0914], [-1.6686, 1.0475, 0.0914], [-1.6686, -1.0475, 0.0914]],
  groundOffset: 0.50855 - 0.0914,
  torqueCurve: [[800, 234], [1200, 300], [1600, 352], [2000, 392], [2400, 428], [2800, 460], [3200, 486], [3600, 504], [4000, 514],
    [4400, 518], [4800, 520], [5200, 520], [5800, 520], [6200, 506], [6600, 486], [7000, 462], [7600, 448], [8500, 442]],
  gearRatios: [4.2, 3.1, 2.5, 1.9, 1.5, 1.2],
  driveEfficiency: 0.92,
  upshiftRpm: 6800,
  rollingResistance: 0.015,
  airDensity: 1.225,
  maxBrakeTorquePerWheel: 2600,
};

const DEFAULTS = {
  StaticMesh: () => ({ asset: null, lod: -1, castShadow: true, slots: {}, collisionPreset: 'Use compiled', simulatePhysics: false, mass: 0 }),
  PointLight: () => ({ intensity: 6, color: '#ffd9a8', range: 8 }),
  SpotLight: () => ({ intensity: 20, color: '#f4f6ff', angle: 30, range: 25 }),
  Camera: () => ({ fov: 70, autoActivate: true }),
  BoxCollision: () => ({ extent: [0.5, 0.5, 0.5], trigger: false }),
  SphereCollision: () => ({ radius: 0.5, trigger: false }),
  CapsuleCollision: () => ({ radius: 0.35, halfHeight: 0.6 }),
  Arrow: () => ({ color: '#ffb238', length: 1 }),
  VehicleMovement: () => ({
    mass: 1300, comHeight: 0.53, frontWeight: 49.14, dragArea: 0.74, liftArea: 1.1, springHz: 2, dampingRatio: 0.6, maxSteer: 0.52,
    finalDrive: 3.55, idleRpm: 700, redlineRpm: 7200, tyreRadius: 0.50855, wheels: [],
    torqueCurve: VEHICLE_SHEET.torqueCurve.map((p) => p.slice()), interpolation: 'smooth',
  }),
  RotatingMovement: () => ({ rate: 45, target: null }),
  Socket: () => socketDefaults('fixed'),
};

let nextComponent = 1;
let nextBlueprint = 1;

export function createComponent(type, name, overrides = {}) {
  const cls = COMPONENT_CLASSES[type];
  if (!cls) throw new Error(`Unknown component class ${type}`);
  const c = {
    id: `C${nextComponent++}`, type, name: name || cls.label.replace(/\s+/g, ''), parent: null,
    props: { ...(DEFAULTS[type] ? DEFAULTS[type]() : {}), tags: '' },
  };
  if (cls.scene) Object.assign(c.props, { location: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1], visible: true, hiddenInGame: false });
  for (const [k, v] of Object.entries(overrides)) c.props[k] = v;
  return c;
}

export function createBlueprint(name, parentClass = 'Actor') {
  const root = createComponent('Origin', 'Origin');
  return { id: `BP${nextBlueprint++}`, name, parentClass, components: [root], rootId: root.id, dirty: false, compiledRevision: 0, revision: 0 };
}

export function findComponent(bp, id) { return bp.components.find((c) => c.id === id) || null; }
export function childrenOf(bp, id) { return bp.components.filter((c) => c.parent === id); }
export function sceneComponents(bp) { return bp.components.filter((c) => COMPONENT_CLASSES[c.type].scene); }
export function actorComponents(bp) { return bp.components.filter((c) => !COMPONENT_CLASSES[c.type].scene); }

// Adds a component. Scene components attach to `parentId` (or the root); actor components have no parent.
export function addComponent(bp, type, { parentId = null, name = null, props = {} } = {}) {
  const c = createComponent(type, uniqueName(bp, name || COMPONENT_CLASSES[type].label.replace(/\s+/g, '')), props);
  if (COMPONENT_CLASSES[type].scene) {
    const parent = parentId && findComponent(bp, parentId);
    c.parent = parent && COMPONENT_CLASSES[parent.type].scene ? parent.id : bp.rootId;
  }
  bp.components.push(c);
  touch(bp);
  return c;
}

export function removeComponent(bp, id) {
  if (id === bp.rootId) return false;
  const c = findComponent(bp, id);
  if (!c) return false;
  for (const child of childrenOf(bp, id)) child.parent = c.parent; // children move up, like Unreal
  bp.components = bp.components.filter((x) => x.id !== id);
  for (const x of bp.components) {
    if (x.props.target === id) x.props.target = null;
    if (Array.isArray(x.props.wheels)) x.props.wheels = x.props.wheels.filter((w) => w !== id);
  }
  touch(bp);
  return true;
}

export function isAncestor(bp, ancestorId, id) {
  let c = findComponent(bp, id);
  while (c && c.parent) { if (c.parent === ancestorId) return true; c = findComponent(bp, c.parent); }
  return false;
}

// Re-parents a scene component. Refuses cycles and the root.
export function attachComponent(bp, id, parentId) {
  if (id === bp.rootId || id === parentId) return false;
  const c = findComponent(bp, id), p = findComponent(bp, parentId);
  if (!c || !p || !COMPONENT_CLASSES[c.type].scene || !COMPONENT_CLASSES[p.type].scene) return false;
  if (isAncestor(bp, id, parentId)) return false;
  c.parent = parentId;
  touch(bp);
  return true;
}

export function duplicateComponent(bp, id) {
  const c = findComponent(bp, id);
  if (!c || id === bp.rootId) return null;
  const copy = createComponent(c.type, uniqueName(bp, c.name), JSON.parse(JSON.stringify(c.props)));
  copy.parent = c.parent;
  bp.components.push(copy);
  touch(bp);
  return copy;
}

export function renameComponent(bp, id, name) {
  const c = findComponent(bp, id);
  const clean = String(name).trim().replace(/[^\w\- ]/g, '');
  if (!c || !clean) return false;
  if (bp.components.some((x) => x !== c && x.name === clean)) return false;
  c.name = clean;
  touch(bp);
  return true;
}

export function uniqueName(bp, base) {
  const names = new Set(bp.components.map((c) => c.name));
  if (!names.has(base)) return base;
  let i = 1;
  while (names.has(`${base}${i}`)) i++;
  return `${base}${i}`;
}

export function touch(bp) { bp.revision++; bp.dirty = true; }

// Depth-first scene order (for the tree and the construction graph).
export function walkScene(bp, visit) {
  const go = (id, depth) => {
    const c = findComponent(bp, id);
    if (!c) return;
    visit(c, depth);
    for (const child of childrenOf(bp, id)) go(child.id, depth + 1);
  };
  go(bp.rootId, 0);
}

// Compile check: what Unreal would flag before the class compiles.
export function validateBlueprint(bp, assets) {
  const issues = [];
  for (const c of bp.components) {
    if (c.type === 'StaticMesh') {
      if (!c.props.asset) issues.push({ level: 'warn', id: c.id, text: `${c.name} has no static mesh` });
      else {
        const a = assets.get(c.props.asset);
        if (!a) issues.push({ level: 'error', id: c.id, text: `${c.name} references a missing asset` });
        else if (!a.out.geometry) issues.push({ level: 'error', id: c.id, text: `${c.name}: ${a.name} is not compiled` });
      }
    }
    if (c.type === 'VehicleMovement') {
      if (c.props.wheels.length !== 4) issues.push({ level: 'warn', id: c.id, text: `${c.name} drives ${c.props.wheels.length} wheels (4 expected)` });
      const curve = c.props.torqueCurve;
      for (let i = 1; i < curve.length; i++) if (curve[i][0] <= curve[i - 1][0]) { issues.push({ level: 'error', id: c.id, text: 'Torque curve RPM must increase' }); break; }
    }
    if (c.type === 'RotatingMovement' && c.props.target && !findComponent(bp, c.props.target)) issues.push({ level: 'error', id: c.id, text: `${c.name} target is missing` });
    if (c.type === 'Socket') {
      const p = c.props, kind = SOCKET_KIND[p.kind];
      if (!kind) issues.push({ level: 'error', id: c.id, text: `${c.name} has an unknown socket kind` });
      if (p.kind === 'hinge' && p.angleMin >= p.angleMax) issues.push({ level: 'error', id: c.id, text: `${c.name}: hinge minimum must be below maximum` });
      if (p.kind === 'slider' && p.travelMin >= p.travelMax) issues.push({ level: 'error', id: c.id, text: `${c.name}: slider stops must increase` });
      if (p.kind === 'dock') {
        const kids = childrenOf(bp, c.id);
        if (!kids.length) issues.push({ level: 'warn', id: c.id, text: `${c.name}: dock holds nothing (attach a child to plug in)` });
        for (const k of kids) if (!dockAccepts(p.key, k.props.tags)) issues.push({ level: 'warn', id: c.id, text: `${k.name} is not tagged “${p.key}”, so ${c.name} will reject it` });
      }
    }
  }
  return issues;
}

// ------------------------------------------------------------------------------------------- Vehicle Lab drive preview
// Sample the torque curve (rpm → N·m) with the chosen interpolation (Vehicle Lab's Linear / Smooth / Ease / Step).
export function sampleCurve(points, x, mode = 'smooth') {
  if (!points.length) return 0;
  if (x <= points[0][0]) return points[0][1];
  const last = points[points.length - 1];
  if (x >= last[0]) return last[1];
  let i = 1;
  while (points[i][0] <= x) i++;
  const p0 = points[Math.max(0, i - 2)], p1 = points[i - 1], p2 = points[i], p3 = points[Math.min(points.length - 1, i + 1)];
  const t = (x - p1[0]) / (p2[0] - p1[0]);
  if (mode === 'step') return p1[1];
  if (mode === 'linear') return p1[1] + (p2[1] - p1[1]) * t;
  if (mode === 'ease') { const e = t * t * (3 - 2 * t); return p1[1] + (p2[1] - p1[1]) * e; }
  // Catmull–Rom through neighbours.
  const t2 = t * t, t3 = t2 * t;
  return 0.5 * ((2 * p1[1]) + (-p0[1] + p2[1]) * t + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3);
}

// One straight-line drive step: engine torque through the gearbox against drag and rolling resistance.
export function stepDrive(state, vm, dt) {
  const s = VEHICLE_SHEET;
  const ratio = s.gearRatios[state.gear] * vm.finalDrive;
  const wheelRpm = (state.speed / vm.tyreRadius) * 60 / (2 * Math.PI);
  state.rpm = Math.max(vm.idleRpm, Math.min(vm.redlineRpm, wheelRpm * ratio));
  const torque = state.throttle * sampleCurve(vm.torqueCurve, state.rpm, vm.interpolation) * (state.rpm >= vm.redlineRpm ? 0 : 1);
  const drive = (torque * ratio * s.driveEfficiency) / vm.tyreRadius;
  const drag = 0.5 * s.airDensity * vm.dragArea * state.speed * state.speed;
  const rolling = state.speed > 0.01 ? s.rollingResistance * vm.mass * 9.81 : 0;
  const brake = state.brake * (4 * s.maxBrakeTorquePerWheel) / vm.tyreRadius * (state.speed > 0.01 ? 1 : 0);
  state.acceleration = (drive - drag - rolling - brake) / vm.mass;
  state.speed = Math.max(0, state.speed + state.acceleration * dt);
  state.torque = torque;
  if (state.rpm > Math.min(s.upshiftRpm, vm.redlineRpm - 200) && state.gear < s.gearRatios.length - 1) state.gear++;
  else if (state.gear > 0 && state.rpm < 2600) {
    const down = (state.speed / vm.tyreRadius) * 60 / (2 * Math.PI) * s.gearRatios[state.gear - 1] * vm.finalDrive;
    if (down < s.upshiftRpm - 800) state.gear--;
  }
  state.wheelAngle += (state.speed / vm.tyreRadius) * dt;
  state.distance += state.speed * dt;
  return state;
}

export function createDriveState() {
  return { speed: 0, rpm: 700, gear: 0, torque: 0, acceleration: 0, throttle: 0.62, brake: 0, wheelAngle: 0, distance: 0 };
}
