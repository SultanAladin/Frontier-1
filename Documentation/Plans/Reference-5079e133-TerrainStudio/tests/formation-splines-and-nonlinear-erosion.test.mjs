import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

import * as erosionShaders from '../src/erosion/erosion-shaders.js';
import { FORMATION_PRESETS } from '../src/geology/presets.js';
import {
  getFormationSplineSpec,
  areSplinesAtDefault,
  cloneSplineCurves,
} from '../src/geology/formation-splines.js';
import { buildMacroFormation } from '../src/geology/formations.js';
import { createTectonicField } from '../src/geology/tectonics.js';
import { generateRockFormation } from '../src/geology/generator.js';
import { computeChemicalStains } from '../src/geology/chemical-staining.js';
import { createRng } from '../src/core/prng.js';

test('Terrain Studio shader removes fake Voronoi fracture-network crack overlay', () => {
  const allShaders = Object.values(erosionShaders).filter((v) => typeof v === 'string').join('\n');
  assert.ok(allShaders.length > 1000, 'Expected terrain shader source strings');
  assert.ok(!allShaders.includes('evalRockFractureNetwork'), 'evalRockFractureNetwork must be removed');
  assert.ok(!allShaders.includes('fractureNet'), 'fractureNet must be removed');
});

test('All 25 Rock Studio presets expose valid 3D spline rigs including 3-spline canyon rigs', () => {
  const presetIds = Object.keys(FORMATION_PRESETS);
  assert.equal(presetIds.length, 25);

  for (const id of presetIds) {
    const preset = FORMATION_PRESETS[id];
    const spec = getFormationSplineSpec(id, preset.params.scale);
    assert.ok(spec, `Missing spline spec for ${id}`);
    assert.ok(Array.isArray(spec.roles) && spec.roles.length >= 1, `Missing roles for ${id}`);
    assert.equal(spec.defaultCurves.length, spec.roles.length, `Curves/roles mismatch for ${id}`);
    for (const curve of spec.defaultCurves) {
      assert.ok(curve.length >= 2, `Spline in ${id} must have >= 2 control points`);
      for (const pt of curve) {
        assert.ok(Number.isFinite(pt.x) && Number.isFinite(pt.y) && Number.isFinite(pt.z));
      }
    }
    assert.equal(areSplinesAtDefault(id, preset.params.scale, cloneSplineCurves(spec.defaultCurves)), true);
  }

  for (const canyonId of ['slot-canyon', 'studlagil-colonnade']) {
    const spec = getFormationSplineSpec(canyonId, 16);
    assert.equal(spec.family, 'canyon');
    assert.equal(spec.roles.length, 3);
    assert.deepEqual(
      spec.roles.map((r) => r.label),
      ['Left Wall', 'Riverbed / Thalweg', 'Right Wall']
    );
  }
});

test('Stretching a spline dynamically scales span & populates more geological blocks along arc-length', () => {
  for (const archId of ['sedimentary-cliff', 'hoodoo-columns', 'slot-canyon']) {
    const scale = 16;
    const baseSpec = getFormationSplineSpec(archId, scale);
    const stretchedCurves = cloneSplineCurves(baseSpec.defaultCurves);
    for (const curve of stretchedCurves) {
      for (const pt of curve) {
        if (archId === 'slot-canyon') {
          pt.z *= 1.85;
        } else {
          pt.x *= 1.85;
        }
      }
    }

    const rngA = createRng(4029);
    const baseParams = { ...FORMATION_PRESETS[archId].params, archetype: archId, scale, seed: 4029 };
    const macroBase = buildMacroFormation(baseParams, createTectonicField(baseParams, rngA.fork('t')), rngA.fork('m'));

    const rngB = createRng(4029);
    const extParams = {
      ...baseParams,
      formationSplines: stretchedCurves,
    };
    const macroExt = buildMacroFormation(extParams, createTectonicField(extParams, rngB.fork('t')), rngB.fork('m'));

    assert.ok(
      macroExt.blocks.length > macroBase.blocks.length * 1.25,
      `${archId}: expected stretched spline to populate more blocks (${macroExt.blocks.length} vs ${macroBase.blocks.length})`
    );
  }
});

test('preserveSplineWorldSpace pins 3D spline edits in world coordinates without recentering drift', () => {
  const scale = 15;
  const spec = getFormationSplineSpec('saharan-yardang-fin', scale);
  const shiftedSplines = cloneSplineCurves(spec.defaultCurves);
  const worldShiftX = 9.5;
  const worldShiftZ = -6.0;
  for (const c of shiftedSplines) {
    for (const p of c) {
      p.x += worldShiftX;
      p.z += worldShiftZ;
    }
  }

  const pinned = generateRockFormation({
    archetype: 'saharan-yardang-fin',
    seed: 4029,
    scale,
    quality: 'draft',
    basePreviewOnly: true,
    preserveSplineWorldSpace: true,
    formationSplines: shiftedSplines,
  });

  const wb = pinned.stats.worldBounds;
  assert.ok(wb, 'Expected worldBounds in stats');
  const centerX = 0.5 * (wb.minX + wb.maxX);
  const centerZ = 0.5 * (wb.minZ + wb.maxZ);
  assert.ok(
    Math.abs(centerX - worldShiftX) < 3.0 && Math.abs(centerZ - worldShiftZ) < 3.0,
    `Expected world-pinned mesh near (${worldShiftX}, ${worldShiftZ}), got (${centerX.toFixed(2)}, ${centerZ.toFixed(2)})`
  );
  assert.equal(wb.minY, 0, 'Mesh must remain grounded at Y = 0');
});

test('Fast Base Preview skips SDF erosion, QEM & stains for interactive sculpting, while Bake runs full pipeline', () => {
  const common = {
    archetype: 'exfoliation-dome',
    seed: 4029,
    quality: 'fast',
    decimation: 0.55,
    sdfErosionSource: 'mesh',
    erosionSdfResolution: 128,
    rainErosionAmount: 0.55,
    windErosionAmount: 0.35,
    chemAmount: 0.8,
    preserveSplineWorldSpace: true,
  };

  const preview = generateRockFormation({ ...common, basePreviewOnly: true });
  assert.equal(preview.stats.basePreviewOnly, true);
  assert.equal(preview.stats.sdfErosion, null);
  assert.equal(preview.stats.meshToSdf, null);
  assert.equal(preview.stats.chemicalStaining, null);
  assert.equal(preview.mesh.stains, null);
  assert.ok(preview.mesh.indices.length / 3 > 1000);

  const baked = generateRockFormation({ ...common, basePreviewOnly: false });
  assert.equal(baked.stats.basePreviewOnly, false);
  assert.ok(baked.stats.meshToSdf, 'Expected Mesh -> SDF conversion on full bake');
  assert.ok(baked.stats.sdfErosion, 'Expected 3D SDF erosion on full bake');
  assert.ok(baked.stats.chemicalStaining, 'Expected chemical staining on full bake');
  assert.ok(baked.mesh.stains instanceof Float32Array);
});

test('Multi-neighbor slope-weighted chemical staining disperses laterally instead of collapsing to 1-column razor stripes', () => {
  // Build a regular 21 x 21 inclined rock wall grid and verify stain spreads across neighboring columns
  const nx = 21, ny = 21;
  const positions = new Float32Array(nx * ny * 3);
  const normals = new Float32Array(nx * ny * 3);
  const colors = new Float32Array(nx * ny * 4);
  const indices = [];

  for (let iy = 0; iy < ny; iy++) {
    for (let ix = 0; ix < nx; ix++) {
      const idx = iy * nx + ix;
      positions[idx * 3] = (ix - 10) * 0.5;
      positions[idx * 3 + 1] = iy * 0.5;
      positions[idx * 3 + 2] = -iy * 0.12;
      normals[idx * 3] = 0;
      normals[idx * 3 + 1] = 0.23;
      normals[idx * 3 + 2] = 0.97;
      colors[idx * 4] = 0.72;     // AO
      colors[idx * 4 + 1] = 0.50; // neutral curvature
      colors[idx * 4 + 2] = 0.50;
      colors[idx * 4 + 3] = 1.0;
      if (ix < nx - 1 && iy < ny - 1) {
        const a = idx, b = idx + 1, c = idx + nx, d = idx + nx + 1;
        indices.push(a, b, c, b, d, c);
      }
    }
  }

  const res = computeChemicalStains(
    { positions, normals, colors, indices: new Uint32Array(indices) },
    { amount: 0.85, iron: 0.8, copper: 0.3, seed: 4029 }
  );

  // Check bottom third of wall: no single column should contain >35% of row stain, and adjacent differences stay smooth
  let stainedCols = 0;
  let totalRowIron = 0;
  let maxColIron = 0;
  const iyCheck = 3;
  for (let ix = 1; ix < nx - 1; ix++) {
    const fe = res.stains[(iyCheck * nx + ix) * 2];
    totalRowIron += fe;
    if (fe > maxColIron) maxColIron = fe;
    if (fe > 0.02) stainedCols++;
  }
  assert.ok(stainedCols >= 8, `Expected broad multi-column stain dispersion, got ${stainedCols} columns`);
  assert.ok(maxColIron < totalRowIron * 0.32, 'Stain must disperse across neighbors rather than collapsing into a single razor stripe');
});

test('Rock Studio UI wires multi-spline toolbar, canyon role pills, fast base preview, and Bake controls', () => {
  const html = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  const mainJs = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');
  const gizmoJs = fs.readFileSync(new URL('../src/app/arch-curve-gizmo.js', import.meta.url), 'utf8');

  assert.ok(html.includes('id="rock-spline-toolbar"'));
  assert.ok(html.includes('id="rock-spline-role-pills"'));
  assert.ok(html.includes('id="chk-fast-base-preview"'));
  assert.ok(html.includes('id="btn-bake-formation"'));
  assert.ok(mainJs.includes('preserveSplineWorldSpace: true'));
  assert.ok(mainJs.includes('basePreviewOnly'));
  assert.ok(gizmoJs.includes('setSplineRig'));
});
