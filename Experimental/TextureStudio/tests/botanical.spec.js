import { test, expect } from "@playwright/test";
import { materials, createMaterial } from "../src/materials.js";
import { createBotanicalGeometry } from "../src/botanicalGeometry.js";
import { getRecipe } from "../src/materialProfiles.js";
import * as THREE from "three";

test("botanical meshes have complete UVs and new fields have no bitmap inputs", () => {
  for (const kind of ["Leaf", "Petal", "Grass blade", "Stem", "Cactus"]) {
    const g = createBotanicalGeometry(kind);
    expect(g.getAttribute("uv").count).toBe(g.getAttribute("position").count);
    expect(
      Array.from(g.getAttribute("normal").array).every(Number.isFinite),
    ).toBe(true);
    const uv = Array.from(g.getAttribute("uv").array);
    expect(Math.min(...uv)).toBeGreaterThanOrEqual(0);
    expect(Math.max(...uv)).toBeLessThanOrEqual(1);
    g.dispose();
  }
  for (const p of materials.filter((m) => m.type >= 30)) {
    const m = createMaterial(p);
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    m.onBeforeCompile(shader);
    expect(shader.fragmentShader).toContain("#define uType " + p.type);
    expect(m.map).toBeNull();
    expect(m.alphaMap).toBeNull();
    expect(m.normalMap).toBeNull();
    expect(m.alphaToCoverage).toBe(false);
    m.dispose();
  }
  const scratch = materials.find((p) => p.id === "scratches");
  expect(
    getRecipe(scratch).controls.find((c) => c.id === "density").targets[0].max,
  ).toBe(12);
  expect(scratch.scratchBend).toBeGreaterThan(0.12);
});

test("mesh foliage, petals, fruit and cactus render and bake opaque surfaces; scratches are denser and curved", async ({
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
  await page.goto("/?material=broadleaf-green");
  await expect(page.getByLabel("Preview object")).toHaveValue("Leaf");
  async function frame() {
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
  for (const [name, shape] of [
    ["Broadleaf Green", "Leaf"],
    ["Meadow Grass Blades", "Grass blade"],
    ["Rose Petal", "Petal"],
    ["Lily Petal", "Petal"],
    ["Apple Skin", "Sphere"],
    ["Orange Peel", "Sphere"],
    ["Strawberry Skin", "Sphere"],
    ["Cactus Epidermis", "Cactus"],
    ["Living Plant Stem", "Stem"],
    ["Crocodile Belly Leather", "Leather swatch"],
    ["Cognac Leather", null],
    ["Scratches", "Panel"],
  ]) {
    await page
      .getByRole("button", { name: "Apply " + name, exact: true })
      .click();
    if (shape)
      await expect(page.getByLabel("Preview object")).toHaveValue(shape);
    await frame();
    if (process.env.ALLOY_CAPTURE) {
      await page.getByRole("button", { name: "Fit", exact: true }).click();
      await frame();
      await page.screenshot({
        path: testInfo.outputPath(name.replaceAll(" ", "-") + ".png"),
      });
      if (
        [
          "Crocodile Belly Leather",
          "Cognac Leather",
          "Scratches",
          "Broadleaf Green",
        ].includes(name)
      ) {
        await page.getByRole("button", { name: "Macro", exact: true }).click();
        await frame();
        await page.screenshot({
          path: testInfo.outputPath(name.replaceAll(" ", "-") + "-macro.png"),
        });
        await page.getByRole("button", { name: "Fit", exact: true }).click();
      }
    }
  }
  const result = await page.evaluate(async () => {
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { materials } = await import("/src/materials.js");
    const { unzipSync, strFromU8 } =
      await import("/node_modules/.vite/deps/fflate.js");
    async function pixels(bytes) {
      const img = await createImageBitmap(
        new Blob([bytes], { type: "image/png" }),
      );
      const c = document.createElement("canvas");
      c.width = img.width;
      c.height = img.height;
      const ctx = c.getContext("2d");
      ctx.drawImage(img, 0, 0);
      img.close();
      return ctx.getImageData(0, 0, c.width, c.height).data;
    }
    async function bake(name, extra = {}) {
      const p = { ...materials.find((p) => p.name === name), ...extra };
      const { bytes } = await bakeMaterial(p, { resolution: 256 });
      const maps = unzipSync(bytes);
      const stats = {};
      for (const ch of [
        "base-color",
        "normal",
        "height",
        "roughness",
        "metalness",
        "emission",
      ]) {
        const px = await pixels(maps[ch + ".png"]);
        let lo = 255,
          hi = 0,
          alphaMin = 255,
          alphaMax = 0,
          cuts = 0;
        for (let i = 0; i < px.length; i += 4) {
          lo = Math.min(lo, px[i]);
          hi = Math.max(hi, px[i]);
          alphaMin = Math.min(alphaMin, px[i + 3]);
          alphaMax = Math.max(alphaMax, px[i + 3]);
          if (px[i] < 126) cuts++;
        }
        stats[ch] = { lo, hi, alphaMin, alphaMax, cuts };
      }
      stats.manifest = JSON.parse(strFromU8(maps["material.json"]));
      return stats;
    }
    return {
      leaf: await bake("Broadleaf Green"),
      flatLeaf: await bake("Broadleaf Green", { veinRelief: 0, cellRelief: 0 }),
      grass: await bake("Meadow Grass Blades"),
      petal: await bake("Lily Petal"),
      fruit: await bake("Orange Peel"),
      cactus: await bake("Cactus Epidermis"),
      croc: await bake("Crocodile Belly Leather"),
      sparse: await bake("Scratches", { scratchDensity: 0.5, scratchBend: 0 }),
      dense: await bake("Scratches", { scratchDensity: 4, scratchBend: 0 }),
      curved: await bake("Scratches", { scratchDensity: 4, scratchBend: 0.7 }),
    };
  });
  for (const name of ["leaf", "grass", "petal", "fruit", "cactus", "croc"]) {
    for (const ch of [
      "base-color",
      "normal",
      "height",
      "roughness",
      "metalness",
      "emission",
    ]) {
      expect(result[name][ch].alphaMin).toBe(255);
      expect(result[name][ch].alphaMax).toBe(255);
    }
    expect(
      result[name]["base-color"].hi - result[name]["base-color"].lo,
      // Leather structure now comes from height, not painted dark joint lines.
    ).toBeGreaterThan(name === "croc" ? 1 : 4);
    expect(result[name].normal.hi - result[name].normal.lo).toBeGreaterThan(1);
  }
  expect(result.leaf.manifest.domain.projection).toContain("UV0");
  expect(result.flatLeaf.height.hi - result.flatLeaf.height.lo).toBe(0);
  expect(result.dense.height.cuts).toBeGreaterThan(
    result.sparse.height.cuts * 2,
  );
  expect(result.curved.height).not.toEqual(result.dense.height);
  expect(errors).toEqual([]);
});
