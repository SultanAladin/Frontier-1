import { Color } from "three";
import { withMaskAtlasState } from "../src/maskPreviewController.js";
import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  validateTextureDocument,
  parseTextureDocument,
  patchTextureLayer,
  duplicateTextureLayer,
  createTextureLayer,
  isTextureLayerLocked,
} from "../src/textureDocument.js";
import { normalizeTextureMask } from "../src/textureMasks.js";
import {
  normalizeMaskPaint,
  normalizeMaskRegion,
  maskRegionFromHit,
  maskRegionContains,
  appendMaskOperation,
  MASK_OPERATION_LIMIT,
  MASK_POINT_LIMIT,
  MASK_TOTAL_POINT_LIMIT,
} from "../src/textureMaskPaint.js";
import {
  createTextureTeapotGeometry,
  AUTHORING_UV_SET,
} from "../src/textureSurfaceModel.js";
import {
  uvTopology,
  uvSurfaceHit,
  surfaceHitDetails,
} from "../src/textureUVGeometry.js";
import * as THREE from "three";
import {
  createTextureHistory,
  commitTextureHistory,
  travelTextureHistory,
} from "../src/textureHistory.js";
const geometry = createTextureTeapotGeometry(20),
  topology = uvTopology(geometry),
  lidTriangle = topology.triangles.find(
    (t) => t.part.id === "lid" && t.chartId === 25,
  ),
  bodyTriangle = topology.triangles.find((t) => t.part.id === "body"),
  centroid = (t) =>
    [0, 1].map((c) => t.points.reduce((s, p) => s + p[c] / 3, 0)),
  lidUV = centroid(lidTriangle),
  hit = uvSurfaceHit(geometry, ...lidUV),
  region = { kind: "object", partId: "lid" },
  op = {
    kind: "brush",
    region,
    value: 0,
    opacity: 0.7,
    radius: 0.04,
    hardness: 0.8,
    points: [lidUV],
  },
  paint = (operations = [op]) => ({ uvSet: AUTHORING_UV_SET, operations });
function masked(mask = { kind: "paint", fill: 1, paint: paint() }) {
  const doc = createTextureDocument();
  return validateTextureDocument(
    patchTextureLayer(doc, doc.layers[0].id, { mask }),
  );
}

test("mask strokes are declarative, normalized, bounded and round-trip without changing legacy masks", () => {
  const legacy = normalizeTextureMask({ fill: 0, inverted: true });
  expect(legacy).not.toHaveProperty("paint");
  const doc = masked();
  expect(doc.layers[0].mask.kind).toBe("paint");
  expect(validateTextureDocument(doc)).toEqual(doc);
  expect(parseTextureDocument(JSON.stringify(doc))).toEqual(doc);
  expect(appendMaskOperation(legacy, op).fill).toBe(0);
  expect(
    normalizeMaskPaint({ ...paint(), arbitraryScript: "bad()" }),
  ).not.toHaveProperty("arbitraryScript");
});
for (const mode of ["face", "tile", "island", "object"])
  test(`real ${mode} selection scopes only the native Lid component`, () => {
    const selection = maskRegionFromHit(hit, mode),
      matched = topology.triangles.filter((t) =>
        maskRegionContains(selection, t),
      );
    expect(selection.partId).toBe("lid");
    expect(matched.length).toBeGreaterThan(0);
    expect(matched.every((t) => t.part.id === "lid")).toBe(true);
    if (mode === "face")
      expect(matched.map((t) => t.faceIndex)).toEqual([hit.faceIndex]);
    if (mode === "island") {
      expect(new Set(matched.map((t) => t.chartId)).size).toBe(1);
      expect(selection.id).toBe(25);
    }
    if (mode === "tile") expect(selection.id).toBe(1002);
    if (mode === "object") expect(matched.length).toBe(6320);
    const record = masked({
      kind: "fill",
      fill: 0,
      paint: paint([
        { kind: "region", region: selection, value: 1, opacity: 1 },
      ]),
    });
    expect(parseTextureDocument(JSON.stringify(record))).toEqual(record);
  });
test("3D intersection and UV lookup identify the same actual triangle, chart, component and authoring UV", () => {
  const mesh = new THREE.Mesh(geometry),
    t = lidTriangle,
    point = new THREE.Vector3();
  t.ids.forEach((id) =>
    point.addScaledVector(
      new THREE.Vector3().fromBufferAttribute(geometry.attributes.position, id),
      1 / 3,
    ),
  );
  mesh.position.set(1, 2, 3);
  mesh.rotation.set(0.15, 0.4, 0);
  mesh.updateWorldMatrix(true, false);
  const details = surfaceHitDetails(mesh, {
    faceIndex: t.faceIndex,
    point: mesh.localToWorld(point),
  });
  expect(details.faceIndex).toBe(t.faceIndex);
  expect(details.chartId).toBe(25);
  expect(details.target.partId).toBe("lid");
  details.uvPosition.forEach((n, i) => expect(n).toBeCloseTo(lidUV[i], 6));
  expect(maskRegionFromHit(null, "face")).toBeNull();
  expect(
    maskRegionFromHit(
      { ...hit, target: { objectId: "fake", partId: "lid" } },
      "object",
    ),
  ).toBeNull();
});
test("invalid mask identities, mismatched native parts and foreign UV layouts reject atomically", () => {
  for (const value of [
    { kind: "island", partId: "lid", id: 1 },
    { kind: "tile", partId: "lid", id: 1001 },
    { kind: "face", partId: "lid", id: 25440 },
    { kind: "object", partId: "teapot" },
  ])
    expect(() => normalizeMaskRegion(value)).toThrow();
  expect(() => normalizeMaskPaint({ ...paint(), uvSet: "uv0" })).toThrow(
    /authoring UV/,
  );
  expect(() =>
    masked({
      kind: "paint",
      paint: paint([
        {
          kind: "region",
          region: { kind: "face", partId: "lid", id: bodyTriangle.faceIndex },
          value: 1,
          opacity: 1,
        },
      ]),
    }),
  ).toThrow(/another component/);
});
for (const value of [NaN, Infinity, "0.5", null])
  test(`non-finite or coerced mask value ${value} is not accepted`, () => {
    expect(() => normalizeMaskPaint(paint([{ ...op, value }]))).toThrow(
      /Invalid mask paint/,
    );
  });
test("brush points must be finite UV pairs inside the targeted object's packed tile", () => {
  for (const points of [
    [[0.5, 0.5]],
    [[1.2, NaN]],
    [[Infinity, 0.1]],
    [[1.2, 0.5, 0.1]],
    [],
  ])
    expect(() => normalizeMaskPaint(paint([{ ...op, points }]))).toThrow();
  expect(() =>
    normalizeMaskPaint(
      paint([
        {
          ...op,
          points: Array.from({ length: MASK_POINT_LIMIT + 1 }, () => lidUV),
        },
      ]),
    ),
  ).toThrow(/samples/);
});
test("operation, point and interpolation budgets prevent unbounded JSON-driven raster work", () => {
  expect(() =>
    normalizeMaskPaint(
      paint(Array.from({ length: MASK_OPERATION_LIMIT + 1 }, () => op)),
    ),
  ).toThrow(/128/);
  const repeating = { ...op, points: Array.from({ length: 512 }, () => lidUV) };
  expect(() =>
    normalizeMaskPaint(
      paint(
        Array.from(
          { length: MASK_TOTAL_POINT_LIMIT / 512 + 1 },
          () => repeating,
        ),
      ),
    ),
  ).toThrow(/sampling budget/);
  expect(() =>
    normalizeMaskPaint(
      paint([
        {
          ...op,
          points: Array.from({ length: 100 }, (_, i) => [
            i % 2 ? 1.99 : 1.01,
            0.5,
          ]),
        },
      ]),
    ),
  ).toThrow(/sampling budget/);
});
test("a mask gesture is one genuine history revision, undo/redo restores exact records, duplication is independent", () => {
  const before = createTextureDocument(),
    id = before.layers[0].id,
    selection = { id, mask: true },
    after = patchTextureLayer(before, id, {
      mask: { kind: "gradient", fill: 0, paint: paint() },
    });
  let history = createTextureHistory({
    doc: before,
    selection: { id, mask: false },
  });
  history = commitTextureHistory(
    history,
    { doc: after, selection },
    { label: "Paint mask" },
  );
  expect(history.past.length).toBe(1);
  expect(travelTextureHistory(history, "undo").present.doc).toEqual(before);
  expect(
    travelTextureHistory(travelTextureHistory(history, "undo"), "redo").present
      .doc,
  ).toEqual(after);
  const copy = duplicateTextureLayer(after, id);
  expect(copy.doc.layers.find((l) => l.id === copy.id).mask.paint).toEqual(
    after.layers[0].mask.paint,
  );
  const changed = patchTextureLayer(copy.doc, copy.id, {
    mask: { ...after.layers[0].mask, paint: null },
  });
  expect(
    changed.layers.find((l) => l.id === id).mask.paint.operations.length,
  ).toBe(1);
});
test("a locked ancestor prevents mask edits and gradient sources survive adding painted coverage", () => {
  const initial = createTextureDocument(),
    group = createTextureLayer("folder", "Locked group"),
    doc = validateTextureDocument({
      ...initial,
      layers: [
        { ...group, locked: true },
        { ...initial.layers[0], parentId: group.id },
        ...initial.layers.slice(1),
      ],
    }),
    id = initial.layers[0].id;
  expect(isTextureLayerLocked(doc, id)).toBe(true);
  expect(
    patchTextureLayer(doc, id, { mask: { kind: "paint", paint: paint() } }),
  ).toBe(doc);
  const mask = normalizeTextureMask({ kind: "gradient" }),
    painted = normalizeTextureMask(appendMaskOperation(mask, op));
  expect(painted.kind).toBe("gradient");
  expect(painted.gradient).toEqual(mask.gradient);
});

for (const fails of [false, true])
  test(`mask atlas ${fails ? "failed readback" : "successful draw"} restores renderer, depth, wireframe and UV uniform state`, () => {
    const priorTarget = { name: "previous framebuffer" },
      target = { name: "mask atlas" };
    const previousColor = new Color("#abcdef"),
      clearColor = previousColor.clone();
    let currentTarget = priorTarget,
      clearAlpha = 0.37;
    const renderer = {
      getRenderTarget: () => currentTarget,
      getClearColor: (c) => c.copy(clearColor),
      getClearAlpha: () => clearAlpha,
      setRenderTarget: (v) => {
        currentTarget = v;
      },
      setClearColor: (color, alpha) => {
        clearColor.set(color);
        clearAlpha = alpha;
      },
    };
    // Nondefault states ensure restoration does not assume Lit / depth=true.
    const material = { depthTest: false, depthWrite: true, wireframe: true },
      drawUV = { value: 7 };
    const draw = () => {
      expect(currentTarget).toBe(target);
      expect(material).toEqual({
        depthTest: false,
        depthWrite: false,
        wireframe: false,
      });
      expect(drawUV.value).toBe(1);
      expect(clearColor.getHexString()).toBe("202124");
      expect(clearAlpha).toBe(1);
      if (fails) throw new Error("GPU readback failed");
      return "filled atlas";
    };
    const perform = () =>
      withMaskAtlasState(renderer, material, drawUV, target, draw);
    if (fails) expect(perform).toThrow("GPU readback failed");
    else expect(perform()).toBe("filled atlas");
    expect(currentTarget).toBe(priorTarget);
    expect(clearColor.equals(previousColor)).toBe(true);
    expect(clearAlpha).toBe(0.37);
    expect(material).toEqual({
      depthTest: false,
      depthWrite: true,
      wireframe: true,
    });
    expect(drawUV.value).toBe(7);
  });
