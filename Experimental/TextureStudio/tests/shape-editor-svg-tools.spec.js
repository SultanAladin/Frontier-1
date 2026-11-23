import { test, expect } from "@playwright/test";
import {
  patternStarter,
  patternLayer,
  validatePattern,
} from "../src/patternDocument.js";
import { worldPathLayer } from "../src/shapeEditorGeometry.js";
import {
  worldPathNodes,
  pathSegment,
  cubicPathPoint,
} from "../src/pathEditorGeometry.js";
async function open(page, { snap = false } = {}) {
  await page.goto("/?studio=pattern&pattern=blank&material=natural-cotton");
  await expect(page.getByLabel("Pattern design canvas")).toBeVisible();
  if (!snap)
    await page
      .getByRole("button", { name: "Smart alignment guides", exact: true })
      .click();
}
async function screen(page, x, y) {
  return page.locator('[data-coordinate-space="tile"]').evaluate(
    (g, p) => {
      const v = new DOMPoint(p.x, p.y).matrixTransform(g.getScreenCTM());
      return { x: v.x, y: v.y };
    },
    { x, y },
  );
}
async function at(page, x, y) {
  const p = await screen(page, x, y);
  await page.mouse.move(p.x, p.y);
}
async function click(page, x, y) {
  const p = await screen(page, x, y);
  await page.mouse.click(p.x, p.y);
}
async function draw(page, label, x, y, endX, endY) {
  await page.getByRole("button", { name: label, exact: true }).click();
  await at(page, x, y);
  await page.mouse.down();
  await at(page, endX, endY);
  await page.mouse.up();
}
async function readDownload(download) {
  const parts = [];
  for await (const c of await download.createReadStream()) parts.push(c);
  return Buffer.concat(parts).toString();
}
async function save(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  return JSON.parse(await readDownload(await waiting));
}
async function draft(page) {
  await expect(page.locator(".se-draft-status")).toContainText(
    "Path draft saved locally",
  );
  return page.evaluate(
    () => JSON.parse(localStorage.getItem("alloy-pattern-draft-v2")).doc,
  );
}
async function importLayers(page, layers) {
  await page.locator('.pe-overlay input[accept*="json"]').setInputFiles({
    name: "tools.pattern.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(validatePattern({ ...patternStarter("Blank"), layers })),
    ),
  });
}
const rows = (page) => page.locator(".pe-layers button");
const svgSample = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 150"><defs><linearGradient id="shade"><stop offset="0" stop-color="#aabbcc"/><stop offset="1" stop-color="#c08052"/></linearGradient><path id="leaf" d="M0 40 Q30 0 60 40 Q30 80 0 40Z"/><clipPath id="clip"><rect width="300" height="150" rx="15"/></clipPath><mask id="fade"><rect width="300" height="150" fill="white"/></mask></defs><g clip-path="url(#clip)" mask="url(#fade)"><rect width="300" height="150" style="fill:#eee8dc;stroke:#263c48;stroke-width:3"/><use href="#leaf" x="35" y="30" fill="url(#shade)"/><use href="#leaf" x="175" y="30" fill="url(#shade)"/><text x="150" y="135" text-anchor="middle" font-size="18">SVG artwork</text></g></svg>`;

test("curve stroke and fill controls are visible in the workbench and zero really removes the stroke", async ({
  page,
}) => {
  test.setTimeout(120000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  const width = page.getByRole("spinbutton", {
    name: "Path stroke width",
    exact: true,
  });
  await expect(width).toBeVisible();
  await expect(width).toHaveValue("1.4");
  // A deliberate zero must also work before the very first anchor.
  await width.fill("0");
  await click(page, 80, 80);
  await click(page, 160, 100);
  await expect(width).toHaveValue("0");
  expect((await draft(page)).layers[0].strokeWidth).toBe(0);
  await page.getByRole("button", { name: "Cancel path", exact: true }).click();
  await width.fill("6");
  await page.getByLabel("Path stroke color", { exact: true }).fill("#c08052");
  await page
    .getByLabel("Path line cap", { exact: true })
    .selectOption("square");
  await page
    .getByLabel("Path line join", { exact: true })
    .selectOption("bevel");
  await click(page, 90, 320);
  await click(page, 220, 120);
  await click(page, 390, 320);
  await expect(width).toHaveValue("6");
  await width.press("ArrowUp");
  await expect(width).toHaveValue("6.1");
  await page
    .getByRole("button", { name: "Undo path edit", exact: true })
    .click();
  await expect(width).toHaveValue("6");
  await page.getByLabel("Path fill color", { exact: true }).fill("#aabbcc");
  await page.getByRole("button", { name: "Close path", exact: true }).click();
  let document = await save(page);
  expect(document.layers[0]).toMatchObject({
    strokeWidth: 6,
    stroke: "#c08052",
    color: "#aabbcc",
    strokeLinecap: "square",
    strokeLinejoin: "bevel",
  });
  await width.fill("0");
  document = await save(page);
  expect(document.layers[0].strokeWidth).toBe(0);
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export seamless SVG", exact: true })
    .click();
  expect(await readDownload(await waiting)).toContain('stroke="none"');
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await page.getByRole("button", { name: "New path", exact: true }).click();
  await width.fill("0");
  await click(page, 80, 400);
  await click(page, 160, 440);
  await expect(width).toHaveValue("0");
  document = await save(page);
  expect(document.layers[1].strokeWidth).toBe(0);
  expect(errors).toEqual([]);
});

test("Curve and Pen snap to anchors, other curves, the grid and constrained angles", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page, { snap: true });
  const reference = worldPathLayer(
    [
      { x: 300, y: 330, out: { x: 340, y: 230 } },
      { x: 440, y: 330, in: { x: 410, y: 400 } },
    ],
    false,
    { paint: true, fillEnabled: false, stroke: "#263c48", strokeWidth: 5 },
  );
  await importLayers(page, [
    patternLayer("rect", {
      x: 150,
      y: 110,
      width: 100,
      height: 60,
      paint: true,
      fillEnabled: true,
      locked: true,
    }),
    reference,
  ]);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  await page.getByRole("button", { name: "New path", exact: true }).click();
  await page.getByLabel("Grid snapping", { exact: true }).selectOption("32");
  await click(page, 62, 62);
  await click(page, 202, 142);
  let pending = await draft(page),
    nodes = worldPathNodes(pending.layers.at(-1));
  expect(nodes[0].x).toBeCloseTo(64, 3);
  expect(nodes[0].y).toBeCloseTo(64, 3);
  expect(nodes[1].x).toBeCloseTo(200, 3);
  expect(nodes[1].y).toBeCloseTo(140, 3);
  await page.getByLabel("Grid snapping", { exact: true }).selectOption("0");
  await page.locator(".se-snap-details summary").click();
  await page.getByLabel("Snap to curves & edges", { exact: true }).check();
  await page.locator(".se-snap-details summary").click();
  const target = cubicPathPoint(
    pathSegment(worldPathNodes(reference), 0),
    0.27,
  );
  await at(page, target.x, target.y + 3);
  await expect(
    page.getByLabel("Snapped to curve / edge", { exact: true }),
  ).toBeVisible();
  await click(page, target.x, target.y + 3);
  pending = await draft(page);
  const third = worldPathNodes(pending.layers.at(-1))[2];
  expect(Math.hypot(third.x - target.x, third.y - target.y)).toBeLessThan(3.1);
  await page.getByRole("button", { name: "Finish path", exact: true }).click();
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await page.getByRole("button", { name: "New path", exact: true }).click();
  await page
    .getByRole("button", { name: "Smart alignment guides", exact: true })
    .click();
  await page.locator(".se-snap-details summary").click();
  await page.getByLabel("Angle snapping", { exact: true }).selectOption("45");
  await page.locator(".se-snap-details summary").click();
  await click(page, 75, 450);
  await click(page, 160, 440);
  pending = await draft(page);
  const pen = worldPathNodes(pending.layers.at(-1));
  expect(pen[0].y).toBeCloseTo(pen[1].y, 3);
});

test("point and tangent edits use snap targets while a multiple-anchor move remains rigid", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page, { snap: true });
  const path = worldPathLayer(
      [
        { x: 100, y: 100, out: { x: 160, y: 130 }, mode: "corner" },
        { x: 250, y: 180, in: { x: 220, y: 140 }, mode: "corner" },
        { x: 350, y: 300 },
      ],
      false,
      { paint: true, fillEnabled: false, strokeWidth: 4 },
    ),
    reference = worldPathLayer(
      [
        { x: 170, y: 220 },
        { x: 470, y: 420 },
      ],
      false,
      { paint: true, fillEnabled: false, strokeWidth: 3, locked: true },
    );
  await importLayers(page, [path, reference]);
  await rows(page).nth(1).click();
  await page
    .getByRole("button", { name: "Edit points (A)", exact: true })
    .click();
  await page.getByLabel("Path point 1", { exact: true }).click();
  await page.getByLabel("Point 1 out handle", { exact: true }).hover();
  await page.mouse.down();
  await at(page, 168, 222);
  await page.mouse.up();
  let document = await save(page),
    first = worldPathNodes(document.layers[0])[0];
  expect(first.out.x).toBeCloseTo(170, 3);
  expect(first.out.y).toBeCloseTo(220, 3);
  await page.getByLabel("Path point 1", { exact: true }).click();
  await page.keyboard.down("Shift");
  await page.getByLabel("Path point 2", { exact: true }).click();
  await page.keyboard.up("Shift");
  const before = worldPathNodes(document.layers[0]);
  await page.getByLabel("Path point 1", { exact: true }).hover();
  await page.mouse.down();
  await at(page, 168, 222);
  await page.mouse.up();
  document = await save(page);
  const after = worldPathNodes(document.layers[0]);
  expect(after[0].x).toBeCloseTo(170, 3);
  expect(after[0].y).toBeCloseTo(220, 3);
  expect(after[1].x - after[0].x).toBeCloseTo(before[1].x - before[0].x, 3);
  expect(after[1].y - after[0].y).toBeCloseTo(before[1].y - before[0].y, 3);
});

test("text supports multiline typography, native editing, outlines and portable vector exports", async ({
  page,
}) => {
  test.setTimeout(180000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await page
    .getByRole("button", { name: "Text tool (X)", exact: true })
    .click();
  const text = page.getByLabel("Text content", { exact: true });
  await expect(text).toBeEnabled();
  await text.fill("ALLØY\nSTUDIO");
  await page
    .getByLabel("Text font family", { exact: true })
    .selectOption("Space Grotesk");
  await page
    .getByLabel("Text font weight", { exact: true })
    .selectOption("600");
  await page.getByLabel("Text font size", { exact: true }).fill("32");
  await page.getByLabel("Text letter spacing", { exact: true }).fill("1.2");
  await page
    .getByLabel("Text alignment", { exact: true })
    .selectOption("center");
  await click(page, 35, 80);
  await expect(rows(page)).toHaveCount(1);
  await expect(text).toBeFocused();
  let document = await save(page),
    layer = document.layers[0];
  expect(layer.kind).toBe("text");
  expect(layer.text).toMatchObject({
    value: "ALLØY\nSTUDIO",
    fontFamily: "Space Grotesk",
    fontWeight: 600,
    fontSize: 32,
    letterSpacing: 1.2,
    align: "center",
  });
  expect(layer.path).toContain("M");
  await text.fill("VECTOR");
  await text.press("End");
  await text.press("x");
  await expect(text).toHaveValue("VECTORx");
  document = await save(page);
  expect(document.layers).toHaveLength(1);
  expect(document.layers[0].text.value).toBe("VECTORx");
  const path = document.layers[0].path;
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export seamless SVG", exact: true })
    .click();
  const exported = await readDownload(await waiting);
  expect(exported).not.toContain("<text");
  expect(exported).not.toContain("font-family");
  expect(exported).toContain("<path");
  await page
    .getByRole("button", { name: "Convert text to outlines", exact: true })
    .click();
  document = await save(page);
  expect(document.layers[0].kind).toBe("path");
  expect(document.layers[0].text).toBeUndefined();
  expect(document.layers[0].path).toBe(path);
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await expect(text).toHaveValue("VECTORx");
  expect(errors).toEqual([]);
});

test("SVG markup embeds definitions, local reuse and safe styles, edits transactionally and namespaces copies", async ({
  page,
}) => {
  test.setTimeout(180000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await page
    .getByRole("button", { name: "Paste SVG source", exact: true })
    .first()
    .click();
  const modal = page.getByRole("dialog", {
      name: "SVG source editor",
      exact: true,
    }),
    markup = modal.getByLabel("SVG markup", { exact: true });
  await markup.fill(svgSample);
  await modal
    .getByRole("button", { name: "Validate SVG", exact: true })
    .click();
  await expect(modal.getByRole("status")).toContainText("Valid");
  await markup.press("Control+Enter");
  await expect(modal).toHaveCount(0);
  await expect(rows(page)).toHaveCount(1);
  let document = await save(page),
    original = document.layers[0];
  expect(original.kind).toBe("svg");
  expect(original.width).toBe(256);
  expect(original.height).toBe(128);
  expect(original.svg).toContain('href="#leaf"');
  expect(original.svg).not.toContain("style=");
  expect(original.svg).toContain('fill="#eee8dc"');
  await page
    .getByRole("button", { name: "Edit SVG source", exact: true })
    .click();
  await markup.fill(original.svg.replace("#eee8dc", "#c8dcbb"));
  await modal.getByRole("button", { name: "Update SVG", exact: true }).click();
  document = await save(page);
  expect(document.layers[0].svg).toContain("#c8dcbb");
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  expect((await save(page)).layers[0].svg).toBe(original.svg);
  await page
    .getByRole("button", { name: "Duplicate selection", exact: true })
    .click();
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export seamless SVG", exact: true })
    .click();
  const exported = await readDownload(await waiting);
  expect(exported).toContain('href="#layer-0-leaf"');
  expect(exported).toContain('href="#layer-1-leaf"');
  expect(exported).toContain("url(#layer-1-shade)");
  expect(errors).toEqual([]);
});

test("SVG source refuses scripts, external resources, recursive use and invalid edits without modifying the document", async ({
  page,
}) => {
  test.setTimeout(120000);
  const external = [];
  page.on("request", (r) => {
    if (r.url().startsWith("https://unsafe.example")) external.push(r.url());
  });
  await open(page);
  await page
    .getByRole("button", { name: "Paste SVG source", exact: true })
    .first()
    .click();
  const modal = page.getByRole("dialog", {
      name: "SVG source editor",
      exact: true,
    }),
    markup = modal.getByLabel("SVG markup", { exact: true });
  for (const [source, error] of [
    [
      `<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>`,
      "Unsupported SVG element",
    ],
    [
      `<svg xmlns="http://www.w3.org/2000/svg"><path d="M0 0" onclick="alert(1)"/></svg>`,
      "Unsupported SVG attribute",
    ],
    [
      `<svg xmlns="http://www.w3.org/2000/svg"><image href="https://unsafe.example/x.png"/></svg>`,
      "local IDs",
    ],
    [
      `<svg xmlns="http://www.w3.org/2000/svg"><defs><g id="loop"><use href="#loop"/></g></defs><use href="#loop"/></svg>`,
      "Circular SVG",
    ],
    [
      `<svg xmlns="http://www.w3.org/2000/svg"><rect style="fill:url(https://unsafe.example/x)"/></svg>`,
      "External SVG",
    ],
  ]) {
    await markup.fill(source);
    await modal.getByRole("button", { name: "Embed SVG", exact: true }).click();
    await expect(modal.getByRole("alert")).toContainText(error);
    await expect(rows(page)).toHaveCount(0);
  }
  await markup.fill(svgSample);
  await modal.getByRole("button", { name: "Embed SVG", exact: true }).click();
  const before = await save(page);
  await page
    .getByRole("button", { name: "Edit SVG source", exact: true })
    .click();
  await markup.fill("<svg><bad/></svg>");
  await modal.getByRole("button", { name: "Update SVG", exact: true }).click();
  await expect(modal).toBeVisible();
  await modal.getByRole("button", { name: "Cancel", exact: true }).click();
  expect(await save(page)).toEqual(before);
  expect(external).toEqual([]);
});

test("arc, style eyedropper and true boolean differences remain editable and undoable", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.getByLabel("Drawing fill color", { exact: true }).fill("#c08052");
  await page.getByLabel("Drawing stroke width", { exact: true }).fill("5");
  await draw(page, "Rectangle tool (R)", 20, 230, 160, 370);
  await page.getByLabel("Drawing fill color", { exact: true }).fill("#aabbcc");
  await draw(page, "Arc tool (C)", 200, 40, 400, 240);
  await page.getByLabel("Arc start angle", { exact: true }).fill("0");
  await page.getByLabel("Arc sweep angle", { exact: true }).fill("270");
  await page.getByLabel("Arc closure", { exact: true }).selectOption("pie");
  let document = await save(page);
  expect(document.layers[1]).toMatchObject({
    kind: "arc",
    arcStart: 0,
    arcSweep: 270,
    arcClosure: "pie",
    fillEnabled: true,
  });
  await page
    .getByRole("button", { name: "Eyedropper tool (I)", exact: true })
    .click();
  await click(page, 80, 300);
  document = await save(page);
  expect(document.layers[1].color).toBe("#c08052");
  expect(document.layers[1].strokeWidth).toBe(5);
  await importLayers(page, [
    patternLayer("rect", {
      x: 256,
      y: 256,
      width: 240,
      height: 240,
      paint: true,
      fillEnabled: true,
      color: "#c08052",
    }),
    patternLayer("rect", {
      x: 256,
      y: 256,
      width: 100,
      height: 100,
      paint: true,
      fillEnabled: true,
    }),
  ]);
  await rows(page).nth(0).click();
  await rows(page)
    .nth(1)
    .click({ modifiers: ["Control"] });
  await expect(page.getByRole("group", { name: "Boolean operations", exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Difference", exact: true }).click();
  await expect(rows(page)).toHaveCount(1);
  document = await save(page);
  expect(document.layers[0].path.match(/M/g) || []).toHaveLength(2);
  expect(document.layers[0].nodes).toBeUndefined();
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await expect(rows(page)).toHaveCount(2);
});

test("phone text, snap options and SVG source controls have no horizontal overflow", async ({
  page,
}) => {
  test.setTimeout(120000);
  await page.setViewportSize({ width: 390, height: 844 });
  await open(page);
  await page
    .getByRole("button", { name: "Text tool (X)", exact: true })
    .click();
  await expect(page.getByLabel("Text content", { exact: true })).toBeEnabled();
  await page.getByLabel("Text content", { exact: true }).fill("VECTOR");
  await page.getByLabel("Pattern design canvas", { exact: true }).focus();
  await click(page, 30, 150);
  await expect(rows(page)).toHaveCount(1);
  await page.locator(".se-snap-details summary").click();
  await expect(
    page.getByLabel("Angle snapping", { exact: true }),
  ).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth - innerWidth,
    ),
  ).toBe(0);
  await page.locator(".se-snap-details summary").click();
  await page
    .getByRole("button", { name: "Paste SVG source", exact: true })
    .first()
    .click();
  const modal = page.getByRole("dialog", {
    name: "SVG source editor",
    exact: true,
  });
  await modal.getByLabel("SVG markup", { exact: true }).fill(svgSample);
  await modal.getByRole("button", { name: "Embed SVG", exact: true }).click();
  await expect(rows(page)).toHaveCount(2);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth - innerWidth,
    ),
  ).toBe(0);
});
