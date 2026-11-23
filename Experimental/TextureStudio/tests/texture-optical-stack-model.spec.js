import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  textureOpticalTargetSize,
  textureOpticalSteps,
  textureOpticalUncoveredPasses,
  installTextureOpticalHistory,
  withTextureOpticalState,
  TEXTURE_OPTICAL_PIXEL_LIMIT,
  TEXTURE_OPTICAL_EDGE_LIMIT,
} from "../src/textureOpticalStackController.js";
import { createMaterial, materials } from "../src/materials.js";

for (const [width, height] of [
  [796, 428],
  [1592, 856],
  [12000, 9000],
  [1024, 256],
  [0, Infinity],
])
  test(`optical lit history ${width}x${height} keeps aspect and bounded GPU allocation`, () => {
    const [w, h] = textureOpticalTargetSize(width, height);
    expect(w).toBeGreaterThanOrEqual(1);
    expect(h).toBeGreaterThanOrEqual(1);
    expect(w * h).toBeLessThanOrEqual(TEXTURE_OPTICAL_PIXEL_LIMIT);
    expect(Math.max(w, h)).toBeLessThanOrEqual(TEXTURE_OPTICAL_EDGE_LIMIT);
    if (width > 0 && Number.isFinite(height))
      expect(w / h).toBeCloseTo(width / height, 2);
    if (width === 1024) expect([w, h]).toEqual([1024, 256]);
  });
function pass(name, order, transmission = 0) {
  const mesh = new THREE.Mesh(
    new THREE.BufferGeometry(),
    new THREE.MeshPhysicalMaterial({ transmission }),
  );
  mesh.name = name;
  mesh.renderOrder = order;
  return mesh;
}
test("optical barriers capture only lower passes in common document order, never a higher stamp or material", () => {
  const passes = [
    pass("upper-stamp", 600),
    pass("lower-paint", 100),
    pass("glass", 300, 0.97),
    pass("lower-stamp", 200),
    pass("wax", 500, 0.16),
    pass("mid-paint", 400),
  ];
  expect(
    textureOpticalSteps(passes).map((s) => [
      s.kind,
      s.meshes.map((m) => m.name),
    ]),
  ).toEqual([
    ["ordinary", ["lower-paint", "lower-stamp"]],
    ["optical", ["glass"]],
    ["ordinary", ["mid-paint"]],
    ["optical", ["wax"]],
    ["ordinary", ["upper-stamp"]],
  ]);
  expect(passes[0].name).toBe("upper-stamp");
  passes.forEach((p) => {
    p.geometry.dispose();
    p.material.dispose();
  });
});
test("glass and wax keep native optical parameters; the hook replaces only the lower-framebuffer consumer and bounds UV sampling", () => {
  for (const id of ["crystal-glass", "beeswax"]) {
    const m = createMaterial(materials.find((p) => p.id === id)),
      t = m.transmission,
      thickness = m.thickness,
      ior = m.ior,
      color = m.attenuationColor.getHexString(),
      uniform = { value: 1 },
      tex = new THREE.Texture(),
      uniforms = {
        uTextureOpticalHistory: { value: tex },
        uTextureOpticalSize: { value: new THREE.Vector2(1024, 256) },
      };
    m.userData.textureStackUV = uniform;
    installTextureOpticalHistory(m, uniforms);
    const installed = m.onBeforeCompile;
    installTextureOpticalHistory(m, uniforms);
    expect(m.onBeforeCompile).toBe(installed);
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    m.onBeforeCompile(shader);
    expect(m.transmission).toBe(t);
    expect(t).toBeGreaterThan(0);
    expect(m.thickness).toBe(thickness);
    expect(m.ior).toBe(ior);
    expect(m.attenuationColor.getHexString()).toBe(color);
    expect(shader.uniforms.uTextureOpticalHistory).toBe(
      uniforms.uTextureOpticalHistory,
    );
    expect(shader.uniforms.uTextureOpticalUV).toBe(uniform);
    expect(shader.fragmentShader).toContain("getIBLVolumeRefraction");
    expect(shader.fragmentShader).toContain("volumeAttenuation");
    expect(shader.fragmentShader).not.toContain(
      "uniform sampler2D transmissionSamplerMap",
    );
    expect(shader.fragmentShader).toContain(
      "uniform sampler2D uTextureOpticalHistory",
    );
    expect(shader.fragmentShader).toContain(
      "gl_FragCoord.xy/uTextureOpticalSize,0.",
    );
    expect(shader.fragmentShader).toContain("lod=clamp(lod");
    tex.dispose();
    m.dispose();
  }
});
test("nontransmissive material recipes do not acquire another optical sampler or shader hook", () => {
  const m = createMaterial(materials.find((p) => p.id === "racing-green")),
    original = m.onBeforeCompile;
  installTextureOpticalHistory(m, {});
  expect(m.onBeforeCompile).toBe(original);
  expect(m.userData.textureOpticalHistory).toBeUndefined();
  m.dispose();
});
function mockRenderer() {
  let target = { name: "original" },
    cube = 2,
    mip = 3,
    viewport = new THREE.Vector4(10, 20, 300, 200),
    scissor = new THREE.Vector4(1, 2, 99, 88),
    scissorTest = true,
    clear = new THREE.Color("#223344"),
    alpha = 0.25;
  return {
    autoClear: true,
    toneMapping: THREE.ACESFilmicToneMapping,
    transmissionResolutionScale: 1,
    shadowMap: { autoUpdate: true, needsUpdate: true },
    getRenderTarget: () => target,
    getActiveCubeFace: () => cube,
    getActiveMipmapLevel: () => mip,
    setRenderTarget: (v, c = 0, m = 0) => {
      target = v;
      cube = c;
      mip = m;
    },
    getViewport: (v) => v.copy(viewport),
    setViewport: (...v) => {
      viewport = v.length === 1 ? v[0].clone() : new THREE.Vector4(...v);
    },
    getScissor: (v) => v.copy(scissor),
    setScissor: (v) => {
      scissor = v.clone();
    },
    getScissorTest: () => scissorTest,
    setScissorTest: (v) => {
      scissorTest = v;
    },
    getClearColor: (c) => c.copy(clear),
    getClearAlpha: () => alpha,
    setClearColor: (c, a) => {
      clear = new THREE.Color(c);
      alpha = a;
    },
  };
}
for (const fails of [false, true])
  test(`optical ${fails ? "failed" : "successful"} render restores renderer, native material visibility/side and mesh-parent traversal`, () => {
    const scene = new THREE.Scene(),
      root = pass("base", 0),
      child = pass("glass", 300, 0.97),
      floor = pass("floor", 0),
      hidden = pass("hidden", 0),
      r = mockRenderer();
    scene.background = new THREE.Color("#334455");
    root.add(child);
    scene.add(root, floor, hidden);
    hidden.visible = false;
    child.material.side = THREE.DoubleSide;
    const originalTarget = r.getRenderTarget(),
      engine = { scene, specimen: root, renderer: r };
    const run = () =>
      withTextureOpticalState(engine, ({ activate }) => {
        activate(new Set([child]));
        expect(root.visible).toBe(true);
        expect(root.material.visible).toBe(false);
        expect(child.visible).toBe(true);
        expect(child.material.visible).toBe(true);
        expect(floor.visible).toBe(false);
        r.setRenderTarget({ name: "temporary" });
        r.setViewport(0, 0, 64, 64);
        r.setScissor(new THREE.Vector4(0, 0, 64, 64));
        r.setScissorTest(false);
        r.setClearColor("#ff0000", 0);
        r.autoClear = false;
        r.toneMapping = THREE.NoToneMapping;
        r.transmissionResolutionScale = 0.0625;
        r.shadowMap.autoUpdate = false;
        r.shadowMap.needsUpdate = false;
        scene.background = null;
        child.material.side = THREE.BackSide;
        if (fails) throw new Error("GPU failure");
        return 1;
      });
    if (fails) expect(run).toThrow("GPU failure");
    else expect(run()).toBe(1);
    expect(r.getRenderTarget()).toBe(originalTarget);
    expect(r.getActiveCubeFace()).toBe(2);
    expect(r.getActiveMipmapLevel()).toBe(3);
    expect(r.getViewport(new THREE.Vector4()).toArray()).toEqual([
      10, 20, 300, 200,
    ]);
    expect(r.getScissor(new THREE.Vector4()).toArray()).toEqual([1, 2, 99, 88]);
    expect(r.getScissorTest()).toBe(true);
    expect(r.getClearColor(new THREE.Color()).getHexString()).toBe("223344");
    expect(r.getClearAlpha()).toBe(0.25);
    expect(r.autoClear).toBe(true);
    expect(r.toneMapping).toBe(THREE.ACESFilmicToneMapping);
    expect(r.transmissionResolutionScale).toBe(1);
    expect(r.shadowMap.autoUpdate).toBe(true);
    expect(r.shadowMap.needsUpdate).toBe(fails);
    expect(scene.background.getHexString()).toBe("334455");
    expect(root.visible).toBe(true);
    expect(root.material.visible).toBe(true);
    expect(child.material.side).toBe(THREE.DoubleSide);
    expect(floor.visible).toBe(true);
    expect(hidden.visible).toBe(false);
    [root, child, floor, hidden].forEach((m) => {
      m.geometry.dispose();
      m.material.dispose();
    });
  });

for (const full of [true, false])
  test(`higher ${full ? "opaque" : "partial or masked"} component coverage ${full ? "culls" : "retains"} lower native glass`, () => {
    const glass = pass("glass", 100, 0.97),
      body = pass("body-metal", 200),
      lid = pass("lid-metal", 300);
    glass.userData.textureStackParts = ["body", "lid"];
    body.userData.textureStackParts = ["body"];
    lid.userData.textureStackParts = ["lid"];
    body.userData.textureStackFullyCovers = true;
    lid.userData.textureStackFullyCovers = full;
    expect(
      textureOpticalUncoveredPasses([lid, glass, body]).includes(glass),
    ).toBe(!full);
    expect(textureOpticalUncoveredPasses([body, glass]).includes(glass)).toBe(
      true,
    );
    [glass, body, lid].forEach((m) => {
      m.geometry.dispose();
      m.material.dispose();
    });
  });

test("lit history never samples its write target, reuses native/UV resources independently, resizes and disposes without retaining texture aliases", async () => {
  const { createTextureOpticalStackController } =
      await import("../src/textureOpticalStackController.js"),
    scene = new THREE.Scene(),
    root = pass("base", 0),
    metal = pass("metal", 100),
    glass = pass("glass", 200, 0.97),
    wax = pass("wax", 300, 0.16),
    r = mockRenderer();
  root.add(metal, glass, wax);
  scene.add(root);
  metal.userData.textureStackPass =
    glass.userData.textureStackPass =
    wax.userData.textureStackPass =
      true;
  r.setRenderTarget(null);
  r.toneMappingExposure = 1.22;
  r.outputColorSpace = THREE.SRGBColorSpace;
  r.domElement = { dataset: {} };
  r.shadowMap.enabled = false;
  r.extensions = { has: () => true };
  let width = 800,
    height = 400,
    disposed = 0;
  const used = new Set(),
    stages = [];
  r.getDrawingBufferSize = (v) => v.set(width, height);
  r.clear = () => {};
  const engine = {
      scene,
      specimen: root,
      renderer: r,
      camera: new THREE.PerspectiveCamera(),
    },
    controller = createTextureOpticalStackController(engine);
  const shaders = new Map();
  for (const m of [glass, wax]) {
    controller.install(m.material);
    const shader = {
      uniforms: {},
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    m.material.onBeforeCompile(shader);
    shaders.set(m, shader);
  }
  r.render = (s) => {
    const target = r.getRenderTarget();
    if (target?.isWebGLRenderTarget && !used.has(target)) {
      used.add(target);
      target.addEventListener("dispose", () => disposed++);
    }
    if (s !== scene) return;
    const active = [];
    scene.traverseVisible((o) => {
      if (o.isMesh && o.material.visible) active.push(o.name);
    });
    stages.push(active);
    for (const [m, shader] of shaders) {
      if (m.visible && m.material.visible) {
        expect(
          shader.uniforms.uTextureOpticalHistory.value === target?.texture,
        ).toBe(false);
        expect(shader.uniforms.uTextureOpticalSize.value.toArray()).toEqual([
          target.width,
          target.height,
        ]);
      }
    }
  };
  controller.render();
  expect(stages).toEqual([["base"], ["metal"], ["glass"], ["wax"]]);
  expect(used.size).toBe(2);
  expect(disposed).toBe(0);
  const nativeHistory =
    shaders.get(glass).uniforms.uTextureOpticalHistory.value;
  controller.render();
  expect(used.size).toBe(2);
  expect(disposed).toBe(0);
  const uv = new THREE.WebGLRenderTarget(1024, 256);
  r.setRenderTarget(uv);
  width = 1024;
  height = 256;
  controller.render({ uv: true });
  expect(used.size).toBe(5); // two pairs plus caller's target
  expect(
    shaders.get(glass).uniforms.uTextureOpticalHistory.value === nativeHistory,
  ).toBe(false);
  expect(disposed).toBe(0);
  r.setRenderTarget(null);
  width = 800;
  height = 400;
  controller.render();
  expect(
    shaders.get(glass).uniforms.uTextureOpticalHistory.value === nativeHistory,
  ).toBe(true);
  expect(disposed).toBe(0);
  width = 640;
  height = 360;
  controller.render();
  expect(disposed).toBe(2);
  expect(glass.material.transmission).toBe(0.97);
  expect(wax.material.transmission).toBe(0.16);
  controller.dispose();
  expect(disposed).toBe(6);
  expect(
    shaders.get(glass).uniforms.uTextureOpticalHistory.value === nativeHistory,
  ).toBe(false);
  expect(
    shaders.get(glass).uniforms.uTextureOpticalSize.value.toArray(),
  ).toEqual([16, 16]);
  const count = stages.length;
  controller.render();
  expect(stages.length).toBe(count);
  uv.dispose();
  [root, metal, glass, wax].forEach((m) => {
    m.geometry.dispose();
    m.material.dispose();
  });
});
