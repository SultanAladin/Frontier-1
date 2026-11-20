// Boot: the compile context (workers + icon baker, used headlessly for the ControlVehicle sample), the asset library,
// the import paths (file picker, drag-and-drop, sample sources) and the Geometry Compiler hand-off. The compiler is a
// separate workspace (Experimental/GeometryCompiler/Compile.html) framed full-window; see CompileExchange.js.
import { $, toast } from '../Shared/Studio/Ui.js';
import { hydrateIcons } from '../Shared/Studio/Icons.js';
import { createAsset, compileAll, fmt } from '../Shared/GeometryCompile/CompileSteps.js';
import { readFacet } from '../Shared/GeometryCompile/Engine/ImportFormats.js';
import { createCompileContext } from '../Shared/GeometryCompile/CompileContext.js';
import { COMPILE_SAMPLES, assetFromFile, fetchModel } from '../Shared/GeometryCompile/CompileSources.js';
import { hostCompiler } from '../Shared/GeometryCompile/CompileExchange.js';
import { createBlueprint, addComponent, findComponent, VEHICLE_SHEET } from './Blueprint.js';
import { VEHICLE_PALETTE } from '../Shared/Studio/Render.js';
import { createComponentViewer } from './ComponentViewer.js';

const assets = new Map();
const { ctx, iconRenderer } = createCompileContext();

// ------------------------------------------------------------------------------------------------- import paths
// Sessions run in the Geometry Compiler page; it hands back a compiled copy of the asset (or a cancel).
const compiler = hostCompiler($('Compile'), {
  onFinish(asset, { cancelled, open, existing }) {
    if (cancelled) { viewer.refresh(); return; }
    assets.set(asset.id, asset);
    if (existing) { viewer.refresh(); toast(`${asset.name} recompiled · placed meshes updated`); return; }
    viewer.refresh();
    if (open) {
      const bp = createBlueprintFor(asset);
      viewer.open(bp);
      toast(`${asset.name} compiled · opened as ${bp.name}`);
    }
  },
  onError(error) { $('Compile').hidden = true; toast(`Geometry Compiler: ${error.message || error}`); },
});

function openCompile(asset, options = {}) {
  compiler.open(asset, options);
}

async function importFiles(files) {
  for (const file of files) {
    try {
      openCompile(await assetFromFile(file));
      return; // one compile session at a time; the rest can be imported after
    } catch (error) {
      toast(String(error.message || error));
    }
  }
}

$('FileInput').addEventListener('change', (e) => { importFiles(e.target.files); e.target.value = ''; });
addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
addEventListener('drop', (e) => { if (e.dataTransfer.files.length && !e.defaultPrevented) { e.preventDefault(); importFiles(e.dataTransfer.files); } });

// A blueprint wrapping one asset: root → Static Mesh (+ a collision component matching the compiled shape).
function createBlueprintFor(asset) {
  const bp = createBlueprint(`BP_${asset.name.replace(/[^\w]+/g, '')}`);
  const mesh = addComponent(bp, 'StaticMesh', { name: asset.name.replace(/[^\w]+/g, ''), props: { asset: asset.id } });
  const c = asset.out.collision;
  if (c && c.type === 'box') addComponent(bp, 'BoxCollision', { parentId: mesh.id, name: 'Bounds', props: { extent: c.half.map((v) => +v.toFixed(3)), location: c.centre.map((v) => +v.toFixed(3)) } });
  if (c && c.type === 'sphere') addComponent(bp, 'SphereCollision', { parentId: mesh.id, name: 'Bounds', props: { radius: +c.radius.toFixed(3), location: c.centre.map((v) => +v.toFixed(3)) } });
  if (c && c.type === 'capsule') addComponent(bp, 'CapsuleCollision', { parentId: mesh.id, name: 'Bounds', props: { radius: +c.radius.toFixed(3), halfHeight: +c.halfHeight.toFixed(3), location: c.centre.map((v) => +v.toFixed(3)), rotation: c.axis === 0 ? [0, 0, 90] : c.axis === 2 ? [90, 0, 0] : [0, 0, 0] } });
  const g = asset.out.geometry;
  const r = g ? g.bounds.radius : 1;
  addComponent(bp, 'PointLight', { name: 'KeyLight', props: { location: [r * 1.2, r * 1.6 + 0.4, r * 1.2], intensity: 4 + r * 4, range: r * 6 + 2 } });
  bp.dirty = true;
  return bp;
}
window.FrontierComponentViewer = { createBlueprintFor, assets };

// ------------------------------------------------------------------------------------------------------ samples
const samples = COMPILE_SAMPLES.map((s) => ({ ...s, async open() { openCompile(await s.source()); } }));

// ---------------------------------------------------------------------------------------------- vehicle sample
const VEHICLE_PARTS = [
  { key: 'body', file: 'ControlVehicleBody.facet', name: 'ControlVehicle Body', tags: 'vehicle, body, fct1', collision: 'box', normals: 'angle' },
  { key: 'tyre', file: 'ControlVehicleTyre.facet', name: 'ControlVehicle Tyre', tags: 'vehicle, wheel, fct1', collision: 'box', normals: 'smooth' },
  { key: 'rim', file: 'ControlVehicleRim.facet', name: 'ControlVehicle Rim', tags: 'vehicle, wheel, fct1', collision: 'none', normals: 'angle' },
  { key: 'wing', build: buildWing, name: 'Rear Wing', tags: 'vehicle, wing, aero', collision: 'box', normals: 'angle' },
];

// A rear wing built from boxes (Y-up, metres, origin at the mounting point; +X is the car's forward direction): carbon
// main plane with a gurney flap, body-colour endplates and blue pylons, using the Vehicle Lab material families.
//   Angle: 8° angle of attack for downforce — the leading edge (front, +X) sits LOW and the trailing edge (rear, −X)
//   HIGH, and the gurney flap stands up off the trailing edge.  (It was tilted the other way, which is a lift wing.)
//   Span: endplate outer faces line up with the body's sides (ControlVehicleBody is 2.4996 m wide, ±1.2498 m).
const WING = { aoa: 8, bodyHalfWidth: 1.2498, chordHalf: 0.17, planeHalfThick: 0.016, plateHalfThick: 0.008, planeY: 0.25 };
function buildWing() {
  const pos = [], idx = [], fam = [];
  const box = (c, h, family, tilt = 0) => {
    const base = pos.length / 3, cs = Math.cos(tilt), sn = Math.sin(tilt);
    for (let i = 0; i < 8; i++) {
      const x = (i & 1 ? 1 : -1) * h[0], y = (i & 2 ? 1 : -1) * h[1], z = (i & 4 ? 1 : -1) * h[2];
      pos.push(c[0] + x * cs - y * sn, c[1] + x * sn + y * cs, c[2] + z);
    }
    const quads = [[0, 2, 3, 1], [4, 5, 7, 6], [0, 1, 5, 4], [2, 6, 7, 3], [0, 4, 6, 2], [1, 3, 7, 5]];
    for (const [a, b, c2, d] of quads) { idx.push(base + a, base + b, base + c2, base + a, base + c2, base + d); fam.push(family, family); }
  };
  const { aoa, bodyHalfWidth, chordHalf, planeHalfThick, plateHalfThick, planeY } = WING;
  const tilt = -aoa * Math.PI / 180;                       // negative about Z: front edge down, rear edge up
  const plateZ = bodyHalfWidth - plateHalfThick;            // endplate centres, outer faces flush with the body sides
  const span = plateZ - plateHalfThick;                     // main plane meets the endplates' inner faces
  box([0, planeY, 0], [chordHalf, planeHalfThick, span], 8, tilt);
  // Gurney flap: a 44 mm lip standing on the trailing edge's top surface, square to the plane.
  const te = [-chordHalf * Math.cos(tilt) - planeHalfThick * Math.sin(tilt), planeY - chordHalf * Math.sin(tilt) + planeHalfThick * Math.cos(tilt)];
  box([te[0] + 0.008 * Math.cos(tilt) - 0.022 * Math.sin(tilt), te[1] + 0.008 * Math.sin(tilt) + 0.022 * Math.cos(tilt), 0], [0.008, 0.022, span], 6, tilt);
  for (const z of [-plateZ, plateZ]) box([-0.01, 0.23, z], [0.2, 0.1, plateHalfThick], 6);
  // Pylons spread with the wider span (about 45 % out from the centre line), so the plane is not cantilevered too far.
  for (const z of [-0.56, 0.56]) box([0.02, 0.11, z], [0.05, 0.11, 0.01], 10);
  return { positions: new Float32Array(pos), indices: new Uint32Array(idx), families: new Uint32Array(fam), format: 'Procedural', notes: ['Rear wing built in Main.js for the Dock socket demo'] };
}

async function loadVehicle(report) {
  const parts = {};
  let i = 0;
  for (const part of VEHICLE_PARTS) {
    report(`Reading ${part.file}`, i / VEHICLE_PARTS.length);
    const bytes = part.build ? null : await fetchModel(part.file);
    const raw = part.build ? part.build() : readFacet(bytes);
    const asset = createAsset({
      name: part.name, fileName: part.file || `${part.key}.procedural`, fileSize: bytes ? bytes.byteLength : 0, raw, origin: 'sample', palette: VEHICLE_PALETTE,
      settings: {
        source: { up: part.build ? 'y' : 'z' }, geometry: { centre: 'none', normals: part.normals }, collision: { type: part.collision },
        sdf: { resolution: 40 }, icon: { yaw: part.key === 'body' ? 40 : 60, pitch: 18 },
        meta: { name: part.name, category: 'Vehicle part', tags: part.tags, notes: part.build ? 'Procedural rear wing for the Dock socket demo.' : 'Vehicle Lab ControlVehicle source mesh (Frontier-OLD · VehicleProvingGrounds/Archive).' },
      },
    });
    assets.set(asset.id, asset);
    await compileAll(asset, ctx, (step, f) => report(`Compiling ${part.name} · ${step}`, (i + f * 0.9) / VEHICLE_PARTS.length));
    asset.compiledAt = Date.now();
    parts[part.key] = asset;
    i++;
  }
  return parts;
}

// ControlVehicle as a Blueprint class: Vehicle Lab's sockets become component transforms (authored Z-up → Y-up:
// x, y, z → x, z, −y), Vehicle Lab's sheets become the Vehicle Movement component.
function buildVehicleBlueprint(parts) {
  const bp = createBlueprint('BP_ControlVehicle', 'Pawn');
  const body = addComponent(bp, 'StaticMesh', { name: 'Body', props: { asset: parts.body.id, location: [0, +VEHICLE_SHEET.groundOffset.toFixed(5), 0], mass: 1220, simulatePhysics: true } });
  // Each wheel hangs from a Spring socket at its axle mount (meets road bumps, squats and lifts with acceleration);
  // the front ones turn on a Hinge socket (±32° steering lock) between the spring and the wheel.
  const names = ['FL', 'FR', 'RL', 'RR'];
  const wheels = VEHICLE_SHEET.axleMount.map((m, i) => {
    const spring = addComponent(bp, 'Socket', { parentId: body.id, name: `Suspension${names[i]}`, props: { kind: 'spring', location: [m[0], m[2], -m[1]], springHz: 2, damping: 0.42, springTravel: 0.09, bumpSpacing: 7, loadGain: 1 } });
    const mount = m[0] > 0 ? addComponent(bp, 'Socket', { parentId: spring.id, name: `Steer${names[i]}`, props: { kind: 'hinge', angleMin: -32, angleMax: 32, pose: 0.5 } }) : spring;
    const tyre = addComponent(bp, 'StaticMesh', { parentId: mount.id, name: `Wheel${names[i]}`, props: { asset: parts.tyre.id, rotation: [0, m[1] < 0 ? 180 : 0, 0], mass: 20 } });
    addComponent(bp, 'StaticMesh', { parentId: tyre.id, name: `Wheel${names[i]}Rim`, props: { asset: parts.rim.id } });
    return tyre;
  });
  addComponent(bp, 'SpotLight', { parentId: body.id, name: 'HeadlightL', props: { location: [2.95, 0.42, -0.78], rotation: [0, 0, -4], intensity: 30, angle: 28, range: 30 } });
  addComponent(bp, 'SpotLight', { parentId: body.id, name: 'HeadlightR', props: { location: [2.95, 0.42, 0.78], rotation: [0, 0, -4], intensity: 30, angle: 28, range: 30 } });
  // Vehicle Lab's rear wing socket, now a Dock keyed “wing”: Simulate runs a pull test (the wing tears off above
  // 1.2 kN, drifts out and snaps back inside 0.3 m). Release / Connect work in the editor too.
  const wingDock = addComponent(bp, 'Socket', { parentId: body.id, name: 'RearWingSocket', props: { kind: 'dock', location: [-2.9822, 0.8367, 0], key: 'wing', snapRadius: 0.3, breakForce: 1200, drive: 'pull' } });
  addComponent(bp, 'StaticMesh', { parentId: wingDock.id, name: 'RearWing', props: { asset: parts.wing.id, tags: 'wing', mass: 6 } });
  addComponent(bp, 'Socket', { parentId: body.id, name: 'TowStrap', props: { kind: 'tether', location: [-3.1, 0.1, 0], length: 0.32, swing: 30, tetherDamping: 0.3 } });
  addComponent(bp, 'Arrow', { parentId: body.id, name: 'Forward', props: { location: [3.2, 0.2, 0], length: 0.8 } });
  addComponent(bp, 'Camera', { name: 'ChaseCamera', props: { location: [-7.2, 2.4, 0], rotation: [0, 0, -12], fov: 62 } });
  addComponent(bp, 'BoxCollision', { parentId: body.id, name: 'FrontBumperTrigger', props: { location: [3.05, 0.1, 0], extent: [0.18, 0.28, 1.05], trigger: true } });
  const vm = addComponent(bp, 'VehicleMovement', { name: 'VehicleMovement' });
  vm.props.wheels = wheels.map((w) => w.id);
  bp.dirty = false;
  return bp;
}

// ---------------------------------------------------------------------------------------------------------- boot
const viewer = createComponentViewer({ assets, iconRenderer, openCompile, importFiles, samples });
hydrateIcons(document);

(async () => {
  const boot = $('Boot');
  const report = (text, f) => { $('BootText').textContent = text; $('BootBar').style.width = `${Math.round(f * 100)}%`; };
  try {
    const parts = await loadVehicle(report);
    report('Assembling BP_ControlVehicle', 1);
    const bp = buildVehicleBlueprint(parts);
    viewer.open(bp);
    bp.icon = null;
    boot.classList.add('done');
    setTimeout(() => boot.remove(), 400);
    const tris = Object.values(parts).reduce((n, a) => n + a.out.geometry.indices.length / 3, 0);
    toast(`ControlVehicle compiled · ${fmt(tris)} tris · 3 assets`);
  } catch (error) {
    console.error(error);
    $('BootText').textContent = `Could not load the ControlVehicle sample: ${error.message}. Import an object to start.`;
    boot.classList.add('failed');
    const bp = createBlueprint('BP_Empty');
    viewer.open(bp);
    setTimeout(() => boot.remove(), 2500);
  }
})();

export { assets, findComponent };
