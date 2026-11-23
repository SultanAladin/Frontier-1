import { weavePeriod } from "../src/patternWeaves.js";
import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import {
  patternStarter,
  patternStarterCatalog,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import {
  textileLibraryEntries,
  textilePattern,
  weaveDraft,
  weaveFamilies,
  replacePatternColor,
} from "../src/patternLibrary.js";
import {
  stitchTypes,
  stitchLayers,
  normalizeStitches,
  applyStitches,
} from "../src/patternStitches.js";
import { rebuildCollectionFade } from "../src/patternCollections.js";

test("Public families are bounded and palette options do not multiply constructions", () => {
  expect(patternStarterCatalog).toHaveLength(103);
  expect(new Set(patternStarterCatalog.map((p) => p.name)).size).toBe(103);
  expect(textileLibraryEntries).toHaveLength(132);
  const structures = new Map();
  for (const p of textileLibraryEntries) {
    const raw = textilePattern(p.name),
      d = patternStarter(p.name);
    expect(d.layers.length).toBeLessThanOrEqual(64);
    expect(d.layers.length).toBeGreaterThan(1);
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
    expect(d.library).toEqual({ family: p.family, palette: p.palette });
    for (let i = 0; i < d.layers.length; i++) {
      expect(d.layers[i].path.length).toBeLessThan(100000);
      expect(d.layers[i].path).toBe(raw.layers[i].path || "");
    }
    const signature = createHash("sha256")
      .update(
        JSON.stringify(
          d.layers.map(
            ({ path, kind, width, height, x, y, strokeWidth, fillRule }) => ({
              path,
              kind,
              width,
              height,
              x,
              y,
              strokeWidth,
              fillRule,
            }),
          ),
        ),
      )
      .digest("hex");
    if (structures.has(p.family))
      expect(signature).toBe(structures.get(p.family));
    else structures.set(p.family, signature);
  }
  expect(structures.size).toBe(48);
  expect(new Set(structures.values()).size).toBe(48);
  expect(
    textileLibraryEntries.filter((p) => p.group === "Fabric weaves"),
  ).toHaveLength(10);
  expect(
    textileLibraryEntries.filter((p) => p.group === "Stitch patterns"),
  ).toHaveLength(10);
  expect(
    textileLibraryEntries.filter((p) => p.group === "Geometric designs"),
  ).toHaveLength(64);
  expect(
    textileLibraryEntries.filter((p) => p.group === "Color patterns"),
  ).toHaveLength(48);
  for (const family of weaveFamilies) {
    const n = weavePeriod(family);
    for (let i = 0; i < n; i++) {
      expect(weaveDraft(family, i, 0)).toBe(weaveDraft(family, i, n));
      expect(weaveDraft(family, 0, i)).toBe(weaveDraft(family, n, i));
    }
  }
  expect(patternStarter("Gingham check - Indigo").layers.at(-1).name).toBe(
    "Gingham check color 2",
  );
  expect(patternSVG(patternStarter("Concentric rings - Indigo"))).toContain(
    'fill-rule="evenodd"',
  );
});

test("stitch overlays are bounded, preserve underlying edits and survive fade regeneration", () => {
  const base = patternStarter("Gingham check - Earth");
  base.layers[0].rotation = 12;
  for (const type of stitchTypes)
    for (const layout of ["rows", "columns", "border", "diagonal"]) {
      const raw = applyStitches(base, {
        type,
        layout,
        spacing: 16,
        width: 8,
        inset: 12,
        relief: 1,
      });
      const doc = validatePattern(raw);
      expect(doc.layers.slice(0, base.layers.length)).toEqual(base.layers);
      expect(doc.layers.filter((l) => l.stitchRole)).toHaveLength(3);
      expect(
        doc.layers.every(
          (l) => l.path.length < 100000 && !l.path.includes("NaN"),
        ),
      ).toBe(true);
      expect(validatePattern(applyStitches(doc, {}))).toEqual(doc);
      expect(
        validatePattern(applyStitches(doc, { enabled: false })).layers,
      ).toEqual(base.layers);
      expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    }
  expect(
    normalizeStitches({ spacing: 0, width: 100, relief: 100 }),
  ).toMatchObject({ spacing: 16, width: 8, relief: 1 });
  expect(normalizeStitches(null).type).toBe("Running stitch");
  expect(() => applyStitches(patternStarter("Graduated lattice"))).toThrow(
    "3 free layers",
  );
  const d = validatePattern(
    applyStitches(patternStarter("Ink Cube Fade"), {
      type: "Chain stitch",
      color: "#ffaa11",
    }),
  );
  const next = validatePattern(rebuildCollectionFade(d, { direction: "left" }));
  expect(next.stitch).toEqual(d.stitch);
  expect(next.layers.filter((l) => l.stitchRole)).toHaveLength(3);
  const recolored = replacePatternColor(base, base.layers[0].color, "#11aaff");
  expect(recolored.layers[0].rotation).toBe(12);
  expect(recolored.layers[0].color).toBe("#11aaff");
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
}

test("search, paging, palette edits, stitch overlays, undo and portable saved documents", async ({
  page,
}) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?material=natural-cotton&studio=pattern");
  await expect(page.locator(".pe-library-count")).toContainText(
    "one card per family",
  );
  await expect(page.locator(".pe-starters button")).toHaveCount(7);
  await page.getByLabel("Pattern collection").selectOption("Fabric weaves");
  await expect(page.locator(".pe-library-count")).toContainText("10 results");
  await expect(
    page.getByRole("button", { name: "Next pattern page" }),
  ).toBeDisabled();
  await expect(page.locator(".pe-library-paging")).toContainText("Page 1 / 1");
  await expect(page.getByLabel("Textile family colorway")).toHaveCount(0);
  await expect(page.locator(".pe-starters button")).toHaveCount(10);
  await page.getByLabel("Search patterns").fill("herringbone");
  await expect(page.locator(".pe-starters button")).toHaveCount(1);
  await page
    .getByRole("button", { name: "Herringbone weave", exact: true })
    .click();
  await page.locator("summary").filter({ hasText: "Design palette" }).click();
  await page.getByLabel("Palette color 1", { exact: true }).fill("#172c47");
  await page.locator("summary").filter({ hasText: "Stitch overlay" }).click();
  await page.getByLabel("Enable stitch overlay").check();
  await page.getByLabel("Stitch construction").selectOption("Chain stitch");
  await page.getByLabel("Stitch placement").selectOption("border");
  await page.getByLabel("Stitch thread color").fill("#e2ab69");
  await page.getByLabel("Stitch spacing").fill("28");
  await page.getByLabel("Thread width", { exact: true }).fill("4");
  await page.getByLabel("Stitch relief (mm)").fill("0.8");
  const doc = JSON.parse(await download(page, "Save document"));
  expect(doc.stitch).toMatchObject({
    enabled: true,
    type: "Chain stitch",
    layout: "border",
    color: "#e2ab69",
    spacing: 28,
    width: 4,
    relief: 0.8,
  });
  expect(doc.layers).toHaveLength(10);
  expect(doc.layers[0].color).toBe("#172c47");
  const svg = await download(page, "Export seamless SVG");
  expect(svg).toContain("#e2ab69");
  expect(svg).toContain('stroke-linecap="round"');
  await page.getByLabel("Enable stitch overlay").uncheck();
  expect(JSON.parse(await download(page, "Save document")).layers).toHaveLength(
    7,
  );
  await page.getByTitle("Undo", { exact: true }).click();
  expect(JSON.parse(await download(page, "Save document"))).toEqual(doc);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "stitched-weave.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(doc)),
  });
  expect(JSON.parse(await download(page, "Save document"))).toEqual(doc);
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await ready(page);
  await page.getByLabel("Pattern preview object").selectOption("Panel");
  await ready(page);
  await page.getByLabel("Pattern preview zoom").fill("200");
  await ready(page);
  await page.screenshot({ path: ".playwright/final-stitched-weave.png" });
  await page.getByRole("button", { name: "Design tile", exact: true }).click();
  await page.getByLabel("Search patterns").fill("");
  await page.getByLabel("Pattern collection").selectOption("Stitch patterns");
  await page.screenshot({ path: ".playwright/final-stitch-library.png" });
  await page.getByLabel("Search patterns").fill("not a real pattern");
  await expect(page.getByText("No matches.", { exact: false })).toBeVisible();
  expect(errors).toEqual([]);
});

test("stitched fabrics produce different height and normal maps, not only printed color", async ({
  page,
}) => {
  test.setTimeout(600000);
  await page.goto("/?material=natural-cotton&studio=pattern");
  const data = await page.evaluate(async () => {
    const { patternStarter, validatePattern } =
      await import("/src/patternDocument.js");
    const { applyStitches } = await import("/src/patternStitches.js");
    const { materials } = await import("/src/materials.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js");
    const { unzipSync } = await import("/node_modules/.vite/deps/fflate.js");
    const base = patternStarter("Plain weave - Indigo"),
      stitched = validatePattern(
        applyStitches(base, {
          type: "Cross stitch",
          spacing: 32,
          width: 6,
          relief: 0.85,
        }),
      );
    const arrays = [];
    for (const pattern of [base, stitched]) {
      const b = await bakeMaterial(
        { ...materials.find((m) => m.id === "natural-cotton"), pattern },
        { resolution: 256 },
      );
      const z = unzipSync(b.bytes),
        maps = {};
      for (const name of ["height", "normal"]) {
        const image = await createImageBitmap(
          new Blob([z[name + ".png"]], { type: "image/png" }),
        );
        const c = document.createElement("canvas");
        c.width = c.height = 256;
        const ctx = c.getContext("2d");
        ctx.drawImage(image, 0, 0);
        maps[name] = ctx.getImageData(0, 0, 256, 256).data;
        image.close();
      }
      arrays.push(maps);
    }
    const diffs = {};
    for (const name of ["height", "normal"]) {
      let count = 0,
        max = 0;
      for (let i = 0; i < arrays[0][name].length; i += 4) {
        const diff = Math.abs(arrays[0][name][i] - arrays[1][name][i]);
        if (diff > 4) count++;
        max = Math.max(max, diff);
      }
      diffs[name] = { count, max };
    }
    return diffs;
  });
  expect(data.height.count).toBeGreaterThan(200);
  expect(data.height.max).toBeGreaterThan(10);
  expect(data.normal.count).toBeGreaterThan(200);
});
