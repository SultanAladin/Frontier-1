import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { PAINT_TOOLS } from "../src/paintToolCatalogue.js";
import { PAINT_CHANNELS } from "../src/paintChannelModel.js";
import { TEXTURE_DRAFT_KEY } from "../src/textureDocument.js";

test.describe.configure({ timeout: 150000 });
function errorsOf(page) {
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
async function open(page) {
  await page.goto("/?studio=texture");
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
}
async function downloaded(page, button) {
  const waiting = page.waitForEvent("download");
  await button.click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return Buffer.concat(chunks).toString();
}
async function project(page) {
  return JSON.parse(
    await downloaded(
      page,
      page.getByRole("button", { name: "Save project", exact: true }),
    ),
  );
}
async function draft(page) {
  return page.evaluate(
    (key) => JSON.parse(localStorage.getItem(key)),
    TEXTURE_DRAFT_KEY,
  );
}
const browserOf = (page) =>
  page.getByRole("region", { name: "Content browser", exact: true });

test("reference channel card enables all 14 targets, enforces Height/Normal and retains disabled source values", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page.getByLabel("Roughness paint value", { exact: true }).fill("0");
  await page
    .getByRole("button", { name: "Add painting channel", exact: true })
    .click();
  for (const channel of PAINT_CHANNELS)
    await page
      .getByRole("checkbox", { name: channel.label + " channel", exact: true })
      .check();
  let saved = await project(page);
  expect(saved.layers.at(-1).channels).toHaveLength(14);
  await page
    .getByRole("checkbox", { name: "Height channel", exact: true })
    .uncheck();
  await expect(
    page.getByRole("checkbox", { name: "Normal channel", exact: true }),
  ).not.toBeChecked();
  await page
    .getByRole("checkbox", { name: "Normal channel", exact: true })
    .check();
  await expect(
    page.getByRole("checkbox", { name: "Height channel", exact: true }),
  ).toBeChecked();
  await page.getByRole("button", { name: "Clear all", exact: true }).click();
  await expect(
    page.getByText(
      "No painting channels enabled. Existing channel values are retained.",
    ),
  ).toBeVisible();
  saved = await project(page);
  expect(saved.layers.at(-1).channels).toEqual([]);
  expect(saved.layers.at(-1).channelSettings.roughness.value).toBe(0);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect((await project(page)).layers.at(-1).channels).toHaveLength(14);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  expect((await project(page)).layers.at(-1).channels).toEqual([]);
  await page.reload();
  await expect(
    page.getByText(
      "No painting channels enabled. Existing channel values are retained.",
    ),
  ).toBeVisible();
  expect(errors).toEqual([]);
});

test("all instrument families browse safely, tool properties persist with undo, focus trap and context-menu access", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const opener = page.getByRole("button", {
    name: "Open paint tool menu",
    exact: true,
  });
  await opener.click();
  await page
    .getByRole("button", {
      name: "Instrument catalogue configuration only",
      exact: true,
    })
    .click();
  const menu = page.getByRole("dialog", {
    name: "Paint tool menu",
    exact: true,
  });
  await expect(
    menu
      .getByRole("navigation", { name: "Paint tool families" })
      .getByRole("button"),
  ).toHaveCount(10);
  await menu.getByRole("button", { name: /^Brushes/ }).click();
  await expect(
    menu.getByRole("button", { name: /^Select paint tool/ }),
  ).toHaveCount(23);
  const tool = PAINT_TOOLS.find((t) => t.key === "brush");
  await menu
    .getByRole("button", {
      name: `Select paint tool ${tool.label}`,
      exact: true,
    })
    .click();
  await menu.getByLabel("Paint Opacity value", { exact: true }).fill("0");
  await menu.getByLabel("Paint tool color", { exact: true }).fill("#123456");
  await expect(
    menu.getByText("Representative mark only · not a paint-engine stroke"),
  ).toBeVisible();
  const last = menu
    .locator("button:not(:disabled),input:not(:disabled),select:not(:disabled)")
    .last();
  await last.focus();
  await page.keyboard.press("Tab");
  expect(await menu.evaluate((el) => el.contains(document.activeElement))).toBe(
    true,
  );
  const menuSave = JSON.parse(
    await downloaded(page, { click: () => page.keyboard.press("Control+s") }),
  );
  expect(menuSave.painting.params.opacity).toBe(0);
  expect(menuSave.painting.color).toBe("#123456");
  await page.screenshot({ path: ".playwright/paint-tool-properties.png" });
  await page.keyboard.press("Escape");
  await expect(
    menu.getByLabel("Search paint instruments", { exact: true }),
  ).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(menu).toHaveCount(0);
  await expect(opener).toBeFocused();
  let saved = await project(page);
  expect(saved.painting.toolId).toBe(tool.id);
  expect(saved.painting.params.opacity).toBe(0);
  expect(saved.painting.color).toBe("#123456");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  saved = await project(page);
  expect(saved.painting.color).not.toBe("#123456");
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await project(page);
  await page.reload();
  await opener.click();
  await page
    .getByRole("button", {
      name: "Instrument catalogue configuration only",
      exact: true,
    })
    .click();
  await menu
    .getByRole("button", {
      name: `Select paint tool ${tool.label}`,
      exact: true,
    })
    .click();
  await expect(
    menu.getByLabel("Paint Opacity value", { exact: true }),
  ).toHaveValue("0");
  await expect(
    menu.getByLabel("Paint tool color", { exact: true }),
  ).toHaveValue("#123456");
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  await page.locator(".tp-scene-viewport canvas").click({ button: "right" });
  await page
    .getByRole("button", {
      name: "Instrument catalogue configuration only",
      exact: true,
    })
    .click();
  await expect(menu).toBeVisible();
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  expect(errors).toEqual([]);
});

test("draggable content browser shares full material controls, preview choices, preset service and portable exports", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const originalScene = await page.locator(".tp-scene-viewport").boundingBox();
  const browser = browserOf(page),
    divider = page.getByRole("separator", {
      name: "Resize content browser",
      exact: true,
    }),
    box = await divider.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2, box.y - 530, { steps: 10 });
  await page.mouse.up();
  await expect(browser).toHaveAttribute("data-expanded", "true");
  await browser
    .getByLabel("Search content assets", { exact: true })
    .fill("Layered Sand");
  await browser
    .getByRole("button", { name: "Inspect asset Layered Sand", exact: true })
    .click();
  const left = await browser
      .getByRole("complementary", { name: "Asset outliner", exact: true })
      .boundingBox(),
    grid = await browser
      .getByRole("region", { name: "Asset grid", exact: true })
      .boundingBox(),
    right = await browser
      .getByRole("region", { name: "Asset preview and inspector", exact: true })
      .boundingBox();
  expect(left.x + left.width).toBeLessThanOrEqual(grid.x);
  expect(grid.x + grid.width).toBeLessThanOrEqual(right.x);
  expect(right.x + right.width).toBeLessThanOrEqual(1440);
  const sceneBounds = await page.locator(".tp-scene-viewport").boundingBox(),
    canvasBounds = await page
      .locator(".tp-scene-viewport canvas")
      .boundingBox();
  expect(sceneBounds).toEqual(originalScene);
  expect(canvasBounds.y).toBeCloseTo(sceneBounds.y, 0);
  expect(canvasBounds.height).toBeCloseTo(sceneBounds.height, 0);
  await expect
    .poll(() =>
      page.locator(".tp-scene-viewport canvas").evaluate((c) => {
        const sample = document.createElement("canvas");
        sample.width = 32;
        sample.height = 32;
        const ctx = sample.getContext("2d");
        ctx.drawImage(c, 0, 0, 32, 32);
        const data = ctx.getImageData(0, 0, 32, 32).data;
        let opaque = 0;
        for (let i = 3; i < data.length; i += 4) if (data[i] > 0) opaque++;
        return opaque;
      }),
    )
    .toBeGreaterThan(20);
  const canvas = browser.locator(".cb-material-preview canvas");
  await expect(canvas).toHaveAttribute("data-material-ready", "true");
  await expect(canvas).toHaveAttribute("data-preview-shape", "Panel");
  expect(Number(await canvas.getAttribute("height"))).toBeGreaterThan(100);
  await browser.getByLabel("Bed grain size value", { exact: true }).fill("1.4");
  await browser
    .getByLabel("Asset preview object", { exact: true })
    .selectOption("Teapot");
  await expect(canvas).toHaveAttribute("data-preview-shape", "Teapot");
  await browser
    .getByLabel("Asset preview lighting", { exact: true })
    .selectOption("Daylight");
  await browser
    .getByRole("button", { name: "Asset wireframe", exact: true })
    .click();
  await expect(canvas).toHaveAttribute("data-wireframe", "true");
  await browser
    .getByRole("button", { name: "Asset wireframe", exact: true })
    .click();
  await browser.getByRole("button", { name: /Save as preset/ }).click();
  const dialog = page.getByRole("dialog", {
    name: "Save asset material preset",
    exact: true,
  });
  await dialog
    .getByLabel("Asset material preset name", { exact: true })
    .fill("Browser Sand Study");
  await dialog
    .getByRole("button", { name: "Save to shared library", exact: true })
    .click();
  await expect(dialog).toHaveCount(0);
  const json = JSON.parse(
    await downloaded(
      page,
      browser.getByRole("button", { name: "JSON", exact: true }),
    ),
  );
  expect(json.schema).toBe("alloy.material.v6");
  expect(json.material.name).toBe("Browser Sand Study");
  expect(json.material.sandLayers[0].size).toBe(1.4);
  const shader = await downloaded(
    page,
    browser.getByRole("button", { name: "Shader", exact: true }),
  );
  const code = shader
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export (const|let|var|class|function|async function) /g, "$1 ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const material = new Function("THREE", code)(THREE);
  expect(material.userData.params.sandLayers[0].size).toBe(1.4);
  material.dispose();
  await browser.getByLabel("Search content assets", { exact: true }).fill("");
  await browser.getByRole("button", { name: /^Saved/ }).click();
  await expect(
    browser.getByRole("button", {
      name: "Inspect asset Browser Sand Study",
      exact: true,
    }),
  ).toBeVisible();
  await page.screenshot({ path: ".playwright/content-browser-material.png" });
  await page
    .getByRole("navigation", { name: "Studio workspace" })
    .getByRole("button", { name: "Material", exact: true })
    .click();
  await expect(page.locator(".viewport-panel canvas")).toHaveAttribute(
    "data-preview-shape",
    "Teapot",
  );
  await expect(
    page
      .getByRole("heading", { name: "Browser Sand Study", exact: true })
      .first(),
  ).toBeVisible();
  await expect(
    page.getByLabel("Bed grain size value", { exact: true }),
  ).toHaveValue("1.4");
  await expect(page.getByLabel("Studio lighting", { exact: true })).toHaveValue(
    "Daylight",
  );
  expect(errors).toEqual([]);
});

test("browser brush / generator assets assign undoable metadata, support drop and honour locked layers", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  const browser = browserOf(page);
  await browser
    .getByRole("button", { name: /^Brushes \/ instruments/ })
    .click();
  await browser
    .getByLabel("Search content assets", { exact: true })
    .fill("Airbrush Gun");
  const tool = PAINT_TOOLS.find((t) => t.label === "Airbrush Gun");
  await browser
    .getByRole("button", { name: "Inspect asset Airbrush Gun", exact: true })
    .click();
  await browser.getByLabel("Paint Opacity value", { exact: true }).fill("0");
  await browser
    .getByRole("button", { name: "Assign to layer", exact: true })
    .click();
  let saved = await project(page);
  expect(saved.layers.at(-1).sourceAsset).toMatchObject({
    type: "brush",
    id: tool.id,
  });
  expect(saved.painting.toolId).toBe(tool.id);
  expect(saved.painting.params.opacity).toBe(0);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect((await project(page)).layers.at(-1).sourceAsset).toBe(null);
  await browser.getByRole("button", { name: /^Generators/ }).click();
  await browser.getByLabel("Search content assets", { exact: true }).fill("");
  await browser
    .getByRole("button", { name: "Inspect asset Perlin Noise", exact: true })
    .click();
  const scale = browser.getByLabel("Asset generator Scale", { exact: true });
  await scale.fill("0.12");
  await browser
    .getByRole("button", { name: "Assign to layer", exact: true })
    .click();
  saved = await project(page);
  expect(saved.layers.at(-1).sourceAsset.id).toBe("PerlinNoise");
  expect(
    saved.layers.at(-1).channelSettings.baseColor.generator.parameters.Scale,
  ).toBe(0.12);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await expect(
    browser.getByRole("button", { name: "Assign to layer", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Unlock selected layer", exact: true })
    .click();
  await browser
    .getByRole("button", { name: "Inspect asset Dirt", exact: true })
    .dragTo(page.locator('[data-layer-id="surface-detail"]'));
  saved = await project(page);
  expect(saved.layers[0].sourceAsset.id).toBe("DirtAccumulation");
  await page.screenshot({ path: ".playwright/content-browser-generator.png" });
  expect(errors).toEqual([]);
});

test("phone content browser exposes assets and shared preview/inspector without horizontal overflow", async ({
  page,
}) => {
  await page.setViewportSize({ width: 390, height: 844 });
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  const browser = browserOf(page);
  await browser
    .getByLabel("Asset type", { exact: true })
    .selectOption("material");
  await browser
    .getByLabel("Search content assets", { exact: true })
    .fill("Zinc");
  await browser
    .getByRole("button", { name: "Inspect asset Corrugated Zinc", exact: true })
    .click();
  await browser
    .getByRole("button", { name: "Preview + inspector", exact: true })
    .click();
  await expect(
    browser.getByRole("heading", { name: "Material inspector", exact: true }),
  ).toBeVisible();
  await expect(browser.locator(".cb-material-preview canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
  await page.screenshot({ path: ".playwright/content-browser-phone.png" });
  expect(errors).toEqual([]);
});

test("channel source modes and generator controls have separate undo steps and locked inputs", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const source = page.getByRole("group", {
    name: "Base color source",
    exact: true,
  });
  await source.getByRole("button", { name: "Generator", exact: true }).click();
  await page
    .getByLabel("Base color generator", { exact: true })
    .selectOption("PerlinNoise");
  await page
    .getByLabel("Base color Perlin Noise Scale value", { exact: true })
    .fill("0.11");
  await source.getByRole("button", { name: "Texture", exact: true }).click();
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect(
    source.getByRole("button", { name: "Generator", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect(
    page.getByLabel("Base color Perlin Noise Scale value", { exact: true }),
  ).toHaveValue("0.11");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect(
    page.getByLabel("Base color Perlin Noise Scale value", { exact: true }),
  ).toHaveValue("0.4");
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  const saved = await project(page);
  expect(
    saved.layers.at(-1).channelSettings.baseColor.generator.parameters.Scale,
  ).toBe(0.11);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await expect(
    page.getByLabel("Base color generator", { exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByLabel("Base color Perlin Noise Scale value", { exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Clear all", exact: true }),
  ).toBeDisabled();
  await page.screenshot({ path: ".playwright/channel-property-card.png" });
  expect(errors).toEqual([]);
});
