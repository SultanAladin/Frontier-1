import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { TeapotGeometry } from "three/addons/geometries/TeapotGeometry.js";
import {
  AUTHORING_UV_SET,
  TEXTURE_PARTS,
  createTextureTeapotGeometry,
  partGeometry,
  normalizeSurfaceMaterial,
  normalizeSurfaceTarget,
  readAssetDrop,
  installSurfaceCoordinates,
} from "../src/textureSurfaceModel.js";
import {
  uvTopology,
  uvSurfaceHit,
  barycentricUV,
  uvTile,
  uvDecalProjector,
  projectArtworkGeometry,
  uvBoundaryLoops,
} from "../src/textureUVGeometry.js";
import { materials } from "../src/materials.js";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  parseTextureDocument,
  assignTexturePartMaterial,
  activeTexturePartMaterials,
  patchTextureLayer,
  isTexturePartLocked,
  addTextureDecal,
  patchTextureDecal,
  duplicateTextureLayer,
} from "../src/textureDocument.js";
import { normalizeDecal, collectDecalRecords } from "../src/decalModel.js";
import {
  normalizeStampPreset,
  createStampDocument,
} from "../src/stampDocument.js";
import {
  projectDecalGeometry,
  decalLocalProjector,
  updateSurfaceMaskUniforms,
  SURFACE_MASK_GLSL,
} from "../src/decalProjection.js";
import {
  createTextureHistory,
  commitTextureHistory,
  travelTextureHistory,
} from "../src/textureHistory.js";
const ceramic = materials.find((m) => m.id === "carbon-ceramic"),
  lid = { objectId: "teapot", partId: "lid" };
const preset = () =>
  normalizeStampPreset({
    project: createStampDocument(),
    raster: {
      name: "ink.png",
      width: 1,
      height: 1,
      dataUrl:
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==",
    },
  });
const uvDecal = (placement = {}) =>
  normalizeDecal({
    id: "uv-test",
    stampId: "stamp-test",
    space: "uv",
    uvSet: AUTHORING_UV_SET,
    uvPosition: [1.125, 0.75],
    target: lid,
    ...placement,
  });

test("authoring teapot keeps exact original geometry, normals, UV0 and topology, with four semantic part attributes", () => {
  const geometry = createTextureTeapotGeometry(),
    source = new TeapotGeometry(1.05, 20);
  for (const key of ["position", "normal", "uv"])
    expect(geometry.attributes[key].array).toEqual(
      source.attributes[key].array,
    );
  expect(geometry.index.array).toEqual(source.index.array);
  expect(geometry.userData.authoringUVSet).toBe(AUTHORING_UV_SET);
  expect([...new Set(geometry.attributes.surfacePart.array)].sort()).toEqual([
    1, 2, 3, 4,
  ]);
  const chartParts = new Map();
  for (let i = 0; i < geometry.attributes.position.count; i++)
    chartParts.set(
      geometry.attributes.surfaceChart.getX(i),
      geometry.attributes.surfacePart.getX(i),
    );
  expect([...chartParts].filter(([, p]) => p === 2).map(([p]) => p)).toEqual([
    21, 22, 23, 24, 25, 26, 27, 28,
  ]);
  expect([...chartParts].filter(([, p]) => p === 3).map(([p]) => p)).toEqual([
    13, 14, 15, 16,
  ]);
  expect([...chartParts].filter(([, p]) => p === 4).map(([p]) => p)).toEqual([
    17, 18, 19, 20,
  ]);
  geometry.dispose();
  source.dispose();
});
test("real authoring topology has 32 non-overlapping charts and UDIMs 1001–1004, while original UV inspection honestly overlaps", () => {
  const geometry = createTextureTeapotGeometry(),
    topology = uvTopology(geometry);
  expect(topology.triangles).toHaveLength(25440);
  expect(topology.tiles.map((t) => t.id)).toEqual([1001, 1002, 1003, 1004]);
  expect(topology.tiles.map((t) => t.triangles)).toEqual([
    12720, 6320, 3200, 3200,
  ]);
  expect(topology.charts).toHaveLength(32);
  for (let i = 0; i < topology.charts.length; i++)
    for (let j = i + 1; j < topology.charts.length; j++) {
      const a = topology.charts[i],
        b = topology.charts[j];
      expect(
        a.max[0] <= b.min[0] ||
          b.max[0] <= a.min[0] ||
          a.max[1] <= b.min[1] ||
          b.max[1] <= a.min[1],
      ).toBeTruthy();
    }
  expect(uvTopology(geometry)).toBe(topology);
  expect(uvTopology(geometry, true).tiles.map((t) => t.id)).toEqual([1001]);
  expect(uvTopology(geometry, true).overlaps).toBe(true);
  expect(uvTile(3.2, 2.8).id).toBe(1024);
  expect(uvTile(-0.01, 0)).toBeNull();
  expect(uvTile(10, 0)).toBeNull();
  geometry.dispose();
});
test("barycentric UV picking returns the actual indexed 3D surface anchor and normal, never a hit in a gutter or empty tile", () => {
  const geometry = createTextureTeapotGeometry(),
    hit = uvSurfaceHit(geometry, 1.125, 0.75),
    triangle = uvTopology(geometry).triangles.find(
      (t) => t.part.id === "lid" && barycentricUV(hit.uvPosition, t),
    );
  expect(hit.target).toEqual(lid);
  expect(hit.tile).toBe(1002);
  const weights = barycentricUV(hit.uvPosition, triangle),
    actual = new THREE.Vector3(),
    normal = new THREE.Vector3();
  triangle.ids.forEach((id, i) => {
    actual.addScaledVector(
      new THREE.Vector3().fromBufferAttribute(geometry.attributes.position, id),
      weights[i],
    );
    normal.addScaledVector(
      new THREE.Vector3().fromBufferAttribute(geometry.attributes.normal, id),
      weights[i],
    );
  });
  const decoded = new THREE.Vector3(...hit.position)
    .multiplyScalar(geometry.boundingSphere.radius)
    .add(geometry.boundingSphere.center);
  expect(decoded.distanceTo(actual)).toBeLessThan(1e-7);
  expect(
    new THREE.Vector3(0, 0, 1)
      .applyQuaternion(new THREE.Quaternion(...hit.quaternion))
      .dot(normal.normalize()),
  ).toBeCloseTo(1);
  expect(uvSurfaceHit(geometry, 1, 0.5)).toBeNull();
  expect(uvSurfaceHit(geometry, 4.2, 0.5)).toBeNull();
  expect(uvSurfaceHit(geometry, 1.125, 0.75, "body")).toBeNull();
  expect(uvSurfaceHit(geometry, 2.25, 0.25).target.partId).toBe("handle");
  expect(uvSurfaceHit(geometry, 3.25, 0.25).target.partId).toBe("spout");
  geometry.dispose();
});
test("part extraction creates native material geometry containing only the requested semantic region and every original triangle once", () => {
  const geometry = createTextureTeapotGeometry();
  let count = 0;
  for (const part of TEXTURE_PARTS) {
    const extracted = partGeometry(geometry, part.id);
    expect(new Set(extracted.attributes.surfacePart.array)).toEqual(
      new Set([part.index]),
    );
    expect(extracted.attributes.uvAuthoring.count).toBe(
      extracted.attributes.position.count,
    );
    expect(extracted.boundingSphere.radius).toBeGreaterThan(0);
    count += extracted.attributes.position.count;
    extracted.dispose();
  }
  expect(count).toBe(geometry.index.count);
  expect(partGeometry(geometry, "entire-object")).toBeNull();
  geometry.dispose();
});
test("part snapshots are bounded declarative native materials, idempotent across all 127 presets and reject foreign code or invalid values", () => {
  for (const material of materials) {
    const snapshot = normalizeSurfaceMaterial(material);
    expect(normalizeSurfaceMaterial(snapshot)).toEqual(snapshot);
  }
  const clean = normalizeSurfaceMaterial({
    ...ceramic,
    script: "alert(1)",
    url: "https://example.com/evil",
    onBeforeCompile: "function(){}",
    tuning: { safe: 0.5, invalid: Infinity },
  });
  expect(clean).not.toHaveProperty("script");
  expect(clean).not.toHaveProperty("url");
  expect(clean).not.toHaveProperty("onBeforeCompile");
  expect(clean.tuning).toEqual({ safe: 0.5 });
  for (const material of [
    { roughness: NaN },
    { roughness: "0.5" },
    { colors: [{ script: "x" }] },
    { color: "url(evil)" },
    { description: "x".repeat(210000) },
  ])
    expect(() => normalizeSurfaceMaterial(material)).toThrow();
  expect(() =>
    normalizeSurfaceTarget({ objectId: "other", partId: "lid" }),
  ).toThrow();
  expect(() =>
    normalizeSurfaceTarget({ objectId: "teapot", partId: "body<script>" }),
  ).toThrow();
});
test("a lid material drop is one scoped, immutable, portable layer record; replacement doesn't recolour or rewrite the other layers", () => {
  const before = createTextureDocument(),
    result = assignTexturePartMaterial(before, lid, ceramic);
  expect(result.doc.layers).toHaveLength(before.layers.length + 1);
  expect(result.doc.layers.filter((l) => l.id !== result.id)).toEqual(
    before.layers,
  );
  expect(result.doc.layers[0].surfaceTarget).toEqual(lid);
  expect(result.doc.layers[0].surfaceMaterial.id).toBe(ceramic.id);
  const snapshot = result.doc.layers[0].surfaceMaterial;
  expect(snapshot).not.toBe(ceramic);
  const replaced = assignTexturePartMaterial(result.doc, lid, {
    ...ceramic,
    color: "#ff0000",
  });
  expect(replaced.id).toBe(result.id);
  expect(replaced.doc.layers).toHaveLength(result.doc.layers.length);
  expect(snapshot.color).toBe(ceramic.color);
  expect(replaced.doc.layers[0].surfaceMaterial.color).toBe("#ff0000");
  expect(parseTextureDocument(JSON.stringify(replaced.doc))).toEqual(
    replaced.doc,
  );
  let history = createTextureHistory({
    doc: before,
    selection: { id: "base-material" },
  });
  history = commitTextureHistory(
    history,
    { doc: result.doc, selection: { id: result.id } },
    { label: "Assign ceramic to Lid" },
  );
  expect(history.past).toHaveLength(1);
  expect(travelTextureHistory(history, "undo").present.doc).toEqual(before);
});
test("native assignments respect top-to-bottom hierarchy, inherited visibility/opacity, locks and hidden replacement without losing other parts", () => {
  let { doc, id } = assignTexturePartMaterial(
    createTextureDocument(),
    lid,
    ceramic,
  );
  const body = assignTexturePartMaterial(
    doc,
    { objectId: "teapot", partId: "body" },
    materials[0],
  );
  doc = body.doc;
  const folder = createTextureLayer("folder", { id: "parts", opacity: 50 }),
    assigned = doc.layers.find((l) => l.id === id);
  doc = validateTextureDocument({
    ...doc,
    layers: [
      folder,
      { ...assigned, parentId: "parts", opacity: 40 },
      ...doc.layers.filter((l) => l.id !== id),
    ],
  });
  expect(activeTexturePartMaterials(doc).get("lid").opacity).toBeCloseTo(0.2);
  doc = patchTextureLayer(doc, "parts", { locked: true });
  expect(isTexturePartLocked(doc, "lid")).toBe(true);
  expect(() => assignTexturePartMaterial(doc, lid, materials[0])).toThrow(
    /locked/,
  );
  doc = patchTextureLayer(doc, "parts", { locked: false, visible: false });
  expect(activeTexturePartMaterials(doc).has("lid")).toBe(false);
  expect(activeTexturePartMaterials(doc).has("body")).toBe(true);
  const next = assignTexturePartMaterial(doc, lid, ceramic);
  expect(next.id).not.toBe(id);
  expect(activeTexturePartMaterials(next.doc).get("lid").layer.id).toBe(
    next.id,
  );
  expect(next.doc.layers.find((l) => l.id === id).parentId).toBe("parts");
});
test("UV components use validated authoring units and tile scope without silently converting legacy 3D projectors", () => {
  const legacy = normalizeDecal({ id: "legacy", stampId: "stamp-test" });
  expect(legacy.space).toBe("object-sphere");
  expect(legacy).not.toHaveProperty("uvPosition");
  expect(legacy).not.toHaveProperty("target");
  const uv = uvDecal({ size: [0.001, 0.002] });
  expect(uv.size).toEqual([0.001, 0.002]);
  expect(normalizeDecal(uv)).toEqual(uv);
  expect(uvDecal({ target: undefined }).target).toEqual(lid);
  for (const patch of [
    { uvSet: "source" },
    { uvPosition: [NaN, 0.5] },
    { uvPosition: [2.5, 0.5] },
    { uvPosition: [1.5, 3] },
    { space: "world" },
  ])
    expect(() => uvDecal(patch)).toThrow();
});
test("rotated UV artwork clips on true teapot triangles, keeps the lid's authored UVs, and never bleeds onto a neighbouring UDIM or part", () => {
  const geometry = createTextureTeapotGeometry(),
    decal = uvDecal({ uvPosition: [1.03, 0.72], size: [0.7, 0.6], roll: 37 }),
    result = projectArtworkGeometry(geometry, decal),
    inverse = uvDecalProjector(decal).invert();
  expect(result.attributes.position.count).toBeGreaterThan(100);
  expect(new Set(result.attributes.surfacePart.array)).toEqual(new Set([2]));
  const bounds = new THREE.Box3().setFromBufferAttribute(
    result.attributes.position,
  );
  expect(bounds.max.y - bounds.min.y).toBeGreaterThan(0.005);
  for (let i = 0; i < result.attributes.position.count; i++) {
    const u = result.attributes.uvAuthoring.getX(i),
      v = result.attributes.uvAuthoring.getY(i),
      p = new THREE.Vector3(u, v, 0).applyMatrix4(inverse);
    expect(u).toBeGreaterThan(1);
    expect(u).toBeLessThan(2);
    expect(Math.abs(p.x)).toBeLessThanOrEqual(0.50001);
    expect(Math.abs(p.y)).toBeLessThanOrEqual(0.50001);
    expect(result.attributes.uv.getX(i)).toBeCloseTo(p.x + 0.5, 5);
    expect(result.attributes.uv.getY(i)).toBeCloseTo(p.y + 0.5, 5);
    expect(
      new THREE.Vector3()
        .fromBufferAttribute(result.attributes.normal, i)
        .length(),
    ).toBeCloseTo(1);
  }
  result.dispose();
  geometry.dispose();
});
test("authoring-aware 3D projectors retain real source UVs and respect part scope on a transformed specimen", () => {
  const geometry = createTextureTeapotGeometry(),
    mesh = new THREE.Mesh(geometry),
    hit = uvSurfaceHit(geometry, 1.125, 0.75),
    decal = normalizeDecal({
      id: "surface-test",
      stampId: "stamp-test",
      ...hit,
      space: "object-sphere",
      size: [0.65, 0.65],
      depth: 0.5,
    });
  mesh.position.set(2, 3, -4);
  mesh.rotation.set(0.2, 0.4, 0);
  const projected = projectDecalGeometry(mesh, decal),
    inverse = decalLocalProjector(mesh, decal).invert();
  expect(projected.attributes.position.count).toBeGreaterThan(0);
  expect(new Set(projected.attributes.surfacePart.array)).toEqual(new Set([2]));
  for (let i = 0; i < projected.attributes.position.count; i++) {
    const p = new THREE.Vector3()
      .fromBufferAttribute(projected.attributes.position, i)
      .applyMatrix4(inverse);
    expect(
      Math.max(Math.abs(p.x), Math.abs(p.y), Math.abs(p.z)),
    ).toBeLessThanOrEqual(0.50001);
    expect(projected.attributes.uvAuthoring.getX(i)).toBeGreaterThan(1);
  }
  projected.dispose();
  geometry.dispose();
  mesh.material.dispose();
});
test("scoped decal operations inherit the part and reject foreign hits, cross-part patches, material locks and duplicate identity aliasing", () => {
  const assigned = assignTexturePartMaterial(
      createTextureDocument(),
      lid,
      ceramic,
    ),
    stamp = preset(),
    geometry = createTextureTeapotGeometry(),
    hit = uvSurfaceHit(geometry, 1.125, 0.75);
  const added = addTextureDecal(assigned.doc, assigned.id, stamp, hit);
  expect(added.doc.layers[0].decals[0].target).toEqual(lid);
  expect(() =>
    addTextureDecal(assigned.doc, assigned.id, stamp, {
      target: { objectId: "teapot", partId: "body" },
    }),
  ).toThrow(/targets Lid/);
  expect(() =>
    patchTextureDecal(added.doc, assigned.id, added.id, {
      target: { objectId: "teapot", partId: "body" },
      uvPosition: [0.125, 0.75],
    }),
  ).toThrow(/target/);
  const locked = patchTextureLayer(added.doc, assigned.id, { locked: true });
  expect(() => addTextureDecal(locked, "base-material", stamp, hit)).toThrow(
    /Unlock/,
  );
  expect(patchTextureDecal(locked, assigned.id, added.id, { roll: 60 })).toBe(
    locked,
  );
  const copy = duplicateTextureLayer(added.doc, assigned.id);
  const duplicated = copy.doc.layers.find((l) => l.id === copy.id);
  expect(duplicated.decals[0].id).not.toBe(added.id);
  expect(duplicated.surfaceMaterial).toEqual(
    added.doc.layers[0].surfaceMaterial,
  );
  expect(parseTextureDocument(JSON.stringify(copy.doc))).toEqual(copy.doc);
  geometry.dispose();
});
test("GPU mask rows carry actual inverse UV projectors plus part scope while retaining the bounded legacy 7×64 table", () => {
  const geometry = createTextureTeapotGeometry(),
    mesh = new THREE.Mesh(geometry),
    hit = uvSurfaceHit(geometry, 1.125, 0.75),
    source = preset(),
    doc = addTextureDecal(createTextureDocument(), "base-material", source, {
      ...hit,
      size: [0.2, 0.15],
      roll: 23,
      asMask: true,
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
  const data = uniforms.uSurfaceStampData.value.image.data,
    centre = new THREE.Vector3(...hit.uvPosition, 0).applyMatrix4(
      new THREE.Matrix4().fromArray(data.slice(0, 16)),
    );
  expect(centre.length()).toBeLessThan(1e-5);
  expect(uniforms.uSurfaceStampCount.value).toBe(1);
  expect(data[23]).toBe(-3);
  expect(data[22]).toBeCloseTo(0.7);
  expect(SURFACE_MASK_GLSL).toContain("vAuthoringUv");
  expect(SURFACE_MASK_GLSL).toContain("vSurfacePart-targetPart");
  uniforms.uSurfaceStampData.value.dispose();
  geometry.dispose();
  mesh.material.dispose();
});
test("surface shader attributes are installed only once and native drop parsing is bounded, typed and inert", () => {
  const material = new THREE.MeshPhysicalMaterial();
  installSurfaceCoordinates(material);
  installSurfaceCoordinates(material);
  const shader = {
    uniforms: {},
    vertexShader: "#include <begin_vertex>",
    fragmentShader: "void main(){}",
  };
  material.onBeforeCompile(shader);
  expect(shader.vertexShader.match(/attribute vec2 uvAuthoring/g)).toHaveLength(
    1,
  );
  expect(
    shader.fragmentShader.match(/varying float vSurfacePart/g),
  ).toHaveLength(1);
  material.dispose();
  const drop = (text) => readAssetDrop({ getData: () => text });
  expect(drop(JSON.stringify({ type: "material", id: ceramic.id }))).toEqual({
    type: "material",
    id: ceramic.id,
  });
  for (const value of [
    "{",
    "x".repeat(210001),
    JSON.stringify({ type: "generator", id: "PerlinNoise" }),
    JSON.stringify({ type: "material", id: "../evil" }),
  ])
    expect(drop(value)).toBeNull();
});

test("UV boundary contours preserve indexed chart coverage and holes without tessellating overlapping subpaths on every gesture", () => {
  const geometry = createTextureTeapotGeometry(),
    loops = uvBoundaryLoops(geometry);
  expect(loops).toHaveLength(32);
  expect(loops.reduce((n, loop) => n + loop.points.length, 0)).toBeLessThan(
    3000,
  );
  for (const loop of loops) {
    expect(loop.points[0]).toEqual(loop.points.at(-1));
    expect(new Set(loop.points.map(([u]) => Math.floor(u)))).toEqual(
      new Set([loop.part.u]),
    );
  }
  geometry.dispose();
});
