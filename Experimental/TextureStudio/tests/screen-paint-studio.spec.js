import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
} from "../src/textureDocument.js";
import { normalizeDigitalBrush } from "../src/digitalBrush.js";
test.use({ deviceScaleFactor: 2 });
test.describe.configure({ timeout: 600000 });
const STORAGE = "alloy-texture-project-v1";
async function open(page, { size = 48 } = {}) {
  const d = createTextureDocument();
  d.layers = [
    createTextureLayer("paint", {
      id: "screen",
      name: "SCREEN FOOTPRINT",
      channels: ["baseColor"],
    }),
  ];
  d.digitalPainting = normalizeDigitalBrush({
    tool: "paint",
    size,
    color: "#ff1030",
    hardness: 1,
    opacity: 1,
    flow: 1,
    spacing: 0.1,
    sizePressure: 0,
    opacityPressure: 0,
  });
  await page.addInitScript((d) => {
    if (localStorage.getItem("screen-paint-seeded")) return;
    localStorage.setItem("screen-paint-seeded", "yes");
    localStorage.setItem("alloy-texture-project-v1", JSON.stringify(d));
    localStorage.setItem(
      "alloy-texture-authoring-v1",
      JSON.stringify({
        selection: { id: "screen", mask: false },
        preview: "material",
        tool: "navigate",
      }),
    );
  }, d);
  await page.goto("/?studio=texture");
  const canvas = page
    .locator(".tp-scene-viewport > .canvas-host > canvas")
    .first();
  await expect(canvas).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
  await expect(canvas).toHaveAttribute("data-layer-atlas-ready", "true", {
    timeout: 120000,
  });
  await page
    .getByRole("button", { name: "Paint layer channels", exact: true })
    .click();
  return canvas;
}
async function doc(page) {
  return page.evaluate((s) => JSON.parse(localStorage.getItem(s)), STORAGE);
}
async function overlayBox(canvas) {
  return canvas.evaluate((c) => {
    const a = c.getContext("2d").getImageData(0, 0, c.width, c.height).data,
      r = c.getBoundingClientRect();
    let x0 = c.width,
      y0 = c.height,
      x1 = -1,
      y1 = -1;
    for (let y = 0; y < c.height; y++)
      for (let x = 0; x < c.width; x++)
        if (a[(y * c.width + x) * 4 + 3]) {
          x0 = Math.min(x0, x);
          x1 = Math.max(x1, x);
          y0 = Math.min(y0, y);
          y1 = Math.max(y1, y);
        }
    return {
      box: [
        (x0 * r.width) / c.width,
        (y0 * r.height) / c.height,
        ((x1 - x0 + 1) * r.width) / c.width,
        ((y1 - y0 + 1) * r.height) / c.height,
      ],
      cursor: JSON.parse(c.dataset.brushCursor || "null"),
    };
  });
}
async function uvPoint(canvas, uv) {
  return canvas.evaluate((c, [u, v]) => {
    const r = c.getBoundingClientRect(),
      camera = JSON.parse(c.dataset.uvCamera),
      size = {
        width: c.width / Math.min(devicePixelRatio, 2),
        height: c.height / Math.min(devicePixelRatio, 2),
      };
    return {
      x: r.x + ((camera.x + u * camera.scale) * r.width) / size.width,
      y: r.y + ((camera.y - v * camera.scale) * r.height) / size.height,
    };
  }, uv);
}
async function redScore(canvas, p) {
  return canvas.evaluate((source, p) => {
    const r = source.getBoundingClientRect(),
      c = document.createElement("canvas");
    c.width = source.width;
    c.height = source.height;
    const ctx = c.getContext("2d");
    ctx.drawImage(source, 0, 0);
    const x = Math.round(((p.x - r.x) * c.width) / r.width),
      y = Math.round(((p.y - r.y) * c.height) / r.height),
      d = ctx.getImageData(x - 1, y - 1, 3, 3).data;
    let score = 0;
    for (let i = 0; i < d.length; i += 4)
      score += d[i] - (d[i + 1] + d[i + 2]) / 2;
    return score / 9;
  }, p);
}
test("Lit XY preview and deposited paint are circles despite distorted teapot UVs; saved projection survives undo, reload and navigation", async ({
  page,
}, info) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  const canvas = await open(page),
    b = await canvas.boundingBox(),
    p = { x: b.x + b.width * 0.5, y: b.y + b.height * 0.49 };
  await page.mouse.move(p.x, p.y);
  const overlay = page.locator(".tp-scene-viewport .digital-screen-cursor");
  await expect(overlay).toHaveAttribute(
    "data-brush-cursor-label",
    "Paint · 48.0 px",
  );
  const box = await overlayBox(overlay);
  expect(box.cursor.space).toBe("screen");
  expect(Math.abs(box.box[2] - box.box[3])).toBeLessThan(1);
  expect(box.box[2]).toBeGreaterThan(46);
  expect(box.box[2]).toBeLessThan(51);
  expect(box.box[0] + box.box[2] / 2).toBeCloseTo(p.x - b.x, 0);
  expect(box.box[1] + box.box[3] / 2).toBeCloseTo(p.y - b.y, 0);
  expect((await doc(page)).layers[0].paint).toBeUndefined();
  await page.screenshot({
    path: info.outputPath("lit-round-screen-preview.png"),
  });
  await page.mouse.click(p.x, p.y);
  await page.mouse.move(100, 120);
  await expect
    .poll(async () => redScore(canvas, p), { timeout: 120000 })
    .toBeGreaterThan(90);
  const circle = await canvas.evaluate((source, p) => {
    const r = source.getBoundingClientRect(),
      c = document.createElement("canvas");
    c.width = source.width;
    c.height = source.height;
    const ctx = c.getContext("2d");
    ctx.drawImage(source, 0, 0);
    const sx = c.width / r.width,
      sy = c.height / r.height,
      cx = (p.x - r.x) * sx,
      cy = (p.y - r.y) * sy;
    const x0 = Math.floor(cx - 45 * sx),
      y0 = Math.floor(cy - 45 * sy),
      w = Math.round(90 * sx),
      h = Math.round(90 * sy),
      a = ctx.getImageData(x0, y0, w, h).data;
    let minX = w,
      minY = h,
      maxX = -1,
      maxY = -1;
    for (let y = 0; y < h; y++)
      for (let x = 0; x < w; x++) {
        const i = (y * w + x) * 4;
        if (a[i] - (a[i + 1] + a[i + 2]) / 2 > 85) {
          minX = Math.min(minX, x);
          maxX = Math.max(maxX, x);
          minY = Math.min(minY, y);
          maxY = Math.max(maxY, y);
        }
      }
    return {
      width: (maxX - minX + 1) / sx,
      height: (maxY - minY + 1) / sy,
      center: [
        (minX + maxX + 1) / 2 / sx + x0 / sx,
        (minY + maxY + 1) / 2 / sy + y0 / sy,
      ],
    };
  }, p);
  expect(Math.abs(circle.width - circle.height)).toBeLessThan(5);
  expect(circle.width).toBeGreaterThan(41);
  expect(circle.width).toBeLessThan(55);
  expect(Math.abs(circle.center[0] - (p.x - b.x))).toBeLessThan(2);
  expect(Math.abs(circle.center[1] - (p.y - b.y))).toBeLessThan(2);
  const op = (await doc(page)).layers[0].paint.operations[0];
  expect(op.region).toEqual({ kind: "surface" });
  expect(op.projection.kind).toBe("lit");
  expect(op.points[0][0]).toBeCloseTo((p.x - b.x) / 256, 5);
  expect(op.points[0][1]).toBeCloseTo((p.y - b.y) / 256, 5);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => redScore(canvas, p), { timeout: 120000 })
    .toBeLessThan(10);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => redScore(canvas, p), { timeout: 120000 })
    .toBeGreaterThan(90);
  await page.screenshot({
    path: info.outputPath("lit-round-deposited-before-reload.png"),
  });
  await page.reload();
  await expect(canvas).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
  await expect(canvas).toHaveAttribute("data-layer-atlas-ready", "true", {
    timeout: 120000,
  });
  await expect
    .poll(async () => redScore(canvas, p), { timeout: 120000 })
    .toBeGreaterThan(90);
  await page.screenshot({
    path: info.outputPath("lit-round-after-reload.png"),
  });
  expect((await doc(page)).layers[0].paint.operations[0].projection).toEqual(
    op.projection,
  );
  await page.screenshot({
    path: info.outputPath("lit-round-deposited-paint.png"),
  });
  await page
    .getByRole("button", { name: "Paint layer channels", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Zoom in teapot", exact: true })
    .click();
  await page.mouse.move(p.x, p.y);
  await expect(overlay).toHaveAttribute(
    "data-brush-cursor-label",
    "Paint · 48.0 px",
  );
  const zoomPreview = await overlayBox(overlay);
  expect(Math.abs(zoomPreview.box[2] - box.box[2])).toBeLessThan(1);
  expect(Math.abs(zoomPreview.box[3] - box.box[3])).toBeLessThan(1);
  expect((await doc(page)).layers[0].paint.operations[0].projection).toEqual(
    op.projection,
  );
  expect(errors).toEqual([]);
});
test("UV chart-gap centre still paints both neighbouring UDIM tiles and the XY tip stays circular at zoom and CSS scaling", async ({
  page,
}, info) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page, { size: 64 });
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  const canvas = page.locator(".tp-uv-canvas");
  await expect(canvas).toHaveAttribute(
    "data-layer-atlas",
    "ordered-lit-appearance",
    { timeout: 120000 },
  );
  const p = await uvPoint(canvas, [1, 0.56]),
    left = await uvPoint(canvas, [0.94, 0.56]),
    right = await uvPoint(canvas, [1.06, 0.56]);
  await page.mouse.move(p.x, p.y);
  const overlay = page.locator(".digital-uv-cursor");
  await expect(overlay).toHaveAttribute(
    "data-brush-cursor-label",
    "Paint · 64.0 px",
  );
  const box = await overlayBox(overlay);
  expect(box.cursor.space).toBe("screen");
  expect(Math.abs(box.box[2] - box.box[3])).toBeLessThan(1);
  expect(box.box[2]).toBeGreaterThan(62);
  await page.screenshot({
    path: info.outputPath("uv-unclipped-chart-gap-preview.png"),
  });
  await page.mouse.click(p.x, p.y);
  await page.mouse.move(100, 120);
  for (const q of [left, right])
    await expect
      .poll(async () => redScore(canvas, q), { timeout: 120000 })
      .toBeGreaterThan(60);
  const operations = (await doc(page)).layers[0].paint.operations;
  expect(operations).toHaveLength(1);
  expect(operations[0].region).toEqual({ kind: "surface" });
  expect(operations[0].projection.kind).toBe("uv");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  for (const q of [left, right])
    await expect
      .poll(async () => redScore(canvas, q), { timeout: 120000 })
      .toBeLessThan(10);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  for (const q of [left, right])
    await expect
      .poll(async () => redScore(canvas, q), { timeout: 120000 })
      .toBeGreaterThan(60);
  await page.getByRole("button", { name: "Zoom in UVs" }).click();
  const q = await uvPoint(canvas, [1, 0.56]);
  await page.mouse.move(q.x, q.y);
  await expect
    .poll(
      async () =>
        JSON.parse((await overlay.getAttribute("data-brush-cursor")) || "null")
          ?.screenPosition[0],
    )
    .toBeCloseTo(q.x - (await canvas.boundingBox()).x, 4);
  const zoom = await overlayBox(overlay);
  expect(zoom.box[2]).toBeCloseTo(box.box[2], 0);
  expect(zoom.box[3]).toBeCloseTo(box.box[3], 0);
  await page.locator(".tp-uv-view").evaluate((el) => {
    el.style.transformOrigin = "0 0";
    el.style.transform = "scale(.8)";
  });
  const scaled = await uvPoint(canvas, [1, 0.56]);
  await page.mouse.move(scaled.x, scaled.y);
  await page.waitForTimeout(150);
  const css = await overlayBox(overlay);
  // Alpha bounding boxes quantize to physical pixels. A fractional CSS
  // position at DPR 2 can add one edge pixel without changing tip geometry.
  expect(css.cursor.dab.radius).toBe(box.cursor.dab.radius);
  expect(css.cursor.brush.roundness).toBe(1);
  expect(Math.abs(css.box[2] - box.box[2])).toBeLessThan(1);
  expect(Math.abs(css.box[3] - box.box[3])).toBeLessThan(1);
  expect(errors).toEqual([]);
});
