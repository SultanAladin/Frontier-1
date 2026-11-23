import { test, expect } from "@playwright/test";
const native = (page) =>
    page.locator(".tp-scene-viewport > .canvas-host canvas"),
  uv = (page) => page.getByLabel("UV texture-space canvas", { exact: true });
const components = (doc) => doc.layers.flatMap((l) => l.decals);
const ceramic = {
  type: "material",
  id: "carbon-ceramic",
  name: "Carbon Ceramic",
};
test.describe.configure({ timeout: 180000 });
test.use({ actionTimeout: 60000 });
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
async function ready(page) {
  await expect(native(page)).toHaveAttribute("data-material-ready", "true", {
    timeout: 90000,
  });
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
}
async function open(page, phone = false) {
  if (phone) await page.setViewportSize({ width: 390, height: 844 });
  await page.goto("/?studio=texture");
  await ready(page);
  await expect(
    page.getByRole("button", { name: "Viewport stamp brush", exact: true }),
  ).toBeEnabled();
}
async function project(page) {
  const waiting = page.waitForEvent("download", { timeout: 90000 });
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function drop(page, asset, x = 0.5, y = 0.35, target = native(page)) {
  await target.evaluate(
    (element, { asset, x, y }) => {
      const rect = element.getBoundingClientRect(),
        transfer = new DataTransfer();
      transfer.setData("application/x-alloy-asset", JSON.stringify(asset));
      element.dispatchEvent(
        new DragEvent("dragover", {
          bubbles: true,
          cancelable: true,
          dataTransfer: transfer,
          clientX: rect.x + rect.width * x,
          clientY: rect.y + rect.height * y,
        }),
      );
      element.dispatchEvent(
        new DragEvent("drop", {
          bubbles: true,
          cancelable: true,
          dataTransfer: transfer,
          clientX: rect.x + rect.width * x,
          clientY: rect.y + rect.height * y,
        }),
      );
    },
    { asset, x, y },
  );
}
async function uvPoint(page, part = "lid", chart = 25) {
  const probes = JSON.parse(await uv(page).getAttribute("data-uv-probes")),
    point =
      probes.find((p) => p.partId === part && p.chartId === chart) ||
      probes.find((p) => p.partId === part),
    rect = await uv(page).boundingBox();
  return {
    x: rect.x + point.x,
    y: rect.y + point.y,
    relativeX: point.x / rect.width,
    relativeY: point.y / rect.height,
    uv: point.uv,
  };
}
async function uvMode(page) {
  await page
    .getByLabel("Viewport shading", { exact: true })
    .selectOption("uv");
  await expect(uv(page)).toHaveAttribute(
    "data-uv-tiles",
    "1001,1002,1003,1004",
  );
}
async function pixels(page, region = [0, 0, 1, 1], source = native(page)) {
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
  return source.evaluate((image, region) => {
    const canvas = document.createElement("canvas"),
      x = Math.round(image.width * region[0]),
      y = Math.round(image.height * region[1]);
    canvas.width = Math.max(1, Math.round(image.width * region[2]));
    canvas.height = Math.max(1, Math.round(image.height * region[3]));
    const ctx = canvas.getContext("2d");
    ctx.drawImage(
      image,
      x,
      y,
      canvas.width,
      canvas.height,
      0,
      0,
      canvas.width,
      canvas.height,
    );
    return Array.from(ctx.getImageData(0, 0, canvas.width, canvas.height).data);
  }, region);
}
function difference(a, b) {
  return a.reduce((n, v, i) => n + Math.abs(v - b[i]), 0) / a.length;
}
async function stampAtUV(page, part = "lid", chart = 25) {
  if (!(await page.getByLabel("Decal stamp preset", { exact: true }).count()))
    await page
      .getByRole("button", { name: "Viewport stamp brush", exact: true })
      .click();
  const id = await page
      .getByLabel("Decal stamp preset", { exact: true })
      .inputValue(),
    point = await uvPoint(page, part, chart);
  await drop(
    page,
    { type: "stamp", id },
    point.relativeX,
    point.relativeY,
    uv(page),
  );
  await expect(uv(page)).not.toHaveAttribute("data-uv-artwork-triangles", "0");
  return point;
}

test("a raycast material drop renders only the lid: body/handle/spout pixels stay identical, assignment is one undoable saved snapshot", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    body = await pixels(page, [0.45, 0.55, 0.1, 0.1]),
    handle = await pixels(page, [0.344, 0.446, 0.02, 0.025]),
    spout = await pixels(page, [0.713, 0.446, 0.032, 0.014]),
    lid = await pixels(page, [0.47, 0.389, 0.08, 0.026]);
  await drop(page, ceramic);
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  await page
    .getByLabel("Texture mesh parts", { exact: true })
    .selectOption("");
  expect(difference(body, await pixels(page, [0.45, 0.55, 0.1, 0.1]))).toBe(0);
  expect(
    difference(handle, await pixels(page, [0.344, 0.446, 0.02, 0.025])),
  ).toBe(0);
  expect(
    difference(spout, await pixels(page, [0.713, 0.446, 0.032, 0.014])),
  ).toBe(0);
  expect(
    difference(lid, await pixels(page, [0.47, 0.389, 0.08, 0.026])),
  ).toBeGreaterThan(5);
  const saved = await project(page),
    assigned = saved.layers.find((l) => l.surfaceMaterial);
  expect(assigned.surfaceTarget).toEqual({ objectId: "teapot", partId: "lid" });
  expect(assigned.surfaceMaterial.id).toBe(ceramic.id);
  expect(saved.layers.filter((l) => l.id !== assigned.id)).toEqual(
    before.layers,
  );
  await expect(
    page.getByRole("region", { name: "Assigned part material", exact: true }),
  ).toContainText("Lid only");
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute("data-part-material-count", "0");
  await ready(page);
  expect(await project(page)).toEqual(before);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  expect(await project(page)).toEqual(saved);
  await page.reload();
  await ready(page);
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  expect(await project(page)).toEqual(saved);
  await page.screenshot({ path: ".playwright/parts-uv/lid-only.png" });
  expect(errors).toEqual([]);
});
test("actual shared library drag/drop reaches the scene and preserves its bounds instead of shrinking the viewport", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const bounds = await native(page).boundingBox();
  await page
    .getByRole("button", { name: "Expand content browser", exact: true })
    .click();
  await page
    .getByLabel("Search content assets", { exact: true })
    .fill("Carbon Ceramic");
  const asset = page.getByRole("button", {
    name: "Inspect asset Carbon Ceramic",
    exact: true,
  });
  await expect(asset).toBeVisible();
  expect(await native(page).boundingBox()).toEqual(bounds);
  const start = await asset.boundingBox();
  await page.mouse.move(start.x + start.width / 2, start.y + start.height / 2);
  await page.mouse.down();
  await page.mouse.move(
    start.x + start.width / 2 + 14,
    start.y + start.height / 2 - 10,
    { steps: 4 },
  );
  await expect(
    page.getByRole("region", { name: "Content browser", exact: true }),
  ).toHaveClass(/is-asset-dragging/, { timeout: 10000 });
  await page.mouse.move(
    bounds.x + bounds.width * 0.5,
    bounds.y + bounds.height * 0.35,
    { steps: 10 },
  );
  await page.mouse.up();
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  const saved = await project(page);
  expect(saved.layers[0].surfaceTarget.partId).toBe("lid");
  expect(saved.layers[0].surfaceMaterial.id).toBe(ceramic.id);
  expect(await native(page).boundingBox()).toEqual(bounds);
  expect(errors).toEqual([]);
});
test("empty/unsupported/unknown drops and locked part targets never mutate the document; visibility and exact zero opacity restore the neutral part", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page);
  for (const [asset, x, y] of [
    [ceramic, 0.05, 0.08],
    [{ type: "brush", id: "round" }, 0.5, 0.35],
    [{ type: "material", id: "missing" }, 0.5, 0.35],
  ]) {
    await drop(page, asset, x, y);
    expect(await project(page)).toEqual(before);
  }
  await drop(page, ceramic);
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  await page
    .getByRole("button", { name: "Lock selected layer", exact: true })
    .click();
  const locked = await project(page);
  await drop(page, { type: "material", id: "brushed-aluminium" });
  expect(await project(page)).toEqual(locked);
  await expect(page.getByRole("alert")).toContainText(/locked/);
  await page
    .getByRole("button", { name: "Unlock selected layer", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Hide selected layer", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute("data-part-material-count", "0");
  await page
    .getByRole("button", { name: "Show selected layer", exact: true })
    .click();
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  await page.getByLabel("Layer opacity value", { exact: true }).fill("0");
  await expect(native(page)).toHaveAttribute("data-part-material-count", "0");
  await page.getByLabel("Layer opacity value", { exact: true }).fill("100");
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  expect(errors).toEqual([]);
});
test("UV mode shows real charts and UDIMs, permits scoped material drops, guards overlapping source UVs and preserves one WebGL canvas with independent zoom", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await native(page).evaluate((c) => {
    window.__uvOriginalCanvas = c;
  });
  await uvMode(page);
  await expect(uv(page)).toHaveAttribute("data-uv-charts", "32");
  await expect(uv(page)).toHaveAttribute("data-uv-triangles", "25440");
  const point = await uvPoint(page, "handle", 13);
  await drop(page, ceramic, point.relativeX, point.relativeY, uv(page));
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  const saved = await project(page);
  expect(saved.layers[0].surfaceTarget.partId).toBe("handle");
  await page.getByRole("button", { name: "Zoom in UVs", exact: true }).click();
  await expect(page.getByLabel("UV zoom", { exact: true })).toHaveText("125%");
  await expect(native(page)).toHaveAttribute("data-zoom", "100");
  await page
    .getByLabel("UV coordinate set", { exact: true })
    .selectOption("source");
  await expect(uv(page)).toHaveAttribute("data-uv-tiles", "1001");
  await expect(page.locator(".tp-uv-source-notice")).toContainText("overlap");
  await expect(
    page.getByRole("button", { name: "Viewport stamp brush", exact: true }),
  ).toBeDisabled();
  await drop(page, ceramic, 0.5, 0.5, uv(page));
  expect(await project(page)).toEqual(saved);
  await page
    .getByLabel("UV coordinate set", { exact: true })
    .selectOption("authoring");
  await expect(uv(page)).toHaveAttribute(
    "data-uv-tiles",
    "1001,1002,1003,1004",
  );
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await ready(page);
  expect(
    await native(page).evaluate((c) => c === window.__uvOriginalCanvas),
  ).toBe(true);
  await expect(page.getByLabel("Teapot zoom", { exact: true })).toHaveText(
    "100%",
  );
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("wireframe");
  await expect(native(page)).toHaveAttribute("data-wireframe", "true");
  await ready(page);
  expect(errors).toEqual([]);
});
test("stamp drag/drop and brush hits in UV space create real part-clipped 3D artwork; gutters miss, UV poses persist and never attach a 3D gizmo", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const initial = await pixels(page);
  await uvMode(page);
  await stampAtUV(page);
  let saved = await project(page);
  expect(components(saved)).toHaveLength(1);
  const component = components(saved)[0];
  expect(component.space).toBe("uv");
  expect(component.target.partId).toBe("lid");
  expect(component.uvPosition[0]).toBeCloseTo(1.125);
  expect(component.size).toEqual([0.18, 0.18]);
  const before = saved,
    camera = JSON.parse(await uv(page).getAttribute("data-uv-camera")),
    rect = await uv(page).boundingBox();
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  await page.mouse.click(
    rect.x + camera.x + camera.scale,
    rect.y + camera.y - 0.5 * camera.scale,
  );
  expect(await project(page)).toEqual(before);
  const point = await uvPoint(page, "lid", 26);
  await page.mouse.click(point.x, point.y);
  await expect(native(page)).toHaveAttribute("data-decal-count", "2");
  saved = await project(page);
  expect(components(saved)).toHaveLength(2);
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await ready(page);
  await expect(native(page)).toHaveAttribute("data-decal-gizmo", "off");
  expect(
    Number(await native(page).getAttribute("data-decal-triangles")),
  ).toBeGreaterThan(100);
  expect(difference(initial, await pixels(page))).toBeGreaterThan(0.001);
  await page.reload();
  await ready(page);
  expect(await project(page)).toEqual(saved);
  await uvMode(page);
  await expect(uv(page)).not.toHaveAttribute("data-uv-artwork-triangles", "0");
  await page.screenshot({ path: ".playwright/parts-uv/udim-decals.png" });
  expect(errors).toEqual([]);
});
test("UV move/rotate/scale use one confirm transaction, cancel restores it, controls use UV units and locks cannot be bypassed", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await uvMode(page);
  await stampAtUV(page);
  let saved = await project(page),
    component = components(saved)[0];
  await expect(
    page.getByLabel("Decal projection depth", { exact: true }),
  ).toHaveCount(0);
  expect(
    Number(
      await page.getByLabel("Decal UV U value", { exact: true }).inputValue(),
    ),
  ).toBeCloseTo(component.uvPosition[0], 4);
  let p = await uvPoint(page),
    scale = JSON.parse(await uv(page).getAttribute("data-uv-camera")).scale;
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x + scale * 0.035, p.y - scale * 0.02, { steps: 5 });
  expect(
    JSON.parse(
      await page.evaluate(() =>
        localStorage.getItem("alloy-texture-project-v1"),
      ),
    ),
  ).toEqual(saved);
  await page.mouse.up();
  let moved = await project(page);
  expect(components(moved)[0].uvPosition[0]).toBeCloseTo(
    component.uvPosition[0] + 0.035,
    3,
  );
  await page
    .getByRole("button", { name: "Undo layer edit", exact: true })
    .click();
  expect(await project(page)).toEqual(saved);
  await page
    .getByRole("button", { name: "Redo layer edit", exact: true })
    .click();
  expect(await project(page)).toEqual(moved);
  component = components(moved)[0];
  const c = JSON.parse(await uv(page).getAttribute("data-uv-camera")),
    r = await uv(page).boundingBox();
  p = {
    x: r.x + c.x + component.uvPosition[0] * c.scale,
    y: r.y + c.y - component.uvPosition[1] * c.scale,
  };
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x + 8, p.y + 6, { steps: 3 });
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect(await project(page)).toEqual(moved);
  for (const event of ["pointercancel", "blur"]) {
    await page.mouse.move(p.x, p.y);
    await page.mouse.down();
    await page.mouse.move(p.x + 8, p.y + 6, { steps: 3 });
    if (event === "pointercancel")
      await uv(page).dispatchEvent("pointercancel", { pointerId: 1 });
    else await page.evaluate(() => window.dispatchEvent(new Event("blur")));
    await page.mouse.up();
    expect(await project(page)).toEqual(moved);
  }
  await page
    .getByRole("button", { name: "Decal rotate gizmo", exact: true })
    .click();
  await page.mouse.move(p.x + scale * 0.05, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x, p.y - scale * 0.05, { steps: 5 });
  await page.mouse.up();
  const rotated = await project(page);
  expect(
    Math.abs(components(rotated)[0].roll - component.roll),
  ).toBeGreaterThan(30);
  await page
    .getByRole("button", { name: "Decal scale gizmo", exact: true })
    .click();
  await page.mouse.move(p.x + scale * 0.04, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x + scale * 0.06, p.y, { steps: 4 });
  await page.mouse.up();
  const scaled = await project(page);
  expect(components(scaled)[0].size[0]).toBeGreaterThan(component.size[0]);
  await page
    .getByRole("button", { name: "Lock decal component", exact: true })
    .click();
  const locked = await project(page);
  await expect(
    page.getByLabel("Decal UV U value", { exact: true }),
  ).toBeDisabled();
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x + 15, p.y);
  await page.mouse.up();
  expect(await project(page)).toEqual(locked);
  expect(errors).toEqual([]);
});
test("UV mask coverage and inversion change the real lid material, not body pixels or a decorative UV image", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  await drop(page, ceramic);
  await expect(native(page)).toHaveAttribute("data-part-material-count", "1");
  await ready(page);
  await page
    .getByLabel("Texture mesh parts", { exact: true })
    .selectOption("");
  const body = await pixels(page, [0.45, 0.55, 0.1, 0.1]),
    material = await pixels(page);
  await uvMode(page);
  await stampAtUV(page);
  await page
    .getByRole("checkbox", { name: "Use decal as mask", exact: true })
    .check();
  await expect(native(page)).toHaveAttribute("data-decal-mask-count", "1");
  await page.getByLabel("Viewport shading", { exact: true }).selectOption("shaded");
  await ready(page);
  await page
    .getByLabel("Texture mesh parts", { exact: true })
    .selectOption("");
  const masked = await pixels(page);
  expect(difference(material, masked)).toBeGreaterThan(0.05);
  expect(difference(body, await pixels(page, [0.45, 0.55, 0.1, 0.1]))).toBe(0);
  await page
    .getByRole("checkbox", { name: "Invert decal mask", exact: true })
    .check();
  await ready(page);
  expect(difference(masked, await pixels(page))).toBeGreaterThan(0.05);
  expect(components(await project(page))[0]).toMatchObject({
    space: "uv",
    asMask: true,
    inverted: true,
    target: { objectId: "teapot", partId: "lid" },
  });
  expect(errors).toEqual([]);
});
test("phone UV tools, part selectors, authoring/source controls and decal placement fit without overflowing or replacing the shared scene", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page, true);
  await uvMode(page);
  await expect(
    page.getByLabel("Texture mesh parts", { exact: true }),
  ).toBeVisible();
  await expect(
    page.getByLabel("UV coordinate set", { exact: true }),
  ).toBeVisible();
  const rect = await uv(page).boundingBox();
  expect(rect.width).toBeGreaterThan(300);
  expect(rect.height).toBeGreaterThan(180);
  expect(
    JSON.parse(await uv(page).getAttribute("data-uv-camera")).scale,
  ).toBeGreaterThan(60);
  await page
    .getByRole("button", { name: "Viewport stamp brush", exact: true })
    .click();
  const point = await uvPoint(page);
  await page.mouse.click(point.x, point.y);
  await expect(native(page)).toHaveAttribute("data-decal-count", "1");
  expect(components(await project(page))[0].space).toBe("uv");
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(390);
  const controls = await page
    .locator(".tp-viewport-toolbar,.tp-authoring-bar")
    .evaluateAll((elements) =>
      elements.map((el) => ({
        left: el.getBoundingClientRect().left,
        right: el.getBoundingClientRect().right,
      })),
    );
  for (const control of controls) {
    expect(control.left).toBeGreaterThanOrEqual(0);
    expect(control.right).toBeLessThanOrEqual(390);
  }
  await page.screenshot({
    path: ".playwright/parts-uv/phone-uv.png",
    timeout: 90000,
  });
  expect(errors).toEqual([]);
});
