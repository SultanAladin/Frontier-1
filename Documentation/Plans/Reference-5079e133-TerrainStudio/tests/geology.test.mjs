import test from 'node:test';
import assert from 'node:assert/strict';

import { QefAccumulator } from '../src/core/qef.js';
import { createRng } from '../src/core/prng.js';
import { createBoxPoly, splitPolyByPlane, measurePoly } from '../src/geology/polyhedron.js';
import { FORMATION_PRESETS, resolveParams } from '../src/geology/presets.js';
import { createTectonicField } from '../src/geology/tectonics.js';
import { buildMacroFormation } from '../src/geology/formations.js';
import { evalCurvedArchRib } from '../src/geology/arch-curves.js';
import { generateRockFormation } from '../src/geology/generator.js';
import { exportFormationToGLB } from '../src/io/glb-exporter.js';
import { exportFormationToOBJ } from '../src/io/obj-exporter.js';

test('QEF solver snaps accurately onto sharp 90-degree dihedral rock crease', () => {
  const qef = new QefAccumulator();
  qef.add(0.35, 0.20, 0.30, 1, 0, 0);
  qef.add(0.35, 0.55, 0.70, 1, 0, 0);
  qef.add(0.15, 0.62, 0.30, 0, 1, 0);
  qef.add(0.30, 0.62, 0.70, 0, 1, 0);

  const outPos = [0, 0, 0];
  const outNorm = [0, 0, 0];
  const disp = qef.solve(0, 0, 0, 1, 1, 1, 0.005, outPos, outNorm);

  assert.ok(Math.abs(outPos[0] - 0.35) < 0.01, `expected x ~ 0.35, got ${outPos[0]}`);
  assert.ok(Math.abs(outPos[1] - 0.62) < 0.01, `expected y ~ 0.62, got ${outPos[1]}`);
  assert.ok(disp > 0.2, 'expected high normal dispersion on 90-deg crease');
});

test('Exact 3D convex polyhedron splitter conserves volume across arbitrary oblique joint plane', () => {
  const box = createBoxPoly(-2, 0, -1.5, 2, 3, 1.5);
  const mFull = measurePoly(box);
  assert.ok(Math.abs(mFull.volume - 36.0) < 1e-3);

  const [childA, childB] = splitPolyByPlane(box, [0.45, 0.75, -0.32], 0.4, 0.0);
  assert.ok(childA && childB);
  const mA = measurePoly(childA);
  const mB = measurePoly(childB);
  assert.ok(Math.abs((mA.volume + mB.volume) - 36.0) < 1e-2, 'split children volumes sum to parent volume');
});

test('Curve-based arches sweep analytical 3D Bézier ribs with ZERO hole-punching cuts', () => {
  for (const archKey of ['desert-arch', 'double-arch', 'sea-arch']) {
    const params = resolveParams({ archetype: archKey, seed: 4029 });
    const rng = createRng(params.seed);
    const tectonics = createTectonicField(params, rng);
    const macro = buildMacroFormation(params, tectonics, rng);

    assert.equal(macro.cuts.length, 0, `${archKey}: must use zero hole-punching cuts`);
    assert.ok(macro.blocks.length >= 60, `${archKey}: must construct boulder-from-start-to-finish assembly (got ${macro.blocks.length})`);
    assert.ok(Array.isArray(macro.curves) && macro.curves.length >= 1, `${archKey}: must construct 3D curve rib(s)`);
    if (archKey === 'double-arch') {
      assert.equal(macro.curves.length, 2, 'double-arch must sweep two distinct 3D Bézier arch ribs');
    }

    const rib = macro.curves[0];
    // Crown midpoint of rib should be inside solid rock (d < 0), while portal space below crown should be open air (d > 0)
    const midIdx = Math.floor(rib.M / 2);
    const crownX = rib.px[midIdx];
    const crownY = rib.py[midIdx];
    const crownZ = rib.pz[midIdx];
    assert.ok(evalCurvedArchRib(rib, crownX, crownY, crownZ) < -0.1, `${archKey}: arch crown core is solid rock`);
    assert.ok(evalCurvedArchRib(rib, crownX, crownY * 0.45, crownZ) > 0.5, `${archKey}: space beneath arch curve is open portal air`);
  }
});

test('All 25 geological archetypes synthesize grounded, valid, watertight-grade AAA 3D meshes & isolate cracks to cracked-monolith', () => {
  const keys = Object.keys(FORMATION_PRESETS);
  assert.equal(keys.length, 25);
  const expectedNew = [
    'desert-arch',
    'double-arch',
    'sea-arch',
    'sedimentary-cliff',
    'slot-canyon',
    'hoodoo-columns',
    'balanced-rock',
    'limestone-karst',
    'exfoliation-dome',
    'cracked-monolith',
    'arch-encampment',
    'grand-canyon-temple',
    'studlagil-colonnade',
    'icelandic-tuya-crag',
    'saharan-yardang-fin',
    'calved-cliff-boulders',
  ];
  for (const k of expectedNew) {
    assert.ok(keys.includes(k), `missing formation preset ${k}`);
  }

  for (const key of keys) {
    // Verify strict crack isolation: ONLY `cracked-monolith` defines `crackNetwork`
    const mParams = resolveParams({ archetype: key, seed: 4029 });
    const mRng = createRng(mParams.seed);
    const mTec = createTectonicField(mParams, mRng);
    const macro = buildMacroFormation(mParams, mTec, mRng);
    if (key === 'cracked-monolith') {
      assert.ok(macro.crackNetwork, 'cracked-monolith must define isolated crackNetwork');
      assert.equal(macro.cuts.length, 0, 'cracked-monolith must use splinter-free polyhedral fracture splitting (zero void cuts)');
      assert.ok(macro.blocks.length >= 25, 'cracked-monolith must produce >= 25 substantial cleaved blocks & chockstones');
      assert.ok(macro.crackNetwork.throughSplitCount >= 7, 'cracked-monolith must register >= 7 through-split fracture planes');
    } else {
      assert.equal(macro.crackNetwork, undefined, `${key}: must NOT have crackNetwork (cracks isolated to cracked-monolith)`);
    }

    const res = generateRockFormation({ archetype: key, seed: 4029, quality: 'fast' });
    const { positions, normals, uvs, colors, albedos, hasCustomAlbedo, indices } = res.mesh;

    if (key === 'sedimentary-cliff' || key === 'cracked-monolith' || key === 'hoodoo-columns') {
      assert.equal(hasCustomAlbedo, true, `${key}: must emit multilayered per-vertex albedos`);
      assert.equal(albedos.length, positions.length, `${key}: albedos length matches positions`);
    } else {
      assert.equal(hasCustomAlbedo, false, `${key}: retains clean flat lithology material by default`);
    }

    assert.ok(res.stats.triangles > 1500, `${key}: expected > 1500 triangles, got ${res.stats.triangles}`);
    assert.equal(positions.length % 3, 0);
    assert.equal(normals.length, positions.length);
    assert.equal((uvs.length / 2) * 3, positions.length);
    assert.equal((colors.length / 4) * 3, positions.length);
    assert.equal(indices.length % 3, 0);

    let minY = Infinity;
    for (let i = 0; i < positions.length; i += 3) {
      assert.ok(Number.isFinite(positions[i]) && Number.isFinite(positions[i + 1]) && Number.isFinite(positions[i + 2]));
      if (positions[i + 1] < minY) minY = positions[i + 1];

      const nl = Math.hypot(normals[i], normals[i + 1], normals[i + 2]);
      assert.ok(Math.abs(nl - 1.0) < 1e-2, `${key}: non-unit normal ${nl}`);
    }
    assert.ok(Math.abs(minY) < 1e-4, `${key}: expected grounded minY == 0, got ${minY}`);

    for (let i = 0; i < colors.length; i++) {
      assert.ok(colors[i] >= 0 && colors[i] <= 1.0001, `${key}: invalid COLOR_0 channel ${colors[i]}`);
    }
  }
});

test('Seed determinism and genuine procedural diversity across seeds', () => {
  const a1 = generateRockFormation({ archetype: 'sedimentary-cliff', seed: 111, quality: 'fast' });
  const a2 = generateRockFormation({ archetype: 'sedimentary-cliff', seed: 111, quality: 'fast' });
  const b1 = generateRockFormation({ archetype: 'sedimentary-cliff', seed: 999, quality: 'fast' });

  assert.equal(a1.stats.triangles, a2.stats.triangles);
  assert.deepEqual(a1.stats.dimensions, a2.stats.dimensions);
  assert.notEqual(a1.stats.triangles, b1.stats.triangles);
});

test('QEM adaptive decimation reduces polycount cleanly and GLB/OBJ exporters produce valid binaries', () => {
  const full = generateRockFormation({ archetype: 'hero-boulder', seed: 42, quality: 'fast', decimation: 1.0 });
  const lod1 = generateRockFormation({ archetype: 'hero-boulder', seed: 42, quality: 'fast', decimation: 0.25 });

  assert.ok(lod1.stats.triangles < full.stats.triangles * 0.45, 'QEM decimation reduced triangle count');
  assert.ok(lod1.stats.triangles > 100);

  const glb = exportFormationToGLB(lod1);
  const view = new DataView(glb.buffer, glb.byteOffset, glb.byteLength);
  assert.equal(view.getUint32(0, true), 0x46546c67, 'GLB magic == glTF');
  assert.equal(view.getUint32(4, true), 2, 'GLB version == 2');
  assert.equal(view.getUint32(8, true), glb.byteLength, 'GLB total byteLength matches header');
  assert.equal(glb.byteLength % 4, 0, 'GLB 4-byte aligned');

  const jsonLen = view.getUint32(12, true);
  const jsonText = new TextDecoder().decode(glb.subarray(20, 20 + jsonLen));
  const parsed = JSON.parse(jsonText);
  assert.equal(parsed.asset.version, '2.0');
  assert.equal(parsed.meshes[0].primitives[0].attributes.COLOR_0, 3);

  const obj = exportFormationToOBJ(lod1);
  assert.ok(obj.includes('\nv ') && obj.includes('\nvn ') && obj.includes('\nf '));
});
