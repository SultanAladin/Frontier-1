import { test, expect } from "@playwright/test";

test.describe.configure({ timeout: 90000 });
test.use({ actionTimeout: 20000 });
const boardOf = (page) =>
  page.getByRole("region", { name: "Stamp composition canvas", exact: true });
const valueOf = (page, name) =>
  page.getByLabel(`Stamp ${name} value`, { exact: true });
const undoOf = (page) =>
  page.getByRole("button", { name: "Undo stamp edit", exact: true });

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
async function open(page) {
  await page.goto("/?studio=stamp");
  await expect(boardOf(page)).toBeVisible();
  await expect
    .poll(() =>
      page
        .getByAltText("Composed stamp preview", { exact: true })
        .evaluate((e) => e.complete && e.naturalWidth > 0),
    )
    .toBe(true);
}
async function project(page) {
  const pending = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save stamp project", exact: true })
    .click();
  const chunks = [];
  for await (const chunk of await (await pending).createReadStream())
    chunks.push(chunk);
  return JSON.parse(Buffer.concat(chunks).toString());
}
async function clientPoint(page, x, y) {
  const rect = await boardOf(page).boundingBox();
  return {
    x: rect.x + (x / 512) * rect.width,
    y: rect.y + (y / 512) * rect.height,
  };
}
async function move(page, x, y, steps = 1) {
  const point = await clientPoint(page, x, y);
  await page.mouse.move(point.x, point.y, { steps });
  return point;
}
async function previewSVG(page) {
  return page
    .getByAltText("Composed stamp preview", { exact: true })
    .evaluate(async (e) => {
      try {
        return await (await fetch(e.src)).text();
      } catch {
        return "";
      }
    });
}
async function mode(page, name) {
  await expect(boardOf(page)).toHaveAttribute("data-transform-mode", name);
}

async function ArtworkPixelDifference(page, BeforePixels, AfterPixels)
{
  return await page.evaluate(async ([Before, After]) =>
  {
    async function Decode(Encoded)
    {
      const Artwork = new Image(); Artwork.src = `data:image/png;base64,${Encoded}`;
      await Artwork.decode();
      const Canvas = document.createElement("canvas"); Canvas.width = Artwork.width; Canvas.height = Artwork.height;
      const Context = Canvas.getContext("2d"); Context.drawImage(Artwork, 0, 0);
      return Context.getImageData(0, 0, Canvas.width, Canvas.height).data;
    }
    const Original = await Decode(Before), Hovered = await Decode(After);
    let SignificantPixels = 0, ChannelDelta = 0;
    for (let Offset = 0; Offset < Original.length; Offset += 4)
    {
      let MaximumDelta = 0;
      for (let Channel = 0; Channel < 4; ++Channel)
      {
        const Delta = Math.abs(Original[Offset + Channel] - Hovered[Offset + Channel]);
        ChannelDelta += Delta; MaximumDelta = Math.max(MaximumDelta, Delta);
      }
      if (MaximumDelta > 8) ++SignificantPixels;
    }
    return { SignificantPixels, Population: Original.length / 4, MeanChannelDelta: ChannelDelta / Original.length };
  }, [BeforePixels.toString("base64"), AfterPixels.toString("base64")]);
}

// Compare actual artwork pixels, not just the preview img's visibility. The
// original regression left that img present but painted an opaque button on it.
test("Stamp artwork stays visible under selected, unselected, SVG and image hover/active hit areas", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const title = page.getByRole("button", {
    name: "Canvas stamp component Text",
    exact: true,
  });
  await page.mouse.move(15, 850);
  const rect = await title.boundingBox(),
    clip = {
      x: Math.ceil(rect.x + 8),
      y: Math.ceil(rect.y + 15),
      width: Math.floor(rect.width - 16),
      height: Math.floor(rect.height - 30),
    },
    before = await page.screenshot({ clip });
  await title.hover();
  // Wait beyond the former shared button background transition.
  await page.waitForTimeout(200);
  await expect(title).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  const HoverPixels = await page.screenshot({ clip });
  // Hover can rerasterize SVG edges in the software compositor. Compare decoded
  // pixels with a bounded antialiasing allowance, not compressed PNG byte identity.
  const Difference = await ArtworkPixelDifference(page, before, HoverPixels);
  expect(Difference.SignificantPixels).toBeLessThanOrEqual(Math.ceil(Difference.Population * .001));
  expect(Difference.MeanChannelDelta).toBeLessThan(.1);
  // Negative control: the original opaque-hover defect must exceed the allowance.
  await title.evaluate(Artwork => Artwork.style.background = "#242422");
  const CoveredPixels = await page.screenshot({ clip });
  await title.evaluate(Artwork => Artwork.style.removeProperty("background"));
  const CoveredDifference = await ArtworkPixelDifference(page, before, CoveredPixels);
  expect(CoveredDifference.SignificantPixels).toBeGreaterThan(Math.ceil(CoveredDifference.Population * .001));
  expect(CoveredDifference.MeanChannelDelta).toBeGreaterThan(.1);
  const ring = page.getByRole("button", {
    name: "Canvas stamp component Maker ring",
    exact: true,
  });
  await move(page, 256, 420);
  await expect(ring).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
  await page.getByRole("menuitem", { name: "SVG", exact: true }).click();
  const svg = page.getByRole("button", {
    name: "Canvas stamp component SVG artwork",
    exact: true,
  });
  await svg.hover();
  await expect(svg).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  const bytes = await page.evaluate(() => {
    const c = document.createElement("canvas");
    c.width = c.height = 16;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "#00ff00";
    ctx.fillRect(0, 0, 16, 16);
    return c.toDataURL("image/png").split(",")[1];
  });
  const choosing = page.waitForEvent("filechooser");
  await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
  await page.getByRole("menuitem", { name: "Image", exact: true }).click();
  await (
    await choosing
  ).setFiles({
    name: "green.png",
    mimeType: "image/png",
    buffer: Buffer.from(bytes, "base64"),
  });
  const image = page.getByRole("button", {
    name: "Canvas stamp component green.png",
    exact: true,
  });
  await image.hover();
  await expect(image).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  await page.mouse.down();
  await expect(image).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  await page.keyboard.press("Escape");
  await page.mouse.up();
  await page.screenshot({
    path: ".playwright/stamp-fix/hover-transparent.png",
  });
  expect(errors).toEqual([]);
});

test("G previews real artwork movement, confirms one undoable edit and ignores a no-op confirmation", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    layer = before.layers[0];
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("g");
  await mode(page, "move");
  await page.keyboard.press("Enter");
  await mode(page, "");
  await expect(undoOf(page)).toBeDisabled();
  await page.keyboard.press("g");
  await mode(page, "move");
  await move(page, layer.x + 140, layer.y + 24, 8);
  await expect(valueOf(page, "position X")).toHaveValue(String(layer.x + 40));
  await expect(valueOf(page, "position Y")).toHaveValue(String(layer.y + 24));
  await expect(undoOf(page)).toBeDisabled();
  await expect
    .poll(() => previewSVG(page))
    .toContain(`translate(${layer.x + 40} ${layer.y + 24})`);
  await page.keyboard.press("Enter");
  await mode(page, "");
  const saved = await project(page);
  expect(saved.layers[0]).toEqual({
    ...layer,
    x: layer.x + 40,
    y: layer.y + 24,
  });
  expect(saved.layers.slice(1)).toEqual(before.layers.slice(1));
  await page.keyboard.press("Control+z");
  expect((await project(page)).layers).toEqual(before.layers);
  await expect(undoOf(page)).toBeDisabled();
  await page.keyboard.press("Control+Shift+z");
  expect((await project(page)).layers).toEqual(saved.layers);
  expect(errors).toEqual([]);
});

test("R rotates through the angle seam, snaps by 15 degrees and right-click cancels without saving a draft", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    layer = before.layers[0];
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("r");
  await mode(page, "rotate");
  for (const degrees of [20, 40, 90, 140, 175, 185, 225])
    await move(
      page,
      layer.x + 100 * Math.cos((degrees * Math.PI) / 180),
      layer.y + 100 * Math.sin((degrees * Math.PI) / 180),
    );
  await expect(valueOf(page, "rotation")).toHaveValue("225");
  await expect.poll(() => previewSVG(page)).toContain("rotate(225)");
  await page.keyboard.press("Enter");
  expect((await project(page)).layers[0]).toEqual({ ...layer, rotation: 225 });
  await page.keyboard.press("Control+z");
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("r");
  await page.keyboard.down("Shift");
  await move(
    page,
    layer.x + 100 * Math.cos((38 * Math.PI) / 180),
    layer.y + 100 * Math.sin((38 * Math.PI) / 180),
  );
  await expect(valueOf(page, "rotation")).toHaveValue("45");
  await page.keyboard.press("Enter");
  await page.keyboard.up("Shift");
  const snapped = await project(page);
  expect(snapped.layers[0].rotation).toBe(45);
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("r");
  const end = await move(page, layer.x, layer.y + 100);
  await expect(valueOf(page, "rotation")).toHaveValue("135");
  await page.mouse.click(end.x, end.y, { button: "right" });
  await mode(page, "");
  expect((await project(page)).layers).toEqual(snapped.layers);
  await page.keyboard.press("Control+z");
  expect((await project(page)).layers).toEqual(before.layers);
  expect(errors).toEqual([]);
});

test("S scales proportionally, left-click confirms without selecting an underlying component, and pivot/limits are safe", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    layer = before.layers[0];
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("s");
  await mode(page, "scale");
  const end = await move(page, layer.x, layer.y + 150, 8);
  await expect(valueOf(page, "width")).toHaveValue("547.5");
  await expect(valueOf(page, "height")).toHaveValue("216");
  await page.mouse.click(end.x, end.y);
  await mode(page, "");
  await expect(page.locator(".stamp-canvas-object.selected")).toHaveAttribute(
    "aria-label",
    "Canvas stamp component Text",
  );
  expect((await project(page)).layers[0]).toEqual({
    ...layer,
    width: 547.5,
    height: 216,
  });
  await page.keyboard.press("Control+z");
  await move(page, layer.x, layer.y);
  await page.keyboard.press("s");
  await move(page, layer.x + 50, layer.y);
  await expect
    .poll(async () => Number(await valueOf(page, "width").inputValue()))
    .toBeCloseTo(layer.width * Math.exp(0.5), 2);
  await page.keyboard.press("Escape");
  expect((await project(page)).layers).toEqual(before.layers);
  await move(page, layer.x + 10, layer.y);
  await page.keyboard.press("s");
  await move(page, layer.x + 250, layer.y);
  await expect(valueOf(page, "width")).toHaveValue("1024");
  await page.keyboard.press("Enter");
  const max = (await project(page)).layers[0];
  expect(max.width).toBe(1024);
  expect(max.width / max.height).toBeCloseTo(layer.width / layer.height, 6);
  expect(errors).toEqual([]);
});

test("modal axes/snapping, Escape/blur/selection cancellation and hidden/locked guards preserve document data", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    layer = before.layers[0];
  await move(page, layer.x + 100, layer.y);
  await page.keyboard.press("g");
  await page.keyboard.press("x");
  await page.keyboard.down("Shift");
  await move(page, layer.x + 127, layer.y + 23);
  await expect(valueOf(page, "position X")).toHaveValue("280");
  await expect(valueOf(page, "position Y")).toHaveValue("232");
  await expect(boardOf(page)).toHaveAttribute("data-transform-axis", "x");
  await page.keyboard.press("y");
  await expect(valueOf(page, "position X")).toHaveValue("256");
  await expect(valueOf(page, "position Y")).toHaveValue("256");
  await page.keyboard.press("y");
  await expect(boardOf(page)).toHaveAttribute("data-transform-axis", "");
  await expect(valueOf(page, "position X")).toHaveValue("280");
  await page.keyboard.press("Enter");
  await page.keyboard.up("Shift");
  expect((await project(page)).layers[0]).toEqual({ ...layer, x: 280, y: 256 });
  await page.keyboard.press("Control+z");
  for (const cancel of ["escape", "blur", "pointercancel", "selection"]) {
    await move(page, layer.x + 100, layer.y);
    await page.keyboard.press("g");
    await move(page, layer.x + 140, layer.y + 40);
    await expect(valueOf(page, "position X")).toHaveValue("296");
    if (cancel === "escape") await page.keyboard.press("Escape");
    else if (cancel === "blur")
      await page.evaluate(() => window.dispatchEvent(new Event("blur")));
    else if (cancel === "pointercancel")
      await page.evaluate(() =>
        window.dispatchEvent(
          new PointerEvent("pointercancel", { pointerId: 1, bubbles: true }),
        ),
      );
    else {
      await page
        .getByRole("button", {
          name: "Select stamp component Edition",
          exact: true,
        })
        .click();
      await page
        .getByRole("button", {
          name: "Select stamp component Text",
          exact: true,
        })
        .click();
    }
    await mode(page, "");
    expect((await project(page)).layers).toEqual(before.layers);
  }
  await page
    .getByRole("button", { name: "Lock stamp component", exact: true })
    .click();
  for (const key of ["g", "r", "s"]) {
    await expect(
      page.getByRole("button", {
        name: `${key === "g" ? "Move" : key === "r" ? "Rotate" : "Scale"} stamp component`,
        exact: true,
      }),
    ).toBeDisabled();
    await page.keyboard.press(key);
    await mode(page, "");
  }
  await page
    .getByRole("button", { name: "Unlock stamp component", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Hide stamp component Text", exact: true })
    .click();
  for (const key of ["g", "r", "s"]) {
    await page.keyboard.press(key);
    await mode(page, "");
  }
  await page
    .getByRole("button", { name: "Show stamp component Text", exact: true })
    .click();
  expect((await project(page)).layers).toEqual(before.layers);
  expect(errors).toEqual([]);
});

test("GRS hotkeys do not steal typing from rich text, names, SVG markup, numeric fields or modified shortcuts", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const rich = page.getByRole("textbox", {
    name: "Stamp rich text",
    exact: true,
  });
  await rich.fill("");
  await rich.pressSequentially("GRSgrs");
  await mode(page, "");
  expect((await project(page)).layers[0].runs.map((r) => r.text).join("")).toBe(
    "GRSgrs",
  );
  const name = page.getByLabel("Stamp name", { exact: true });
  await name.fill("");
  await name.pressSequentially("GRS mark");
  await name.press("Enter");
  await mode(page, "");
  await page.getByRole("button", { name: "Add stamp component", exact: true }).click();
  await page.getByRole("menuitem", { name: "SVG", exact: true }).click();
  const svg = page.getByRole("textbox", {
    name: "Stamp SVG markup",
    exact: true,
  });
  await svg.fill("static artwork ");
  await svg.pressSequentially("GRSgrs");
  await expect(svg).toHaveValue("static artwork GRSgrs");
  await mode(page, "");
  await valueOf(page, "width").fill("320");
  await valueOf(page, "width").press("r");
  await mode(page, "");
  await boardOf(page).focus();
  await boardOf(page).evaluate((e) => {
    for (const [key, modifier] of [
      ["s", "ctrlKey"],
      ["g", "altKey"],
      ["r", "metaKey"],
    ])
      e.dispatchEvent(
        new KeyboardEvent("keydown", { key, [modifier]: true, bubbles: true }),
      );
    e.dispatchEvent(
      new KeyboardEvent("keydown", {
        key: "g",
        isComposing: true,
        bubbles: true,
      }),
    );
  });
  await mode(page, "");
  expect((await project(page)).name).toBe("GRS mark");
  expect(errors).toEqual([]);
});

test("captured drag and rotated corner resize remain single transactions; pointer cancellation and no-op clicks are safe", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const before = await project(page),
    layer = before.layers[0];
  const title = page.getByRole("button", {
    name: "Canvas stamp component Text",
    exact: true,
  });
  await move(page, layer.x, layer.y);
  const centre = await clientPoint(page, layer.x, layer.y);
  await page.mouse.click(centre.x, centre.y);
  await expect(undoOf(page)).toBeDisabled();
  await page.mouse.down();
  await expect(boardOf(page)).toHaveAttribute(
    "data-transform-source",
    "pointer",
  );
  await move(page, layer.x + 40, layer.y + 12, 6);
  await expect(valueOf(page, "position X")).toHaveValue("296");
  await expect(title).toHaveCSS("background-color", "rgba(0, 0, 0, 0)");
  await page.mouse.up();
  expect((await project(page)).layers[0]).toEqual({ ...layer, x: 296, y: 244 });
  await page.keyboard.press("Control+z");
  await page.evaluate(() =>
    window.addEventListener(
      "pointerdown",
      (e) => {
        window.__stampPointerId = e.pointerId;
      },
      { once: true, capture: true },
    ),
  );
  await move(page, layer.x, layer.y);
  await page.mouse.down();
  await move(page, layer.x + 25, layer.y + 20);
  await expect(valueOf(page, "position X")).toHaveValue("281");
  await page.evaluate(() =>
    window.dispatchEvent(
      new PointerEvent("pointercancel", {
        pointerId: window.__stampPointerId,
        bubbles: true,
      }),
    ),
  );
  await mode(page, "");
  await page.mouse.up();
  expect((await project(page)).layers).toEqual(before.layers);
  await valueOf(page, "rotation").fill("90");
  const handle = page.getByRole("button", {
      name: "Resize selected stamp component",
      exact: true,
    }),
    corner = await handle.boundingBox(),
    canvasRect = await boardOf(page).boundingBox();
  await page.mouse.move(
    corner.x + corner.width / 2,
    corner.y + corner.height / 2,
  );
  await page.mouse.down();
  await mode(page, "resize");
  await page.mouse.move(
    corner.x + corner.width / 2 - (10 * canvasRect.width) / 512,
    corner.y + corner.height / 2 + (20 * canvasRect.height) / 512,
    { steps: 6 },
  );
  await expect(valueOf(page, "width")).toHaveValue("405");
  await expect(valueOf(page, "height")).toHaveValue("164");
  await page.mouse.up();
  expect((await project(page)).layers[0]).toEqual({
    ...layer,
    rotation: 90,
    width: 405,
    height: 164,
  });
  await page.keyboard.press("Control+z");
  expect((await project(page)).layers[0]).toEqual({ ...layer, rotation: 90 });
  await page.keyboard.press("Control+z");
  expect((await project(page)).layers).toEqual(before.layers);
  expect(errors).toEqual([]);
});

test("Stamp sliders reuse Texture's rounded tracks/value capsules, keep zero/negative values, and phone transform/export actions fit", async ({
  page,
}) => {
  test.setTimeout(150000);
  const errors = errorsOf(page);
  await page.goto("/?studio=texture");
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const styleOf = (input) =>
    input.evaluate((e) => {
      const style = getComputedStyle(e),
        capsule = getComputedStyle(
          e.closest(".slider-field").querySelector(".slider-value"),
        );
      return {
        height: style.height,
        radius: style.borderRadius,
        appearance: style.appearance,
        background: style.backgroundImage.replace(/-?[\d.]+%/g, "%"),
        capsuleHeight: capsule.height,
        capsuleRadius: capsule.borderRadius,
      };
    });
  const reference = await styleOf(
    page.getByRole("slider", { name: "Layer opacity", exact: true }),
  );
  await page
    .getByRole("navigation", { name: "Studio workspace" })
    .getByRole("button", { name: "Stamp", exact: true })
    .click();
  for (const name of [
    "Stamp position X",
    "Stamp position Y",
    "Stamp width",
    "Stamp height",
    "Stamp rotation",
    "Stamp component opacity",
    "Stamp Roughness",
  ])
    expect(
      await styleOf(page.getByRole("slider", { name, exact: true })),
    ).toEqual(reference);
  expect(reference.height).toBe("18px");
  await valueOf(page, "component opacity").fill("0");
  await valueOf(page, "position X").fill("-64");
  await valueOf(page, "rotation").fill("225");
  await valueOf(page, "Roughness").fill("0");
  const saved = await project(page);
  expect(saved.layers[0]).toMatchObject({ opacity: 0, x: -64, rotation: 225 });
  expect(saved.channelValues.roughness).toBe(0);
  await page
    .getByRole("button", { name: "Lock stamp component", exact: true })
    .click();
  await expect(
    page.getByRole("slider", { name: "Stamp width", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Unlock stamp component", exact: true })
    .click();
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByLabel("Document actions", { exact: true }).click();
  for (const name of [
    "Move stamp component",
    "Rotate stamp component",
    "Scale stamp component",
    "Open stamp project",
    "Save stamp project",
    "Use in Texture",
  ])
    await expect(
      page.getByRole("button", { name, exact: true }),
    ).toBeInViewport();
  await page.getByLabel("Document actions", { exact: true }).click();
  for (const name of ["SVG", "PNG"])
    await expect(
      page
        .locator(".stamp-canvas-toolbar")
        .getByRole("button", { name, exact: true }),
    ).toBeInViewport();
  await page
    .getByRole("button", { name: "Move stamp component", exact: true })
    .click();
  await mode(page, "move");
  await page.keyboard.press("Escape");
  await mode(page, "");
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({
    path: ".playwright/stamp-fix/phone-transform-toolbar.png",
  });
  expect(errors).toEqual([]);
});
