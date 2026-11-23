import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { heritagePatternCatalog } from "../src/heritagePatterns.js";
import { patternStarter, patternSVG } from "../src/patternDocument.js";

test.describe.configure({ timeout: 240000 });
test.use({ actionTimeout: 60000 });
function watchErrors(page) {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (
      m.type() === "error" ||
      /Invalid hook call|Multiple instances of Three/.test(m.text())
    )
      errors.push(m.text());
  });
  return errors;
}
async function open(page, id = "zellige-eight-point-stars") {
  await page.goto(`/?studio=pattern&material=natural-cotton&pattern=${id}`);
  await expect(page.getByLabel("Pattern name", { exact: true })).toBeVisible();
}
async function download(page, name, exact = true) {
  const waiting = page.waitForEvent("download", { timeout: 90000 });
  await page.getByRole("button", { name, exact }).click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return Buffer.concat(chunks).toString();
}
async function save(page) {
  return JSON.parse(await download(page, "Save document"));
}
async function ready(page) {
  const region = page.getByRole("region", {
    name: "Pattern material preview",
    exact: true,
  });
  await expect(
    region.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready", { timeout: 90000 });
  await expect(region.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
    { timeout: 90000 },
  );
  return region;
}

test("two ten-card collections, description/culture search, native thumbnails and no fictitious palette variants", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await open(page);
  const filter = page.getByLabel("Pattern collection", { exact: true });
  await expect(filter).toHaveValue("Islamic geometry");
  for (const group of ["Islamic geometry", "African traditional"]) {
    await filter.selectOption(group);
    await expect(page.locator(".pe-starters button")).toHaveCount(10);
    await expect(page.locator(".pe-library-count")).toContainText("10 results");
    await expect(
      page.getByLabel("Textile family colorway", { exact: true }),
    ).toHaveCount(0);
    await expect(
      page.getByRole("button", { name: "Next pattern page", exact: true }),
    ).toBeDisabled();
    await expect(page.locator(".pe-heritage-note")).toContainText(
      "not historical reproductions",
    );
    expect(
      await page
        .locator(".pe-starters img")
        .evaluateAll((imgs) =>
          imgs.every((img) => img.src.startsWith("data:image/svg+xml,")),
        ),
    ).toBe(true);
  }
  await page.getByLabel("Search patterns", { exact: true }).fill("Mali");
  await expect(page.locator(".pe-starters button")).toHaveCount(1);
  await expect(
    page.getByRole("button", {
      name: "Bamana Bogolanfini Dashes",
      exact: true,
    }),
  ).toBeVisible();
  await page.getByLabel("Search patterns", { exact: true }).fill("Ghana");
  await expect(page.locator(".pe-starters button")).toHaveCount(2);
  await page.getByLabel("Search patterns", { exact: true }).fill("");
  await filter.selectOption("All patterns");
  await expect(page.locator(".pe-library-count")).toContainText("103 results");
  await expect(page.locator(".pe-library-paging")).toContainText("Page 1 / 5");
  expect(errors).toEqual([]);
});

test("every new card loads actual editable geometry and saves exactly its native construction through the shared editor", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await open(page);
  for (const spec of heritagePatternCatalog) {
    await page
      .getByLabel("Pattern collection", { exact: true })
      .selectOption(spec.group);
    await page.getByRole("button", { name: spec.name, exact: true }).click();
    await expect(page.getByLabel("Pattern name", { exact: true })).toHaveValue(
      spec.name,
    );
    const doc = await save(page);
    expect(doc).toEqual(patternStarter(spec.name));
    await expect(
      page.getByLabel("Pattern design canvas", { exact: true }),
    ).toBeVisible();
    expect(
      doc.layers.every((l) => l.kind === "path" && l.paint && !l.locked),
    ).toBe(true);
  }
  expect(errors).toEqual([]);
});

test("Islamic and African vectors remain editable: colour, native width/rotation, undo/redo, SVG export and JSON reload", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await open(page, "kuba-raffia-steps");
  const before = await save(page);
  await page.getByLabel("Motif color", { exact: true }).fill("#735a99");
  let doc = await save(page);
  expect(doc.layers[0].color).toBe("#735a99");
  await page.getByTitle("Undo", { exact: true }).click();
  expect(await save(page)).toEqual(before);
  await page.getByTitle("Redo", { exact: true }).click();
  expect((await save(page)).layers[0].color).toBe("#735a99");
  await page.getByLabel("Width", { exact: true }).fill("490");
  await page.getByLabel("Angle", { exact: true }).fill("7");
  doc = await save(page);
  expect(doc.layers[0]).toMatchObject({
    width: 490,
    rotation: 7,
    color: "#735a99",
  });
  const svg = await download(page, "Export seamless SVG");
  expect(svg).toContain("#735a99");
  expect(svg).toContain("rotate(7)");
  expect(svg).not.toMatch(/<image|<script/);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "edited-raffia.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(doc)),
  });
  expect(await save(page)).toEqual(doc);
  await page
    .getByLabel("Pattern collection", { exact: true })
    .selectOption("Islamic geometry");
  await page
    .getByRole("button", { name: "Girih Decagon Rosettes", exact: true })
    .click();
  await page.locator("summary").filter({ hasText: "Design palette" }).click();
  await page.getByLabel("Palette color 2", { exact: true }).fill("#347f88");
  const recoloured = await save(page);
  expect(
    recoloured.layers.some(
      (l) => l.color === "#347f88" || l.stroke === "#347f88",
    ),
  ).toBe(true);
  expect(recoloured.construction).toBe("girih-decagon-rosettes");
  await page.screenshot({
    path: ".playwright/heritage-patterns/editor-girih.png",
    timeout: 90000,
  });
  expect(errors).toEqual([]);
});

test("both collections use the real 3D material preview, apply back to Material and export standalone factories with working new generators", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await open(page);
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  const region = await ready(page);
  await page
    .getByLabel("Pattern preview object", { exact: true })
    .selectOption("Panel");
  await ready(page);
  await page.screenshot({
    path: ".playwright/heritage-patterns/zellige-material.png",
    timeout: 90000,
  });
  await page
    .getByLabel("Pattern collection", { exact: true })
    .selectOption("African traditional");
  await page
    .getByRole("button", { name: "Asante Kente Blocks", exact: true })
    .click();
  await ready(page);
  await expect(
    page.getByLabel("Pattern base material", { exact: true }),
  ).toHaveValue("natural-cotton");
  await page.screenshot({
    path: ".playwright/heritage-patterns/kente-material.png",
    timeout: 90000,
  });
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await expect(page.locator(".pe-overlay")).toHaveCount(0);
  await page.getByRole("button", { name: /Export material/ }).click();
  const shader = await download(page, /Three.js procedural shader/, false);
  expect(shader).toContain("asante-kente-blocks");
  expect(shader).toContain("heritagePatternCatalog");
  const factory = shader
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return {material:createMaterial(preset), make:patternStarter, catalog:patternStarterCatalog};",
    )
    .replace(/export /g, "");
  const exported = new Function("THREE", factory)(THREE);
  expect(exported.material.userData.params.pattern).toEqual(
    patternStarter("Asante Kente Blocks"),
  );
  expect(exported.catalog).toHaveLength(103);
  for (const spec of heritagePatternCatalog)
    expect(exported.make(spec.name)).toEqual(patternStarter(spec.name));
  exported.material.dispose();
  expect(errors).toEqual([]);
});

test("all twenty actual SVGs rasterize with visible artwork, no external resources, and matching declared repeat boundaries", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await page.goto("/");
  const results = await page.evaluate(
    async (entries) => {
      const results = [];
      for (const e of entries) {
        const image = new Image();
        image.src =
          "data:image/svg+xml;base64," +
          btoa(unescape(encodeURIComponent(e.svg)));
        await image.decode();
        const canvas = document.createElement("canvas");
        canvas.width = canvas.height = 256;
        const ctx = canvas.getContext("2d", { willReadFrequently: true });
        ctx.drawImage(image, 0, 0, 256, 256);
        const pixels = ctx.getImageData(0, 0, 256, 256).data;
        const rgb = e.background.match(/\w\w/g).map((v) => parseInt(v, 16));
        let artwork = 0,
          opaque = 0,
          seamX = 0,
          seamY = 0;
        const diff = (a, b) =>
          Math.max(
            ...[0, 1, 2].map((c) => Math.abs(pixels[a + c] - pixels[b + c])),
          );
        for (let i = 0; i < pixels.length; i += 4) {
          if (pixels[i + 3] === 255) opaque++;
          if ([0, 1, 2].some((c) => Math.abs(pixels[i + c] - rgb[c]) > 12))
            artwork++;
        }
        for (let i = 0; i < 256; i++) {
          if (diff(i * 256 * 4, (i * 256 + 255) * 4) > 24) seamX++;
          if (diff(i * 4, (255 * 256 + i) * 4) > 24) seamY++;
        }
        results.push({
          name: e.name,
          artwork,
          opaque,
          seamX,
          seamY,
          tileAxes: e.tileAxes,
        });
      }
      return results;
    },
    heritagePatternCatalog.map((e) => {
      const d = patternStarter(e.name);
      return {
        name: e.name,
        svg: patternSVG(d),
        background: d.background.slice(1),
        tileAxes: d.tileAxes,
      };
    }),
  );
  for (const r of results) {
    expect(r.artwork, r.name).toBeGreaterThan(2000);
    expect(r.opaque, r.name).toBe(256 * 256);
    expect(r.seamX, r.name).toBeLessThan(32);
    if (r.tileAxes === "xy") expect(r.seamY, r.name).toBeLessThan(32);
  }
  expect(errors).toEqual([]);
});

test("new collection cards and editable canvas remain reachable on a 390px phone", async ({
  page,
}) => {
  const errors = watchErrors(page);
  await page.setViewportSize({ width: 390, height: 844 });
  await open(page, "yoruba-adire-rings");
  const filter = page.getByLabel("Pattern collection", { exact: true });
  await filter.selectOption("African traditional");
  await expect(page.locator(".pe-starters button")).toHaveCount(10);
  await page
    .getByRole("button", { name: "Ndebele Painted Chevrons", exact: true })
    .click();
  await expect(page.getByLabel("Pattern name", { exact: true })).toHaveValue(
    "Ndebele Painted Chevrons",
  );
  expect((await save(page)).construction).toBe("ndebele-painted-chevrons");
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
  await page.screenshot({
    path: ".playwright/heritage-patterns/phone-library.png",
    timeout: 90000,
  });
  const canvas = page.getByLabel("Pattern design canvas", { exact: true });
  await canvas.scrollIntoViewIfNeeded();
  await expect(canvas).toBeVisible();
  const box = await canvas.boundingBox();
  expect(box.x).toBeGreaterThanOrEqual(0);
  expect(box.x + box.width).toBeLessThanOrEqual(390);
  expect(box.width).toBeGreaterThan(300);
  expect(box.y).toBeLessThan(844);
  await page.screenshot({
    path: ".playwright/heritage-patterns/phone-canvas.png",
    timeout: 90000,
  });
  expect(errors).toEqual([]);
});
