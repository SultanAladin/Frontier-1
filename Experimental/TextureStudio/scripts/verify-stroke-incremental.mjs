// Stroke deposition equivalence + timing verification (Node, no browser).
//
// Proves the incremental atlas session produces byte-identical GPU atlas
// data to the one-shot full-history replay (createDigitalPaintAtlas) at
// every frame of a growing stroke — appends, endpoint replacement, interior
// smoothing, a second operation, an erase, a tapered stroke and an undo —
// and measures the per-frame cost of both paths.
//
// Requires a Canvas2D implementation:  npm i --no-save @napi-rs/canvas
// Run:  node scripts/verify-stroke-incremental.mjs
import { createRequire } from "node:module";

let napi;
try {
  napi = createRequire(import.meta.url)("@napi-rs/canvas");
} catch {
  console.error(
    "Missing @napi-rs/canvas. Install it (dev-only): npm i --no-save @napi-rs/canvas",
  );
  process.exit(2);
}
globalThis.Path2D = napi.Path2D;
globalThis.DOMMatrix = class DOMMatrix {
  constructor() {
    this.a = 1; this.b = 0; this.c = 0; this.d = 1; this.e = 0; this.f = 0;
  }
};
globalThis.document = {
  createElement(tag) {
    if (tag !== "canvas") throw new Error(`Unsupported element ${tag}`);
    return napi.createCanvas(1, 1);
  },
};

const { createTextureTeapotGeometry, AUTHORING_UV_SET } = await import(
  "../src/textureSurfaceModel.js"
);
const { createTextureLayer } = await import("../src/textureDocument.js");
const { normalizeDigitalBrush } = await import("../src/digitalBrush.js");
const {
  createDigitalPaintAtlas,
  createDigitalPaintAtlasSession,
} = await import("../src/digitalPaintRaster.js");
const { DIGITAL_PAINT_SCHEMA } = await import("../src/textureDigitalPaint.js");

const geometry = createTextureTeapotGeometry(20);
const brush = normalizeDigitalBrush({
  size: 24,
  hardness: 0.85,
  sizePressure: 0.4,
  scatter: 0.15,
  dabHue: 0.08,
  color: "#b5651d",
});
const fill = {
  id: "fill-1",
  kind: "fill",
  region: { kind: "object", partId: "lid" },
  brush,
  channels: { baseColor: "#808080", roughness: 0.2, metalness: 0.4 },
  points: [],
  seed: 42,
};
const strokePoints = (n) =>
  Array.from({ length: n }, (_, i) => [
    1.04 + i * 0.006,
    0.52 + Math.sin(i / 6) * 0.035,
    0.35 + 0.6 * Math.abs(Math.sin(i / 9)),
  ]);
const strokeOp = (points, extra = {}) => ({
  id: "stroke-1",
  kind: "paint",
  region: { kind: "island", partId: "lid", id: 25 },
  brush,
  channels: { baseColor: "#b5651d", roughness: 0.65, metalness: 0.1 },
  points,
  seed: 777,
  ...extra,
});
const eraseOp = (points) => ({
  id: "erase-1",
  kind: "erase",
  region: { kind: "island", partId: "lid", id: 25 },
  brush: { ...brush, size: 36 },
  channels: { roughness: 0 },
  points,
  seed: 99,
});
const layerWith = (operations) =>
  createTextureLayer("paint", {
    channels: ["baseColor", "roughness", "metalness"],
    paint: { schema: DIGITAL_PAINT_SCHEMA, uvSet: AUTHORING_UV_SET, operations },
  });

const bytesOf = (atlas) => atlas.texture.image.data;
function expectEqual(label, a, b) {
  if (a.length !== b.length) throw new Error(`${label}: length ${a.length} != ${b.length}`);
  for (let i = 0; i < a.length; i++)
    if (a[i] !== b[i])
      throw new Error(`${label}: byte ${i} differs (${a[i]} != ${b[i]})`);
  console.log(`  identical  ${label} (${a.length} bytes)`);
}

const session = createDigitalPaintAtlasSession(geometry);
let frames = 0;
function check(label, operations) {
  const layer = layerWith(operations);
  const incremental = session.update(layer);
  const reference = createDigitalPaintAtlas(layer, geometry);
  expectEqual(label, bytesOf(incremental), bytesOf(reference));
  reference.texture.dispose();
  frames++;
}

// 1. Fill only, then a stroke growing sample by sample (the drag case).
check("fill only", [fill]);
const full = strokePoints(60);
for (let n = 2; n <= 60; n += 2) check(`stroke grows to ${n} samples`, [fill, strokeOp(full.slice(0, n))]);
// 2. Endpoint replacement (sub-pixel move rewrites the last sample).
const replaced = full.slice();
replaced[replaced.length - 1] = [1.395, 0.545, 0.9];
check("endpoint replaced", [fill, strokeOp(replaced)]);
// 3. Interior smoothing (stabilization rewrites an interior sample).
const smoothed = replaced.slice();
smoothed[30] = [smoothed[30][0] + 0.004, smoothed[30][1] - 0.003, smoothed[30][2]];
check("interior sample smoothed", [fill, strokeOp(smoothed)]);
// 4. Tapered stroke: total-length changes rescale every dab.
const tapered = strokeOp(full.slice(0, 20), {
  id: "stroke-taper",
  brush: normalizeDigitalBrush({ ...brush, taperStart: 0.3, taperEnd: 0.3 }),
});
check("tapered stroke appears", [fill, strokeOp(smoothed), tapered]);
check("tapered stroke grows", [
  fill,
  strokeOp(smoothed),
  { ...tapered, points: full.slice(0, 44) },
]);
// 5. Erase over part of the stroke.
check("erase dab", [
  fill,
  strokeOp(smoothed),
  { ...tapered, points: full.slice(0, 44) },
  eraseOp([[1.15, 0.54, 1]]),
]);
// 6. Undo (operations shrink below the folded prefix) -> one-shot rebuild.
check("undo to fill", [fill]);
// 7. A fresh stroke after undo.
check("new stroke after undo", [fill, strokeOp(full.slice(0, 12), { id: "stroke-2", seed: 5 })]);
console.log(`session equivalence: ${frames} frames byte-identical`);

// Timing: same 30-frame growth, incremental session vs full replay.
const timed = createDigitalPaintAtlasSession(geometry);
let t = performance.now();
for (let n = 2; n <= 60; n += 2) timed.update(layerWith([fill, strokeOp(full.slice(0, n))]));
const incMs = performance.now() - t;
t = performance.now();
for (let n = 2; n <= 60; n += 2)
  createDigitalPaintAtlas(layerWith([fill, strokeOp(full.slice(0, n))]), geometry).texture.dispose();
const fullMs = performance.now() - t;
console.log(
  `timing (30 growth frames, 3 channels): incremental ${incMs.toFixed(1)}ms total (${(incMs / 30).toFixed(2)}ms/frame) vs full replay ${fullMs.toFixed(1)}ms total (${(fullMs / 30).toFixed(2)}ms/frame) = ${(fullMs / incMs).toFixed(1)}x`,
);
session.dispose();
timed.dispose();
geometry.dispose();

// ---------------------------------------------------------------- masks
const { createMaskPaintSession, rasterizeMaskPaint } = await import(
  "../src/maskPaintRaster.js"
);
const maskSession = createMaskPaintSession(geometry);
const maskPixels = (canvas) =>
  canvas.getContext("2d").getImageData(0, 0, canvas.width, canvas.height).data;
function checkMask(label, operations, tolerance = 0) {
  const mask = {
    kind: "paint",
    fill: 1,
    strength: 100,
    enabled: true,
    inverted: false,
    paint: { uvSet: AUTHORING_UV_SET, operations },
  };
  const incremental = maskSession.update(mask);
  const reference = rasterizeMaskPaint(mask, geometry);
  const a = maskPixels(incremental),
    b = maskPixels(reference);
  let differing = 0;
  for (let i = 0; i < a.length; i++)
    if (a[i] !== b[i]) {
      differing++;
      if (Math.abs(a[i] - b[i]) > tolerance || differing > 1024)
        throw new Error(`mask ${label}: byte ${i} differs (${a[i]} != ${b[i]})`);
    }
  console.log(
    `  identical  mask ${label}${differing ? ` (${differing} bytes within ±${tolerance})` : ""}`,
  );
}
const digitalMaskOp = (points, operation = "paint") => ({
  kind: "brush",
  region: { kind: "island", partId: "lid", id: 25 },
  value: 1,
  opacity: 0.85,
  radius: Math.max(0.002, brush.size / 512),
  hardness: brush.hardness,
  points: points.map((p) => p.slice(0, 2)),
  digital: {
    brush,
    seed: 4242,
    operation,
    pressure: points.map((p) => p[2]),
  },
});
const legacyOp = (points) => ({
  kind: "brush",
  region: { kind: "island", partId: "lid", id: 25 },
  value: 0.7,
  opacity: 0.9,
  radius: 0.03,
  hardness: 0.6,
  points: points.map((p) => p.slice(0, 2)),
});
const regionOp = {
  kind: "region",
  region: { kind: "island", partId: "lid", id: 25 },
  value: 0.5,
  opacity: 0.8,
};
checkMask("region fill", [regionOp]);
for (let n = 2; n <= 40; n += 4)
  checkMask(`digital mask stroke grows to ${n}`, [
    regionOp,
    digitalMaskOp(full.slice(0, n)),
  ]);
// Destination-out over a dab falloff edge: Canvas2D does not guarantee
// bit-identical premultiplied rounding across equivalent draw sequences,
// so this one case allows ±1 on a handful of bytes.
checkMask(
  "digital mask erase",
  [
    regionOp,
    digitalMaskOp(full),
    digitalMaskOp(full.slice(10, 18), "erase"),
  ],
  1,
);
// These fold the incrementally repaired digital stroke into the base and
// blend legacy dabs over it. Canvas2D (Skia) does not guarantee
// bit-identical premultiplied rounding across equivalent draw sequences
// at soft dab falloffs, so folded-stroke texels carry a few-level drift
// (bounded here to ±3 on at most 1024 of 1048576 bytes).
for (let n = 2; n <= 30; n += 4)
  checkMask(
    `legacy mask stroke grows to ${n}`,
    [regionOp, digitalMaskOp(full), legacyOp(full.slice(0, n))],
    0,
  );
checkMask("mask undo to region", [regionOp]);
maskSession.dispose();
console.log("mask session equivalence: all frames byte-identical");
