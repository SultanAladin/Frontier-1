import { test, expect } from "@playwright/test";
import { PAINT_TOOLS } from "../src/paintToolCatalogue.js";
test.describe.configure({ timeout: 150000 });
test.use({ actionTimeout: 20000 });
const row = (page, id) => page.locator(`[data-layer-id="${id}"]`);
const history = (page) =>
  page.getByRole("tabpanel", { name: "History", exact: true });
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
  const wait = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await wait).createReadStream()) chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function source(page) {
  await row(page, "surface-detail")
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await page
    .getByRole("group", { name: "Base color source", exact: true })
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Paint Base color texture", exact: true })
    .click();
  const canvas = page.getByLabel("Base color texture canvas", { exact: true });
  await expect(canvas).toHaveAttribute("aria-disabled", "false");
  await canvas.scrollIntoViewIfNeeded();
  return canvas;
}
async function stroke(page, canvas, reverse = false) {
  await canvas.scrollIntoViewIfNeeded();
  const b = await canvas.boundingBox(),
    points = reverse
      ? [
          [0.2, 0.7],
          [0.35, 0.55],
          [0.55, 0.5],
          [0.8, 0.3],
        ]
      : [
          [0.15, 0.2],
          [0.25, 0.4],
          [0.5, 0.45],
          [0.6, 0.65],
          [0.85, 0.75],
        ];
  await page.mouse.move(
    b.x + b.width * points[0][0],
    b.y + b.height * points[0][1],
  );
  await page.mouse.down();
  for (const [x, y] of points.slice(1))
    await page.mouse.move(b.x + b.width * x, b.y + b.height * y, { steps: 5 });
  await page.mouse.up();
}
async function opacity(page, value) {
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
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
  await menu
    .getByRole("button", {
      name: "Select paint tool Classic Gold-Nib Fountain",
      exact: true,
    })
    .click();
  await menu
    .getByLabel("Paint Opacity value", { exact: true })
    .fill(String(value));
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
}

test("instrument cards have equal circular icon wells, centred artwork and labels entirely below, including phones", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  for (const width of [1440, 940, 390]) {
    await page.setViewportSize({ width, height: 900 });
    await page
      .getByRole("button", { name: "Open paint tool menu", exact: true })
      .click();
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
    const cards = menu.locator(".paint-instrument-card");
    await expect(cards).toHaveCount(10);
    for (const card of await cards.all()) {
      const portrait = card.locator(".paint-instrument-portrait"),
        icon = portrait.locator("img"),
        label = card.locator(".paint-instrument-label");
      const [p, i, l] = await Promise.all([
        portrait.boundingBox(),
        icon.boundingBox(),
        label.boundingBox(),
      ]);
      expect(p.width).toBeCloseTo(p.height, 1);
      expect(i.x + i.width / 2).toBeCloseTo(p.x + p.width / 2, 1);
      expect(i.y + i.height / 2).toBeCloseTo(p.y + p.height / 2, 1);
      expect(l.y).toBeGreaterThan(p.y + p.height);
      expect(l.width).toBeGreaterThan(20);
      expect(
        await portrait.evaluate((e) => getComputedStyle(e).borderRadius),
      ).toBe("50%");
    }
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth > innerWidth,
      ),
    ).toBe(false);
    await page.screenshot({
      path: `.playwright/instruments-circles-${width}.png`,
    });
    await menu
      .getByRole("button", { name: "Close paint tool menu", exact: true })
      .click();
  }
  expect(errors).toEqual([]);
});

test("History is a keyboard-accessible tab behind Inspector, with real edit labels, state inspection and restore/redo", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await page.getByLabel("Layer opacity value", { exact: true }).fill("65");
  await page.getByLabel("Layer opacity value", { exact: true }).fill("40");
  await page.getByLabel("Layer name", { exact: true }).fill("History base");
  await page.getByLabel("Layer name", { exact: true }).press("Enter");
  const tabs = page.getByRole("tablist", {
    name: "Inspector panels",
    exact: true,
  });
  await tabs.getByRole("tab", { name: "Inspector", exact: true }).focus();
  await page.keyboard.press("ArrowRight");
  await expect(
    tabs.getByRole("tab", { name: "History", exact: true }),
  ).toBeFocused();
  await expect(history(page)).toBeVisible();
  await expect(
    history(page).getByText("No recorded stroke", { exact: true }),
  ).toBeVisible();
  await expect(history(page).getByRole("listitem")).toHaveCount(3);
  await expect(
    history(page).getByText("Rename layer", { exact: true }),
  ).toBeVisible();
  await expect(
    history(page).getByText("Base material · 40%", { exact: true }),
  ).toBeVisible();
  const before = await project(page);
  await history(page)
    .getByRole("button", {
      name: "Inspect history 0: New project",
      exact: true,
    })
    .click();
  expect(await project(page)).toEqual(before);
  await history(page)
    .getByRole("button", { name: "Restore this state", exact: true })
    .click();
  expect((await project(page)).layers.at(-1)).toMatchObject({
    name: "Base material",
    opacity: 100,
  });
  await history(page)
    .getByRole("button", { name: "Redo history step", exact: true })
    .click();
  expect((await project(page)).layers.at(-1).opacity).toBe(40);
  await history(page)
    .getByRole("button", { name: "Redo history step", exact: true })
    .click();
  expect((await project(page)).layers.at(-1).name).toBe("History base");
  await page.screenshot({ path: ".playwright/texture-history-edits.png" });
  await tabs.getByRole("tab", { name: "Inspector", exact: true }).click();
  await expect(page.getByLabel("Layer name", { exact: true })).toHaveValue(
    "History base",
  );
  expect(errors).toEqual([]);
});

test("real source strokes update the layer texture tile and Last stroke preview, with independent undo/redo and reload", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await expect(
    row(page, "surface-detail").getByRole("img", {
      name: "Surface detail empty texture",
      exact: true,
    }),
  ).toBeVisible();
  const canvas = await source(page);
  await stroke(page, canvas);
  let doc = await project(page),
    first = doc.layers[0].channelSettings.baseColor.texture,
    firstStroke = doc.lastStroke;
  expect(first).toMatchObject({ width: 256, height: 256, origin: "paint" });
  expect(firstStroke.layerId).toBe("surface-detail");
  expect(firstStroke.points.length).toBeGreaterThan(5);
  await expect(
    row(page, "surface-detail").getByRole("img", {
      name: "Surface detail texture preview",
      exact: true,
    }),
  ).toHaveAttribute("src", first.dataUrl);
  await opacity(page, 50);
  await stroke(page, canvas, true);
  doc = await project(page);
  const second = doc.layers[0].channelSettings.baseColor.texture,
    secondStroke = doc.lastStroke;
  expect(second.dataUrl).not.toBe(first.dataUrl);
  expect(secondStroke.id).not.toBe(firstStroke.id);
  expect(secondStroke.settings.params.opacity).toBe(50);
  expect(secondStroke.image.dataUrl).not.toBe(second.dataUrl);
  await page.getByRole("tab", { name: "History", exact: true }).click();
  await expect(
    history(page).getByRole("img", {
      name: "Last recorded paint stroke",
      exact: true,
    }),
  ).toHaveAttribute("src", secondStroke.image.dataUrl);
  await history(page)
    .getByRole("button", { name: "Strokes", exact: true })
    .click();
  await expect(history(page).getByRole("listitem")).toHaveCount(2);
  await page.screenshot({
    path: ".playwright/texture-history-last-stroke.png",
  });
  await history(page)
    .getByRole("button", { name: "Undo history step", exact: true })
    .click();
  doc = await project(page);
  expect(doc.lastStroke.id).toBe(firstStroke.id);
  expect(doc.layers[0].channelSettings.baseColor.texture).toEqual(first);
  await expect(
    row(page, "surface-detail").getByRole("img", {
      name: "Surface detail texture preview",
      exact: true,
    }),
  ).toHaveAttribute("src", first.dataUrl);
  await history(page)
    .getByRole("button", { name: "Redo history step", exact: true })
    .click();
  expect((await project(page)).lastStroke.id).toBe(secondStroke.id);
  await page.reload();
  await page.getByRole("tab", { name: "History", exact: true }).click();
  await expect(
    history(page).getByRole("img", {
      name: "Last recorded paint stroke",
      exact: true,
    }),
  ).toHaveAttribute("src", secondStroke.image.dataUrl);
  await expect(
    row(page, "surface-detail").getByRole("img", {
      name: "Surface detail texture preview",
      exact: true,
    }),
  ).toHaveAttribute("src", second.dataUrl);
  expect(errors).toEqual([]);
});

test("zero-opacity/cancelled source gestures create no stroke states and inherited folder locks block late pointer commits", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const canvas = await source(page);
  await opacity(page, 0);
  const before = await project(page);
  await stroke(page, canvas);
  expect(await project(page)).toEqual(before);
  await opacity(page, 100);
  await canvas.scrollIntoViewIfNeeded();
  let b = await canvas.boundingBox();
  await page.mouse.move(b.x + b.width * 0.2, b.y + b.height * 0.3);
  await page.mouse.down();
  await page.mouse.move(b.x + b.width * 0.7, b.y + b.height * 0.7, {
    steps: 8,
  });
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect((await project(page)).lastStroke).toBe(null);
  await page
    .getByRole("button", { name: "Add layer folder", exact: true })
    .click();
  const group = (await project(page)).layers.find((l) => l.kind === "folder");
  await row(page, "surface-detail")
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await page.getByLabel("Layer folder", { exact: true }).selectOption(group.id);
  await page
    .getByRole("button", { name: "Paint Base color texture", exact: true })
    .click();
  const active = page.getByLabel("Base color texture canvas", { exact: true });
  await active.scrollIntoViewIfNeeded();
  b = await active.boundingBox();
  await page.mouse.move(b.x + b.width * 0.2, b.y + b.height * 0.2);
  await page.mouse.down();
  await page.mouse.move(b.x + b.width * 0.8, b.y + b.height * 0.8, {
    steps: 8,
  });
  await row(page, group.id)
    .getByRole("button", { name: "Lock Folder", exact: true })
    .evaluate((button) => button.click());
  await expect(active).toHaveAttribute("aria-disabled", "true");
  await page.mouse.up();
  expect((await project(page)).lastStroke).toBe(null);
  expect(errors).toEqual([]);
});

test("source erase is a real undoable pixel operation and phone History tab preserves the single-dock layout", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const canvas = await source(page);
  await stroke(page, canvas);
  const painted = await project(page);
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
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
  await menu.getByRole("button", { name: /^Erasers/ }).click();
  const eraser = PAINT_TOOLS.find((t) => t.key === "eraser");
  await menu
    .getByRole("button", {
      name: `Select paint tool ${eraser.label}`,
      exact: true,
    })
    .click();
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  await stroke(page, canvas);
  const erased = await project(page);
  expect(erased.lastStroke.operation).toBe("erase");
  expect(erased.layers[0].channelSettings.baseColor.texture.dataUrl).not.toBe(
    painted.layers[0].channelSettings.baseColor.texture.dataUrl,
  );
  await page.getByRole("tab", { name: "History", exact: true }).click();
  await expect(
    history(page).getByRole("img", {
      name: "Last recorded erase footprint",
      exact: true,
    }),
  ).toBeVisible();
  await page.setViewportSize({ width: 390, height: 844 });
  const mobile = page.getByRole("tablist", {
    name: "Left dock panels",
    exact: true,
  });
  await mobile.getByRole("tab", { name: "History", exact: true }).click();
  await expect(history(page)).toBeVisible();
  await expect(
    page.getByRole("complementary", { name: "Layer stack", exact: true }),
  ).toBeHidden();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth > innerWidth,
    ),
  ).toBe(false);
  await history(page)
    .getByRole("button", { name: "Undo history step", exact: true })
    .click();
  expect(
    (await project(page)).layers[0].channelSettings.baseColor.texture,
  ).toEqual(painted.layers[0].channelSettings.baseColor.texture);
  await page.screenshot({ path: ".playwright/texture-history-phone.png" });
  await mobile.getByRole("tab", { name: "Inspector", exact: true }).click();
  await expect(page.getByLabel("Layer name", { exact: true })).toBeVisible();
  await mobile.getByRole("tab", { name: "Layers", exact: true }).click();
  await expect(row(page, "surface-detail")).toBeVisible();
  expect(errors).toEqual([]);
});
