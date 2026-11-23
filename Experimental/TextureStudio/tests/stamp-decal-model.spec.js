import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { PAINT_CHANNELS } from "../src/paintChannelModel.js";
import {
  createStampDocument,
  createStampLayer,
  normalizeStampDocument,
  parseStampDocument,
  normalizeStampPreset,
  normalizeStampRuns,
  formatStampRuns,
  replaceStampText,
  normalizeStampChannels,
  STAMP_FILE_LIMIT,
} from "../src/stampDocument.js";
import {
  normalizeDecal,
  createDecal,
  collectDecalRecords,
  DECAL_LIMIT,
} from "../src/decalModel.js";
import {
  createTextureDocument,
  validateTextureDocument,
  createTextureLayer,
  insertTextureLayer,
  addTextureDecal,
  patchTextureDecal,
  removeTextureDecal,
  duplicateTextureLayer,
  removeTextureLayer,
  isTextureLayerVisible,
  moveTextureLayer,
  textureLayerDropPosition,
  patchTextureLayer,
  parseTextureDocument,
} from "../src/textureDocument.js";
import {
  decalLocalProjector,
  projectDecalGeometry,
  updateSurfaceMaskUniforms,
  surfaceMaskOwners,
} from "../src/decalProjection.js";
import {
  createTextureHistory,
  commitTextureHistory,
  travelTextureHistory,
} from "../src/textureHistory.js";
const png = {
  name: "ink.png",
  width: 1,
  height: 1,
  dataUrl:
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==",
};
const preset = () =>
  normalizeStampPreset({ project: createStampDocument(), raster: png });
const text = (runs) => runs.map((r) => r.text).join("");

test("stamp projects preserve mixed runs, static SVG and embedded images without retaining foreign fields", () => {
  const doc = createStampDocument();
  doc.layers[0].runs = [
    { text: "AL", fontFamily: "DM Sans", fontSize: 64, color: "#ff0000" },
    {
      text: "LOY\n01",
      fontFamily: "Space Grotesk",
      fontSize: 24,
      fontWeight: 600,
      color: "#00ff00",
    },
  ];
  doc.layers.push(
    createStampLayer("image", { image: png, script: "alert(1)" }),
  );
  const next = parseStampDocument(JSON.stringify(doc));
  expect(next.layers[0].runs.map((r) => [r.fontFamily, r.fontSize])).toEqual([
    ["DM Sans", 64],
    ["Space Grotesk", 24],
  ]);
  expect(text(next.layers[0].runs)).toBe("ALLOY\n01");
  expect(next.layers.at(-1).image).toEqual(png);
  expect(next.layers.at(-1)).not.toHaveProperty("script");
});
test("character-range formatting splits and coalesces styles, preserves unselected text and supports empty text", () => {
  const runs = normalizeStampRuns([{ text: "ALLOY maker", fontSize: 48 }]);
  const mixed = formatStampRuns(runs, 6, 11, {
    fontFamily: "Space Grotesk",
    fontSize: 24,
    fontWeight: 600,
  });
  expect(mixed.map((r) => r.text)).toEqual(["ALLOY ", "maker"]);
  expect(mixed[0].fontSize).toBe(48);
  expect(mixed[1].fontSize).toBe(24);
  const formatted = formatStampRuns(mixed, 0, 11, {
    fontFamily: "DM Sans",
    fontSize: 48,
    fontWeight: 400,
  });
  expect(formatted).toHaveLength(1);
  expect(text(formatted)).toBe("ALLOY maker");
  const empty = formatStampRuns(
    [{ text: "", fontFamily: "Space Grotesk", fontSize: 32 }],
    0,
    0,
    { fontSize: 88 },
  );
  expect(empty[0].fontSize).toBe(88);
  expect(empty[0].fontFamily).toBe("Space Grotesk");
});
test("rich-text insertion and replacement keep style boundaries and literal markup as text, not executable content", () => {
  const runs = normalizeStampRuns([
    { text: "AB", fontSize: 64 },
    { text: "CD", fontSize: 24 },
  ]);
  const next = replaceStampText(runs, 1, 3, "\n<script>", {
    fontFamily: "Space Grotesk",
    fontSize: 16,
  });
  expect(text(next)).toBe("A\n<script>D");
  expect(next[0].fontSize).toBe(64);
  expect(next.at(-1).fontSize).toBe(24);
  expect(() => normalizeStampRuns([{ text: "a".repeat(1025) }])).toThrow(
    /1024/,
  );
  expect(() =>
    normalizeStampRuns(Array.from({ length: 129 }, () => ({ text: "x" }))),
  ).toThrow(/128/);
});
test("stamp validators reject scripts, missing/duplicate identities, foreign rasters, caps and oversized files", () => {
  const doc = createStampDocument();
  expect(() =>
    normalizeStampDocument({ ...doc, layers: [...doc.layers, doc.layers[0]] }),
  ).toThrow(/unique/);
  expect(() => createStampLayer("text", { id: undefined })).toThrow(/identity/);
  expect(() =>
    createStampLayer("image", {
      image: { ...png, dataUrl: "https://example.invalid/a.png" },
    }),
  ).toThrow(/embedded PNG/);
  for (const svg of [
    "<svg><script>alert(1)</script></svg>",
    '<svg onload="alert(1)"/>',
    "<!DOCTYPE svg><svg/>",
  ])
    expect(() => createStampLayer("svg", { svg })).toThrow();
  expect(() =>
    normalizeStampDocument({
      ...doc,
      layers: Array.from({ length: 25 }, () => createStampLayer()),
    }),
  ).toThrow(/24/);
  expect(() => parseStampDocument(" ".repeat(STAMP_FILE_LIMIT + 1))).toThrow(
    /600 KB/,
  );
});
test("all 14 channel targets can be toggled independently with the Normal/Height dependency and exact zero retained", () => {
  const doc = normalizeStampDocument({
    ...createStampDocument(),
    channels: PAINT_CHANNELS.map((c) => c.id),
    channelValues: {
      roughness: 0,
      opacity: 0,
      metalness: 1,
      anisotropyAngle: 360,
    },
    layers: [],
  });
  expect(doc.channels).toHaveLength(14);
  expect(doc.channelValues.roughness).toBe(0);
  expect(doc.channelValues.opacity).toBe(0);
  expect(normalizeStampChannels(["normal"])).toEqual(["height", "normal"]);
  expect(normalizeStampChannels([])).toEqual([]);
  expect(
    normalizeStampDocument({ ...doc, channels: [] }).channelValues,
  ).toEqual(doc.channelValues);
});
test("decals store finite normalized object-space transforms, clamp geometry and reject identities/foreign coordinate spaces", () => {
  const d = createDecal(preset(), {
    position: [Infinity, -8, 0.5],
    quaternion: [0, 0, 0, 0],
    size: [0, 8],
    opacity: 0,
    roll: 999,
  });
  expect(d.position).toEqual([0, -4, 0.5]);
  expect(d.quaternion).toEqual([0, 0, 0, 1]);
  expect(d.size).toEqual([0.01, 4]);
  expect(d.opacity).toBe(0);
  expect(d.roll).toBe(180);
  expect(() => normalizeDecal({ ...d, space: "screen" })).toThrow(
    /object-space/,
  );
  expect(() => normalizeDecal({ ...d, id: undefined })).toThrow(/identity/);
});
test("a regular layer hosts decals and a Decal layer hosts many placements while deduplicating the immutable stamp snapshot", () => {
  let doc = createTextureDocument();
  const source = preset();
  doc = addTextureDecal(doc, "base-material", source).doc;
  const added = insertTextureLayer(doc, "decal", "surface-detail");
  doc = added.doc;
  for (let i = 0; i < 10; i++)
    doc = addTextureDecal(doc, added.id, source, {
      position: [i * 0.04, 0, 1],
    }).doc;
  expect(doc.stamps).toHaveLength(1);
  expect(doc.layers.find((l) => l.id === added.id).decals).toHaveLength(10);
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
});
test("re-saving changed artwork does not mutate placed decals or allocate another snapshot for every new dab", () => {
  const source = preset();
  let doc = addTextureDecal(
    createTextureDocument(),
    "base-material",
    source,
  ).doc;
  const changed = {
    ...source,
    project: { ...source.project, name: "New mark" },
  };
  doc = addTextureDecal(doc, "base-material", changed).doc;
  doc = addTextureDecal(doc, "base-material", changed).doc;
  expect(doc.stamps).toHaveLength(2);
  const decals = doc.layers.at(-1).decals;
  expect(decals[0].stampId).not.toBe(decals[1].stampId);
  expect(decals[1].stampId).toBe(decals[2].stampId);
  expect(doc.stamps[0].project.name).toBe(source.name);
});
test("folder/layer/component locks protect decal mutations; eyes and unlocking remain available", () => {
  const source = preset();
  let doc = addTextureDecal(
      createTextureDocument(),
      "base-material",
      source,
    ).doc,
    id = doc.layers.at(-1).decals[0].id;
  doc = patchTextureDecal(doc, "base-material", id, { locked: true });
  expect(patchTextureDecal(doc, "base-material", id, { opacity: 0.2 })).toBe(
    doc,
  );
  expect(removeTextureDecal(doc, "base-material", id)).toBe(doc);
  doc = patchTextureDecal(doc, "base-material", id, { visible: false });
  expect(doc.layers.at(-1).decals[0].visible).toBe(false);
  doc = patchTextureDecal(doc, "base-material", id, { locked: false });
  doc = patchTextureLayer(doc, "base-material", { locked: true });
  expect(addTextureDecal(doc, "base-material", source).doc).toBe(doc);
  expect(patchTextureDecal(doc, "base-material", id, { opacity: 0.8 })).toBe(
    doc,
  );
});
test("layer duplication creates independent component identities, removal prunes only unused stamp snapshots", () => {
  let doc = addTextureDecal(
    createTextureDocument(),
    "base-material",
    preset(),
  ).doc;
  const duplicated = duplicateTextureLayer(doc, "base-material");
  doc = duplicated.doc;
  const original = doc.layers.find((l) => l.id === "base-material"),
    copy = doc.layers.find((l) => l.id === duplicated.id);
  expect(copy.decals[0].id).not.toBe(original.decals[0].id);
  expect(copy.decals[0].stampId).toBe(original.decals[0].stampId);
  expect(copy.decals[0]).not.toBe(original.decals[0]);
  doc = removeTextureLayer(doc, duplicated.id).doc;
  expect(doc.stamps).toHaveLength(1);
  doc = removeTextureDecal(doc, "base-material", original.decals[0].id);
  expect(doc.stamps).toHaveLength(0);
});
test("decal caps, total byte budgets, missing stamp references and cross-layer identity collisions cannot bypass validation", () => {
  const source = preset();
  let doc = createTextureDocument();
  for (let i = 0; i < DECAL_LIMIT; i++)
    doc = addTextureDecal(doc, "base-material", source).doc;
  expect(() => addTextureDecal(doc, "surface-detail", source)).toThrow(/64/);
  expect(() => validateTextureDocument({ ...doc, stamps: [] })).toThrow(
    /missing stamp/,
  );
  const bad = structuredClone(doc);
  bad.layers[0].decals = [bad.layers.at(-1).decals[0]];
  expect(() => validateTextureDocument(bad)).toThrow(/64|unique/);
});
test("channel-disabled decals are muted, mask stamps still work and hidden folders suppress their whole subtree", () => {
  let doc = createTextureDocument();
  const source = preset();
  const group = insertTextureLayer(doc, "folder", "surface-detail");
  doc = group.doc;
  doc = moveTextureLayer(doc, "base-material", group.id);
  doc = addTextureDecal(doc, "base-material", source, { channels: [] }).doc;
  doc = addTextureDecal(doc, "base-material", source, {
    channels: [],
    asMask: true,
  }).doc;
  expect(collectDecalRecords(doc, isTextureLayerVisible)).toHaveLength(1);
  doc = patchTextureLayer(doc, group.id, { visible: false });
  expect(collectDecalRecords(doc, isTextureLayerVisible)).toHaveLength(0);
});
test("projected triangles are surface-clipped, forward-facing and specimen-local even on a translated/rotated mesh", () => {
  const mesh = new THREE.Mesh(new THREE.SphereGeometry(1, 40, 30));
  mesh.position.set(2, 3, -4);
  mesh.rotation.set(0.2, 0.4, 0);
  const d = createDecal(preset(), {
    position: [0, 0, 1],
    size: [0.6, 0.6],
    depth: 0.4,
  });
  const geometry = projectDecalGeometry(mesh, d);
  expect(geometry.attributes.position.count).toBeGreaterThan(0);
  const inverse = decalLocalProjector(mesh, d).invert();
  for (let i = 0; i < geometry.attributes.position.count; i++) {
    const p = new THREE.Vector3()
      .fromBufferAttribute(geometry.attributes.position, i)
      .applyMatrix4(inverse);
    expect(Math.abs(p.x)).toBeLessThanOrEqual(0.50001);
    expect(Math.abs(p.y)).toBeLessThanOrEqual(0.50001);
    expect(Math.abs(p.z)).toBeLessThanOrEqual(0.50001);
    expect(geometry.attributes.normal.getZ(i)).toBeGreaterThan(0.7);
    expect(geometry.attributes.uv.getX(i)).toBeGreaterThanOrEqual(-0.00001);
    expect(geometry.attributes.uv.getX(i)).toBeLessThanOrEqual(1.00001);
  }
  geometry.dispose();
  mesh.geometry.dispose();
  mesh.material.dispose();
});
test("surface-mask GPU data packs true inverse projector transforms, atlas rectangles, mask mode and owner identities", () => {
  const mesh = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 16));
  const source = preset();
  let doc = addTextureDecal(createTextureDocument(), "base-material", source, {
    asMask: true,
    inverted: true,
    opacity: 0.7,
  }).doc;
  const uniforms = {
    uSurfaceStampData: {
      value: new THREE.DataTexture(
        new Float32Array(7 * 64 * 4),
        7,
        64,
        THREE.RGBAFormat,
        THREE.FloatType,
      ),
    },
    uSurfaceStampCount: { value: 0 },
  };
  updateSurfaceMaskUniforms(uniforms, collectDecalRecords(doc), mesh, [
    source.id,
  ]);
  expect(uniforms.uSurfaceStampCount.value).toBe(1);
  const data = uniforms.uSurfaceStampData.value.image.data;
  const projected = new THREE.Vector3(0, 0, 1).applyMatrix4(
    new THREE.Matrix4().fromArray(data.slice(0, 16)),
  );
  expect(projected.length()).toBeLessThan(0.00001);
  expect(data[21]).toBe(1);
  expect(data[22]).toBeCloseTo(0.7);
  expect(data[27]).toBe(3);
  expect(
    surfaceMaskOwners(doc, "base-material").uSurfaceStampOwners.value[0],
  ).toBe(3);
  uniforms.uSurfaceStampData.value.dispose();
  mesh.geometry.dispose();
  mesh.material.dispose();
});
test("before/after/inside drop positions use siblings, atomically move complete folder subtrees and reject self/cycles/locks", () => {
  const folder = createTextureLayer("folder", { id: "folder" }),
    child = createTextureLayer("paint", { id: "child", parentId: "folder" });
  let doc = validateTextureDocument({
    ...createTextureDocument(),
    layers: [folder, child, ...createTextureDocument().layers],
  });
  const move = (id, target, zone) => {
    const drop = textureLayerDropPosition(doc, id, target, zone);
    return drop ? moveTextureLayer(doc, id, drop.parentId, drop.beforeId) : doc;
  };
  doc = move("base-material", "surface-detail", "after");
  expect(doc.layers.map((l) => l.id)).toEqual([
    "folder",
    "child",
    "surface-detail",
    "base-material",
    "surface-finish",
  ]);
  doc = move("surface-detail", "folder", "inside");
  expect(doc.layers.find((l) => l.id === "surface-detail").parentId).toBe(
    "folder",
  );
  expect(textureLayerDropPosition(doc, "folder", "child", "after")).toBeNull();
  expect(
    textureLayerDropPosition(doc, "folder", "folder", "inside"),
  ).toBeNull();
  doc = move("folder", "surface-finish", "after");
  expect(doc.layers.at(-1).id).toBe("surface-detail");
  const same = move("base-material", "surface-finish", "before");
  expect(same).toBe(doc);
  doc = patchTextureLayer(doc, "folder", { locked: true });
  expect(
    textureLayerDropPosition(doc, "base-material", "folder", "inside"),
  ).toBeNull();
});
test("a surface placement and a transform gesture are undoable whole-document transactions with portable stamp snapshots", () => {
  const before = createTextureDocument(),
    added = addTextureDecal(before, "base-material", preset());
  let history = createTextureHistory({
    doc: before,
    selection: { id: "base-material", mask: false },
  });
  history = commitTextureHistory(
    history,
    { doc: added.doc, selection: history.present.selection },
    { label: "Stamp surface decal" },
  );
  const transformed = patchTextureDecal(added.doc, "base-material", added.id, {
    position: [0.2, 0.1, 0.8],
    size: [0.5, 0.6],
  });
  history = commitTextureHistory(
    history,
    { doc: transformed, selection: history.present.selection },
    { label: "Transform surface decal" },
  );
  history = travelTextureHistory(history, "undo");
  expect(history.present.doc).toEqual(added.doc);
  history = travelTextureHistory(history, "undo");
  expect(history.present.doc).toEqual(before);
  history = travelTextureHistory(history, "redo");
  expect(history.present.doc.stamps).toHaveLength(1);
});

test("explicit artwork replacement retains instance transforms, prunes the old snapshot and cannot alter component identities through a patch", async () => {
  const { replaceTextureDecalStamp } =
    await import("../src/textureDocument.js");
  let doc = addTextureDecal(
    createTextureDocument(),
    "base-material",
    preset(),
    { position: [0.3, 0.2, 0.8], size: [0.45, 0.6], asMask: true, roll: 30 },
  ).doc;
  const old = doc.layers.at(-1).decals[0],
    replacement = preset();
  replacement.project.name = "Replacement";
  replacement.project.channels = ["normal"];
  doc = replaceTextureDecalStamp(doc, "base-material", old.id, replacement);
  const next = doc.layers.at(-1).decals[0];
  expect(next.id).toBe(old.id);
  expect(next.position).toEqual(old.position);
  expect(next.size).toEqual(old.size);
  expect(next.roll).toBe(30);
  expect(next.asMask).toBe(true);
  expect(next.channels).toEqual(["height", "normal"]);
  expect(doc.stamps).toHaveLength(1);
  expect(doc.stamps[0].name).toBe("Replacement");
  doc = patchTextureDecal(doc, "base-material", next.id, {
    id: "foreign",
    stampId: "missing",
    space: "screen",
  });
  expect(doc.layers.at(-1).decals[0].id).toBe(old.id);
  expect(doc.layers.at(-1).decals[0].space).toBe("object-sphere");
});

test("sixteen distinct embedded snapshots remain bounded; replacing an exclusively used snapshot at the cap does not require a seventeenth slot", async () => {
  const { replaceTextureDecalStamp } =
    await import("../src/textureDocument.js");
  let doc = createTextureDocument();
  for (let i = 0; i < 16; i++) {
    const p = preset();
    p.project.name = `Snapshot ${i}`;
    doc = addTextureDecal(doc, "base-material", p).doc;
  }
  expect(doc.stamps).toHaveLength(16);
  const more = preset();
  more.project.name = "Another snapshot";
  expect(() => addTextureDecal(doc, "base-material", more)).toThrow(/16/);
  doc = replaceTextureDecalStamp(
    doc,
    "base-material",
    doc.layers.at(-1).decals[0].id,
    more,
  );
  expect(doc.stamps).toHaveLength(16);
  expect(doc.stamps.some((s) => s.name === "Another snapshot")).toBe(true);
});

test("locked decal descendants protect layer/subtree moves, copies and deletion as well as their own inspector", () => {
  let doc = addTextureDecal(
    createTextureDocument(),
    "base-material",
    preset(),
    { locked: true },
  ).doc;
  expect(
    textureLayerDropPosition(doc, "base-material", "surface-detail", "before"),
  ).toBeNull();
  expect(moveTextureLayer(doc, "base-material", null, "surface-detail")).toBe(
    doc,
  );
  expect(duplicateTextureLayer(doc, "base-material").doc).toBe(doc);
  expect(removeTextureLayer(doc, "base-material").doc).toBe(doc);
});

test("drop feedback rejects over-depth folder nesting before mutating the tree", () => {
  const layers = Array.from({ length: 8 }, (_, i) =>
    createTextureLayer("folder", {
      id: `deep-${i}`,
      parentId: i ? `deep-${i - 1}` : null,
    }),
  );
  layers.push(
    createTextureLayer("folder", { id: "source" }),
    createTextureLayer("paint", { id: "source-child", parentId: "source" }),
  );
  const doc = validateTextureDocument({ ...createTextureDocument(), layers });
  expect(
    textureLayerDropPosition(doc, "source", "deep-7", "inside"),
  ).toBeNull();
  expect(
    textureLayerDropPosition(doc, "source-child", "deep-7", "inside"),
  ).toEqual({ parentId: "deep-7", beforeId: null });
});

test("canonical transforms are idempotent and imported stamp projects without an identity get a real stable string ID", () => {
  const p = createStampDocument();
  delete p.id;
  const project = normalizeStampDocument(p);
  expect(typeof project.id).toBe("string");
  expect(normalizeStampDocument(project)).toEqual(project);
  const d = createDecal(preset(), { quaternion: [0.4, 0.2, 0.7, 0.13] });
  expect(normalizeDecal(normalizeDecal(d))).toEqual(d);
  const doc = addTextureDecal(
    createTextureDocument(),
    "base-material",
    preset(),
    d,
  ).doc;
  expect(validateTextureDocument(validateTextureDocument(doc))).toEqual(doc);
});
