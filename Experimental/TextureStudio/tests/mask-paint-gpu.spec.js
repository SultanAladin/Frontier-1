import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  patchTextureLayer,
  validateTextureDocument,
} from "../src/textureDocument.js";

// Mask strokes, mask-field baking and the coverage multiply run on the GPU
// when the studio has a WebGL2 renderer (maskPaintGpu.js). This spec paints
// a screen-projected mask stroke in the lit viewport and a UV-space mask
// stroke over a gradient mask, and requires visible coverage changes,
// persisted operations and a clean console. The CPU chain
// (maskPaintRaster.js) is the fallback; a silent fallback would still pass
// these assertions, which is why tests/digital-paint-gpu.spec.js pins the
// channel session's data-digital-paint-backend=gpu marker and
// scripts/verify-mask-gpu.mjs pins the GPU helpers' math.
const native = (page) =>
  page.locator(".tp-scene-viewport > .canvas-host canvas").first();
test.describe.configure({ timeout: 600000 });
test.use({ actionTimeout: 30000 });
function errorsOf(page) {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  return errors;
}
async function settle(page) {
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
}
async function open(page, mask) {
  const doc = createTextureDocument(),
    id = doc.layers.at(-1).id,
    value = validateTextureDocument(patchTextureLayer(doc, id, { mask }));
  await page.addInitScript(
    ({ doc, id }) => {
      if (!localStorage.getItem("mask-gpu-test-seeded")) {
        localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc));
        localStorage.setItem(
          "alloy-texture-authoring-v1",
          JSON.stringify({
            selection: { id, mask: true },
            preview: "mask",
            tool: "brush",
            scope: "object",
          }),
        );
        localStorage.setItem("mask-gpu-test-seeded", "1");
      }
    },
    { doc: value, id },
  );
  await page.goto("/?studio=texture");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
  await settle(page);
  return id;
}
async function saved(page) {
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function pixels(page) {
  await settle(page);
  return native(page).evaluate((c) => {
    const image = document.createElement("canvas");
    image.width = 320;
    image.height = 200;
    const ctx = image.getContext("2d");
    ctx.drawImage(c, 0, 0, image.width, image.height);
    return Array.from(ctx.getImageData(0, 0, image.width, image.height).data);
  });
}
const difference = (a, b) =>
  a.reduce((n, v, i) => n + Math.abs(v - b[i]), 0) / a.length;
async function brushStroke(page) {
  const r = await native(page).boundingBox();
  await page.mouse.move(r.x + r.width * 0.5, r.y + r.height * 0.45);
  await page.mouse.down();
  await page.mouse.move(r.x + r.width * 0.52, r.y + r.height * 0.5, {
    steps: 6,
  });
  await settle(page);
  await page.mouse.up();
  await settle(page);
}

test("GPU mask coverage: projected mask stroke paints and persists", async ({
  page,
}) => {
  const errors = errorsOf(page);
  const id = await open(page, { kind: "paint", fill: 1 });
  const before = await pixels(page);
  await brushStroke(page);
  const painted = await pixels(page);
  expect(difference(painted, before)).toBeGreaterThan(0.3);
  const doc = await saved(page),
    layer = doc.layers.find((l) => l.id === id),
    operations = layer.mask.paint.operations;
  expect(operations).toHaveLength(1);
  // A lit-viewport mask stroke is a screen projection: it must have gone
  // through the GPU projection transfer into the mask session.
  expect(operations[0].digital.projection.kind).toBe("lit");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => difference(await pixels(page), before), {
      timeout: 120000,
    })
    .toBeLessThan(0.2);
  expect(errors).toEqual([]);
});

test("GPU mask field: stroke over a gradient mask bakes coverage", async ({
  page,
}) => {
  const errors = errorsOf(page);
  const id = await open(page, { kind: "gradient", fill: 0 });
  const before = await pixels(page);
  await brushStroke(page);
  const painted = await pixels(page);
  // The field shader blends the baked gradient field with the stroke; a
  // broken field pass leaves either the flat fill or the raw stroke only.
  expect(difference(painted, before)).toBeGreaterThan(0.3);
  const doc = await saved(page),
    layer = doc.layers.find((l) => l.id === id);
  expect(layer.mask.kind).toBe("gradient");
  expect(layer.mask.paint.operations).toHaveLength(1);
  expect(errors).toEqual([]);
});
