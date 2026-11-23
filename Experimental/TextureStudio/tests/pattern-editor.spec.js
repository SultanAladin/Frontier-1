import * as THREE from "three";
import { test, expect } from "@playwright/test";
import {
  movePatternLayer,
  resizePatternLayer,
  rotatePatternLayer,
  reflectPatternLayer,
  radialPatternLayers,
} from "../src/patternTransforms.js";
import {
  patternStarter,
  patternLayer,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import {
  resolvePatternBase,
  composePatternMaterial,
  patternPreviewShape,
} from "../src/patternSurface.js";
import { createMaterial, materials } from "../src/materials.js";

test("transform constraints, reflected SVG repeats and new rug compositions", () => {
  const l = { ...patternLayer("triangle"), rotation: 0, width: 80, height: 40 };
  expect(movePatternLayer(l, [11, 18], 16)).toMatchObject({ x: 272, y: 272 });
  expect(movePatternLayer(l, [1e6, -1e6], 16)).toMatchObject({
    x: 1024,
    y: -512,
  });
  const resized = resizePatternLayer(l, [340, 300], { grid: 16, aspect: true });
  expect(resized.width / resized.height).toBe(2);
  const turned = rotatePatternLayer(l, [256, 200], [320, 256], true);
  expect(turned.rotation).toBe(90);
  expect(reflectPatternLayer({ ...l, x: 90, rotation: 30 }, "x")).toMatchObject(
    { x: 422, rotation: -30, flipX: true },
  );
  expect(radialPatternLayers(l, 6, 100)[0]).toMatchObject({ x: 256, y: 156 });
  expect(radialPatternLayers(l, 6, 100)[3].y).toBeCloseTo(356);
  for (const name of [
    "Banded geometry",
    "Medallion rug",
    "Graduated lattice",
  ]) {
    const d = patternStarter(name);
    expect(d.name).toBe(name);
    expect(d.layers.length).toBeLessThanOrEqual(64);
    expect(d.layers.length).toBeGreaterThan(12);
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
  }
  const doc = validatePattern({
    ...patternStarter("Blank"),
    repeat: "mirror",
    layers: [{ ...l, rotation: 30, flipX: true }],
  });
  expect(patternSVG(doc)).toContain("rotate(-30)");
  expect(patternSVG(doc)).toContain("scale(-0.8 0.4)");
  const current = materials.find((m) => m.id === "natural-cotton");
  const base = resolvePatternBase(current, "pottery");
  expect(patternPreviewShape(base)).toBe("Teapot");
  expect(composePatternMaterial(base, doc).recipeId).toBe("pottery");
});

test("preview metadata edits reuse raster sources instead of rebuilding maps", () => {
  const base = materials.find((m) => m.id === "natural-cotton"),
    pattern = patternStarter("Medallion rug");
  const a = createMaterial({ ...base, pattern }),
    b = createMaterial({
      ...base,
      pattern: {
        ...pattern,
        name: "Renamed",
        repeats: 7,
        rotation: 30,
        mapping: "object",
      },
    });
  const shader = () => ({
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  });
  const sa = shader(),
    sb = shader();
  a.onBeforeCompile(sa);
  b.onBeforeCompile(sb);
  expect(sa.uniforms.uPatternColor.value).toBe(sb.uniforms.uPatternColor.value);
  expect(sb.uniforms.uPatternRepeats.value).toBe(7);
  expect(sb.uniforms.uPatternAngle.value).toBeCloseTo(Math.PI / 6);
  a.dispose();
  b.dispose();
});

async function exportedDoc(page) {
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const stream = await (await waiting).createReadStream(),
    chunks = [];
  for await (const c of stream) chunks.push(c);
  return JSON.parse(Buffer.concat(chunks).toString());
}

test("pointer resize/rotate, snapping, nudge, reflections and bounded radial copies", async ({
  page,
}) => {
  await page.goto("/?material=natural-cotton&studio=pattern");
  await page.getByLabel("Pattern collection").selectOption("Originals");
  await page.getByRole("button", { name: "Blank", exact: true }).click();
  await page.getByRole("button", { name: "triangle", exact: true }).click();
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.getByLabel("Grid snapping").selectOption("16");
  const canvas = page.getByLabel("Pattern design canvas");
  await canvas.focus();
  await page.keyboard.press("ArrowRight");
  await expect(page.getByLabel("X", { exact: true })).toHaveValue("272");
  await page.keyboard.press("Shift+ArrowDown");
  await expect(page.getByLabel("Y", { exact: true })).toHaveValue("416");
  await page.getByRole("button", { name: "Center Y", exact: true }).click();
  await page.getByRole("button", { name: "Center X", exact: true }).click();
  await page.getByLabel("Keep aspect ratio", { exact: true }).check();
  const size = page.getByLabel("Resize selected motif"),
    bounds = await size.boundingBox();
  await page.mouse.move(
    bounds.x + bounds.width / 2,
    bounds.y + bounds.height / 2,
  );
  await page.mouse.down();
  await page.mouse.move(bounds.x + 60, bounds.y + 40, { steps: 5 });
  await page.mouse.up();
  const width = Number(
    await page.getByLabel("Width", { exact: true }).inputValue(),
  );
  expect(width).toBeGreaterThan(160);
  expect(
    Number(await page.getByLabel("Height", { exact: true }).inputValue()),
  ).toBe(width);
  const rotation = await page.getByLabel("Rotate selected motif").boundingBox(),
    box = await canvas.boundingBox();
  await page.mouse.move(
    rotation.x + rotation.width / 2,
    rotation.y + rotation.height / 2,
  );
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.9, box.y + box.height * 0.5, {
    steps: 5,
  });
  await page.mouse.up();
  expect(
    Number(await page.getByLabel("Angle", { exact: true }).inputValue()),
  ).toBe(90);
  await page
    .getByRole("button", { name: "Reflect copy X", exact: true })
    .click();
  let doc = await exportedDoc(page);
  expect(doc.layers).toHaveLength(2);
  expect(doc.layers[1].flipX).toBe(true);
  await page.getByLabel("Radial copies").fill("6");
  await page
    .getByRole("button", { name: "Arrange in a ring", exact: true })
    .click();
  doc = await exportedDoc(page);
  expect(doc.layers).toHaveLength(7);
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.locator(".pe-layers button")).toHaveCount(2);
  await page
    .getByRole("button", { name: "Graduated lattice", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Arrange in a ring", exact: true })
    .click();
  await expect(page.getByRole("alert")).toContainText("64 layers");
  await expect(page.locator(".pe-layers button")).toHaveCount(64);
  await page.getByRole("button", { name: "Blank", exact: true }).click();
  await page.getByTitle("Draw path", { exact: true }).click();
  const drawing = await canvas.boundingBox();
  await page.mouse.move(
    drawing.x + drawing.width * 0.3,
    drawing.y + drawing.height * 0.3,
  );
  await page.mouse.down();
  await page.mouse.move(
    drawing.x + drawing.width * 0.7,
    drawing.y + drawing.height * 0.6,
    { steps: 8 },
  );
  await page.mouse.up();
  await expect(page.getByLabel("SVG path", { exact: true })).toHaveValue(
    /M.*L/,
  );
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.locator(".pe-layers button")).toHaveCount(0);
  expect(errors).toEqual([]);
});

test("live preview uses the applied shader, updates finishes and switches preview meshes", async ({
  page,
}, info) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?material=natural-cotton&studio=pattern");
  await page.getByLabel("Pattern collection").selectOption("Originals");
  await page
    .getByRole("button", { name: "Banded geometry", exact: true })
    .click();
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  const preview = page.getByRole("region", {
      name: "Pattern material preview",
    }),
    canvas = preview.locator("canvas");
  const ready = async () => {
    await expect(
      preview.getByRole("status", { name: "Material preview status" }),
    ).toContainText("Live material · ready");
    await expect(canvas).toHaveAttribute("data-material-ready", "true");
    await page.evaluate(
      () =>
        new Promise((r) =>
          requestAnimationFrame(() => requestAnimationFrame(r)),
        ),
    );
  };
  await ready();
  // Authoring workspaces unmount the hidden material renderer to avoid competing
  // WebGL contexts; the active preview remains the one authoritative canvas.
  expect(await page.locator("canvas").count()).toBe(1);
  const before = await canvas.evaluate((c) => c.toDataURL());
  await page.getByLabel("Motif color", { exact: true }).fill("#ff1020");
  await ready();
  expect(await canvas.evaluate((c) => c.toDataURL())).not.toBe(before);
  await page.getByLabel("Pattern preview object").selectOption("Panel");
  await ready();
  await page
    .getByLabel("Pattern preview lighting")
    .selectOption("Warm atelier");
  await ready();
  await page
    .getByRole("button", { name: "Inspect detail", exact: true })
    .click();
  await ready();
  await page.screenshot({ path: info.outputPath("rug-live-preview.png") });
  await page.getByLabel("Pattern base material").selectOption("pottery");
  await ready();
  await expect(page.getByLabel("Pattern preview object")).toHaveValue("Teapot");
  await page
    .getByRole("button", { name: "Painted blossoms", exact: true })
    .click();
  await page
    .getByLabel("Motif material", { exact: true })
    .selectOption("ceramic");
  await page.getByLabel("Relief (mm)", { exact: true }).fill("0");
  await page
    .getByRole("button", { name: "Assign this finish to all motifs" })
    .click();
  await ready();
  await page.getByRole("button", { name: "Fit preview", exact: true }).click();
  await ready();
  await page.getByLabel("Pattern preview zoom").fill("160");
  await ready();
  await page.screenshot({ path: info.outputPath("pottery-live-preview.png") });
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await expect(
    page.getByRole("dialog", { name: "Pattern studio" }),
  ).toHaveCount(0);
  await expect(page.locator("canvas")).toHaveCount(1);
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Teapot",
  );
  await expect(page.getByText("Glazed pottery", { exact: true })).toBeVisible();
  expect(errors).toEqual([]);
});
