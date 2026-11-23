import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
  insertTextureLayer,
  patchTextureLayer,
  moveTextureLayer,
  reorderTextureLayer,
  duplicateTextureLayer,
  removeTextureLayer,
  ungroupTextureFolder,
  textureAncestors,
  textureSubtree,
  isTextureLayerLocked,
  isTextureLayerVisible,
  TEXTURE_FOLDER_DEPTH_LIMIT,
  TEXTURE_LAYER_LIMIT,
} from "../src/textureDocument.js";
import {
  normalizeTextureMask,
  colourMaskMatch,
  colourMaskSwatch,
} from "../src/textureMasks.js";
import {
  normalizeTextureSource,
  rasterDimensions,
} from "../src/textureSource.js";
import { normalizeChannelSettings } from "../src/paintChannelModel.js";
const PNG =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==";
const source = {
  name: "source.png",
  width: 1,
  height: 1,
  dataUrl: `data:image/png;base64,${PNG}`,
};
function grouped() {
  let doc = createTextureDocument();
  const added = insertTextureLayer(doc, "folder", doc.layers[2].id);
  doc = moveTextureLayer(added.doc, "surface-detail", added.id);
  return { doc, id: added.id };
}
function pngHeader(width, height) {
  const b = Buffer.alloc(24);
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]).copy(b);
  b.write("IHDR", 12);
  b.writeUInt32BE(width, 16);
  b.writeUInt32BE(height, 20);
  return b;
}

test("legacy flat projects and shuffled folder records canonicalize to a complete preorder tree", () => {
  const { doc, id } = grouped();
  const nested = createTextureLayer("folder", { id: "nested", parentId: id }),
    leaf = createTextureLayer("paint", {
      id: "nested-leaf",
      parentId: nested.id,
    });
  const input = { ...doc, layers: [leaf, nested, ...doc.layers] };
  const result = validateTextureDocument(input);
  expect(result.layers).toHaveLength(input.layers.length);
  expect(result.layers.map((l) => l.id)).toEqual([
    "surface-finish",
    id,
    "nested",
    "nested-leaf",
    "surface-detail",
    "base-material",
  ]);
  expect(textureAncestors(result, leaf.id).map((l) => l.id)).toEqual([
    "nested",
    id,
  ]);
  expect(textureSubtree(result, id).map((l) => l.id)).toEqual([
    id,
    "nested",
    "nested-leaf",
    "surface-detail",
  ]);
  const legacy = createTextureDocument();
  for (const l of legacy.layers) delete l.parentId;
  expect(
    validateTextureDocument(legacy).layers.every((l) => l.parentId === null),
  ).toBe(true);
  expect(
    validateTextureDocument({
      ...legacy,
      layers: [{ ...legacy.layers[0], parentId: "" }],
    }).layers,
  ).toHaveLength(1);
  expect(parseTextureDocument(JSON.stringify(result))).toEqual(result);
});

test("invalid parents, self/cyclic folders, identities and excessive nesting are rejected, not dropped", () => {
  const doc = createTextureDocument(),
    folder = createTextureLayer("folder", { id: "folder" });
  for (const layers of [
    [{ ...doc.layers[0], parentId: "missing" }],
    [doc.layers[1], { ...doc.layers[0], parentId: doc.layers[1].id }],
    [{ ...folder, parentId: "folder" }],
    [
      { ...folder, parentId: "other" },
      createTextureLayer("folder", { id: "other", parentId: "folder" }),
    ],
    [folder, { ...doc.layers[0], parentId: { id: "folder" } }],
    [folder, folder],
  ])
    expect(() => validateTextureDocument({ ...doc, layers })).toThrow();
  const levels = Array.from(
    { length: TEXTURE_FOLDER_DEPTH_LIMIT + 1 },
    (_, i) =>
      createTextureLayer("folder", {
        id: `level-${i}`,
        parentId: i ? `level-${i - 1}` : null,
      }),
  );
  expect(
    validateTextureDocument({ ...doc, layers: levels }).layers,
  ).toHaveLength(TEXTURE_FOLDER_DEPTH_LIMIT + 1);
  expect(() =>
    validateTextureDocument({
      ...doc,
      layers: [
        ...levels,
        createTextureLayer("paint", { parentId: levels.at(-1).id }),
      ],
    }),
  ).toThrow(/nested levels/);
});

test("folder visibility and lock are inherited without overwriting child settings; subtree operations preserve locks", () => {
  let { doc, id } = grouped();
  doc = patchTextureLayer(doc, id, { locked: true, visible: false });
  expect(isTextureLayerLocked(doc, "surface-detail")).toBe(true);
  expect(isTextureLayerVisible(doc, "surface-detail")).toBe(false);
  expect(doc.layers.find((l) => l.id === "surface-detail")).toMatchObject({
    locked: false,
    visible: true,
  });
  expect(
    patchTextureLayer(doc, "surface-detail", {
      name: "changed",
      channels: [],
      mask: { kind: "color" },
      opacity: 0,
    }),
  ).toBe(doc);
  expect(moveTextureLayer(doc, "surface-detail", null)).toBe(doc);
  expect(duplicateTextureLayer(doc, id).doc).toBe(doc);
  expect(removeTextureLayer(doc, id).doc).toBe(doc);
  expect(ungroupTextureFolder(doc, id)).toBe(doc);
  expect(() => insertTextureLayer(doc, "paint", id)).toThrow(/Unlock/);
  doc = patchTextureLayer(doc, id, { locked: false, visible: true });
  expect(isTextureLayerLocked(doc, "surface-detail")).toBe(false);
  expect(isTextureLayerVisible(doc, "surface-detail")).toBe(true);
  doc = patchTextureLayer(doc, "surface-detail", { locked: true });
  expect(removeTextureLayer(doc, id).doc).toBe(doc);
  expect(duplicateTextureLayer(doc, id).doc).toBe(doc);
  expect(moveTextureLayer(doc, id, null, "surface-finish")).toBe(doc);
  expect(ungroupTextureFolder(doc, id)).toBe(doc);
});

test("folders copy/delete complete independent subtrees, masks and portable texture sources", () => {
  let { doc, id } = grouped();
  const nested = insertTextureLayer(doc, "folder", id);
  doc = moveTextureLayer(nested.doc, "surface-detail", nested.id);
  doc = patchTextureLayer(doc, id, {
    mask: {
      kind: "color",
      color: "#ff0000",
      strength: 0,
      tolerance: 0,
      softness: 0,
    },
  });
  doc = patchTextureLayer(doc, "surface-detail", {
    channelSettings: normalizeChannelSettings({
      baseColor: { mode: "texture", texture: source },
    }),
  });
  const copied = duplicateTextureLayer(doc, id),
    tree = textureSubtree(copied.doc, copied.id);
  expect(tree).toHaveLength(3);
  expect(copied.doc.layers).toHaveLength(doc.layers.length + 3);
  expect(tree[0].name).toMatch(/copy$/);
  expect(tree[1].parentId).toBe(copied.id);
  expect(tree[2].parentId).toBe(tree[1].id);
  expect(tree[0].mask).toEqual(doc.layers.find((l) => l.id === id).mask);
  expect(tree[0].mask).not.toBe(doc.layers.find((l) => l.id === id).mask);
  expect(tree[2].channelSettings.baseColor.texture).toEqual(source);
  expect(tree[2].channelSettings.baseColor.texture).not.toBe(
    doc.layers.find((l) => l.id === "surface-detail").channelSettings.baseColor
      .texture,
  );
  const removed = removeTextureLayer(copied.doc, copied.id);
  expect(removed.doc).toEqual(doc);
  expect(new Set(copied.doc.layers.map((l) => l.id)).size).toBe(
    copied.doc.layers.length,
  );
});

test("folder membership and subtree ordering are atomic; cycles and foreign before-targets are no-ops", () => {
  let { doc, id } = grouped();
  const folder = insertTextureLayer(doc, "folder", id);
  doc = folder.doc;
  expect(moveTextureLayer(doc, id, folder.id)).toBe(doc);
  expect(reorderTextureLayer(doc, id, "surface-detail")).toBe(doc);
  expect(moveTextureLayer(doc, "surface-detail", id, "base-material")).toBe(
    doc,
  );
  expect(moveTextureLayer(doc, "surface-detail", "base-material")).toBe(doc);
  const after = moveTextureLayer(doc, "surface-finish", id, "surface-detail");
  expect(after.layers.map((l) => l.id)).toEqual([
    id,
    folder.id,
    "surface-finish",
    "surface-detail",
    "base-material",
  ]);
  const root = moveTextureLayer(after, "surface-detail", null, id);
  expect(root.layers[0]).toMatchObject({
    id: "surface-detail",
    parentId: null,
  });
  const ungrouped = ungroupTextureFolder(after, id);
  expect(ungrouped.layers.map((l) => l.id)).toEqual([
    folder.id,
    "surface-finish",
    "surface-detail",
    "base-material",
  ]);
  expect(ungrouped.layers.every((l) => l.parentId === null)).toBe(true);
  expect(doc.layers.find((l) => l.id === "surface-finish").parentId).toBe(null);
});

test("folder copies and imported source bytes obey record/depth/document limits on every route", () => {
  const { doc, id } = grouped();
  const full = validateTextureDocument({
    ...doc,
    layers: [
      ...doc.layers,
      ...Array.from(
        { length: TEXTURE_LAYER_LIMIT - doc.layers.length },
        (_, i) => createTextureLayer("paint", { id: `extra-${i}` }),
      ),
    ],
  });
  expect(() => duplicateTextureLayer(full, id)).toThrow(/limit/);
  expect(() => insertTextureLayer(full, "folder")).toThrow(/limit/);
  const large = {
    ...source,
    dataUrl:
      "data:image/png;base64," +
      Buffer.concat([
        Buffer.from(PNG, "base64"),
        Buffer.alloc(210000),
      ]).toString("base64"),
  };
  expect(normalizeTextureSource(large)).toEqual(large);
  const heavy = {
    ...createTextureDocument(),
    layers: Array.from({ length: 4 }, (_, i) =>
      createTextureLayer("paint", {
        id: `large-${i}`,
        channelSettings: { baseColor: { mode: "texture", texture: large } },
      }),
    ),
  };
  expect(() => validateTextureDocument(heavy)).toThrow(/1 MB/);
});

test("fill and colour masks retain exact zero, finite bounds, inversion and explicit normalized sRGB matching", () => {
  expect(
    normalizeTextureMask({
      kind: "color",
      strength: 0,
      tolerance: 0,
      softness: 0,
      color: "#FF0000",
    }),
  ).toMatchObject({
    kind: "color",
    strength: 0,
    tolerance: 0,
    softness: 0,
    color: "#ff0000",
  });
  expect(
    normalizeTextureMask({
      strength: Infinity,
      tolerance: 10,
      softness: -2,
      color: "javascript:bad",
    }),
  ).toMatchObject({
    strength: 100,
    tolerance: 1,
    softness: 0,
    color: "#b87333",
  });
  expect(normalizeTextureMask({ kind: "script", fill: 0 })).toMatchObject({
    kind: "fill",
    fill: 0,
  });
  const mask = { kind: "color", color: "#ff0000", tolerance: 0, softness: 0 };
  expect(colourMaskMatch([1, 0, 0], mask)).toBe(1);
  expect(colourMaskMatch([0, 0, 1], mask)).toBe(0);
  expect(colourMaskMatch([1, 0, 0], { ...mask, inverted: true })).toBe(0);
  expect(colourMaskSwatch("#ff0000", mask)).toBe(1);
  expect(colourMaskSwatch("#ff0000", { ...mask, tolerance: 1 })).toBe(1);
  expect(
    colourMaskMatch([0.5, 0, 0], { ...mask, softness: 1 }),
  ).toBeGreaterThan(0);
  expect(colourMaskMatch([NaN, 0, 0], mask)).toBe(0);
  expect(colourMaskSwatch("bad", mask)).toBe(0);
});

test("Texture accepts legacy Painted sources, bounds portable raster metadata and rejects executable or foreign URLs", () => {
  expect(
    normalizeChannelSettings({ baseColor: { mode: "paint", texture: source } })
      .baseColor,
  ).toMatchObject({ mode: "texture", texture: source });
  expect(
    normalizeTextureSource({ ...source, name: "abc\u0000.png" }).name,
  ).toBe("abc.png");
  for (const bad of [
    { ...source, dataUrl: "https://example.invalid/image.png" },
    { ...source, dataUrl: "data:image/svg+xml;base64,PHN2Zz4=" },
    { ...source, width: 2 },
    { ...source, width: 9000 },
    { ...source, dataUrl: `data:image/png;base64,${"A".repeat(300001)}` },
    { ...source, dataUrl: "data:image/png;base64,PHNjcmlwdD4=" },
  ])
    expect(normalizeTextureSource(bad)).toBe(null);
  expect(rasterDimensions(new Uint8Array(Buffer.from(PNG, "base64")))).toEqual([
    1, 1,
  ]);
  expect(() => rasterDimensions(pngHeader(9000, 1))).toThrow(/8192/);
  expect(() => rasterDimensions(pngHeader(8192, 8192))).toThrow(/16 million/);
  expect(() => rasterDimensions(Buffer.from('<svg onload="evil()"/>'))).toThrow(
    /raster/,
  );
});

test("JPEG and all WebP header variants expose bounded dimensions before browser decoding", () => {
  const jpeg = Buffer.from([
    255, 216, 255, 192, 0, 17, 8, 1, 0, 2, 0, 3, 1, 17, 0, 2, 17, 0, 3, 17, 0,
  ]);
  expect(rasterDimensions(jpeg)).toEqual([512, 256]);
  const webp = (tag) => {
    const b = Buffer.alloc(30);
    b.write("RIFF");
    b.writeUInt32LE(22, 4);
    b.write("WEBP", 8);
    b.write(tag, 12);
    return b;
  };
  const extended = webp("VP8X");
  extended[24] = 255;
  extended[25] = 1;
  extended[27] = 255;
  expect(rasterDimensions(extended)).toEqual([512, 256]);
  const lossy = webp("VP8 ");
  lossy[26] = 0;
  lossy[27] = 2;
  lossy[28] = 0;
  lossy[29] = 1;
  expect(rasterDimensions(lossy)).toEqual([512, 256]);
  const lossless = webp("VP8L");
  lossless[20] = 47;
  lossless[21] = 255;
  lossless[22] = 193;
  lossless[23] = 63;
  expect(rasterDimensions(lossless)).toEqual([512, 256]);
  extended[24] = 255;
  extended[25] = 255;
  extended[26] = 255;
  expect(() => rasterDimensions(extended)).toThrow(/8192/);
  expect(() => rasterDimensions(jpeg.subarray(0, 12))).toThrow(/dimensions/);
});
