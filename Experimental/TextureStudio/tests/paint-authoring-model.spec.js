import { test, expect } from "@playwright/test";
import {
  PAINT_TOOLS,
  PAINT_FAMILIES,
  normalizePaintSettings,
  selectPaintInstrument,
  toolSchema,
  toolArt,
  visibleToolSchema,
} from "../src/paintToolCatalogue.js";
import {
  PAINT_CHANNELS,
  CHANNEL_GENERATORS,
  normalizeChannelSettings,
  normalizeGenerator,
  togglePaintChannel,
} from "../src/paintChannelModel.js";
import {
  createTextureDocument,
  createTextureLayer,
  patchTextureLayer,
  duplicateTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
} from "../src/textureDocument.js";

test("all 102 prototype instruments retain SVG art, bounded schemas and ten complete families", () => {
  expect(PAINT_TOOLS).toHaveLength(102);
  expect(PAINT_FAMILIES).toHaveLength(10);
  expect(new Set(PAINT_TOOLS.map((t) => t.id)).size).toBe(102);
  expect(PAINT_FAMILIES.reduce((n, f) => n + f.tally, 0)).toBe(102);
  for (const tool of PAINT_TOOLS) {
    const art = toolArt(tool.id),
      settings = normalizePaintSettings({ toolId: tool.id });
    expect(art).toMatch(/^<svg/);
    expect(art).not.toMatch(
      /<script|onload=|onerror=|javascript:|foreignObject/i,
    );
    expect(toolArt(tool.id, true)).toContain('viewBox="188 6 48 48"');
    expect(settings.toolId).toBe(tool.id);
    expect(visibleToolSchema(settings).length).toBeGreaterThan(0);
    for (const g of toolSchema(tool.id))
      for (const c of g.controls) {
        const value = settings.params[c.k];
        expect(value).not.toBeUndefined();
        if (c.type === "slider") {
          expect(Number.isFinite(value)).toBe(true);
          expect(value).toBeGreaterThanOrEqual(c.min);
          expect(value).toBeLessThanOrEqual(c.max);
        } else if (c.type === "switch") expect(typeof value).toBe("boolean");
        else expect(c.opts).toContain(value);
      }
  }
});
test("tool settings reject code, unbounded fields, invalid colours and non-finite numbers while retaining zero", () => {
  const seed = normalizePaintSettings(),
    settings = normalizePaintSettings({
      toolId: seed.toolId,
      color: "javascript:evil",
      params: {
        size: Infinity,
        opacity: 0,
        flow: 999999,
        smoothing: -999,
        script: "evil",
      },
    });
  expect(settings.color).toBe(seed.color);
  expect(settings.params.opacity).toBe(0);
  expect(settings.params.size).toBe(seed.params.size);
  expect(settings.params.flow).toBe(100);
  expect(settings.params.smoothing).toBe(0);
  expect(settings.params).not.toHaveProperty("script");
  expect(normalizePaintSettings(null)).toEqual(seed);
  expect(normalizePaintSettings({ toolId: "unknown" })).toEqual(seed);
});
test("reopening or assigning the active instrument does not discard its edited configuration", () => {
  const edited = normalizePaintSettings({
    toolId: PAINT_TOOLS[0].id,
    color: "#123456",
    params: { opacity: 0, size: 12 },
  });
  expect(selectPaintInstrument(edited, edited.toolId)).toEqual(edited);
  const other = PAINT_TOOLS.find((t) => t.key === "brush");
  expect(selectPaintInstrument(edited, other.id)).toEqual(
    normalizePaintSettings({ toolId: other.id }),
  );
});

test("14 channel targets have Height-dependent Normal and can all be disabled", () => {
  expect(PAINT_CHANNELS).toHaveLength(14);
  expect(CHANNEL_GENERATORS).toHaveLength(10);
  expect(togglePaintChannel([], "normal", true)).toEqual(["height", "normal"]);
  expect(
    togglePaintChannel(["height", "normal", "roughness"], "height", false),
  ).toEqual(["roughness"]);
  expect(togglePaintChannel(["height", "normal"], "normal", false)).toEqual([
    "height",
  ]);
  expect(togglePaintChannel([], "not-a-channel", true)).toEqual([]);
  const doc = createTextureDocument();
  expect(
    patchTextureLayer(doc, doc.layers[0].id, { channels: [] }).layers[0]
      .channels,
  ).toEqual([]);
  expect(
    createTextureLayer("paint", { channels: ["normal"] }).channels,
  ).toEqual(["height", "normal"]);
});
test("channel source settings are finite, bounded, null-safe and preserve disabled values", () => {
  const channels = normalizeChannelSettings({
    roughness: { mode: "value", value: 0 },
    refractionIndex: { value: 99 },
    baseColor: { value: "#ABCDEF" },
    height: {
      mode: "generator",
      generator: {
        id: "PerlinNoise",
        parameters: { Scale: 0, Contrast: Infinity, Octaves: 2, code: "evil" },
      },
    },
  });
  expect(channels.roughness.value).toBe(0);
  expect(channels.refractionIndex.value).toBe(3);
  expect(channels.baseColor.value).toBe("#abcdef");
  expect(channels.height.generator).toEqual({
    id: "PerlinNoise",
    parameters: { Scale: 0, Octaves: 1, Contrast: 0.5 },
  });
  expect(normalizeGenerator({ id: "JavaScript" })).toBe(null);
  expect(normalizeGenerator(null)).toBe(null);
  expect(normalizeChannelSettings(null)).toEqual(normalizeChannelSettings());
  const doc = createTextureDocument(),
    id = doc.layers[0].id;
  const changed = patchTextureLayer(doc, id, {
    channels: [],
    channelSettings: channels,
  });
  expect(changed.layers[0].channelSettings.roughness.value).toBe(0);
  expect(
    patchTextureLayer(changed, id, { channels: ["roughness"] }).layers[0]
      .channelSettings,
  ).toEqual(changed.layers[0].channelSettings);
});
test("v1 projects round-trip tool, channel and lightweight asset configuration with legacy aliases", () => {
  const doc = createTextureDocument();
  doc.painting = normalizePaintSettings({
    toolId: PAINT_TOOLS.find((t) => t.key === "brush").id,
    color: "#123456",
    params: { opacity: 0 },
  });
  doc.layers[0] = createTextureLayer("paint", {
    id: doc.layers[0].id,
    channels: ["baseColour", "metallic", "ambientOcclusion"],
    sourceAsset: {
      type: "material",
      id: "corrugated-zinc",
      name: "Corrugated Zinc",
      code: "evil",
    },
    channelSettings: { roughness: { value: 0 } },
  });
  const result = parseTextureDocument(JSON.stringify(doc));
  expect(result).toEqual(doc);
  expect(result.layers[0].channels).toEqual([
    "baseColor",
    "metalness",
    "occlusion",
  ]);
  expect(result.layers[0].sourceAsset).toEqual({
    type: "material",
    id: "corrugated-zinc",
    name: "Corrugated Zinc",
  });
  expect(JSON.stringify(doc)).not.toMatch(
    /data:image|textureData|"code"|"atlas"|"strokes"/,
  );
  const legacy = { ...doc };
  delete legacy.painting;
  delete legacy.layers[1].channelSettings;
  expect(validateTextureDocument(legacy).painting).toEqual(
    normalizePaintSettings(),
  );
});
test("copied channel sources are independent and locks reject source and target edits", () => {
  let doc = createTextureDocument(),
    id = doc.layers[0].id;
  doc = patchTextureLayer(doc, id, {
    channelSettings: normalizeChannelSettings({
      height: { mode: "generator", generator: { id: "PerlinNoise" } },
    }),
  });
  const copy = duplicateTextureLayer(doc, id).doc;
  copy.layers[0].channelSettings.height.generator.parameters.Scale = 0;
  expect(copy.layers[1].channelSettings.height.generator.parameters.Scale).toBe(
    0.4,
  );
  doc = patchTextureLayer(doc, id, { locked: true });
  expect(
    patchTextureLayer(doc, id, {
      channels: [],
      sourceAsset: { type: "brush", id: PAINT_TOOLS[0].id, name: "Pen" },
      channelSettings: {},
    }),
  ).toBe(doc);
});
