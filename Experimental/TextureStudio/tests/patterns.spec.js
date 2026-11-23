import { test, expect } from "@playwright/test";
import {
  patternStarter,
  validatePattern,
  patternSVG,
  patternDimensions,
} from "../src/patternDocument.js";
import { leatherSourceSVG } from "../src/leatherSource.js";
import { materials, normalizeMaterial } from "../src/materials.js";

test("portable pattern bounds, vector source and stable leather identities", () => {
  for (const name of [
    "Diamond weave",
    "Painted blossoms",
    "Cube lattice",
    "Inlaid tile",
  ]) {
    const doc = patternStarter(name);
    expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    expect(patternSVG(doc)).toContain("<svg");
    expect(doc.layers.length).toBeLessThanOrEqual(64);
  }
  expect(patternDimensions(patternStarter("Painted blossoms"))).toEqual([
    1024, 512,
  ]);
  expect(() =>
    validatePattern({ ...patternStarter(), layers: Array(65).fill({}) }),
  ).toThrow("64");
  expect(() =>
    validatePattern({
      ...patternStarter(),
      layers: [{ kind: "path", path: '"/><script>bad</script>' }],
    }),
  ).toThrow("Invalid");
  expect(leatherSourceSVG()).toBe(leatherSourceSVG());
  for (const p of materials.filter((m) => m.category === "Leather")) {
    expect(normalizeMaterial({ ...p, id: "custom" }).hideVariant).toBe(
      p.hideVariant,
    );
  }
  expect(
    new Set(
      materials
        .filter((m) => m.category === "Leather")
        .map((p) => p.hideVariant),
    ).size,
  ).toBe(4);
});

async function frame(page) {
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page.evaluate(
    () =>
      new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))),
  );
}
async function download(page, button) {
  const promise = page.waitForEvent("download");
  await button.click();
  const stream = await (await promise).createReadStream(),
    chunks = [];
  for await (const c of stream) chunks.push(c);
  return Buffer.concat(chunks).toString();
}

test("editor shapes, vector/image imports, undo, assignments, save/reload and independent shader export", async ({
  page,
}, info) => {
  test.setTimeout(600000);
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.goto("/?material=natural-cotton");
  await frame(page);
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  const editor = page.getByRole("dialog", { name: "Pattern studio" });
  await page.getByLabel("Pattern collection").selectOption("Originals");
  await editor
    .getByRole("button", { name: "Inlaid tile", exact: true })
    .click();
  await page.getByLabel("Pattern name").fill("Workshop pattern");
  await page.getByLabel("Repeat layout").selectOption("mirror");
  await expect(page.getByLabel("Repeat layout")).toHaveValue("mirror");
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.getByLabel("Repeat layout")).toHaveValue("straight");
  await page.getByTitle("Redo", { exact: true }).click();
  await page.getByRole("button", { name: "ellipse", exact: true }).click();
  await page.getByLabel("Motif material", { exact: true }).selectOption("wool");
  await page.getByLabel("Relief (mm)", { exact: true }).fill(".7");
  await page.getByTitle("Duplicate motif").click();
  await page.getByTitle("Delete motif").click();
  await page.locator('input[accept=".svg"]').setInputFiles({
    name: "unsafe.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg xmlns="http://www.w3.org/2000/svg"><script>window.pwned=1</script></svg>',
    ),
  });
  await expect(page.getByRole("alert")).toContainText(
    "Unsupported SVG element",
  );
  await page.locator('input[accept=".svg"]').setInputFiles({
    name: "ornament.svg",
    mimeType: "image/svg+xml",
    buffer: Buffer.from(
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200"><g fill="#123456"><circle cx="100" cy="100" r="80"/><path d="M10 10L50 10L30 60Z"/></g></svg>',
    ),
  });
  await expect(page.getByLabel("Layer name")).toHaveValue("ornament.svg");
  await expect(page.getByLabel("SVG source")).toBeAttached();
  const png = await page.evaluate(() => {
    const c = document.createElement("canvas");
    c.width = c.height = 20;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "red";
    ctx.fillRect(0, 0, 10, 20);
    return c.toDataURL().split(",")[1];
  });
  await page
    .getByLabel("Import pattern image", { exact: true })
    .setInputFiles({
      name: "stamp.png",
      mimeType: "image/png",
      buffer: Buffer.from(png, "base64"),
    });
  await expect(page.getByLabel("Layer name")).toHaveValue("stamp.png");
  const doc = JSON.parse(
    await download(
      page,
      page.getByRole("button", { name: "Save document", exact: true }),
    ),
  );
  expect(doc.layers.some((l) => l.kind === "svg")).toBe(true);
  expect(doc.layers.some((l) => l.kind === "image")).toBe(true);
  const svg = await download(
    page,
    page.getByRole("button", { name: "Export seamless SVG", exact: true }),
  );
  expect(svg).toContain("<circle");
  expect(svg).toContain("data:image/png");
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await frame(page);
  await page.getByLabel("Preview object").selectOption("Teapot");
  await frame(page);
  await page.getByRole("button", { name: /Save as preset/ }).click();
  await page
    .getByPlaceholder("Give your material a name")
    .fill("Pattern persistence");
  await page.getByRole("button", { name: "Save to my library" }).click();
  const saved = await page.evaluate(() =>
    JSON.parse(localStorage.getItem("alloy-saved")).at(-1),
  );
  expect(saved.pattern).toEqual(doc);
  await page.reload();
  await page
    .getByRole("button", { name: "Apply Pattern persistence", exact: true })
    .click();
  await frame(page);
  await page.getByRole("button", { name: /Export material/ }).click();
  const source = await download(
    page,
    page.getByRole("button", { name: /Three.js procedural shader/ }),
  );
  const factory = source
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    )
    .replace(/export /g, "");
  const compiled = await page.evaluate(async (factory) => {
    const THREE = await import("/node_modules/.vite-studio/deps/three.js");
    const m = new Function("THREE", factory)(THREE);
    await m.userData.ready;
    const renderer = new THREE.WebGLRenderer(),
      scene = new THREE.Scene(),
      cam = new THREE.PerspectiveCamera(40, 1, 0.1, 20),
      g = new THREE.PlaneGeometry(2, 2);
    renderer.setSize(64, 64);
    cam.position.z = 4;
    scene.add(new THREE.AmbientLight(0xffffff, 2));
    scene.add(new THREE.Mesh(g, m));
    let error = "";
    renderer.debug.onShaderError = (gl, p, v, f) =>
      (error = gl.getShaderInfoLog(f));
    await renderer.compileAsync(scene, cam);
    renderer.render(scene, cam);
    const out = {
      error,
      pattern: m.userData.params.pattern.name,
      textures: !!m.userData.shader.uniforms.uPatternFinish.value.image,
    };
    m.dispose();
    g.dispose();
    renderer.dispose();
    return out;
  }, factory);
  expect(compiled).toEqual({
    error: "",
    pattern: "Workshop pattern",
    textures: true,
  });
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await expect(page.getByLabel("Pattern name")).toHaveValue("Workshop pattern");
  await page.screenshot({ path: info.outputPath("pattern-editor.png") });
  expect(await page.evaluate(() => window.pwned)).toBeUndefined();
  expect(errors).toEqual([]);
});

test("material slots reach real bake channels; seamless raster and continuous leather variation checks", async ({
  page,
}) => {
  test.setTimeout(600000);
  await page.goto("/?material=cognac-leather");
  await frame(page);
  const data = await page.evaluate(async () => {
    const { patternStarter, validatePattern, patternSVG } =
        await import("/src/patternDocument.js"),
      { rasterPatternSVG, rasterLeatherSource } =
        await import("/src/patternRuntime.js");
    const { bakeMaterial } = await import("/src/bakeMaterial.js"),
      { materials } = await import("/src/materials.js"),
      { unzipSync } = await import("/node_modules/.vite-studio/deps/fflate.js");
    const doc = validatePattern({
      ...patternStarter("Blank"),
      repeats: 1,
      layers: [
        {
          kind: "rect",
          x: 128,
          y: 256,
          width: 256,
          height: 512,
          color: "#cc6633",
          finish: "wool",
        },
        {
          kind: "rect",
          x: 384,
          y: 256,
          width: 256,
          height: 512,
          color: "#cc6633",
          finish: "foil",
        },
      ],
    });
    const { bytes, manifest } = await bakeMaterial(
      { ...materials.find((p) => p.id === "natural-cotton"), pattern: doc },
      { resolution: 256 },
    );
    const z = unzipSync(bytes),
      maps = {};
    for (const name of ["roughness", "metalness", "height", "normal"]) {
      const image = await createImageBitmap(
        new Blob([z[name + ".png"]], { type: "image/png" }),
      );
      const c = document.createElement("canvas");
      c.width = c.height = 256;
      const ctx = c.getContext("2d");
      ctx.drawImage(image, 0, 0);
      const rgba = ctx.getImageData(0, 0, 256, 256).data;
      maps[name] = [rgba[(128 * 256 + 64) * 4], rgba[(128 * 256 + 192) * 4]];
      image.close();
    }
    // Compare an ordinary repeat boundary with a wrapped image edge: a seamless
    // motif must match when independently translated by one complete supertile.
    const tile = patternStarter("Painted blossoms");
    const svg = patternSVG(tile);
    const image = await rasterPatternSVG(svg, 1024, 512);
    const c = document.createElement("canvas");
    c.width = 2048;
    c.height = 512;
    const ctx = c.getContext("2d");
    ctx.drawImage(image, 0, 0);
    ctx.drawImage(image, 1024, 0);
    const rgba = ctx.getImageData(0, 0, 2048, 512).data;
    let edge = 0;
    for (let y = 0; y < 512; y++)
      for (let k = 0; k < 3; k++)
        edge += Math.abs(
          rgba[(y * 2048 + 1023) * 4 + k] - rgba[(y * 2048 + 1024) * 4 + k],
        );
    const atlas = await rasterLeatherSource();
    const ac = atlas.getContext("2d");
    const left = ac.getImageData(0, 0, 2, 1028).data,
      right = ac.getImageData(1024, 0, 2, 1028).data;
    let gutterError = 0;
    for (let i = 0; i < left.length; i++)
      gutterError = Math.max(gutterError, Math.abs(left[i] - right[i]));
    // Procedural variation should change height without changing its amplitude range.
    const hides = [];
    for (const hideVariation of [0, 0.9]) {
      const baked = await bakeMaterial(
        { ...materials.find((p) => p.id === "cognac-leather"), hideVariation },
        { resolution: 256 },
      );
      hides.push(unzipSync(baked.bytes)["height.png"]);
    }
    return {
      maps,
      range: manifest.channels["height.png"].rangeSceneUnits,
      edge: edge / (512 * 3),
      gutterError,
      different: hides[0].some((v, i) => v !== hides[1][i]),
    };
  });
  expect(data.maps.roughness[0]).toBeGreaterThan(220);
  expect(data.maps.roughness[1]).toBeLessThan(100);
  expect(data.maps.metalness[0]).toBeLessThan(3);
  expect(data.maps.metalness[1]).toBeGreaterThan(250);
  expect(data.maps.height[0]).toBeGreaterThan(data.maps.height[1] + 15);
  expect(data.range).toBe(0.05);
  expect(data.edge).toBeLessThan(5);
  expect(data.gutterError).toBe(0);
  expect(data.different).toBe(true);
});

test("pattern deep link, keyboard focus and seeded generator", async ({
  page,
}) => {
  await page.goto("/?studio=pattern");
  await expect(
    page.getByRole("dialog", { name: "Pattern studio" }),
  ).toBeVisible();
  await expect(
    page.getByRole("button", { name: "Save document", exact: true }),
  ).toBeFocused();
  await page.getByLabel("Layout seed").fill("42");
  await page.getByLabel("Layout count").fill("24");
  await page
    .getByRole("button", { name: "Generate pattern", exact: true })
    .click();
  await expect(page.getByLabel("Pattern name")).toHaveValue(
    "Geometric study 42",
  );
  await expect(page.locator(".pe-layers button")).toHaveCount(24);
  await page.getByTitle("Undo", { exact: true }).click();
  await expect(page.getByLabel("Pattern name")).toHaveValue("Diamond weave");
});
