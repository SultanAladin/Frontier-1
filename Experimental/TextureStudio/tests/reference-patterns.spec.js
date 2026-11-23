import { referenceDesignCatalog } from "../src/referencePatterns.js";
import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  patternStarter,
  patternStarterCatalog,
  patternInventory,
  validatePattern,
  patternSVG,
  resolvePatternStarterName,
} from "../src/patternDocument.js";
import { createMaterial, materials } from "../src/materials.js";
import { createLeatherSwatchGeometry } from "../src/leatherGeometry.js";

const references = [
  { name: "Chromatic Diamond Tapestry" },
  ...referenceDesignCatalog,
];
test("catalog audit: one ombre, no palette or compatibility-alias cards; no primitive/colorway padding", () => {
  expect(references).toHaveLength(4);
  expect(patternInventory).toEqual({
    catalogEntries: 102,
    stitches: 10,
    weaves: 10,
  });
  expect(
    patternStarterCatalog.filter((p) => p.name.startsWith("Ombre")),
  ).toHaveLength(1);
  expect(
    patternStarterCatalog.filter((p) =>
      / - (Indigo|Earth|Studio|Mulberry)$/.test(p.name),
    ),
  ).toHaveLength(0);
  expect(
    patternStarterCatalog.some((p) => p.name === "African Diamond Carpet"),
  ).toBe(false);
  expect(patternStarterCatalog.find((p) => p.name === "Polka dots").group).toBe(
    "Geometric designs",
  );
  for (const palette of ["Indigo", "Earth", "Studio", "Mulberry"]) {
    const d = patternStarter("Ombre bands", palette);
    expect(d.library).toEqual({ family: "Ombre bands", palette });
    expect(d.layers.map((l) => l.path)).toEqual(
      patternStarter("Ombre bands").layers.map((l) => l.path),
    );
    const legacy = "Ombre bands - " + palette;
    expect(
      resolvePatternStarterName(legacy.toLowerCase().replaceAll(" ", "-")),
    ).toBeUndefined();
    expect(patternStarter(legacy)).toEqual(d);
  }
  expect(resolvePatternStarterName("african-diamond-carpet")).toBeUndefined();
});

test("reference documents: complete portable paths, physical aspect, bead metadata and guarded shader", () => {
  for (const entry of references) {
    const d = patternStarter(entry.name);
    expect(d.layers.length).toBeLessThanOrEqual(61);
    expect(
      d.layers.every(
        (l) =>
          l.kind === "path" &&
          l.path.length < 100000 &&
          !/NaN|Infinity/.test(l.path),
      ),
    ).toBe(true);
    expect(JSON.stringify(d).length).toBeLessThan(12000000);
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
    expect(d.tileAxes).toBe("none");
    const svg = patternSVG(d);
    expect(svg).not.toContain("<image");
    expect(svg).toContain('preserveAspectRatio="none"');
    expect((svg.match(/<g opacity=/g) || []).length).toBe(d.layers.length);
  }
  const beads = patternStarter("Beaded Diamond Weave");
  expect(beads.beadwork).toEqual({ columns: 84, rows: 64, height: 0.004 });
  expect(
    beads.layers.reduce((n, l) => n + (l.path.match(/M/g) || []).length, 0),
  ).toBeGreaterThan(20000);
  const material = createMaterial({
    ...materials.find((m) => m.id === "natural-cotton"),
    pattern: beads,
  });
  const shader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  material.onBeforeCompile(shader);
  expect(shader.uniforms.uPatternBeads.value.toArray()).toEqual([
    84, 64, 0.004,
  ]);
  expect(shader.fragmentShader).toContain("beadRound");
  material.dispose();
  const aspect = patternStarter("Chromatic Diamond Tapestry").designAspect;
  const mesh = createLeatherSwatchGeometry({ rug: true, rugAspect: aspect });
  mesh.computeBoundingBox();
  expect(
    (mesh.boundingBox.max.x - mesh.boundingBox.min.x) /
      (mesh.boundingBox.max.y - mesh.boundingBox.min.y),
  ).toBeCloseTo(aspect, 4);
  mesh.dispose();
});

async function save(page) {
  const promise = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await promise).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks));
}

test("Retired saved documents still load with SVG aspect, bead 3D and save/reload", async ({
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
      JSON.stringify(patternStarter("Chromatic Diamond Tapestry")),
    ),
  });
  const art = await page.getByLabel("Pattern design canvas").boundingBox();
  expect(art.width / art.height).toBeCloseTo(640 / 360, 1);
  await page.screenshot({ path: ".playwright/reference-v75/woven-studio.png" });
  for (const name of [
    "Turquoise Faceted Vault",
    "Crimson Star and Cross Carpet",
    "Beaded Diamond Weave",
  ]) {
    await page.locator('input[accept=".json"]').setInputFiles({
      name: "legacy.json",
      mimeType: "application/json",
      buffer: Buffer.from(JSON.stringify(patternStarter(name))),
    });
    const d = await save(page);
    expect(d).toEqual(patternStarter(name));
    await page.screenshot({
      path: ".playwright/reference-v75/" + name + "-studio.png",
    });
  }
  const before = await save(page);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "beads.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(before)),
  });
  expect(await save(page)).toEqual(before);
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByLabel("Pattern preview zoom").fill("150");
  await page.screenshot({ path: ".playwright/reference-v75/beads-live.png" });
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  expect(errors).toEqual([]);
});

test("bead relief and gap roughness reach the shared bake channels", async ({
  page,
}) => {
  test.setTimeout(600000);
  await page.goto("/?material=natural-cotton&studio=pattern");
  const result = await page.evaluate(async () => {
    const { patternStarter } = await import("/src/patternDocument.js");
    const { materials } = await import("/src/materials.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { unzipSync } = await import("/node_modules/.vite/deps/fflate.js");
    const pattern = patternStarter("Beaded Diamond Weave");
    const baked = await bakeMaterial(
      { ...materials.find((m) => m.id === "natural-cotton"), pattern },
      { resolution: 512 },
    );
    const zip = unzipSync(baked.bytes),
      stats = {};
    for (const name of ["height", "normal", "roughness", "metalness"]) {
      const image = await createImageBitmap(
        new Blob([zip[name + ".png"]], { type: "image/png" }),
      );
      const canvas = document.createElement("canvas");
      canvas.width = canvas.height = 512;
      const ctx = canvas.getContext("2d");
      ctx.drawImage(image, 0, 0);
      const rgba = ctx.getImageData(0, 0, 512, 512).data;
      let min = 255,
        max = 0;
      for (let i = 0; i < rgba.length; i += 4) {
        min = Math.min(min, rgba[i]);
        max = Math.max(max, rgba[i]);
      }
      stats[name] = { min, max };
      image.close();
    }
    return stats;
  });
  expect(result.height.max - result.height.min).toBeGreaterThan(4);
  expect(result.normal.max - result.normal.min).toBeGreaterThan(30);
  expect(result.roughness.max - result.roughness.min).toBeGreaterThan(40);
  expect(result.metalness.max).toBeLessThan(3);
});
