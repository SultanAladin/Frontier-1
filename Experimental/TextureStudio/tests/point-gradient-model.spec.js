import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  defaultPointGradient,
  normalizePointGradient,
  compilePointGradient,
  linearToSRGB,
  gradientMaskCoverage,
  rasterizePointGradient,
  GRADIENT_POINT_LIMIT,
} from "../src/pointGradient.js";
import {
  createTextureDocument,
  validateTextureDocument,
  patchTextureLayer,
  parseTextureDocument,
  duplicateTextureLayer,
} from "../src/textureDocument.js";
import {
  createMaterialGenerator,
  normalizeMaterialGenerator,
  materialGeneratorSnapshot,
  generatorStamp,
  generatorChannelValue,
} from "../src/materialGeneratorModel.js";
import { normalizeTextureMask } from "../src/textureMasks.js";
import {
  normalizeChannelSettings,
  normalizeGenerator,
} from "../src/paintChannelModel.js";
import { materials } from "../src/materials.js";
import {
  createPointGradientUniforms,
  updatePointGradientUniforms,
} from "../src/pointGradientViewport.js";
const png = {
  name: "field.png",
  width: 1,
  height: 1,
  dataUrl:
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==",
};

test("point gradients normalize finite object-space coordinates, retain exact zero and reject invalid IDs/caps/space", () => {
  const g = defaultPointGradient();
  g.points[0].position = [-100, 0, 100];
  g.points[0].radius = 0;
  g.points[0].weight = 0;
  g.script = "bad";
  const normal = normalizePointGradient(g);
  expect(normal.points[0]).toMatchObject({
    position: [-2, 0, 2],
    radius: 0,
    weight: 0,
  });
  expect(normal).not.toHaveProperty("script");
  for (const bad of [
    { ...g, space: "screen" },
    { ...g, points: [g.points[0], g.points[0]] },
    { ...g, points: [{ ...g.points[0], position: [NaN, 0, 0] }] },
    {
      ...g,
      points: Array.from({ length: GRADIENT_POINT_LIMIT + 1 }, (_, i) => ({
        ...g.points[0],
        id: "p" + i,
      })),
    },
  ])
    expect(() => normalizePointGradient(bad)).toThrow(/gradient/i);
});

test("distance blending is linear RGB, point radius/weight alter the field and all-muted points fall back honestly", () => {
  const g = defaultPointGradient();
  g.points = [
    { id: "r", position: [-0.5, 0, 0], color: "#ff0000", radius: 1, weight: 1 },
    { id: "b", position: [0.5, 0, 0], color: "#0000ff", radius: 1, weight: 1 },
  ];
  const sample = compilePointGradient(g);
  expect(sample([0, 0, 0])).toEqual([0.5, 0, 0.5]);
  expect(linearToSRGB(0.5)).toBeCloseTo(0.735357);
  expect(sample([-0.5, 0, 0])[0]).toBeGreaterThan(sample([-0.5, 0, 0])[2]);
  const muted = compilePointGradient({
    ...g,
    background: "#ffffff",
    points: g.points.map((p) => ({ ...p, weight: 0 })),
  });
  expect(muted([0, 0, 0])).toEqual([1, 1, 1]);
  const zeroRadius = compilePointGradient({
    ...g,
    background: "#000000",
    points: g.points.map((p) => ({ ...p, radius: 0 })),
  });
  expect(zeroRadius([0, 0, 0])).toEqual([0, 0, 0]);
});

test("gradient masks evaluate point luminance, invert, zero strength and disabled coverage including empty fields", () => {
  const mask = normalizeTextureMask({ kind: "gradient" });
  expect(mask.gradient).toEqual(defaultPointGradient(true));
  expect(gradientMaskCoverage([0, 0, 0], mask)).toBe(0);
  expect(gradientMaskCoverage([1, 1, 1], mask)).toBe(1);
  expect(gradientMaskCoverage([0, 0, 0], { ...mask, inverted: true })).toBe(1);
  expect(gradientMaskCoverage([0, 0, 0], { ...mask, strength: 0 })).toBe(1);
  expect(gradientMaskCoverage([0, 0, 0], { ...mask, enabled: false })).toBe(1);
  const pixels = rasterizePointGradient(
    { ...mask.gradient, points: [], background: "#000000" },
    { edge: 2, mask },
  );
  expect([...pixels]).toEqual([
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
  ]);
});

test("source gradients and folder masks round-trip as portable independent point records, without mesh/UV stroke reinterpretation", () => {
  let doc = createTextureDocument();
  doc = patchTextureLayer(doc, "surface-detail", {
    channelSettings: {
      ...doc.layers[0].channelSettings,
      baseColor: {
        ...doc.layers[0].channelSettings.baseColor,
        mode: "gradient",
        gradient: defaultPointGradient(),
      },
    },
    mask: {
      kind: "gradient",
      gradient: defaultPointGradient(true),
      strength: 0,
    },
  });
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
  const copy = duplicateTextureLayer(doc, "surface-detail").doc;
  const original = copy.layers.find((l) => l.id === "surface-detail"),
    cloned = copy.layers.find(
      (l) => l.id !== "surface-detail" && l.name === "Surface detail copy",
    );
  cloned.channelSettings.baseColor.gradient.points[0].color = "#000000";
  expect(original.channelSettings.baseColor.gradient.points[0].color).not.toBe(
    "#000000",
  );
  expect(
    normalizeChannelSettings({ baseColor: { mode: "gradient" } }).baseColor
      .gradient,
  ).toEqual(defaultPointGradient());
});

test("viewport uniforms pack full 3D point colour/radius/weight and ancestor masks into a bounded float texture", () => {
  const u = createPointGradientUniforms(),
    geometry = new THREE.SphereGeometry(2),
    g = defaultPointGradient();
  updatePointGradientUniforms(
    u,
    {
      fill: g,
      value: "#ff0000",
      opacity: 0.5,
      masks: [normalizeTextureMask({ kind: "gradient", strength: 25 })],
    },
    geometry,
  );
  expect(u.uPointGradientCounts.value[0]).toBe(2);
  expect(u.uPointGradientCounts.value[1]).toBe(2);
  expect(u.uPointGradientMaskCount.value).toBe(1);
  expect(u.uPointGradientRadius.value).toBeCloseTo(2);
  expect(u.uPointGradientOpacity.value).toBe(0.5);
  expect(u.uPointGradientData.value.image.data[3]).toBeCloseTo(0.8);
  expect(u.uPointGradientMaskInfo.value[1].z).toBe(0.25);
  updatePointGradientUniforms(u, null, geometry);
  expect(u.uPointGradientEnabled.value).toBe(0);
  u.uPointGradientData.value.dispose();
  geometry.dispose();
});

test("Material studio generators sanitize known procedural presets and executable/external field inputs never become code", () => {
  const source = materials.find((m) => m.id === "scratches"),
    raw = {
      ...source,
      script: "evil",
      onBeforeCompile: "evil",
      pattern: { code: "evil" },
      colorStops: [0, 1],
      unknown: { huge: "bad" },
    };
  const snapshot = materialGeneratorSnapshot(raw);
  expect(snapshot.recipeId).toBe("scratches");
  for (const key of ["script", "onBeforeCompile", "pattern", "unknown"])
    expect(snapshot).not.toHaveProperty(key);
  expect(() => materialGeneratorSnapshot({ name: "a".repeat(65000) })).toThrow(
    /bounded/,
  );
  const gen = createMaterialGenerator(raw);
  expect(normalizeGenerator(gen)).toEqual(gen);
  expect(
    normalizeMaterialGenerator({
      ...gen,
      widthMM: 0,
      low: 0,
      high: 0,
      gamma: Infinity,
    }),
  ).toMatchObject({ widthMM: 1, low: 0, high: 0, gamma: 1 });
});

test("generated pixels are kept only for the matching material/configuration stamp; independent copies and round-trip stay bounded", () => {
  const g = createMaterialGenerator();
  const rendered = normalizeMaterialGenerator({ ...g, result: png });
  expect(rendered.result).toEqual(png);
  expect(normalizeMaterialGenerator({ ...rendered, low: 0.2 }).result).toBe(
    null,
  );
  expect(
    normalizeMaterialGenerator({ ...rendered, output: "roughness" }).result,
  ).toBe(null);
  let doc = createTextureDocument();
  doc = patchTextureLayer(doc, "base-material", {
    channelSettings: {
      ...doc.layers.at(-1).channelSettings,
      baseColor: {
        ...doc.layers.at(-1).channelSettings.baseColor,
        mode: "generator",
        generator: rendered,
      },
    },
  });
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
  expect(generatorStamp(rendered)).toBe(rendered.stamp);
  expect(
    normalizeMaterialGenerator({
      ...rendered,
      result: { ...png, dataUrl: "https://invalid.test/image" },
    }).result,
  ).toBe(null);
});

test("material field transfer levels honour zero thresholds, hard steps, gamma/inversion and scratch cavity extraction", () => {
  const g = createMaterialGenerator();
  expect(generatorChannelValue(128, g)).toBe(0);
  expect(generatorChannelValue(0, g)).toBe(1);
  expect(generatorChannelValue(128, { ...g, inverted: true })).toBe(1);
  expect(
    generatorChannelValue(0, { ...g, output: "roughness", low: 0, high: 0 }),
  ).toBe(1);
  expect(
    generatorChannelValue(128, { ...g, output: "roughness", gamma: 1 }),
  ).toBeCloseTo(128 / 255);
});
