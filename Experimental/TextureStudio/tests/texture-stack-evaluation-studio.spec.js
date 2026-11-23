import { test, expect } from "@playwright/test";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import {
  createTextureDocument,
  createTextureLayer,
  validateTextureDocument,
  patchTextureLayer,
  moveTextureLayer,
} from "../src/textureDocument.js";
import { materials } from "../src/materials.js";
import {
  TEXTURE_BAKE_UV_SET,
  TEXTURE_MESH_MAP_SCHEMA,
} from "../src/textureMeshMaps.js";
const png = JSON.parse(
  readFileSync(
    new URL("./fixtures/texture-mesh-map-pngs.json", import.meta.url),
    "utf8",
  ),
);
const native = (page) =>
  page.locator(".tp-scene-viewport > .canvas-host canvas");
const recipe = materials.find((m) => m.id === "racing-green");
const coat = (options = {}) =>
  createTextureLayer("material", {
    id: "upper-car-paint",
    name: "Upper car paint",
    surfaceMaterial: recipe,
    sourceAsset: { type: "material", id: recipe.id, name: recipe.name },
    ...options,
  });
const map = (dataUrl) => ({ dataUrl, width: 16, height: 16 });
const support = (maps) => ({
  schema: TEXTURE_MESH_MAP_SCHEMA,
  target: "teapot",
  uvSet: TEXTURE_BAKE_UV_SET,
  maps,
});
test.describe.configure({ timeout: 360000 });
test.use({ actionTimeout: 60000 });
function watch(page) {
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
async function ready(page) {
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 120000,
  });
  await expect(native(page)).toHaveAttribute("data-layer-stack-ready", "true", {
    timeout: 120000,
  });
  await settle(page);
}
async function open(page, doc = createTextureDocument()) {
  await page.addInitScript(
    (doc) =>
      localStorage.setItem("alloy-texture-project-v1", JSON.stringify(doc)),
    doc,
  );
  await page.goto("/?studio=texture");
  await ready(page);
  return doc;
}
async function save(page) {
  const wait = page.waitForEvent("download", {
    predicate: (download) =>
      download.suggestedFilename().endsWith(".texture.json"),
  });
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const download = await wait;
  expect(await download.failure()).toBe(null);
  const data = readFileSync(await download.path());
  expect(data.length).toBeGreaterThan(0);
  return JSON.parse(data.toString());
}
async function load(page, doc) {
  await page.getByLabel("Open texture layer JSON").setInputFiles({
    name: "stack.texture.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(doc)),
  });
  await ready(page);
}
async function pixels(page) {
  await settle(page);
  return native(page).evaluate((c) => {
    const out = document.createElement("canvas");
    out.width = 320;
    out.height = 200;
    const ctx = out.getContext("2d");
    ctx.drawImage(c, 0, 0, 320, 200);
    return Array.from(ctx.getImageData(0, 0, 320, 200).data);
  });
}
const diff = (a, b) =>
  a.reduce((v, n, i) => v + Math.abs(n - b[i]), 0) / a.length;
async function stamped(page) {
  await open(page);
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  const r = await native(page).boundingBox();
  await page.mouse.click(r.x + r.width * 0.5, r.y + r.height * 0.5);
  await expect(native(page)).toHaveAttribute("data-decal-count", "1");
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  return save(page);
}

test("upper metallic car-paint covers a lower stamp; reordering and undo reveal it, not a permanently topmost decal", async ({
  page,
}) => {
  const errors = watch(page),
    base = await stamped(page),
    stampedPixels = await pixels(page);
  const upper = coat(),
    doc = validateTextureDocument({ ...base, layers: [upper, ...base.layers] });
  await load(page, doc);
  const covered = await pixels(page);
  await expect(native(page)).toHaveAttribute("data-system-base-parts", "[]");
  const plain = validateTextureDocument({
    ...doc,
    layers: doc.layers.map((l) => ({ ...l, decals: [] })),
  });
  await load(page, plain);
  expect(diff(covered, await pixels(page))).toBeLessThan(0.12);
  await load(page, doc);
  await page
    .getByRole("button", { name: "Select Upper car paint", exact: true })
    .click();
  // Moving the material below the stamped base row must expose the same stamp.
  await page
    .getByRole("button", { name: "Move layer down", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Move layer down", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Move layer down", exact: true })
    .click();
  await ready(page);
  const revealed = await pixels(page);
  expect(diff(covered, revealed)).toBeGreaterThan(0.1);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await ready(page);
  expect(diff(covered, await pixels(page))).toBeLessThan(0.12);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await ready(page);
  expect(diff(revealed, await pixels(page))).toBeLessThan(0.12);
  expect(diff(stampedPixels, covered)).toBeGreaterThan(0.1);
  expect((await save(page)).layers.flatMap((l) => l.decals)).toHaveLength(1);
  await page.screenshot({
    path: ".playwright/layer-evaluation/stamp-above-material.png",
  });
  expect(errors).toEqual([]);
});
test("hidden, zero-opacity and black-masked upper materials reveal lower stamp and automatically restore white fallback", async ({
  page,
}) => {
  const errors = watch(page),
    base = await stamped(page),
    baseline = await pixels(page),
    upper = coat();
  const doc = validateTextureDocument({
    ...base,
    layers: [upper, ...base.layers],
  });
  await load(page, doc);
  const covered = await pixels(page);
  for (const patch of [
    { visible: false },
    { opacity: 0 },
    { mask: { kind: "fill", fill: 0 } },
  ]) {
    await load(
      page,
      validateTextureDocument(patchTextureLayer(doc, upper.id, patch)),
    );
    expect(diff(baseline, await pixels(page))).toBeLessThan(0.15);
    expect(
      JSON.parse(await native(page).getAttribute("data-system-base-parts")),
    ).toHaveLength(4);
  }
  await load(
    page,
    validateTextureDocument(patchTextureLayer(doc, upper.id, { opacity: 50 })),
  );
  const half = await pixels(page);
  expect(diff(half, covered)).toBeGreaterThan(0.1);
  expect(diff(half, baseline)).toBeGreaterThan(0.1);
  await page
    .getByRole("button", { name: "Inspect locked system Layer 0", exact: true })
    .click();
  await expect(
    page.getByLabel("System Layer 0", { exact: true }),
  ).toContainText("cannot be painted, moved, deleted or unlocked");
  expect(
    (await save(page)).layers.some((l) => l.id === "alloy-system-layer-0"),
  ).toBe(false);
  expect(errors).toEqual([]);
});
test("a material dropped on an ordinary Material layer stores a real global snapshot instead of only a source label", async ({
  page,
}) => {
  const errors = watch(page);
  await open(page);
  await page.getByRole("button", { name: "Add layer", exact: true }).click();
  await page.getByRole("menuitem", { name: /^Material layer/ }).click();
  const selected = page.getByRole("button", {
    name: "Select Material layer",
    exact: true,
  });
  await selected.evaluate(
    (el, asset) => {
      const row = el.closest(".tp-layer-row");
      const data = new DataTransfer();
      data.setData("application/x-alloy-asset", JSON.stringify(asset));
      row.dispatchEvent(
        new DragEvent("drop", {
          bubbles: true,
          cancelable: true,
          dataTransfer: data,
        }),
      );
    },
    { type: "material", id: recipe.id, name: recipe.name },
  );
  await ready(page);
  const saved = await save(page),
    layer = saved.layers.find((l) => l.name === "Material layer");
  expect(layer.surfaceMaterial.id).toBe(recipe.id);
  expect(layer.surfaceTarget).toBeUndefined();
  await expect(page.getByLabel("Assigned stack material")).toContainText(
    "Whole object",
  );
  await expect(native(page)).toHaveAttribute("data-system-base-parts", "[]");
  expect(errors).toEqual([]);
});
test("baked AO stays active after Layer 0 color is replaced, while tangent and bevel normals reorient rather than multiply RGB", async ({
  page,
}) => {
  const errors = watch(page),
    upper = coat(),
    base = validateTextureDocument({
      ...createTextureDocument(),
      layers: [upper],
    });
  await open(page, base);
  const normal = await pixels(page);
  const occluded = validateTextureDocument({
    ...base,
    meshMaps: support({ occlusion: map(png.blackAO) }),
  });
  await load(page, occluded);
  await expect(native(page)).toHaveAttribute(
    "data-mesh-support-maps",
    '["occlusion"]',
  );
  expect(diff(normal, await pixels(page))).toBeGreaterThan(0.1);
  await expect(native(page)).toHaveAttribute("data-system-base-parts", "[]");
  await load(
    page,
    validateTextureDocument({
      ...base,
      meshMaps: support({
        normal: map(png.tiltNormal),
        "bevel-normal": map(png.bevelNormal),
      }),
    }),
  );
  await expect(native(page)).toHaveAttribute(
    "data-mesh-support-maps",
    '["normal","bevel-normal"]',
  );
  expect(diff(normal, await pixels(page))).toBeGreaterThan(0.1);
  const after = await save(page);
  expect(after.layers[0].surfaceMaterial).toEqual(
    base.layers[0].surfaceMaterial,
  );
  await page.screenshot({
    path: ".playwright/layer-evaluation/material-with-baked-normals.png",
  });
  expect(errors).toEqual([]);
});

test("Texture-to-Baking uses a matching teapot low mesh and hands genuine high/low AO, normal and bevel maps back without replacing layers", async ({
  page,
}) => {
  const errors = watch(page);
  await open(page);
  const before = await save(page);
  await page
    .getByRole("button", { name: "Inspect locked system Layer 0", exact: true })
    .click();
  await page
    .getByRole("button", {
      name: "Bake matching teapot support · 512",
      exact: true,
    })
    .click();
  await expect(
    page.getByRole("dialog", { name: "Baking studio", exact: true }),
  ).toBeVisible();
  await expect(
    page.getByLabel("Mesh bake resolution", { exact: true }),
  ).toHaveValue("512");
  // Keep the matching target at the default 512 for real output verification.
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await expect(
    page.getByText(/AO \/ normal support attached to Texture's locked Layer 0/),
  ).toBeVisible({ timeout: 180000 });
  const attached = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("alloy-texture-project-v1")),
  );
  expect(attached.layers).toEqual(before.layers);
  expect(Object.keys(attached.meshMaps.maps)).toEqual([
    "occlusion",
    "normal",
    "bevel-normal",
  ]);
  for (const m of Object.values(attached.meshMaps.maps))
    expect(m.width).toBe(512);
  // The published PNGs came from the real worker, not the synthetic shader fixtures.
  expect(attached.meshMaps.maps.occlusion.dataUrl).not.toBe(png.blackAO);
  const supportSize = Buffer.byteLength(JSON.stringify(attached.meshMaps)),
    projectSize = Buffer.byteLength(JSON.stringify(attached));
  expect(supportSize).toBeLessThanOrEqual(700000);
  expect(projectSize).toBeLessThanOrEqual(1024 * 1024);
  // Inspection artifacts stay in ignored QA storage, not in audited deliverables.
  const artifactDir = ".playwright/layer-evaluation/bake-512";
  mkdirSync(artifactDir, { recursive: true });
  const mapSizes = {};
  for (const [role, value] of Object.entries(attached.meshMaps.maps)) {
    const bytes = Buffer.from(value.dataUrl.split(",")[1], "base64");
    writeFileSync(`${artifactDir}/${role}.png`, bytes);
    mapSizes[role] = bytes.length;
  }
  writeFileSync(`${artifactDir}/project.json`, JSON.stringify(attached));
  writeFileSync(
    `${artifactDir}/sizes.json`,
    JSON.stringify(
      {
        resolution: 512,
        lowSegments: 20,
        highSegments: 24,
        supportSize,
        projectSize,
        mapSizes,
      },
      null,
      2,
    ),
  );
  await page
    .locator(".bk-studio .tp-workspace-switch")
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  await ready(page);
  await expect(native(page)).toHaveAttribute(
    "data-mesh-support-maps",
    '["occlusion","normal","bevel-normal"]',
  );
  expect((await save(page)).layers).toEqual(before.layers);
  expect(errors).toEqual([]);
});

test("UV view uses the evaluated appearance rather than drawing every lower stamp over upper material; hidden material restores it while native rendering is paused", async ({
  page,
}) => {
  const errors = watch(page),
    base = await stamped(page),
    upper = coat(),
    doc = validateTextureDocument({ ...base, layers: [upper, ...base.layers] });
  await load(page, doc);
  await expect(native(page)).toHaveAttribute("data-layer-atlas-ready", "true");
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  const uv = page.getByLabel("UV texture-space canvas", { exact: true });
  await expect(uv).toHaveAttribute(
    "data-layer-atlas",
    "ordered-lit-appearance",
  );
  const samples = () =>
    uv.evaluate((c) => {
      const out = document.createElement("canvas");
      out.width = 320;
      out.height = 200;
      const ctx = out.getContext("2d");
      ctx.drawImage(c, 0, 0, 320, 200);
      return Array.from(ctx.getImageData(0, 0, 320, 200).data);
    });
  const revision = Number(await uv.getAttribute("data-layer-atlas-revision")),
    withLower = await samples();
  await load(
    page,
    validateTextureDocument({
      ...doc,
      layers: doc.layers.map((l) => ({ ...l, decals: [] })),
    }),
  );
  await expect
    .poll(async () =>
      Number(await uv.getAttribute("data-layer-atlas-revision")),
    )
    .toBeGreaterThan(revision);
  expect(diff(withLower, await samples())).toBeLessThan(0.15);
  await load(page, doc);
  let before = Number(await uv.getAttribute("data-layer-atlas-revision"));
  await page
    .getByRole("button", { name: "Hide Upper car paint", exact: true })
    .click();
  await expect
    .poll(async () =>
      Number(await uv.getAttribute("data-layer-atlas-revision")),
    )
    .toBeGreaterThan(before);
  await expect
    .poll(async () => diff(withLower, await samples()), { timeout: 20000 })
    .toBeGreaterThan(0.1);
  await page.screenshot({ path: ".playwright/layer-evaluation/uv-stack.png" });
  expect(errors).toEqual([]);
});

test("native glass above an opaque coat visibly evaluates after it; moving glass below that coat restores exactly the opaque result", async ({
  page,
}) => {
  const errors = watch(page),
    glass = createTextureLayer("material", {
      id: "upper-optical-glass",
      name: "Upper optical glass",
      surfaceMaterial: {
        ...materials.find((m) => m.id === "crystal-glass"),
        color: "#9a3554",
        roughness: 0.18,
        depth: 0.4,
        ior: 1.3,
      },
    }),
    doc = validateTextureDocument({
      ...createTextureDocument(),
      layers: [glass, coat(), ...createTextureDocument().layers],
    });
  await open(page, doc);
  const withGlass = await pixels(page);
  await page.screenshot({
    path: ".playwright/optical-evaluation/glass-over-carpaint.png",
  });
  await page
    .getByRole("button", { name: "Hide Upper optical glass", exact: true })
    .click();
  await ready(page);
  const opaqueOnly = await pixels(page);
  expect(diff(withGlass, opaqueOnly)).toBeGreaterThan(0.15);
  await page
    .getByRole("button", { name: "Show Upper optical glass", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Select Upper optical glass", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Move layer down", exact: true })
    .click();
  await ready(page);
  await expect
    .poll(async () => diff(opaqueOnly, await pixels(page)), { timeout: 20000 })
    .toBeLessThan(0.15);
  const saved = await save(page);
  expect(saved.layers.find((l) => l.id === glass.id).surfaceMaterial.type).toBe(
    5,
  );
  expect(errors).toEqual([]);
});

test("native wax, partial optical opacity and a black optical mask evaluate against lower paint without corrupting native recipe values", async ({
  page,
}) => {
  // SwiftShader may block each new optical/mask shader and readback for tens
  // of seconds; preserve the framebuffer thresholds, not a 20s wall-clock cap.
  test.setTimeout(600000);
  const errors = watch(page),
    base = createTextureDocument(),
    metal = coat(),
    wax = createTextureLayer("material", {
      id: "upper-wax",
      name: "Upper wax",
      surfaceMaterial: materials.find((m) => m.id === "beeswax"),
    }),
    opaque = validateTextureDocument({
      ...base,
      layers: [metal, ...base.layers],
    }),
    doc = validateTextureDocument({
      ...base,
      layers: [wax, metal, ...base.layers],
    });
  await open(page, opaque);
  const without = await pixels(page);
  await load(page, doc);
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "1",
  );
  const full = await pixels(page);
  expect(diff(full, without)).toBeGreaterThan(0.1);
  const params = JSON.parse(
    await native(page).getAttribute("data-optical-stack-materials"),
  );
  expect(params[0].transmission).toBeCloseTo(0.16);
  for (const patch of [
    { visible: false },
    { opacity: 0 },
    { mask: { kind: "fill", fill: 0 } },
  ]) {
    await load(
      page,
      validateTextureDocument(patchTextureLayer(doc, wax.id, patch)),
    );
    // Also wait for the expected authored passes: ready can still refer to
    // the prior import for one microtask before the new compile job begins.
    await expect(native(page)).toHaveAttribute(
      "data-layer-pass-count",
      patch.mask ? "2" : "1",
    );
    await expect
      .poll(async () => diff(without, await pixels(page)), { timeout: 120000 })
      .toBeLessThan(0.15);
  }
  await load(
    page,
    validateTextureDocument(patchTextureLayer(doc, wax.id, { opacity: 50 })),
  );
  const half = await pixels(page);
  expect(diff(half, full)).toBeGreaterThan(0.1);
  expect(diff(half, without)).toBeGreaterThan(0.1);
  await load(
    page,
    validateTextureDocument(
      moveTextureLayer(doc, wax.id, null, base.layers[0].id),
    ),
  );
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "0",
  );
  expect(diff(without, await pixels(page))).toBeLessThan(0.15);
  await load(page, doc);
  await page
    .getByRole("button", { name: "Hide Upper wax", exact: true })
    .click();
  await ready(page);
  expect(diff(without, await pixels(page))).toBeLessThan(0.15);
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await ready(page);
  expect(diff(full, await pixels(page))).toBeLessThan(0.15);
  expect(
    (await save(page)).layers.find((l) => l.id === wax.id).surfaceMaterial
      .translucency,
  ).toBeCloseTo(0.16);
  expect(errors).toEqual([]);
});

test("stamps on both sides of native glass obey document order; a covered lower stamp never leaks through optical history", async ({
  page,
}) => {
  const errors = watch(page),
    base = await stamped(page),
    owner = base.layers.find((l) => l.decals.length),
    glass = createTextureLayer("material", {
      id: "glass-barrier",
      name: "Glass barrier",
      surfaceMaterial: {
        ...materials.find((m) => m.id === "crystal-glass"),
        color: "#875744",
        roughness: 0.14,
        ior: 1.2,
        depth: 0.25,
      },
    }),
    metal = coat(),
    doc = validateTextureDocument({
      ...base,
      layers: [glass, metal, ...base.layers],
    }),
    strip = (d) =>
      validateTextureDocument({
        ...d,
        layers: d.layers.map((l) => ({ ...l, decals: [] })),
      });
  await load(page, doc);
  const covered = await pixels(page);
  await load(page, strip(doc));
  expect(diff(covered, await pixels(page))).toBeLessThan(0.15);
  const lowerStamp = validateTextureDocument(
    moveTextureLayer(doc, owner.id, null, metal.id),
  );
  await load(page, lowerStamp);
  const throughGlass = await pixels(page);
  expect(diff(covered, throughGlass)).toBeGreaterThan(0.1);
  const above = validateTextureDocument(
    moveTextureLayer(lowerStamp, owner.id, null, glass.id),
  );
  await load(page, above);
  const aboveGlass = await pixels(page);
  expect(diff(throughGlass, aboveGlass)).toBeGreaterThan(0.1);
  const plan = JSON.parse(
    await native(page).getAttribute("data-optical-stack-history"),
  );
  expect(plan.at(-1).kind).toBe("ordinary");
  expect(plan.at(-1).names.some((n) => n.startsWith("decal-"))).toBe(true);
  const atlas = await native(page).getAttribute("data-layer-atlas-hash");
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-uv-pass-count",
    "1",
  );
  await expect(
    page.getByLabel("UV texture-space canvas", { exact: true }),
  ).toBeVisible();
  expect(atlas).not.toBe("");
  expect((await save(page)).layers.flatMap((l) => l.decals)).toHaveLength(1);
  await page.screenshot({
    path: ".playwright/optical-evaluation/stamp-and-glass-uv.png",
  });
  expect(errors).toEqual([]);
});

test("multiple native optical rows retain AO/RNM support and update lit UV while paused, then survive resize and return to single opaque coverage", async ({
  page,
}) => {
  const errors = watch(page),
    base = createTextureDocument(),
    metal = coat(),
    glass = createTextureLayer("material", {
      id: "layer-glass",
      name: "Layer glass",
      surfaceMaterial: {
        ...materials.find((m) => m.id === "crystal-glass"),
        color: "#967254",
        roughness: 0.15,
      },
    }),
    wax = createTextureLayer("material", {
      id: "layer-wax",
      name: "Layer wax",
      surfaceMaterial: materials.find((m) => m.id === "beeswax"),
    }),
    doc = validateTextureDocument({
      ...base,
      layers: [wax, glass, metal, ...base.layers],
      meshMaps: support({
        occlusion: map(png.blackAO),
        normal: map(png.tiltNormal),
        "bevel-normal": map(png.bevelNormal),
      }),
    });
  await open(page, doc);
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "2",
  );
  const original = await pixels(page);
  await expect(native(page)).toHaveAttribute(
    "data-mesh-support-maps",
    /occlusion/,
  );
  const oldHash = await native(page).getAttribute("data-layer-atlas-hash");
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-uv-pass-count",
    "2",
  );
  await page
    .getByRole("button", { name: "Hide Layer wax", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-uv-pass-count",
    "1",
  );
  await expect
    .poll(() => native(page).getAttribute("data-layer-atlas-hash"), {
      timeout: 30000,
    })
    .not.toBe(oldHash);
  await page
    .getByRole("button", { name: "Show Layer wax", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-uv-pass-count",
    "2",
  );
  await page.setViewportSize({ width: 1050, height: 750 });
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await ready(page);
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "2",
  );
  const size = JSON.parse(
    await native(page).getAttribute("data-optical-stack-size"),
  );
  expect(size[0] * size[1]).toBeLessThanOrEqual(1024 * 1024);
  expect((await pixels(page)).some((n) => n > 0)).toBe(true);
  await page.setViewportSize({ width: 1440, height: 900 });
  await ready(page);
  await expect
    .poll(async () => diff(original, await pixels(page)), { timeout: 20000 })
    .toBeLessThan(0.2);
  const covered = validateTextureDocument({
    ...doc,
    layers: [metal, wax, glass, ...base.layers],
  });
  await load(page, covered);
  await expect(native(page)).toHaveAttribute(
    "data-optical-stack-pass-count",
    "0",
  );
  const frame = await pixels(page);
  await load(
    page,
    validateTextureDocument({ ...covered, layers: [metal, ...base.layers] }),
  );
  expect(diff(frame, await pixels(page))).toBeLessThan(0.15);
  const saved = await save(page);
  expect(saved.meshMaps.maps.normal.dataUrl).toBe(png.tiltNormal);
  expect(errors).toEqual([]);
});
