// Node checks for the GPU paint session's pure logic: dab instance packing,
// renderer support detection, and the CPU-session factory fallback. The GL
// passes themselves need a browser (see tests/ for the Playwright specs);
// everything that can be decided without a context is decided here.
//
//   node scripts/verify-gpu-packing.mjs
import assert from "node:assert";
import {
  digitalBrushDabs,
  normalizeDigitalBrush,
} from "../src/digitalBrush.js";
import {
  createDigitalPaintSession,
  digitalPaintGpuSupportError,
  packDabInstances,
} from "../src/digitalPaintGpu.js";

const brush = normalizeDigitalBrush({ color: "#d78c59", size: 24 });
const points = [
  [0.4, 0.4, 1],
  [0.9, 0.55, 0.8],
  [1.4, 0.45, 0.6],
];
const dabs = digitalBrushDabs(points, brush, 7, 256, brush.color, true);
assert.ok(dabs.length > 2, "expected a multi-dab stroke");

const packed = packDabInstances(dabs);
assert.equal(packed.count, dabs.length);
assert.equal(packed.dab0.length, dabs.length * 4);
assert.equal(packed.dab1.length, dabs.length * 4);
dabs.forEach((dab, i) => {
  // Instance attributes are Float32 by construction.
  assert.equal(packed.dab0[i * 4], Math.fround(dab.x));
  assert.equal(packed.dab0[i * 4 + 1], Math.fround(dab.y));
  assert.equal(packed.dab0[i * 4 + 2], Math.fround(dab.radius));
  assert.equal(packed.dab0[i * 4 + 3], Math.fround(dab.opacity));
  assert.equal(packed.dab1[i * 4], Math.fround(dab.angle));
  const n = parseInt(dab.color.slice(1), 16);
  assert.equal(packed.dab1[i * 4 + 1], Math.fround(((n >> 16) & 255) / 255));
  assert.equal(packed.dab1[i * 4 + 2], Math.fround(((n >> 8) & 255) / 255));
  assert.equal(packed.dab1[i * 4 + 3], Math.fround((n & 255) / 255));
});

// Scalar/erase colours arrive as plain hex strings and pack identically.
const grey = packDabInstances([{ x: 1, y: 2, radius: 3, opacity: 0.5, angle: 0, color: "#808080" }]);
assert.deepEqual(
  [...grey.dab1.slice(1)],
  [Math.fround(128 / 255), Math.fround(128 / 255), Math.fround(128 / 255)],
);

assert.ok(digitalPaintGpuSupportError(null) instanceof Error);
assert.ok(digitalPaintGpuSupportError({ capabilities: { isWebGL2: false } }) instanceof Error);
assert.equal(digitalPaintGpuSupportError({ capabilities: { isWebGL2: true } }), null);

// Without WebGL2 the factory hands back the CPU session untouched; with a
// renderer that claims WebGL2 but cannot build the GPU session (no usable
// geometry here), construction failure still lands on the CPU session.
const cpu = createDigitalPaintSession(null, null);
assert.equal(typeof cpu.update, "function");
assert.equal(typeof cpu.dispose, "function");
const fallenBack = createDigitalPaintSession({ capabilities: { isWebGL2: true } }, null);
assert.equal(typeof fallenBack.update, "function");

console.log("verify-gpu-packing: dab packing, support detection and CPU fallback OK");
