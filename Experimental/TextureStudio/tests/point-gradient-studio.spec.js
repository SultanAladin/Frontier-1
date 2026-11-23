import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 180000 });
test.use({ actionTimeout: 20000 });
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
    { timeout: 45000 },
  );
}
async function project(page) {
  const pending = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await pending).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
const base = (page) =>
  page.getByRole("group", { name: "Base color source", exact: true });

test("gradient fill previews a real surface field; balls place/drag/select with colour/radius/weight and undo", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await base(page)
    .getByRole("button", { name: "Gradient", exact: true })
    .click();
  const editor = page.getByRole("region", {
    name: "Base color gradient editor",
    exact: true,
  });
  await expect(
    editor.getByRole("img", {
      name: "Gradient colour field slice",
      exact: true,
    }),
  ).toBeVisible();
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-gradient-preview",
    "fill",
  );
  await editor
    .getByRole("button", { name: "Edit in viewport", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: /^Viewport gradient point/ }),
  ).toHaveCount(2);
  await editor
    .getByLabel("Base color gradient point colour", { exact: true })
    .fill("#ff0000");
  await editor
    .getByLabel("Base color gradient point radius value", { exact: true })
    .fill("0.35");
  await editor
    .getByLabel("Base color gradient point weight value", { exact: true })
    .fill("0");
  let saved = await project(page);
  expect(
    saved.layers.at(-1).channelSettings.baseColor.gradient.points[0],
  ).toMatchObject({ color: "#ff0000", radius: 0.35, weight: 0 });
  await editor
    .getByRole("button", { name: "Place point", exact: true })
    .click();
  const canvas = page.locator(".tp-scene-viewport canvas"),
    box = await canvas.boundingBox();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.55);
  await expect(
    page.getByRole("button", { name: /^Viewport gradient point/ }),
  ).toHaveCount(3);
  saved = await project(page);
  expect(
    saved.layers.at(-1).channelSettings.baseColor.gradient.points,
  ).toHaveLength(3);
  await page
    .getByRole("button", { name: "Viewport gradient point 3", exact: true })
    .focus();
  await page.keyboard.press("ArrowRight");
  let moved = await project(page);
  expect(
    moved.layers.at(-1).channelSettings.baseColor.gradient.points[2]
      .position[0],
  ).toBeGreaterThan(
    saved.layers.at(-1).channelSettings.baseColor.gradient.points[2]
      .position[0],
  );
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(
    (await project(page)).layers.at(-1).channelSettings.baseColor.gradient
      .points[2].position,
  ).toEqual(
    saved.layers.at(-1).channelSettings.baseColor.gradient.points[2].position,
  );
  const beforeDrag = await project(page),
    handle = page.getByRole("button", {
      name: "Viewport gradient point 3",
      exact: true,
    }),
    h = await handle.boundingBox();
  await page.mouse.move(h.x + h.width / 2, h.y + h.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.48, box.y + box.height * 0.61, {
    steps: 12,
  });
  await page.mouse.up();
  const dragged = await project(page);
  expect(
    dragged.layers.at(-1).channelSettings.baseColor.gradient.points[2].position,
  ).not.toEqual(
    beforeDrag.layers.at(-1).channelSettings.baseColor.gradient.points[2]
      .position,
  );
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(
    (await project(page)).layers.at(-1).channelSettings.baseColor.gradient,
  ).toEqual(beforeDrag.layers.at(-1).channelSettings.baseColor.gradient);
  await page.screenshot({ path: ".playwright/gradient-fill.png" });
  expect(errors).toEqual([]);
});

test("Material-studio custom generator produces real portable scratch field pixels using shared recipe controls", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await base(page)
    .getByRole("button", { name: "Generator", exact: true })
    .click();
  await page
    .getByLabel("Base color generator", { exact: true })
    .selectOption("MaterialStudio");
  const source = page.getByLabel("Material studio generator", { exact: true });
  await expect(
    source.getByRole("img", {
      name: "Generated Material-studio field",
      exact: true,
    }),
  ).toBeVisible({ timeout: 90000 });
  const doc = await project(page),
    gen = doc.layers.at(-1).channelSettings.baseColor.generator;
  expect(gen.id).toBe("MaterialStudio");
  expect(gen.material.recipeId).toBe("scratches");
  expect(gen.output).toBe("scratch-mask");
  expect(gen.result.dataUrl).toMatch(/^data:image\/png/);
  await page.screenshot({ path: ".playwright/material-generator-source.png" });
  expect(errors).toEqual([]);
});

test("gradient masks affect actual mesh shading, honour inversion/zero strength and are inherited from folders", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await base(page)
    .getByRole("button", { name: "Gradient", exact: true })
    .click();
  const canvas = page.locator(".tp-scene-viewport canvas");
  const frame = () => canvas.evaluate((c) => c.toDataURL());
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await page
    .getByRole("group", { name: "Mask type", exact: true })
    .getByRole("button", { name: "Gradient", exact: true })
    .click();
  // This test checks material shading, not the newly independent grayscale
  // mask preview. Keep the same mask editor/canvas bounds for the baseline.
  await page
    .getByRole("button", { name: "Material preview", exact: true })
    .click();
  const mask = page.getByRole("region", {
    name: "Mask gradient editor",
    exact: true,
  });
  await expect(
    mask.getByRole("img", { name: "Gradient mask field slice", exact: true }),
  ).toBeVisible();
  await mask
    .getByRole("button", { name: "Mask gradient point 1", exact: true })
    .click();
  await mask
    .getByLabel("Mask gradient point colour", { exact: true })
    .fill("#000000");
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  await page.waitForTimeout(600);
  const unmasked = await frame();
  await page.getByLabel("Mask strength value", { exact: true }).fill("100");
  await expect.poll(frame).not.toBe(unmasked);
  const masked = await frame();
  await page.getByLabel("Invert mask", { exact: true }).check();
  await expect.poll(frame).not.toBe(masked);
  await page.getByLabel("Invert mask", { exact: true }).uncheck();
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  await expect.poll(frame).toBe(unmasked);
  let doc = await project(page);
  expect(doc.layers.at(-1).mask).toMatchObject({
    kind: "gradient",
    strength: 0,
  });
  await page.screenshot({ path: ".playwright/gradient-mask.png" });
  await page.reload();
  await page
    .getByRole("button", { name: "Select mask of Base material", exact: true })
    .click();
  await expect(
    page.getByLabel("Mask strength value", { exact: true }),
  ).toHaveValue("0");
  await page
    .getByRole("button", { name: "Select Base material", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Add layer folder", exact: true })
    .click();
  doc = await project(page);
  const folder = doc.layers.find((l) => l.kind === "folder");
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await page
    .getByRole("group", { name: "Mask type", exact: true })
    .getByRole("button", { name: "Gradient", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Select Base material", exact: true })
    .click();
  await page
    .getByLabel("Layer folder", { exact: true })
    .selectOption(folder.id);
  await page
    .locator(`[data-layer-id="${folder.id}"]`)
    .getByRole("button", { name: "Lock Folder", exact: true })
    .click();
  await expect(
    base(page).getByRole("button", { name: "Gradient", exact: true }),
  ).toBeDisabled();
  expect(errors).toEqual([]);
});

test("instrument properties use shared cards/sliders and an HSV colour picker, retain zero and keyboard interaction", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
  const menu = page.getByRole("dialog", {
    name: "Paint tool menu",
    exact: true,
  });
  await menu
    .getByRole("button", {
      name: "Select paint tool Classic Gold-Nib Fountain",
      exact: true,
    })
    .click();
  await expect(
    menu.locator(".paint-control-group .slider-field"),
  ).not.toHaveCount(0);
  await expect(menu.locator(".paint-color")).toHaveCount(0);
  await expect(
    menu.getByRole("region", { name: "Paint tool color picker", exact: true }),
  ).toBeVisible();
  await menu.getByLabel("Paint Opacity value", { exact: true }).fill("0");
  await menu.getByLabel("Paint tool color", { exact: true }).fill("#00ff00");
  await menu
    .getByLabel("Paint tool color hue value", { exact: true })
    .fill("240");
  await expect(
    menu.getByLabel("Paint tool color", { exact: true }),
  ).toHaveValue("#0000FF");
  const field = menu.getByRole("slider", {
    name: "Paint tool color saturation and brightness",
    exact: true,
  });
  await field.focus();
  await field.press("ArrowLeft");
  await expect(
    menu.getByLabel("Paint tool color", { exact: true }),
  ).not.toHaveValue("#0000FF");
  await page.screenshot({
    path: ".playwright/shared-instrument-properties.png",
  });
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  expect((await project(page)).painting.params.opacity).toBe(0);
  expect(errors).toEqual([]);
});

test("Material generator parameter changes regenerate pixels and a late render cannot overwrite a changed source mode", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await base(page)
    .getByRole("button", { name: "Generator", exact: true })
    .click();
  await page
    .getByLabel("Base color generator", { exact: true })
    .selectOption("MaterialStudio");
  const image = page.getByRole("img", {
    name: "Generated Material-studio field",
    exact: true,
  });
  await expect(image).toBeVisible({ timeout: 90000 });
  const first = (await project(page)).layers.at(-1).channelSettings.baseColor
    .generator;
  await page.getByLabel("Invert Material generator", { exact: true }).check();
  await expect(image).toBeVisible({ timeout: 90000 });
  await expect(image).not.toHaveAttribute("src", first.result.dataUrl, {
    timeout: 90000,
  });
  await page
    .getByLabel("Material generator patch width value", { exact: true })
    .fill("250");
  await base(page).getByRole("button", { name: "Value", exact: true }).click();
  const before = await project(page);
  await page.waitForTimeout(1800);
  expect(await project(page)).toEqual(before);
  expect(errors).toEqual([]);
});
