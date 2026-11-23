import { test, expect } from "@playwright/test";
import { patternStarter, validatePattern } from "../src/patternDocument.js";
import { worldPathLayer } from "../src/shapeEditorGeometry.js";
import {
  worldPathNodes,
  pathSegment,
  cubicPathPoint,
} from "../src/pathEditorGeometry.js";

async function open(page) {
  await page.goto("/?studio=pattern&pattern=blank&material=natural-cotton");
  await expect(page.getByLabel("Pattern design canvas")).toBeVisible();
  // These fixtures assert unrestricted document coordinates; target snapping
  // has dedicated coverage in shape-editor-svg-tools.spec.js.
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
async function place(page, x, y, hx, hy) {
  await at(page, x, y);
  await page.mouse.down();
  if (hx != null) await at(page, hx, hy);
  await page.mouse.up();
}
async function doc(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function importLayers(page, layers) {
  await page.locator('.pe-overlay input[accept*="json"]').setInputFiles({
    name: "paths.pattern.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(validatePattern({ ...patternStarter("Blank"), layers })),
    ),
  });
}
async function draft(page) {
  await expect(page.locator(".se-draft-status")).toContainText(
    "Path draft saved locally",
  );
  return page.evaluate(
    () => JSON.parse(localStorage.getItem("alloy-pattern-draft-v2")).doc,
  );
}
const rows = (page) => page.locator(".pe-layers button");
const equalPoint = (a, b) => {
  expect(a.x).toBeCloseTo(b.x, 3);
  expect(a.y).toBeCloseTo(b.y, 3);
};
const samplePath = () =>
  worldPathLayer(
    [
      { x: 100, y: 300, out: { x: 155, y: 155 }, mode: "smooth" },
      {
        x: 265,
        y: 155,
        in: { x: 215, y: 115 },
        out: { x: 340, y: 215 },
        mode: "smooth",
      },
      { x: 420, y: 340, in: { x: 345, y: 335 }, mode: "smooth" },
    ],
    false,
    {
      paint: true,
      fillEnabled: false,
      stroke: "#bd7444",
      strokeWidth: 6,
      name: "Copper contour",
    },
  );

test("precision pen edits unfinished anchors and independent handles with per-path undo and redo", async ({
  page,
}) => {
  test.setTimeout(120000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await place(page, 80, 330, 130, 330);
  await at(page, 250, 130);
  await page.mouse.down();
  await at(page, 310, 130);
  await page.keyboard.down("Alt");
  await at(page, 290, 170);
  await page.mouse.up();
  await page.keyboard.up("Alt");
  await expect(rows(page)).toHaveCount(0);
  let pending = await draft(page),
    n = worldPathNodes(pending.layers[0]);
  expect(n[1].mode).toBe("corner");
  equalPoint(n[1].in, { x: 190, y: 130 });
  equalPoint(n[1].out, { x: 290, y: 170 });
  await page.getByLabel("Draft point 1", { exact: true }).click();
  await page.getByLabel("Path anchor x", { exact: true }).fill("100");
  await page
    .getByLabel("Path point type", { exact: true })
    .selectOption("smooth");
  await page.getByLabel("Path out handle length", { exact: true }).fill("90");
  n = worldPathNodes((await draft(page)).layers[0]);
  equalPoint(n[0], { x: 100, y: 330 });
  expect(Math.hypot(n[0].out.x - n[0].x, n[0].out.y - n[0].y)).toBeCloseTo(90);
  expect(Math.hypot(n[0].in.x - n[0].x, n[0].in.y - n[0].y)).toBeCloseTo(50);
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+z");
  await expect(page.getByLabel("Path out handle length")).toHaveValue("50");
  await page.keyboard.press("Control+Shift+z");
  await expect(page.getByLabel("Path out handle length")).toHaveValue("90");
  await page.getByRole("button", { name: "Finish path", exact: true }).click();
  const saved = await doc(page);
  expect(saved.layers).toHaveLength(1);
  expect(saved.layers[0].nodes[0].mode).toBe("smooth");
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(rows(page)).toHaveCount(0);
  expect(errors).toEqual([]);
});

test("curve tool interpolates click points, controls tension and closes with automatic and corner nodes", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.keyboard.press("u");
  await expect(
    page.getByRole("button", { name: "Curve tool (U)", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await click(page, 90, 320);
  await click(page, 155, 120);
  await page.keyboard.down("Alt");
  await click(page, 330, 150);
  await page.keyboard.up("Alt");
  await click(page, 420, 340);
  await page
    .getByRole("slider", { name: "Curve tension", exact: true })
    .evaluate((e) => {
      Object.getOwnPropertyDescriptor(
        HTMLInputElement.prototype,
        "value",
      ).set.call(e, "65");
      e.dispatchEvent(new Event("input", { bubbles: true }));
    });
  await expect(
    page.getByRole("slider", { name: "Curve tension", exact: true }),
  ).toHaveValue("65");
  const pending = await draft(page),
    nodes = worldPathNodes(pending.layers[0]);
  expect(nodes.map((n) => n.mode)).toEqual(["auto", "auto", "corner", "auto"]);
  equalPoint(nodes[1], { x: 155, y: 120 });
  expect(nodes[1].in).toBeTruthy();
  expect(nodes[2].in).toBeUndefined();
  expect(pending.layers[0].curveTension).toBe(0.65);
  await page.getByRole("button", { name: "Close path", exact: true }).click();
  const saved = await doc(page);
  expect(saved.layers[0].closed).toBe(true);
  expect(saved.layers[0].fillEnabled).toBe(true);
  await page.getByRole("button", { name: "Reverse", exact: true }).click();
  const reversed = worldPathNodes((await doc(page)).layers[0]);
  equalPoint(reversed[0], nodes.at(-1));
});

test("pen extension from either endpoint preserves a transformed source and can be canceled or undone", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  const source = {
      ...samplePath(),
      rotation: 24,
      flipX: true,
      width: 220,
      height: 160,
    },
    initial = validatePattern({ ...patternStarter("Blank"), layers: [source] });
  await importLayers(page, [source]);
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await page.getByRole("button", { name: "Continue end", exact: true }).click();
  await click(page, 440, 420);
  await page.getByRole("button", { name: "Cancel path", exact: true }).click();
  expect((await doc(page)).layers).toEqual(initial.layers);
  await page
    .getByRole("button", { name: "Continue start", exact: true })
    .click();
  await click(page, 65, 420);
  await page.keyboard.press("Enter");
  const extended = await doc(page),
    before = worldPathNodes(initial.layers[0]),
    after = worldPathNodes(extended.layers[0]);
  expect(extended.layers).toHaveLength(1);
  expect(after).toHaveLength(4);
  before.reverse().forEach((p, i) => equalPoint(after[i], p));
  equalPoint(after[3], { x: 65, y: 420 });
  await page.getByTitle("Undo", { exact: true }).click();
  expect((await doc(page)).layers).toEqual(initial.layers);
});

test("insert, bend, split and join are exact topology edits with one transaction per gesture", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await importLayers(page, [samplePath()]);
  await page
    .getByRole("button", { name: "Edit path points", exact: true })
    .click();
  const old = worldPathNodes((await doc(page)).layers[0]),
    p = cubicPathPoint(pathSegment(old, 0), 0.31);
  await page.keyboard.down("Alt");
  await click(page, p.x, p.y);
  await page.keyboard.up("Alt");
  const inserted = worldPathNodes((await doc(page)).layers[0]);
  expect(inserted).toHaveLength(4);
  for (let i = 0; i <= 10; i++) {
    const t = i / 10;
    equalPoint(
      cubicPathPoint(pathSegment(old, 0), t),
      t <= 0.31
        ? cubicPathPoint(pathSegment(inserted, 0), t / 0.31)
        : cubicPathPoint(pathSegment(inserted, 1), (t - 0.31) / 0.69),
    );
  }
  const mid = cubicPathPoint(pathSegment(inserted, 2), 0.5);
  await at(page, mid.x, mid.y);
  await page.mouse.down();
  await at(page, mid.x, mid.y + 30);
  await page.mouse.up();
  let edited = worldPathNodes((await doc(page)).layers[0]);
  equalPoint(edited[2], inserted[2]);
  equalPoint(edited[3], inserted[3]);
  equalPoint(cubicPathPoint(pathSegment(edited, 2), 0.5), {
    x: mid.x,
    y: mid.y + 30,
  });
  await page.getByTitle("Undo", { exact: true }).click();
  edited = worldPathNodes((await doc(page)).layers[0]);
  edited.forEach((n, i) => equalPoint(n, inserted[i]));
  await page.getByLabel("Path point 2", { exact: true }).click();
  await page.getByRole("button", { name: "Split path", exact: true }).click();
  await expect(rows(page)).toHaveCount(2);
  await page.getByRole("button", { name: "Join paths", exact: true }).click();
  const joined = worldPathNodes((await doc(page)).layers[0]);
  expect(joined).toHaveLength(4);
  joined.forEach((n, i) => equalPoint(n, inserted[i]));
});

test("multi-anchor selection moves rigidly and locked paths never expose editable nodes", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await importLayers(page, [samplePath()]);
  await page
    .getByRole("button", { name: "Edit path points", exact: true })
    .click();
  const old = worldPathNodes((await doc(page)).layers[0]);
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+a");
  await page.keyboard.press("Shift+ArrowRight");
  const moved = worldPathNodes((await doc(page)).layers[0]);
  moved.forEach((p, i) => equalPoint(p, { x: old[i].x + 10, y: old[i].y }));
  await page
    .getByRole("checkbox", {
      name: "Layer lock 1: Copper contour",
      exact: true,
    })
    .check();
  await expect(page.getByLabel("Path point 1", { exact: true })).toHaveCount(0);
  const protectedDoc = await doc(page);
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("ArrowRight");
  await page.keyboard.press("Delete");
  expect((await doc(page)).layers).toEqual(protectedDoc.layers);
});

test("unfinished paths survive pointer cancel, focus loss and draft recovery without accidental commits", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  await click(page, 100, 330);
  await click(page, 250, 160);
  await at(page, 400, 350);
  await page.mouse.down();
  await at(page, 430, 370);
  await page.getByLabel("Pattern design canvas").dispatchEvent("pointercancel");
  await page.mouse.up();
  await expect(page.getByLabel("Draft point 3", { exact: true })).toHaveCount(
    0,
  );
  await expect(rows(page)).toHaveCount(0);
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await expect(page.getByLabel("Draft point 2", { exact: true })).toBeVisible();
  const pending = await draft(page);
  expect(pending.layers[0].nodes).toHaveLength(2);
  await page.reload();
  await page
    .getByRole("button", { name: "Restore draft", exact: true })
    .click();
  expect((await doc(page)).layers).toEqual(pending.layers);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  await page.getByRole("button", { name: "Continue end", exact: true }).click();
  await click(page, 400, 350);
  await page.getByRole("button", { name: "Finish path", exact: true }).click();
  expect((await doc(page)).layers[0].nodes).toHaveLength(3);
});

test("pen placement can reposition anchors with Space and export an unfinished curve without losing it", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await at(page, 100, 300);
  await page.mouse.down();
  await at(page, 150, 300);
  await page.keyboard.down("Space");
  await at(page, 180, 330);
  await page.keyboard.up("Space");
  await page.mouse.up();
  await click(page, 320, 150);
  const n = worldPathNodes((await draft(page)).layers[0]);
  equalPoint(n[0], { x: 130, y: 330 });
  equalPoint(n[0].out, { x: 180, y: 330 });
  const download = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export seamless SVG", exact: true })
    .click();
  const bytes = [];
  for await (const c of await (await download).createReadStream())
    bytes.push(c);
  expect(Buffer.concat(bytes).toString()).toContain(" C");
  await expect(rows(page)).toHaveCount(1);
  expect((await doc(page)).layers[0].nodes).toHaveLength(2);
});

test("touch curvature placement and responsive path controls remain usable on a phone", async ({
  browser,
}) => {
  test.setTimeout(120000);
  const context = await browser.newContext({
    baseURL: test.info().project.use.baseURL,
    viewport: { width: 390, height: 844 },
    hasTouch: true,
    isMobile: true,
  });
  try {
    const page = await context.newPage();
    await open(page);
    await page
      .getByRole("button", { name: "Curve tool (U)", exact: true })
      .click();
    const cdp = await context.newCDPSession(page);
    for (const [x, y] of [
      [100, 330],
      [250, 140],
      [400, 330],
    ]) {
      const p = await screen(page, x, y);
      await cdp.send("Input.dispatchTouchEvent", {
        type: "touchStart",
        touchPoints: [{ ...p, id: 1 }],
      });
      await cdp.send("Input.dispatchTouchEvent", {
        type: "touchEnd",
        touchPoints: [],
      });
    }
    await page
      .getByRole("button", { name: "Finish path", exact: true })
      .click();
    const saved = await doc(page);
    expect(saved.layers[0].nodes).toHaveLength(3);
    expect(saved.layers[0].nodes.every((n) => n.mode === "auto")).toBe(true);
    expect(
      await page.evaluate(
        () => document.documentElement.scrollWidth - innerWidth,
      ),
    ).toBeLessThanOrEqual(1);
  } finally {
    await context.close();
  }
});

test("pen double-click finishes, Curve double-click converts, and in-flight undo cancels only the current placement", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await click(page, 90, 330);
  const end = await screen(page, 330, 150);
  await page.mouse.dblclick(end.x, end.y);
  await expect(rows(page)).toHaveCount(1);
  expect((await doc(page)).layers[0].nodes).toHaveLength(2);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  await page.getByRole("button", { name: "New path", exact: true }).click();
  await click(page, 100, 390);
  const mid = await screen(page, 250, 250);
  await page.mouse.dblclick(mid.x, mid.y);
  await expect(rows(page)).toHaveCount(1);
  await expect(page.getByLabel("Path point type", { exact: true })).toHaveValue(
    "corner",
  );
  await at(page, 400, 350);
  await page.mouse.down();
  await at(page, 420, 340);
  await page.keyboard.press("Control+z");
  await page.mouse.up();
  await expect(page.getByLabel("Draft point 3", { exact: true })).toHaveCount(
    0,
  );
  await expect(page.getByLabel("Draft point 2", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: "Finish path", exact: true }).click();
  const saved = await doc(page);
  expect(saved.layers).toHaveLength(2);
  expect(saved.layers[1].nodes).toHaveLength(2);
});
