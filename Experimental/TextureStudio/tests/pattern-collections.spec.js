import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  collectionPatterns,
  collectionPattern,
  normalizeCollectionFade,
  collectionFadeAmount,
  rebuildCollectionFade,
} from "../src/patternCollections.js";
import {
  patternStarter,
  patternStarterNames,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import { createMaterial, materials } from "../src/materials.js";
import { createLeatherSwatchGeometry } from "../src/leatherGeometry.js";

test("all ten collections are bounded editable vectors, including maximum-density fades", () => {
  expect(patternStarterNames).toHaveLength(103);
  for (const p of collectionPatterns) {
    const raw = collectionPattern(p.name),
      doc = patternStarter(p.name);
    expect(doc.layers.length).toBeGreaterThan(0);
    expect(doc.layers.length).toBeLessThanOrEqual(64);
    expect(doc.collection).toBe(p.group);
    expect(
      doc.layers.every((l) => ["rect", "diamond", "path"].includes(l.kind)),
    ).toBe(true);
    expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    if (p.rug) expect(doc.presentation).toBe("rug");
    for (let i = 0; i < doc.layers.length; i++)
      expect(doc.layers[i].path).toBe(raw.layers[i].path || "");
  }
  for (const direction of ["down", "up", "left", "right"])
    for (const loop of [false, true]) {
      const raw = collectionPattern("Golden Cube Fade", {
        columns: 22,
        direction,
        loop,
      });
      const doc = validatePattern(raw);
      expect(
        doc.layers.every(
          (l, i) => l.path === raw.layers[i].path && l.path.length < 100000,
        ),
      ).toBe(true);
      expect(patternSVG(doc)).not.toContain("NaN");
      const a = createMaterial({
        ...materials.find((m) => m.id === "natural-cotton"),
        pattern: doc,
      });
      const shader = {
        uniforms: {},
        vertexShader: THREE.ShaderLib.physical.vertexShader,
        fragmentShader: THREE.ShaderLib.physical.fragmentShader,
      };
      a.onBeforeCompile(shader);
      for (const name of [
        "uPatternColor",
        "uPatternParams",
        "uPatternFinish",
      ]) {
        const t = shader.uniforms[name].value;
        expect(t.wrapS).toBe(
          doc.tileAxes === "y"
            ? THREE.ClampToEdgeWrapping
            : THREE.RepeatWrapping,
        );
        expect(t.wrapT).toBe(
          doc.tileAxes === "x"
            ? THREE.ClampToEdgeWrapping
            : THREE.RepeatWrapping,
        );
      }
      a.dispose();
      expect(validatePattern({ ...doc, repeat: "half-drop" }).repeat).toBe(
        loop ? "half-drop" : "straight",
      );
    }
  const rug = createLeatherSwatchGeometry({ rug: true }),
    hide = createLeatherSwatchGeometry();
  rug.computeBoundingBox();
  hide.computeBoundingBox();
  expect(rug.boundingBox.max.x - rug.boundingBox.min.x).toBeCloseTo(3.3);
  expect(hide.boundingBox.max.x - hide.boundingBox.min.x).toBeCloseTo(2.8);
  expect(
    [...rug.attributes.position.array, ...rug.attributes.normal.array].every(
      Number.isFinite,
    ),
  ).toBe(true);
  rug.dispose();
  hide.dispose();
});

test("fade geometry changes size, reverses direction, loops and preserves material assignments", () => {
  expect(normalizeCollectionFade(null).columns).toBe(12);
  const settings = normalizeCollectionFade({
    columns: 1000,
    strength: -2,
    start: 9,
    end: 0,
    minimum: 2,
    gap: 0,
  });
  expect(settings).toMatchObject({
    columns: 22,
    strength: 0.3,
    start: 0.9,
    minimum: 0.6,
    gap: 0.01,
  });
  expect(settings.end).toBeGreaterThan(settings.start);
  for (const direction of ["down", "up", "left", "right"]) {
    const f = { direction };
    const positive = ["down", "right"].includes(direction);
    const small = collectionFadeAmount(0.05, 0.05, f),
      large = collectionFadeAmount(0.95, 0.95, f);
    expect(positive ? large : small).toBeGreaterThan(positive ? small : large);
    expect(collectionFadeAmount(0.2, 0.2, { ...f, loop: true })).toBeCloseTo(
      collectionFadeAmount(0.8, 0.8, { ...f, loop: true }),
    );
  }
  const d = patternStarter("Ink Cube Fade");
  d.name = "Custom fade";
  d.layers[0].color = "#f22a40";
  d.layers[0].finish = "wool";
  d.layers[0].relief = 0.8;
  const next = validatePattern(
    rebuildCollectionFade(d, { direction: "left", columns: 22 }),
  );
  expect(next.name).toBe("Custom fade");
  expect(next.tileAxes).toBe("y");
  expect(next.layers[0]).toMatchObject({
    color: "#f22a40",
    finish: "wool",
    relief: 0.8,
    opacity: 1,
  });
  expect(next.layers[0].path).not.toBe(d.layers[0].path);
  expect(validatePattern(JSON.parse(JSON.stringify(next)))).toEqual(next);
});

async function saveDoc(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const stream = await (await waiting).createReadStream();
  const chunks = [];
  for await (const c of stream) chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
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

test("Legacy saved fade: controls, undo, border export and JSON reload", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=golden-cube-fade",
  );
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "fade.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(patternStarter("Golden Cube Fade"))),
  });
  await page.getByLabel("Motif color", { exact: true }).fill("#cf1234");
  await page.getByLabel("Fade direction").selectOption("left");
  await page.getByLabel("Motif density").fill("18");
  await page.getByLabel("Fade strength").fill("2.1");
  let d = await saveDoc(page);
  expect(d.fade).toMatchObject({
    direction: "left",
    columns: 18,
    strength: 2.1,
  });
  expect(d.tileAxes).toBe("y");
  expect(d.layers[0].color).toBe("#cf1234");
  await expect(page.getByLabel("Repeat layout")).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Export border SVG", exact: true }),
  ).toBeVisible();
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.getByLabel("Fade strength")).toHaveValue("1.3");
  await page.getByTitle("Redo", { exact: true }).click();
  await page.getByLabel("Loop fade seamlessly").check();
  d = await saveDoc(page);
  expect(d.tileAxes).toBe("xy");
  expect(d.fade.loop).toBe(true);
  await expect(page.getByLabel("Repeat layout")).toBeEnabled();
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "fade.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(d)),
  });
  expect(await saveDoc(page)).toEqual(d);
  await page.getByLabel("Loop fade seamlessly").uncheck();
  await page.getByLabel("Fade direction").selectOption("down");
  await page.screenshot({ path: ".playwright/final-fade-design.png" });
  expect(errors).toEqual([]);
});

test("Legacy saved rug live preview renders a wool surface and applies the dedicated rug mesh", async ({
  page,
}) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=african-diamond-carpet&view=3d",
  );
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(patternStarter("African Diamond Carpet")),
    ),
  });
  await ready(page);
  await expect(page.getByLabel("Pattern preview object")).toHaveValue("Rug");
  await page.getByLabel("Pattern preview zoom").fill("180");
  await ready(page);
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  await page.screenshot({ path: ".playwright/final-african-rug.png" });
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(patternStarter("Azure Arabesque Medallion")),
    ),
  });
  await ready(page);
  await page.screenshot({ path: ".playwright/final-islamic-rug.png" });
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Rug",
  );
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await expect(page.getByLabel("Pattern base material")).toHaveValue("current");
  expect(errors).toEqual([]);
});

test("rasterized fades have increasing coverage and loop seams; gold survives baking", async ({
  page,
}) => {
  test.setTimeout(600000);
  await page.goto("/?material=natural-cotton&studio=pattern");
  const data = await page.evaluate(async () => {
    const { patternStarter, patternSVG, validatePattern } =
      await import("/src/patternDocument.js");
    const { rebuildCollectionFade } =
      await import("/src/patternCollections.js");
    const { rasterPatternSVG } = await import("/src/patternRuntime.js");
    const { materials } = await import("/src/materials.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { unzipSync } = await import("/node_modules/.vite/deps/fflate.js");
    const down = patternStarter("Ink Cube Fade");
    const coverage = async (doc) => {
      const c = await rasterPatternSVG(patternSVG(doc), 512, 512),
        a = c.getContext("2d").getImageData(0, 0, 512, 512).data;
      const bands = Array(4).fill(0);
      for (let y = 0; y < 512; y++)
        for (let x = 0; x < 512; x++)
          if (a[(y * 512 + x) * 4] < 100) bands[Math.floor(y / 128)]++;
      return { a, bands };
    };
    const a = await coverage(down),
      b = await coverage(
        validatePattern(rebuildCollectionFade(down, { direction: "up" })),
      ),
      loop = await coverage(
        validatePattern(
          rebuildCollectionFade(down, { loop: true, minimum: 0.3 }),
        ),
      );
    let seam = 0;
    for (let i = 0; i < 512; i++)
      for (let k = 0; k < 3; k++)
        seam +=
          Math.abs(loop.a[i * 512 * 4 + k] - loop.a[(i * 512 + 511) * 4 + k]) +
          Math.abs(loop.a[i * 4 + k] - loop.a[(511 * 512 + i) * 4 + k]);
    const gold = patternStarter("Golden Cube Fade"),
      baked = await bakeMaterial(
        { ...materials.find((m) => m.id === "natural-cotton"), pattern: gold },
        { resolution: 256 },
      );
    const zip = unzipSync(baked.bytes);
    const im = await createImageBitmap(
        new Blob([zip["metalness.png"]], { type: "image/png" }),
      ),
      c = document.createElement("canvas");
    c.width = c.height = 256;
    c.getContext("2d").drawImage(im, 0, 0);
    const m = c.getContext("2d").getImageData(0, 0, 256, 256).data;
    let hi = 0;
    for (let i = 0; i < m.length; i += 4) if (m[i] > 240) hi++;
    im.close();
    return {
      down: a.bands,
      up: b.bands,
      seam: seam / (512 * 6),
      metalPixels: hi,
      schema: baked.manifest.schema,
    };
  });
  for (let i = 1; i < 4; i++) {
    expect(data.down[i]).toBeGreaterThan(data.down[i - 1]);
    expect(data.up[i]).toBeLessThan(data.up[i - 1]);
  }
  expect(data.seam).toBeLessThan(12);
  expect(data.metalPixels).toBeGreaterThan(1000);
  expect(data.schema).toBe("alloy.surface-bake.v1");
});
