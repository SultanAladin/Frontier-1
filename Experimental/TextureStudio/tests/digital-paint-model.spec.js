import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  DIGITAL_BRUSH_DEFAULTS,
  DIGITAL_BRUSH_PRESETS,
  normalizeDigitalBrush,
  selectDigitalBrush,
  digitalBrushDabs,
} from "../src/digitalBrush.js";
import {
  DIGITAL_PAINT_SCHEMA,
  PAINT_OPERATION_LIMIT,
  PAINT_TOTAL_POINT_LIMIT,
  normalizeDigitalPaint,
  hasDigitalPaint,
} from "../src/textureDigitalPaint.js";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  patchTextureLayer,
  duplicateTextureLayer,
} from "../src/textureDocument.js";
import {
  AUTHORING_UV_SET,
  createTextureTeapotGeometry,
} from "../src/textureSurfaceModel.js";
import {
  evaluateTextureStack,
  textureLayerFullyCovers,
} from "../src/textureStackEvaluation.js";
import { normalizeMaskPaint } from "../src/textureMaskPaint.js";
import { normalizeMaterial } from "../src/materials.js";
import { describeTextureChange } from "../src/textureHistory.js";
import { installDigitalPaintMaterial } from "../src/digitalPaintSurface.js";
const brush = normalizeDigitalBrush({
  ...DIGITAL_BRUSH_DEFAULTS,
  sizePressure: 0,
});
const op = (changes = {}) => ({
  id: "stroke-1",
  kind: "paint",
  seed: 42,
  brush,
  region: { kind: "island", partId: "body", id: 1 },
  channels: { baseColor: "#ff0000", roughness: 0.2 },
  points: [
    [0.06, 0.08, 1],
    [0.14, 0.12, 0.5],
  ],
  ...changes,
});
const paint = (ops = [op()]) => ({
  schema: DIGITAL_PAINT_SCHEMA,
  uvSet: AUTHORING_UV_SET,
  operations: ops,
});
test("digital presets are bounded tips, with zero size and opacity retained, not physical instruments", () => {
  expect(DIGITAL_BRUSH_PRESETS.map((p) => p.id)).toEqual([
    "round",
    "soft",
    "flat",
    "pixel",
    "grain",
  ]);
  const zero = normalizeDigitalBrush({ size: 0, opacity: 0, flow: 0 });
  expect(zero.size).toBe(0);
  expect(zero.opacity).toBe(0);
  expect(zero.flow).toBe(0);
  const malicious = normalizeDigitalBrush({
    size: 1e9,
    spacing: 0,
    roundness: 0,
    rotation: Infinity,
    color: "javascript:alert(1)",
    grain: "<script>",
    tool: "spray",
  });
  expect(malicious.size).toBe(128);
  expect(malicious.spacing).toBe(0.05);
  expect(malicious.roundness).toBe(0.05);
  expect(malicious.tool).toBe("paint");
  expect(
    selectDigitalBrush({ ...brush, color: "#abcdef", tool: "erase" }, "soft"),
  ).toMatchObject({
    preset: "soft",
    color: "#abcdef",
    tool: "erase",
    hardness: 0.1,
  });
});
test("deterministic arc-length dabs do not depend on redundant pointer samples", () => {
  const a = digitalBrushDabs(
    [
      [0, 0, 1],
      [1, 0, 1],
    ],
    brush,
    42,
  );
  const b = digitalBrushDabs(
    [
      [0, 0, 1],
      [0.25, 0, 1],
      [0.5, 0, 1],
      [0.75, 0, 1],
      [1, 0, 1],
    ],
    brush,
    42,
  );
  expect(a).toEqual(b);
  expect(a.at(-1).x).toBe(256);
});
for (const key of ["size", "opacity", "flow"])
  test(`zero ${key} deposits no dabs`, () => {
    expect(
      digitalBrushDabs(
        [
          [0, 0, 1],
          [1, 1, 1],
        ],
        { ...brush, [key]: 0 },
      ),
    ).toEqual([]);
  });
test("pressure, taper, spacing, scatter, rotation and seeded colour dynamics change real dabs", () => {
  const path = [
      [0, 0, 0.1],
      [1, 0, 1],
    ],
    solid = digitalBrushDabs(path, brush, 42),
    pressure = digitalBrushDabs(
      path,
      { ...brush, sizePressure: 1, opacityPressure: 1 },
      42,
    );
  expect(pressure[0].radius).toBeLessThan(pressure.at(-1).radius);
  expect(pressure[0].opacity).toBeCloseTo(0.1);
  const taper = digitalBrushDabs(
    path,
    { ...brush, taperStart: 0.3, taperEnd: 0.3 },
    42,
  );
  expect(taper[0].radius).toBeLessThan(
    taper[Math.floor(taper.length / 2)].radius,
  );
  expect(
    digitalBrushDabs(path, { ...brush, spacing: 1 }, 42).length,
  ).toBeLessThan(solid.length);
  const varied = {
    ...brush,
    scatter: 0.2,
    rotationJitter: 0.3,
    dabHue: 0.5,
    dabBrightness: 0.3,
  };
  const first = digitalBrushDabs(path, varied, 123);
  expect(first).toEqual(digitalBrushDabs(path, varied, 123));
  expect(first).not.toEqual(digitalBrushDabs(path, varied, 124));
  expect(new Set(first.map((d) => d.color)).size).toBeGreaterThan(10);
  expect(
    digitalBrushDabs(path, varied, 123, 256, "#ffffff", false).every(
      (d) => d.color === "#ffffff",
    ),
  ).toBe(true);
});
test("paint, channel erase and region fill round-trip independently of legacy 2D source records", () => {
  const doc = createTextureDocument(),
    id = doc.layers[0].id;
  const next = patchTextureLayer(doc, id, {
    paint: paint([
      op(),
      op({ id: "erase", kind: "erase", channels: { roughness: 0 } }),
      op({
        id: "fill",
        kind: "fill",
        region: { kind: "object", partId: "lid" },
        points: [],
      }),
    ]),
  });
  expect(validateTextureDocument(JSON.parse(JSON.stringify(next)))).toEqual(
    next,
  );
  expect(next.layers[0].paint.operations).toHaveLength(3);
  expect(next.lastStroke).toBeNull();
  const copied = duplicateTextureLayer(next, id).doc;
  copied.layers[0].paint.operations[0].points[0][0] = 0.07;
  expect(copied.layers[1].paint.operations[0].points[0][0]).toBe(0.06);
  const locked = patchTextureLayer(next, id, { locked: true });
  expect(patchTextureLayer(locked, id, { paint: null })).toEqual(locked);
});
let badCase = 0;
for (const bad of [
  { schema: "foreign" },
  { uvSet: "uv0" },
  { operations: [op({ seed: NaN })] },
  { operations: [op({ points: [[0.1, 0.1, Infinity]] })] },
  { operations: [op({ points: [[1.1, 0.1, 1]] })] },
  { operations: [op({ channels: { normal: "#8090ff" } })] },
  { operations: [op({ channels: { roughness: "0.5" } })] },
  { operations: [op({ channels: { baseColor: "javascript:alert(1)" } })] },
  { operations: [op({ region: { kind: "island", partId: "lid", id: 1 } })] },
])
  test(`malformed digital paint case ${++badCase} is rejected`, () => {
    expect(() => normalizeDigitalPaint({ ...paint(), ...bad })).toThrow();
  });
test("operation, sample, dab-work, atlas-slice and original 1 MiB budgets reject atomically", () => {
  expect(() =>
    normalizeDigitalPaint(
      paint(Array.from({ length: PAINT_OPERATION_LIMIT + 1 }, () => op())),
    ),
  ).toThrow(/128/);
  expect(() =>
    normalizeDigitalPaint(
      paint([op({ points: Array.from({ length: 513 }, () => [0.1, 0.1, 1]) })]),
    ),
  ).toThrow(/512/);
  expect(() =>
    normalizeDigitalPaint(
      paint(
        Array.from({ length: 17 }, (_, i) =>
          op({
            id: `p-${i}`,
            points: Array.from({ length: 512 }, () => [0.1, 0.1, 1]),
          }),
        ),
      ),
    ),
  ).toThrow(/sampling budget/);
  const long = op({
    brush: { ...brush, size: 1, spacing: 0.05 },
    points: Array.from({ length: 512 }, (_, i) => [
      i % 2 ? 0.01 : 0.24,
      0.1,
      1,
    ]),
  });
  expect(() => normalizeDigitalPaint(paint([long]))).toThrow(/dab budget/);
  const doc = createTextureDocument();
  doc.layers = Array.from({ length: 33 }, (_, i) =>
    createTextureLayer("paint", {
      id: `paint-${i}`,
      channels: ["baseColor"],
      paint: paint([op({ channels: { baseColor: "#ffffff" } })]),
    }),
  );
  expect(() => validateTextureDocument(doc)).toThrow(/channel-slice budget/);
  expect(() =>
    validateTextureDocument({
      ...createTextureDocument(),
      layers: [createTextureLayer("folder", { paint: paint() })],
    }),
  ).toThrow(/Folders/);
  expect(PAINT_TOTAL_POINT_LIMIT).toBe(8192);
});
test("digital rows enter ordered evaluation; disabling channels retains data and restores lower passes", () => {
  const doc = createTextureDocument();
  doc.layers = [
    createTextureLayer("paint", { id: "ink", paint: paint() }),
    createTextureLayer("material", {
      id: "green",
      surfaceMaterial: normalizeMaterial({
        id: "green",
        type: 8,
        color: "#225533",
      }),
    }),
  ];
  const plan = evaluateTextureStack(doc);
  expect(plan.surfaces.map((r) => r.layer.id)).toEqual(["green", "ink"]);
  expect(plan.surfaces.at(-1).paintOnly).toBe(true);
  expect(textureLayerFullyCovers(plan.surfaces.at(-1))).toBe(false);
  const off = patchTextureLayer(doc, "ink", { channels: [] });
  expect(off.layers[0].paint).toEqual(doc.layers[0].paint);
  expect(hasDigitalPaint(off.layers[0])).toBe(false);
  expect(evaluateTextureStack(off).surfaces.map((r) => r.layer.id)).toEqual([
    "green",
  ]);
  const moved = { ...doc, layers: [doc.layers[1], doc.layers[0]] };
  expect(evaluateTextureStack(moved).surfaces.map((r) => r.layer.id)).toEqual([
    "ink",
    "green",
  ]);
  const nativePaint = patchTextureLayer(doc, "green", { paint: paint() });
  expect(
    evaluateTextureStack(nativePaint).surfaces.find(
      (r) => r.layer.id === "green",
    ).paintOnly,
  ).toBe(false);
});
test("digital masks retain their original source and pressure; erasing is distinct from black mask ink", () => {
  const digital = { brush, seed: 42, operation: "erase", pressure: [0.2, 1] };
  const raw = {
    uvSet: AUTHORING_UV_SET,
    operations: [
      {
        kind: "brush",
        region: op().region,
        value: 0,
        opacity: 0.8,
        radius: 0.03,
        hardness: 1,
        points: [
          [0.1, 0.1],
          [0.15, 0.12],
        ],
        digital,
      },
    ],
  };
  const normalized = normalizeMaskPaint(raw);
  expect(normalized.operations[0].digital.operation).toBe("erase");
  expect(normalized.operations[0].digital.pressure).toEqual([0.2, 1]);
  expect(() =>
    normalizeMaskPaint({
      ...raw,
      operations: [
        { ...raw.operations[0], digital: { ...digital, pressure: [NaN, 1] } },
      ],
    }),
  ).toThrow(/pressure/);
  expect(() =>
    normalizeMaskPaint({
      ...raw,
      operations: [
        { ...raw.operations[0], digital: { ...digital, pressure: [1] } },
      ],
    }),
  ).toThrow(/pressure/);
});
test("digital shader uses linear premultiplied fields, sparse alpha and RNM; native optics remain unchanged", () => {
  const material = new THREE.MeshPhysicalMaterial({
    transmission: 0.97,
    ior: 1.45,
    thickness: 0.4,
    clearcoat: 0.2,
  });
  const before = [
    material.transmission,
    material.ior,
    material.thickness,
    material.clearcoat,
  ];
  const uniforms = {};
  installDigitalPaintMaterial(material, uniforms, {});
  const shader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  material.onBeforeCompile(shader);
  expect([
    material.transmission,
    material.ior,
    material.thickness,
    material.clearcoat,
  ]).toEqual(before);
  expect(shader.fragmentShader).toContain("sampler2DArray");
  expect(shader.fragmentShader).toContain("p.rgb/=max(p.a,1e-6)");
  expect(shader.fragmentShader).toContain("unionAlpha");
  expect(shader.fragmentShader).toContain("p.a/max(coverage,1e-6)");
  expect(shader.fragmentShader).toContain(
    "digitalRelief(normal,nonPerturbedNormal)",
  );
  expect(shader.fragmentShader.indexOf("if(uDigitalCapture>0)")).toBeLessThan(
    shader.fragmentShader.indexOf("#include <lights_fragment_begin>"),
  );
  expect(shader.fragmentShader).toContain("nativeAniso>1e-6?degrees(atan");
  expect(shader.fragmentShader).toContain(
    "material.anisotropy>1e-6?fract(atan",
  );
  // Runs after either native vector path, including USE_ANISOTROPYMAP.
  expect(
    shader.fragmentShader.indexOf("if(uDigitalInherited==1||digitalField("),
  ).toBeGreaterThan(
    shader.fragmentShader.indexOf("vec2 anisotropyV = anisotropyVector;"),
  );
  expect(shader.fragmentShader).not.toContain("transmission = 0");
  material.dispose();
});
test("old projects recover digital defaults without removing projects, channels, masks or source settings", () => {
  const old = createTextureDocument();
  delete old.digitalPainting;
  delete old.lastDigitalStroke;
  const upgraded = validateTextureDocument(old);
  expect(upgraded.digitalPainting).toEqual(normalizeDigitalBrush());
  expect(upgraded.layers).toEqual(old.layers);
  expect(
    describeTextureChange(upgraded, {
      ...upgraded,
      digitalPainting: { ...upgraded.digitalPainting, dabHue: 0.2 },
    }).kind,
  ).toBe("tool");
});

test("imported digital segments cannot bridge charts or spoof a face; raster work is bounded per layer", () => {
  const doc = createTextureDocument();
  doc.layers = [
    createTextureLayer("paint", {
      id: "ink",
      paint: paint([op({ points: [[0.6, 0.6, 1]] })]),
    }),
  ];
  expect(() => validateTextureDocument(doc)).toThrow(/UV chart/);
  expect(() => normalizeDigitalPaint(paint([op(), op()]))).toThrow(
    /IDs must be unique/,
  );
  expect(() =>
    normalizeDigitalPaint(
      paint([op({ region: { kind: "object", partId: "body" } })]),
    ),
  ).toThrow(/one face or UV chart/);
  const long = op({
    brush: { ...brush, size: 1, spacing: 0.05 },
    points: Array.from({ length: 100 }, (_, i) => [
      i % 2 ? 0.05 : 0.15,
      0.12,
      1,
    ]),
  });
  expect(() =>
    normalizeDigitalPaint(
      paint(Array.from({ length: 24 }, (_, i) => ({ ...long, id: `p-${i}` }))),
    ),
  ).toThrow(/raster-work budget/);
});
