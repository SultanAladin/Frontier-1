import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 60000 });
async function modules(page) {
  // Same-origin module document, no second app or WebGL renderer for CPU tests.
  await page.goto("/src/digitalBrush.js");
}
test("real round/soft/square/grain tips and live-preview dabs change actual pixels deterministically", async ({
  page,
}) => {
  await modules(page);
  const values = await page.evaluate(async () => {
    const { normalizeDigitalBrush, digitalBrushDabs, drawDigitalDabs } =
      await import("/src/digitalBrush.js");
    const render = (props, seed = 42) => {
      const c = document.createElement("canvas");
      c.width = 256;
      c.height = 128;
      const b = normalizeDigitalBrush({
        size: 48,
        sizePressure: 0,
        color: "#ec3021",
        ...props,
      });
      drawDigitalDabs(
        c.getContext("2d"),
        digitalBrushDabs(
          [
            [0.15, 0.25, 0.15],
            [0.85, 0.25, 1],
          ],
          b,
          seed,
        ),
        b,
      );
      const data = c.getContext("2d").getImageData(0, 0, 256, 128).data;
      let hash = 2166136261,
        alpha = 0;
      for (let i = 0; i < data.length; i++) {
        hash = Math.imul(hash ^ data[i], 16777619);
        if (i % 4 === 3) alpha += data[i];
      }
      return { hash: hash >>> 0, alpha };
    };
    return {
      round: render({}),
      repeat: render({}),
      soft: render({ hardness: 0.1 }),
      square: render({ shape: "square" }),
      grain: render({ grain: "noise", grainAmount: 0.7 }),
      zero: render({ opacity: 0 }),
      pressure: render({ sizePressure: 1 }),
      dynamics: render({ dabHue: 0.4, dabBrightness: 0.4 }),
    };
  });
  expect(values.round).toEqual(values.repeat);
  for (const key of ["soft", "square", "grain", "pressure", "dynamics"])
    expect(values[key].hash).not.toBe(values.round.hash);
  expect(values.soft.alpha).toBeLessThan(values.round.alpha);
  expect(values.grain.alpha).toBeLessThan(values.round.alpha);
  expect(values.zero.alpha).toBe(0);
});
test("channel-specific erasing removes only its target, fills clip to Lid, and field filtering uses premultiplied linear RGB", async ({
  page,
}) => {
  await modules(page);
  const result = await page.evaluate(async () => {
    const { createTextureLayer } = await import("/src/textureDocument.js");
    const { createTextureTeapotGeometry, AUTHORING_UV_SET } =
      await import("/src/textureSurfaceModel.js");
    const { normalizeDigitalBrush } = await import("/src/digitalBrush.js");
    const { createDigitalPaintAtlas, rasterizeDigitalChannels } =
      await import("/src/digitalPaintRaster.js");
    const { DIGITAL_PAINT_SCHEMA } =
      await import("/src/textureDigitalPaint.js");
    const geometry = createTextureTeapotGeometry(20),
      b = normalizeDigitalBrush({
        size: 24,
        hardness: 1,
        sizePressure: 0,
        color: "#808080",
      });
    const fill = {
      id: "fill",
      kind: "fill",
      region: { kind: "object", partId: "lid" },
      brush: b,
      channels: { baseColor: "#808080", roughness: 0.2 },
      points: [],
      seed: 42,
    };
    const erase = {
      ...fill,
      id: "erase",
      kind: "erase",
      region: { kind: "island", partId: "lid", id: 25 },
      points: [[1.12, 0.56, 1]],
      channels: { roughness: 0 },
      brush: { ...b, size: 32 },
    };
    const layer = createTextureLayer("paint", {
      channels: ["baseColor", "roughness"],
      paint: {
        schema: DIGITAL_PAINT_SCHEMA,
        uvSet: AUTHORING_UV_SET,
        operations: [fill, erase],
      },
    });
    const images = rasterizeDigitalChannels(layer, geometry),
      sample = (id, u, v) =>
        Array.from(
          images
            .get(id)
            .getContext("2d")
            .getImageData(Math.floor(u * 256), Math.floor((1 - v) * 256), 1, 1)
            .data,
        );
    const base = sample("baseColor", 1.12, 0.56),
      rough = sample("roughness", 1.12, 0.56),
      body = sample("baseColor", 0.12, 0.12),
      unErased = sample("roughness", 1.37, 0.56);
    const atlas = createDigitalPaintAtlas(layer, geometry),
      index = (Math.floor(0.56 * 256) * 1024 + Math.floor(1.12 * 256)) * 4,
      linear = Array.from(atlas.texture.image.data.slice(index, index + 4));
    const off = createTextureLayer("paint", { ...layer, channels: [] });
    const retained = JSON.stringify(off.paint) === JSON.stringify(layer.paint);
    atlas.texture.dispose();
    geometry.dispose();
    return {
      base,
      rough,
      body,
      unErased,
      linear,
      retained,
      offSize: rasterizeDigitalChannels(off, createTextureTeapotGeometry(20))
        .size,
    };
  });
  expect(result.base[3]).toBeGreaterThan(250);
  expect(result.rough[3]).toBe(0);
  expect(result.body[3]).toBe(0);
  expect(result.unErased[3]).toBeGreaterThan(250);
  expect(result.linear[0]).toBeGreaterThan(52);
  expect(result.linear[0]).toBeLessThan(57);
  expect(result.linear[3]).toBe(255);
  expect(result.retained).toBe(true);
  expect(result.offSize).toBe(0);
});
test("digital mask erase restores fill/gradient instead of painting black; other parts remain untouched", async ({
  page,
}) => {
  await modules(page);
  const result = await page.evaluate(async () => {
    const { createTextureTeapotGeometry, AUTHORING_UV_SET } =
      await import("/src/textureSurfaceModel.js");
    const { normalizeDigitalBrush } = await import("/src/digitalBrush.js");
    const { rasterizeMaskField } = await import("/src/maskPaintRaster.js");
    const { normalizeTextureMask } = await import("/src/textureMasks.js");
    const geometry = createTextureTeapotGeometry(20),
      brush = normalizeDigitalBrush({ size: 28, hardness: 1, sizePressure: 0 }),
      region = { kind: "island", partId: "body", id: 1 };
    const ink = {
      kind: "brush",
      region,
      value: 0,
      opacity: 1,
      radius: 0.055,
      hardness: 1,
      points: [[0.12, 0.12]],
      digital: { brush, seed: 4, operation: "paint", pressure: [1] },
    };
    const erase = { ...ink, digital: { ...ink.digital, operation: "erase" } };
    const mask = normalizeTextureMask({
        kind: "fill",
        fill: 1,
        paint: { uvSet: AUTHORING_UV_SET, operations: [ink] },
      }),
      after = normalizeTextureMask({
        ...mask,
        paint: { uvSet: AUTHORING_UV_SET, operations: [ink, erase] },
      });
    const sample = (m, u, v) =>
      rasterizeMaskField(m, geometry)
        .getContext("2d")
        .getImageData(Math.floor(u * 256), Math.floor((1 - v) * 256), 1, 1)
        .data[0];
    const out = {
      before: sample(mask, 0.12, 0.12),
      after: sample(after, 0.12, 0.12),
      lid: sample(mask, 1.12, 0.56),
    };
    geometry.dispose();
    return out;
  });
  expect(result.before).toBe(0);
  expect(result.after).toBe(255);
  expect(result.lid).toBe(255);
});
test("stroke opacity is applied once, not multiplied by the number of pointer events", async ({
  page,
}) => {
  await modules(page);
  const result = await page.evaluate(async () => {
    const { createTextureTeapotGeometry, AUTHORING_UV_SET } =
      await import("/src/textureSurfaceModel.js");
    const { normalizeDigitalBrush } = await import("/src/digitalBrush.js");
    const { createTextureLayer } = await import("/src/textureDocument.js");
    const { rasterizeDigitalChannels } =
      await import("/src/digitalPaintRaster.js");
    const { DIGITAL_PAINT_SCHEMA } =
      await import("/src/textureDigitalPaint.js");
    const geometry = createTextureTeapotGeometry(20),
      brush = normalizeDigitalBrush({
        size: 32,
        opacity: 0.5,
        hardness: 1,
        sizePressure: 0,
      });
    const layer = createTextureLayer("paint", {
      channels: ["baseColor"],
      paint: {
        schema: DIGITAL_PAINT_SCHEMA,
        uvSet: AUTHORING_UV_SET,
        operations: [
          {
            id: "half",
            kind: "paint",
            seed: 1,
            region: { kind: "island", partId: "body", id: 1 },
            brush,
            channels: { baseColor: "#ffffff" },
            points: [
              [0.07, 0.12, 1],
              [0.18, 0.12, 1],
            ],
          },
        ],
      },
    });
    const sample = rasterizeDigitalChannels(layer, geometry)
      .get("baseColor")
      .getContext("2d")
      .getImageData(31, 225, 1, 1).data[3];
    geometry.dispose();
    return sample;
  });
  expect(result).toBeGreaterThan(125);
  expect(result).toBeLessThan(130);
});

test("channel bindings survive native painting, all-channel disabling and re-enabling; underlay cache is lower-only and restores visibility on failure", async ({
  page,
}) => {
  await modules(page);
  const result = await page.evaluate(async () => {
    const source = await (await fetch("/src/digitalPaintSurface.js")).text();
    const THREE = await import(
      source.match(/from\s*["']([^"']*\/three\.js[^"']*)["']/)[1]
    );
    const { createDigitalPaintSurfaceController } =
      await import("/src/digitalPaintSurface.js");
    const { createTextureDocument, createTextureLayer } =
      await import("/src/textureDocument.js");
    const { createTextureTeapotGeometry, AUTHORING_UV_SET } =
      await import("/src/textureSurfaceModel.js");
    const { textureLayerRenderOrder } =
      await import("/src/textureStackEvaluation.js");
    const { normalizeDigitalBrush } = await import("/src/digitalBrush.js");
    const { DIGITAL_PAINT_SCHEMA } =
      await import("/src/textureDigitalPaint.js");
    const geometry = createTextureTeapotGeometry(20),
      scene = new THREE.Scene(),
      specimen = new THREE.Mesh(geometry, new THREE.MeshPhysicalMaterial()),
      camera = new THREE.PerspectiveCamera();
    scene.add(specimen);
    const doc = createTextureDocument();
    doc.layers = [
      createTextureLayer("paint", { id: "ink", channels: ["baseColor"] }),
      createTextureLayer("material", { id: "base" }),
    ];
    const lower = new THREE.Mesh(geometry, new THREE.MeshPhysicalMaterial()),
      own = new THREE.Mesh(geometry, new THREE.MeshPhysicalMaterial()),
      higher = new THREE.Mesh(geometry, new THREE.MeshPhysicalMaterial());
    for (const m of [lower, own, higher]) m.userData.textureStackPass = true;
    const order = textureLayerRenderOrder(doc, "ink");
    lower.renderOrder = order - 1;
    own.renderOrder = order;
    higher.renderOrder = order + 1;
    scene.add(lower, own, higher);
    let renders = 0,
      fails = false,
      captureOff = false;
    const errors = [];
    const renderer = {
      domElement: { dataset: {} },
      setRenderTarget() {},
      setClearColor() {},
      clear() {},
      render() {
        if (!lower.visible || own.visible || higher.visible)
          throw new Error("Invalid lower-only prefix");
        renders++;
        if (fails) throw new Error("Injected GPU failure");
      },
    };
    const controller = createDigitalPaintSurfaceController(
      {
        renderer,
        specimen,
        scene,
        camera,
        stackUV: { withState: (t, draw) => draw() },
      },
      (e) => errors.push(e),
    );
    controller.update(doc);
    controller.install(own.material, "ink");
    const uniforms = own.material.userData.digitalPaint;
    const fill = {
      id: "one",
      kind: "fill",
      region: { kind: "object", partId: "body" },
      seed: 42,
      brush: normalizeDigitalBrush({}),
      channels: { baseColor: "#ed253c" },
      points: [],
    };
    doc.layers[0].paint = {
      schema: DIGITAL_PAINT_SCHEMA,
      uvSet: AUTHORING_UV_SET,
      operations: [fill],
    };
    controller.update(doc);
    controller.prepare();
    const first = renders,
      active = uniforms.uDigitalEnabled.value;
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    own.material.onBeforeCompile(shader);
    captureOff = shader.uniforms.uDigitalCapture.value === 0;
    doc.layers[0].paint.operations[0] = { ...fill, seed: 43 };
    controller.update(doc);
    controller.prepare();
    const ownCached = renders === first;
    doc.layers[1].opacity = 80;
    controller.update(doc);
    controller.prepare();
    const lowerInvalidated = renders === first + 8;
    doc.layers[0].channels = [];
    controller.update(doc);
    const disabled = uniforms.uDigitalEnabled.value === 0;
    doc.layers[0].channels = ["baseColor"];
    controller.update(doc);
    const rebound =
      uniforms === own.material.userData.digitalPaint &&
      uniforms.uDigitalEnabled.value === 1;
    const priorGeometryAtlas = uniforms.uDigitalAtlas.value;
    let geometryAtlasDisposed = false;
    priorGeometryAtlas.addEventListener("dispose", () => {
      geometryAtlasDisposed = true;
    });
    const replacementGeometry = geometry.clone();
    specimen.geometry = replacementGeometry;
    controller.update(doc);
    const geometryInvalidated =
      geometryAtlasDisposed &&
      uniforms.uDigitalAtlas.value !== priorGeometryAtlas &&
      uniforms === own.material.userData.digitalPaint;
    fails = true;
    doc.layers[1].opacity = 70;
    controller.update(doc);
    controller.prepare();
    const restored =
      lower.visible &&
      own.visible &&
      higher.visible &&
      shader.uniforms.uDigitalCapture.value === 0 &&
      renderer.domElement.dataset.digitalPaintReady === "false";
    const old = uniforms.uDigitalAtlas.value;
    let disposed = false;
    old.addEventListener("dispose", () => {
      disposed = true;
    });
    controller.dispose();
    for (const m of [specimen, lower, own, higher]) m.material.dispose();
    replacementGeometry.dispose();
    geometry.dispose();
    return {
      first,
      active,
      captureOff,
      ownCached,
      lowerInvalidated,
      geometryInvalidated,
      disabled,
      rebound,
      restored,
      disposed,
      errors,
    };
  });
  expect(result).toMatchObject({
    first: 8,
    active: 1,
    captureOff: true,
    ownCached: true,
    lowerInvalidated: true,
    geometryInvalidated: true,
    disabled: true,
    rebound: true,
    restored: true,
    disposed: true,
  });
  expect(result.errors).toEqual([
    "Digital channel underlay: Injected GPU failure",
  ]);
});
