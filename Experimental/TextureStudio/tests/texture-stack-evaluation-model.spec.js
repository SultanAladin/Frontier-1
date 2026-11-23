import { test, expect } from "@playwright/test";
import { readFileSync } from "node:fs";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  patchTextureLayer,
  moveTextureLayer,
} from "../src/textureDocument.js";
import { materials } from "../src/materials.js";
import {
  evaluateTextureStack,
  textureLayerRenderOrder,
  TEXTURE_SYSTEM_BASE,
} from "../src/textureStackEvaluation.js";
import {
  textureBakeUV,
  reorientTextureNormal,
  normalizeTextureMeshMaps,
  TEXTURE_BAKE_UV_SET,
  TEXTURE_MESH_MAP_SCHEMA,
} from "../src/textureMeshMaps.js";
import {
  attachTextureMeshMaps,
  publishTextureMeshMaps,
  isTextureBakeTarget,
} from "../src/textureMeshMapBridge.js";
import { createTextureTeapotGeometry } from "../src/textureSurfaceModel.js";
import {
  createDemoBakeAssets,
  disposeBakeParts,
} from "../src/baking/bakeMeshes.js";
const png = JSON.parse(
  readFileSync(
    new URL("./fixtures/texture-mesh-map-pngs.json", import.meta.url),
    "utf8",
  ),
);
const recipe = materials.find((m) => m.id === "racing-green");
const material = (opts = {}) =>
  createTextureLayer("material", { surfaceMaterial: recipe, ...opts });
const map = (dataUrl) => ({ dataUrl, width: 16, height: 16 });
const maps = (values = { occlusion: map(png.blackAO) }) => ({
  schema: TEXTURE_MESH_MAP_SCHEMA,
  target: "teapot",
  uvSet: TEXTURE_BAKE_UV_SET,
  maps: values,
});

test("global material snapshots round-trip while old authoring records stay intact", () => {
  const before = createTextureDocument(),
    upper = material();
  const doc = validateTextureDocument({
    ...before,
    layers: [upper, ...before.layers],
  });
  expect(validateTextureDocument(doc)).toEqual(doc);
  expect(doc.layers.slice(1)).toEqual(before.layers);
  expect(doc.layers[0].surfaceTarget).toBeUndefined();
  expect(doc.layers[0].surfaceMaterial.id).toBe(recipe.id);
});
test("one shared bottom-to-top order places upper material after lower stamp and owner decals after their own material", () => {
  const doc = {
    ...createTextureDocument(),
    layers: [material({ id: "upper" }), material({ id: "lower" })],
  };
  const plan = evaluateTextureStack(doc);
  expect(plan.surfaces.map((r) => r.layer.id)).toEqual(["lower", "upper"]);
  expect(textureLayerRenderOrder(doc, "upper")).toBeGreaterThan(
    textureLayerRenderOrder(doc, "lower", 63),
  );
  expect(textureLayerRenderOrder(doc, "lower", 0)).toBeGreaterThan(
    textureLayerRenderOrder(doc, "lower"),
  );
  expect(textureLayerRenderOrder(doc, "missing")).toBe(0);
});
test("system Layer 0 is immutable hidden white support, outside editable layer capacity", () => {
  const doc = createTextureDocument(),
    plan = evaluateTextureStack(doc);
  expect(plan.base).toEqual(TEXTURE_SYSTEM_BASE);
  expect(plan.base).toMatchObject({
    level: 0,
    color: "#ffffff",
    locked: true,
    hidden: true,
    editable: false,
  });
  expect(Object.isFrozen(plan.base)).toBe(true);
  expect(plan.fallbackParts).toEqual(["body", "lid", "handle", "spout"]);
  expect(doc.layers.some((l) => l.id === plan.base.id)).toBe(false);
});
for (const change of [
  { visible: false },
  { opacity: 0 },
  { opacity: 50 },
  { mask: { kind: "fill", fill: 0 } },
])
  test(`fallback returns for ${JSON.stringify(change)}`, () => {
    let doc = validateTextureDocument({
      ...createTextureDocument(),
      layers: [material({ id: "cover" })],
    });
    expect(evaluateTextureStack(doc).fallbackParts).toEqual([]);
    doc = validateTextureDocument(patchTextureLayer(doc, "cover", change));
    expect(evaluateTextureStack(doc).fallbackParts).toEqual([
      "body",
      "lid",
      "handle",
      "spout",
    ]);
  });
test("part coverage switches off only Lid fallback, with hierarchy opacity and masks respected", () => {
  const folder = createTextureLayer("folder", { id: "folder", opacity: 50 });
  const lid = material({
    id: "lid",
    parentId: "folder",
    surfaceTarget: { objectId: "teapot", partId: "lid" },
  });
  let doc = validateTextureDocument({
    ...createTextureDocument(),
    layers: [folder, lid],
  });
  expect(evaluateTextureStack(doc).surfaces[0].opacity).toBe(0.5);
  expect(evaluateTextureStack(doc).fallbackParts).toContain("lid");
  doc = validateTextureDocument(
    patchTextureLayer(doc, "folder", { opacity: 100 }),
  );
  expect(evaluateTextureStack(doc).fallbackParts).toEqual([
    "body",
    "handle",
    "spout",
  ]);
});
test("reordering, not current selection, changes evaluation; source Paint is still not deposited", () => {
  const before = createTextureDocument(),
    first = material({ id: "a" }),
    second = material({ id: "b" });
  const doc = validateTextureDocument({
    ...before,
    layers: [first, second, ...before.layers],
  });
  const moved = moveTextureLayer(doc, "b", null, "a");
  expect(evaluateTextureStack(moved).surfaces.map((r) => r.layer.id)).toEqual([
    "a",
    "b",
  ]);
  expect(evaluateTextureStack(moved).stateOnlyLayers).toContain(
    "surface-detail",
  );
});
test("every teapot authoring vertex maps to the same original 8x4 bake chart without altering UV0", () => {
  const g = createTextureTeapotGeometry(8),
    uv = g.attributes.uv,
    chart = g.attributes.surfaceChart,
    part = g.attributes.surfacePart,
    author = g.attributes.uvAuthoring;
  for (let i = 0; i < uv.count; i += 3) {
    const actual = textureBakeUV(
      [author.getX(i), author.getY(i)],
      part.getX(i),
    );
    const patch = chart.getX(i) - 1;
    expect(actual[0]).toBeCloseTo(
      ((patch % 8) + 0.045 + uv.getX(i) * 0.91) / 8,
      5,
    );
    expect(actual[1]).toBeCloseTo(
      (Math.floor(patch / 8) + 0.045 + uv.getY(i) * 0.91) / 4,
      5,
    );
  }
  g.dispose();
});
test("RNM preserves a flat normal and independent details and produces unit normals rather than RGB products", () => {
  const detail = [0.4, 0, Math.sqrt(0.84)],
    flat = [0, 0, 1];
  expect(reorientTextureNormal(flat, detail)).toEqual(
    detail.map((v) => expect.closeTo(v, 10)),
  );
  expect(reorientTextureNormal(detail, flat)).toEqual(
    detail.map((v) => expect.closeTo(v, 10)),
  );
  const combined = reorientTextureNormal(detail, [0, 0.3, Math.sqrt(0.91)]);
  expect(Math.hypot(...combined)).toBeCloseTo(1, 10);
  expect(combined[0]).toBeGreaterThan(0.3);
  expect(combined[1]).toBeGreaterThan(0.2);
});
test("baked AO/normal inputs preserve all user layers and round-trip within the original document budget", () => {
  const doc = createTextureDocument(),
    support = maps({
      occlusion: map(png.blackAO),
      normal: { ...map(png.tiltNormal), normalY: "-Y" },
    });
  const next = attachTextureMeshMaps(doc, support);
  expect(next.layers).toEqual(doc.layers);
  expect(validateTextureDocument(next)).toEqual(next);
  expect(next.meshMaps.maps.normal.normalY).toBe("-Y");
  const updated = attachTextureMeshMaps(
    next,
    maps({ "bevel-normal": map(png.bevelNormal) }),
  );
  expect(Object.keys(updated.meshMaps.maps)).toEqual([
    "occlusion",
    "normal",
    "bevel-normal",
  ]);
});
test("untrusted mesh data rejects foreign layouts, vectors, external URLs, dimensions and normal conventions atomically", () => {
  const valid = maps();
  for (const bad of [
    { ...valid, target: "cube" },
    { ...valid, uvSet: "udim" },
    maps({ "world-normal": map(png.flatNormal) }),
    maps({ occlusion: map("https://example.com/ao.png") }),
    maps({ normal: { ...map(png.flatNormal), width: 32, height: 32 } }),
    maps({ normal: { ...map(png.flatNormal), normalY: "bad" } }),
  ])
    expect(() => normalizeTextureMeshMaps(bad)).toThrow();
});
test("bake bridge accepts chart-tagged built-in low only and never clears an existing project on failure", () => {
  const assets = createDemoBakeAssets("Teapot", { textureTarget: true });
  expect(isTextureBakeTarget(assets.low[0])).toBe(true);
  expect(
    isTextureBakeTarget({ name: "Teapot", geometry: { userData: {} } }),
  ).toBe(false);
  let value = JSON.stringify(createTextureDocument());
  const storage = {
    getItem: () => value,
    setItem: (key, v) => {
      value = v;
    },
  };
  publishTextureMeshMaps(maps(), storage);
  const original = value;
  expect(() =>
    publishTextureMeshMaps(maps({ occlusion: map("not PNG") }), storage),
  ).toThrow();
  expect(value).toBe(original);
  disposeBakeParts(assets.low);
  disposeBakeParts(assets.high);
});

test("support hook composes after native recipe detail and before PBR lighting, without replacing native normals", async () => {
  const THREE = await import("three"),
    { createMaterial } = await import("../src/materials.js"),
    { installTextureMeshSupport } =
      await import("../src/textureMeshSupportController.js");
  const uniforms = {};
  for (const native of [true, false]) {
    const m = native
      ? createMaterial(recipe)
      : new THREE.MeshPhysicalMaterial();
    installTextureMeshSupport(m, uniforms);
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    m.onBeforeCompile(shader);
    const combined = shader.fragmentShader.indexOf(
      "normal=alloyMeshSupportedNormal(normal",
    );
    expect(combined).toBeGreaterThan(
      shader.fragmentShader.indexOf(
        "#include <clearcoat_normal_fragment_maps>",
      ),
    );
    expect(combined).toBeLessThan(
      shader.fragmentShader.indexOf("#include <lights_fragment_begin>"),
    );
    expect(shader.fragmentShader).toContain(
      "clearcoatSpecularIndirect*=meshAO;",
    );
    expect(shader.fragmentShader).toContain("sheenSpecularIndirect*=meshAO;");
    if (native)
      expect(combined).toBeGreaterThan(
        shader.fragmentShader.indexOf("normal ="),
      );
    m.dispose();
  }
});

test("UV appearance pass restores material, visibility, renderer and shadow state on success and GPU failure", async () => {
  const THREE = await import("three"),
    { withTextureStackUVState } =
      await import("../src/textureStackUVController.js");
  for (const fails of [false, true]) {
    const uniform = { value: 0 },
      m = new THREE.MeshPhysicalMaterial({
        depthTest: true,
        depthWrite: true,
        wireframe: true,
      });
    m.userData.textureStackUV = uniform;
    const mesh = new THREE.Mesh(new THREE.BufferGeometry(), m),
      helper = new THREE.Mesh(
        new THREE.BufferGeometry(),
        new THREE.MeshBasicMaterial(),
      );
    const scene = new THREE.Scene();
    scene.background = new THREE.Color("#334455");
    scene.add(mesh, helper);
    let current = null,
      clear = new THREE.Color("#000000"),
      alpha = 1,
      viewport = new THREE.Vector4(1, 2, 3, 4),
      scissor = new THREE.Vector4(2, 3, 4, 5),
      testScissor = true;
    const r = {
      shadowMap: { autoUpdate: true, needsUpdate: true },
      getRenderTarget: () => current,
      setRenderTarget: (v) => {
        current = v;
      },
      getViewport: (v) => v.copy(viewport),
      setViewport: (...v) => {
        viewport = v.length === 1 ? v[0].clone() : new THREE.Vector4(...v);
      },
      getScissor: (v) => v.copy(scissor),
      setScissor: (v) => {
        scissor = v.clone();
      },
      getScissorTest: () => testScissor,
      setScissorTest: (v) => {
        testScissor = v;
      },
      getClearColor: (c) => c.copy(clear),
      getClearAlpha: () => alpha,
      setClearColor: (c, a) => {
        clear = new THREE.Color(c);
        alpha = a;
      },
      clear: () => {},
    };
    const engine = { renderer: r, scene },
      target = { width: 1024, height: 256 };
    const run = () =>
      withTextureStackUVState(engine, target, uniform, () => {
        expect(m.depthTest).toBe(false);
        expect(helper.visible).toBe(false);
        expect(mesh.frustumCulled).toBe(false);
        expect(uniform.value).toBe(1);
        if (fails) throw new Error("GPU unavailable");
        return 1;
      });
    if (fails) expect(run).toThrow("GPU unavailable");
    else expect(run()).toBe(1);
    expect(m.depthTest).toBe(true);
    expect(m.depthWrite).toBe(true);
    expect(m.wireframe).toBe(true);
    expect(helper.visible).toBe(true);
    expect(mesh.frustumCulled).toBe(true);
    expect(helper.frustumCulled).toBe(true);
    expect(uniform.value).toBe(0);
    expect(r.shadowMap).toEqual({ autoUpdate: true, needsUpdate: true });
    expect(scene.background.getHexString()).toBe("334455");
    expect(current).toBe(null);
    expect(viewport.toArray()).toEqual([1, 2, 3, 4]);
    expect(scissor.toArray()).toEqual([2, 3, 4, 5]);
    expect(testScissor).toBe(true);
    expect(alpha).toBe(1);
    m.dispose();
    helper.material.dispose();
    mesh.geometry.dispose();
    helper.geometry.dispose();
  }
});

test("system white fallback is a plain nonmetal physical material, never a recipe that forces brushed-metal defaults", async () => {
  const { createTextureSystemBaseMaterial } =
    await import("../src/textureSystemBaseMaterial.js");
  const m = createTextureSystemBaseMaterial();
  expect(m.color.getHexString()).toBe("ffffff");
  expect(m.metalness).toBe(0);
  expect(m.roughness).toBe(0.75);
  expect(m.clearcoat).toBe(0);
  expect(m.anisotropy).toBe(0);
  m.dispose();
});

test("returning to the currently displayed mesh maps cancels a pending replacement without disposing the live textures", async () => {
  const { createTextureMeshSupportController } =
    await import("../src/textureMeshSupportController.js");
  const originalImage = globalThis.Image,
    pending = [];
  globalThis.Image = class {
    width = 16;
    height = 16;
    decode() {
      return new Promise((resolve, reject) =>
        pending.push({ resolve, reject }),
      );
    }
  };
  const engine = { renderer: { domElement: { dataset: {} } }, dirty: false },
    errors = [],
    c = createTextureMeshSupportController(engine, (error) =>
      errors.push(error),
    );
  try {
    const a = maps({ occlusion: map(png.whiteAO) }),
      b = maps();
    const initial = c.update(a);
    pending.shift().resolve();
    await initial;
    const live = c.uniforms.uMeshAO.value;
    let disposed = 0;
    live.addEventListener("dispose", () => disposed++);
    const replacement = c.update(b);
    await c.update(a);
    pending.shift().resolve();
    await replacement;
    expect(c.uniforms.uMeshAO.value === live).toBe(true);
    expect(disposed).toBe(0);
    expect(c.uniforms.uMeshSupportBits.value).toBe(1);
    expect(errors).toEqual([]);
  } finally {
    c.dispose();
    if (originalImage === undefined) delete globalThis.Image;
    else globalThis.Image = originalImage;
  }
});

test("failed mesh-map bundle decode leaves the complete previous support live, and identical pending requests do not allocate twice", async () => {
  const { createTextureMeshSupportController } =
    await import("../src/textureMeshSupportController.js");
  const originalImage = globalThis.Image,
    pending = [];
  globalThis.Image = class {
    width = 16;
    height = 16;
    decode() {
      return new Promise((resolve, reject) =>
        pending.push({ resolve, reject }),
      );
    }
  };
  const engine = { renderer: { domElement: { dataset: {} } }, dirty: false },
    errors = [],
    c = createTextureMeshSupportController(engine, (error) =>
      errors.push(error),
    );
  try {
    const initial = c.update(maps());
    pending.shift().resolve();
    await initial;
    const live = c.uniforms.uMeshAO.value;
    let disposed = 0;
    live.addEventListener("dispose", () => disposed++);
    const bundle = maps({
      occlusion: map(png.whiteAO),
      normal: map(png.flatNormal),
    });
    const job = c.update(bundle);
    const repeated = c.update(bundle);
    expect(pending.length).toBe(1);
    pending.shift().resolve();
    await Promise.resolve();
    expect(pending.length).toBe(1);
    pending.shift().reject(new Error("Invalid PNG payload"));
    await job;
    await repeated;
    expect(c.uniforms.uMeshAO.value === live).toBe(true);
    expect(c.uniforms.uMeshSupportBits.value).toBe(1);
    expect(engine.renderer.domElement.dataset.meshSupportMaps).toBe(
      '["occlusion"]',
    );
    expect(disposed).toBe(0);
    expect(errors).toEqual(["Mesh support: Invalid PNG payload"]);
  } finally {
    c.dispose();
    // Do not leave unresolved mock decodes alive if an assertion fails.
    pending.forEach((p) => p.resolve());
    if (originalImage === undefined) delete globalThis.Image;
    else globalThis.Image = originalImage;
  }
});

test("cutout and physical-transmission recipes keep the white support beneath uncovered parts", () => {
  for (const params of [
    { ...recipe, type: 5 },
    { ...recipe, translucency: 0.4 },
    { ...recipe, type: 27 },
    { ...recipe, type: 28 },
  ]) {
    const doc = validateTextureDocument({
      ...createTextureDocument(),
      layers: [createTextureLayer("material", { surfaceMaterial: params })],
    });
    expect(evaluateTextureStack(doc).fallbackParts).toEqual([
      "body",
      "lid",
      "handle",
      "spout",
    ]);
  }
});
