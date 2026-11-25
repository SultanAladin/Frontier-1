import assert from 'node:assert/strict';
import test from 'node:test';

import { computeChemicalStains } from '../src/geology/chemical-staining.js';
import { generateRockFormation } from '../src/geology/generator.js';

function makeSlopeMesh() {
  const N = 25;
  const pos = [], nor = [], col = [], idx = [];
  for (let j = 0; j < N; j++) {
    for (let i = 0; i < N; i++) {
      pos.push(i * 0.4, 6 - j * 0.32, j * 0.4);
      nor.push(0, 0.78, 0.62);
      col.push(1, 0.35, 0.1, 0); // (ao, cavity, edge, .)
    }
  }
  for (let j = 0; j < N - 1; j++) {
    for (let i = 0; i < N - 1; i++) {
      const a = j * N + i, b = a + 1, c = a + N, d = c + 1;
      idx.push(a, c, b, b, c, d);
    }
  }
  return {
    positions: new Float32Array(pos),
    normals: new Float32Array(nor),
    colors: new Float32Array(col),
    indices: new Uint32Array(idx),
    N,
  };
}

test('Chemical stains accumulate downslope by steepest-descent flow (deterministic)', () => {
  const mesh = makeSlopeMesh();
  const { N } = mesh;
  const r = computeChemicalStains(mesh, { amount: 1, iron: 0.9, copper: 0, seed: 7 });

  assert.equal(r.stains.length, (mesh.positions.length / 3) * 2, 'vec2 stain per vertex');
  for (let i = 0; i < r.stains.length; i++) {
    assert.ok(Number.isFinite(r.stains[i]) && r.stains[i] >= 0 && r.stains[i] <= 1, 'stain in [0,1]');
  }
  assert.equal(r.stats.maxCu, 0, 'No copper content -> no patina');
  assert.ok(r.stats.maxFe > 0.3, 'Iron staining present');

  const rowAvgFe = (j) => {
    let s = 0;
    for (let i = 0; i < N; i++) s += r.stains[(j * N + i) * 2];
    return s / N;
  };
  assert.ok(rowAvgFe(23) > rowAvgFe(1) * 1.25,
    `Flow accumulation must enrich downhill rows (top ${rowAvgFe(1).toFixed(3)} vs bottom ${rowAvgFe(23).toFixed(3)})`);

  const r2 = computeChemicalStains(mesh, { amount: 1, iron: 0.9, copper: 0, seed: 7 });
  assert.deepEqual(Buffer.from(r.stains.buffer), Buffer.from(r2.stains.buffer), 'Deterministic for fixed seed/params');

  const rOff = computeChemicalStains(mesh, { amount: 0, iron: 0.9, copper: 0.9, seed: 7 });
  assert.ok(rOff.stains.every((v) => v === 0), 'amount 0 -> chemically pristine');

  const rCu = computeChemicalStains(mesh, { amount: 1, iron: 0, copper: 0.9, seed: 7 });
  assert.equal(rCu.stats.maxFe, 0, 'No iron content -> no rust');
  assert.ok(rCu.stats.maxCu > 0.1, 'Copper veining produces patina somewhere');
});

test('Generator integration: formation carries baked stains when chemAmount > 0, null by default', () => {
  const res = generateRockFormation({
    archetype: 'cliff-wall', seed: 4029, quality: 'fast',
    chemAmount: 0.85, ironContent: 0.8, copperContent: 0.45,
  });
  const vCount = res.mesh.positions.length / 3;
  assert.ok(res.mesh.stains instanceof Float32Array, 'mesh.stains present');
  assert.equal(res.mesh.stains.length, vCount * 2, 'one (fe, cu) pair per vertex');
  assert.ok(res.stats.chemicalStaining, 'stats report chemical staining');
  assert.ok(res.stats.chemicalStaining.stainedFracFe > 0.05, 'meaningful rust coverage at strong settings');
  assert.ok(res.stats.chemicalStaining.maxFe > 0.5, 'deep rust cores exist');
  for (let i = 0; i < res.mesh.stains.length; i += 97) {
    assert.ok(Number.isFinite(res.mesh.stains[i]) && res.mesh.stains[i] >= 0 && res.mesh.stains[i] <= 1);
  }

  const plain = generateRockFormation({ archetype: 'cliff-wall', seed: 4029, quality: 'fast' });
  assert.equal(plain.mesh.stains, null, 'API default: chemical weathering off');
  assert.equal(plain.stats.chemicalStaining, null, 'API default: no chem stats');
});
