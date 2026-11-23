import { test, expect } from "@playwright/test";
import {
  createTextureDocument,
  patchTextureLayer,
  validateTextureDocument,
} from "../src/textureDocument.js";
import { createTextureTeapotGeometry } from "../src/textureSurfaceModel.js";
import { uvTopology, uvSurfaceHit } from "../src/textureUVGeometry.js";
import { normalizeTextureMask } from "../src/textureMasks.js";
import { compilePointGradient } from "../src/pointGradient.js";

const native = (page) =>
    page.locator(".tp-scene-viewport > .canvas-host canvas"),
  uv = (page) => page.getByLabel("UV texture-space canvas", { exact: true });
const geometry = createTextureTeapotGeometry(20),
  topology = uvTopology(geometry);
test.describe.configure({ timeout: 240000 });
test.use({ actionTimeout: 25000 });
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
async function settle(page) {
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
}
async function open(
  page,
  mask = { kind: "paint", fill: 1 },
  phone = false,
  settings = {},
) {
  if (phone) await page.setViewportSize({ width: 390, height: 844 });
  const doc = createTextureDocument(),
    id = doc.layers.at(-1).id;
  const value = mask
    ? validateTextureDocument(patchTextureLayer(doc, id, { mask }))
    : doc;
  await page.addInitScript(
    ({ doc, id, mask, settings }) => {
      if (!localStorage.getItem("mask-test-seeded")) {
        localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc));
        localStorage.setItem(
          "alloy-texture-authoring-v1",
          JSON.stringify({
            selection: { id, mask: !!mask },
            preview: mask ? "mask" : "material",
            tool: "brush",
            scope: "object",
            ...settings,
          }),
        );
        localStorage.setItem("mask-test-seeded", "1");
      }
    },
    { doc: value, id, mask, settings },
  );
  await page.goto("/?studio=texture");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 90000,
  });
  await settle(page);
  return value;
}
async function saved(page) {
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function importDoc(page, doc) {
  await page.getByLabel("Open texture layer JSON").setInputFiles({
    name: "masks.texture.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(doc)),
  });
  await settle(page);
}
async function pixels(page, target = native(page)) {
  await settle(page);
  return target.evaluate((c) => {
    const image = document.createElement("canvas");
    // Fixed-resolution real framebuffer readback avoids serializing millions
    // of Playwright per-pixel objects (and permits stable undo comparisons).
    image.width = 320;
    image.height = 200;
    const ctx = image.getContext("2d");
    ctx.drawImage(c, 0, 0, image.width, image.height);
    return Array.from(ctx.getImageData(0, 0, image.width, image.height).data);
  });
}
const difference = (a, b) =>
  a.reduce((n, v, i) => n + Math.abs(v - b[i]), 0) / a.length;
async function brush(page, x = 0.5, y = 0.45, up = true) {
  const r = await native(page).boundingBox();
  await page.mouse.move(r.x + r.width * x, r.y + r.height * y);
  await page.mouse.down();
  await page.mouse.move(
    r.x + r.width * (x + 0.02),
    r.y + r.height * (y + 0.05),
    { steps: 6 },
  );
  await settle(page);
  if (up) {
    await page.mouse.up();
    await settle(page);
  }
}
async function uvMode(page) {
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(uv(page)).toHaveAttribute("data-uv-charts", "32");
  await expect(uv(page)).toHaveAttribute("data-mask-preview", "mask");
}
async function point(page, chartId = 25) {
  const triangle = topology.triangles.find(
      (t) =>
        t.chartId === chartId &&
        Math.abs(t.points[0][0] - t.points[1][0]) > 0.00001 &&
        Math.abs(t.points[0][1] - t.points[2][1]) > 0.00001,
    ),
    p = [0, 1].map((c) => triangle.points.reduce((s, v) => s + v[c] / 3, 0)),
    camera = JSON.parse(await uv(page).getAttribute("data-uv-camera")),
    rect = await uv(page).boundingBox();
  return {
    x: rect.x + camera.x + p[0] * camera.scale,
    y: rect.y + camera.y - p[1] * camera.scale,
    uv: p,
    hit: uvSurfaceHit(geometry, ...p),
  };
}
async function sampleUV(page, p) {
  return uv(page).evaluate((c, p) => {
    const d = window.devicePixelRatio || 1;
    return Array.from(
      c
        .getContext("2d")
        .getImageData(
          Math.round((p.x - c.getBoundingClientRect().x) * Math.min(d, 2)),
          Math.round((p.y - c.getBoundingClientRect().y) * Math.min(d, 2)),
          1,
          1,
        ).data,
    );
  }, p);
}

test("digital tools stay unarmed on selection; previews and mask tools switch independently of Lit/UV", async ({
  page,
}) => {
  const errors = errorsOf(page),
    initial = await open(page, null);
  for (const name of ["Surface detail", "Surface finish", "Base material"]) {
    await page
      .getByRole("button", { name: `Select ${name}`, exact: true })
      .click();
    await expect(page.getByLabel("Texture editing mode")).toHaveText(
      "Paint mode · Digital tools available",
    );
    const r = await native(page).boundingBox();
    await page.mouse.click(r.x + r.width * 0.5, r.y + r.height * 0.5);
    expect(await saved(page)).toEqual(initial);
    expect(
      await page.evaluate(
        () =>
          JSON.parse(localStorage.getItem("alloy-texture-authoring-v1")).mode,
      ),
    ).toBe("paint");
  }
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Mask preview", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect(native(page)).toHaveAttribute("data-mask-preview", "mask");
  await expect(
    page.getByRole("button", { name: "Select mask UV Tile", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Material preview", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute("data-mask-preview", "material");
  await expect(page.getByLabel("Texture editing mode")).toContainText(
    "Mask mode",
  );
  await page
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await page.reload();
  await expect(
    page.getByRole("button", { name: "Select Surface detail", exact: true }),
  ).toHaveAttribute("data-layer-select", initial.layers[0].id);
  await expect(page.getByLabel("Texture editing mode")).toHaveText(
    "Paint mode · Digital tools available",
  );
  expect(
    (await saved(page)).layers.find((l) => l.id === initial.layers[0].id).mask,
  ).toBeNull();
  expect(errors).toEqual([]);
});

test("Lit mask strokes have live grayscale feedback, one undo step, cancellation, save and reload", async ({
  page,
}) => {
  test.setTimeout(420000);
  const errors = errorsOf(page),
    before = await open(page),
    id = before.layers.at(-1).id;
  await expect(native(page)).toHaveAttribute("data-mask-preview", "mask");
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  const initial = await pixels(page),
    states = Number(
      (await page.locator(".tp-history-heading > span").textContent()).split(
        " ",
      )[0],
    );
  await brush(page, 0.5, 0.45, false);
  const live = await pixels(page);
  expect(difference(initial, live)).toBeGreaterThan(0.1);
  expect(
    JSON.parse(
      await page.evaluate(() =>
        localStorage.getItem("alloy-texture-project-v1"),
      ),
    ).layers.find((l) => l.id === id).mask.paint,
  ).toBeUndefined();
  await page.mouse.up();
  const after = await saved(page),
    operations = after.layers.find((l) => l.id === id).mask.paint.operations;
  expect(operations.length).toBeGreaterThan(0);
  expect(
    operations.every(
      (op) =>
        op.region.partId === "body" && op.kind === "brush" && op.value === 0,
    ),
  ).toBe(true);
  await expect(page.locator(".tp-history-heading > span")).toHaveText(
    `${states + 1} states`,
  );
  await page.screenshot({ path: ".playwright/mask-paint/lit-painted.png" });
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(await saved(page)).toEqual(before);
  expect(difference(initial, await pixels(page))).toBeLessThan(0.05);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  expect(await saved(page)).toEqual(after);
  await brush(page, 0.54, 0.5, false);
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect(await saved(page)).toEqual(after);
  await brush(page, 0.54, 0.5, false);
  await native(page).evaluate((c) =>
    c.dispatchEvent(
      new PointerEvent("pointercancel", { bubbles: true, pointerId: 1 }),
    ),
  );
  await page.mouse.up();
  expect(await saved(page)).toEqual(after);
  await page.reload();
  await expect(native(page)).toHaveAttribute("data-mask-preview", "mask");
  await expect(page.getByLabel("Texture editing mode")).toContainText(
    "Mask mode",
  );
  expect(await saved(page)).toEqual(after);
  expect(difference(initial, await pixels(page))).toBeGreaterThan(0.1);
  expect(errors).toEqual([]);
});

test("Face, UV Island, UV Tile and Object fill real geometry only, keeping Lid separate from Body", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, { kind: "paint", fill: 0 });
  await uvMode(page);
  const target = await point(page, 25),
    other = await point(page, 1),
    baseline = await sampleUV(page, other);
  for (const [scope, label] of [
    ["face", "Face"],
    ["island", "UV Island"],
    ["tile", "UV Tile"],
    ["object", "Object"],
  ]) {
    await page
      .getByRole("button", { name: `Select mask ${label}`, exact: true })
      .click();
    await page.mouse.click(target.x, target.y);
    await expect(
      page.getByLabel("Selected mask region", { exact: true }),
    ).toContainText("Lid");
    await page
      .getByRole("button", { name: "Fill selected mask region", exact: true })
      .click();
    await settle(page);
    const current = await saved(page),
      op = current.layers.at(-1).mask.paint.operations.at(-1);
    expect(op.kind).toBe("region");
    expect(op.region).toMatchObject({ kind: scope, partId: "lid" });
    if (scope === "face") expect(op.region.id).toBe(target.hit.faceIndex);
    if (scope === "island") expect(op.region.id).toBe(25);
    if (scope === "tile") expect(op.region.id).toBe(1002);
    // Clear the gold selection outline before reading the underlying field.
    await page
      .getByRole("button", { name: "Clear mask selection", exact: true })
      .click();
    await settle(page);
    // One triangle is subpixel at this UV zoom; test visible coverage,
    // not a fully white resampled screen pixel at its antialiased edge.
    expect((await sampleUV(page, target))[0]).toBeGreaterThan(
      scope === "face" ? 70 : 200,
    );
    expect(await sampleUV(page, other)).toEqual(baseline);
    if (scope === "island" || scope === "face") {
      const anotherIsland = await point(page, 26);
      expect((await sampleUV(page, anotherIsland))[0]).toBeLessThan(20);
    }
    await page
      .getByRole("button", { name: "Undo layer edit", exact: true })
      .click();
    expect((await saved(page)).layers.at(-1).mask.paint).toBeUndefined();
  }
  await page.screenshot({ path: ".playwright/mask-paint/uv-region-tools.png" });
  expect(errors).toEqual([]);
});

test("painted mask coverage affects a real native lid material and preserves unrelated part assignments", async ({
  page,
}) => {
  // Native physical-material + coverage shader warm-up can block a software
  // WebGL readback longer than a short action timeout; retain the test deadline.
  page.setDefaultTimeout(60000);
  const errors = errorsOf(page);
  await open(page, null);
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  const target = await point(page, 25);
  await uv(page).evaluate((c, p) => {
    const transfer = new DataTransfer();
    transfer.setData(
      "application/x-alloy-asset",
      JSON.stringify({
        type: "material",
        id: "carbon-ceramic",
        name: "Carbon Ceramic",
      }),
    );
    c.dispatchEvent(
      new DragEvent("drop", {
        bubbles: true,
        cancelable: true,
        dataTransfer: transfer,
        clientX: p.x,
        clientY: p.y,
      }),
    );
  }, target);
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true");
  const docBefore = await saved(page),
    assigned = docBefore.layers.find((l) => l.surfaceTarget?.partId === "lid");
  expect(assigned).toBeTruthy();
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Material preview", exact: true })
    .click();
  const materialBefore = await pixels(page);
  await page.getByRole("button", { name: "Mask preview", exact: true }).click();
  await uvMode(page);
  await page
    .getByRole("button", { name: "Select mask Object", exact: true })
    .click();
  const p = await point(page, 25);
  await page.mouse.click(p.x, p.y);
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Fill selected mask region", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Clear mask selection", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Material preview", exact: true })
    .click();
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await expect(native(page)).toHaveAttribute("data-material-ready", "true");
  const after = await saved(page),
    covered = after.layers.find((l) => l.id === assigned.id);
  expect(covered.mask.paint.operations[0].region).toEqual({
    kind: "object",
    partId: "lid",
  });
  expect(covered.surfaceMaterial).toEqual(assigned.surfaceMaterial);
  expect(
    after.layers
      .filter((l) => l.surfaceTarget)
      .map((l) => l.surfaceTarget.partId),
  ).toEqual(["lid"]);
  expect(difference(materialBefore, await pixels(page))).toBeGreaterThan(0.05);
  await page.screenshot({
    path: ".playwright/mask-paint/lid-material-coverage.png",
  });
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(
    (await saved(page)).layers.find((l) => l.id === assigned.id).mask.paint,
  ).toBeUndefined();
  expect(errors).toEqual([]);
});

test("selected Fill and Gradient previews honor actual coverage, inversion, strength and bypass", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, { kind: "gradient" });
  await uvMode(page);
  const p = await point(page, 25),
    original = await sampleUV(page, p),
    mask = normalizeTextureMask({ kind: "gradient" }),
    sample = compilePointGradient(mask.gradient)(p.hit.position),
    expected =
      (sample[0] * 0.2126 + sample[1] * 0.7152 + sample[2] * 0.0722) * 255;
  expect(original[0]).toBeCloseTo(expected, -1);
  expect(Math.abs(original[0] - original[1])).toBeLessThan(2);
  await page.getByLabel("Invert mask", { exact: true }).check();
  await settle(page);
  expect((await sampleUV(page, p))[0]).toBeCloseTo(255 - original[0], -1);
  await page.getByLabel("Mask enabled", { exact: true }).uncheck();
  await settle(page);
  expect((await sampleUV(page, p))[0]).toBe(255);
  await page.getByLabel("Mask enabled", { exact: true }).check();
  await page
    .getByRole("group", { name: "Mask type", exact: true })
    .getByRole("button", { name: "Fill", exact: true })
    .click();
  await page.getByLabel("Invert mask", { exact: true }).uncheck();
  await page.getByLabel("Mask fill", { exact: true }).selectOption("0");
  await settle(page);
  expect((await sampleUV(page, p))[0]).toBe(0);
  await page.getByLabel("Mask strength value", { exact: true }).fill("50");
  await settle(page);
  expect((await sampleUV(page, p))[0]).toBeGreaterThan(120);
  expect((await sampleUV(page, p))[0]).toBeLessThan(135);
  expect(errors).toEqual([]);
});

test("locked and read-only targets reject mask edits; invalid imported mask records preserve the current project", async ({
  page,
}) => {
  const errors = errorsOf(page),
    original = await open(page);
  await page
    .getByRole("button", { name: "Lock Base material", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Paint mask", exact: true }),
  ).toBeDisabled();
  const locked = await saved(page);
  await brush(page);
  expect(await saved(page)).toEqual(locked);
  await page
    .getByRole("button", { name: "Unlock Base material", exact: true })
    .click();
  await uvMode(page);
  await page
    .getByLabel("UV coordinate set", { exact: true })
    .selectOption("source");
  await expect(
    page.getByRole("button", { name: "Paint mask", exact: true }),
  ).toBeDisabled();
  await expect(
    page.getByRole("button", { name: "Mask preview", exact: true }),
  ).toBeDisabled();
  const before = await saved(page),
    invalid = structuredClone(before);
  invalid.layers.at(-1).mask.paint = { uvSet: "uv0", operations: [] };
  await importDoc(page, invalid);
  await expect(page.getByRole("alert")).toContainText("authoring UV");
  expect(await saved(page)).toEqual(before);
  expect(original.layers.at(-1).mask).toEqual(before.layers.at(-1).mask);
  expect(errors).toEqual([]);
});

test("390px mask tools and a real phone drawing canvas remain reachable, with UV tile selection and saved state", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, { kind: "paint", fill: 0 }, true);
  await expect(
    page.getByRole("button", { name: "Paint mask", exact: true }),
  ).toBeVisible();
  await uvMode(page);
  const rect = await uv(page).boundingBox();
  expect(rect.width).toBeGreaterThan(320);
  expect(rect.height).toBeGreaterThan(180);
  expect(rect.x + rect.width).toBeLessThanOrEqual(391);
  await page
    .getByRole("button", { name: "Select mask UV Tile", exact: true })
    .click();
  const p = await point(page, 25);
  await page.mouse.click(p.x, p.y);
  await expect(
    page.getByLabel("Selected mask region", { exact: true }),
  ).toContainText("UV Tile 1002");
  await page
    .getByRole("button", { name: "Fill selected mask region", exact: true })
    .click();
  const doc = await saved(page);
  expect(doc.layers.at(-1).mask.paint.operations[0].region).toEqual({
    kind: "tile",
    partId: "lid",
    id: 1002,
  });
  await page
    .getByRole("button", { name: "Clear mask selection", exact: true })
    .click();
  await page.screenshot({ path: ".playwright/mask-paint/phone-mask.png" });
  const tree = page.getByRole("tree", { name: "Texture layers", exact: true });
  expect((await tree.boundingBox()).height).toBeGreaterThan(28);
  await page
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await expect(page.getByLabel("Texture editing mode")).toHaveText(
    "Paint mode · Digital tools available",
  );
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(391);
  expect(errors).toEqual([]);
});

test("Colour mask preview uses actual source RGB per tile, not the shaded material colour", async ({
  page,
}) => {
  const errors = errorsOf(page),
    before = await open(page, {
      kind: "color",
      color: "#ffffff",
      tolerance: 0,
      softness: 0,
    });
  const image = await page.evaluate(() => {
    const c = document.createElement("canvas");
    c.width = c.height = 64;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "black";
    ctx.fillRect(0, 0, 64, 64);
    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, 16, 64);
    return {
      name: "Two-tone source",
      width: 64,
      height: 64,
      dataUrl: c.toDataURL("image/png"),
    };
  });
  const id = before.layers.at(-1).id,
    owner = before.layers.at(-1),
    doc = patchTextureLayer(before, id, {
      channelSettings: {
        ...owner.channelSettings,
        baseColor: {
          ...owner.channelSettings.baseColor,
          mode: "texture",
          texture: image,
        },
      },
    });
  await importDoc(page, doc);
  await page
    .getByRole("button", { name: "Select mask of Base material", exact: true })
    .click();
  await uvMode(page);
  const light = await point(page, 25),
    dark = await point(page, 27);
  expect((await sampleUV(page, light))[0]).toBeGreaterThan(200);
  expect((await sampleUV(page, dark))[0]).toBeLessThan(20);
  await expect(
    page.getByLabel("Selected mask region", { exact: true }),
  ).toContainText("Source RGB per UV tile, not shaded material");
  await page.getByLabel("Invert mask", { exact: true }).check();
  await settle(page);
  expect((await sampleUV(page, light))[0]).toBeLessThan(20);
  expect((await sampleUV(page, dark))[0]).toBeGreaterThan(200);
  await page.getByLabel("Mask enabled", { exact: true }).uncheck();
  await settle(page);
  expect((await sampleUV(page, light))[0]).toBe(255);
  expect((await sampleUV(page, dark))[0]).toBe(255);
  expect(errors).toEqual([]);
});

test("decal mode stays functional and Mask preview isolates the selected stamp mask", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, null);
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  await page.getByLabel("Stamp brush as mask", { exact: true }).check();
  const lid = await point(page, 25),
    body = await point(page, 1);
  await page.mouse.click(lid.x, lid.y);
  await page.mouse.click(body.x, body.y);
  await expect(native(page)).toHaveAttribute("data-decal-mask-count", "2");
  await expect(page.getByLabel("Texture editing mode")).toHaveText(
    "Decal mode",
  );
  const doc = await saved(page),
    components = doc.layers.flatMap((l) => l.decals);
  expect(components.length).toBe(2);
  expect(components[0].target.partId).toBe("lid");
  expect(components[1].target.partId).toBe("body");
  // Expand the owner and pick the lid component from real component records.
  const disclosure = page.getByRole("button", {
    name: "Expand Base material",
    exact: true,
  });
  if (await disclosure.count()) await disclosure.click();
  await page
    .locator(".tp-decal-tree-row button")
    .filter({ hasText: components[0].name })
    .first()
    .click();
  await page.getByRole("button", { name: "Mask preview", exact: true }).click();
  await expect(uv(page)).toHaveAttribute("data-mask-preview", "mask");
  await settle(page);
  const p = await point(page, 1);
  expect((await sampleUV(page, p))[0]).toBeLessThan(20);
  const texture = await pixels(page, uv(page));
  expect(texture.some((v, i) => i % 4 === 0 && v > 200)).toBe(true);
  expect(await saved(page)).toEqual(doc);
  // A bypassed selected component must not accidentally preview its sibling's
  // mask union after it falls out of the shared active-stamp table.
  await page.getByLabel("Decal opacity value", { exact: true }).fill("0");
  await settle(page);
  expect((await sampleUV(page, p))[0]).toBeGreaterThan(200);
  await page
    .getByRole("button", { name: "Material preview", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Viewport decal transform", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Viewport decal transform", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  expect(errors).toEqual([]);
});

test("mask Wireframe changes the actual mesh while UV mask atlases stay filled", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const solid = await pixels(page);
  // WebGL is transparent. Antialiased white wire edges can retain RGB=255
  // even with tiny alpha; count visible opaque coverage, not invisible RGB.
  const whitePixels = (data) =>
    data.reduce(
      (n, v, i) =>
        n +
        (i % 4 === 0 &&
        v > 245 &&
        data[i + 1] > 245 &&
        data[i + 2] > 245 &&
        data[i + 3] > 230
          ? 1
          : 0),
      0,
    );
  const solidWhite = whitePixels(solid);
  expect(solidWhite).toBeGreaterThan(1000);
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("wireframe");
  await expect(native(page)).toHaveAttribute("data-wireframe", "true");
  await expect(native(page)).toHaveAttribute("data-mask-preview", "mask");
  await expect
    .poll(async () => whitePixels(await pixels(page)), { timeout: 20000 })
    .toBeLessThan(solidWhite * 0.95);
  // Force an atlas update while the 3D material is still Wireframe. Merely
  // opening UV would set the geometry view back to solid before rendering it.
  await page
    .getByRole("button", { name: "Select mask Object", exact: true })
    .click();
  const rect = await native(page).boundingBox();
  await page.mouse.click(rect.x + rect.width * 0.5, rect.y + rect.height * 0.5);
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Fill selected mask region", exact: true })
    .click();
  expect(
    (await saved(page)).layers.at(-1).mask.paint.operations[0].region,
  ).toEqual({ kind: "object", partId: "body" });
  await page
    .getByRole("button", { name: "Clear mask selection", exact: true })
    .click();
  await uvMode(page);
  const p = await point(page),
    body = await point(page, 10);
  expect((await sampleUV(page, p))[0]).toBeGreaterThan(230);
  expect((await sampleUV(page, body))[0]).toBeLessThan(25);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await expect(native(page)).toHaveAttribute("data-wireframe", "false");
  await expect
    .poll(async () => whitePixels(await pixels(page)), { timeout: 20000 })
    .toBeGreaterThan(solidWhite * 0.95);
  expect(errors).toEqual([]);
});

test("UV mask strokes ignore unrelated pointer moves, ends and nonprimary touches", async ({
  page,
}) => {
  const errors = errorsOf(page),
    before = await open(page);
  await uvMode(page);
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  const p = await point(page),
    other = await point(page, 24);
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await settle(page);
  // Real primary pointer owns capture. A second finger must neither replace
  // its draft, add samples, commit it early, nor cancel it.
  await uv(page).evaluate((c, p) => {
    for (const type of [
      "pointermove",
      "pointerup",
      "pointercancel",
      "lostpointercapture",
      "pointerdown",
    ])
      c.dispatchEvent(
        new PointerEvent(type, {
          bubbles: true,
          pointerId: 222,
          pointerType: "touch",
          isPrimary: false,
          button: 0,
          clientX: p.x,
          clientY: p.y,
        }),
      );
  }, other);
  await settle(page);
  expect(
    JSON.parse(
      await page.evaluate(() =>
        localStorage.getItem("alloy-texture-project-v1"),
      ),
    ),
  ).toEqual(before);
  await page.mouse.move(p.x + 2, p.y + 1);
  await page.mouse.up();
  const after = await saved(page),
    ops = after.layers.at(-1).mask.paint.operations;
  expect(ops).toHaveLength(1);
  expect(ops[0].points[0]).toEqual(p.uv.map((n) => expect.closeTo(n, 5)));
  expect(
    ops[0].points.every(
      (v) => Math.hypot(v[0] - p.uv[0], v[1] - p.uv[1]) < 0.08,
    ),
  ).toBe(true);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(await saved(page)).toEqual(before);
  expect(errors).toEqual([]);
});

test("zero mask opacity survives reload without brush or fill history entries", async ({
  page,
}) => {
  const errors = errorsOf(page),
    before = await open(page, { kind: "paint", fill: 1 }, false, {
      opacity: 0,
    });
  await expect(
    page.getByLabel("Mask brush opacity", { exact: true }),
  ).toHaveValue("0", { timeout: 15000 });
  await page
    .getByRole("button", { name: "Black mask ink", exact: true })
    .click();
  const initial = await pixels(page),
    states = await page.locator(".tp-history-heading > span").textContent();
  await brush(page);
  expect(await saved(page)).toEqual(before);
  expect(difference(initial, await pixels(page))).toBeLessThan(0.05);
  await uvMode(page);
  await page
    .getByRole("button", { name: "Select mask Object", exact: true })
    .click();
  const p = await point(page);
  await page.mouse.click(p.x, p.y);
  await page
    .getByRole("button", { name: "Fill selected mask region", exact: true })
    .click();
  expect(await saved(page)).toEqual(before);
  await expect(page.locator(".tp-history-heading > span")).toHaveText(states);
  await page.reload();
  await expect(
    page.getByLabel("Mask brush opacity", { exact: true }),
  ).toHaveValue("0", { timeout: 15000 });
  expect(await saved(page)).toEqual(before);
  // Returning to positive opacity must resume real editing, not leave capture
  // or the brush permanently disabled after the zero-strength no-op.
  await uvMode(page);
  await page.getByLabel("Mask brush opacity", { exact: true }).fill("100");
  await page.getByRole("button", { name: "Paint mask", exact: true }).click();
  const next = await point(page);
  await page.mouse.click(next.x, next.y);
  expect((await saved(page)).layers.at(-1).mask.paint.operations).toHaveLength(
    1,
  );
  expect(errors).toEqual([]);
});

for (const mode of ["Lit", "UV"])
  test(`${mode} mask drafts cancel on blur, hidden document and Escape outside the canvas without blocking the next stroke`, async ({
    page,
  }) => {
    const errors = errorsOf(page),
      before = await open(page);
    if (mode === "UV") await uvMode(page);
    await page
      .getByRole("button", { name: "Black mask ink", exact: true })
      .click();
    const target = mode === "UV" ? uv(page) : native(page);
    const initial = await pixels(page, target);
    const states = await page
      .locator(".tp-history-heading > span")
      .textContent();
    async function start() {
      if (mode === "UV") {
        const p = await point(page);
        await page.mouse.move(p.x, p.y);
        await page.mouse.down();
        await settle(page);
      } else await brush(page, 0.5, 0.45, false);
    }
    for (const event of ["blur", "visibilitychange", "escape-away"]) {
      await start();
      expect(difference(initial, await pixels(page, target))).toBeGreaterThan(
        0.01,
      );
      if (mode === "Lit") {
        const r = await target.boundingBox();
        await target.evaluate(
          (c, p) => {
            for (const type of [
              "pointermove",
              "pointerup",
              "pointercancel",
              "lostpointercapture",
              "pointerdown",
            ])
              c.dispatchEvent(
                new PointerEvent(type, {
                  bubbles: true,
                  pointerId: 222,
                  pointerType: "touch",
                  isPrimary: false,
                  button: 0,
                  clientX: p.x,
                  clientY: p.y,
                }),
              );
          },
          { x: r.x + r.width * 0.52, y: r.y + r.height * 0.53 },
        );
        expect(difference(initial, await pixels(page, target))).toBeGreaterThan(
          0.01,
        );
        expect(
          JSON.parse(
            await page.evaluate(() =>
              localStorage.getItem("alloy-texture-project-v1"),
            ),
          ),
        ).toEqual(before);
      }
      if (event === "escape-away") {
        await page
          .getByRole("button", { name: "Save project", exact: true })
          .focus();
        await page.keyboard.press("Escape");
      } else {
        await page.evaluate((type) => {
          if (type === "blur") window.dispatchEvent(new Event("blur"));
          else {
            Object.defineProperty(document, "visibilityState", {
              configurable: true,
              value: "hidden",
            });
            document.dispatchEvent(new Event("visibilitychange"));
            delete document.visibilityState;
          }
        }, event);
      }
      expect(
        await target.evaluate((c) =>
          [c, c.parentElement].some((el) => el.hasPointerCapture(1)),
        ),
      ).toBe(false);
      await page.mouse.up();
      expect(await saved(page)).toEqual(before);
      expect(difference(initial, await pixels(page, target))).toBeLessThan(
        0.05,
      );
      await expect(page.locator(".tp-history-heading > span")).toHaveText(
        states,
      );
    }
    await start();
    await page.mouse.up();
    expect(
      (await saved(page)).layers.at(-1).mask.paint.operations.length,
    ).toBeGreaterThan(0);
    await page
      .getByRole("button", { name: "Undo layer edit", exact: true })
      .click();
    expect(await saved(page)).toEqual(before);
    expect(errors).toEqual([]);
  });
