import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import * as THREE from "three";
import { rugDesignCatalog } from "../src/rugDesigns.js";
import {
  richRugPattern,
  rebuildRugComposition,
  normalizeRugComposition,
} from "../src/rugCompositions.js";
import {
  patternStarter,
  patternStarterCatalog,
  patternSVG,
  validatePattern,
  patternLayer,
} from "../src/patternDocument.js";
import { applyStitches } from "../src/patternStitches.js";
import { createMaterial, materials } from "../src/materials.js";
const signature = (d) =>
  createHash("sha256")
    .update(
      JSON.stringify(
        d.layers.map(({ path, x, y, width, height, rotation }) => ({
          path,
          x,
          y,
          width,
          height,
          rotation,
        })),
      ),
    )
    .digest("hex");

test("108 legacy blueprints: bounds, determinism, complete serialization", () => {
  expect(rugDesignCatalog).toHaveLength(108);
  expect(patternStarterCatalog).toHaveLength(103);
  for (const group of [
    "African compositions",
    "Islamic carpets",
    "Rug atelier",
  ])
    expect(rugDesignCatalog.filter((p) => p.group === group)).toHaveLength(36);
  const shapes = new Set();
  for (const p of rugDesignCatalog) {
    const raw = richRugPattern(p.name),
      d = patternStarter(p.name);
    expect(d.layers.length).toBeLessThanOrEqual(61);
    expect(d.layers.length).toBeGreaterThan(15);
    expect(
      d.layers.every(
        (l) =>
          l.kind === "path" &&
          l.finish ===
            (p.name === "Chromatic Diamond Tapestry" ? "cotton" : "wool") &&
          !!l.ornamentRole,
      ),
    ).toBe(true);
    expect(d.tileAxes).toBe("none");
    expect(d.presentation).toBe("rug");
    expect(d.layers.map((l) => l.path)).toEqual(raw.layers.map((l) => l.path));
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
    expect(signature(d)).toBe(signature(patternStarter(p.name)));
    shapes.add(signature(d));
    for (const borderWidth of [28, 76]) {
      const extreme = richRugPattern(p.name, { detail: 3, borderWidth });
      expect(extreme.layers.length).toBeLessThanOrEqual(61);
      expect(JSON.stringify(extreme).length).toBeLessThan(12000000);
      expect(
        extreme.layers.every(
          (l) => l.path.length < 100000 && !/NaN|Infinity/.test(l.path),
        ),
      ).toBe(true);
      expect(validatePattern(extreme).layers.map((l) => l.path)).toEqual(
        extreme.layers.map((l) => l.path),
      );
    }
  }
  expect(shapes.size).toBe(108);
  expect(patternStarter("African Diamond Carpet").layers).toEqual(
    patternStarter("Chromatic Diamond Tapestry").layers,
  );
  expect(patternStarter("Islamic Medallion Carpet").layers).toEqual(
    patternStarter("Azure Arabesque Medallion").layers,
  );
});

test("regeneration preserves palette and assignments, user motifs and stitches; finite texture axes", () => {
  let d = patternStarter("Saffron Rosette Court");
  d.name = "My ornate rug";
  d.layers[0].color = "#173a49";
  d.layers[0].finish = "cotton";
  d.layers[0].roughness = 0.75;
  d.layers.push(
    patternLayer("diamond", { name: "Hand placed marker", color: "#ee11aa" }),
  );
  d = validatePattern(
    applyStitches(d, { type: "Chain stitch", layout: "border", inset: 18 }),
  );
  const next = validatePattern(
    rebuildRugComposition(d, { detail: 3, borderWidth: 60 }),
  );
  expect(next.name).toBe(d.name);
  expect(next.layers[0]).toMatchObject({
    color: "#173a49",
    finish: "cotton",
    roughness: 0.75,
  });
  expect(next.layers.find((l) => l.name === "Hand placed marker")).toEqual(
    d.layers.find((l) => l.name === "Hand placed marker"),
  );
  expect(next.stitch).toEqual(d.stitch);
  expect(next.layers.filter((l) => l.stitchRole)).toHaveLength(3);
  expect(signature(next)).not.toBe(signature(d));
  const off = validatePattern(
    rebuildRugComposition(d, {
      field: false,
      medallions: false,
      corners: false,
      borderOrnaments: false,
    }),
  );
  expect(off.layers.length).toBeLessThan(next.layers.length);
  expect(
    normalizeRugComposition({ detail: 999, borderWidth: -1 }),
  ).toMatchObject({ detail: 3, borderWidth: 28 });
  const m = createMaterial({
    ...materials.find((m) => m.id === "natural-cotton"),
    pattern: next,
  });
  const shader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  m.onBeforeCompile(shader);
  for (const key of ["uPatternColor", "uPatternParams", "uPatternFinish"]) {
    expect(shader.uniforms[key].value.wrapS).toBe(THREE.ClampToEdgeWrapping);
    expect(shader.uniforms[key].value.wrapT).toBe(THREE.ClampToEdgeWrapping);
  }
  m.dispose();
  expect(validatePattern({ ...next, repeat: "mirror" }).repeat).toBe(
    "straight",
  );
  const svg = patternSVG(next);
  expect((svg.match(/<g opacity=/g) || []).length).toBe(next.layers.length);
});
async function download(page, name) {
  const wait = page.waitForEvent("download");
  await page.getByRole("button", { name, exact: true }).click();
  const stream = await (await wait).createReadStream(),
    chunks = [];
  for await (const c of stream) chunks.push(c);
  return Buffer.concat(chunks).toString();
}
async function ready(page) {
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await expect(
    page
      .getByRole("region", { name: "Pattern material preview" })
      .locator("canvas"),
  ).toHaveAttribute("data-material-ready", "true");
}

test("Retired saved composition: controls, undo, SVG/JSON and rug preview", async ({
  page,
}) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=chromatic-diamond-tapestry",
  );
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(patternStarter("Saffron Rosette Court")),
    ),
  });
  const before = JSON.parse(await download(page, "Save document"));
  await page.getByLabel("Rug detail level").selectOption("3");
  await page.getByLabel("Rug border width").fill("60");
  await page.getByLabel("Ornamental border").uncheck();
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.getByLabel("Ornamental border")).toBeChecked();
  const edited = JSON.parse(await download(page, "Save document"));
  expect(edited.ornament).toMatchObject({
    detail: 3,
    borderWidth: 60,
    borderOrnaments: true,
  });
  expect(signature(edited)).not.toBe(signature(before));
  const svg = await download(page, "Export rug SVG");
  expect(svg).toContain("<path");
  expect(svg).not.toContain("<image");
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "ornate-rug.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(edited)),
  });
  expect(JSON.parse(await download(page, "Save document"))).toEqual(edited);
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await ready(page);
  await expect(page.getByLabel("Pattern preview object")).toHaveValue("Rug");
  await page.getByLabel("Pattern preview zoom").fill("180");
  await ready(page);
  await page.locator(".pe-inspector").evaluate((e) => (e.scrollTop = 0));
  await page.screenshot({ path: ".playwright/final-ornate-islamic-rug.png" });
  await page.getByRole("button", { name: "Design tile", exact: true }).click();
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(patternStarter("Chromatic Diamond Tapestry")),
    ),
  });
  await page.locator(".pe-inspector").evaluate((e) => (e.scrollTop = 0));
  await page.screenshot({
    path: ".playwright/final-detailed-african-design.png",
  });
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await ready(page);
  await page.getByLabel("Pattern preview zoom").fill("180");
  await ready(page);
  await page.screenshot({ path: ".playwright/final-detailed-african-rug.png" });
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Rug",
  );
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  expect(errors).toEqual([]);
});

test("ornamental wool composition reaches height, normal and roughness bake channels", async ({
  page,
}) => {
  test.setTimeout(600000);
  await page.goto("/?material=natural-cotton&studio=pattern");
  const result = await page.evaluate(async () => {
    const { patternStarter } = await import("/src/patternDocument.js");
    const { materials } = await import("/src/materials.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { unzipSync } = await import("/node_modules/.vite/deps/fflate.js");
    const pattern = patternStarter("Ivory Palmette Garden");
    const baked = await bakeMaterial(
      { ...materials.find((m) => m.id === "natural-cotton"), pattern },
      { resolution: 256 },
    );
    const zip = unzipSync(baked.bytes),
      stats = {};
    for (const name of ["height", "normal", "roughness", "metalness"]) {
      const image = await createImageBitmap(
        new Blob([zip[name + ".png"]], { type: "image/png" }),
      );
      const c = document.createElement("canvas");
      c.width = c.height = 256;
      const ctx = c.getContext("2d");
      ctx.drawImage(image, 0, 0);
      const rgba = ctx.getImageData(0, 0, 256, 256).data;
      let min = 255,
        max = 0,
        sum = 0;
      for (let i = 0; i < rgba.length; i += 4) {
        min = Math.min(min, rgba[i]);
        max = Math.max(max, rgba[i]);
        sum += rgba[i];
      }
      stats[name] = { min, max, mean: sum / (256 * 256) };
      image.close();
    }
    return { stats, schema: baked.manifest.schema };
  });
  expect(result.schema).toBe("alloy.surface-bake.v1");
  expect(result.stats.height.max - result.stats.height.min).toBeGreaterThan(3);
  expect(result.stats.normal.max - result.stats.normal.min).toBeGreaterThan(30);
  expect(result.stats.roughness.mean).toBeGreaterThan(225);
  expect(result.stats.metalness.max).toBeLessThan(3);
});
