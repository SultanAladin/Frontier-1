import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

import { createRng } from '../src/core/prng.js';
import { buildMacroFormation } from '../src/geology/formations.js';
import { generateRockFormation } from '../src/geology/generator.js';
import { resolveParams } from '../src/geology/presets.js';
import { erodeSdfWithSlotFlood } from '../src/geology/sdf-erosion-suite.js';
import { createGeologicalSdfEvaluator } from '../src/geology/surface-detail.js';
import { createTectonicField } from '../src/geology/tectonics.js';

function boxSdf(cx, cy, cz, hx, hy, hz) {
  return (x, y, z) => {
    const qx = Math.abs(x - cx) - hx;
    const qy = Math.abs(y - cy) - hy;
    const qz = Math.abs(z - cz) - hz;
    return Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) +
      Math.min(Math.max(qx, Math.max(qy, qz)), 0);
  };
}

function makeSyntheticSlotSdf() {
  const solids = [
    boxSdf(0.6, 1.5, 3, 0.6, 1.5, 3),       // left fin:  x 0.0..1.2
    boxSdf(3.4, 1.5, 3, 0.6, 1.5, 3),       // right fin: x 2.8..4.0
    boxSdf(2, 0.225, 3, 2, 0.225, 3),       // bedrock floor: y 0..0.45
    boxSdf(1.375, 1.25, 2.5, 0.175, 0.75, 0.5), // left upstream-facing rib at z=2
    boxSdf(2.625, 1.25, 3.5, 0.175, 0.75, 0.5), // right upstream-facing rib at z=3
  ];
  const sdfFn = (x, y, z) => Math.min(...solids.map((f) => f(x, y, z)));
  const nx = 80, ny = 60, nz = 120, h = 0.05;
  const gx = nx + 1, gy = ny + 1, gz = nz + 1;
  const strideY = gx, strideZ = gx * gy;
  const values = new Float32Array(gx * gy * gz);
  for (let iz = 0; iz < gz; iz++) {
    for (let iy = 0; iy < gy; iy++) {
      for (let ix = 0; ix < gx; ix++) {
        values[ix + iy * strideY + iz * strideZ] = sdfFn(ix * h, iy * h, iz * h);
      }
    }
  }
  return {
    nx, ny, nz, gx, gy, gz, strideY, strideZ,
    originX: 0, originY: 0, originZ: 0,
    voxelSize: h,
    values,
    ensureSample: (ix, iy, iz) => values[ix + iy * strideY + iz * strideZ],
  };
}

function meanSurfacePhiDelta(sdf, before, predicate) {
  let sum = 0;
  let n = 0;
  for (let iz = 0; iz < sdf.gz; iz++) {
    for (let iy = 0; iy < sdf.gy; iy++) {
      for (let ix = 0; ix < sdf.gx; ix++) {
        const idx = ix + iy * sdf.strideY + iz * sdf.strideZ;
        const f0 = before[idx];
        if (Math.abs(f0) > sdf.voxelSize * 0.8) continue;
        const x = ix * sdf.voxelSize;
        const y = iy * sdf.voxelSize;
        const z = iz * sdf.voxelSize;
        if (!predicate(x, y, z)) continue;
        sum += sdf.values[idx] - f0;
        n++;
      }
    }
  }
  assert.ok(n > 100, 'surface sample region must be populated');
  return sum / n;
}

function buildSlotMacro(seed = 4029) {
  const params = resolveParams({ archetype: 'slot-canyon', seed, quality: 'fast' });
  const rng = createRng(params.seed);
  const tectonics = createTectonicField(params, rng.fork('tectonics'));
  const macro = buildMacroFormation(params, tectonics, rng.fork('macro'));
  return { params, rng, macro };
}

test('slot flash flood is confined to the corridor and preferentially abrades inner walls, ribs and bed', () => {
  const sdf = makeSyntheticSlotSdf();
  const before = Float32Array.from(sdf.values);
  const samples = [];
  for (let z = 0; z <= 6.001; z += 0.5) {
    samples.push({ z, x: 2, floorY: 0.45, halfGap: 0.8, crestY: 3 });
  }

  const stats = erodeSdfWithSlotFlood(sdf, {
    amount: 0.85,
    windAmount: 0.30,
    particleCount: 700,
    windPolishParticles: 140,
    sedimentSupply: 0.62,
    slotCanyon: { flowDirection: 1, groundY: 0, height: 3, samples },
    edgeProtection: 0.65,
    groundY: 0,
    rng: createRng(1234),
    seedSalt: 99,
  });

  assert.equal(stats.floodParticles, 700);
  assert.equal(stats.windParticles, 140);
  assert.ok(stats.steps > 50000, `flow packets should traverse the slot (${stats.steps} steps)`);
  assert.ok(stats.wallEvents > 5000, `wall impacts should dominate (${stats.wallEvents})`);
  assert.ok(stats.floorEvents > 3000, `bed impacts should be recorded (${stats.floorEvents})`);
  assert.ok(stats.erodedVolume > 0.005, `measurable abrasion (${stats.erodedVolume} m3)`);
  assert.ok(sdf.values.every(Number.isFinite), 'SDF remains finite');

  const inner = meanSurfacePhiDelta(sdf, before, (x, y, z) =>
    ((x > 1.0 && x < 1.7) || (x > 2.3 && x < 3.0)) && y > 0.6 && y < 2.4 && z > 0.5 && z < 5.5);
  const outer = meanSurfacePhiDelta(sdf, before, (x, y, z) =>
    (x < 0.25 || x > 3.75) && y > 0.6 && y < 2.4 && z > 0.5 && z < 5.5);
  const floor = meanSurfacePhiDelta(sdf, before, (x, y, z) =>
    x > 1.3 && x < 2.7 && y > 0.28 && y < 0.62 && z > 0.5 && z < 5.5);
  const ribUpstream = meanSurfacePhiDelta(sdf, before, (x, y, z) =>
    (((x > 1.15 && x < 1.6 && z > 1.85 && z < 2.15) ||
      (x > 2.4 && x < 2.85 && z > 2.85 && z < 3.15)) && y > 0.6 && y < 1.9));

  assert.ok(inner > 0.00005, `inner walls recede (mean phi delta ${inner})`);
  assert.ok(floor > 0.00003, `bedrock floor is abraded (mean phi delta ${floor})`);
  assert.ok(Math.abs(outer) < 0.00002, `confined flood does not erode outer faces (${outer})`);
  assert.ok(inner > outer + 0.00005, 'inner-wall abrasion exceeds outer-wall change');
  assert.ok(ribUpstream > inner * 1.5,
    `upstream-facing ribs abrade faster than average inner wall (${ribUpstream} vs ${inner})`);

  for (const z of [1, 2, 3, 4, 5]) {
    const idx = Math.round(2 / sdf.voxelSize) +
      Math.round(1.2 / sdf.voxelSize) * sdf.strideY +
      Math.round(z / sdf.voxelSize) * sdf.strideZ;
    assert.ok(sdf.values[idx] > 0.25, `corridor centre remains open at z=${z}`);
  }
});

test('slot builder returns a descending sculptable floor, world-space flow path and hard-union wall blocks', () => {
  const { params, rng, macro } = buildSlotMacro();
  assert.ok(macro.slotCanyon, 'slot flow metadata is returned');
  assert.equal(macro.slotCanyon.axis, 'z');
  assert.equal(macro.slotCanyon.flowDirection, 1);
  assert.ok(macro.slotCanyon.samples.length >= 40);
  assert.ok(macro.blocks.length >= 350, `rich wall/floor construction (${macro.blocks.length} blocks)`);

  const samples = macro.slotCanyon.samples;
  assert.ok(samples[0].floorY > samples.at(-1).floorY + 0.15,
    'bedrock floor descends from upstream to downstream');
  for (const sample of samples) {
    assert.ok(Number.isFinite(sample.x) && Number.isFinite(sample.floorY));
    assert.ok(sample.halfGap > 0.55 && sample.halfGap < 2.6,
      `sampled slot half-gap stays narrow but resolvable (${sample.halfGap})`);
    assert.ok(sample.crestY > sample.floorY + 6, 'sampled fins remain tall');
  }

  // Regression for the old waffle/louver read: sequential smooth-min over
  // hundreds of boundary-matched blocks swelled every overlap. The slot's
  // bedrock union must remain exact-min.
  assert.ok(macro.blocks.every((block) => (block.smoothUnionR ?? 0) === 0),
    'slot wall/floor blocks use hard union, not sequential smooth-min');

  const evaluator = createGeologicalSdfEvaluator(macro, params, rng.fork('detail'));
  for (const sample of [samples[4], samples[16], samples[24], samples[32], samples[44]]) {
    const belowFloor = evaluator.evalMacro(sample.x, sample.floorY - 0.15, sample.z);
    const corridor = evaluator.evalMacro(sample.x, sample.floorY + 1.35, sample.z);
    assert.ok(belowFloor < 0, `real sculptable floor exists at z=${sample.z}`);
    assert.ok(corridor > 0.25, `open corridor above floor at z=${sample.z} (${corridor})`);
  }
});

test('generator runs slot flood on the Mesh→SDF bake, reports it and emits it in live preview', () => {
  const previewProcesses = [];
  const result = generateRockFormation(
    {
      archetype: 'slot-canyon',
      seed: 4029,
      quality: 'fast',
      decimation: 1,
      sdfErosionSource: 'mesh',
      erosionSdfResolution: 64,
      erosionPreviewFrames: 4,
      rainErosionAmount: 0.45,
      rainDroplets: 1800,
      rainEdgeProtection: 0.7,
      slotFloodAmount: 0.85,
      slotFloodParticles: 900,
      slotSedimentSupply: 0.62,
      windErosionAmount: 0.25,
      windParticles: 1200,
      windAzimuth: 35,
      windShiftMode: 'variable',
      windShiftRange: 60,
      windShiftCycles: 2,
      thermalErosionAmount: 0,
      cryoErosionAmount: 0,
      chemDissolutionAmount: 0,
    },
    null,
    (preview) => previewProcesses.push(preview.process)
  );

  const slotFlood = result.stats.sdfErosion?.slotFlood;
  assert.ok(slotFlood, 'generator reports slot-flood stats');
  assert.equal(slotFlood.floodParticles, 900);
  assert.ok(slotFlood.windParticles > 250, 'channelled wind polish runs after the flood');
  assert.ok(slotFlood.wallEvents > 1000);
  assert.ok(slotFlood.floorEvents > 500);
  assert.ok(slotFlood.erodedM3 > 0.05);
  assert.ok(previewProcesses.includes('slotFlood'),
    `live preview includes the slot-flood checkpoint (${previewProcesses})`);

  assert.ok(result.mesh.positions.length > 300);
  assert.ok(result.mesh.positions.every(Number.isFinite));
  let minY = Infinity;
  for (let i = 1; i < result.mesh.positions.length; i += 3) {
    minY = Math.min(minY, result.mesh.positions[i]);
  }
  assert.ok(Math.abs(minY) < 1e-4, `final slot remains grounded at Y=0 (${minY})`);
  assert.ok(result.stats.dimensions[2] / result.stats.dimensions[0] > 1.25,
    `final asset remains a long slot (${result.stats.dimensions})`);
});

test('Rock Studio and CLI expose slot-flood amount, packets and sediment supply', () => {
  const read = (path) => fs.readFileSync(new URL(path, import.meta.url), 'utf8');
  const main = read('../src/app/main.js');
  const cli = read('../tools/generate-cli.mjs');
  const suite = read('../src/geology/sdf-erosion-suite.js');

  for (const token of [
    "key: 'slotFloodAmount'",
    "key: 'slotFloodParticles'",
    "key: 'slotSedimentSupply'",
    'slotFloodAmount: 0.85',
    'slotFloodParticles: state.params.slotFloodParticles',
    'stats.sdfErosion?.slotFlood',
  ]) assert.ok(main.includes(token), `Rock Studio token ${token}`);

  for (const token of [
    "'--slot-flood'",
    "'--slot-flood-particles'",
    "'--slot-sediment'",
    "'slotFlood'",
    'suite.slotFlood',
  ]) assert.ok(cli.includes(token), `CLI token ${token}`);

  for (const token of [
    'export function erodeSdfWithSlotFlood',
    "key: 'slotFlood'",
    'Slot flash-flood abrasion + channelled wind polish',
    'tools/cover',
  ]) assert.ok(suite.includes(token), `suite token ${token}`);
});
