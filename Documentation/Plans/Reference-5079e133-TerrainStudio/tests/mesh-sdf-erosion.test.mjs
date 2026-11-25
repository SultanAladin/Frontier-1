import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';

import { convertMeshToSdf } from '../src/core/mesh-to-sdf.js';
import { extractDualContourMesh } from '../src/core/dual-contour.js';
import { createRng } from '../src/core/prng.js';
import { generateRockFormation } from '../src/geology/generator.js';
import { normalizeErosionSdfResolution } from '../src/geology/presets.js';
import {
  applySdfErosionSuite,
  erodeSdfWithWind,
} from '../src/geology/sdf-erosion-suite.js';
import { exportFormationToGLB } from '../src/io/glb-exporter.js';

function boxMesh(minX, minY, minZ, maxX, maxY, maxZ) {
  return {
    positions: new Float32Array([
      minX, minY, minZ,  maxX, minY, minZ,  maxX, maxY, minZ,  minX, maxY, minZ,
      minX, minY, maxZ,  maxX, minY, maxZ,  maxX, maxY, maxZ,  minX, maxY, maxZ,
    ]),
    indices: new Uint32Array([
      0, 1, 2, 0, 2, 3,
      5, 4, 7, 5, 7, 6,
      4, 0, 3, 4, 3, 7,
      1, 5, 6, 1, 6, 2,
      4, 5, 1, 4, 1, 0,
      3, 2, 6, 3, 6, 7,
    ]),
  };
}

function meshBounds(positions) {
  const b = [Infinity, Infinity, Infinity, -Infinity, -Infinity, -Infinity];
  for (let i = 0; i < positions.length; i += 3) {
    for (let k = 0; k < 3; k++) {
      const v = positions[i + k];
      if (v < b[k]) b[k] = v;
      if (v > b[k + 3]) b[k + 3] = v;
    }
  }
  return b;
}

function makeDenseSdf(sdfFn, nx, ny, nz, h) {
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

function roundedBoxSdf(cx, cy, cz, hx, hy, hz, r) {
  return (x, y, z) => {
    const qx = Math.abs(x - cx) - hx;
    const qy = Math.abs(y - cy) - hy;
    const qz = Math.abs(z - cz) - hz;
    const ax = Math.max(qx, 0), ay = Math.max(qy, 0), az = Math.max(qz, 0);
    return Math.hypot(ax, ay, az) + Math.min(Math.max(qx, Math.max(qy, qz)), 0) - r;
  };
}

test('Mesh → SDF conversion recovers sign, exact near-surface distance, world bounds and selectable resolution', () => {
  const mesh = boxMesh(0, 0, 0, 2, 1, 2);
  const low = convertMeshToSdf(mesh, { maxAxisVoxels: 40 });
  const high = convertMeshToSdf(mesh, { maxAxisVoxels: 96 });

  assert.equal(normalizeErosionSdfResolution(0), 0);
  assert.equal(normalizeErosionSdfResolution(257.4), 257);
  assert.equal(normalizeErosionSdfResolution(9999), 512);
  assert.ok(high.voxelSize < low.voxelSize * 0.55, 'Higher erosion resolution must produce proportionally smaller voxels');
  assert.ok(Math.max(...high.conversionStats.gridDims) >= 96);
  assert.equal(high.conversionStats.openRayRows, 0, 'Closed box rays must pair cleanly');
  assert.ok(high.conversionStats.solidNodes > 0);
  assert.ok(high.conversionStats.bvhNodes > 0);
  assert.ok(high.conversionStats.distanceTriangleTests > high.conversionStats.distanceQueries);

  assert.ok(high.sampleWorld(1, 0.5, 1) < 0, 'Box centre must be inside');
  assert.ok(Math.abs(high.sampleWorld(0.03, 0.5, 1) + 0.03) < 0.012, 'Near-face inside distance must be metric');
  assert.ok(Math.abs(high.sampleWorld(-0.05, 0.5, 1) - 0.05) < 0.012, 'Near-face outside distance must be metric');
  assert.ok(Math.abs(high.sampleWorld(1, -0.03, 1) - 0.03) < 0.012, 'Grounded base distance/origin must be preserved');
  assert.deepEqual(high.conversionStats.meshBounds, [0, 0, 0, 2, 1, 2]);

  const remeshed = extractDualContourMesh(high, { qefRegularization: 0.014 });
  assert.ok(remeshed.indices.length > 3000, 'Converted SDF must contour back to a real mesh');
  const b = meshBounds(remeshed.positions);
  const expected = [0, 0, 0, 2, 1, 2];
  for (let i = 0; i < 6; i++) {
    assert.ok(Math.abs(b[i] - expected[i]) < 0.05, `Remeshed bound ${i} should preserve the source box (${b[i]} vs ${expected[i]})`);
  }

  const clamped = convertMeshToSdf(mesh, { maxAxisVoxels: 256, maxNodes: 120000 });
  assert.equal(clamped.conversionStats.resolutionClamped, true);
  assert.ok(clamped.conversionStats.nodeCount <= 120000, 'Memory guard must cap the SDF node count');
});

test('Wind saltation abrades the windward face more than the lee face of a mesh-converted SDF', () => {
  const mesh = boxMesh(0, 0, 0, 2, 1, 2);
  const fresh = convertMeshToSdf(mesh, { maxAxisVoxels: 64 });
  const sdf = convertMeshToSdf(mesh, { maxAxisVoxels: 64 });

  const stats = erodeSdfWithWind(sdf, {
    amount: 0.85,
    particleCount: 700,
    azimuthDeg: 0, // toward +X, so x=0 is windward and x=2 is lee
    turbulence: 0.25,
    streamlining: 0.65,
    edgeProtection: 0.7,
    rng: createRng(90210),
    groundY: 0,
  });

  assert.ok(stats.particles > 250, `Wind rays must hit the box, got ${stats.particles}`);
  assert.ok(stats.erodedVolume > 0, 'Wind must abrade measurable material');
  assert.ok(stats.steps > stats.particles * 8, 'Saltating grains must travel over the surface');

  let west = 0, westN = 0, east = 0, eastN = 0;
  for (let iz = 0; iz < sdf.gz; iz++) {
    for (let iy = 0; iy < sdf.gy; iy++) {
      for (let ix = 0; ix < sdf.gx; ix++) {
        const idx = ix + iy * sdf.strideY + iz * sdf.strideZ;
        const f = fresh.values[idx];
        if (!Number.isFinite(f) || Math.abs(f) > 0.045) continue;
        const x = sdf.originX + ix * sdf.voxelSize;
        const y = sdf.originY + iy * sdf.voxelSize;
        const z = sdf.originZ + iz * sdf.voxelSize;
        if (y < 0.25 || y > 0.75 || z < 0.45 || z > 1.55) continue;
        const d = sdf.values[idx] - f;
        if (x < 0.06) { west += d; westN++; }
        else if (x > 1.94) { east += d; eastN++; }
      }
    }
  }
  assert.ok(westN > 100 && eastN > 100);
  const westMean = west / westN;
  const eastMean = east / eastN;
  assert.ok(westMean > 0.0004, `Windward face must recede (mean phi delta ${westMean})`);
  assert.ok(westMean > eastMean + 0.0003, `Windward abrasion must exceed lee change (${westMean} vs ${eastMean})`);
});

test('Full SDF erosion suite is deterministic, finite, process-active and does not attack the sheltered core', () => {
  const make = () => makeDenseSdf(roundedBoxSdf(2, 1.2, 2, 1.2, 1.0, 1.2, 0.06), 64, 40, 64, 0.0625);
  const fresh = make();
  const run = () => {
    const sdf = make();
    const stats = applySdfErosionSuite(sdf, {
      chemicalDissolutionAmount: 0.55, chemicalDroplets: 220,
      windAmount: 0.65, windParticles: 260, windAzimuthDeg: 20,
      rainAmount: 0.55, rainDroplets: 260,
      cryoAmount: 0.55, cryoParticles: 220,
      thermalAmount: 0.55, thermalParticles: 260, thermalReposeAngleDeg: 34,
      edgeProtection: 0.75,
      groundY: 0.05,
      rng: createRng(5150),
    });
    return { sdf, stats };
  };

  const a = run();
  const b = run();
  for (const key of ['chemicalDissolution', 'wind', 'rain', 'cryo', 'thermal']) {
    assert.ok(a.stats[key], `${key} must run in the suite`);
    assert.ok((a.stats[key].erodedVolume ?? a.stats[key].dissolvedVolume ?? 0) > 0, `${key} must remove material`);
    assert.ok(a.stats[key].steps > 0, `${key} must advance particles`);
  }
  assert.ok(a.stats.rain.droplets > 50);
  assert.ok(a.stats.wind.particles > 50);

  for (let i = 0; i < a.sdf.values.length; i++) {
    assert.ok(Number.isFinite(a.sdf.values[i]), `Suite must never leave NaN/Inf in a dense SDF (node ${i})`);
    assert.equal(b.sdf.values[i], a.sdf.values[i], `Same seed/config must be bit-deterministic (node ${i})`);
  }

  // Deep core nodes under the block centre are outside every process's
  // narrow-band brush and must remain untouched.
  let maxCoreDelta = 0;
  for (let iz = 28; iz < 36; iz++) {
    for (let ix = 28; ix < 36; ix++) {
      const idx = ix + 19 * a.sdf.strideY + iz * a.sdf.strideZ;
      maxCoreDelta = Math.max(maxCoreDelta, Math.abs(a.sdf.values[idx] - fresh.values[idx]));
    }
  }
  assert.ok(maxCoreDelta < 1e-6, `Sheltered solid core must be untouched, delta ${maxCoreDelta}`);
});

test('Rock generator converts its source mesh to a high-resolution SDF, erodes it, and exports a grounded placement mesh', () => {
  const base = generateRockFormation({
    archetype: 'cliff-wall', seed: 4029, quality: 'fast', decimation: 0.35,
  });
  const eroded = generateRockFormation({
    archetype: 'cliff-wall', seed: 4029, quality: 'fast', decimation: 0.35,
    sdfErosionSource: 'mesh', erosionSdfResolution: 128,
    rainErosionAmount: 0.55, rainDroplets: 700, rainEdgeProtection: 0.75,
    windErosionAmount: 0.55, windParticles: 500, windAzimuth: 35,
    thermalErosionAmount: 0.4, thermalParticles: 400,
    cryoErosionAmount: 0.4, cryoParticles: 300,
    chemDissolutionAmount: 0.4, chemDissolutionDroplets: 300,
    chemAmount: 0.35,
  });

  assert.equal(base.stats.meshToSdf, null);
  assert.ok(eroded.stats.meshToSdf, 'Mesh-source bake must report conversion stats');
  assert.equal(eroded.stats.meshToSdf.effectiveMaxAxisVoxels, 128);
  assert.ok(eroded.stats.meshToSdf.sourceTriangles > 1000);
  assert.ok(eroded.stats.meshToSdf.openRayRows <= 2, 'Generated source mesh should voxelize as a closed surface');
  assert.ok(eroded.stats.sdfErosion, 'Generator must report the SDF suite');
  assert.equal(eroded.stats.sdfErosion.source, 'mesh');
  assert.ok(Math.max(...eroded.stats.sdfErosion.gridDims) >= 128);
  assert.ok(eroded.stats.sdfErosion.voxelSize < eroded.stats.sourceVoxelSize);
  for (const key of ['chemicalDissolution', 'wind', 'rain', 'cryo', 'thermal']) {
    assert.ok(eroded.stats.sdfErosion[key]?.particles > 0, `${key} particles must engage the formation`);
    assert.ok(eroded.stats.sdfErosion[key]?.erodedM3 > 0, `${key} must change rock volume`);
  }

  const pos = eroded.mesh.positions;
  assert.ok(pos.length > 1000 && eroded.mesh.indices.length > 1000);
  for (let i = 0; i < pos.length; i++) assert.ok(Number.isFinite(pos[i]), 'Final placement mesh must be finite');
  const b = meshBounds(pos);
  assert.ok(Math.abs(b[1]) < 1e-4, `Placement mesh must remain grounded at Y=0, got ${b[1]}`);
  for (let axis = 0; axis < 3; axis++) {
    const ratio = eroded.stats.dimensions[axis] / base.stats.dimensions[axis];
    assert.ok(ratio > 0.82 && ratio < 1.18, `Eroded dimension ${axis} must remain placement-stable (ratio ${ratio})`);
  }

  const glb = exportFormationToGLB(eroded);
  const view = new DataView(glb.buffer, glb.byteOffset, glb.byteLength);
  assert.equal(view.getUint32(0, true), 0x46546c67, 'Eroded placement asset must export as valid GLB');
});

test('Rock Studio UI wires mesh→SDF source, bake resolution, process sliders and SDF HUD', () => {
  const html = fs.readFileSync(new URL('../index.html', import.meta.url), 'utf8');
  const main = fs.readFileSync(new URL('../src/app/main.js', import.meta.url), 'utf8');

  assert.ok(html.includes('id="select-sdf-erosion-source"'));
  assert.ok(html.includes('Placement Mesh → SDF'));
  assert.ok(html.includes('id="select-erosion-sdf-resolution"'));
  assert.ok(html.includes('<option value="384">384 — Extreme (slow)</option>'));
  assert.ok(html.includes('id="stat-sdf"'));
  assert.ok(html.includes('id="sdf-erosion-readout"'));

  for (const token of [
    "sdfErosionSource: 'mesh'",
    'erosionSdfResolution: 192',
    "key: 'windErosionAmount'",
    "key: 'windAzimuth'",
    "key: 'windStreamlining'",
    "key: 'thermalErosionAmount'",
    "key: 'cryoErosionAmount'",
    "key: 'chemDissolutionAmount'",
    "spec.type === 'heading'",
    "document.getElementById('select-sdf-erosion-source')",
    "document.getElementById('select-erosion-sdf-resolution')",
  ]) {
    assert.ok(main.includes(token), `Rock Studio main.js must wire ${token}`);
  }
});
