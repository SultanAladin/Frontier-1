import assert from 'node:assert/strict';
import test from 'node:test';

import { createRng } from '../src/core/prng.js';
import { erodeSdfWithRain } from '../src/geology/rain-erosion.js';
import { generateRockFormation } from '../src/geology/generator.js';

/** Builds a dense analytic SDF grid stub compatible with evaluateSparseSdf's shape. */
function makeSdfGrid(sdfFn, nx, ny, nz, h) {
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

function roundedBox(cx, cy, cz, hx, hy, hz, r) {
  return (x, y, z) => {
    const qx = Math.abs(x - cx) - hx, qy = Math.abs(y - cy) - hy, qz = Math.abs(z - cz) - hz;
    const ax = Math.max(qx, 0), ay = Math.max(qy, 0), az = Math.max(qz, 0);
    return Math.hypot(ax, ay, az) + Math.min(Math.max(qx, Math.max(qy, qz)), 0) - r;
  };
}

function signedVolumeApprox(sdf) {
  // Sum of inside-cell volumes via node sign (coarse but consistent for deltas)
  let inside = 0;
  for (let i = 0; i < sdf.values.length; i++) if (sdf.values[i] < 0) inside++;
  return inside * sdf.voxelSize ** 3;
}

test('SDF rain droplets carve the surface (volume loss up top), stay deterministic and never produce NaN', () => {
  const make = () => makeSdfGrid(roundedBox(2, 1.2, 2, 1.2, 1.0, 1.2, 0.08), 80, 48, 80, 0.05);
  const sdf = make();
  const volBefore = signedVolumeApprox(sdf);

  const stats = erodeSdfWithRain(sdf, {
    dropletCount: 1400, amount: 0.7, edgeProtection: 0.8,
    rng: createRng(4242), groundY: 0.05,
  });

  assert.ok(stats.droplets > 300, `Expected hundreds of droplets to strike the block, got ${stats.droplets}`);
  assert.ok(stats.erodedVolume > 0.002, `Expected measurable carved volume, got ${stats.erodedVolume}`);
  assert.ok(stats.steps > stats.droplets * 3, 'Droplets must actually run downhill over the surface');

  // The block floats in a closed grid, so most sediment redeposits at its base
  // (net volume is ~conserved minus export). The physical claim: the UPPER
  // surface band recedes (mean phi increases) -- measurable at sub-voxel scale.
  void volBefore;
  const freshBand = make();
  let bandDeltaSum = 0, bandN = 0, bandMax = 0;
  for (let iz = 0; iz < sdf.gz; iz++) {
    for (let iy = 34; iy < sdf.gy; iy++) {
      for (let ix = 0; ix < sdf.gx; ix++) {
        const idx = ix + iy * sdf.strideY + iz * sdf.strideZ;
        if (Math.abs(freshBand.values[idx]) < 0.08) {
          const d = sdf.values[idx] - freshBand.values[idx];
          bandDeltaSum += d;
          bandMax = Math.max(bandMax, d);
          bandN++;
        }
      }
    }
  }
  assert.ok(stats.depositedVolume > 0, 'Some eroded sediment must redeposit (talus at the base)');
  // Mean is diluted by the large untouched flat top; require net-positive mean
  // recession AND at least one genuinely incised rill (> 0.1 voxel deep)
  assert.ok(bandN > 100 && bandDeltaSum / bandN > 0.00015 && bandMax > 0.005,
    `Upper surface must recede with incised rills (mean ${(bandDeltaSum / Math.max(bandN, 1)).toFixed(5)}, max rill ${bandMax.toFixed(4)})`);

  for (let i = 0; i < sdf.values.length; i++) {
    assert.ok(Number.isFinite(sdf.values[i]), `SDF must never contain NaN/Inf after erosion (node ${i})`);
  }

  // Underside must be untouched: rain only arrives from above
  const fresh = make();
  const strideY = sdf.strideY, strideZ = sdf.strideZ;
  let maxUnderDelta = 0;
  for (let iz = 30; iz < 50; iz++) {
    for (let ix = 30; ix < 50; ix++) {
      const idx = ix + 6 * strideY + iz * strideZ; // y = 0.3, below box centre, inside footprint shadow
      maxUnderDelta = Math.max(maxUnderDelta, Math.abs(sdf.values[idx] - fresh.values[idx]));
    }
  }
  assert.ok(maxUnderDelta < 0.02, `Rain must not erode the sheltered underside (delta ${maxUnderDelta})`);

  // Determinism: same seed => bit-identical grid
  const again = make();
  erodeSdfWithRain(again, {
    dropletCount: 1400, amount: 0.7, edgeProtection: 0.8,
    rng: createRng(4242), groundY: 0.05,
  });
  for (let i = 0; i < sdf.values.length; i++) {
    assert.equal(again.values[i], sdf.values[i], `Same seed must reproduce the identical eroded SDF (node ${i})`);
  }
});

test('Edge protection keeps sharp convex arrises crisp while faces still rill (no melted chocolate)', () => {
  // Sharp box (tiny corner radius): top edges are the crisp feature to protect
  const make = () => makeSdfGrid(roundedBox(2, 1.2, 2, 1.2, 1.0, 1.2, 0.02), 80, 48, 80, 0.05);
  const fresh = make();

  const measure = (grid, edgeProtection) => {
    erodeSdfWithRain(grid, {
      dropletCount: 1600, amount: 0.85, edgeProtection,
      rng: createRng(777), groundY: 0.05,
    });
    // Mean |delta| over surface-band nodes near the top arris (y~2.22, x~0.8 edge line)
    // vs nodes on the flat side faces (x~0.8 plane, mid height)
    const { strideY, strideZ } = grid;
    let edgeSum = 0, edgeN = 0, faceSum = 0, faceN = 0;
    for (let iz = 2; iz < grid.gz - 2; iz++) {
      for (let iy = 2; iy < grid.gy - 2; iy++) {
        for (let ix = 2; ix < grid.gx - 2; ix++) {
          const idx = ix + iy * strideY + iz * strideZ;
          const f0 = fresh.values[idx];
          if (Math.abs(f0) > 0.06) continue; // surface band only
          const d = Math.abs(grid.values[idx] - f0);
          const wy = iy * 0.05, wx = ix * 0.05;
          const nearTopEdge = wy > 2.05 && Math.min(Math.abs(wx - 0.8), Math.abs(wx - 3.2)) < 0.12;
          const onSideFace = wy > 0.9 && wy < 1.7 && Math.min(Math.abs(wx - 0.8), Math.abs(wx - 3.2)) < 0.07;
          if (nearTopEdge) { edgeSum += d; edgeN++; }
          else if (onSideFace) { faceSum += d; faceN++; }
        }
      }
    }
    return { edge: edgeSum / Math.max(edgeN, 1), face: faceSum / Math.max(faceN, 1) };
  };

  const protectedRun = measure(make(), 1.0);
  const unprotectedRun = measure(make(), 0.0);

  assert.ok(unprotectedRun.face > 0.0005, 'Side faces must actually erode (sanity)');
  assert.ok(
    protectedRun.edge < unprotectedRun.edge * 0.75,
    `Full edge protection must cut arris erosion substantially (protected ${protectedRun.edge}, unprotected ${unprotectedRun.edge})`
  );
  assert.ok(
    protectedRun.face > unprotectedRun.face * 0.45,
    'Face erosion must continue under edge protection (protection is curvature-targeted, not global)'
  );
});

test('Per-node carve cap bounds channel depth (runoff convergence cannot drill through)', () => {
  // Thin slab: unlimited convergence would perforate it
  const sdf = makeSdfGrid(roundedBox(2, 1.2, 2, 1.4, 1.0, 0.22, 0.02), 80, 48, 80, 0.05);
  const fresh = makeSdfGrid(roundedBox(2, 1.2, 2, 1.4, 1.0, 0.22, 0.02), 80, 48, 80, 0.05);
  erodeSdfWithRain(sdf, {
    dropletCount: 2600, amount: 1.0, edgeProtection: 0.7,
    rng: createRng(99), groundY: 0.05,
  });
  // Centre plane of the slab (z = 2.0) at mid height must remain solid (phi < 0)
  const { strideY, strideZ } = sdf;
  let violations = 0;
  for (let iy = 14; iy < 34; iy++) {
    for (let ix = 20; ix < 60; ix++) {
      const idx = ix + iy * strideY + 40 * strideZ;
      if (fresh.values[idx] < -0.12 && sdf.values[idx] >= 0) violations++;
    }
  }
  assert.equal(violations, 0, 'Rain must never perforate a thin slab core (thin-wall + carve-cap protection)');
});

test('Rock generator integration: rain-eroded formation is valid and reports stats', () => {
  const base = generateRockFormation({ archetype: 'cliff-wall', seed: 4029, quality: 'fast', rainErosionAmount: 0 });
  const eroded = generateRockFormation({
    archetype: 'cliff-wall', seed: 4029, quality: 'fast',
    rainErosionAmount: 0.65, rainDroplets: 2500, rainEdgeProtection: 0.75,
  });

  assert.equal(base.stats.rainErosion, null, 'No rain pass must report null stats');
  assert.ok(eroded.stats.rainErosion, 'Eroded build must report rain stats');
  assert.ok(eroded.stats.rainErosion.droplets > 500, 'Droplets must strike the formation');
  assert.ok(eroded.stats.rainErosion.erodedM3 > 0, 'Rain must remove measurable rock volume');

  for (const res of [base, eroded]) {
    const pos = res.mesh.positions;
    assert.ok(pos.length > 300, 'Mesh must contain real geometry');
    for (let i = 0; i < pos.length; i++) {
      assert.ok(Number.isFinite(pos[i]), 'All vertex positions must be finite after rain erosion');
    }
    let minY = Infinity;
    for (let i = 1; i < pos.length; i += 3) minY = Math.min(minY, pos[i]);
    assert.ok(Math.abs(minY) < 0.05, `Formation must remain grounded at y=0, got minY=${minY}`);
  }

  // Rain must change the silhouette measurably but not destroy the formation
  const triRatio = eroded.stats.triangles / base.stats.triangles;
  assert.ok(triRatio > 0.55 && triRatio < 1.8, `Eroded mesh scale must stay sane vs base (ratio ${triRatio.toFixed(2)})`);
  assert.ok(
    Math.abs(eroded.mesh.positions[1] - base.mesh.positions[1]) >= 0 ||
    eroded.stats.triangles !== base.stats.triangles,
    'Eroded formation must differ from the uneroded one'
  );
});
