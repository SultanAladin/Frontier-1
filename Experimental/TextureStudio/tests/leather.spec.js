import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { materials, createMaterial } from "../src/materials.js";
import { createLeatherSwatchGeometry } from "../src/leatherGeometry.js";
import { botanicalColor } from "../src/botanicalKernels.js";

test("leather is continuous relief with matching finish normals, and a closed swatch preview", () => {
  const g = createLeatherSwatchGeometry(),
    p = g.getAttribute("position"),
    uv = g.getAttribute("uv");
  expect(p.count).toBe(uv.count);
  expect(
    Array.from(g.getAttribute("normal").array).every(Number.isFinite),
  ).toBe(true);
  const a = new THREE.Vector3(),
    b = new THREE.Vector3(),
    c = new THREE.Vector3();
  let volume = 0;
  for (let i = 0; i < g.index.count; i += 3) {
    a.fromBufferAttribute(p, g.index.array[i]);
    b.fromBufferAttribute(p, g.index.array[i + 1]);
    c.fromBufferAttribute(p, g.index.array[i + 2]);
    volume += a.dot(b.cross(c)) / 6;
  }
  expect(volume).toBeCloseTo(2.8 * 2.1 * 0.035, 4);
  g.dispose();
  const croc = botanicalColor().split("if(uType==31")[0];
  expect(croc).not.toContain("boxMask");
  expect(croc).not.toContain("scute");
  expect(croc).toContain("vectorHide");
  expect(croc).toContain("source-.74");
  for (const preset of materials.filter((p) => p.category === "Leather")) {
    const m = createMaterial(preset),
      shader = {
        uniforms: {},
        vertexShader: THREE.ShaderLib.physical.vertexShader,
        fragmentShader: THREE.ShaderLib.physical.fragmentShader,
      };
    m.onBeforeCompile(shader);
    expect(m.map).toBeNull();
    expect(m.normalMap).toBeNull();
    expect(shader.fragmentShader).toContain("(uType==11 || uType==20");
    expect(shader.fragmentShader).toContain(
      "material.clearcoatRoughness+leatherFold",
    );
    m.dispose();
  }
});

test("all leather finishes render on the swatch and flat panel; actual maps carry relief", async ({
  page,
}, testInfo) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?material=crocodile-belly-leather");
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
  for (const material of materials.filter((p) => p.category === "Leather")) {
    await page
      .getByRole("button", { name: "Apply " + material.name, exact: true })
      .click();
    await expect(page.getByLabel("Preview object")).toHaveValue(
      "Leather swatch",
    );
    await frame();
    const bent = await page.locator("canvas").evaluate((c) => c.toDataURL());
    await page.getByLabel("Preview object").selectOption("Panel");
    await frame();
    expect(
      await page.locator("canvas").evaluate((c) => c.toDataURL()),
    ).not.toBe(bent);
  }
  const maps = await page.evaluate(async () => {
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { materials } = await import("/src/materials.js");
    const { unzipSync } = await import("/node_modules/.vite/deps/fflate.js");
    const result = {};
    for (const id of ["cognac-leather", "crocodile-belly-leather"]) {
      const { bytes, manifest } = await bakeMaterial(
        materials.find((p) => p.id === id),
        { resolution: 512 },
      );
      const z = unzipSync(bytes);
      const stats = {};
      for (const channel of ["base-color", "normal", "height", "roughness"]) {
        const image = await createImageBitmap(
          new Blob([z[channel + ".png"]], { type: "image/png" }),
        );
        const canvas = document.createElement("canvas");
        canvas.width = 512;
        canvas.height = 512;
        const ctx = canvas.getContext("2d");
        ctx.drawImage(image, 0, 0);
        image.close();
        const rgba = ctx.getImageData(0, 0, 512, 512).data;
        let lo = 255,
          hi = 0,
          alpha = 255;
        for (let i = 0; i < rgba.length; i += 4) {
          lo = Math.min(lo, rgba[i]);
          hi = Math.max(hi, rgba[i]);
          alpha = Math.min(alpha, rgba[i + 3]);
        }
        stats[channel] = { lo, hi, alpha };
      }
      result[id] = {
        stats,
        range: manifest.channels["height.png"].rangeSceneUnits,
      };
    }
    return result;
  });
  for (const data of Object.values(maps)) {
    expect(data.range).toBe(0.012);
    for (const channel of Object.values(data.stats)) {
      expect(channel.alpha).toBe(255);
      expect(channel.hi - channel.lo).toBeGreaterThan(1);
    }
    expect(data.stats.height.lo).toBeGreaterThan(0);
    expect(data.stats.height.hi).toBeLessThan(255);
  }
  await expect(page.locator(".material-preview img")).toHaveCount(
    materials.length,
    { timeout: 360000 },
  );
  const thumbs = await page
    .locator(".material-preview img")
    .evaluateAll((images) => images.map((i) => i.src));
  expect(new Set(thumbs).size).toBe(materials.length);
  if (process.env.ALLOY_CAPTURE) {
    for (const id of ["crocodile-belly-leather", "cognac-leather"]) {
      const p = materials.find((p) => p.id === id);
      await page
        .getByRole("button", { name: "Apply " + p.name, exact: true })
        .click();
      await page.getByRole("button", { name: "Fit", exact: true }).click();
      await page.getByLabel("Viewport zoom").fill("2.32");
      await frame();
      await page.screenshot({ path: testInfo.outputPath(id + "-detail.png") });
      await page.getByRole("button", { name: "Macro", exact: true }).click();
      await frame();
      await page.screenshot({ path: testInfo.outputPath(id + "-macro.png") });
      await page.getByLabel("Preview object").selectOption("Panel");
      await frame();
      await page.screenshot({ path: testInfo.outputPath(id + "-flat.png") });
    }
  }
  expect(errors).toEqual([]);
});
