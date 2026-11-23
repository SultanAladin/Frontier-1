import { test, expect } from "@playwright/test";
import { materials, createMaterial } from "../src/materials.js";
import { getRecipe } from "../src/materialProfiles.js";
import * as THREE from "three";

test("architectural families, isolated scratches and the pure-metal inspector", () => {
  for (const name of [
    "Corrugated Aluminium",
    "Porcelain Grid Tiles",
    "Zellige Ceramic Tiles",
    "Salt and Pepper Granite",
    "Rose Granite",
    "Carrara Marble",
    "Nero Marble",
    "Cast Concrete",
    "Board Form Concrete",
    "Lime Stucco",
    "Broadleaf Green",
    "Autumn Leaf",
    "Meadow Grass Blades",
    "Scratches",
  ])
    expect(materials.some((p) => p.name === name)).toBe(true);
  for (const p of materials.filter((p) => p.recipeId === "bareMetal")) {
    expect(getRecipe(p).controls.map((c) => c.label)).toEqual([
      "Metal polish",
      "Surface tooth",
    ]);
    expect(p.metalness).toBe(1);
    expect(p.coat).toBe(0);
  }
  expect(materials.filter((p) => getRecipe(p).id === "scratches")).toHaveLength(
    1,
  );
  for (const p of materials.filter((p) => p.type >= 20)) {
    const m = createMaterial({ ...p, bakeMode: 1 });
    const s = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    m.onBeforeCompile(s);
    expect(s.fragmentShader).toContain("#define uType " + p.type);
    expect(s.fragmentShader).toContain("surfaceCoverage");
    expect(m.map).toBeNull();
    m.dispose();
  }
});

test("finite scratch grooves, seed variation, LED contacts and full-surface foliage actually bake", async ({
  page,
}) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?material=scratches");
  await expect(page.locator("h1")).toHaveText("Scratches");
  await expect(page.getByLabel("Preview object")).toHaveValue("Panel");
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Apply Pure Aluminium", exact: true })
    .click();
  await expect(
    page.getByLabel("Tooling scale value", { exact: true }),
  ).toHaveCount(0);
  await expect(
    page.getByLabel("Surface tooth value", { exact: true }),
  ).toBeAttached();
  await page
    .getByRole("button", { name: "Apply Scratches", exact: true })
    .click();
  await expect(page.getByLabel("Preview object")).toHaveValue("Panel");
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const scratched = await page.locator("canvas").evaluate((c) => c.toDataURL());
  await page.getByLabel("Scratch density value", { exact: true }).fill("0");
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  expect(await page.locator("canvas").evaluate((c) => c.toDataURL())).not.toBe(
    scratched,
  );
  const data = await page.evaluate(async () => {
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { materials } = await import("/src/materials.js");
    async function bake(name, extra = {}) {
      const result = await bakeMaterial(
        { ...materials.find((p) => p.name === name), ...extra },
        { resolution: 256 },
      );
      return Array.from(result.bytes);
    }
    return {
      on: await bake("Scratches"),
      off: await bake("Scratches", { scratchDensity: 0 }),
      seed: await bake("Scratches", { surfaceSeed: 83 }),
      led: await bake("LED Pixel Matrix"),
      leaf: await bake("Broadleaf Green"),
      grass: await bake("Meadow Grass Blades"),
    };
  });
  const { unzipSync } = await import("fflate");
  const maps = Object.fromEntries(
    Object.entries(data).map(([key, bytes]) => [
      key,
      unzipSync(new Uint8Array(bytes)),
    ]),
  );
  const stats = await page.evaluate(
    async (packed) => {
      const results = {};
      for (const [name, bytes] of Object.entries(packed)) {
        const img = await createImageBitmap(
          new Blob([new Uint8Array(bytes)], { type: "image/png" }),
        );
        const c = document.createElement("canvas");
        c.width = img.width;
        c.height = img.height;
        const ctx = c.getContext("2d");
        ctx.drawImage(img, 0, 0);
        img.close();
        const pixels = ctx.getImageData(0, 0, c.width, c.height).data;
        let lo = 255,
          hi = 0,
          alphaLo = 255,
          alphaHi = 0,
          lowCount = 0;
        for (let i = 0; i < pixels.length; i += 4) {
          lo = Math.min(lo, pixels[i]);
          hi = Math.max(hi, pixels[i]);
          alphaLo = Math.min(alphaLo, pixels[i + 3]);
          alphaHi = Math.max(alphaHi, pixels[i + 3]);
          if (pixels[i] < 125) lowCount++;
        }
        results[name] = { lo, hi, alphaLo, alphaHi, lowCount };
      }
      return results;
    },
    {
      on: Array.from(maps.on["height.png"]),
      off: Array.from(maps.off["height.png"]),
      led: Array.from(maps.led["metalness.png"]),
      leaf: Array.from(maps.leaf["base-color.png"]),
      grass: Array.from(maps.grass["base-color.png"]),
    },
  );
  expect(stats.on.lo).toBeLessThan(124);
  expect(stats.on.lowCount).toBeGreaterThan(10);
  expect(stats.on.lowCount).toBeLessThan(256 * 256 * 0.3);
  expect(stats.off.hi - stats.off.lo).toBe(0);
  expect(stats.off.lo).toBe(128);
  expect(
    Buffer.from(maps.on["height.png"]).equals(
      Buffer.from(maps.seed["height.png"]),
    ),
  ).toBe(false);
  expect(stats.led.lo).toBe(0);
  expect(stats.led.hi).toBeGreaterThan(200);
  for (const name of ["leaf", "grass"]) {
    expect(stats[name].alphaLo).toBe(255);
    expect(stats[name].alphaHi).toBe(255);
  }
  // Distinct uniforms remain correct with shared program caching.
  await expect(page.locator(".material-preview img")).toHaveCount(
    materials.length,
    { timeout: 360000 },
  );
  const previews = await page
    .locator(".material-preview img")
    .evaluateAll((imgs) => imgs.map((img) => img.src));
  expect(new Set(previews).size).toBe(materials.length);
  expect(errors).toEqual([]);
});
