import { test, expect } from "@playwright/test";
import { PAINT_CHANNELS } from "../src/paintChannelModel.js";
test.describe.configure({ timeout: 180000 });
test.use({ actionTimeout: 20000 });
test.beforeEach(({ page }) => {
  page.setDefaultTimeout(20000);
});
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
const canvas = (page) => page.locator(".tp-scene-viewport canvas");
async function open(page, workspace = "texture") {
  await page.goto(`/?studio=${workspace}`);
  if (workspace === "texture") {
    await expect(canvas(page)).toHaveAttribute("data-material-ready", "true", {
      timeout: 45000,
    });
    await expect(
      page.getByRole("button", { name: "Viewport stamp brush", exact: true }),
    ).toBeEnabled();
  } else
    await expect(
      page.getByAltText("Composed stamp preview", { exact: true }),
    ).toBeVisible();
}
async function downloaded(page, button) {
  const waiting = page.waitForEvent("download");
  await button.click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return Buffer.concat(chunks);
}
async function project(page) {
  return JSON.parse(
    (
      await downloaded(
        page,
        page.getByRole("button", { name: "Save project", exact: true }),
      )
    ).toString(),
  );
}
async function stampProject(page) {
  return JSON.parse(
    (
      await downloaded(
        page,
        page.getByRole("button", { name: "Save stamp project", exact: true }),
      )
    ).toString(),
  );
}
const components = (doc) => doc.layers.flatMap((l) => l.decals || []);
async function surfaceClick(page, x = 0.5, y = 0.5) {
  const box = await canvas(page).boundingBox();
  await page.mouse.click(box.x + box.width * x, box.y + box.height * y);
}
async function frameHash(page) {
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  return canvas(page).evaluate((source) => {
    const c = document.createElement("canvas");
    c.width = 160;
    c.height = 120;
    const ctx = c.getContext("2d");
    ctx.drawImage(source, 0, 0, 160, 120);
    const data = ctx.getImageData(0, 0, 160, 120).data;
    let hash = 2166136261;
    for (let i = 0; i < data.length; i++)
      hash = Math.imul(hash ^ data[i], 16777619);
    return hash >>> 0;
  });
}
async function selectText(page, start, end) {
  await page
    .getByRole("textbox", { name: "Stamp rich text", exact: true })
    .evaluate(
      (element, { start, end }) => {
        element.focus();
        const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT),
          nodes = [];
        for (let n; (n = walker.nextNode());) nodes.push(n);
        const point = (offset) => {
          for (const node of nodes) {
            if (offset <= node.textContent.length) return [node, offset];
            offset -= node.textContent.length;
          }
          return [nodes.at(-1), nodes.at(-1).textContent.length];
        };
        const range = document.createRange();
        range.setStart(...point(start));
        range.setEnd(...point(end));
        const selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        element.dispatchEvent(new MouseEvent("mouseup", { bubbles: true }));
      },
      { start, end },
    );
}

test("rich stamp text formats a real character range with mixed fonts/sizes, grows geometry and exports real outlines", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, "stamp");
  await page
    .getByRole("textbox", { name: "Stamp rich text", exact: true })
    .fill("ALLOY maker");
  await selectText(page, 6, 11);
  await page
    .getByLabel("Stamp text font", { exact: true })
    .selectOption("DM Sans");
  await page.getByLabel("Stamp text size", { exact: true }).fill("24");
  await page.getByLabel("Stamp text colour", { exact: true }).fill("#00ff00");
  const saved = await stampProject(page),
    layer = saved.layers[0];
  expect(layer.runs.map((r) => [r.text, r.fontFamily, r.fontSize])).toEqual([
    ["ALLOY ", "Space Grotesk", 64],
    ["maker", "DM Sans", 24],
  ]);
  expect(layer.runs[1].color).toBe("#00ff00");
  await selectText(page, 0, 0);
  await page.getByLabel("Stamp text size", { exact: true }).fill("80");
  expect((await stampProject(page)).layers[0].height).toBeGreaterThan(
    layer.height,
  );
  const svg = (
    await downloaded(
      page,
      page
        .locator(".stamp-canvas-toolbar")
        .getByRole("button", { name: "SVG", exact: true }),
    )
  ).toString();
  expect(svg).toContain("<path");
  expect(svg).not.toContain("<text");
  expect(svg).not.toMatch(/@font-face|https?:\/\/(?!www\.w3\.org)/);
  const png = await downloaded(
    page,
    page
      .locator(".stamp-canvas-toolbar")
      .getByRole("button", { name: "PNG", exact: true }),
  );
  expect(png.subarray(0, 8)).toEqual(
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
  );
  await page.screenshot({ path: ".playwright/stamp-rich-text.png" });
  expect(errors).toEqual([]);
});

test("Stamp composes embedded/imported SVG and a raster, rejects executable markup and round-trips editable preset/project data", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, "stamp");
  await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
  await page.getByRole("menuitem", { name: "SVG", exact: true }).click();
  const markup = page.getByRole("textbox", {
    name: "Stamp SVG markup",
    exact: true,
  });
  const safe =
    '<svg viewBox="0 0 100 100"><defs><linearGradient id="ink"><stop offset="0" stop-color="#00ff00"/><stop offset="1" stop-color="#0000ff"/></linearGradient></defs><rect x="10" y="10" width="80" height="80" fill="url(#ink)"/></svg>';
  await markup.fill(safe);
  await page.getByRole("button", { name: "Apply SVG", exact: true }).click();
  const before = await stampProject(page);
  await markup.fill(
    '<svg onload="window.__stampExecuted=1"><script>window.__stampExecuted=1</script></svg>',
  );
  await page.getByRole("button", { name: "Apply SVG", exact: true }).click();
  await expect(page.getByRole("alert")).toContainText(
    /Unsupported|static|not allowed/,
  );
  expect((await stampProject(page)).layers[0].svg).toBe(before.layers[0].svg);
  expect(await page.evaluate(() => window.__stampExecuted)).toBeUndefined();
  await markup.fill(safe);
  await page.getByRole("button", { name: "Apply SVG", exact: true }).click();
  const choosingSVG = page.waitForEvent("filechooser");
  await page.getByRole("button", { name: "Import SVG", exact: true }).click();
  await (
    await choosingSVG
  ).setFiles({
    name: "ring.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg viewBox="0 0 100 100"><circle cx="50" cy="50" r="35" fill="none" stroke="#ff0000" stroke-width="3"/></svg>',
    ),
  });
  const imageBytes = await page.evaluate(() => {
    const c = document.createElement("canvas");
    c.width = 32;
    c.height = 16;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "#00ff00";
    ctx.fillRect(0, 0, 16, 16);
    ctx.fillStyle = "#0000ff";
    ctx.fillRect(16, 0, 16, 16);
    return c.toDataURL("image/png").split(",")[1];
  });
  const choosingImage = page.waitForEvent("filechooser");
  await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
  await page.getByRole("menuitem", { name: "Image", exact: true }).click();
  await (
    await choosingImage
  ).setFiles({
    name: "split.png",
    mimeType: "image/png",
    buffer: Buffer.from(imageBytes, "base64"),
  });
  await expect(
    page.getByRole("button", {
      name: "Select stamp component split.png",
      exact: true,
    }),
  ).toBeVisible();
  const saved = await stampProject(page);
  expect(saved.layers[0].kind).toBe("image");
  expect(saved.layers[0].image.width).toBe(32);
  expect(saved.layers.filter((l) => l.kind === "svg")).toHaveLength(3);
  await page
    .locator(".stamp-inspector-scroll")
    .getByRole("button", { name: "Save stamp preset", exact: true })
    .click();
  await expect(
    page.getByRole("button", {
      name: "Open stamp preset Alloy maker mark",
      exact: true,
    }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Save as new preset", exact: true })
    .click();
  await expect(
    page.getByRole("button", {
      name: "Open stamp preset Alloy maker mark copy",
      exact: true,
    }),
  ).toBeVisible();
  await page.getByLabel("Document actions", { exact: true }).click();
  await page
    .getByRole("button", { name: "New stamp project", exact: true })
    .click();
  await page.getByLabel("Document actions", { exact: true }).click();
  const selecting = page.waitForEvent("filechooser");
  await page
    .getByRole("button", { name: "Open stamp project", exact: true })
    .click();
  await (
    await selecting
  ).setFiles({
    name: "mark.stamp.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(saved)),
  });
  expect((await stampProject(page)).layers).toEqual(saved.layers);
  expect(errors).toEqual([]);
});

test("stamp brush places real visible decals under a regular layer, multiple components under a Decal layer, and persists undo/redo", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const original = await frameHash(page);
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  await surfaceClick(page);
  await expect(canvas(page)).toHaveAttribute("data-decal-count", "1");
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  expect(await frameHash(page)).not.toBe(original);
  let saved = await project(page);
  expect(saved.layers.at(-1).kind).toBe("material");
  expect(saved.layers.at(-1).decals).toHaveLength(1);
  expect(
    Number(await canvas(page).getAttribute("data-decal-triangles")),
  ).toBeGreaterThan(0);
  await page.getByRole("button", { name: "Add layer", exact: true }).click();
  await page.getByRole("menuitem", { name: /^Decal layer/ }).click();
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  await surfaceClick(page, 0.48, 0.52);
  await surfaceClick(page, 0.52, 0.53);
  await expect(canvas(page)).toHaveAttribute("data-decal-count", "3");
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  saved = await project(page);
  expect(saved.layers.find((l) => l.kind === "decal").decals).toHaveLength(2);
  expect(saved.stamps).toHaveLength(1);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(components(await project(page))).toHaveLength(2);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  expect(components(await project(page))).toHaveLength(3);
  await page.reload();
  await expect(canvas(page)).toHaveAttribute("data-decal-count", "3");
  expect(components(await project(page))).toHaveLength(3);
  expect(errors).toEqual([]);
});

test("surface transform placement uses real 3D handles, one undo per drag, and re-anchor preserves an existing component", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Viewport decal transform", exact: true })
    .click();
  await surfaceClick(page);
  await expect(canvas(page)).toHaveAttribute("data-decal-gizmo", "translate");
  const before = await project(page),
    old = components(before)[0],
    box = await canvas(page).boundingBox();
  const x = box.x + box.width * 0.5,
    y = box.y + box.height * 0.5;
  await page.mouse.move(x + 42, y + 7);
  await page.mouse.down();
  await page.mouse.move(x + 78, y + 13, { steps: 8 });
  await page.mouse.up();
  let saved = await project(page);
  expect(components(saved)[0].position).not.toEqual(old.position);
  expect(components(saved)).toHaveLength(1);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(components(await project(page))[0].position).toEqual(old.position);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Re-anchor to surface", exact: true })
    .click();
  await surfaceClick(page, 0.52, 0.52);
  saved = await project(page);
  expect(components(saved)).toHaveLength(1);
  expect(components(saved)[0].id).toBe(old.id);
  await page
    .getByRole("button", { name: "Decal rotate gizmo", exact: true })
    .click();
  await expect(canvas(page)).toHaveAttribute("data-decal-gizmo", "rotate");
  await page
    .getByRole("button", { name: "Decal scale gizmo", exact: true })
    .click();
  await expect(canvas(page)).toHaveAttribute("data-decal-gizmo", "scale");
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  await page.screenshot({ path: ".playwright/decal-placement-tested.png" });
  expect(errors).toEqual([]);
});

test("stamp masks actually change surface pixels; inversion, exact zero opacity, inherited locks and all channel targets are real", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const initial = await frameHash(page);
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  await surfaceClick(page);
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Use decal as mask", exact: true })
    .check();
  await expect(canvas(page)).toHaveAttribute("data-decal-mask-count", "1");
  expect(await frameHash(page)).not.toBe(initial);
  const normal = await frameHash(page);
  await page
    .getByRole("checkbox", { name: "Invert decal mask", exact: true })
    .check();
  expect(await frameHash(page)).not.toBe(normal);
  await page.getByLabel("Decal opacity value", { exact: true }).fill("0");
  await expect(canvas(page)).toHaveAttribute("data-decal-mask-count", "0");
  expect(await frameHash(page)).toBe(initial);
  await page.getByLabel("Decal opacity value", { exact: true }).fill("100");
  await page
    .getByRole("checkbox", { name: "Use decal as mask", exact: true })
    .uncheck();
  for (const channel of PAINT_CHANNELS) {
    const btn = page.getByRole("button", {
      name: `Decal ${channel.label} channel`,
      exact: true,
    });
    if ((await btn.getAttribute("aria-pressed")) !== "true") await btn.click();
  }
  let saved = await project(page);
  expect(components(saved)[0].channels).toHaveLength(14);
  await page.getByLabel("Decal Roughness value", { exact: true }).fill("0");
  await page
    .getByRole("button", { name: "Decal Base color channel", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  await frameHash(page);
  saved = await project(page);
  expect(components(saved)[0].channelValues.roughness).toBe(0);
  expect(components(saved)[0].channels).not.toContain("baseColor");
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await expect(
    page.getByLabel("Decal width value", { exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Viewport stamp brush", exact: true }),
  ).toBeDisabled();
  expect(errors).toEqual([]);
});

for (const width of [1440, 940])
  test(`asset list rows fill the centre pane at ${width}px without changing scene bounds or fixed grid media`, async ({
    page,
  }) => {
    const errors = errorsOf(page);
    await page.setViewportSize({ width, height: 900 });
    await open(page);
    const bounds = await page.locator(".tp-scene-viewport").boundingBox();
    await page
      .getByRole("button", { name: "Open content browser", exact: true })
      .click();
    const browser = page.getByRole("region", {
      name: "Content browser",
      exact: true,
    });
    await browser
      .getByRole("button", { name: /^Brushes \/ instruments/ })
      .click();
    await browser
      .getByRole("button", { name: "Asset list view", exact: true })
      .click();
    const sizes = await browser.locator(".cb-list").evaluate((element) => {
      const style = getComputedStyle(element),
        tile = element.querySelector(".cb-asset-tile"),
        rect = element.getBoundingClientRect(),
        row = tile.getBoundingClientRect();
      return {
        available:
          element.clientWidth -
          parseFloat(style.paddingLeft) -
          parseFloat(style.paddingRight),
        width: row.width,
        left: row.left - rect.left,
        padding: parseFloat(style.paddingLeft),
      };
    });
    expect(Math.abs(sizes.width - sizes.available)).toBeLessThan(1);
    expect(Math.abs(sizes.left - sizes.padding)).toBeLessThan(1);
    expect(await page.locator(".tp-scene-viewport").boundingBox()).toEqual(
      bounds,
    );
    await page.screenshot({
      path: `.playwright/full-width-asset-list-${width}.png`,
    });
    await browser
      .getByRole("button", { name: "Asset grid view", exact: true })
      .click();
    expect(
      await browser
        .locator(".cb-grid>.cb-asset-tile")
        .first()
        .evaluate((e) => e.getBoundingClientRect().height),
    ).toBe(172);
    await browser.getByRole("button", { name: /^Stamp presets/ }).click();
    await expect(
      browser.getByRole("button", {
        name: "Inspect asset Alloy maker mark",
        exact: true,
      }),
    ).toBeVisible();
    await browser
      .getByRole("button", { name: "Stamp on selected layer", exact: true })
      .click();
    await expect(browser).toHaveAttribute("data-expanded", "false", {
      timeout: 10000,
    });
    await surfaceClick(page);
    await expect(canvas(page)).toHaveAttribute("data-decal-count", "1");
    expect(errors).toEqual([]);
  });

test("dedicated layer handles expose before/after/inside drop zones, keep full subtrees, root moves and undo without cycles", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Add layer folder", exact: true })
    .click();
  await page.getByLabel("Layer name", { exact: true }).fill("Stamp group");
  await page.getByLabel("Layer name", { exact: true }).press("Enter");
  const folder = (await project(page)).layers.find(
    (l) => l.name === "Stamp group",
  ).id;
  const row = (id) => page.locator(`[data-layer-id="${id}"]`);
  await page
    .getByRole("button", { name: "Drag Surface detail", exact: true })
    .dragTo(row(folder), { targetPosition: { x: 100, y: 30 } });
  expect(
    (await project(page)).layers.find((l) => l.id === "surface-detail")
      .parentId,
  ).toBe(folder);
  await page
    .getByRole("button", { name: "Drag Surface finish", exact: true })
    .dragTo(row(folder), { targetPosition: { x: 100, y: 57 } });
  let saved = await project(page);
  expect(saved.layers.map((l) => l.id).indexOf("surface-finish")).toBe(
    saved.layers.map((l) => l.id).indexOf("surface-detail") + 1,
  );
  expect(
    saved.layers.find((l) => l.id === "surface-finish").parentId,
  ).toBeNull();
  await page
    .getByRole("button", { name: "Drag Stamp group", exact: true })
    .dragTo(row("surface-detail"));
  expect(
    (await project(page)).layers.find((l) => l.id === folder).parentId,
  ).toBeNull();
  await page
    .getByRole("button", { name: "Drag Surface detail", exact: true })
    .dragTo(row("base-material"), { targetPosition: { x: 100, y: 4 } });
  expect(
    (await project(page)).layers.find((l) => l.id === "surface-detail")
      .parentId,
  ).toBeNull();
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(
    (await project(page)).layers.find((l) => l.id === "surface-detail")
      .parentId,
  ).toBe(folder);
  expect(errors).toEqual([]);
});

test("Stamp workspace keeps left composition/right inspector at desktop, supports phone tabs and cached workspace return", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, "stamp");
  const left = await page.locator(".stamp-left").boundingBox(),
    centre = await page.locator(".stamp-centre").boundingBox(),
    right = await page.locator(".stamp-right").boundingBox();
  expect(left.x + left.width).toBeLessThanOrEqual(centre.x);
  expect(centre.x + centre.width).toBeLessThanOrEqual(right.x);
  await page.getByLabel("Stamp name", { exact: true }).fill("Persistent mark");
  await page.getByLabel("Stamp name", { exact: true }).press("Enter");
  await page
    .getByRole("navigation", { name: "Studio workspace" })
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  await expect(canvas(page)).toHaveAttribute("data-material-ready", "true");
  await page
    .getByRole("navigation", { name: "Studio workspace" })
    .getByRole("button", { name: "Stamp", exact: true })
    .click();
  await expect(page.getByLabel("Stamp name", { exact: true })).toHaveValue(
    "Persistent mark",
  );
  await page.reload();
  await expect(page.getByLabel("Stamp name", { exact: true })).toHaveValue(
    "Persistent mark",
  );
  await page.setViewportSize({ width: 414, height: 896 });
  await page.getByLabel("Document actions", { exact: true }).click();
  for (const name of [
    "Open stamp project",
    "Save stamp project",
    "Use in Texture",
  ])
    await expect(
      page.getByRole("button", { name, exact: true }),
    ).toBeInViewport();
  await page.getByLabel("Document actions", { exact: true }).click();
  await expect(page.locator(".stamp-centre")).toBeVisible();
  await page
    .locator(".stamp-mobile-nav")
    .getByRole("button", { name: "inspector", exact: true })
    .click();
  await expect(page.getByLabel("Stamp name", { exact: true })).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({ path: ".playwright/stamp-phone.png" });
  expect(errors).toEqual([]);
});

test("rich text handles empty edits, literal pasted/dropped markup and rejected oversized input without retaining unsafe DOM", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, "stamp");
  const editor = page.getByRole("textbox", {
    name: "Stamp rich text",
    exact: true,
  });
  await editor.fill("");
  await page
    .getByLabel("Stamp text font", { exact: true })
    .selectOption("DM Sans");
  await page.getByLabel("Stamp text size", { exact: true }).fill("32");
  await editor.fill("Safe text");
  await selectText(page, 0, 9);
  await editor.evaluate((e) => {
    const d = new DataTransfer();
    d.setData(
      "text/html",
      '<img src="missing" onerror="window.__unsafeStamp=1">',
    );
    d.setData("text/plain", "<b>plain</b>");
    e.dispatchEvent(
      new ClipboardEvent("paste", {
        bubbles: true,
        cancelable: true,
        clipboardData: d,
      }),
    );
  });
  let saved = await stampProject(page);
  expect(saved.layers[0].runs.map((r) => r.text).join("")).toBe("<b>plain</b>");
  expect(await editor.locator("img,b,script").count()).toBe(0);
  await selectText(page, 0, 12);
  await editor.evaluate((e) => {
    const d = new DataTransfer();
    d.setData("text/html", "<script>window.__unsafeStamp=1</script>");
    d.setData("text/plain", "literal drop");
    e.dispatchEvent(
      new DragEvent("drop", {
        bubbles: true,
        cancelable: true,
        dataTransfer: d,
      }),
    );
  });
  await expect(editor).toHaveText("literal drop");
  const before = await stampProject(page);
  await editor.fill("x".repeat(1025));
  await expect(editor).toHaveText("literal drop");
  expect((await stampProject(page)).layers[0].runs).toEqual(
    before.layers[0].runs,
  );
  expect(await page.evaluate(() => window.__unsafeStamp)).toBeUndefined();
  await page
    .getByRole("button", { name: "Undo stamp edit", exact: true })
    .click();
  await expect(editor).toHaveText("<b>plain</b>");
  expect(errors).toEqual([]);
});

test("Escape cancels a 3D transform draft and explicit preset replacement retains the selected instance's placement", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page
    .getByRole("button", { name: "Viewport decal transform", exact: true })
    .click();
  await surfaceClick(page);
  await expect(canvas(page)).toHaveAttribute("data-decal-gizmo", "translate");
  const before = await project(page),
    box = await canvas(page).boundingBox(),
    x = box.x + box.width * 0.5,
    y = box.y + box.height * 0.5;
  await page.mouse.move(x + 42, y + 7);
  await page.mouse.down();
  await page.mouse.move(x + 75, y + 11, { steps: 5 });
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect(components(await project(page))).toEqual(components(before));
  const oldPixels = await frameHash(page);
  await page
    .getByRole("button", { name: "Select decal Alloy maker mark", exact: true })
    .click();
  await page.getByRole("button", { name: "Edit artwork", exact: true }).click();
  await expect(
    page.getByRole("textbox", { name: "Stamp rich text", exact: true }),
  ).toBeVisible();
  await page
    .getByRole("textbox", { name: "Stamp rich text", exact: true })
    .fill("EDITED");
  await page
    .getByRole("button", { name: "Use in Texture", exact: true })
    .click();
  await expect(canvas(page)).toHaveAttribute("data-decal-count", "1");
  await page
    .getByRole("button", { name: "Select decal Alloy maker mark", exact: true })
    .click();
  await page
    .getByRole("button", {
      name: "Replace artwork & targets with selected preset",
      exact: true,
    })
    .click();
  const saved = await project(page);
  expect(components(saved)[0].position).toEqual(components(before)[0].position);
  expect(components(saved)[0].id).toBe(components(before)[0].id);
  expect(
    saved.stamps[0].project.layers[0].runs.map((r) => r.text).join(""),
  ).toBe("EDITED");
  expect(saved.stamps).toHaveLength(1);
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  expect(await frameHash(page)).not.toBe(oldPixels);
  expect(errors).toEqual([]);
});
