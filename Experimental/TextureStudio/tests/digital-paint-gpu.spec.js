import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
} from "../src/textureDocument.js";
import { normalizeDigitalBrush } from "../src/digitalBrush.js";

// End-to-end proof that the production stroke path runs on the GPU session:
// the surface controller must report the gpu backend, painting must change
// real pixels, erase must remove them again, and undo must restore the
// unpainted material — all with a clean console.
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
function fixture() {
  const doc = createTextureDocument();
  doc.digitalPainting = normalizeDigitalBrush({
    color: "#ed253c",
    size: 32,
    sizePressure: 0,
    hardness: 1,
  });
  doc.layers = [
    createTextureLayer("paint", {
      id: "ink",
      name: "Digital ink",
      channels: ["baseColor", "roughness"],
    }),
  ];
  return validateTextureDocument(doc);
}
async function open(page) {
  const doc = fixture();
  await page.addInitScript((doc) => {
    if (!localStorage.getItem("digital-gpu-test-seeded")) {
      localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc));
      localStorage.setItem(
        "alloy-texture-authoring-v1",
        JSON.stringify({
          selection: { id: "ink", mask: false },
          preview: "material",
          tool: "navigate",
        }),
      );
      localStorage.setItem("digital-gpu-test-seeded", "1");
    }
  }, doc);
  await page.goto("/?studio=texture");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
}
async function project(page) {
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const bytes = [];
  for await (const chunk of await (await waiting).createReadStream())
    bytes.push(chunk);
  return JSON.parse(Buffer.concat(bytes).toString());
}
async function pixels(page) {
  return native(page).evaluate((c) => {
    const out = document.createElement("canvas");
    out.width = 256;
    out.height = 160;
    const ctx = out.getContext("2d");
    ctx.drawImage(c, 0, 0, 256, 160);
    return Array.from(ctx.getImageData(0, 0, 256, 160).data);
  });
}
const diff = (a, b) =>
  a.reduce((sum, n, i) => sum + Math.abs(n - b[i]), 0) / a.length;
async function litStroke(page) {
  const r = await native(page).boundingBox();
  await page.mouse.move(r.x + r.width * 0.5, r.y + r.height * 0.48);
  await page.mouse.down();
  await page.mouse.move(r.x + r.width * 0.54, r.y + r.height * 0.55, {
    steps: 4,
  });
  await page.mouse.up();
}

test("GPU paint session deposits, erases and undoes on the lit teapot", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  // The controller only reports "gpu" when the WebGL2 session is live;
  // a CPU fallback (or a shader failure swallowed by it) fails here.
  await expect(native(page)).toHaveAttribute("data-digital-paint-backend", /gpu/, {
    timeout: 120000,
  });
  const before = await pixels(page);
  await page
    .getByRole("button", { name: "Paint layer channels", exact: true })
    .click();
  await litStroke(page);
  await expect(native(page)).toHaveAttribute(
    "data-digital-paint-pass-count",
    "1",
    { timeout: 120000 },
  );
  const paintedPixels = await pixels(page),
    paintedDiff = diff(paintedPixels, before);
  expect(paintedDiff).toBeGreaterThan(0.3);
  expect(
    Number(await native(page).getAttribute("data-digital-paint-slices")),
  ).toBeGreaterThan(0);
  const painted = await project(page);
  expect(painted.layers[0].paint.operations).toHaveLength(1);
  expect(painted.layers[0].paint.operations[0].channels.baseColor).toBe(
    "#ed253c",
  );
  // Painting in the lit viewport produces a screen-projection operation;
  // it must have deposited through the GPU transfer (previously the whole
  // layer silently delegated to the CPU session), and the stroke record's
  // thumbnail must be a real PNG produced by the GPU thumbnail path.
  expect(painted.layers[0].paint.operations[0].projection?.kind).toBe("lit");
  expect(painted.lastDigitalStroke.image.dataUrl).toMatch(
    /^data:image\/png;base64,/,
  );
  expect(painted.lastDigitalStroke.image.width).toBe(256);
  await page
    .getByRole("button", { name: "Erase layer channels", exact: true })
    .click();
  await litStroke(page);
  const erased = await project(page);
  expect(erased.layers[0].paint.operations).toHaveLength(2);
  expect(erased.layers[0].paint.operations[1].kind).toBe("erase");
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeLessThan(paintedDiff);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeLessThan(0.2);
  expect(errors).toEqual([]);
});
