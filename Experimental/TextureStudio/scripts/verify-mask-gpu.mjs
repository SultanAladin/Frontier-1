// Node checks for the pure pieces of the GPU mask/projection expansion:
//  1. legacyMaskDabs (maskPaintGpu.js) reproduces, dab for dab, the
//     interpolated radial-dab loop rasterizeMaskPaint performs on canvas.
//  2. packGradientUniforms + the field shader's Gaussian math reproduce
//     compilePointGradient (pointGradient.js) at arbitrary positions.
//  3. screenStencilLayout (screenPaintGpu.js) reproduces stencil()'s
//     bounding-box / downscale arithmetic in screenPaintProjection.js.
// The GL stages themselves are covered by tests/mask-paint-gpu.spec.js and
// the projection cases in tests/digital-paint-gpu.spec.js (browser CI);
// everything decidable without a GL context is decided here.
import assert from "node:assert/strict";
import { legacyMaskDabs, packGradientUniforms } from "../src/maskPaintGpu.js";
import { screenStencilLayout } from "../src/screenPaintGpu.js";
import { compilePointGradient } from "../src/pointGradient.js";

let state = 123456789;
const rnd = () => {
  state = (Math.imul(state, 1103515245) + 12345) >>> 0;
  return state / 4294967296;
};

// --- 1. legacy dab interpolation -------------------------------------------
function cpuLegacyDabs(op, fromPoint) {
  const dabs = [];
  const dab = (x, y) =>
    dabs.push({ x: x * 256, y: y * 256, radius: op.radius * 256 });
  const points = op.points || [];
  if (!fromPoint && points.length) dab(...points[0]);
  for (let i = Math.max(1, fromPoint); i < points.length; i++) {
    const a = points[i - 1],
      b = points[i],
      n = Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35));
    for (let j = 1; j <= n; j++)
      dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
  }
  return dabs;
}
for (let trial = 0; trial < 200; trial++) {
  const count = 1 + Math.floor(rnd() * 24),
    op = {
      radius: 0.01 + rnd() * 0.3,
      points: Array.from({ length: count }, () => [rnd() * 4, rnd()]),
    },
    fromPoint = Math.floor(rnd() * (count + 1));
  assert.deepEqual(legacyMaskDabs(op, fromPoint), cpuLegacyDabs(op, fromPoint));
  assert.deepEqual(legacyMaskDabs(op, 0), cpuLegacyDabs(op, 0));
}
console.log("legacyMaskDabs: 200 randomized strokes match the CPU dab loop");

// --- 2. gradient uniform packing --------------------------------------------
function shaderGradient(packed, position) {
  let r = 0,
    g = 0,
    b = 0,
    total = 0;
  for (let i = 0; i < packed.count; i++) {
    const radius = packed.pointA[i * 4 + 3],
      weight = packed.pointB[i * 4 + 3];
    if (weight <= 0 || radius * radius <= 0) continue;
    const d2 =
      (packed.pointA[i * 4] - position[0]) ** 2 +
      (packed.pointA[i * 4 + 1] - position[1]) ** 2 +
      (packed.pointA[i * 4 + 2] - position[2]) ** 2,
      w = weight * Math.exp(-d2 / (radius * radius));
    total += w;
    r += packed.pointB[i * 4] * w;
    g += packed.pointB[i * 4 + 1] * w;
    b += packed.pointB[i * 4 + 2] * w;
  }
  return total > 1e-8
    ? [r / total, g / total, b / total]
    : [...packed.background];
}
const hex = () =>
  `#${Array.from({ length: 3 }, () =>
    Math.floor(rnd() * 256)
      .toString(16)
      .padStart(2, "0"),
  ).join("")}`;
for (let trial = 0; trial < 60; trial++) {
  const gradient = {
      schema: "alloy.point-gradient.v1",
      space: "object-sphere",
      background: hex(),
      points: Array.from({ length: Math.floor(rnd() * 6) }, (_, i) => ({
        id: `p${i}`,
        position: [rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1],
        color: hex(),
        radius: trial % 7 === 0 ? 0 : 0.1 + rnd() * 2,
        weight: trial % 5 === 0 ? 0 : rnd(),
      })),
    },
    packed = packGradientUniforms(gradient),
    sample = compilePointGradient(gradient);
  assert.equal(packed.count, gradient.points.length);
  for (let s = 0; s < 40; s++) {
    const position = [rnd() * 2 - 1, rnd() * 2 - 1, rnd() * 2 - 1],
      expected = sample(position),
      actual = shaderGradient(packed, position);
    for (let c = 0; c < 3; c++)
      assert.ok(
        Math.abs(actual[c] - expected[c]) < 1e-5,
        `gradient mismatch ${actual[c]} vs ${expected[c]}`,
      );
  }
}
console.log("packGradientUniforms: shader math matches compilePointGradient");

// --- 3. stencil layout --------------------------------------------------------
function cpuStencilLayout(dabs, brush, size) {
  const [w, h] = size;
  if (!dabs.length) return null;
  const ext = (d) => Math.hypot(d.radius, d.radius * brush.roundness),
    x0 = Math.max(0, Math.floor(Math.min(...dabs.map((d) => d.x - ext(d))) - 1)),
    y0 = Math.max(0, Math.floor(Math.min(...dabs.map((d) => d.y - ext(d))) - 1)),
    x1 = Math.min(w, Math.ceil(Math.max(...dabs.map((d) => d.x + ext(d))) + 1)),
    y1 = Math.min(h, Math.ceil(Math.max(...dabs.map((d) => d.y + ext(d))) + 1));
  if (x1 <= x0 || y1 <= y0) return null;
  const factor = Math.min(
      1,
      2048 / Math.max(x1 - x0, y1 - y0),
      Math.sqrt(1048576 / ((x1 - x0) * (y1 - y0))),
    ),
    width = Math.max(1, Math.floor((x1 - x0) * factor)),
    height = Math.max(1, Math.floor((y1 - y0) * factor));
  return { x0, y0, x1, y1, width, height, sx: width / (x1 - x0), sy: height / (y1 - y0) };
}
for (let trial = 0; trial < 200; trial++) {
  const size = [320 + Math.floor(rnd() * 4000), 240 + Math.floor(rnd() * 3000)],
    brush = { roundness: 0.05 + rnd() },
    dabs = Array.from({ length: Math.floor(rnd() * 30) }, () => ({
      x: rnd() * size[0],
      y: rnd() * size[1],
      radius: 1 + rnd() * 240,
    }));
  assert.deepEqual(
    screenStencilLayout(dabs, brush, size),
    cpuStencilLayout(dabs, brush, size),
  );
}
assert.equal(screenStencilLayout([], { roundness: 1 }, [100, 100]), null);
console.log("screenStencilLayout: 200 randomized layouts match stencil()");
console.log("verify-mask-gpu: all checks OK");
