import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  TEXTURE_DRAFT_KEY,
} from "../src/textureDocument.js";

async function open(page) {
  await page.goto("/?studio=texture");
  await expect(page.locator(".tp-studio canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
}
async function project(page) {
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function importProject(page, data) {
  await page.getByLabel("Open texture layer JSON").setInputFiles({
    name: "surface.texture.json",
    mimeType: "application/json",
    buffer: Buffer.from(typeof data === "string" ? data : JSON.stringify(data)),
  });
}
function row(page, id) {
  return page.locator(`[data-layer-id="${id}"]`);
}

test("texture studio renders a real teapot with layer stack left and inspector right, paint configuration and separate bake workspace", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await open(page);
  const canvas = page.locator(".tp-studio canvas");
  await expect(canvas).toHaveAttribute("data-preview-shape", "Teapot");
  expect(
    Number(await canvas.getAttribute("data-triangle-count")),
  ).toBeGreaterThan(10000);
  expect(await canvas.evaluate((c) => c.toDataURL().length)).toBeGreaterThan(
    10000,
  );
  const stack = await page
    .getByRole("complementary", { name: "Layer stack", exact: true })
    .boundingBox();
  const inspector = await page
    .getByRole("complementary", { name: "Layer inspector", exact: true })
    .boundingBox();
  const viewport = await page
    .getByRole("region", { name: "3D scene", exact: true })
    .boundingBox();
  expect(stack.x + stack.width).toBeLessThanOrEqual(viewport.x);
  expect(viewport.x + viewport.width).toBeLessThanOrEqual(inspector.x);
  await expect(
    page.getByRole("button", { name: "Open paint tool menu", exact: true }),
  ).toBeVisible();
  await expect(page.getByRole("button", { name: /^Bake/ })).toHaveCount(0);
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("wireframe");
  await expect(canvas).toHaveAttribute("data-wireframe", "true");
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await expect(canvas).toHaveAttribute("data-wireframe", "false");
  await page
    .getByRole("button", { name: "Toggle viewport grid", exact: true })
    .click();
  await expect(canvas).toHaveAttribute("data-grid-visible", "false");
  await page
    .getByRole("button", { name: "Zoom in teapot", exact: true })
    .click();
  await expect(page.getByLabel("Teapot zoom")).toHaveText("150%");
  await page.getByRole("button", { name: "Frame teapot", exact: true }).click();
  await expect(page.getByLabel("Teapot zoom")).toHaveText("100%");
  expect(errors).toEqual([]);
});

test("texture layer menu, numeric properties, channels and per-document undo are functional", async ({
  page,
}) => {
  await open(page);
  for (const kind of ["Paint", "Fill", "Material", "Generator"]) {
    await page.getByRole("button", { name: "Add layer", exact: true }).click();
    await page
      .getByRole("menuitem", { name: new RegExp(`^${kind} layer`) })
      .click();
    await expect(
      page.getByRole("textbox", { name: "Layer name", exact: true }),
    ).toHaveValue(`${kind} layer`);
  }
  const name = page.getByRole("textbox", { name: "Layer name", exact: true });
  await name.fill("Fine procedural detail");
  await name.press("Enter");
  await expect(
    page.getByRole("button", {
      name: "Select Fine procedural detail",
      exact: true,
    }),
  ).toBeVisible();
  await page
    .getByLabel("Layer blend mode", { exact: true })
    .selectOption("Multiply");
  await page
    .getByRole("spinbutton", { name: "Layer opacity value", exact: true })
    .fill("0");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect(
    page.getByRole("spinbutton", { name: "Layer opacity value", exact: true }),
  ).toHaveValue("100");
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await expect(
    page.getByRole("spinbutton", { name: "Layer opacity value", exact: true }),
  ).toHaveValue("0");
  await page
    .getByRole("button", { name: "Add painting channel", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Emission channel", exact: true })
    .check();
  const saved = await project(page);
  expect(saved.layers).toHaveLength(7);
  expect(
    saved.layers.find((l) => l.name === "Fine procedural detail"),
  ).toMatchObject({ kind: "generator", opacity: 0, blend: "Multiply" });
  expect(
    saved.layers.find((l) => l.name === "Fine procedural detail").channels,
  ).toContain("emission");
  expect(saved.scene).toBe("teapot");
});

test("texture masks have independent inspectors, copies and reversible removal, and locked layers are protected", async ({
  page,
}) => {
  await open(page);
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await expect(
    page.getByRole("heading", { name: "Layer mask", exact: true }),
  ).toBeVisible();
  await page.getByLabel("Mask fill", { exact: true }).selectOption("0");
  await page
    .getByRole("checkbox", { name: "Invert mask", exact: true })
    .check();
  await page
    .getByRole("spinbutton", { name: "Mask strength value", exact: true })
    .fill("32");
  await page
    .getByRole("button", { name: "Duplicate layer", exact: true })
    .click();
  let doc = await project(page);
  expect(doc.layers).toHaveLength(4);
  expect(doc.layers[2].mask).toEqual(doc.layers[3].mask);
  expect(doc.layers[2].id).not.toBe(doc.layers[3].id);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await expect(
    page.getByRole("textbox", { name: "Layer name", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Delete layer", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Duplicate layer", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Hide selected layer", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Unlock selected layer", exact: true })
    .click();
  await page
    .getByRole("button", {
      name: "Select mask of Base material copy",
      exact: true,
    })
    .click();
  await page
    .getByRole("button", { name: "Remove layer mask", exact: true })
    .click();
  await expect(
    page.getByRole("button", {
      name: "Select mask of Base material copy",
      exact: true,
    }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  doc = await project(page);
  expect(doc.layers.find((l) => l.name === "Base material copy")).toMatchObject(
    {
      visible: false,
      locked: false,
      mask: { fill: 0, inverted: true, strength: 32 },
    },
  );
});

test("texture stack reorder, search, visibility filtering and keyboard menus preserve layer identities", async ({
  page,
}) => {
  await open(page);
  await page
    .getByRole("button", { name: "Move layer up", exact: true })
    .click();
  let doc = await project(page);
  expect(doc.layers.map((l) => l.id)).toEqual([
    "surface-detail",
    "base-material",
    "surface-finish",
  ]);
  await page
    .getByRole("button", { name: "Move layer down", exact: true })
    .click();
  await row(page, "base-material").dragTo(row(page, "surface-detail"));
  doc = await project(page);
  expect(doc.layers.map((l) => l.id)).toEqual([
    "base-material",
    "surface-detail",
    "surface-finish",
  ]);
  await page
    .getByRole("button", { name: "Hide Base material", exact: true })
    .click();
  await page.getByRole("button", { name: /^Hidden/ }).click();
  await expect(page.locator(".tp-layer-row")).toHaveCount(1);
  await page.getByRole("button", { name: /Clear filter/ }).click();
  await page.getByLabel("Find a layer", { exact: true }).fill("finish");
  await expect(page.locator(".tp-layer-row")).toHaveCount(1);
  await expect(row(page, "surface-finish")).toBeVisible();
  await page
    .getByRole("button", { name: "Clear layer search", exact: true })
    .click();
  await page.getByRole("button", { name: "Add layer", exact: true }).click();
  await expect(
    page.getByRole("menuitem", { name: /^Paint layer/ }),
  ).toBeFocused();
  await page.keyboard.press("ArrowDown");
  await expect(
    page.getByRole("menuitem", { name: /^Fill layer/ }),
  ).toBeFocused();
  await page.keyboard.press("Escape");
  await expect(
    page.getByRole("button", { name: "Add layer", exact: true }),
  ).toBeFocused();
});

test("texture save and reopen validate metadata, recover a local draft and do not change the teapot material", async ({
  page,
}) => {
  await open(page);
  const name = page.getByRole("textbox", { name: "Layer name", exact: true });
  await name.fill("Local base with spaces");
  await name.press("Enter");
  const saved = await project(page);
  expect(saved.layers.at(-1).name).toBe("Local base with spaces");
  await page.reload();
  await expect(
    page.getByRole("textbox", { name: "Layer name", exact: true }),
  ).toHaveValue("Local base with spaces");
  await importProject(page, {
    ...saved,
    layers: [saved.layers[0]],
    name: "One layer",
  });
  await expect(page.locator(".tp-layer-row")).toHaveCount(1);
  await importProject(page, "{bad JSON");
  await expect(page.getByRole("alert")).toContainText("valid JSON");
  await expect(page.locator(".tp-layer-row")).toHaveCount(1);
  await page
    .getByRole("button", { name: "Dismiss layer error", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await page
    .getByRole("spinbutton", { name: "Layer opacity value", exact: true })
    .fill("20");
  await expect(page.locator(".tp-studio canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await expect(
    page.getByText("Layer setup only · preview is not composited", {
      exact: true,
    }),
  ).toBeVisible();
  // No authoring operation is registered on the viewport in this pass.
  const before = await project(page);
  const box = await page.locator(".tp-studio canvas").boundingBox();
  await page.mouse.move(box.x + box.width * 0.45, box.y + box.height * 0.5);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.55, box.y + box.height * 0.52, {
    steps: 4,
  });
  await page.mouse.up();
  expect(await project(page)).toEqual(before);
});

test("texture capacity, empty projects and corrupted browser drafts fail safely", async ({
  page,
}) => {
  await page.addInitScript(
    (key) => localStorage.setItem(key, "not valid JSON"),
    TEXTURE_DRAFT_KEY,
  );
  await open(page);
  await expect(page.getByRole("alert")).toContainText("could not be restored");
  await page
    .getByRole("button", { name: "Dismiss layer error", exact: true })
    .click();
  const doc = createTextureDocument();
  const layers = Array.from({ length: 64 }, (_, i) =>
    createTextureLayer("fill", { id: `capacity-${i}` }),
  );
  await importProject(page, { ...doc, layers });
  await expect(
    page.getByRole("button", { name: "Add layer", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Duplicate layer", exact: true }),
  ).toBeDisabled();
  await importProject(page, {
    ...doc,
    layers: [...layers, createTextureLayer("paint")],
  });
  await expect(page.getByRole("alert")).toContainText("64");
  await expect(page.locator(".tp-layer-row")).toHaveCount(64);
  await importProject(page, { ...doc, layers: [] });
  await expect(
    page.getByText("No layer selected", { exact: true }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Add layer", exact: true })
    .first()
    .click();
  await page.getByRole("menuitem", { name: /^Material layer/ }).click();
  await expect(page.locator(".tp-layer-row")).toHaveCount(1);
});

test("texture dock resize, phone inspector and workspace switches remain usable without horizontal overflow", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  const divider = page.getByRole("separator", {
    name: "Resize layer stack",
    exact: true,
  });
  const width = Number(await divider.getAttribute("aria-valuenow"));
  await divider.focus();
  await divider.press("ArrowRight");
  await expect(divider).toHaveAttribute("aria-valuenow", String(width + 10));
  const rightDivider = page.getByRole("separator", {
    name: "Resize layer inspector",
    exact: true,
  });
  const rightWidth = Number(await rightDivider.getAttribute("aria-valuenow"));
  await rightDivider.focus();
  await rightDivider.press("ArrowLeft");
  await expect(rightDivider).toHaveAttribute(
    "aria-valuenow",
    String(rightWidth + 10),
  );
  await rightDivider.press("ArrowRight");
  await expect(rightDivider).toHaveAttribute(
    "aria-valuenow",
    String(rightWidth),
  );
  await page.setViewportSize({ width: 390, height: 844 });
  await expect(
    page.getByRole("tab", { name: "Layers", exact: true }),
  ).toBeVisible();
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  const name = page.getByRole("textbox", { name: "Layer name", exact: true });
  await name.fill("Phone base");
  await name.press("Enter");
  await expect(name).toHaveValue("Phone base");
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
  await page.getByRole("tab", { name: "Layers", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Select Phone base", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Pattern", exact: true }).click();
  await expect(
    page.getByLabel("Pattern design canvas", { exact: true }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Close pattern studio", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Texture studio", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Select Phone base", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Material", exact: true }).click();
  await expect(page.locator(".tp-studio")).toHaveCount(0);
  await expect(page.locator(".main-header")).toBeVisible();
  expect(errors).toEqual([]);
});
