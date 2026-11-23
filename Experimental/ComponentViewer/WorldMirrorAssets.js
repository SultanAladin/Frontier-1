// Model catalog and analytical prop parts from Frontier World Mirror. Meshes are tessellated on demand,
// rather than seeding the scene with presets. Materials/analytic shading are approximate in this viewer.
import { WORLD_PLACEABLE_MODELS } from '../FrontierWorldMirror/WorldDragDrop.js';
import { ExpandPropPrimitives } from '../FrontierWorldMirror/WorldPropLibrary.js';
import { THREE } from '../Shared/Studio/Render.js';
import { createAsset, runStep } from '../Shared/GeometryCompile/CompileSteps.js';

export const worldModels = WORLD_PLACEABLE_MODELS;
export async function makeWorldModel(id, ctx) {
  const model = worldModels.find((m) => m.Id === id);
  if (!model) throw new Error('Unknown World Mirror model');
  const parts = model.PropId
    ? ExpandPropPrimitives({ Id: model.Id, Name: model.Name, PropId: model.PropId }, { Position: [0, 0, 0] })
    : [{ Shape: ({ box: 0, sphere: 1, cylinder: 2, cone: 3, torus: 4, 'cylinder-cone': 5 })[model.PrimitiveShape], Center: [0, 0, 0], RotationRad: [0, 0, 0], HalfExtents: model.Scale.map((n) => n / 2), ResolvedMaterial: model.BuildMaterial?.() }];
  const positions = [], indices = [], families = [], palette = [];
  for (const [family, part] of parts.entries()) {
    const [x, y, z] = part.HalfExtents;
    let geometry;
    if (part.Shape === 1) { geometry = new THREE.SphereGeometry(1, 20, 12); geometry.scale(x, y, z); }
    else if (part.Shape === 4) { geometry = new THREE.TorusGeometry(Math.max(x - z, z), z, 8, 24); geometry.scale(1, y / x, 1); }
    else if (part.Shape >= 2) {
      geometry = new THREE.CylinderGeometry(part.Shape === 3 ? x * 0.13 : part.Shape === 5 ? x * 0.37 : x, part.Shape === 3 ? x * 0.72 : part.Shape === 5 ? x * 0.5 : x, z * 2, 20);
      geometry.rotateX(Math.PI / 2); geometry.scale(1, y / x, 1);
    } else geometry = new THREE.BoxGeometry(x * 2, y * 2, z * 2);
    geometry.applyMatrix4(new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(...part.RotationRad, 'XYZ')));
    geometry.translate(...part.Center);
    const p = geometry.getAttribute('position');
    const base = positions.length / 3;
    // World Mirror is Z-up; the component viewer is Y-up.
    for (let i = 0; i < p.count; i++) positions.push(p.getX(i), p.getZ(i), -p.getY(i));
    const index = geometry.index;
    const count = index ? index.count : p.count;
    for (let i = 0; i < count; i++) indices.push(base + (index ? index.getX(i) : i));
    for (let i = 0; i < count / 3; i++) families.push(family);
    const channels = part.ResolvedMaterial?.Channels || {};
    const color = new THREE.Color(channels.colour?.Fill || '#bcbcbc');
    palette.push({ name: `Part ${family + 1}`, albedo: [color.r, color.g, color.b], roughness: Number(channels.roughness?.Fill ?? 0.5), metalness: Number(channels.metallic?.Fill ?? 0) });
    geometry.dispose();
  }
  if (!indices.length) throw new Error(`${model.Name} has no geometry`);
  const asset = createAsset({ name: model.Name, fileName: `${model.Id}.worldmodel`, origin: 'world-mirror', palette,
    raw: { positions: new Float32Array(positions), indices: new Uint32Array(indices), families: new Uint32Array(families), format: 'World Mirror model', notes: ['Tessellated from Frontier World Mirror analytical model'] },
    settings: { geometry: { centre: 'none' } } });
  await runStep(asset, 'geometry', ctx);
  return asset;
}
