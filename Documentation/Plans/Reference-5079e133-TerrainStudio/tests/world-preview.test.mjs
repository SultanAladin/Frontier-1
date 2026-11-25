import test from 'node:test';
import assert from 'node:assert/strict';
import { addShapeLayer, addRegion, bakeWorld, createWorld, normalizeBounds, paintMask } from '../src/world/world-map.js';
import { createTerrainTileGeometry, PREVIEW_TILE_SPAN, PREVIEW_HEIGHT_SCALE, WORLD_WATER_VERTEX, WORLD_WATER_FRAGMENT } from '../src/app/world-preview.js';

test('3D terrain tile is real indexed mesh geometry built from baked terrain heights', () => {
  const world = createWorld(), shape = addShapeLayer(world);
  paintMask(shape, .5, .5, .5, .5, .4);
  const region = addRegion(world, shape.id, normalizeBounds(0, 0, 1, 1));
  region.bake = { size: 2, heights: new Float32Array([40, 40, 40, 40]) };
  const data = bakeWorld(world, 5), geometry = createTerrainTileGeometry(data, 0, 0);
  assert.ok(geometry.index && geometry.index.count === 4 * 4 * 6);
  const center = geometry.attributes.position;
  assert.ok(Math.abs(center.getY(12) - 40 * PREVIEW_HEIGHT_SCALE) < .001);
  assert.equal(center.getX(12), PREVIEW_TILE_SPAN / 2);
  assert.equal(center.getZ(12), PREVIEW_TILE_SPAN / 2);
  assert.ok(center.getY(0) < 0, 'empty land remains below water');
  geometry.dispose();
});

test('adjacent terrain meshes join at the same world-space heights', () => {
  const world = createWorld(), shape = addShapeLayer(world);
  paintMask(shape, .9, .5, 1.1, .5, .2);
  const a = createTerrainTileGeometry(bakeWorld(world, 9, {x:0,y:0,width:1,height:1}), 0, 0);
  const b = createTerrainTileGeometry(bakeWorld(world, 9, {x:1,y:0,width:1,height:1}), 1, 0);
  for (let y = 0; y < 9; y++) {
    const left = y * 9 + 8, right = y * 9;
    assert.equal(a.attributes.position.getX(left), b.attributes.position.getX(right));
    assert.equal(a.attributes.position.getY(left), b.attributes.position.getY(right));
    assert.equal(a.attributes.position.getZ(left), b.attributes.position.getZ(right));
  }
  a.dispose(); b.dispose();
});

test('world water is an animated depth-tested visual shader with a controllable sea level', () => {
  assert.match(WORLD_WATER_VERTEX, /uniform float uTime/);
  assert.match(WORLD_WATER_VERTEX, /uniform float uSeaLevel/);
  assert.match(WORLD_WATER_VERTEX, /world\.y = uSeaLevel/);
  assert.match(WORLD_WATER_FRAGMENT, /cameraPosition/);
});
