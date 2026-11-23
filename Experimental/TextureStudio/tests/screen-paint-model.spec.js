import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  normalizeScreenProjection,
  normalizeScreenPoints,
  uvScreenProjection,
  screenScopeContains,
} from "../src/screenPaintProjection.js";
import { normalizeDigitalBrush } from "../src/digitalBrush.js";
import {
  normalizeDigitalPaint,
  DIGITAL_PAINT_SCHEMA,
} from "../src/textureDigitalPaint.js";
import {
  normalizeMaskPaint,
  normalizeMaskRegion,
} from "../src/textureMaskPaint.js";
import {
  AUTHORING_UV_SET,
  createTextureTeapotGeometry,
} from "../src/textureSurfaceModel.js";
import { validateDigitalPaintGeometry } from "../src/textureDigitalPaint.js";
const projection = {
    version: 1,
    kind: "lit",
    size: [800, 450],
    matrix: new THREE.Matrix4().toArray(),
  },
  brush = normalizeDigitalBrush({ size: 24 }),
  op = {
    id: "screen",
    kind: "paint",
    region: { kind: "surface" },
    seed: 3,
    brush,
    channels: { baseColor: "#ff1030" },
    points: [[700 / 256, 400 / 256, 0.8]],
    projection,
  };
test("screen operations round-trip camera, pressure and all-surface scope without misinterpreting samples as UVs", () => {
  const saved = normalizeDigitalPaint({
    schema: DIGITAL_PAINT_SCHEMA,
    uvSet: AUTHORING_UV_SET,
    operations: [op],
  });
  expect(saved.operations[0].projection).toEqual(projection);
  expect(saved.operations[0].region).toEqual({ kind: "surface" });
  const g = createTextureTeapotGeometry(20);
  expect(() => validateDigitalPaintGeometry(saved, g)).not.toThrow();
  g.dispose();
  expect(normalizeDigitalPaint(JSON.parse(JSON.stringify(saved)))).toEqual(
    saved,
  );
});
test("digital mask projection round-trips independently of the original mask source", () => {
  const paint = normalizeMaskPaint({
    uvSet: AUTHORING_UV_SET,
    operations: [
      {
        kind: "brush",
        region: op.region,
        value: 0,
        opacity: 1,
        radius: 0.03,
        hardness: 1,
        points: op.points.map((p) => p.slice(0, 2)),
        digital: {
          brush,
          seed: 3,
          operation: "erase",
          pressure: [0.8],
          projection,
        },
      },
    ],
  });
  expect(paint.operations[0].digital.projection).toEqual(projection);
  expect(paint.operations[0].digital.operation).toBe("erase");
  expect(() => normalizeMaskRegion({ kind: "surface" })).toThrow();
});
test("screen projection rejects malformed matrices, sizes, singular views and nonfinite samples", () => {
  for (const p of [
    { ...projection, version: 9 },
    { ...projection, size: [1e9, 10] },
    { ...projection, matrix: Array(16).fill(0) },
    { ...projection, matrix: [NaN, ...projection.matrix.slice(1)] },
  ])
    expect(() => normalizeScreenProjection(p)).toThrow();
  expect(() => normalizeScreenPoints([[Infinity, 0, 1]], projection)).toThrow();
  expect(() => normalizeScreenPoints([[0, 0, 2]], projection)).toThrow();
  expect(() =>
    normalizeDigitalPaint({
      schema: DIGITAL_PAINT_SCHEMA,
      uvSet: AUTHORING_UV_SET,
      operations: [{ ...op, kind: "fill" }],
    }),
  ).toThrow();
});
test("UV screen transform uses the displayed CSS rectangle and preserves explicit face/part constraints", () => {
  const p = uvScreenProjection(
    { x: 50, y: 100, scale: 200 },
    { width: 400, height: 200 },
    { width: 320, height: 160 },
  );
  expect(p.matrix).toEqual([160, 0, 0, -160, 40, 80]);
  expect(p.size).toEqual([320, 160]);
  const triangle = {
    faceIndex: 10,
    chartId: 5,
    part: { id: "body", tile: 1001 },
  };
  expect(screenScopeContains({ kind: "surface" }, triangle)).toBe(true);
  expect(
    screenScopeContains({ kind: "face", partId: "body", id: 10 }, triangle),
  ).toBe(true);
  expect(screenScopeContains({ kind: "object", partId: "lid" }, triangle)).toBe(
    false,
  );
});
test("legacy UV strokes keep their saved units and chart clipping instead of being migrated silently", () => {
  const legacy = {
    ...op,
    region: { kind: "island", partId: "body", id: 5 },
    points: [[0.125, 0.375, 1]],
  };
  delete legacy.projection;
  const saved = normalizeDigitalPaint({
    schema: DIGITAL_PAINT_SCHEMA,
    uvSet: AUTHORING_UV_SET,
    operations: [legacy],
  });
  expect(saved.operations[0].projection).toBeUndefined();
  expect(saved.operations[0].points).toEqual(legacy.points);
});
