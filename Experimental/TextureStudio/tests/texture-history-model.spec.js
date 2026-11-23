import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  patchTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
  createTextureLayer,
} from "../src/textureDocument.js";
import {
  createTextureHistory,
  commitTextureHistory,
  textureHistoryTimeline,
  travelTextureHistory,
  restoreTextureHistory,
  TEXTURE_HISTORY_LIMIT,
  describeTextureChange,
} from "../src/textureHistory.js";
import {
  normalizeSourceStroke,
  SOURCE_STROKE_SCHEMA,
  SOURCE_STROKE_POINT_LIMIT,
  sourceStrokeOptions,
} from "../src/textureStrokes.js";
import {
  normalizePaintSettings,
  PAINT_TOOLS,
} from "../src/paintToolCatalogue.js";
import { normalizeTextureSource } from "../src/textureSource.js";
const png = {
  name: "stroke.png",
  width: 1,
  height: 1,
  dataUrl:
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==",
};
const state = (doc) => ({
  doc,
  selection: { id: "surface-detail", mask: false },
});
const record = () => ({
  schema: SOURCE_STROKE_SCHEMA,
  id: "stroke-1",
  layerId: "surface-detail",
  layerName: "Surface detail",
  channelId: "baseColor",
  settings: normalizePaintSettings(),
  points: [
    { x: 0.1, y: 0.2, pressure: 1 },
    { x: 0.8, y: 0.7, pressure: 0.5 },
  ],
  image: png,
});

test("the history timeline shares exact undo snapshots, labels edits and coalesces only continuous controls", () => {
  const initial = createTextureDocument();
  let h = createTextureHistory(state(initial), "New project", 1);
  h = commitTextureHistory(
    h,
    state(patchTextureLayer(initial, "surface-detail", { opacity: 65 })),
    { key: "opacity" },
    10,
  );
  h = commitTextureHistory(
    h,
    state(patchTextureLayer(h.present.doc, "surface-detail", { opacity: 40 })),
    { key: "opacity" },
    100,
  );
  expect(textureHistoryTimeline(h)).toHaveLength(2);
  expect(h.present.revision).toMatchObject({
    label: "Layer opacity",
    detail: "Surface detail · 40%",
    time: 10,
  });
  const id = h.present.revision.id;
  h = commitTextureHistory(
    h,
    state(patchTextureLayer(h.present.doc, "surface-detail", { channels: [] })),
    {},
    120,
  );
  expect(textureHistoryTimeline(h)).toHaveLength(3);
  expect(h.present.revision.label).toBe("Channel targets");
  h = travelTextureHistory(h, "undo");
  expect(h.present.revision.id).toBe(id);
  expect(h.present.doc.layers[0].opacity).toBe(40);
  expect(h.present.doc.layers[0].channels).toEqual(initial.layers[0].channels);
  expect(textureHistoryTimeline(h).at(-1).undone).toBe(true);
  h = travelTextureHistory(h, "undo");
  expect(h.present.doc).toEqual(initial);
  h = travelTextureHistory(h, "redo");
  expect(h.present.doc.layers[0].opacity).toBe(40);
});

test("jumping to a history state preserves redo; a new branch truncates only future states and never reuses IDs", () => {
  let h = createTextureHistory(
    state(createTextureDocument()),
    "New project",
    0,
  );
  for (let i = 1; i <= 4; i++)
    h = commitTextureHistory(
      h,
      state(patchTextureLayer(h.present.doc, "surface-detail", { opacity: i })),
      {},
      i,
    );
  const old = textureHistoryTimeline(h).map((e) => e.id),
    last = h.present.doc;
  h = restoreTextureHistory(h, old[1]);
  expect(h.present.doc.layers[0].opacity).toBe(1);
  expect(h.future).toHaveLength(3);
  h = restoreTextureHistory(h, old[4]);
  expect(h.present.doc).toEqual(last);
  expect(h.future).toHaveLength(0);
  h = restoreTextureHistory(h, old[1]);
  h = commitTextureHistory(
    h,
    state(
      patchTextureLayer(h.present.doc, "surface-detail", {
        name: "New branch",
      }),
    ),
    {},
    10,
  );
  expect(h.future).toHaveLength(0);
  expect(textureHistoryTimeline(h)).toHaveLength(3);
  expect(old).not.toContain(h.present.revision.id);
  expect(restoreTextureHistory(h, "missing")).toBe(h);
});

test("history retains a bounded 80-step undo window and distinguishes actual stroke transactions from tool illustrations", () => {
  let h = createTextureHistory(state(createTextureDocument()));
  for (let i = 0; i < 110; i++)
    h = commitTextureHistory(
      h,
      state(
        patchTextureLayer(h.present.doc, "surface-detail", {
          name: `Layer ${i}`,
        }),
      ),
      {},
      i * 1000,
    );
  expect(h.past).toHaveLength(TEXTURE_HISTORY_LIMIT);
  expect(textureHistoryTimeline(h)).toHaveLength(TEXTURE_HISTORY_LIMIT + 1);
  const r = record(),
    after = { ...h.present.doc, lastStroke: r };
  h = commitTextureHistory(h, state(after), {}, 120000);
  expect(h.present.revision.kind).toBe("stroke");
  expect(h.present.revision.label).toBe("Paint stroke");
  expect(textureHistoryTimeline(h).at(-1).lastStroke).toEqual(r);
  const next = {
    ...after,
    painting: normalizePaintSettings({ color: "#ff0000" }),
  };
  expect(describeTextureChange(after, next).kind).toBe("tool");
  expect(describeTextureChange(after, next).label).toBe("Tool colour");
});

test("bounded source-stroke records retain actual point/preview provenance and round-trip independently of atlas painting", () => {
  const raw = record();
  raw.points = [
    { x: -1, y: 2, pressure: 0 },
    { x: 0.3333333, y: 0.7777777, pressure: 1 },
  ];
  raw.code = "evil";
  raw.settings.script = "evil";
  const normalized = normalizeSourceStroke(raw);
  expect(normalized.points).toEqual([
    { x: 0, y: 1, pressure: 0 },
    { x: 0.33333, y: 0.77778, pressure: 1 },
  ]);
  expect(normalized).not.toHaveProperty("code");
  expect(normalized.settings).not.toHaveProperty("script");
  let doc = createTextureDocument();
  doc = patchTextureLayer(doc, "surface-detail", {
    channelSettings: {
      ...doc.layers[0].channelSettings,
      baseColor: {
        ...doc.layers[0].channelSettings.baseColor,
        mode: "texture",
        texture: { ...png, origin: "paint" },
      },
    },
  });
  doc = validateTextureDocument({ ...doc, lastStroke: normalized });
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
  expect(doc.layers[0].channelSettings.baseColor.texture.origin).toBe("paint");
  expect(
    validateTextureDocument({ ...doc, layers: doc.layers.slice(1) }).lastStroke,
  ).toBe(null);
  expect(
    validateTextureDocument({
      ...doc,
      layers: [createTextureLayer("folder", { id: "surface-detail" })],
    }).lastStroke,
  ).toBe(null);
});

test("malformed strokes, derived channels, foreign image URLs, unbounded points and invalid previews are rejected", () => {
  for (const bad of [
    { ...record(), schema: "javascript" },
    { ...record(), id: "bad id" },
    { ...record(), settings: { toolId: "unknown" } },
    { ...record(), channelId: "normal" },
    {
      ...record(),
      image: { ...png, dataUrl: "https://example.invalid/image.png" },
    },
    { ...record(), image: { ...png, width: 255 } },
    { ...record(), points: [{ x: NaN, y: 0, pressure: 1 }] },
    { ...record(), points: [] },
    {
      ...record(),
      points: Array.from({ length: SOURCE_STROKE_POINT_LIMIT + 1 }, () => ({
        x: 0,
        y: 0,
        pressure: 1,
      })),
    },
  ]) {
    expect(normalizeSourceStroke(bad)).toBe(null);
    expect(() =>
      validateTextureDocument({ ...createTextureDocument(), lastStroke: bad }),
    ).toThrow(/stroke/);
  }
  expect(normalizeTextureSource({ ...png, origin: "javascript" })).toEqual(png);
});

test("source drawing honours zero opacity, zero flow and zero eraser strength, with explicit finite round-tip bounds", () => {
  expect(
    sourceStrokeOptions(normalizePaintSettings({ params: { opacity: 0 } }))
      .alpha,
  ).toBe(0);
  expect(
    sourceStrokeOptions(normalizePaintSettings({ params: { flow: 0 } })).alpha,
  ).toBe(0);
  const eraser = PAINT_TOOLS.find((t) => t.key === "eraser");
  expect(
    sourceStrokeOptions(
      normalizePaintSettings({ toolId: eraser.id, params: { strength: 0 } }),
    ),
  ).toMatchObject({ alpha: 0, operation: "erase" });
  expect(
    sourceStrokeOptions({ params: { size: Infinity, opacity: Infinity } }).size,
  ).toBeLessThanOrEqual(256);
  expect(
    sourceStrokeOptions({ params: { opacity: 50, flow: 20 } }).alpha,
  ).toBeCloseTo(0.1);
});
