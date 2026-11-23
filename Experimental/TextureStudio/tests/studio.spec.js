import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  materials,
  createMaterial,
  normalizeMaterial,
} from "../src/materials.js";
import {
  getRecipe,
  applyRecipeControl,
  applyRecipeColor,
  basicWeaves,
} from "../src/materialProfiles.js";

test("procedural library, live editing, local presets and export", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  // Full catalogue compilation is covered by expansion.spec.js. These
  // interaction tests deliberately exercise the UI while the queue continues.
  await expect(page.locator(".material-preview img").first()).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await expect(
    page.getByRole("heading", { name: "Racing Green", exact: true, level: 1 }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Paint", exact: true }).click();
  await expect(page.getByRole("button", { name: /^Apply / })).toHaveCount(
    materials.filter((m) => m.category === "Paint").length,
  );
  await page.getByRole("button", { name: "Apply Midnight Amethyst" }).click();
  await page
    .getByRole("slider", { name: "Flake coverage", exact: true })
    .fill("0.91");
  await expect(
    page.getByRole("spinbutton", { name: "Flake coverage value" }),
  ).toHaveValue("91");
  await page.getByRole("button", { name: "Flake colors", exact: true }).click();
  await page.getByLabel("Flake color palette").selectOption("Vivid spectrum");
  await expect(page.getByLabel("Flake color 1", { exact: true })).toHaveValue(
    "#ec5667",
  );
  await page.getByLabel("Paint color hex").fill("#265DA8");
  await expect(page.getByLabel("Paint color", { exact: true })).toHaveValue(
    "#265da8",
  );
  await page.getByRole("button", { name: /Save as preset/ }).click();
  await page
    .getByPlaceholder("Give your material a name")
    .fill("Ocean Chromatic");
  await page.getByRole("button", { name: "Save to my library" }).click();
  await expect(page.locator("h1")).toHaveText("Ocean Chromatic");
  await page.reload();
  await page.getByRole("button", { name: /My collection/ }).click();
  await page.getByRole("button", { name: "Apply Ocean Chromatic" }).click();
  await page.getByRole("button", { name: /Export material/ }).click();
  const downloading = page.waitForEvent("download");
  await page
    .getByRole("button", { name: /Material preset All surface/ })
    .click();
  const download = await downloading;
  const stream = await download.createReadStream();
  let text = "";
  for await (const chunk of stream) text += chunk;
  const preset = JSON.parse(text);
  expect(preset.schema).toBe("alloy.material.v6");
  expect(preset.material.recipeId).toBe("paint");
  expect(preset.material.name).toBe("Ocean Chromatic");
  expect(preset.material.flakes).toBe(0.91);
  expect(preset.material.colors[0]).toBe("#ec5667");
  await page.getByRole("button", { name: /Export material/ }).click();
  const shaderDownload = page.waitForEvent("download");
  await page
    .getByRole("button", { name: /Three.js procedural shader/ })
    .click();
  const shaderStream = await (await shaderDownload).createReadStream();
  let shaderText = "";
  for await (const chunk of shaderStream) shaderText += chunk;
  // Verify exported code is self-contained, not a minified runtime closure.
  const factoryText = shaderText
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export const preset/, "const preset")
    .replace(/export (const|let|var|class|function|async function) /g, "$1 ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const material = new Function("THREE", factoryText)(THREE);
  expect(material.isMeshPhysicalMaterial).toBe(true);
  expect(material.userData.params.flakes).toBe(0.91);
  material.dispose();
  await page.getByRole("button", { name: /Export material/ }).click();
  const imageDownload = page.waitForEvent("download");
  await page.getByRole("button", { name: /Viewport snapshot/ }).click();
  const imageStream = await (await imageDownload).createReadStream();
  const imageParts = [];
  for await (const chunk of imageStream) imageParts.push(chunk);
  const image = Buffer.concat(imageParts);
  expect(image.subarray(1, 4).toString()).toBe("PNG");
  expect(image.length).toBeGreaterThan(1000);

  expect(errors).toEqual([]);
});

test("every material family, preview controls and mobile library", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  // Full catalogue compilation is covered by expansion.spec.js. These
  // interaction tests deliberately exercise the UI while the queue continues.
  await expect(page.locator(".material-preview img").first()).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  for (const name of [
    "Liquid Silver",
    "Carbon Ceramic",
    "Performance Rubber",
    "Woven Fabric",
    "Crystal Glass",
    "Brake Disc",
    "Piano Black",
    "Grained ABS",
    "Worn Polymer",
    "Suede Microfiber",
    "Midnight Velvet",
    "Natural Cotton",
    "Indigo Denim",
    "Champagne Silk",
    "Midnight Satin",
    "Cognac Leather",
    "Nappa Leather",
    "Plain Linen",
    "Basket Cotton",
    "Ribbed Cotton",
    "Oxford Weave",
    "Herringbone Wool",
    "Houndstooth Wool",
    "Aurora Flip",
    "Sunset Prism",
    "Opal Pearl",
    "Carbon Fiber",
  ]) {
    await page
      .getByRole("button", { name: "Apply " + name, exact: true })
      .click();
    await expect(page.locator("h1")).toHaveText(name);
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
  }
  for (const shape of [
    "Rounded cube",
    "Torus knot",
    "Draped cloth",
    "Shader ball",
  ]) {
    await page.getByLabel("Preview object").selectOption(shape);
    await expect(page.getByLabel("Preview object")).toHaveValue(shape);
  }
  await page.getByLabel("Studio lighting").selectOption("Warm atelier");
  await page.getByRole("button", { name: "Toggle wireframe" }).click();
  await expect(page.locator(".viewport-status")).toContainText("WIREFRAME");
  await page.getByRole("button", { name: "Toggle wireframe" }).click();
  await page.getByRole("button", { name: /Layers 2/ }).click();
  await expect(page.locator(".layer-card")).toHaveCount(2);
  await page.getByRole("button", { name: "Edit layer properties" }).click();
  await page.setViewportSize({ width: 390, height: 844 });
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.getByRole("button", { name: "Focus viewport (F)" }).click();
  await expect(page.locator(".inspector-panel")).toBeHidden();
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "Toggle material library" }).click();
  await page.getByRole("button", { name: "Apply Candy Crimson" }).click();
  await expect(page.locator("h1")).toHaveText("Candy Crimson");
  await expect(page.locator(".library-panel")).toBeHidden();
  expect(errors).toEqual([]);
});

test("macro inspection, extended ranges, bounded finish controls and color ramp persist", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  // Full catalogue compilation is covered by expansion.spec.js. These
  // interaction tests deliberately exercise the UI while the queue continues.
  await expect(page.locator(".material-preview img").first()).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page.getByRole("button", { name: "Macro", exact: true }).click();
  await expect(page.locator("canvas")).toHaveAttribute("data-zoom", "800");
  await page.getByLabel("Viewport zoom", { exact: true }).fill("4");
  await expect(page.locator("canvas")).toHaveAttribute("data-zoom", "10000");
  await page.getByRole("button", { name: "Zoom out", exact: true }).click();
  await expect(page.locator("canvas")).toHaveAttribute("data-zoom", "6667");
  await page.getByRole("button", { name: "Fit", exact: true }).click();
  await expect(page.locator("canvas")).toHaveAttribute("data-zoom", "100");
  // Optical zoom leaves the camera outside the surface at every magnification.
  const canvas = page.locator("canvas");
  const rect = await canvas.boundingBox();
  await page.mouse.move(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
  await page.mouse.wheel(0, -500);
  await expect.poll(() => canvas.getAttribute("data-zoom")).not.toBe("100");
  await page.getByRole("button", { name: "Fit", exact: true }).click();

  await page.getByLabel("Flake size value", { exact: true }).fill("0.0001");
  await page.getByLabel("Flake size value", { exact: true }).press("Tab");
  await expect(
    page.getByLabel("Flake size value", { exact: true }),
  ).toHaveValue("0.0001");
  await page.getByLabel("Flake size value", { exact: true }).fill("2500");
  await expect(
    page.getByLabel("Flake size value", { exact: true }),
  ).toHaveValue("2500");
  await page.getByLabel("Flake sparkle value", { exact: true }).fill("75");
  await page.getByLabel("Flake reflectivity value", { exact: true }).fill("25");
  await page
    .getByRole("button", { name: "Surface detail", exact: true })
    .click();
  await page.getByLabel("Flake repetition value", { exact: true }).fill("0.25");
  await page
    .getByLabel("Orange-peel texture value", { exact: true })
    .fill("100");
  await page.getByLabel("Texture scale value", { exact: true }).fill("850");
  await expect(page.getByLabel("Slab IOR value", { exact: true })).toHaveCount(
    0,
  );
  await expect(page.getByLabel("Metallic value", { exact: true })).toHaveCount(
    0,
  );
  await page.getByRole("button", { name: "Flake colors", exact: true }).click();
  await page.getByRole("button", { name: "Color ramp", exact: true }).click();
  await page
    .getByRole("button", { name: "Add flake color", exact: true })
    .click();
  await expect(
    page.getByLabel("Flake color 6", { exact: true }),
  ).toBeAttached();
  await page.getByLabel("Selected flake color hex").fill("#FF3388");
  await page.getByLabel("Stop position value", { exact: true }).fill("73");
  await page.getByRole("button", { name: /Save as preset/ }).click();
  await page
    .getByPlaceholder("Give your material a name")
    .fill("Macro chromatic");
  await page.getByRole("button", { name: "Save to my library" }).click();
  const saved = await page.evaluate(
    () => JSON.parse(localStorage.getItem("alloy-saved"))[0],
  );
  expect(saved).toMatchObject({
    flakeSize: 2500,
    flakeScale: 0.25,
    ior: 1.5,
    coatIor: 1.5,
    orangePeel: 0.9,
    orangePeelScale: 850,
    recipeId: "paint",
    colorMode: "ramp",
  });
  expect(saved.colors[5].toLowerCase()).toBe("#ff3388");
  expect(saved.colorStops[5]).toBe(0.73);
  expect(saved.flakeRoughnessMin).toBeCloseTo(0.12125);
  expect(saved.flakeRoughnessMax).toBeCloseTo(0.26);
  expect(saved.flakeMetalnessMin).toBeCloseTo(0.7825);
  expect(saved.flakeMetalnessMax).toBeCloseTo(0.91);
  expect(errors).toEqual([]);
});

test("bounded physical parameters and extended scale connect to shader uniforms", async () => {
  const { materials, createMaterial, normalizeMaterial } =
    await import("../src/materials.js");
  const p = {
    ...materials[0],
    ior: 1.74,
    coatIor: 1.91,
    specular: 0.61,
    flakeSize: 2500,
    flakeScale: 0.5,
    flakeRoughnessMin: 0.22,
    flakeRoughnessMax: 0.58,
    flakeMetalnessMin: 0.43,
    flakeMetalnessMax: 0.99,
    orangePeel: 1.7,
    orangePeelScale: 800,
  };
  const material = createMaterial(p);
  const shader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  material.onBeforeCompile(shader);
  expect(material.ior).toBe(1.5);
  expect(material.specularIntensity).toBe(0.61);
  for (const [uniform, value] of Object.entries({
    uCoatIor: 1.5,
    uFlakeFrequency: 10.4,
    uFlakeRoughMin: 0.22,
    uFlakeRoughMax: 0.5,
    uFlakeMetalMin: 0.72,
    uFlakeMetalMax: 0.99,
    uPeel: 0.9,
    uPeelScale: 800,
  }))
    expect(shader.uniforms[uniform].value).toBe(value);
  expect(shader.fragmentShader).toContain(
    "material.clearcoatF0 = vec3( pow2( (uCoatIor - 1.0) / (uCoatIor + 1.0) ) );",
  );
  expect(shader.fragmentShader).toContain(
    "roughnessFactor=mix(roughnessFactor,mix(uFlakeRoughMin,uFlakeRoughMax,flakeRandom),flakeMask)",
  );
  expect(shader.fragmentShader).toContain(
    "metalnessFactor=mix(metalnessFactor,mix(uFlakeMetalMin,uFlakeMetalMax,flakeRandom),flakeMask)",
  );
  const legacy = normalizeMaterial({
    type: 0,
    size: 0.35,
    colors: ["#ff0000", "#0000ff"],
  });
  expect(legacy.flakeSize).toBe(350);
  expect(legacy.colorStops).toEqual([0, 1]);
  material.dispose();
});

test("frozen cloth, textile UVs, leather, cellular layers and height-aware wear", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  // Full catalogue compilation is covered by expansion.spec.js. These
  // interaction tests deliberately exercise the UI while the queue continues.
  await expect(page.locator(".material-preview img").first()).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Apply Natural Cotton", exact: true })
    .click();
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Draped cloth",
  );
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-preview-shape",
    "Draped cloth",
  );
  await expect(page.locator(".render-tag")).toHaveText("FROZEN CLOTH");
  const geometry = await page.evaluate(async () => {
    const { createDrapedClothGeometry, clothSupport } =
      await import("/src/clothGeometry.js");
    const a = createDrapedClothGeometry(),
      b = createDrapedClothGeometry(),
      pos = a.getAttribute("position"),
      normal = a.getAttribute("normal");
    let distance = Infinity;
    for (let i = 0; i < 73 * 73; i++)
      distance = Math.min(
        distance,
        Math.hypot(
          pos.getX(i) - clothSupport.center[0],
          pos.getY(i) - clothSupport.center[1],
          pos.getZ(i) - clothSupport.center[2],
        ),
      );
    const result = {
      vertices: pos.count,
      normals: normal.count,
      uvs: a.getAttribute("uv").count,
      finite: [...pos.array, ...normal.array].every(Number.isFinite),
      identical: pos.array.every(
        (v, i) => v === b.getAttribute("position").array[i],
      ),
      frozen: a.userData.frozen,
      minimumSphereClearance: distance - clothSupport.radius,
    };
    a.dispose();
    b.dispose();
    return result;
  });
  expect(geometry.vertices).toBeGreaterThan(5000);
  expect(geometry.normals).toBe(geometry.vertices);
  expect(geometry.uvs).toBe(geometry.vertices);
  expect(geometry.finite).toBe(true);
  expect(geometry.identical).toBe(true);
  expect(geometry.frozen).toBe(true);
  expect(geometry.minimumSphereClearance).toBeGreaterThan(0);
  await page
    .getByRole("button", { name: "Apply Champagne Silk", exact: true })
    .click();
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Draped cloth",
  );
  await expect(
    page.getByLabel("Silk lustre value", { exact: true }),
  ).toBeAttached();
  await expect(
    page.getByLabel("Thread scale value", { exact: true }),
  ).toHaveValue("75");
  await page
    .getByRole("button", { name: "Apply Cognac Leather", exact: true })
    .click();
  await page
    .getByLabel("Preview object", { exact: true })
    .selectOption("Shader ball");
  await expect(
    page.getByLabel("Leather Roughness value", { exact: true }),
  ).toBeAttached();
  await page
    .getByRole("button", { name: "Apply Worn Polymer", exact: true })
    .click();
  const frame = async () =>
    page.evaluate(
      () =>
        new Promise((r) =>
          requestAnimationFrame(() => requestAnimationFrame(r)),
        ),
    );
  await page.getByLabel("Polymer wear value", { exact: true }).fill("0");
  await frame();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const before = await page.locator("canvas").evaluate((c) => c.toDataURL());
  await page.getByLabel("Wear softness value", { exact: true }).fill("80");
  await page.getByLabel("Worn polish value", { exact: true }).fill("80");
  await page.getByLabel("Polymer wear value", { exact: true }).fill("100");
  await frame();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const after = await page.locator("canvas").evaluate((c) => c.toDataURL());
  expect(after).not.toBe(before);
  await page
    .getByRole("button", { name: "Apply Racing Green", exact: true })
    .click();
  await page.getByLabel("Flake layers value", { exact: true }).fill("4");
  await page.getByLabel("Buried flake tint value", { exact: true }).fill("88");
  await page.getByRole("button", { name: /Save as preset/ }).click();
  await page
    .getByPlaceholder("Give your material a name")
    .fill("Layered cellular finish");
  await page.getByRole("button", { name: "Save to my library" }).click();
  const saved = await page.evaluate(
    () => JSON.parse(localStorage.getItem("alloy-saved"))[0],
  );
  expect(saved.flakeLayers).toBe(4);
  expect(saved.flakeLayerDepth).toBe(0.76);
  expect(saved.materialVersion).toBe(6);
  expect(errors).toEqual([]);
});

test("cellular flakes and polished height are actual shader paths", async () => {
  const { createMaterial } = await import("../src/materials.js");
  const paint = createMaterial({
    ...materials[0],
    flakeLayers: 4,
    flakeLayerDepth: 0.8,
  });
  const shader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  paint.onBeforeCompile(shader);
  expect(shader.uniforms.uFlakeLayers.value).toBe(4);
  expect(shader.uniforms.uFlakeLayerDepth.value).toBe(0.8);
  expect(shader.fragmentShader).toContain("cellular3(q,l*97.3+4.1)");
  expect(shader.fragmentShader).not.toContain("flakeCell(");
  expect(shader.fragmentShader).toContain("blended=mix(blended,tint,opacity)");
  const plastic = createMaterial({
    ...materials.find((m) => m.id === "worn-polymer"),
    wearSoftness: 0.8,
    wornRoughness: 0.17,
  });
  plastic.onBeforeCompile(shader);
  expect(shader.uniforms.uWearSoftness.value).toBe(0.8);
  expect(shader.uniforms.uWornRoughness.value).toBe(0.17);
  expect(shader.fragmentShader).toContain(
    "polished=polishHeight(micro,wearField)",
  );
  expect(shader.fragmentShader).toContain("surfaceHeight=polished*uGrain");
  expect(shader.fragmentShader).toContain("uWornRoughness,wearMask");
  const silk = createMaterial({
    ...materials.find((m) => m.id === "champagne-silk"),
    clothMapping: true,
  });
  silk.onBeforeCompile(shader);
  expect(shader.uniforms.uCloth.value).toBe(1);
  expect(shader.uniforms.uFabricMode.value).toBe(4);
  expect(shader.fragmentShader).toContain(
    "if(uCloth==1)weave=twill(vProcUv*4.9)",
  );
  paint.dispose();
  plastic.dispose();
  silk.dispose();
});

test("every material recipe bounds all its art-direction controls", async () => {
  expect(materials.length).toBeGreaterThanOrEqual(100);
  for (const p of materials) {
    const recipe = getRecipe(p);
    expect(recipe.controls.length).toBeGreaterThan(0);
    for (const control of recipe.controls) {
      for (const value of [-10, 0, 0.5, 1, 10]) {
        const result = applyRecipeControl(p, control, value);
        if (control.direct) {
          expect(result[control.key]).toBeGreaterThanOrEqual(
            control.extend ? 0.000001 : control.min,
          );
          expect(result[control.key]).toBeLessThanOrEqual(
            control.extend ? 1000000 : control.max,
          );
        } else
          for (const t of control.targets) {
            expect(result[t.key]).toBeGreaterThanOrEqual(
              Math.min(t.min, t.max) - 1e-9,
            );
            expect(result[t.key]).toBeLessThanOrEqual(
              Math.max(t.min, t.max) + 1e-9,
            );
          }
        for (const [key, value] of Object.entries(recipe.fixed))
          expect(result[key]).toBe(value);
      }
    }
    expect(getRecipe({ ...p, id: "custom-123", name: "Saved recipe" }).id).toBe(
      recipe.id,
    );
  }
  const velvet = materials.find((p) => p.id === "midnight-velvet");
  const rough = getRecipe(velvet).controls.find((c) => c.id === "roughness");
  expect(applyRecipeControl(velvet, rough, 0)).toMatchObject({
    roughness: 0.78,
    sheenRoughness: 0.3,
    metalness: 0,
    coat: 0,
  });
  expect(applyRecipeControl(velvet, rough, 1)).toMatchObject({
    roughness: 0.97,
    sheenRoughness: 0.85,
    metalness: 0,
    coat: 0,
  });
  expect(getRecipe(velvet).controls.map((c) => c.id)).toEqual([
    "roughness",
    "softness",
    "pile",
  ]);
  const dyed = applyRecipeColor(velvet, "color", "#884422");
  expect(dyed.color).toBe("#884422");
  expect(dyed.sheenColor).not.toBe(velvet.sheenColor);
});

test("weave colors and thin-film interference reach physical shader paths", async () => {
  const woven = materials.find((p) => p.id === "natural-cotton");
  const shader = () => ({
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  });
  for (const [i, weave] of basicWeaves().entries()) {
    const material = createMaterial({
        ...woven,
        weavePattern: weave.id,
        warpColor: "#ff0000",
        weftColor: "#0000ff",
      }),
      s = shader();
    material.onBeforeCompile(s);
    expect(s.uniforms.uWeave.value).toBe(i);
    expect(s.uniforms.uWarpColor.value.getHexString()).toBe("ff0000");
    expect(s.uniforms.uWeftColor.value.getHexString()).toBe("0000ff");
    expect(material.color.getHexString()).toBe("ffffff");
    material.dispose();
  }
  for (const p of materials.filter((p) => p.paintEffect === "iridescent")) {
    const material = createMaterial(p),
      s = shader();
    material.onBeforeCompile(s);
    expect(material.iridescence).toBe(1);
    expect(material.iridescenceIOR).toBe(p.iridescenceIOR);
    expect(s.uniforms.uFilmThickness.value).toBe(p.filmThickness);
    expect(s.fragmentShader).toContain(
      "material.iridescenceThickness=uFilmThickness",
    );
    const off = getRecipe(p).controls.find((c) => c.id === "iridescence");
    expect(getRecipe(applyRecipeControl(p, off, 0)).iridescent).toBe(true);
    material.dispose();
  }
});

test("guided inspector, nine live weaves, yarn colors and iridescent paint", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  // Full catalogue compilation is covered by expansion.spec.js. These
  // interaction tests deliberately exercise the UI while the queue continues.
  await expect(page.locator(".material-preview img").first()).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Apply Midnight Velvet", exact: true })
    .click();
  await expect(
    page.locator('.inspector-scroll input[type="range"]'),
  ).toHaveCount(3);
  await expect(page.getByLabel("Weave construction")).toHaveCount(0);
  await expect(page.getByLabel("Metallic value", { exact: true })).toHaveCount(
    0,
  );
  await expect(
    page.getByLabel("Clearcoat IOR value", { exact: true }),
  ).toHaveCount(0);
  await page.getByLabel("Velvet Roughness value", { exact: true }).fill("100");
  await page.getByRole("button", { name: /Save as preset/ }).click();
  await page.getByPlaceholder("Give your material a name").fill("Soft velvet");
  await page.getByRole("button", { name: "Save to my library" }).click();
  const saved = await page.evaluate(
    () => JSON.parse(localStorage.getItem("alloy-saved"))[0],
  );
  expect(saved).toMatchObject({
    recipeId: "velvet",
    roughness: 0.97,
    sheenRoughness: 0.85,
    metalness: 0,
    coat: 0,
  });
  const frame = () =>
    page.evaluate(
      () =>
        new Promise((r) =>
          requestAnimationFrame(() => requestAnimationFrame(r)),
        ),
    );
  const image = async () => {
    await expect(page.locator("canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
    return page.locator("canvas").evaluate((c) => c.toDataURL());
  };
  await page
    .getByRole("button", { name: "Apply Natural Cotton", exact: true })
    .click();
  await page.getByLabel("Thread scale value", { exact: true }).fill("8");
  await page.getByLabel("Warp yarn · lengthwise hex").fill("#a42d26");
  await page.getByLabel("Weft yarn · crosswise hex").fill("#92b3d4");
  await expect(
    page.getByLabel("Weave construction").locator("option"),
  ).toHaveCount(9);
  const patterns = [];
  for (const weave of basicWeaves()) {
    await page.getByLabel("Weave construction").selectOption(weave.id);
    await frame();
    patterns.push(await image());
  }
  expect(new Set(patterns).size).toBe(9);
  await page.getByLabel("Weft yarn · crosswise hex").fill("#39ab68");
  await frame();
  expect(await image()).not.toBe(patterns.at(-1));
  await page
    .getByRole("button", { name: "Apply Aurora Flip", exact: true })
    .click();
  await page.getByLabel("Preview object").selectOption("Shader ball");
  await page
    .getByLabel("Color-shift strength value", { exact: true })
    .fill("0");
  await frame();
  const off = await image();
  await page
    .getByLabel("Color-shift strength value", { exact: true })
    .fill("100");
  await frame();
  expect(await image()).not.toBe(off);
  await page.getByLabel("Color phase value", { exact: true }).fill("100");
  await frame();
  expect(await image()).not.toBe(off);
  await page
    .getByRole("button", { name: "Apply Racing Green", exact: true })
    .click();
  await expect(
    page.getByLabel("Color phase value", { exact: true }),
  ).toHaveCount(0);
  expect(errors).toEqual([]);
});
