#!/usr/bin/env node
//========================================================================================================================
//                                            GEOMETRYCOMPILERVALIDATION.MJS
//========================================================================================================================
// Headless verification suite for Frontier Experimental/GeometryCompiler:
//   1. Nanite-style Cluster LOD DAG (watertight cuts, monotone error, nested spheres, OBJ weld)
//   2. UV Workspace planar unwrap, packing, overlap check & OBJ/SVG round-trip
//   3. Sparse 8x8x8 SDF Baker (conservative Mip1 <= Mip0, thin-wall vase sign, eviction, slices, .fvol, WGSL)
//   4. StudioIconSet integration & zero-And naming hygiene

import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { SOURCE_PRESETS, constructSource, parseObj } from './Engine/SourceGeometry.js';
import { buildClusterHierarchy } from './Engine/ClusterHierarchy.js';
import { createSelectionState, selectClusters, selectBruteForce } from './Engine/ClusterSelection.js';
import { unwrapCharts, packCharts, cornerUvs } from './Engine/UvCharts.js';
import { formatObj, formatUvSvg } from './Engine/ObjExport.js';
import {
  selectBakeIndices,
  compileSparseSdfVolume,
  sampleSparseSdf,
  evictMip0ForBudget,
  extractSdfSliceRgba,
  serializeVolumeFvol,
} from './Engine/SparseSdfBaker.js';
import { SDF_RAYMARCH_WGSL } from './Engine/SdfRaymarchViewport.js';
import { FAMILIES, ICONS as STUDIO_ICONS } from '../StudioIconSet/IconDefinitions.js';
import { STUDIO_ICON_MAP, MISSING_ICON_SLOTS, renderIcon } from './IconRegistry.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
let passed = 0;

function runSuite(name, fn) {
  const t0 = performance.now();
  fn();
  const ms = (performance.now() - t0).toFixed(1);
  console.log(`[PASS] ${name} (${ms} ms)`);
  passed++;
}

function checkWatertight(indices) {
  const edges = new Map();
  for (let i = 0; i < indices.length; i += 3) {
    const tri = [indices[i], indices[i + 1], indices[i + 2]];
    for (let k = 0; k < 3; k++) {
      const a = tri[k], b = tri[(k + 1) % 3];
      const lo = a < b ? a : b, hi = a < b ? b : a;
      const key = lo * 4294967296 + hi;
      edges.set(key, (edges.get(key) || 0) + 1);
    }
  }
  let open = 0, nonManifold = 0;
  for (const count of edges.values()) {
    if (count === 1) open++;
    else if (count > 2) nonManifold++;
  }
  return { open, nonManifold };
}

console.log('=== Frontier Experimental/GeometryCompiler Validation ===');

// 1. Cluster Hierarchy + Watertight Cuts across presets
runSuite('Cluster LOD DAG (monotone error, nested spheres, crack-free cuts)', () => {
  for (const presetId of ['vase', 'braid', 'torus', 'asteroid', 'terrain']) {
    const src = constructSource(presetId, 12000);
    const h = buildClusterHierarchy(src.positions, src.indices);
    assert(h.levels.length >= 3, `${presetId} must build >= 3 LOD levels`);
    for (let g = 0; g < h.groupCount; g++) {
      const start = h.groupChildStart[g], end = h.groupChildStart[g + 1];
      for (let i = start; i < end; i++) {
        const child = h.groupChildren[i];
        const self = h.clusterGroupSelf[child];
        if (self >= 0) {
          assert(h.groupError[g] >= h.groupError[self] - 1e-6, `${presetId} error must be monotone`);
        }
      }
    }
    if (src.closed) {
      // Test 10 random camera positions for oracle parity and crack-free watertight cut
      const selState = createSelectionState(h);
      for (let k = 0; k < 10; k++) {
        const angle = k * 0.63;
        const dist = 1.6 + (k % 4) * 1.4;
        const view = {
          eye: [Math.sin(angle) * dist, 0.5, Math.cos(angle) * dist],
          focal: 900,
          near: 0.05,
          threshold: 1.5,
          planes: null,
          backface: false,
          forcedLevel: -1,
        };
        selectClusters(h, selState, view);
        const oracle = selectBruteForce(h, view);
        assert.equal(selState.clusters, oracle.length, `${presetId} camera ${k} DAG walk != brute-force oracle`);
        const cutTris = [];
        for (let r = 0; r < selState.rangeCount; r++) {
          const first = selState.firsts[r], count = selState.counts[r];
          for (let j = 0; j < count; j++) cutTris.push(h.cornerVertex[first + j]);
        }
        const wt = checkWatertight(cutTris);
        assert.equal(wt.open, 0, `${presetId} camera ${k} has ${wt.open} open edges`);
        assert.equal(wt.nonManifold, 0, `${presetId} camera ${k} has ${wt.nonManifold} non-manifold edges`);
      }
    }
  }
});

// 2. UV Unwrap, Pack & OBJ/SVG Export Round-Trip
runSuite('UV Workspace unwrap, packing & OBJ round-trip', () => {
  const src = constructSource('torus', 4000);
  const layout = packCharts(unwrapCharts(src.positions, src.indices));
  assert(layout.charts.length >= 2, 'Torus must unwrap into multiple charts');
  const uvs = cornerUvs(layout);
  for (let i = 0; i < uvs.length; i++) {
    assert(uvs[i] >= -1e-4 && uvs[i] <= 1 + 1e-4, `UV out of [0,1]: ${uvs[i]}`);
  }
  const objText = formatObj(src.positions, src.indices, uvs, 'torus');
  const parsed = parseObj(objText);
  assert.equal(parsed.indices.length, src.indices.length, 'OBJ round-trip triangle count mismatch');
  const svg = formatUvSvg(layout, uvs, 'torus');
  assert(svg.includes('<svg') && svg.includes('</svg>'), 'UV SVG export malformed');
});

// 3. Sparse 8x8x8 SDF Baker on Fluted Vase (thin-wall hollow interior) + Braided Knot + Open Terrain
runSuite('Sparse 8x8x8 SDF Baker (Fluted Vase thin-wall, Conservative Mip1 <= Mip0)', () => {
  const src = constructSource('vase', 24000);
  const h = buildClusterHierarchy(src.positions, src.indices);
  const picked = selectBakeIndices(src.indices, h, 14000);
  assert(picked.triangleCount <= 14000 && picked.triangleCount > 2000, 'selectBakeIndices should choose watertight mid-LOD');

  const vol32 = compileSparseSdfVolume({ positions: src.positions, indices: picked.indices, closed: true }, { gridDim: 32 });
  assert.equal(vol32.dim, 32);
  assert.equal(vol32.mip1Dim, 16);
  assert.equal(vol32.monotonicityVerified, true, 'Mip1 <= Mip0 conservative invariant failed at 32^3');
  assert(vol32.allocatedBrickCount > 0 && vol32.allocatedBrickCount < vol32.totalBricks, '32^3 vase must prune empty bricks');
  // Hollow center of vase (x=0, y=0.2, z=0) should be OUTSIDE (positive SDF), corner outside bounds also positive
  const hollowCenter = sampleSparseSdf(vol32, 0, 0.1, 0, false);
  const farCorner = sampleSparseSdf(vol32, vol32.boundsMin[0], vol32.boundsMin[1], vol32.boundsMin[2], false);
  assert(hollowCenter > 0, `Hollow vase center should be exterior (>0), got ${hollowCenter}`);
  assert(farCorner > 0, `Far corner should be exterior (>0), got ${farCorner}`);

  // Verify 64^3 higher pruning ratio & conservative Mip1
  const vol64 = compileSparseSdfVolume({ positions: src.positions, indices: picked.indices, closed: true }, { gridDim: 64 });
  assert.equal(vol64.dim, 64);
  assert.equal(vol64.monotonicityVerified, true, 'Mip1 <= Mip0 conservative invariant failed at 64^3');
  assert(vol64.prunedPercent >= 50, `64^3 vase should prune >=50% bricks, got ${vol64.prunedPercent}%`);

  // Verify open-surface terrain tile bakes cleanly without NaN
  const terrain = constructSource('terrain', 4000);
  const volTerr = compileSparseSdfVolume(terrain, { gridDim: 32 });
  assert.equal(volTerr.monotonicityVerified, true, 'Open terrain Mip1 monotonicity failed');
  assert(volTerr.allocatedBrickCount > 0, 'Open terrain should allocate narrow-band bricks');
});

// 4. SDF Slice RGBA, VRAM Eviction Fallback & .fvol Serialization
runSuite('SDF Slice RGBA, Mip0 Eviction Fallback & .fvol Export', () => {
  const src = constructSource('braid', 8000);
  const vol = compileSparseSdfVolume(src, { gridDim: 32 });
  const rgba = extractSdfSliceRgba(vol, 0, 0.5, 128, false);
  assert.equal(rgba.length, 128 * 128 * 4);
  let amberIsoPixels = 0;
  for (let i = 0; i < rgba.length; i += 4) {
    if (rgba[i] === 255 && rgba[i + 1] === 182 && rgba[i + 2] === 78) amberIsoPixels++;
  }
  assert(amberIsoPixels > 10, `Expected visible amber iso-zero contour in slice, got ${amberIsoPixels}`);

  const fvol = JSON.parse(serializeVolumeFvol(vol, 'Braided Knot'));
  assert.equal(fvol.format, 'frontier.fvol.v1');
  assert.deepEqual(fvol.dimensions, [32, 32, 32]);
  assert.equal(fvol.monotonicityVerified, true);

  // Evict Mip0 under tiny 8 KB budget and confirm Mip1 fallback still samples finite values
  const ev = evictMip0ForBudget(vol, 8192);
  assert.equal(ev.evicted, true, 'Mip0 should evict when budget < resident size');
  assert.equal(vol.mip0Evicted, true);
  const dEvicted = sampleSparseSdf(vol, 0, 0, 0, false);
  assert(Number.isFinite(dEvicted), 'Evicted volume must still sample finite Mip1 distance');
});

// 5. WebGPU SDF Raymarch Shader Integrity
runSuite('WebGPU Sparse 8x8x8 SDF Raymarch WGSL Contract', () => {
  for (const token of ['fn vsSdfRay', 'fn fsSdfRay', 'fn sampleMip1', 'fn sampleMip0', 'fn traceBricksOverlay', 'fn calcNormal']) {
    assert(SDF_RAYMARCH_WGSL.includes(token), `Missing WGSL symbol ${token}`);
  }
});

// 6. StudioIconSet Integration + Zero-And Naming Governance
runSuite('StudioIconSet (91 icons) Integration & Zero-And Naming Check', () => {
  assert.equal(STUDIO_ICONS.length, 91, 'StudioIconSet must expose all 91 icons');
  assert.equal(FAMILIES.length, 5, 'StudioIconSet must expose 5 families');
  const studioIds = new Set(STUDIO_ICONS.map((i) => i.id));
  for (const [slot, id] of Object.entries(STUDIO_ICON_MAP)) {
    assert(studioIds.has(id), `Icon slot "${slot}" points to unknown StudioIconSet id "${id}"`);
    const svg = renderIcon(slot);
    assert(svg.includes('<svg') && svg.includes('viewBox="0 0 24 24"'), `renderIcon("${slot}") failed`);
  }
  assert(MISSING_ICON_SLOTS.length > 0, 'Supplemental missing-icon slots should be explicitly tracked');

  const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
  assert(html.includes('id="SdfInspectorPreview"') && html.includes('id="SdfInspectorSlider"'), 'Inspector must include SDF slice preview and slider');
  assert(html.includes('id="OpenSdfWorkspace"') && html.includes('id="SdfWorkspace"'), 'Inspector must open full SDF Preview & Baker workspace');

  // Verify zero And-compound identifiers across all GeometryCompiler JS files
  const scanFiles = [
    'Studio.js', 'IconRegistry.js', 'SdfWorkspace.js', 'GeometryEditor.js', 'IconCapture.js', 'UvWorkspace.js',
    ...fs.readdirSync(path.join(__dirname, 'Engine')).filter((f) => f.endsWith('.js')).map((f) => path.join('Engine', f)),
  ];
  const andRegex = /\b[A-Za-z0-9_]*[a-z]And[A-Z][A-Za-z0-9_]*\b/g;
  for (const rel of scanFiles) {
    const src = fs.readFileSync(path.join(__dirname, rel), 'utf8');
    const hits = src.match(andRegex) || [];
    assert.equal(hits.length, 0, `Forbidden And-compound identifier(s) in ${rel}: ${hits.join(', ')}`);
  }
});

console.log(`\nALL ${passed} GEOMETRY COMPILER VALIDATION SUITES PASSED.`);
