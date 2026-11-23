import { test, expect } from "@playwright/test";
import { readFileSync } from "node:fs";
import {
  materials,
  normalizeMaterial,
  createMaterial,
} from "../src/materials.js";
import {
  getRecipe,
  supportsMetalScratches,
  supportsScratches,
  applyRecipeControl,
  recipeControlValue,
} from "../src/materialProfiles.js";
const legacy = readFileSync(
  new URL("./fixtures/scratch-v6.1.glsl", import.meta.url),
  "utf8",
);

test("metal scratches are opt-in, bounded and independent of the metal finish", () => {
  const oldStudy = {
    ...materials.find((p) => p.id === "scratches"),
    scratchDensity: 2.6,
    tuning: { density: 0.65 },
  };
  expect(
    recipeControlValue(
      oldStudy,
      getRecipe(oldStudy).controls.find((c) => c.id === "density"),
    ),
  ).toBeCloseTo(2.6 / 12);
  for (const p of materials.filter(supportsScratches)) {
    expect(p.metalScratches).toBe(false);
    const on = normalizeMaterial({ ...p, metalScratches: true });
    const controls = getRecipe(on).controls.filter(
      (c) => c.group === "scratches",
    );
    expect(controls.length).toBeGreaterThan(5);
    expect(getRecipe(p).controls.some((c) => c.group === "scratches")).toBe(
      false,
    );
    for (const key of [
      "roughness",
      "metalness",
      "coat",
      "detailScale",
      "weaveAngle",
    ])
      expect(on[key]).toBe(p[key]);
    const direction = controls.find((c) => c.key === "scratchAngle");
    const turned = applyRecipeControl(on, direction, -65);
    expect(turned.scratchAngle).toBe(-65);
    expect(turned.weaveAngle).toBe(p.weaveAngle);
    const depth = controls.find((c) => c.id === "metalScratch_depth");
    const deeper = applyRecipeControl(on, depth, 1);
    expect(deeper.depth).toBe(on.depth);
    expect(deeper.scratchDepth).toBe(0.006);
    const density = controls.find((c) => c.id === "metalScratch_density");
    expect(applyRecipeControl(on, density, 1).scratchDensity).toBe(12);
    const a = createMaterial(p),
      b = createMaterial(on);
    expect(a.customProgramCacheKey()).not.toBe(b.customProgramCacheKey());
    a.dispose();
    b.dispose();
  }
  for (const p of materials.filter((p) => !supportsScratches(p)))
    expect(
      normalizeMaterial({ ...p, metalScratches: true }).metalScratches,
    ).toBe(false);
});

test("plastics carry scratches as a finish option, like metals", () => {
  for (const id of ["piano-black", "grained-abs", "worn-polymer"]) {
    const p = materials.find((m) => m.id === id);
    expect(p, id).toBeTruthy();
    expect(supportsScratches(p)).toBe(true);
    expect(supportsMetalScratches(p)).toBe(false);
    expect(getRecipe(p).supportsScratches).toBe(true);
    const on = normalizeMaterial({ ...p, metalScratches: true });
    // The toggle must survive normalization (it was previously stripped
    // for every non-metal) and expose the scratch control group.
    expect(on.metalScratches).toBe(true);
    expect(
      getRecipe(on).controls.filter((c) => c.group === "scratches").length,
    ).toBeGreaterThan(5);
    const a = createMaterial(p),
      b = createMaterial(on);
    expect(a.customProgramCacheKey()).not.toBe(b.customProgramCacheKey());
    expect(b.userData.params.metalScratches).toBe(true);
    a.dispose();
    b.dispose();
  }
});

test("GPU: accepted scratch shape unchanged, greater density, additive metal bake and reversible switch", async ({
  page,
}, testInfo) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => {
    errors.push(e.message);
    console.error(e.message);
  });
  page.on("console", (m) => {
    if (m.type() === "error") {
      errors.push(m.text());
      console.error(m.text());
    }
  });
  await page.goto("/?material=pure-aluminium");
  await page.locator(".inspector-tabs").getByRole("button", { name: "Properties", exact: true }).click();
  const ready = async () => {
    await expect(page.locator("canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
    await page.evaluate(
      () =>
        new Promise((r) =>
          requestAnimationFrame(() => requestAnimationFrame(r)),
        ),
    );
  };
  await ready();
  const toggle = page.getByRole("switch", { name: "Enable scratches" });
  await expect(toggle).not.toBeChecked();
  const before = await page.locator("canvas").evaluate((c) => c.toDataURL());
  await toggle.check();
  await ready();
  expect(await page.locator("canvas").evaluate((c) => c.toDataURL())).not.toBe(
    before,
  );
  await expect(
    page.getByLabel("Tooling scale value", { exact: true }),
  ).toHaveCount(0);
  await page.getByLabel("Scratch density value", { exact: true }).fill("90");
  await ready();
  if (process.env.ALLOY_CAPTURE)
    await page.screenshot({ path: testInfo.outputPath("metal-scratches.png") });
  await toggle.uncheck();
  await ready();
  expect(await page.locator("canvas").evaluate((c) => c.toDataURL())).toBe(
    before,
  );
  const result = await page.evaluate(async (legacyField) => {
    const THREE = await import("/node_modules/three/build/three.module.js");
    const { materials, createMaterial } = await import("/src/materials.js");
    const { architecturalGLSL } = await import("/src/architecturalKernels.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { unzipSync } = await import("/node_modules/fflate/esm/browser.js");
    const renderer = new THREE.WebGLRenderer({
      alpha: true,
      preserveDrawingBuffer: true,
      antialias: false,
    });
    renderer.setSize(256, 256);
    renderer.outputColorSpace = THREE.LinearSRGBColorSpace;
    renderer.toneMapping = THREE.NoToneMapping;
    const scene = new THREE.Scene(),
      camera = new THREE.OrthographicCamera(0, 1, 1, 0, 0.1, 20);
    camera.position.z = 10;
    const geo = new THREE.PlaneGeometry(1, 1);
    geo.translate(0.5, 0.5, 0);
    const mesh = new THREE.Mesh(geo);
    mesh.material.dispose();
    scene.add(mesh);
    const scratch = materials.find((p) => p.id === "scratches");
    async function field(reference, density) {
      const m = createMaterial({
        ...scratch,
        bakeMode: 5,
        scratchDensity: density,
        bakeHeightRange: 0.03,
      });
      mesh.material = m;
      if (reference) {
        const compile = m.onBeforeCompile;
        const current = architecturalGLSL();
        const head = current.slice(0, current.indexOf("vec3 scratchField("));
        m.onBeforeCompile = (s) => {
          compile(s);
          s.fragmentShader = s.fragmentShader.replace(
            current,
            head + legacyField,
          );
        };
        m.customProgramCacheKey = () => "legacy-reference-scratches";
      }
      try {
        await renderer.compileAsync(scene, camera);
        renderer.render(scene, camera);
        const data = new Uint8Array(256 * 256 * 4);
        renderer
          .getContext()
          .readPixels(
            0,
            0,
            256,
            256,
            renderer.getContext().RGBA,
            renderer.getContext().UNSIGNED_BYTE,
            data,
          );
        return Array.from(data);
      } finally {
        m.dispose();
      }
    }
    let old, now, dense;
    try {
      old = await field(true, 2.6);
      now = await field(false, 2.6);
      dense = await field(false, 12);
    } finally {
      geo.dispose();
      renderer.dispose();
      renderer.forceContextLoss();
    }
    const cuts = (px) => px.filter((v, i) => i % 4 === 0 && v < 126).length;
    async function bake(id, on, density) {
      const { bytes } = await bakeMaterial(
        {
          ...materials.find((p) => p.id === id),
          metalScratches: on,
          scratchDensity: density,
        },
        { resolution: 256 },
      );
      const z = unzipSync(bytes);
      return {
        height: Array.from(z["height.png"]),
        normal: Array.from(z["normal.png"]),
        metal: Array.from(z["metalness.png"]),
      };
    }
    const plain = await bake("pure-aluminium", false, 6),
      zero = await bake("pure-aluminium", true, 0),
      cut = await bake("pure-aluminium", true, 6);
    const pipeOff = await bake("corrugated-aluminium", false, 6),
      pipeZero = await bake("corrugated-aluminium", true, 0),
      pipeOn = await bake("corrugated-aluminium", true, 6);
    return {
      delta: Math.max(
        ...old
          .map((v, i) => Math.abs(v - now[i]))
          .filter((_, i) => i % 4 === 0),
      ),
      sparse: cuts(now),
      dense: cuts(dense),
      plain,
      zero,
      cut,
      pipeOff,
      pipeZero,
      pipeOn,
    };
  }, legacy);
  expect(result.delta).toBeLessThanOrEqual(1);
  expect(result.dense).toBeGreaterThan(result.sparse * 2);
  expect(result.zero).toEqual(result.plain);
  expect(result.cut.height).not.toEqual(result.plain.height);
  expect(result.cut.normal).not.toEqual(result.plain.normal);
  expect(result.cut.metal).toEqual(result.plain.metal);
  expect(result.pipeZero).toEqual(result.pipeOff);
  expect(result.pipeOn.height).not.toEqual(result.pipeOff.height);
  expect(errors).toEqual([]);
});
