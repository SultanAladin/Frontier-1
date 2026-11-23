import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { unzipSync, strFromU8 } from "fflate";
import {
  materials,
  createMaterial,
  normalizeMaterial,
} from "../src/materials.js";
import { getRecipe } from "../src/materialProfiles.js";

const required = [
  "Terracotta Clay",
  "Beeswax",
  "Skin Deep",
  "PVC-U Rigid",
  "PP Polypropylene",
  "PBT Polyester",
  "PTFE Fluoropolymer",
  "PEEK Polymer",
  "Kraft Paper",
  "24K Gold",
  "18K Rose Gold",
  "18K White Gold",
  "Pure Copper",
  "Maraging Steel",
  "Cast Steel",
  "Pure Iron",
  "Rusted Iron",
  "Chromium",
  "Bronze Alloy",
  "Monocrystalline Solar",
  "Tour Golf Ball",
  "Bull Grain Saddle Leather",
  "Raw Selvedge Denim",
  "Cotton Jersey Knit",
  "LED Pixel Matrix",
];

test("growing catalogue preserves the original families and supports bake outputs", () => {
  expect(materials.length).toBeGreaterThanOrEqual(100);
  expect(new Set(materials.map((p) => p.id)).size).toBe(materials.length);
  for (const name of required)
    expect(materials.some((p) => p.name === name)).toBe(true);
  for (const p of materials) {
    const material = createMaterial({ ...p, bakeMode: 1 });
    expect(material.map).toBeNull();
    expect(material.normalMap).toBeNull();
    expect(material.roughnessMap).toBeNull();
    expect(material.userData.params.materialVersion).toBe(6);
    expect(material.userData.params.recipeId).toBe(getRecipe(p).id);
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    material.onBeforeCompile(shader);
    expect(shader.uniforms.uBakeMode.value).toBe(1);
    for (const uniform of Object.values(shader.uniforms))
      if (typeof uniform.value === "number")
        expect(Number.isFinite(uniform.value)).toBe(true);
    expect(shader.fragmentShader).toContain("if(uBakeMode==6)");
    material.dispose();
  }
  const pvc = materials.find((p) => p.name === "PVC-U Rigid");
  expect(getRecipe(pvc).controls.some((c) => c.id === "depth")).toBe(false);
  expect(
    getRecipe(
      materials.find((p) => p.name === "PC Polycarbonate"),
    ).controls.some((c) => c.id === "depth"),
  ).toBe(true);
  const denim = materials.find((p) => p.name === "Raw Selvedge Denim");
  expect(denim.detailScale).toBeGreaterThan(100);
  expect(
    getRecipe({ ...denim, weavePattern: "plain" }).controls.some(
      (c) => c.id === "denimFade",
    ),
  ).toBe(false);
});

test("incremental shader progress, new families and real six-channel ZIP baking", async ({
  page,
}) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/");
  await expect(
    page.getByRole("progressbar", { name: "Material preparation progress" }),
  ).toBeVisible();
  await expect
    .poll(() => page.locator(".material-preview img").count(), {
      timeout: 240000,
    })
    .toBe(materials.length);
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  for (const name of [
    "Beeswax",
    "Skin Deep",
    "PVC-U Rigid",
    "Kraft Paper",
    "Rusted Iron",
    "Cotton Jersey Knit",
    "Monocrystalline Solar",
    "LED Pixel Matrix",
    "Tour Golf Ball",
  ]) {
    await page
      .getByRole("button", { name: "Apply " + name, exact: true })
      .click();
    await expect(page.locator("canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
    await expect(page.locator("h1")).toHaveText(name);
  }
  await expect(page.getByLabel("Preview object")).toHaveValue("Sphere");
  await page.getByRole("button", { name: /Export material/ }).click();
  await page.getByRole("button", { name: /Bake procedural maps/ }).click();
  await page.getByLabel("Bake resolution").selectOption("256");
  await page.getByLabel("Bake patch width").fill("100");
  const downloading = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Bake & download ZIP", exact: true })
    .click();
  const stream = await (await downloading).createReadStream();
  const chunks = [];
  for await (const c of stream) chunks.push(c);
  const zip = unzipSync(Buffer.concat(chunks));
  expect(Object.keys(zip).sort()).toEqual(
    [
      "README.txt",
      "base-color.png",
      "emission.png",
      "height.png",
      "material.json",
      "metalness.png",
      "normal.png",
      "roughness.png",
    ].sort(),
  );
  const manifest = JSON.parse(strFromU8(zip["material.json"]));
  expect(manifest).toMatchObject({
    schema: "alloy.surface-bake.v1",
    resolution: 256,
    domain: { widthMM: 100, repeatable: false },
  });
  for (const name of [
    "base-color",
    "roughness",
    "metalness",
    "normal",
    "height",
    "emission",
  ]) {
    const bytes = Buffer.from(zip[name + ".png"]);
    expect(bytes.subarray(1, 4).toString()).toBe("PNG");
    expect(bytes.readUInt32BE(16)).toBe(256);
    expect(bytes.readUInt32BE(20)).toBe(256);
  }
  // Inspect actual encoded data, not just ZIP entries or nonempty files.
  const stats = await page.evaluate(
    async ({ height, normal, roughness, metalness }) => {
      async function samples(bytes) {
        const img = await createImageBitmap(
          new Blob([new Uint8Array(bytes)], { type: "image/png" }),
        );
        const c = document.createElement("canvas");
        c.width = c.height = 256;
        const ctx = c.getContext("2d");
        ctx.drawImage(img, 0, 0);
        img.close();
        const data = ctx.getImageData(0, 0, 256, 256).data;
        let min = 255,
          max = 0,
          sum = 0;
        for (let i = 0; i < data.length; i += 4) {
          min = Math.min(min, data[i]);
          max = Math.max(max, data[i]);
          sum += data[i];
        }
        return { min, max, mean: sum / (256 * 256) };
      }
      return {
        height: await samples(height),
        normal: await samples(normal),
        roughness: await samples(roughness),
        metalness: await samples(metalness),
      };
    },
    Object.fromEntries(
      ["height", "normal", "roughness", "metalness"].map((n) => [
        n,
        Array.from(zip[n + ".png"]),
      ]),
    ),
  );
  expect(stats.height.max - stats.height.min).toBeGreaterThan(20);
  expect(stats.normal.max - stats.normal.min).toBeGreaterThan(20);
  expect(stats.roughness.mean).toBeCloseTo(
    manifest.material.roughness * 255,
    0,
  );
  expect(stats.metalness.max).toBe(0);
  // Other difficult kernels bake through the exact same path; no input maps.
  const extra = await page.evaluate(async () => {
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { materials } = await import("/src/materials.js");
    const out = [];
    for (const type of [14, 16, 17, 19, 20]) {
      const result = await bakeMaterial(
        materials.find((p) => p.type === type),
        { resolution: 256 },
      );
      out.push({ type, size: result.bytes.length });
    }
    const controller = new AbortController();
    controller.abort();
    let cancelled = false;
    try {
      await bakeMaterial(materials[0], { signal: controller.signal });
    } catch (e) {
      cancelled = e.name === "AbortError";
    }
    return { out, cancelled };
  });
  expect(extra.cancelled).toBe(true);
  expect(extra.out).toHaveLength(5);
  for (const value of extra.out) expect(value.size).toBeGreaterThan(1000);
  expect(errors).toEqual([]);
});
