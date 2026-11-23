// Where compile sessions come from, shared by both workspaces: a dropped / picked file, a Frontier-1 procedural preset
// or the ControlVehicle FCT1 body. Each returns a fresh asset (createAsset) ready for the Geometry Compiler.
import { createAsset } from './CompileSteps.js';
import { readGeometry, formatOf, readFacet } from './Engine/ImportFormats.js';
import { SOURCE_PRESETS, constructSource } from './Engine/SourceGeometry.js';
import { VEHICLE_PALETTE } from '../Studio/Render.js';

export const MODELS = new URL('../Models/', import.meta.url);

// "front_wing-v2.obj" → "Front Wing V2"
export function displayName(fileName) {
  return fileName.replace(/\.[^.]+$/, '').replace(/[_-]+/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

// Throws for unknown types or unreadable files; the caller toasts the message.
export async function assetFromFile(file) {
  const fmtInfo = formatOf(file.name);
  if (!fmtInfo) throw new Error(`Unknown file type: ${file.name}`);
  const raw = readGeometry(file.name, await file.arrayBuffer());
  const settings = fmtInfo.ext === 'facet' ? { source: { up: 'z' } } : {};
  return createAsset({ name: displayName(file.name), fileName: file.name, fileSize: file.size, raw, settings, palette: fmtInfo.ext === 'facet' ? VEHICLE_PALETTE : raw.palette || null });
}

export async function fetchModel(file) {
  return (await fetch(new URL(file, MODELS))).arrayBuffer();
}

// Sample sources listed in the Component Viewer's Content tab and on the Geometry Compiler's start screen.
export const COMPILE_SAMPLES = [
  ...SOURCE_PRESETS.map((p) => ({
    id: p.id, label: p.label, note: `${p.note} · Frontier-1 preset`, icon: p.id === 'terrain' ? 'grid' : 'sparkle',
    async source() {
      const src = constructSource(p.id, 24000);
      const raw = { positions: src.positions, indices: src.indices, families: null, format: 'Procedural', notes: [`Frontier-1 Cluster LOD preset “${p.label}” (24k triangle budget)`] };
      return createAsset({ name: p.label, fileName: `${p.id}.preset`, raw, settings: { meta: { category: 'Prop', tags: 'preset, frontier-1' } } });
    },
  })),
  {
    id: 'control-vehicle-body', label: 'ControlVehicle Body (FCT1)', note: 'Vehicle Lab mesh · Z-up · re-import', icon: 'vehicle',
    async source() {
      const raw = readFacet(await fetchModel('ControlVehicleBody.facet'));
      return createAsset({ name: 'ControlVehicle Body Copy', fileName: 'ControlVehicleBody.facet', fileSize: 64736, raw, palette: VEHICLE_PALETTE, settings: { source: { up: 'z' }, geometry: { centre: 'none' }, meta: { category: 'Vehicle part' } } });
    },
  },
];
