import { test, expect } from "@playwright/test";

// Points are in document units, independent of zoom, rulers, aspect and pan.
async function screenPoint(page, x, y) {
  return page.locator('[data-coordinate-space="tile"]').evaluate(
    (g, p) => {
      const v = new DOMPoint(p.x, p.y).matrixTransform(g.getScreenCTM());
      return { x: v.x, y: v.y };
    },
    { x, y },
  );
}
async function at(page, x, y) {
  const p = await screenPoint(page, x, y);
  await page.mouse.move(p.x, p.y);
}
async function draw(page, tool, x1, y1, x2, y2, modifiers = []) {
  await page.getByRole("button", { name: tool, exact: true }).click();
  for (const m of modifiers) await page.keyboard.down(m);
  await at(page, x1, y1);
  await page.mouse.down();
  await at(page, x2, y2);
  await page.mouse.up();
  for (const m of modifiers.reverse()) await page.keyboard.up(m);
}
async function document(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const stream = await (await waiting).createReadStream(),
    chunks = [];
  for await (const c of stream) chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function open(page) {
  await page.goto("/?studio=pattern&pattern=blank&material=natural-cotton");
  await expect(page.getByLabel("Pattern design canvas")).toBeVisible();
}
const rows = (page) => page.locator(".pe-layers button");

test("drag-to-draw shapes, constrained proportions, multi-select, groups and transactional undo", async ({
  page,
}) => {
  test.setTimeout(120000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await draw(page, "Rectangle tool (R)", 80, 80, 180, 160);
  await expect(rows(page)).toHaveCount(1);
  await draw(page, "Ellipse tool (O)", 250, 80, 330, 170, ["Shift"]);
  await draw(page, "Triangle tool (T)", 100, 260, 190, 350);
  let doc = await document(page);
  expect(doc.layers.map((l) => l.kind)).toEqual([
    "rect",
    "ellipse",
    "triangle",
  ]);
  expect(doc.layers[1].width).toBeCloseTo(doc.layers[1].height);
  expect(doc.layers[0].width).toBeCloseTo(100);
  expect(doc.layers[0].height).toBeCloseTo(80);
  await page
    .getByRole("button", { name: "Select tool (V)", exact: true })
    .click();
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+a");
  await expect(page.locator(".se-canvas-status small")).toContainText(
    "3 selected",
  );
  await page.keyboard.press("Control+g");
  doc = await document(page);
  expect(new Set(doc.layers.map((l) => l.group)).size).toBe(1);
  expect(doc.layers[0].group).toBeTruthy();
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Shift+ArrowRight");
  const moved = await document(page);
  moved.layers.forEach((l, i) => expect(l.x - doc.layers[i].x).toBeCloseTo(10));
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+d");
  await expect(rows(page)).toHaveCount(6);
  let copies = await document(page);
  expect(copies.layers[3].group).not.toBe(copies.layers[0].group);
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(rows(page)).toHaveCount(3);
  await page.getByTitle("Redo", { exact: true }).click();
  await expect(rows(page)).toHaveCount(6);
  await page.getByTitle("Undo", { exact: true }).click();
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+Shift+g");
  await page.getByRole("button", { name: "Align top", exact: true }).click();
  doc = await document(page);
  const tops = doc.layers.map((l) => l.y - l.height / 2);
  tops.forEach((v) => expect(v).toBeCloseTo(tops[0]));
  await page
    .getByRole("button", { name: "Distribute horizontally", exact: true })
    .click();
  doc = await document(page);
  const sorted = [...doc.layers].sort(
    (a, b) => a.x - a.width / 2 - (b.x - b.width / 2),
  );
  expect(
    sorted[1].x - sorted[1].width / 2 - sorted[0].x - sorted[0].width / 2,
  ).toBeCloseTo(
    sorted[2].x - sorted[2].width / 2 - sorted[1].x - sorted[1].width / 2,
  );
  expect(errors).toEqual([]);
});

test("marquee, zoom/pan, all resize handles, rotation, Escape and pointercancel are non-destructive", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await draw(page, "Rectangle tool (R)", 100, 100, 180, 170);
  await draw(page, "Ellipse tool (O)", 250, 200, 330, 280);
  await page
    .getByRole("button", { name: "Select tool (V)", exact: true })
    .click();
  await at(page, 65, 65);
  await page.mouse.down();
  await at(page, 355, 310);
  await page.mouse.up();
  await expect(page.locator(".se-canvas-status small")).toContainText(
    "2 selected",
  );
  await expect(page.locator("[data-handle]")).toHaveCount(9);
  await page.getByLabel("Canvas zoom").selectOption("200");
  await expect(page.getByLabel("Pattern design canvas")).toHaveAttribute(
    "data-zoom",
    "200",
  );
  const before = await document(page);
  // Zoom and camera movement do not dirty the vector document or its undo stack.
  await page
    .getByRole("button", { name: "Hand tool (H)", exact: true })
    .click();
  await at(page, 220, 190);
  await page.mouse.down();
  await page.mouse.move(700, 590);
  await page.mouse.up();
  expect((await document(page)).layers).toEqual(before.layers);
  await page.getByRole("button", { name: "Fit", exact: true }).click();
  await page
    .getByRole("button", { name: "Select tool (V)", exact: true })
    .click();
  await at(page, 140, 135);
  await page.mouse.down();
  await at(page, 190, 185);
  await page.keyboard.press("Escape");
  await page.mouse.up();
  expect((await document(page)).layers).toEqual(before.layers);
  await at(page, 140, 135);
  await page.mouse.down();
  await at(page, 185, 165);
  await page
    .getByLabel("Pattern design canvas")
    .dispatchEvent("pointercancel", { pointerId: 1 });
  await page.mouse.up();
  expect((await document(page)).layers).toEqual(before.layers);
  // Single-object edge resize keeps the opposite edge anchored.
  await rows(page).filter({ hasText: "Rectangle" }).click();
  await page.getByLabel("Smart alignment guides").click();
  const widthBefore = Number(
      await page.getByLabel("Width", { exact: true }).inputValue(),
    ),
    xBefore = Number(await page.getByLabel("X", { exact: true }).inputValue());
  const handle = await page
    .getByLabel("Resize right", { exact: true })
    .boundingBox();
  await page.mouse.move(
    handle.x + handle.width / 2,
    handle.y + handle.height / 2,
  );
  await page.mouse.down();
  await at(page, 220, 135);
  await page.mouse.up();
  expect(
    Number(await page.getByLabel("Width", { exact: true }).inputValue()),
  ).toBeGreaterThan(widthBefore);
  expect(
    Number(await page.getByLabel("X", { exact: true }).inputValue()) -
      Number(await page.getByLabel("Width", { exact: true }).inputValue()) / 2,
  ).toBeCloseTo(xBefore - widthBefore / 2);
  await page.getByTitle("Undo", { exact: true }).click();
  expect((await document(page)).layers).toEqual(before.layers);
});

test("Bézier pen, point and handle editing, subdivision, freehand and editable primitives", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  await at(page, 100, 300);
  const start = await screenPoint(page, 100, 300);
  await page.mouse.click(start.x, start.y);
  await at(page, 250, 120);
  await page.mouse.down();
  await at(page, 300, 120);
  await page.mouse.up();
  const end = await screenPoint(page, 400, 300);
  await page.mouse.click(end.x, end.y);
  await page.keyboard.press("Enter");
  await expect(rows(page)).toHaveCount(1);
  let doc = await document(page);
  expect(doc.layers[0].nodes).toHaveLength(3);
  expect(doc.layers[0].path).toContain(" C");
  await page
    .getByRole("button", { name: "Edit path points", exact: true })
    .click();
  await expect(page.getByLabel("Path point 2", { exact: true })).toBeVisible();
  const anchor = await page
    .getByLabel("Path point 2", { exact: true })
    .boundingBox();
  await page.mouse.move(
    anchor.x + anchor.width / 2,
    anchor.y + anchor.height / 2,
  );
  await page.mouse.down();
  await at(page, 250, 155);
  await page.mouse.up();
  const edited = await document(page);
  expect(edited.layers[0]).not.toEqual(doc.layers[0]);
  expect(
    edited.layers[0].y +
      ((edited.layers[0].nodes[1].y - 50) * edited.layers[0].height) / 100,
  ).toBeCloseTo(155);
  await page.getByRole("button", { name: "Add point", exact: true }).click();
  doc = await document(page);
  expect(doc.layers[0].nodes).toHaveLength(4);
  await page.getByRole("button", { name: "Corner point", exact: true }).click();
  doc = await document(page);
  expect(doc.layers[0].nodes[2].in).toBeUndefined();
  await page.getByRole("button", { name: "Smooth point", exact: true }).click();
  doc = await document(page);
  expect(doc.layers[0].nodes[2].in).toBeTruthy();
  await page.getByRole("button", { name: "Remove point", exact: true }).click();
  expect((await document(page)).layers[0].nodes).toHaveLength(3);
  await page.getByTitle("Undo", { exact: true }).click();
  expect((await document(page)).layers[0].nodes).toHaveLength(4);
  await draw(page, "Freehand tool (B)", 90, 400, 400, 430);
  await expect(rows(page)).toHaveCount(2);
  await draw(page, "Star tool (S)", 320, 50, 450, 180);
  await page
    .getByRole("button", { name: "Convert to editable path", exact: true })
    .click();
  doc = await document(page);
  expect(doc.layers[2].kind).toBe("path");
  expect(doc.layers[2].nodes).toHaveLength(10);
  // An unfinished pen path can be canceled without adding layers or losing focus.
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  const p = await screenPoint(page, 30, 30);
  await page.mouse.click(p.x, p.y);
  await page.keyboard.press("Escape");
  await expect(rows(page)).toHaveCount(3);
  await expect(
    page.getByRole("dialog", { name: "Pattern studio" }),
  ).toBeVisible();
});

test("locked/hidden layers, clipboard, repeat arrays, capacity limit and draft recovery", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await draw(page, "Polygon tool (G)", 190, 190, 270, 270);
  await page.getByLabel("Layer name", { exact: true }).fill("Hex motif");
  await page
    .getByRole("button", { name: "Select tool (V)", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Layer lock 1: Hex motif", exact: true })
    .check();
  await expect(page.getByLabel("X", { exact: true })).toBeDisabled();
  const locked = await document(page);
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Delete");
  expect((await document(page)).layers).toEqual(locked.layers);
  await page.getByRole("button", { name: "Unlock", exact: true }).click();
  await page
    .getByRole("checkbox", {
      name: "Layer visibility 1: Hex motif",
      exact: true,
    })
    .uncheck();
  expect((await document(page)).layers[0].visible).toBe(false);
  await page
    .getByRole("checkbox", {
      name: "Layer visibility 1: Hex motif",
      exact: true,
    })
    .check();
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+c");
  await page.keyboard.press("Control+v");
  await expect(rows(page)).toHaveCount(2);
  await page.getByTitle("Undo", { exact: true }).click();
  await page.locator(".se-array-panel summary").click();
  await page
    .getByRole("button", { name: "Generate repeat array", exact: true })
    .click();
  await expect(rows(page)).toHaveCount(9);
  const array = await document(page);
  expect(new Set(array.layers.map((l) => Math.round(l.x))).size).toBe(3);
  expect(new Set(array.layers.map((l) => Math.round(l.y))).size).toBe(3);
  await page.getByLabel("Array columns").fill("8");
  await page.getByLabel("Array rows").fill("8");
  await page
    .getByRole("button", { name: "Generate repeat array", exact: true })
    .click();
  await expect(page.getByRole("alert")).toContainText("64 layers");
  await expect(rows(page)).toHaveCount(9);
  await page.getByLabel("Pattern name").fill("Recovered hex study");
  await expect(page.locator(".se-draft-status")).toContainText(
    "Draft saved locally",
  );
  await page.reload();
  await page
    .getByRole("button", { name: "Restore draft", exact: true })
    .click();
  await expect(page.getByLabel("Pattern name")).toHaveValue(
    "Recovered hex study",
  );
  await expect(rows(page)).toHaveCount(9);
  await expect(
    page.getByRole("button", { name: "Select tool (V)", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
});

test("SVG/PNG/document export and material viewer receive the same edited vectors", async ({
  page,
}) => {
  test.setTimeout(180000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await open(page);
  await draw(page, "Rectangle tool (R)", 70, 70, 170, 160);
  await page.getByLabel("Corner radius", { exact: true }).fill("16");
  await page.getByLabel("Motif color", { exact: true }).fill("#3d9c7a");
  await page.getByLabel("Motif stroke color", { exact: true }).fill("#ec764a");
  await page.getByLabel("Stroke width", { exact: true }).fill("4");
  await page.getByLabel("Motif material", { exact: true }).selectOption("foil");
  await page.getByLabel("Repeat layout").selectOption("mirror");
  const doc = await document(page);
  expect(doc.layers[0]).toMatchObject({
    cornerRadius: 16,
    paint: true,
    stroke: "#ec764a",
    strokeWidth: 4,
    finish: "foil",
    metalness: 1,
  });
  await page.getByLabel("Pattern PNG resolution").selectOption("512");
  const pngWaiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export PNG", exact: true }).click();
  const png = await (await pngWaiting).createReadStream(),
    chunks = [];
  for await (const c of png) chunks.push(c);
  const buffer = Buffer.concat(chunks);
  expect(buffer.readUInt32BE(16)).toBe(512);
  expect(buffer.readUInt32BE(20)).toBe(512);
  const svgWaiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export seamless SVG", exact: true })
    .click();
  const svgStream = await (await svgWaiting).createReadStream(),
    svgChunks = [];
  for await (const c of svgStream) svgChunks.push(c);
  const svg = Buffer.concat(svgChunks).toString();
  expect(Number(svg.match(/rx="([^"]+)"/)[1])).toBeCloseTo(
    (doc.layers[0].cornerRadius * doc.layers[0].width) / 100,
  );
  expect(svg).toContain('stroke="#ec764a"');
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await expect(
    page.getByRole("dialog", { name: "Pattern studio" }),
  ).toHaveCount(0);
  await expect(page.locator(".pattern-applied")).toBeVisible();
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  const reapplied = await document(page);
  expect(reapplied).toEqual(doc);
  expect(errors).toEqual([]);
});

test("editor hotkeys never steal native text input or trigger workspace shortcuts", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.getByLabel("Pattern name").fill("Rectangle R, star S and pen P");
  await expect(
    page.getByRole("button", { name: "Select tool (V)", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("r");
  await expect(
    page.getByRole("button", { name: "Rectangle tool (R)", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await page.keyboard.press("?");
  await expect(
    page.getByRole("region", { name: "Editor shortcuts" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Hide editor shortcuts" }).click();
  const waiting = page.waitForEvent("download");
  await page.getByLabel("Pattern design canvas").focus();
  await page.keyboard.press("Control+s");
  await waiting;
  await expect(
    page.getByRole("dialog", { name: "Pattern studio" }),
  ).toBeVisible();
  await expect(
    page.getByRole("dialog", { name: "Save a material preset" }),
  ).toHaveCount(0);
});

test("document-space strokes keep their width in SVG raster and material channels at every resolution", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  const results = await page.evaluate(async () => {
    const { patternStarter, patternLayer, validatePattern, patternSVG } =
      await import("/src/patternDocument.js");
    const { rasterPatternSVG } = await import("/src/patternRuntime.js");
    const d = validatePattern({
      ...patternStarter("Blank"),
      backgroundOpacity: 0,
      tileAxes: "none",
      layers: [
        patternLayer("path", {
          paint: true,
          fillEnabled: false,
          stroke: "#ff0000",
          strokeWidth: 10,
          x: 256,
          y: 256,
          width: 360,
          height: 1,
          nodes: [
            { x: 0, y: 50 },
            { x: 100, y: 50 },
          ],
          finish: "foil",
        }),
      ],
    });
    const rows = [];
    for (const size of [512, 1024, 2048]) {
      const widths = [];
      for (const mode of ["color", "params", "finish"]) {
        const c = await rasterPatternSVG(patternSVG(d, mode), size, size),
          data = c.getContext("2d").getImageData(size / 2, 0, 1, size).data;
        let coverage = 0;
        for (let i = 3; i < data.length; i += 4) if (data[i] > 128) coverage++;
        widths.push(coverage);
      }
      rows.push({ size, widths });
    }
    return rows;
  });
  expect(results).toEqual([
    { size: 512, widths: [10, 10, 10] },
    { size: 1024, widths: [20, 20, 20] },
    { size: 2048, widths: [40, 40, 40] },
  ]);
});

test("small screens keep the canvas, draw tools and properties usable without horizontal overflow", async ({
  page,
}) => {
  test.setTimeout(120000);
  await page.setViewportSize({ width: 390, height: 844 });
  await open(page);
  await draw(page, "Rectangle tool (R)", 100, 110, 220, 220);
  await expect(rows(page)).toHaveCount(1);
  await expect(page.getByLabel("Pattern design canvas")).toBeVisible();
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.getByLabel("Layer name").fill("Mobile motif");
  expect((await document(page)).layers[0].name).toBe("Mobile motif");
});

test("editable SVG imports preserve distinct fill/stroke colors, winding rules and stroke caps", async ({
  page,
}) => {
  test.setTimeout(120000);
  await open(page);
  await page.locator('input[accept=".svg"]').setInputFiles({
    name: "outlined-motif.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M5 5H95V95H5Z M25 25H75V75H25Z" fill="#3a7" fill-rule="evenodd" stroke="red" stroke-width="3" stroke-linecap="square" stroke-linejoin="bevel"/></svg>',
    ),
  });
  const doc = await document(page);
  expect(doc.layers[0]).toMatchObject({
    kind: "path",
    paint: true,
    color: "#33aa77",
    stroke: "#ff0000",
    strokeWidth: 3,
    fillRule: "evenodd",
    strokeLinecap: "square",
    strokeLinejoin: "bevel",
  });
  await expect(page.getByLabel("Motif line cap")).toHaveValue("square");
  await expect(page.getByLabel("Motif line join")).toHaveValue("bevel");
  // Alpha paints cannot be represented as one layer opacity: preserve their SVG.
  await page.locator('input[accept=".svg"]').setInputFiles({
    name: "alpha-motif.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M5 5L95 5L50 95Z" fill="#33aa77" fill-opacity=".3" stroke="#ff0000" stroke-width="3"/></svg>',
    ),
  });
  expect((await document(page)).layers[1].kind).toBe("svg");
  await page.locator('input[accept=".svg"]').setInputFiles({
    name: "percentage-outline.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" opacity="50%"><path d="M5 5L95 5L50 95Z" fill="none" stroke="#ff0000" stroke-width="40"/></svg>',
    ),
  });
  const preserved = (await document(page)).layers[2];
  expect(preserved.kind).toBe("svg");
  expect(preserved.svg).toContain('opacity="50%"');
  expect(preserved.svg).toContain('stroke-width="40"');
  await page
    .locator('input[accept=".svg"]')
    .setInputFiles({
      name: "non-scaling-outline.svg",
      mimeType: "image/svg+xml",
      buffer: Buffer.from(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="M5 5L95 5L50 95Z" fill="none" stroke="#ff0000" stroke-width="4" vector-effect="non-scaling-stroke"/></svg>',
      ),
    });
  const nonScaling = (await document(page)).layers[3];
  expect(nonScaling.kind).toBe("svg");
  expect(nonScaling.svg).toContain('vector-effect="non-scaling-stroke"');
});

test("touch drawing and two-finger zoom/pan preserve the document and cursor anchor", async ({
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
    const page = await context.newPage(),
      errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await open(page);
    const cdp = await context.newCDPSession(page);
    await page
      .getByRole("button", { name: "Rectangle tool (R)", exact: true })
      .click();
    const start = await screenPoint(page, 160, 160),
      end = await screenPoint(page, 280, 280);
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ ...start, id: 1 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [{ ...end, id: 1 }],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchEnd",
      touchPoints: [],
    });
    await expect(rows(page)).toHaveCount(1);
    const before = await document(page);
    expect(before.layers[0].width).toBeCloseTo(120, 3);
    expect(before.layers[0].height).toBeCloseTo(120, 3);
    const scale = () =>
      page
        .locator('[data-coordinate-space="tile"]')
        .evaluate((g) => g.getScreenCTM().a);
    const initial = await scale(),
      p1 = await screenPoint(page, 180, 180),
      p2 = await screenPoint(page, 330, 330),
      center = await screenPoint(page, 255, 255);
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [
        { ...p1, id: 1 },
        { ...p2, id: 2 },
      ],
    });
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [
        { x: p1.x - 30, y: p1.y - 30, id: 1 },
        { x: p2.x + 30, y: p2.y + 30, id: 2 },
      ],
    });
    await expect.poll(scale).toBeGreaterThan(initial * 1.2);
    let anchored = await screenPoint(page, 255, 255);
    expect(anchored.x).toBeCloseTo(center.x, 1);
    expect(anchored.y).toBeCloseTo(center.y, 1);
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchMove",
      touchPoints: [
        { x: p1.x - 5, y: p1.y - 10, id: 1 },
        { x: p2.x + 55, y: p2.y + 50, id: 2 },
      ],
    });
    await expect
      .poll(async () => (await screenPoint(page, 255, 255)).x)
      .toBeCloseTo(center.x + 25, 1);
    anchored = await screenPoint(page, 255, 255);
    expect(anchored.y).toBeCloseTo(center.y + 20, 1);
    await cdp.send("Input.dispatchTouchEvent", {
      type: "touchEnd",
      touchPoints: [],
    });
    expect(await document(page)).toEqual(before);
    expect(errors).toEqual([]);
  } finally {
    await context.close();
  }
});
