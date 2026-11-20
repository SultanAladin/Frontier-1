import { readFileSync } from 'node:fs';
import * as THREE from '../Shared/Studio/Vendor/three.esm.min.js';
import { readFacet } from '../Shared/GeometryCompile/Engine/ImportFormats.js';
import { icon } from './LevelIcons.js';
import { PROP_CATALOG, buildPropObject } from './ShowroomProps.js';
import { ENVIRONMENT_PRESETS, kelvinToColor } from './ShowroomEnvironment.js';
import { stoneSlabTextures, epoxyGarageTextures, plasterWallTexture, timberSlatTexture } from './ProceduralTextures.js';

function assert(cond, msg) {
  if (!cond) throw new Error(`Validation failed: ${msg}`);
}

// 1. Validate index.html element IDs expected by LevelDesigner.js & Main.js
const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const js = readFileSync(new URL('./LevelDesigner.js', import.meta.url), 'utf8');
const mainJs = readFileSync(new URL('./Main.js', import.meta.url), 'utf8');

const idUses = new Set([...js.matchAll(/\$\('([^']+)'\)/g), ...mainJs.matchAll(/\$\('([^']+)'\)/g)].map((m) => m[1]));
for (const id of idUses) {
  assert(html.includes(`id="${id}"`), `Missing DOM id="${id}" in Experimental/LevelDesigner/index.html`);
}
assert(!js.includes('../ComponentViewer/Blueprint.js'), 'LevelDesigner.js must not import non-local ComponentViewer path');

// 2. Validate ControlVehicle .facet assets load cleanly and have expected material families
const bodyFacet = readFacet(readFileSync(new URL('../Shared/Models/ControlVehicleBody.facet', import.meta.url)).buffer);
const rimFacet = readFacet(readFileSync(new URL('../Shared/Models/ControlVehicleRim.facet', import.meta.url)).buffer);
const tyreFacet = readFacet(readFileSync(new URL('../Shared/Models/ControlVehicleTyre.facet', import.meta.url)).buffer);
assert(bodyFacet.indices.length > 0 && rimFacet.indices.length > 0 && tyreFacet.indices.length > 0, 'ControlVehicle facet meshes empty');

// 3. Validate ProceduralTextures (Node headless fallback)
assert(stoneSlabTextures('#b8c1cb') instanceof THREE.CanvasTexture || typeof stoneSlabTextures('#b8c1cb').map === 'object', 'stoneSlabTextures ok');
assert(typeof epoxyGarageTextures().map === 'object', 'epoxyGarageTextures ok');
assert(typeof plasterWallTexture('#d8dee5') === 'object', 'plasterWallTexture ok');
assert(typeof timberSlatTexture() === 'object', 'timberSlatTexture ok');

// 4. Validate PROP_CATALOG builds cleanly with multi-part non-crude geometry & proper luminaires
assert(PROP_CATALOG.length >= 18, 'PROP_CATALOG should expose rich multi-part showroom, garage, nature & luminaire options');
for (const entry of PROP_CATALOG) {
  assert(icon(entry.icon).includes('<svg'), `Valid icon ref for ${entry.id} (${entry.icon})`);
  const node = buildPropObject({
    id: `test_${entry.id}`,
    kind: entry.id,
    props: {
      location: [0, 0, 0],
      rotation: [0, 0, 0],
      scale: [1, 1, 1],
      enabled: true,
      intensity: 2,
      lightColor: '#ffffff',
      angle: 45,
      range: 15,
    },
  }, { editorHelpers: true, selected: false });
  assert(node instanceof THREE.Group, `Prop ${entry.id} did not return THREE.Group`);
  let meshCount = 0;
  node.traverse((c) => { if (c.isMesh) meshCount++; });
  assert(meshCount >= 2, `Prop ${entry.id} should be multi-part architectural/luminaire geometry (got ${meshCount} meshes)`);
}

// 5. Validate all 5 Lobby environment presets and Kelvin color mapping
for (const k of ['minimalGallery', 'glassPavilion', 'garageBay', 'studioSet', 'horizonTerrace']) {
  assert(ENVIRONMENT_PRESETS[k], `Missing preset ${k}`);
}
const c5600 = kelvinToColor(5600);
assert(c5600.r > 0.9 && c5600.g > 0.9 && c5600.b > 0.85, '5600K sun color should be neutral daylight');

console.log('LEVEL_DESIGNER_VALIDATION_OK', {
  htmlIds: idUses.size,
  propCatalog: PROP_CATALOG.length,
  envPresets: Object.keys(ENVIRONMENT_PRESETS).length,
});
