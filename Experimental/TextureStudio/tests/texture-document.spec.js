import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
  insertTextureLayer,
  duplicateTextureLayer,
  removeTextureLayer,
  reorderTextureLayer,
  patchTextureLayer,
  TEXTURE_LAYER_LIMIT,
} from "../src/textureDocument.js";

test("texture document has stable ordered identities, no image allocation and a teapot default", () => {
  const doc = createTextureDocument();
  expect(doc.scene).toBe("teapot");
  expect(doc.layers.map((l) => l.kind)).toEqual(["paint", "fill", "material"]);
  expect(new Set(doc.layers.map((l) => l.id)).size).toBe(3);
  expect(validateTextureDocument(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
  expect(JSON.stringify(doc)).not.toMatch(/data:image|atlas|textureData/);
  expect(doc.lastDigitalStroke).toBeNull();
  expect(doc.layers.every((layer) => !layer.paint)).toBe(true);
});
test("texture document validation rejects foreign schemas, duplicate identities, unknown kinds and large files", () => {
  const doc = createTextureDocument();
  for (const input of [
    {},
    { ...doc, schema: "alloy.pattern.v1" },
    { ...doc, layers: [doc.layers[0], doc.layers[0]] },
    { ...doc, layers: [{ ...doc.layers[0], kind: "__proto__" }] },
    { ...doc, layers: [{ ...doc.layers[0], kind: ["paint"] }] },
    { ...doc, layers: [{ ...doc.layers[0], id: "not a valid id" }] },
  ])
    expect(() => validateTextureDocument(input)).toThrow();
  expect(() => parseTextureDocument("no JSON")).toThrow(/valid JSON/);
  expect(() => parseTextureDocument(" ".repeat(1024 * 1024 + 1))).toThrow(
    /1 MB/,
  );
});
test("texture document bounds zero opacity and masks, sanitizes names, and accepts only declared channels", () => {
  const doc = createTextureDocument();
  const l = doc.layers[0];
  const result = patchTextureLayer(doc, l.id, {
    opacity: 0,
    blend: "Screen",
    name: "  Fine detail\u0000  ",
    channels: ["baseColor", "baseColor", "not-a-channel"],
    mask: { fill: 0, strength: 0, inverted: true },
  });
  expect(result.layers[0]).toMatchObject({
    name: "Fine detail",
    opacity: 0,
    blend: "Screen",
    channels: ["baseColor"],
    mask: { fill: 0, strength: 0, inverted: true, enabled: true },
  });
  expect(
    patchTextureLayer(result, l.id, { opacity: Infinity }).layers[0].opacity,
  ).toBe(100);
  expect(
    patchTextureLayer(result, l.id, { opacity: -10 }).layers[0].opacity,
  ).toBe(0);
  expect(
    patchTextureLayer(result, l.id, { opacity: 200, mask: { strength: 200 } })
      .layers[0],
  ).toMatchObject({ opacity: 100, mask: { strength: 100 } });
  expect(doc.layers[0].mask).toBe(null);
});
test("texture layer insertion and duplication keep top-to-bottom order and independent masks", () => {
  const doc = createTextureDocument();
  const added = insertTextureLayer(doc, "generator", doc.layers[1].id);
  expect(added.doc.layers[1].id).toBe(added.id);
  const masked = patchTextureLayer(added.doc, added.id, {
    mask: { fill: 0, inverted: true, strength: 45 },
  });
  const copy = duplicateTextureLayer(masked, added.id);
  expect(copy.id).not.toBe(added.id);
  expect(copy.doc.layers[1].name).toBe("Generator layer copy");
  expect(copy.doc.layers[1].mask).toEqual(copy.doc.layers[2].mask);
  expect(copy.doc.layers[1].mask).not.toBe(copy.doc.layers[2].mask);
  expect(doc.layers).toHaveLength(3);
});
test("texture locked layers are protected while their eyes and lock remain editable", () => {
  let doc = createTextureDocument();
  const id = doc.layers[0].id;
  doc = patchTextureLayer(doc, id, { locked: true });
  expect(
    patchTextureLayer(doc, id, {
      opacity: 12,
      name: "Changed",
      mask: { fill: 0 },
    }),
  ).toBe(doc);
  expect(duplicateTextureLayer(doc, id).doc).toBe(doc);
  expect(removeTextureLayer(doc, id).doc).toBe(doc);
  expect(reorderTextureLayer(doc, id, null)).toBe(doc);
  doc = patchTextureLayer(doc, id, { visible: false });
  expect(doc.layers[0]).toMatchObject({
    visible: false,
    locked: true,
    opacity: 100,
  });
  expect(patchTextureLayer(doc, id, { locked: false }).layers[0].locked).toBe(
    false,
  );
});
test("texture reorders and deletes use identities rather than row positions or names", () => {
  const doc = createTextureDocument();
  const [a, b, c] = doc.layers.map((l) => l.id);
  let reordered = reorderTextureLayer(doc, c, a);
  expect(reordered.layers.map((l) => l.id)).toEqual([c, a, b]);
  reordered = reorderTextureLayer(reordered, c, null);
  expect(reordered.layers.map((l) => l.id)).toEqual([a, b, c]);
  expect(reorderTextureLayer(reordered, a, "missing")).toBe(reordered);
  expect(reorderTextureLayer(reordered, a, a)).toBe(reordered);
  const removed = removeTextureLayer(reordered, b);
  expect(removed.doc.layers.map((l) => l.id)).toEqual([a, c]);
  expect(removed.id).toBe(c);
  expect(doc.layers).toHaveLength(3);
});
test("texture capacity is enforced in every construction route and empty projects stay valid", () => {
  const doc = {
    ...createTextureDocument(),
    layers: Array.from({ length: TEXTURE_LAYER_LIMIT }, (_, i) =>
      createTextureLayer("paint", { id: `test-${i}` }),
    ),
  };
  expect(() => insertTextureLayer(doc, "paint")).toThrow(/limit/);
  expect(() => duplicateTextureLayer(doc, doc.layers[0].id)).toThrow(/limit/);
  expect(() =>
    validateTextureDocument({
      ...doc,
      layers: [...doc.layers, createTextureLayer("fill")],
    }),
  ).toThrow(/64/);
  const empty = { ...doc, layers: [] };
  expect(validateTextureDocument(empty).layers).toEqual([]);
  expect(insertTextureLayer(empty, "material").doc.layers).toHaveLength(1);
});
