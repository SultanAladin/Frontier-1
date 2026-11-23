// Bootstraps ControlVehicle FCT1 parts, procedural rear wing, Geometry Compiler hand-off, and Level Designer Lobby.
import { $, toast } from '../Shared/Studio/Ui.js';
import { hydrateLevelIcons } from './LevelIcons.js';
import { createAsset, compileAll, fmt } from '../Shared/GeometryCompile/CompileSteps.js';
import { readFacet } from '../Shared/GeometryCompile/Engine/ImportFormats.js';
import { createCompileContext } from '../Shared/GeometryCompile/CompileContext.js';
import { COMPILE_SAMPLES, assetFromFile, fetchModel } from '../Shared/GeometryCompile/CompileSources.js';
import { hostCompiler } from '../Shared/GeometryCompile/CompileExchange.js';
import { VEHICLE_PALETTE } from '../Shared/Studio/Render.js';
import { createLevelDesigner } from './LevelDesigner.js';

const assets = new Map();
const { ctx, iconRenderer } = createCompileContext();
let designer = null;

// Geometry Compiler hand-off for importing custom 3D meshes into Lobby
const compiler = hostCompiler($('Compile'), {
  onFinish(asset, { cancelled, open, existing }) {
    if (cancelled) { designer?.refresh(); return; }
    assets.set(asset.id, asset);
    if (existing) {
      designer?.refresh();
      toast(`${asset.name} recompiled · Lobby meshes updated`);
      return;
    }
    designer?.refresh();
    if (open && designer) {
      designer.placeAsset(asset);
    }
  },
  onError(error) {
    $('Compile').hidden = true;
    toast(`Geometry Compiler: ${error.message || error}`);
  },
});

function openCompile(asset, options = {}) {
  compiler.open(asset, options);
}

async function importFiles(files) {
  for (const file of files) {
    try {
      openCompile(await assetFromFile(file));
      return;
    } catch (error) {
      toast(String(error.message || error));
    }
  }
}

$('FileInput').addEventListener('change', (e) => { importFiles(e.target.files); e.target.value = ''; });
addEventListener('dragover', (e) => { if ([...e.dataTransfer.types].includes('Files')) e.preventDefault(); });
addEventListener('drop', (e) => {
  if (e.dataTransfer.files.length && !e.defaultPrevented) {
    e.preventDefault();
    importFiles(e.dataTransfer.files);
  }
});

// Procedural rear aero wing matching Component Viewer Main.js
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
    for (const [a, b, c2, d] of quads) {
      idx.push(base + a, base + b, base + c2, base + a, base + c2, base + d);
      fam.push(family, family);
    }
  };
  const { aoa, bodyHalfWidth, chordHalf, planeHalfThick, plateHalfThick, planeY } = WING;
  const tilt = -aoa * Math.PI / 180;
  const plateZ = bodyHalfWidth - plateHalfThick;
  const span = plateZ - plateHalfThick;
  box([0, planeY, 0], [chordHalf, planeHalfThick, span], 8, tilt);
  const te = [-chordHalf * Math.cos(tilt) - planeHalfThick * Math.sin(tilt), planeY - chordHalf * Math.sin(tilt) + planeHalfThick * Math.cos(tilt)];
  box([te[0] + 0.008 * Math.cos(tilt) - 0.022 * Math.sin(tilt), te[1] + 0.008 * Math.sin(tilt) + 0.022 * Math.cos(tilt), 0], [0.008, 0.022, span], 6, tilt);
  for (const z of [-plateZ, plateZ]) box([-0.01, 0.23, z], [0.2, 0.1, plateHalfThick], 6);
  for (const z of [-0.56, 0.56]) box([0.02, 0.11, z], [0.05, 0.11, 0.01], 10);
  return { positions: new Float32Array(pos), indices: new Uint32Array(idx), families: new Uint32Array(fam), format: 'Procedural', notes: ['Rear wing for Lobby ControlVehicle'] };
}

const VEHICLE_PARTS = [
  { key: 'body', file: 'ControlVehicleBody.facet', name: 'ControlVehicle Body', normals: 'angle' },
  { key: 'tyre', file: 'ControlVehicleTyre.facet', name: 'ControlVehicle Tyre', normals: 'smooth' },
  { key: 'rim', file: 'ControlVehicleRim.facet', name: 'ControlVehicle Rim', normals: 'angle' },
  { key: 'wing', build: buildWing, name: 'ControlVehicle Wing', normals: 'angle' },
];

async function loadVehicleParts(report) {
  const parts = {};
  let i = 0;
  for (const part of VEHICLE_PARTS) {
    report(`Loading ${part.file || part.name}`, i / VEHICLE_PARTS.length);
    const bytes = part.build ? null : await fetchModel(part.file);
    const raw = part.build ? part.build() : readFacet(bytes);
    const asset = createAsset({
      name: part.name,
      fileName: part.file || `${part.key}.procedural`,
      fileSize: bytes ? bytes.byteLength : 0,
      raw,
      origin: 'sample',
      palette: VEHICLE_PALETTE,
      settings: {
        source: { up: part.build ? 'y' : 'z' },
        geometry: { centre: 'none', normals: part.normals },
        uv: { enabled: false },
        sdf: { enabled: false },
        lod: { enabled: false },
        collision: { enabled: false, type: 'none' },
        icon: { yaw: part.key === 'body' ? 40 : 60, pitch: 18 },
        meta: { name: part.name, category: 'Vehicle part', tags: 'vehicle, lobby' },
      },
    });
    assets.set(asset.id, asset);
    await compileAll(asset, ctx, (step, f) => report(`Preparing ${part.name} · ${step}`, (i + f * 0.9) / VEHICLE_PARTS.length));
    asset.compiledAt = Date.now();
    parts[part.key] = asset;
    i++;
  }
  return parts;
}

const samples = COMPILE_SAMPLES.map((s) => ({ ...s, async open() { openCompile(await s.source()); } }));
hydrateLevelIcons(document);

(async () => {
  const boot = $('Boot');
  const report = (text, f) => {
    $('BootText').textContent = text;
    $('BootBar').style.width = `${Math.round(f * 100)}%`;
  };
  try {
    const vehicleParts = await loadVehicleParts(report);
    report('Staging Lobby Showroom', 1);
    designer = createLevelDesigner({ vehicleParts, assets, openCompile, importFiles, samples, iconRenderer });
    boot.classList.add('done');
    setTimeout(() => boot.remove(), 400);
    const tris = Object.values(vehicleParts).reduce((n, a) => n + a.out.geometry.indices.length / 3, 0);
    toast(`Lobby Showroom ready · ControlVehicle ${fmt(tris)} tris`);
  } catch (err) {
    console.error(err);
    $('BootText').textContent = `Could not load ControlVehicle: ${err.message}`;
    boot.classList.add('failed');
  }
})();
