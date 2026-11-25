import test from 'node:test';
import assert from 'node:assert/strict';
import { addRegion, addShapeLayer, bakeWorld, createWorld, maskFingerprint, migrateWorld, normalizeBounds, paintMask, regionMask, sampleMask, terrainImportMapping, terrainImportMask, visibleTileKeys } from '../src/world/world-map.js';

test('painted masks remain layer-local and bounds crop the correct map area', () => {
  const w = createWorld(), a = addShapeLayer(w), b = addShapeLayer(w);
  paintMask(a, .25, .5, .75, .5, .12);
  assert.deepEqual(Object.keys(b.tiles), []);
  const r = addRegion(w, a.id, normalizeBounds(.2, .3, .8, .7));
  const crop = regionMask(w, r, 33);
  assert.ok(crop[16 * 33 + 16] > 240);
  assert.equal(crop[0], 0);
  paintMask(a, .5, .5, .5, .5, .05, true);
  assert.equal(regionMask(w, r, 33)[16 * 33 + 16], 0);
});

test('world preview combines baked terrain only within its selected mask and bounds', () => {
  const w = createWorld(), layer = addShapeLayer(w);
  paintMask(layer, .5, .5, .5, .5, .25);
  const r = addRegion(w, layer.id, normalizeBounds(.25, .25, .75, .75));
  r.bake = { size: 2, heights: new Float32Array([40, 40, 40, 40]) };
  const preview = bakeWorld(w, 65);
  assert.ok(preview.heights[32 * 65 + 32] > 39);
  assert.equal(preview.heights[0], 0);
  layer.visible = false;
  assert.equal(bakeWorld(w, 65).heights[32 * 65 + 32], 0);
});

test('terrain height bakes use the captured bedrock and sediment, not a generated substitute', async () => {
  const { bakeTerrainDocument } = await import('../src/app/world-terrain-session.js');
  const terrain = new Float32Array([
    1, 2, 3, 10, 4, 5, 6, 20,
    7, 8, 9, 30, 10, 11, 12, 40,
  ]);
  assert.deepEqual(Array.from(bakeTerrainDocument({ size: 2, terrain }, 2).heights), [6, 15, 24, 33]);
});

test('shape edits mark the stored mask revision stale', () => {
  const world = createWorld(), layer = addShapeLayer(world);
  const revision = layer.revision;
  paintMask(layer, .2, .2, .2, .2, .03);
  assert.ok(layer.revision > revision);
});


test('sparse masks paint across tile seams and at large negative or positive coordinates', () => {
  const world = createWorld(), layer = addShapeLayer(world);
  paintMask(layer, -.04, .5, .04, .5, .08);
  assert.ok(sampleMask(layer, -.01, .5) > 245);
  assert.ok(sampleMask(layer, .01, .5) > 245);
  paintMask(layer, 1000.3, -987.2, 1000.3, -987.2, .03);
  assert.ok(sampleMask(layer, 1000.3, -987.2) > 245);
  assert.ok(visibleTileKeys(world).includes('1000,-988'));
  const far = addRegion(world, layer.id, normalizeBounds(1000.2, -987.3, 1000.4, -987.1));
  assert.ok(regionMask(world, far, 33)[16 * 33 + 16] > 245);
  assert.ok(Object.keys(layer.tiles).length < 10, 'untouched intervening world allocates no tiles');
});

test('legacy one-tile worlds migrate without changing regions or stored documents', () => {
  const mask = new Uint8Array(256 * 256); mask[128 * 256 + 128] = 255;
  const doc = { terrain: new Float32Array([3]) };
  const old = { version: 1, nextLayer: 2, nextRegion: 2, layers: [{ id: 'shape-1', name: 'Old', visible: true, mask }], regions: [{ id: 'terrain-1', layerId: 'shape-1', bounds: { x: 0, y: 0, width: 1, height: 1 }, document: doc }] };
  const migrated = migrateWorld(old);
  assert.equal(migrated.version, 2);
  assert.equal(migrated.layers[0].tiles['0,0'][128 * 256 + 128], 255);
  assert.equal(migrated.regions[0].document, doc);
  assert.equal(old.version, 1, 'migration does not mutate the stored legacy world');
});

test('adjacent world bakes share the same samples at their tile boundary', () => {
  const world = createWorld(), layer = addShapeLayer(world);
  paintMask(layer, .9, .5, 1.1, .5, .1);
  const left = bakeWorld(world, 17, {x:0,y:0,width:1,height:1});
  const right = bakeWorld(world, 17, {x:1,y:0,width:1,height:1});
  for (let y=0; y<17; y++) assert.equal(left.coverage[y*17+16], right.coverage[y*17]);
});


test('distant edits on the same shape do not invalidate a terrain region mask', () => {
  const world = createWorld(), layer = addShapeLayer(world);
  paintMask(layer, .5, .5, .5, .5, .15);
  const region = addRegion(world, layer.id, normalizeBounds(.25, .25, .75, .75));
  const before = maskFingerprint(regionMask(world, region));
  paintMask(layer, 100, -40, 100, -40, .1);
  assert.equal(maskFingerprint(regionMask(world, region)), before);
  paintMask(layer, .5, .5, .5, .5, .05, true);
  assert.notEqual(maskFingerprint(regionMask(world, region)), before);
});


test('rectangular region is padded in a square simulation and crops back without stretching', async () => {
  const world = createWorld(), layer = addShapeLayer(world);
  paintMask(layer, .3, .5, 1.7, .5, .4);
  const region = addRegion(world, layer.id, normalizeBounds(0, 0, 2, 1));
  const mapping = terrainImportMapping(region);
  assert.deepEqual(mapping, { x: 0, y: .25, width: 1, height: .5, mode: 'padded' });
  const mask = terrainImportMask(world, region, 65);
  assert.equal(mask[2 * 65 + 32], 0, 'padding is empty');
  assert.ok(mask[32 * 65 + 32] > 240, 'authored shape survives inside padding');
  const raw = new Float32Array(9 * 9 * 4);
  for (let y = 0; y < 9; y++) for (let x = 0; x < 9; x++) raw[(y * 9 + x) * 4] = y * 10 + x;
  const { bakeTerrainDocument } = await import('../src/app/world-terrain-session.js');
  const baked = bakeTerrainDocument({ size: 9, terrain: raw }, 3, mapping);
  assert.deepEqual(Array.from(baked.heights), [20, 24, 28, 40, 44, 48, 60, 64, 68]);
  region.importMapping = 'padded'; region.document = { size: 9 };
  assert.deepEqual(terrainImportMapping(region), mapping, 'mapping remains stable after saving');
});

test('legacy rectangular documents retain their existing square mapping', () => {
  const region = { bounds: { x:0, y:0, width:4, height:1 }, document: { size:256 } };
  assert.deepEqual(terrainImportMapping(region), {x:0, y:0, width:1, height:1, mode:'stretch'});
});
