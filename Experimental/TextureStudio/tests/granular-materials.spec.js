import { test, expect } from "@playwright/test";
import * as THREE from "three";
import { unzipSync, strFromU8 } from "fflate";
import {
  materials,
  normalizeMaterial,
  createMaterial,
} from "../src/materials.js";
import {
  getRecipe,
  applyRecipeControl,
  normalizeSandLayers,
  defaultSandLayer,
} from "../src/materialProfiles.js";

const ids = ["corrugated-zinc", "corrugated-steel", "sand", "layered-sand"];
const preset = (id) => materials.find((p) => p.id === id);
function compiled(p) {
  const m = createMaterial(p),
    s = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
  m.onBeforeCompile(s);
  m.dispose();
  return s;
}
async function download(page, button) {
  const waiting = page.waitForEvent("download");
  await button.click();
  const stream = await (await waiting).createReadStream(),
    chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  return Buffer.concat(chunks);
}
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
async function stats(page, files) {
  return page.evaluate(
    async (input) => {
      const out = {};
      for (const [key, bytes] of Object.entries(input)) {
        const bitmap = await createImageBitmap(
            new Blob([new Uint8Array(bytes)], { type: "image/png" }),
          ),
          c = document.createElement("canvas");
        c.width = bitmap.width;
        c.height = bitmap.height;
        const ctx = c.getContext("2d");
        ctx.drawImage(bitmap, 0, 0);
        bitmap.close();
        const data = ctx.getImageData(0, 0, c.width, c.height).data;
        let lo = 255,
          hi = 0,
          minAlpha = 255;
        const unique = new Set();
        for (let i = 0; i < data.length; i += 4) {
          lo = Math.min(lo, data[i]);
          hi = Math.max(hi, data[i]);
          minAlpha = Math.min(minAlpha, data[i + 3]);
          unique.add(`${data[i]},${data[i + 1]},${data[i + 2]}`);
        }
        out[key] = { lo, hi, minAlpha, unique: unique.size };
      }
      return out;
    },
    Object.fromEntries(
      Object.entries(files).map(([key, bytes]) => [key, Array.from(bytes)]),
    ),
  );
}

test("four distinct material presets keep dielectric sand separate from conductor sheets and paint", () => {
  for (const id of ids) expect(preset(id)).toBeTruthy();
  for (const id of ["sand", "layered-sand"]) {
    const p = normalizeMaterial({
      ...preset(id),
      metalness: 1,
      coat: 1,
      translucency: 0.8,
    });
    expect(p).toMatchObject({ metalness: 0, coat: 0, translucency: 0 });
    expect(getRecipe(p).id).toBe(id === "sand" ? "sand" : "layeredSand");
    expect(getRecipe(p).supportsMetalScratches).toBe(false);
    const shader = compiled(p);
    expect(shader.fragmentShader).toContain("#define uType " + p.type);
    expect(shader.fragmentShader).toContain("grainF0*sandSpecular");
    expect(shader.fragmentShader).toContain("roughnessFactor=sandRoughness");
    expect(shader.uniforms.uSandSize.value[0].x).toBeGreaterThanOrEqual(0.0625);
  }
  for (const id of ["corrugated-zinc", "corrugated-steel"]) {
    expect(preset(id)).toMatchObject({
      type: 21,
      metalness: 1,
      previewShape: "Panel",
      weaveAngle: 90,
    });
    expect(getRecipe(preset(id)).supportsMetalScratches).toBe(true);
  }
  expect(preset("corrugated-zinc").ribProfile).toBe(0);
  expect(preset("corrugated-steel").ribProfile).toBe(1);
  expect(preset("corrugated-aluminium").sheetFinish).toBe(0); // Old pipe remains unchanged.
});

test("sand size / specular / roughness controls stay finite and preserve zero values", () => {
  const p = preset("sand"),
    recipe = getRecipe(p);
  for (const control of recipe.controls)
    for (const value of [-100, 0, 1, 100]) {
      const next = applyRecipeControl(p, control, value);
      expect(next[control.key]).toBeGreaterThanOrEqual(control.min);
      expect(next[control.key]).toBeLessThanOrEqual(control.max);
    }
  const p0 = normalizeMaterial({
    ...p,
    grainSpecular: 0,
    grainSpecularVariation: 0,
    grainRelief: 0,
    grainTilt: 0,
    grainSize: NaN,
    grainIOR: Infinity,
  });
  expect(p0).toMatchObject({
    grainSpecular: 0,
    grainSpecularVariation: 0,
    grainRelief: 0,
    grainTilt: 0,
  });
  expect(p0.grainSize).toBe(0.65);
  expect(p0.grainIOR).toBe(1.544);
});

test("sand beds retain independent finite properties, unique identities and a four-bed resource cap", () => {
  const beds = normalizeSandLayers([
    ...preset("layered-sand").sandLayers,
    { ...defaultSandLayer(3), specular: 0, roughness: 0.9, size: 2 },
    defaultSandLayer(4),
  ]);
  expect(beds).toHaveLength(4);
  expect(beds[3]).toMatchObject({ specular: 0, roughness: 0.9, size: 2 });
  expect(new Set(beds.map((b) => b.id)).size).toBe(4);
  const changed = normalizeSandLayers(
    beds.map((b, i) => (i === 1 ? { ...b, size: 0.1, specular: 0 } : b)),
  );
  expect(changed[0]).toEqual(beds[0]);
  expect(changed[2]).toEqual(beds[2]);
  expect(changed[1].specular).toBe(0);
  const shader = compiled({ ...preset("layered-sand"), sandLayers: changed });
  expect(shader.uniforms.uSandCount.value).toBe(4);
  expect(shader.uniforms.uSandFinish.value[1].z).toBe(0);
  expect(shader.uniforms.uSandSize.value[1].x).toBe(0.1);
  const swapped = compiled({
    ...preset("layered-sand"),
    sandLayers: [changed[1], changed[0], ...changed.slice(2)],
  });
  expect(swapped.uniforms.uSandLayerSeeds.value[0]).toBe(
    shader.uniforms.uSandLayerSeeds.value[1],
  );
  expect(swapped.uniforms.uSandLayerSeeds.value[1]).toBe(
    shader.uniforms.uSandLayerSeeds.value[0],
  );
});

test("layered sand edits, bed operations, saved JSON and portable shader exports preserve the real bed properties", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await page.goto("/?material=layered-sand");
  await expect(page.locator("h1")).toHaveText("Layered Sand");
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Panel",
  );
  await expect(page.locator(".viewport-panel canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await expect(
    page.getByRole("button", { name: "Select sand bed 1", exact: true }),
  ).toBeVisible();
  await page.getByLabel("Bed grain size value", { exact: true }).fill("1.5");
  await page.getByLabel("Bed roughness value", { exact: true }).fill("0.2");
  await page.getByLabel("Bed specular value", { exact: true }).fill("0.1");
  await page
    .getByRole("button", { name: "Select sand bed 2", exact: true })
    .click();
  await expect(
    page.getByLabel("Bed grain size value", { exact: true }),
  ).toHaveValue("0.25");
  await expect(
    page.getByLabel("Bed roughness value", { exact: true }),
  ).toHaveValue("0.78");
  await page.getByRole("button", { name: "Add sand bed", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Add sand bed", exact: true }),
  ).toBeDisabled();
  await page
    .getByRole("button", { name: "Remove sand bed", exact: true })
    .click();
  await page
    .getByLabel("Sand layer arrangement", { exact: true })
    .selectOption("bands");
  await expect(
    page.getByLabel("Bed band thickness value", { exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: /Export material/ }).click();
  const json = JSON.parse(
    (
      await download(
        page,
        page.getByRole("button", { name: /Material preset All surface/ }),
      )
    ).toString(),
  );
  expect(json.material.sandLayers).toHaveLength(3);
  expect(json.material.sandArrangement).toBe("bands");
  expect(json.material.sandLayers[0]).toMatchObject({
    size: 1.5,
    roughness: 0.2,
    specular: 0.1,
  });
  await page.getByRole("button", { name: /Export material/ }).click();
  const text = (
    await download(
      page,
      page.getByRole("button", { name: /Three.js procedural shader/ }),
    )
  ).toString();
  expect(text).toContain("sandCellAt");
  expect(text).toContain("grainF0*sandSpecular");
  const code = text
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export (const|let|var|class|function|async function) /g, "$1 ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const m = new Function("THREE", code)(THREE);
  expect(m.userData.params.sandLayers[0]).toMatchObject({
    size: 1.5,
    roughness: 0.2,
    specular: 0.1,
  });
  const s = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  m.onBeforeCompile(s);
  m.dispose();
  expect(s.uniforms.uSandCount.value).toBe(3);
  expect(errors).toEqual([]);
});

test("sand grain size/specular affect the real viewport and corrugated steel offers garage profile controls", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await page.goto("/?material=sand");
  await expect(page.locator(".viewport-panel canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const canvas = page.locator(".viewport-panel canvas"),
    before = await canvas.evaluate((c) => c.toDataURL());
  await page.getByLabel("Grain size value", { exact: true }).fill("1.8");
  await page.getByLabel("Grain specular value", { exact: true }).fill("0");
  await page.getByLabel("Specular variation value", { exact: true }).fill("0");
  await expect
    .poll(() => canvas.evaluate((c) => c.toDataURL()))
    .not.toBe(before);
  await page
    .getByRole("button", { name: "Apply Corrugated Steel", exact: true })
    .click();
  await expect(page.getByLabel("Preview object", { exact: true })).toHaveValue(
    "Panel",
  );
  await expect(
    page.getByLabel("Corrugation profile", { exact: true }),
  ).toHaveValue("1");
  await page
    .getByLabel("Corrugation profile", { exact: true })
    .selectOption("2");
  await expect(
    page.getByLabel("Corrugation profile", { exact: true }),
  ).toHaveValue("2");
  await expect(
    page.getByLabel("Enable scratches", { exact: true }),
  ).toBeVisible();
  expect(errors).toEqual([]);
});

test("all four materials bake actual base color, roughness, normals and height; sand is not metallic", async ({
  page,
}) => {
  test.setTimeout(420000);
  const errors = errorsOf(page);
  const results = {};
  for (const id of ids) {
    await page.goto(`/?studio=baking&material=${id}`);
    await page
      .getByRole("button", { name: "Material patch", exact: true })
      .click();
    await expect(page.locator(".bk-scene canvas")).toHaveAttribute(
      "data-material-ready",
      "true",
    );
    await page
      .getByLabel("Bake resolution", { exact: true })
      .selectOption("256");
    await page
      .getByLabel("Bake patch width", { exact: true })
      .fill(id.includes("sand") ? "16" : "160");
    const files = unzipSync(
      await download(
        page,
        page.getByRole("button", { name: "Bake & download ZIP", exact: true }),
      ),
    );
    const manifest = JSON.parse(strFromU8(files["material.json"]));
    expect(manifest.material.id).toBe(id);
    expect(Object.keys(files).filter((k) => k.endsWith(".png"))).toHaveLength(
      6,
    );
    results[id] = await stats(
      page,
      Object.fromEntries(
        ["base-color", "roughness", "normal", "height", "metalness"].map(
          (k) => [k, files[k + ".png"]],
        ),
      ),
    );
    expect(results[id]["base-color"].unique).toBeGreaterThan(10);
    expect(results[id].normal.unique).toBeGreaterThan(10);
    expect(results[id].height.hi - results[id].height.lo).toBeGreaterThan(5);
    expect(results[id].roughness.hi - results[id].roughness.lo).toBeGreaterThan(
      5,
    );
    for (const map of Object.values(results[id]))
      expect(map.minAlpha).toBe(255);
    if (id.includes("sand")) expect(results[id].metalness.hi).toBe(0);
    else {
      expect(results[id].metalness.hi).toBeGreaterThan(220);
      expect(manifest.channels["height.png"].rangeSceneUnits).toBeGreaterThan(
        preset(id).ribDepth * 2,
      );
    }
  }
  expect(errors).toEqual([]);
});
