import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
} from "../src/textureDocument.js";
import { normalizeDigitalBrush } from "../src/digitalBrush.js";
import { materials, normalizeMaterial } from "../src/materials.js";
import { createTextureTeapotGeometry } from "../src/textureSurfaceModel.js";
import { uvSurfaceHit } from "../src/textureUVGeometry.js";
const geometry = createTextureTeapotGeometry(20);
const native = (page) =>
  page.locator(".tp-scene-viewport > .canvas-host canvas").first();
const uv = (page) =>
  page.getByLabel("UV texture-space canvas", { exact: true });
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
function fixture({
  mask = false,
  nativeMaterial = false,
  channels = ["baseColor", "roughness"],
} = {}) {
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
      channels,
      ...(mask ? { mask: { kind: "fill", fill: 1 } } : {}),
    }),
    ...(nativeMaterial
      ? [
          createTextureLayer("material", {
            id: "under",
            name: "Green plastic",
            surfaceMaterial: normalizeMaterial({
              id: "green",
              name: "Green plastic",
              type: 8,
              color: "#276542",
              gloss: 0.5,
              roughness: 0.65,
              wear: 0,
              depth: 0,
            }),
          }),
        ]
      : []),
  ];
  return validateTextureDocument(doc);
}
async function open(page, doc = fixture(), mask = false) {
  await page.addInitScript(
    ({ doc, mask }) => {
      if (!localStorage.getItem("digital-test-seeded")) {
        localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc));
        localStorage.setItem(
          "alloy-texture-authoring-v1",
          JSON.stringify({
            selection: { id: "ink", mask },
            preview: mask ? "mask" : "material",
            tool: "navigate",
          }),
        );
        localStorage.setItem("digital-test-seeded", "1");
      }
    },
    { doc, mask },
  );
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
async function pixels(page, target = native(page)) {
  return target.evaluate((c) => {
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
async function litStroke(page, up = true) {
  const r = await native(page).boundingBox();
  await page.mouse.move(r.x + r.width * 0.5, r.y + r.height * 0.48);
  await page.mouse.down();
  await page.mouse.move(r.x + r.width * 0.54, r.y + r.height * 0.55, {
    steps: 4,
  });
  if (up) await page.mouse.up();
}
async function uvPoint(page, p) {
  const c = JSON.parse(await uv(page).getAttribute("data-uv-camera")),
    r = await uv(page).boundingBox();
  return {
    x: r.x + c.x + p[0] * c.scale,
    y: r.y + c.y - p[1] * c.scale,
    uv: p,
  };
}
async function uvSample(page, p) {
  return uv(page).evaluate(
    (c, p) =>
      Array.from(
        c
          .getContext("2d")
          .getImageData(
            Math.floor(
              (p.x - c.getBoundingClientRect().x) *
                Math.min(devicePixelRatio, 2),
            ),
            Math.floor(
              (p.y - c.getBoundingClientRect().y) *
                Math.min(devicePixelRatio, 2),
            ),
            1,
            1,
          ).data,
      ),
    p,
  );
}
async function menu(page) {
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
  return page.getByRole("dialog", {
    name: "Digital brush studio",
    exact: true,
  });
}

test("Lit digital painting is visible, one gesture is one undo, cancel is atomic, erase restores lower pixels, save/reload retains vectors", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
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
  await expect(native(page)).toHaveAttribute("data-material-ready", "true");
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeGreaterThan(0.3);
  const painted = await project(page);
  expect(painted.layers[0].paint.operations.length).toBeGreaterThan(0);
  expect(painted.layers[0].paint.operations[0].channels.baseColor).toBe(
    "#ed253c",
  );
  expect(painted.lastDigitalStroke.operation).toBe("paint");
  expect(painted.lastStroke).toBeNull();
  const count = painted.layers[0].paint.operations.length;
  await litStroke(page, false);
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect((await project(page)).layers[0].paint.operations).toHaveLength(count);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeLessThan(0.2);
  expect((await project(page)).layers[0].paint).toBeUndefined();
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute(
    "data-digital-paint-pass-count",
    "1",
  );
  await page.getByRole("tab", { name: "History", exact: true }).click();
  await expect(page.getByLabel("Last stroke preview")).toContainText(
    "Surface channels",
  );
  await expect(
    page.getByLabel("Last stroke preview").locator("img").first(),
  ).toHaveAttribute("src", /^data:image\/png/);
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  await page
    .getByRole("button", { name: "Erase layer channels", exact: true })
    .click();
  await litStroke(page);
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeLessThan(0.3);
  const erased = await project(page);
  expect(erased.layers[0].paint.operations.at(-1).kind).toBe("erase");
  expect(erased.lastDigitalStroke.operation).toBe("erase");
  await page.reload();
  await expect(native(page)).toHaveAttribute("data-material-ready", "true");
  expect((await project(page)).layers[0].paint).toEqual(erased.layers[0].paint);
  await page
    .getByRole("button", { name: "Clear layer digital paint", exact: true })
    .click();
  expect((await project(page)).layers[0].paint).toBeUndefined();
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect((await project(page)).layers[0].paint).toEqual(erased.layers[0].paint);
  expect(errors).toEqual([]);
});

test("packed UV fills touch only Lid; channel-only roughness painting retains underlying colour and obeys ordering/masks", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, fixture({ nativeMaterial: true }));
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(uv(page)).toHaveAttribute("data-uv-charts", "32");
  await expect(native(page)).toHaveAttribute("data-layer-atlas-ready", "true");
  const lid = await uvPoint(page, [1.12, 0.56]),
    body = await uvPoint(page, [0.12, 0.12]);
  expect(uvSurfaceHit(geometry, ...lid.uv).target.partId).toBe("lid");
  const bodyBefore = await uvSample(page, body),
    lidBefore = await uvSample(page, lid);
  await page
    .getByRole("button", { name: "Fill layer channels", exact: true })
    .click();
  await page.mouse.click(lid.x, lid.y);
  await expect
    .poll(
      async () =>
        (await uvSample(page, lid))[0] - (await uvSample(page, lid))[1],
      { timeout: 120000 },
    )
    .toBeGreaterThan(35);
  expect(await uvSample(page, body)).toEqual(bodyBefore);
  const filled = await project(page);
  expect(filled.layers[0].paint.operations.at(-1)).toMatchObject({
    kind: "fill",
    region: { kind: "object", partId: "lid" },
  });
  const toggles = await menu(page);
  await toggles
    .getByLabel("Digital Base color channel", { exact: true })
    .uncheck();
  await toggles
    .getByRole("button", { name: "Close digital brush studio", exact: true })
    .click();
  await expect
    .poll(
      async () =>
        (await uvSample(page, lid))[1] - (await uvSample(page, lid))[0],
      { timeout: 120000 },
    )
    .toBeGreaterThan(12);
  expect((await project(page)).layers[0].paint).toEqual(filled.layers[0].paint);
  // Undo the channel toggle, then the fill. Disabled data is never deleted.
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(
      async () =>
        (await uvSample(page, lid))[0] - (await uvSample(page, lid))[1],
      { timeout: 120000 },
    )
    .toBeGreaterThan(35);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(() => uvSample(page, lid), { timeout: 120000 })
    .toEqual(lidBefore);
  const m = await menu(page);
  await m.getByLabel("Digital Base color channel", { exact: true }).uncheck();
  await m
    .getByRole("button", { name: "Digital Roughness properties", exact: true })
    .click();
  await m
    .getByLabel("Digital Roughness target value", { exact: true })
    .fill("1");
  await m.getByRole("button", { name: "Use fill", exact: true }).click();
  await page.mouse.click(lid.x, lid.y);
  await expect
    .poll(
      async () =>
        JSON.parse(await native(page).getAttribute("data-digital-paint-layers"))
          .length,
      { timeout: 120000 },
    )
    .toBe(1);
  await expect
    .poll(
      async () =>
        (await uvSample(page, lid))[1] - (await uvSample(page, lid))[0],
      { timeout: 120000 },
    )
    .toBeGreaterThan(10);
  const scalar = await project(page);
  expect(scalar.layers[0].paint.operations.at(-1).channels).toEqual({
    roughness: 1,
  });
  expect(scalar.layers[1].surfaceMaterial).toEqual(
    filled.layers[1].surfaceMaterial,
  );
  expect(await uvSample(page, body)).toEqual(bodyBefore);
  await page.screenshot({
    path: ".playwright/digital-paint/channel-only-uv.png",
  });
  // A genuine opaque material above the digital row must cover its pixels.
  const above = { ...scalar, layers: [scalar.layers[1], scalar.layers[0]] };
  await page.getByLabel("Open texture layer JSON").setInputFiles({
    name: "ordered.texture.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(above)),
  });
  await expect
    .poll(() => uvSample(page, lid), { timeout: 120000 })
    .toEqual(lidBefore);
  expect(errors).toEqual([]);
});

test("digital masks paint black, erase to the original source and fill a selected part without changing layer channels", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, fixture({ mask: true }), true);
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(uv(page)).toHaveAttribute("data-mask-preview", "mask");
  const body = await uvPoint(page, [0.12, 0.12]),
    lid = await uvPoint(page, [1.12, 0.56]);
  const m = await menu(page);
  await m
    .getByRole("button", { name: "Black digital mask ink", exact: true })
    .click();
  await m.getByRole("button", { name: "Use paint", exact: true }).click();
  await page.mouse.move(body.x, body.y);
  await page.mouse.down();
  await page.mouse.move(body.x + 3, body.y + 2);
  await page.mouse.up();
  await expect
    .poll(async () => (await uvSample(page, body))[0], { timeout: 90000 })
    .toBeLessThan(5);
  expect((await uvSample(page, lid))[0]).toBeGreaterThan(245);
  let saved = await project(page);
  expect(saved.layers[0].paint).toBeUndefined();
  expect(saved.layers[0].mask.paint.operations.at(-1).digital.operation).toBe(
    "paint",
  );
  expect(saved.lastDigitalStroke.mask).toBe(true);
  await page
    .getByRole("button", { name: "Erase mask digitally", exact: true })
    .click();
  await page.mouse.move(body.x, body.y);
  await page.mouse.down();
  await page.mouse.move(body.x + 3, body.y + 2);
  await page.mouse.up();
  await expect
    .poll(async () => (await uvSample(page, body))[0], { timeout: 90000 })
    .toBeGreaterThan(245);
  await page
    .getByRole("button", { name: "Fill mask digitally", exact: true })
    .click();
  await page.mouse.click(lid.x, lid.y);
  await expect
    .poll(async () => (await uvSample(page, lid))[0], { timeout: 90000 })
    .toBeLessThan(5);
  expect((await uvSample(page, body))[0]).toBeGreaterThan(245);
  saved = await project(page);
  expect(saved.layers[0].mask.fill).toBe(1);
  expect(saved.layers[0].mask.paint.operations.at(-1)).toMatchObject({
    kind: "region",
    region: { kind: "object", partId: "lid" },
    value: 0,
  });
  expect(saved.layers[0].channelSettings).toEqual(
    fixture({ mask: true }).layers[0].channelSettings,
  );
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => (await uvSample(page, lid))[0], { timeout: 90000 })
    .toBeGreaterThan(245);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await page.reload();
  await expect(native(page)).toHaveAttribute("data-mask-preview", "mask");
  expect(
    (await project(page)).layers[0].mask.paint.operations.at(-1).value,
  ).toBe(0);
  expect(errors).toEqual([]);
});

test("brush studio has real live dynamics, channel toggles, zero controls, focus trap and a usable phone layout", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await page.setViewportSize({ width: 390, height: 844 });
  await open(page);
  const m = await menu(page),
    preview = m.getByLabel("Digital brush stroke preview canvas", {
      exact: true,
    });
  const before = await preview.getAttribute("data-preview-hash");
  await m.getByLabel("Digital brush Size value", { exact: true }).fill("40");
  await expect(preview).not.toHaveAttribute("data-preview-hash", before);
  const shapeHash = await preview.getAttribute("data-preview-hash");
  await m
    .getByLabel("Digital brush tip shape", { exact: true })
    .selectOption("square");
  await expect(preview).not.toHaveAttribute("data-preview-hash", shapeHash);
  await m
    .getByRole("button", { name: "Digital brush Grain round", exact: true })
    .click();
  await expect(
    m.getByRole("region", { name: "Grain properties", exact: true }),
  ).toBeAttached();
  await m
    .getByRole("button", { name: "Channel properties", exact: true })
    .click();
  await m.getByLabel("Digital Normal channel", { exact: true }).check();
  await expect(
    m.getByLabel("Digital Height channel", { exact: true }),
  ).toBeChecked();
  await m
    .getByRole("button", { name: "Disable all channels", exact: true })
    .click();
  await expect(
    m.getByText("No channels enabled — painting is a no-op.", { exact: true }),
  ).toBeVisible();
  await expect(
    m.getByRole("button", { name: "Use paint", exact: true }),
  ).toBeDisabled();
  await m.getByLabel("Digital Base color channel", { exact: true }).check();
  await m
    .getByRole("button", { name: "Brush properties", exact: true })
    .click();
  await m.getByLabel("Digital brush Size value", { exact: true }).fill("0");
  expect(
    await preview.evaluate((c) => {
      const pixels = c
        .getContext("2d")
        .getImageData(0, 0, c.width, c.height).data;
      for (let i = 3; i < pixels.length; i += 4) if (pixels[i]) return false;
      return true;
    }),
  ).toBe(true);
  const last = m
    .locator(
      "button:not(:disabled),input:not(:disabled),select:not(:disabled),canvas",
    )
    .last();
  await last.focus();
  await page.keyboard.press("Tab");
  expect(await m.evaluate((el) => el.contains(document.activeElement))).toBe(
    true,
  );
  const dimensions = await m.boundingBox();
  expect(dimensions.width).toBeLessThan(391);
  expect(dimensions.height).toBeLessThan(845);
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThan(391);
  await m.getByRole("button", { name: "Use paint", exact: true }).click();
  await expect(native(page)).toBeVisible();
  expect((await native(page).boundingBox()).height).toBeGreaterThan(120);
  await expect(
    page.getByRole("tab", { name: "Layers", exact: true }),
  ).toHaveAttribute("aria-selected", "true");
  await litStroke(page);
  expect((await project(page)).layers[0].paint).toBeUndefined();
  expect(errors).toEqual([]);
  await page.screenshot({
    path: ".playwright/digital-paint/phone-digital-paint.png",
  });
});

test("native glass can receive digital channel fills without becoming an opaque coating or changing its recipe", async ({
  page,
}) => {
  const errors = errorsOf(page),
    doc = fixture({ nativeMaterial: true });
  doc.layers[0] = createTextureLayer("material", {
    id: "glass-ink",
    name: "Painted glass",
    channels: ["roughness"],
    channelSettings: { roughness: { value: 1 } },
    surfaceMaterial: materials.find((m) => m.id === "crystal-glass"),
  });
  await open(page, doc);
  await page
    .getByRole("button", { name: "Select Painted glass", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "1",
  );
  const params = JSON.parse(
      await native(page).getAttribute("data-optical-stack-materials"),
    ),
    before = await pixels(page);
  const studio = await menu(page);
  await studio
    .getByRole("button", { name: "Digital tool Fill", exact: true })
    .click();
  await studio.getByRole("button", { name: "Use fill", exact: true }).click();
  const r = await native(page).boundingBox();
  await page.mouse.click(r.x + r.width * 0.5, r.y + r.height * 0.49);
  await expect
    .poll(
      async () =>
        JSON.parse(await native(page).getAttribute("data-digital-paint-layers"))
          .length,
      { timeout: 120000 },
    )
    .toBe(1);
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeGreaterThan(0.1);
  expect((await project(page)).layers[0].surfaceMaterial).toEqual(
    doc.layers[0].surfaceMaterial,
  );
  expect((await project(page)).layers[0].paint.operations[0].channels).toEqual({
    roughness: 1,
  });
  expect(
    JSON.parse(await native(page).getAttribute("data-optical-stack-materials")),
  ).toEqual(params);
  await expect(native(page)).toHaveAttribute(
    "data-digital-paint-pass-count",
    "0",
  );
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect
    .poll(async () => diff(await pixels(page), before), { timeout: 120000 })
    .toBeLessThan(0.3);
  expect(errors).toEqual([]);
});
