import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 150000 });
const row = (page, id) => page.locator(`[data-layer-id="${id}"]`);
const browserOf = (page) =>
  page.getByRole("region", { name: "Content browser", exact: true });
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
async function project(page) {
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function folder(page, name = "Wear group") {
  await page
    .getByRole("button", { name: "Add layer folder", exact: true })
    .click();
  await page.getByLabel("Layer name", { exact: true }).fill(name);
  await page.getByLabel("Layer name", { exact: true }).press("Enter");
  return (await project(page)).layers.find((l) => l.name === name).id;
}
async function select(page, id) {
  await row(page, id).locator(".tp-layer-select").click();
}
async function textureFile(page, type = "image/png") {
  return {
    name: `red-blue.${type.split("/")[1]}`,
    mimeType: type,
    buffer: Buffer.from(
      await page.evaluate((type) => {
        const c = document.createElement("canvas");
        c.width = 512;
        c.height = 256;
        const ctx = c.getContext("2d");
        ctx.fillStyle = "#ff0000";
        ctx.fillRect(0, 0, 256, 256);
        ctx.fillStyle = "#0000ff";
        ctx.fillRect(256, 0, 256, 256);
        return c.toDataURL(type).split(",")[1];
      }, type),
      "base64",
    ),
  };
}
async function importTexture(page, file) {
  await page
    .getByRole("group", { name: "Base color source", exact: true })
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  const waiting = page.waitForEvent("filechooser");
  await page
    .getByRole("button", { name: /^(Import|Replace) texture$/, exact: true })
    .first()
    .click();
  await (await waiting).setFiles(file);
}
async function dragAsset(page, tile, target) {
  await tile.scrollIntoViewIfNeeded();
  const a = await tile.boundingBox();
  await page.mouse.move(a.x + a.width / 2, a.y + a.height / 2);
  await page.mouse.down();
  // Start the native drag before aiming at a layer that is initially under the
  // overlay. The library fades and lets the drag reach the underlying tree,
  // leaving the drag source mounted and at the same coordinates.
  await page.mouse.move(a.x + a.width / 2 + 18, a.y + a.height / 2 - 18, {
    steps: 4,
  });
  await expect(browserOf(page)).toHaveClass(/is-asset-dragging/);
  const b = await target.boundingBox();
  await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2, { steps: 8 });
  await page.mouse.move(b.x + b.width / 2 + 1, b.y + b.height / 2);
  await page.mouse.up();
  await expect(browserOf(page)).not.toHaveClass(/is-asset-dragging/);
}
async function maskPixels(page) {
  return page
    .getByRole("img", { name: "Colour-selection mask preview", exact: true })
    .evaluate((img) => {
      const c = document.createElement("canvas");
      c.width = img.naturalWidth;
      c.height = img.naturalHeight;
      const ctx = c.getContext("2d");
      ctx.drawImage(img, 0, 0);
      return [
        ctx.getImageData(10, 10, 1, 1).data[0],
        ctx.getImageData(c.width - 10, 10, 1, 1).data[0],
      ];
    });
}

for (const width of [1440, 940])
  test(`content browser overlays an invariant scene and dock layout; fixed scrolling grid at ${width}px`, async ({
    page,
  }) => {
    await page.setViewportSize({ width, height: 900 });
    const errors = errorsOf(page);
    await open(page);
    const selectors = [
      ".tp-layout",
      ".tp-viewport-dock",
      ".tp-scene-viewport",
      ".tp-scene-viewport canvas",
      ".tp-layers-dock",
      ".tp-inspector-dock",
    ];
    async function bounds() {
      return Promise.all(selectors.map((s) => page.locator(s).boundingBox()));
    }
    const before = await bounds(),
      canvasSize = await page
        .locator(".tp-scene-viewport canvas")
        .evaluate((c) => [c.width, c.height]);
    await page
      .getByRole("button", { name: "Open content browser", exact: true })
      .click();
    const browser = browserOf(page);
    expect(await bounds()).toEqual(before);
    expect(
      await page
        .locator(".tp-scene-viewport canvas")
        .evaluate((c) => [c.width, c.height]),
    ).toEqual(canvasSize);
    const columns = await Promise.all(
      [".cb-outliner", ".cb-assets", ".cb-preview-inspector"].map((s) =>
        browser.locator(s).boundingBox(),
      ),
    );
    expect(columns[0].x + columns[0].width).toBeLessThanOrEqual(columns[1].x);
    expect(columns[1].x + columns[1].width).toBeLessThanOrEqual(columns[2].x);
    expect(columns[2].x + columns[2].width).toBeLessThanOrEqual(width);
    await expect(browser.locator(".cb-grid>.cb-asset-tile")).toHaveCount(127);
    const divider = page.getByRole("separator", {
      name: "Resize content browser",
      exact: true,
    });
    await divider.focus();
    await divider.press("ArrowDown");
    await divider.press("ArrowDown");
    const smallTile = await browser
      .locator(".cb-grid>.cb-asset-tile")
      .first()
      .boundingBox();
    expect(smallTile.height).toBe(172);
    const media = await browser
      .locator(".cb-grid>.cb-asset-tile")
      .first()
      .locator(":scope > img,:scope > div")
      .boundingBox();
    expect(media.height).toBe(124);
    expect(await bounds()).toEqual(before);
    await divider.press("End");
    expect(await bounds()).toEqual(before);
    expect(
      (await browser.locator(".cb-grid>.cb-asset-tile").first().boundingBox())
        .height,
    ).toBe(smallTile.height);
    const scrolling = await browser
      .locator(".cb-asset-items")
      .evaluate((e) => ({
        client: e.clientHeight,
        scroll: e.scrollHeight,
        overflow: getComputedStyle(e).overflowY,
      }));
    expect(scrolling.scroll).toBeGreaterThan(scrolling.client);
    expect(scrolling.overflow).toBe("auto");
    const last = browser.locator(".cb-grid>.cb-asset-tile").last();
    await last.scrollIntoViewIfNeeded();
    await expect(last).toBeInViewport();
    const root = await browser.locator(".cb-asset-items").boundingBox(),
      lastBounds = await last.boundingBox();
    expect(lastBounds.y).toBeGreaterThanOrEqual(root.y);
    expect(lastBounds.y + lastBounds.height).toBeLessThanOrEqual(
      root.y + root.height,
    );
    await expect(
      browser.getByRole("button", { name: /Next assets|Previous assets/ }),
    ).toHaveCount(0);
    await browser
      .getByRole("button", { name: /^Brushes \/ instruments/ })
      .click();
    await expect(browser.locator(".cb-grid>.cb-asset-tile")).toHaveCount(102);
    await browser
      .locator(".cb-grid>.cb-asset-tile")
      .last()
      .scrollIntoViewIfNeeded();
    await expect(
      browser.locator(".cb-grid>.cb-asset-tile").last(),
    ).toBeInViewport();
    await page.screenshot({ path: `.playwright/browser-overlay-${width}.png` });
    await page
      .getByRole("button", { name: "Collapse content browser", exact: true })
      .click();
    expect(await bounds()).toEqual(before);
    expect(errors).toEqual([]);
  });

test("folder tree membership, collapse/search, sibling ordering, inherited locks, subtree duplication and ungroup are undoable", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const id = await folder(page);
  for (const child of ["surface-detail", "surface-finish"]) {
    await select(page, child);
    await page.getByLabel("Layer folder", { exact: true }).selectOption(id);
  }
  await expect(row(page, "surface-detail")).toHaveAttribute("aria-level", "2");
  await expect(row(page, "surface-finish")).toHaveAttribute("aria-level", "2");
  await select(page, "surface-finish");
  await page
    .getByRole("button", { name: "Move layer up", exact: true })
    .click();
  let doc = await project(page);
  expect(doc.layers.filter((l) => l.parentId === id).map((l) => l.id)).toEqual([
    "surface-finish",
    "surface-detail",
  ]);
  await select(page, id);
  await page
    .getByRole("button", { name: "Collapse Wear group", exact: true })
    .click();
  await expect(row(page, "surface-detail")).toHaveCount(0);
  await page.getByLabel("Find a layer", { exact: true }).fill("Surface detail");
  await expect(row(page, "surface-detail")).toBeVisible();
  await expect(row(page, id)).toBeVisible();
  await page
    .getByRole("button", { name: "Clear layer search", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Expand Wear group", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await select(page, "surface-detail");
  await expect(page.getByLabel("Layer name", { exact: true })).toBeDisabled();
  await expect(page.getByLabel("Layer folder", { exact: true })).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Add painting channel", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Duplicate layer", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByText(
      "This layer inherits a locked folder. Unlock the folder to edit its contents.",
      { exact: true },
    ),
  ).toBeVisible();
  await select(page, id);
  await page
    .getByRole("button", { name: "Unlock selected layer", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Duplicate layer", exact: true })
    .click();
  doc = await project(page);
  const copy = doc.layers.find((l) => l.name === "Wear group copy");
  expect(doc.layers.filter((l) => l.parentId === copy.id)).toHaveLength(2);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect((await project(page)).layers).toHaveLength(4);
  await select(page, id);
  await page
    .getByRole("button", { name: "Ungroup folder", exact: true })
    .click();
  doc = await project(page);
  expect(doc.layers).toHaveLength(3);
  expect(doc.layers.every((l) => l.parentId === null)).toBe(true);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  doc = await project(page);
  expect(doc.layers).toHaveLength(4);
  await page.reload();
  await expect(row(page, id)).toBeVisible();
  await select(page, id);
  await expect(page.getByLabel("Layer name", { exact: true })).toHaveValue(
    "Wear group",
  );
  await expect(row(page, "surface-detail")).toHaveAttribute("aria-level", "2");
  await page.screenshot({ path: ".playwright/layer-folders.png" });
  expect(errors).toEqual([]);
});

test("actual imported Texture thumbnails and layer/folder colour masks preview source pixels, preserve zero and round-trip", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const file = await textureFile(page),
    source = page.getByRole("group", {
      name: "Base color source",
      exact: true,
    });
  await expect(
    source.getByRole("button", { name: "Painted", exact: true }),
  ).toHaveCount(0);
  await source.getByRole("button", { name: "Texture", exact: true }).click();
  await importTexture(page, file);
  await expect(
    page.getByRole("img", { name: "Base color imported texture", exact: true }),
  ).toBeVisible();
  await expect(
    row(page, "base-material").getByRole("img", {
      name: "Base material texture preview",
      exact: true,
    }),
  ).toBeVisible();
  let doc = await project(page);
  const texture = doc.layers.find((l) => l.id === "base-material")
    .channelSettings.baseColor.texture;
  expect(texture).toMatchObject({
    name: "red-blue.png",
    width: 256,
    height: 128,
  });
  expect(texture.dataUrl).toMatch(/^data:image\/png;base64,/);
  await page
    .getByRole("button", { name: "Add colour mask", exact: true })
    .click();
  await page
    .getByLabel("Colour mask colour hex", { exact: true })
    .fill("#ff0000");
  await page
    .getByLabel("Colour mask tolerance value", { exact: true })
    .fill("0");
  await page
    .getByLabel("Colour mask softness value", { exact: true })
    .fill("0");
  await expect.poll(() => maskPixels(page)).toEqual([255, 0]);
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  await expect.poll(() => maskPixels(page)).toEqual([255, 255]);
  await page.getByLabel("Mask strength value", { exact: true }).fill("100");
  await page
    .getByRole("checkbox", { name: "Invert mask", exact: true })
    .check();
  await expect.poll(() => maskPixels(page)).toEqual([0, 255]);
  await page
    .getByRole("button", { name: "Back to layer", exact: true })
    .click();
  const id = await folder(page, "Colour group");
  await select(page, "base-material");
  await page.getByLabel("Layer folder", { exact: true }).selectOption(id);
  await select(page, id);
  await page
    .getByRole("button", { name: "Add colour mask", exact: true })
    .click();
  await page
    .getByLabel("Colour mask colour hex", { exact: true })
    .fill("#ff0000");
  await page
    .getByLabel("Colour mask tolerance value", { exact: true })
    .fill("0");
  await page
    .getByLabel("Colour mask softness value", { exact: true })
    .fill("0");
  await expect.poll(() => maskPixels(page)).toEqual([255, 0]);
  await expect(
    page.getByText("Folder & descendants", { exact: true }),
  ).toBeVisible();
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  doc = await project(page);
  expect(doc.layers.find((l) => l.id === id).mask).toMatchObject({
    kind: "color",
    color: "#ff0000",
    strength: 0,
    tolerance: 0,
    softness: 0,
  });
  expect(doc.layers.find((l) => l.id === "base-material")).toMatchObject({
    parentId: id,
    mask: { kind: "color", inverted: true },
  });
  await page.screenshot({ path: ".playwright/folder-colour-mask.png" });
  await page.reload();
  await select(page, id);
  await page
    .getByRole("button", { name: "Select mask of Colour group", exact: true })
    .click();
  await expect(
    page.getByLabel("Mask strength value", { exact: true }),
  ).toHaveValue("0");
  await expect.poll(() => maskPixels(page)).toEqual([255, 255]);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await expect(
    page.getByLabel("Colour mask colour hex", { exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Remove layer mask", exact: true }),
  ).toBeDisabled();
  expect(errors).toEqual([]);
});

test("folder asset drops create assigned children in one undo step; layer drops group without cycles", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const id = await folder(page, "Asset group");
  await select(page, "surface-detail");
  await row(page, "surface-detail").dragTo(row(page, id));
  expect(
    (await project(page)).layers.find((l) => l.id === "surface-detail")
      .parentId,
  ).toBe(id);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await select(page, id);
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  const browser = browserOf(page);
  await browser.getByRole("button", { name: /^Generators/ }).click();
  await dragAsset(
    page,
    browser.getByRole("button", { name: "Inspect asset Dirt", exact: true }),
    row(page, id),
  );
  let doc = await project(page);
  const assigned = doc.layers.find((l) => l.parentId === id);
  expect(assigned).toMatchObject({
    kind: "generator",
    sourceAsset: { type: "generator", id: "DirtAccumulation" },
  });
  expect(doc.layers.find((l) => l.id === id).sourceAsset).toBe(null);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  doc = await project(page);
  expect(doc.layers).toHaveLength(4);
  expect(doc.layers.some((l) => l.parentId === id)).toBe(false);
  await page
    .getByRole("button", { name: "Collapse content browser", exact: true })
    .click();
  await select(page, id);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  await expect(
    browser.getByRole("button", { name: "Assign to layer", exact: true }),
  ).toBeDisabled();
  expect(errors).toEqual([]);
});

test("unsupported/corrupt rasters and imports resolving after a folder lock or selection change preserve the project", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const file = await textureFile(page);
  for (const type of ["image/png", "image/jpeg", "image/webp"]) {
    const raster = await textureFile(page, type);
    await importTexture(page, raster);
    await expect(
      page.getByRole("img", {
        name: "Base color imported texture",
        exact: true,
      }),
    ).toBeVisible();
    const saved = (await project(page)).layers.find(
      (l) => l.id === "base-material",
    ).channelSettings.baseColor.texture;
    expect(saved).toMatchObject({ name: raster.name, width: 256, height: 128 });
    expect(saved.dataUrl).toMatch(/^data:image\/png;base64,/);
  }
  await expect(
    page.getByRole("img", { name: "Base color imported texture", exact: true }),
  ).toBeVisible();
  const before = (await project(page)).layers.find(
    (l) => l.id === "base-material",
  ).channelSettings.baseColor.texture;
  for (const invalid of [
    {
      name: "evil.svg",
      mimeType: "image/svg+xml",
      buffer: Buffer.from('<svg onload="alert(1)"></svg>'),
    },
    {
      name: "bad.png",
      mimeType: "image/png",
      buffer: file.buffer.subarray(0, 24),
    },
    {
      name: "big.png",
      mimeType: "image/png",
      buffer: Buffer.alloc(4 * 1024 * 1024 + 1),
    },
  ]) {
    await importTexture(page, invalid);
    await expect(
      page.getByRole("alert").filter({ hasText: /raster|decode|MB/i }),
    ).toBeVisible();
    expect(
      (await project(page)).layers.find((l) => l.id === "base-material")
        .channelSettings.baseColor.texture,
    ).toEqual(before);
  }
  const id = await folder(page, "Locked imports");
  await select(page, "base-material");
  await page.getByLabel("Layer folder", { exact: true }).selectOption(id);
  await page.evaluate(() => {
    const real = window.createImageBitmap;
    window.createImageBitmap = async (...args) => {
      await new Promise((resolve) => {
        window.releaseTextureImport = resolve;
      });
      return real(...args);
    };
  });
  await importTexture(page, { ...file, name: "late-texture.png" });
  await expect(
    page.getByRole("button", { name: "Importing…", exact: true }),
  ).toBeVisible();
  await row(page, id)
    .getByRole("button", { name: "Lock Locked imports", exact: true })
    .click();
  await page.evaluate(() => window.releaseTextureImport());
  await expect(
    page.getByRole("button", { name: "Replace texture", exact: true }),
  ).toBeDisabled();
  expect(
    (await project(page)).layers.find((l) => l.id === "base-material")
      .channelSettings.baseColor.texture,
  ).toEqual(before);
  await row(page, id)
    .getByRole("button", { name: "Unlock Locked imports", exact: true })
    .click();
  await importTexture(page, { ...file, name: "late-texture.png" });
  await expect(
    page.getByRole("button", { name: "Importing…", exact: true }),
  ).toBeVisible();
  await select(page, "surface-finish");
  await page.evaluate(() => window.releaseTextureImport());
  await expect(page.getByLabel("Layer name", { exact: true })).toHaveValue(
    "Surface finish",
  );
  expect(
    (await project(page)).layers.find((l) => l.id === "surface-finish")
      .channelSettings.baseColor.texture,
  ).toBe(null);
  expect(errors).toEqual([]);
});
