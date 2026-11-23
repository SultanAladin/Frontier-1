import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
} from "../src/textureDocument.js";
import { normalizeDigitalBrush } from "../src/digitalBrush.js";
const native = (page) =>
  page.locator(".tp-scene-viewport > .canvas-host > canvas").first();
const uv = (page) =>
  page.getByLabel("UV texture-space canvas", { exact: true });
const footprint = (page) => page.locator(".digital-uv-cursor");
test.describe.configure({ timeout: 600000 });
test.use({ actionTimeout: 30000 });
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
async function open(page, { brush = {}, mask = false } = {}) {
  const doc = createTextureDocument();
  doc.layers = [
    createTextureLayer("paint", {
      id: "ink",
      name: "Cursor ink",
      channels: ["baseColor"],
      ...(mask ? { mask: { kind: "fill", fill: 1 } } : {}),
    }),
  ];
  doc.digitalPainting = normalizeDigitalBrush({
    size: 6,
    color: "#ff1030",
    hardness: 1,
    sizePressure: 0,
    spacing: 0.05,
    ...brush,
  });
  await page.addInitScript(
    ({ doc, mask }) => {
      localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc));
      localStorage.setItem(
        "alloy-texture-authoring-v1",
        JSON.stringify({
          selection: { id: "ink", mask },
          preview: mask ? "mask" : "material",
          tool: "navigate",
        }),
      );
    },
    { doc, mask },
  );
  await page.goto("/?studio=texture");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
  await expect(native(page)).toHaveAttribute("data-layer-atlas-ready", "true", {
    timeout: 120000,
  });
  await page
    .getByRole("button", {
      name: mask ? "Paint mask digitally" : "Paint layer channels",
      exact: true,
    })
    .click();
}
const stored = (page) =>
  page.evaluate(() =>
    JSON.parse(localStorage.getItem("alloy-texture-project-v1")),
  );
const cursor = async (canvas) => {
  const s = await canvas.getAttribute("data-brush-cursor");
  return s ? JSON.parse(s) : null;
};
async function bodyPoint(page) {
  const b = await native(page).boundingBox();
  return { x: b.x + b.width * 0.5, y: b.y + b.height * 0.49 };
}
async function sample(canvas, p) {
  return canvas.evaluate((c, p) => {
    const r = c.getBoundingClientRect(),
      out = document.createElement("canvas");
    out.width = c.width;
    out.height = c.height;
    const ctx = out.getContext("2d");
    ctx.drawImage(c, 0, 0);
    const x = Math.floor(((p.x - r.left) * c.width) / r.width),
      y = Math.floor(((p.y - r.top) * c.height) / r.height);
    const data = ctx.getImageData(x - 1, y - 1, 3, 3).data;
    const avg = [0, 0, 0, 0];
    for (let i = 0; i < data.length; i++) avg[i % 4] += data[i] / 9;
    return avg;
  }, p);
}
async function redScore(canvas, p) {
  const rgba = await sample(canvas, p);
  return rgba[0] - rgba[1];
}
async function hover(page, canvas, p) {
  await page.mouse.move(p.x, p.y);
  await expect.poll(() => cursor(canvas), { timeout: 120000 }).not.toBeNull();
  return cursor(canvas);
}
async function hide(page, canvas = native(page)) {
  await page.mouse.move(20, 20);
  await expect(canvas).toHaveAttribute("data-brush-cursor", "", {
    timeout: 120000,
  });
}
async function screenUV(canvas, p) {
  return canvas.evaluate((c, p) => {
    const camera = JSON.parse(c.dataset.uvCamera),
      r = c.getBoundingClientRect(),
      dpr = Math.min(devicePixelRatio, 2);
    return {
      x:
        r.left + ((camera.x + p[0] * camera.scale) * r.width) / (c.width / dpr),
      y:
        r.top +
        ((camera.y - p[1] * camera.scale) * r.height) / (c.height / dpr),
    };
  }, p);
}
async function overlayStats(canvas) {
  return canvas.evaluate((c) => {
    const data = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    let count = 0,
      low = 0,
      high = 0,
      x0 = c.width,
      x1 = -1;
    for (let y = 0; y < c.height; y++)
      for (let x = 0; x < c.width; x++) {
        const i = (y * c.width + x) * 4,
          a = data[i + 3];
        if (a) {
          count++;
          x0 = Math.min(x0, x);
          x1 = Math.max(x1, x);
        }
        if (data[i] > data[i + 1] + 100) {
          if (a > 70 && a < 130) high++;
          if (a > 10 && a < 35) low++;
        }
      }
    return { count, low, high, width: x1 - x0 };
  });
}

test.describe("high-DPI Lit rendering", () => {
  test.use({ deviceScaleFactor: 2 });
  test("Lit footprint is non-destructive; small dabs, stabilized moving endpoints and resized/zoomed painting land under the real mouse pixels", async ({
    page,
  }) => {
    const errors = errorsOf(page);
    await open(page, { brush: { stabilization: 1 } });
    const c = native(page),
      start = await bodyPoint(page),
      atlasHash = await c.getAttribute("data-layer-atlas-hash");
    const first = await hover(page, c, start);
    expect(first.space).toBe("screen");
    expect(first.dab.x).toBeCloseTo(first.screenPosition[0], 10);
    expect(first.dab.y).toBeCloseTo(first.screenPosition[1], 10);
    await expect
      .poll(() => redScore(c, start), { timeout: 120000 })
      .toBeLessThan(10);
    await expect(page.locator(".digital-screen-cursor")).toBeVisible();
    expect((await stored(page)).layers[0].paint).toBeUndefined();
    expect((await stored(page)).lastDigitalStroke).toBeNull();
    expect(await c.getAttribute("data-layer-atlas-hash")).toBe(atlasHash);
    await page.mouse.click(start.x, start.y);
    await hide(page, c);
    await expect
      .poll(() => redScore(c, start), { timeout: 120000 })
      .toBeGreaterThan(100);
    await page
      .getByRole("button", { name: "Undo layer edit", exact: true })
      .click();
    await expect
      .poll(() => redScore(c, start), { timeout: 120000 })
      .toBeLessThan(10);
    const end = { x: start.x + 24, y: start.y + 14 },
      predicted = await hover(page, c, end);
    await page.mouse.move(start.x, start.y);
    await page.mouse.down();
    await page.mouse.move(end.x, end.y, { steps: 4 });
    await page.mouse.up();
    await hide(page, c);
    await expect
      .poll(() => redScore(c, end), { timeout: 120000 })
      .toBeGreaterThan(100);
    await expect
      .poll(async () => (await stored(page)).layers[0].paint?.operations.length)
      .toBe(1);
    const op = (await stored(page)).layers[0].paint.operations[0];
    expect(op.points.at(-1)[0]).toBeCloseTo(
      predicted.screenPosition[0] / 256,
      4,
    );
    expect(op.points.at(-1)[1]).toBeCloseTo(
      predicted.screenPosition[1] / 256,
      4,
    );
    expect(op.brush.stabilization).toBe(1);
    await page
      .getByRole("button", { name: "Undo layer edit", exact: true })
      .click();
    await expect
      .poll(() => redScore(c, end), { timeout: 120000 })
      .toBeLessThan(10);
    await page
      .getByRole("button", { name: "Redo layer edit", exact: true })
      .click();
    await expect
      .poll(() => redScore(c, end), { timeout: 120000 })
      .toBeGreaterThan(100);
    await page.setViewportSize({ width: 1280, height: 820 });
    const resized = await bodyPoint(page);
    await page.mouse.move(resized.x, resized.y);
    await page.mouse.wheel(0, -70);
    await expect
      .poll(async () => Number(await c.getAttribute("data-zoom")))
      .toBeGreaterThan(100);
    // Use a fresh rendered cursor hit after the resize/zoom, not stale coordinates.
    await expect.poll(async () => (await cursor(c))?.dab.radius).toBe(3);
    await page.mouse.click(resized.x, resized.y);
    await hide(page, c);
    await expect
      .poll(() => redScore(c, resized), { timeout: 120000 })
      .toBeGreaterThan(90);
    expect(errors).toEqual([]);
  });
});

test.describe("scaled high-DPI UV canvas", () => {
  test.use({ deviceScaleFactor: 2 });
  test("grain/rotation/size preview updates during hover, zoom and gestures; UV deposition matches CSS-scaled client coordinates", async ({
    page,
  }) => {
    const errors = errorsOf(page);
    await open(page, {
      brush: {
        size: 48,
        shape: "square",
        roundness: 0.65,
        rotation: 30,
        grain: "checker",
        grainAmount: 1,
        grainScale: 8,
      },
    });
    await page
      .getByLabel("Viewport shading", { exact: true })
      .selectOption("uv");
    await expect(uv(page)).toHaveAttribute(
      "data-layer-atlas",
      "ordered-lit-appearance",
      { timeout: 120000 },
    );
    await page.locator(".tp-uv-view").evaluate((host) => {
      host.style.transform = "scale(.8)";
      host.style.transformOrigin = "0 0";
    });
    const c = uv(page),
      preview = footprint(page),
      p = [0.125, 0.375],
      point = await screenUV(c, p);
    const base = await sample(c, point),
      before = await hover(page, preview, point);
    expect(before.uvPosition[0]).toBeCloseTo(p[0], 4);
    expect(before.uvPosition[1]).toBeCloseTo(p[1], 4);
    expect(before.dab.radius).toBe(24);
    expect(before.brush.grain).toBe("checker");
    expect(before.dab.angle).toBeCloseTo(Math.PI / 6);
    const first = await overlayStats(preview);
    expect(first.high).toBeGreaterThan(10);
    expect(first.low).toBeGreaterThan(10);
    expect(await sample(c, point)).toEqual(base);
    expect((await stored(page)).layers[0].paint).toBeUndefined();
    const camera = JSON.parse(await c.getAttribute("data-uv-camera"));
    await page.mouse.wheel(0, -120);
    await expect
      .poll(
        async () => JSON.parse(await c.getAttribute("data-uv-camera")).scale,
      )
      .toBeGreaterThan(camera.scale);
    await expect
      .poll(async () => (await overlayStats(preview)).width)
      .toBe(first.width);
    const zoomedPoint = await screenUV(c, p);
    await hover(page, preview, zoomedPoint);
    await page.mouse.down();
    const endUV = [p[0] + 0.015, p[1] + 0.008],
      end = await screenUV(c, endUV);
    await page.mouse.move(end.x, end.y, { steps: 3 });
    await expect
      .poll(async () => (await cursor(preview))?.uvPosition[0])
      .toBeCloseTo(endUV[0], 4);
    await page.mouse.up();
    await hide(page, preview);
    await expect
      .poll(async () => (await stored(page)).layers[0].paint?.operations.length)
      .toBe(1);
    const op = (await stored(page)).layers[0].paint.operations[0];
    const rect = await c.boundingBox();
    expect(op.projection.kind).toBe("uv");
    expect(op.region).toEqual({ kind: "surface" });
    expect(op.points[0][0]).toBeCloseTo((zoomedPoint.x - rect.x) / 256, 4);
    expect(op.points[0][1]).toBeCloseTo((zoomedPoint.y - rect.y) / 256, 4);
    expect(op.points.at(-1)[0]).toBeCloseTo((end.x - rect.x) / 256, 4);
    expect(op.points.at(-1)[1]).toBeCloseTo((end.y - rect.y) / 256, 4);
    await expect
      .poll(() => redScore(c, zoomedPoint), { timeout: 120000 })
      .toBeGreaterThan(35);
    await page
      .getByRole("button", { name: "Erase layer channels", exact: true })
      .click();
    const eraser = await hover(page, preview, zoomedPoint);
    expect(eraser.kind).toBe("erase");
    expect(eraser.dab.color).toBe("#d5dce2");
    await page.mouse.click(zoomedPoint.x, zoomedPoint.y);
    await hide(page, preview);
    await expect
      .poll(
        async () =>
          (await stored(page)).layers[0].paint?.operations.at(-1).kind,
      )
      .toBe("erase");
    await page
      .getByRole("button", { name: "Paint layer channels", exact: true })
      .click();
    await hover(page, preview, zoomedPoint);
    await page.mouse.down();
    await page.mouse.move(end.x + 2, end.y + 2);
    await page.keyboard.press("Escape");
    await page.mouse.up();
    await expect(preview).toHaveAttribute("data-brush-cursor", "");
    expect((await stored(page)).layers[0].paint.operations).toHaveLength(2);
    // Cancellation suppresses the stale release, not future genuine hovering.
    await page.mouse.move(end.x + 4, end.y + 4);
    await expect
      .poll(
        async () =>
          JSON.parse(
            (await preview.getAttribute("data-brush-cursor")) || "null",
          )?.kind,
      )
      .toBe("paint");
    expect((await stored(page)).layers[0].paint.operations).toHaveLength(2);
    expect(errors).toEqual([]);
  });
});

test("mask footprints respect black ink, cancellation and locks; fill previews the hit component and erasing restores the original mask", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, { mask: true, brush: { size: 20 } });
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  const c = native(page),
    p = await bodyPoint(page);
  const black = await hover(page, c, p);
  expect(black.mask).toBe(true);
  expect(black.dab.color).toBe("#000000");
  await page.mouse.down();
  await page.mouse.move(p.x + 12, p.y + 5, { steps: 2 });
  await page.keyboard.press("Escape");
  await page.mouse.up();
  await expect(c).toHaveAttribute("data-brush-cursor", "");
  expect((await stored(page)).layers[0].mask.paint).toBeUndefined();
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  await page.mouse.move(p.x, p.y);
  await expect(c).toHaveAttribute("data-brush-cursor", "");
  await page.mouse.click(p.x, p.y);
  expect((await stored(page)).layers[0].mask.paint).toBeUndefined();
  await page
    .getByRole("button", { name: "Unlock selected layer", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Fill mask digitally", exact: true })
    .click();
  const fill = await hover(page, c, p);
  expect(fill.kind).toBe("fill");
  expect(fill.dab).toBeNull();
  expect(fill.region).toEqual({ kind: "object", partId: "body" });
  expect(fill.color).toBe("#000000");
  await page.mouse.click(p.x, p.y);
  await hide(page, c);
  await expect
    .poll(
      async () => (await stored(page)).layers[0].mask.paint?.operations.length,
    )
    .toBe(1);
  await expect
    .poll(async () => (await sample(c, p))[0], { timeout: 120000 })
    .toBeLessThan(40);
  await page
    .getByRole("button", { name: "Erase mask digitally", exact: true })
    .click();
  const eraser = await hover(page, c, p);
  expect(eraser.mask).toBe(true);
  expect(eraser.kind).toBe("erase");
  expect(eraser.dab.color).toBe("#d5dce2");
  await page.mouse.click(p.x, p.y);
  await hide(page, c);
  await expect
    .poll(async () => (await sample(c, p))[0], { timeout: 120000 })
    .toBeGreaterThan(170);
  const doc = await stored(page);
  expect(doc.layers[0].mask.fill).toBe(1);
  expect(doc.layers[0].mask.paint.operations.at(-1).digital.operation).toBe(
    "erase",
  );
  expect(errors).toEqual([]);
});

// CDP sends real browser pen/touch input, including native pointer capture and
// lift-off pressure. Unrelated-pointer events below are deliberate adversarial
// injections, not a stand-in for a real pen contact.
test.describe("pen and touch input", () => {
  test.use({ deviceScaleFactor: 2, hasTouch: true });
  for (const view of ["Lit", "UV"]) {
    test(`${view} pen pressure sizes the real footprint and keeps release ink at the contact; foreign pointer events cannot move/cancel it`, async ({
      page,
      context,
    }, testInfo) => {
      const errors = errorsOf(page);
      await open(page, {
        brush: {
          size: 40,
          sizePressure: 1,
          opacityPressure: 0.5,
          stabilization: 1,
        },
      });
      if (view === "UV")
        await page
          .getByLabel("Viewport shading", { exact: true })
          .selectOption("uv");
      const surface = view === "UV" ? uv(page) : native(page),
        overlay = view === "UV" ? footprint(page) : surface,
        start =
          view === "UV"
            ? await screenUV(surface, [0.125, 0.375])
            : await bodyPoint(page),
        session = await context.newCDPSession(page);
      await surface.evaluate((c) => {
        window.deviceEvents = [];
        for (const type of [
          "pointerdown",
          "pointermove",
          "pointerup",
          "lostpointercapture",
        ])
          // Native painting stops propagation at the host's capture handler;
          // observe before it, including up events retargeted to the real host.
          document.addEventListener(
            type,
            (e) =>
              window.deviceEvents.push({
                type: e.type,
                id: e.pointerId,
                pressure: e.pressure,
                pointerType: e.pointerType,
                primary: e.isPrimary,
                buttons: e.buttons,
              }),
            { capture: true },
          );
      });
      const pen = (type, p, force, buttons = 1) =>
        session.send("Input.dispatchMouseEvent", {
          type,
          x: p.x,
          y: p.y,
          button: buttons ? "left" : "none",
          buttons,
          pointerType: "pen",
          force,
        });
      await pen("mouseMoved", start, 0, 0);
      await expect
        .poll(async () => (await cursor(overlay))?.dab.radius)
        .toBeCloseTo(20, 5);
      await pen("mousePressed", start, 0.3);
      await expect
        .poll(async () => (await cursor(overlay))?.dab.radius)
        .toBeCloseTo(6, 5);
      const mid = { x: start.x + 8, y: start.y + 4 };
      await pen("mouseMoved", mid, 0.8);
      await expect
        .poll(async () => (await cursor(overlay))?.dab.radius)
        .toBeCloseTo(16, 5);
      const before = await cursor(overlay);
      expect(before.dab.pressure).toBeCloseTo(0.8, 5);
      expect(before.dab.opacity).toBeCloseTo(0.9, 5);
      await page.screenshot({
        path: testInfo.outputPath(
          `${view.toLowerCase()}-pressure-footprint.png`,
        ),
      });
      await surface.evaluate((c, p) => {
        c.dispatchEvent(
          new PointerEvent("pointermove", {
            bubbles: true,
            composed: true,
            pointerId: 999,
            isPrimary: true,
            pointerType: "mouse",
            clientX: p.x + 30,
            clientY: p.y + 12,
            pressure: 1,
            buttons: 0,
          }),
        );
        c.dispatchEvent(
          new PointerEvent("lostpointercapture", {
            bubbles: true,
            composed: true,
            pointerId: 999,
            isPrimary: true,
            pointerType: "mouse",
          }),
        );
      }, mid);
      await page.evaluate(
        () =>
          new Promise((r) =>
            requestAnimationFrame(() => requestAnimationFrame(r)),
          ),
      );
      expect((await cursor(overlay))?.uvPosition).toEqual(before.uvPosition);
      expect((await cursor(overlay))?.dab).toEqual(before.dab);
      // No intermediate move at this position: pointer-up must supply the newer
      // location, retaining .8 contact pressure instead of its reported zero.
      const release = { x: start.x + 17, y: start.y + 8 };
      await session.send("Input.dispatchMouseEvent", {
        type: "mouseReleased",
        x: release.x,
        y: release.y,
        button: "left",
        buttons: 0,
        pointerType: "pen",
        force: 0,
      });
      await expect
        .poll(
          async () => (await stored(page)).layers[0].paint?.operations.length,
        )
        .toBe(1);
      const points = (await stored(page)).layers[0].paint.operations[0].points;
      expect(points[0][2]).toBeCloseTo(0.3, 5);
      expect(points.at(-1)[2]).toBeCloseTo(0.8, 5);
      expect(points.at(-1).slice(0, 2)).not.toEqual(points.at(-2).slice(0, 2));
      const events = await page.evaluate(() => window.deviceEvents);
      expect(events.find((e) => e.type === "pointerdown")).toMatchObject({
        pointerType: "pen",
        primary: true,
        buttons: 1,
      });
      expect(events.find((e) => e.type === "pointerup")).toMatchObject({
        pointerType: "pen",
        pressure: 0,
        buttons: 0,
      });
      await hide(page, overlay);
      await expect
        .poll(() => redScore(surface, release), { timeout: 120000 })
        .toBeGreaterThan(75);
      await page
        .getByRole("button", { name: "Undo layer edit", exact: true })
        .click();
      await expect
        .poll(() => redScore(surface, release), { timeout: 120000 })
        .toBeLessThan(10);
      expect(errors).toEqual([]);
      await session.detach();
    });
    test(`${view} touch paints actual pixels, clears on lift, and discards a ${view === "Lit" ? "pinch" : "lost-capture"} draft without reviving a stale footprint`, async ({
      page,
      context,
    }) => {
      const errors = errorsOf(page);
      await open(page, { brush: { size: 24, sizePressure: 0 } });
      if (view === "UV")
        await page
          .getByLabel("Viewport shading", { exact: true })
          .selectOption("uv");
      const surface = view === "UV" ? uv(page) : native(page),
        overlay = view === "UV" ? footprint(page) : surface,
        start =
          view === "UV"
            ? await screenUV(surface, [0.125, 0.375])
            : await bodyPoint(page),
        session = await context.newCDPSession(page);
      await surface.evaluate((c) => {
        window.captureEvents = [];
        document.addEventListener(
          "pointerdown",
          (e) => {
            if (e.isPrimary) window.contactPointerId = e.pointerId;
          },
          { capture: true },
        );
        for (const type of ["gotpointercapture", "lostpointercapture"])
          c.addEventListener(type, (e) =>
            window.captureEvents.push({ type: e.type, id: e.pointerId }),
          );
      });
      const contact = (p, id = 7) => ({
        x: p.x,
        y: p.y,
        id,
        force: 0.6,
        radiusX: 3,
        radiusY: 3,
      });
      const touch = (type, pts) =>
        session.send("Input.dispatchTouchEvent", { type, touchPoints: pts });
      await touch("touchStart", [contact(start)]);
      await expect
        .poll(async () => (await cursor(overlay))?.dab.radius)
        .toBe(12);
      const end = { x: start.x + 10, y: start.y + 3 };
      await touch("touchMove", [contact(end)]);
      await touch("touchEnd", []);
      await expect(overlay).toHaveAttribute("data-brush-cursor", "");
      await expect
        .poll(() => redScore(surface, end), { timeout: 120000 })
        .toBeGreaterThan(100);
      await expect
        .poll(
          async () => (await stored(page)).layers[0].paint?.operations.length,
        )
        .toBe(1);
      const before = (await stored(page)).layers[0].paint;
      const next = { x: start.x + 18, y: start.y + 8 };
      await touch("touchStart", [contact(next)]);
      await expect.poll(() => cursor(overlay)).not.toBeNull();
      if (view === "Lit") {
        const second = { x: next.x + 60, y: next.y + 10 };
        await touch("touchStart", [contact(next), contact(second, 8)]);
        await touch("touchMove", [
          contact({ x: next.x + 2, y: next.y + 2 }),
          contact({ x: second.x + 16, y: second.y + 10 }, 8),
        ]);
      } else {
        // A down only sets PENDING capture. Process a real move first so
        // release genuinely emits lostpointercapture rather than removing a
        // pending capture that the browser has never activated.
        await touch("touchMove", [contact({ x: next.x + 1, y: next.y + 1 })]);
        await expect
          .poll(() =>
            page.evaluate(() =>
              window.captureEvents.some(
                (e) =>
                  e.type === "gotpointercapture" &&
                  e.id === window.contactPointerId,
              ),
            ),
          )
          .toBe(true);
        await surface.evaluate((c) => {
          if (!c.hasPointerCapture(window.contactPointerId))
            throw new Error("The primary touch was not captured");
          c.releasePointerCapture(window.contactPointerId);
        });
        await touch("touchMove", [contact({ x: next.x + 2, y: next.y + 2 })]);
        await touch("touchMove", [contact({ x: next.x + 3, y: next.y + 2 })]);
        await expect
          .poll(() =>
            page.evaluate(() =>
              window.captureEvents.some(
                (e) =>
                  e.type === "lostpointercapture" &&
                  e.id === window.contactPointerId,
              ),
            ),
          )
          .toBe(true);
      }
      await expect(overlay).toHaveAttribute("data-brush-cursor", "", {
        timeout: 15000,
      });
      await touch("touchEnd", []);
      await expect(overlay).toHaveAttribute("data-brush-cursor", "");
      expect((await stored(page)).layers[0].paint).toEqual(before);
      expect(errors).toEqual([]);
      await session.detach();
    });
  }
});
