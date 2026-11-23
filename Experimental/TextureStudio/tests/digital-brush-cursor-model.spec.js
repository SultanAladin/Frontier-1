import { setRenderTargetViewport } from "../src/renderTargetViewport.js";
import { test, expect } from "@playwright/test";
import {
  appendDigitalBrushSample,
  digitalBrushDabs,
  normalizeDigitalBrush,
} from "../src/digitalBrush.js";
import {
  digitalBrushCursor,
  digitalPointerPressure,
  uvCanvasPoint,
  uvAtCanvasPoint,
} from "../src/digitalBrushCursor.js";
import { createTextureTeapotGeometry } from "../src/textureSurfaceModel.js";
import { uvTopology, uvSurfaceHit } from "../src/textureUVGeometry.js";
const geometry = createTextureTeapotGeometry(20);
const triangle = uvTopology(geometry).triangles.find(
  (t) => t.part.id === "body" && t.chartId === 5,
);
const point = [0, 1].map((i) =>
  triangle.points.reduce((n, p) => n + p[i] / 3, 0),
);
const hit = uvSurfaceHit(geometry, ...point);
const brush = normalizeDigitalBrush({
  size: 24,
  sizePressure: 0,
  hardness: 1,
  color: "#ff1030",
});
const options = {
  hit,
  geometry,
  target: { kind: "object", partId: "body" },
  brush,
  channels: { baseColor: brush.color },
  seed: 456,
};

test("stabilization smooths interior points without pulling either endpoint off the cursor", () => {
  const points = [];
  [
    [0.08, 0.4, 1],
    [0.12, 0.44, 0.7],
    [0.18, 0.4, 0.8],
  ].forEach((p) => appendDigitalBrushSample(points, p, { stabilization: 1 }));
  expect(points[0]).toEqual([0.08, 0.4, 1]);
  expect(points.at(-1)).toEqual([0.18, 0.4, 0.8]);
  expect(points[1][0]).toBeCloseTo(0.125);
  expect(points[1][1]).toBeCloseTo(0.42);
  expect(points[1][2]).toBe(0.7);
  const dab = digitalBrushDabs(points, { ...brush, stabilization: 1 }, 42).at(
    -1,
  );
  expect(dab.x).toBeCloseTo(0.18 * 256);
  expect(dab.y).toBeCloseTo(0.4 * 256);
});
test("dense sub-pixel samples coalesce but keep the precise live endpoint and contact pressure", () => {
  const points = [
    [0.08, 0.4, 1],
    [0.12, 0.44, 0.7],
  ];
  const final = [0.12001, 0.44002, 0.3];
  expect(appendDigitalBrushSample(points, final, brush)).toBe(true);
  expect(points).toEqual([[0.08, 0.4, 1], final]);
  expect(appendDigitalBrushSample(points, final, brush)).toBe(false);
});
test("smoothing cannot push an interior point outside its chart/face", () => {
  const points = [
    [0.08, 0.4, 1],
    [0.12, 0.44, 0.7],
  ];
  appendDigitalBrushSample(
    points,
    [0.18, 0.4, 0.8],
    { stabilization: 1 },
    () => false,
  );
  expect(points[1]).toEqual([0.12, 0.44, 0.7]);
});
test("UV picking converts CSS/fractional screen rectangles to canvas logical pixels, independently of DPR", () => {
  const size = { width: 796, height: 454 },
    rect = { left: 292.25, top: 331.59375, width: 636.8, height: 363.2 },
    camera = { scale: 250, x: 10, y: 350 };
  const logical = [35, 262.5],
    client = [rect.left + logical[0] * 0.8, rect.top + logical[1] * 0.8];
  const p = uvCanvasPoint(...client, rect, size),
    uv = uvAtCanvasPoint(p, camera);
  expect(p[0]).toBeCloseTo(logical[0], 10);
  expect(p[1]).toBeCloseTo(logical[1], 10);
  expect(uv[0]).toBeCloseTo(0.1, 10);
  expect(uv[1]).toBeCloseTo(0.35, 10);
  expect(
    uvAtCanvasPoint([client[0] - rect.left, client[1] - rect.top], camera)[0],
  ).not.toBeCloseTo(0.1);
});
test("mouse, contact and pen hover pressure produce a useful forecast without converting zero contact to full pressure", () => {
  expect(digitalPointerPressure({ pointerType: "mouse", pressure: 0.5 })).toBe(
    1,
  );
  expect(
    digitalPointerPressure(
      { pointerType: "pen", pressure: 0.2, buttons: 1 },
      true,
    ),
  ).toBe(0.2);
  expect(
    digitalPointerPressure(
      { pointerType: "pen", pressure: 0, buttons: 0 },
      true,
    ),
  ).toBe(1);
  expect(
    digitalPointerPressure({ pointerType: "pen", pressure: 0, buttons: 1 }),
  ).toBe(0.02);
});
test("hover footprint exactly predicts the seeded textured, rotated, pressure-sized first dab", () => {
  const b = {
    ...brush,
    shape: "square",
    roundness: 0.3,
    rotation: 41,
    grain: "checker",
    grainAmount: 0.8,
    sizePressure: 0.6,
    opacityPressure: 0.8,
    sizeJitter: 0.2,
    scatter: 0.1,
    rotationJitter: 0.3,
    dabHue: 0.3,
  };
  const cursor = digitalBrushCursor({ ...options, brush: b, pressure: 0.4 });
  expect(cursor.dab).toEqual(
    digitalBrushDabs(
      [[...hit.uvPosition, 0.4]],
      b,
      456,
      256,
      brush.color,
      true,
    )[0],
  );
  expect(cursor.region).toEqual({ kind: "island", partId: "body", id: 5 });
  expect(cursor.brush.grain).toBe("checker");
});
test("a gesture footprint uses the actual final dab, including follow, taper and dynamics", () => {
  const points = [
    [...hit.uvPosition, 1],
    [hit.uvPosition[0] + 0.01, hit.uvPosition[1] + 0.008, 0.5],
  ];
  const b = {
    ...brush,
    follow: true,
    taperEnd: 0.2,
    grain: "noise",
    grainAmount: 0.7,
  };
  const cursor = digitalBrushCursor({ ...options, brush: b, points });
  expect(cursor.dab).toEqual(digitalBrushDabs(points, b, 456).at(-1));
  expect(cursor.dab.angle).toBeCloseTo(Math.atan2(0.008, 0.01));
});
test("cursor clipping shares the actual part/face/chart scopes; misses and zero work have no footprint", () => {
  expect(digitalBrushCursor({ ...options, hit: null })).toBeNull();
  expect(
    digitalBrushCursor({
      ...options,
      target: { kind: "object", partId: "lid" },
    }),
  ).toBeNull();
  expect(
    digitalBrushCursor({
      ...options,
      target: { kind: "island", partId: "body", id: 4 },
    }),
  ).toBeNull();
  const face = { kind: "face", partId: "body", id: hit.faceIndex };
  expect(digitalBrushCursor({ ...options, target: face }).region).toEqual(face);
  for (const key of ["size", "opacity", "flow"])
    expect(
      digitalBrushCursor({ ...options, brush: { ...brush, [key]: 0 } }),
    ).toBeNull();
  expect(
    digitalBrushCursor({
      ...options,
      brush: { ...brush, tool: "fill", size: 0, flow: 0 },
    }).region,
  ).toEqual(options.target);
});
test("mask and eraser previews use their real monochrome/neutral tip, not brush colour dynamics", () => {
  const b = { ...brush, dabHue: 1, strokeBrightness: 1 };
  expect(
    digitalBrushCursor({ ...options, brush: b, mask: true, ink: 0 }).dab.color,
  ).toBe("#000000");
  expect(
    digitalBrushCursor({ ...options, brush: { ...b, tool: "erase" } }).dab
      .color,
  ).toBe("#d5dce2");
  expect(
    digitalBrushCursor({
      ...options,
      brush: { ...brush, tool: "fill" },
      mask: true,
      ink: 0,
    }).color,
  ).toBe("#000000");
});

test("off-screen physical viewports do not multiply device scale twice", () => {
  for (const dpr of [1, 1.25, 1.5, 2]) {
    let actual;
    setRenderTargetViewport(
      {
        getPixelRatio: () => dpr,
        setViewport(x, y, w, h) {
          actual = [x * dpr, y * dpr, w * dpr, h * dpr];
        },
      },
      1024,
      256,
    );
    expect(actual).toEqual([0, 0, 1024, 256]);
  }
});
test("an over-budget draft footprint is hidden instead of crashing the render loop", () => {
  const points = Array.from({ length: 512 }, (_, i) => [
    i % 2 ? 0.238 : 0.012,
    0.375,
    1,
  ]);
  expect(
    digitalBrushCursor({ ...options, brush: { ...brush, size: 1 }, points }),
  ).toBeNull();
});
