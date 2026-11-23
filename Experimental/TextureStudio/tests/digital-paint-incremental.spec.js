import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 60000 });

// The live stroke path must stay byte-identical to the one-shot full-history
// replay while depositing only new dabs. This mirrors
// scripts/verify-stroke-incremental.mjs (Node) for browser canvases.
test("incremental atlas session matches one-shot replay across a growing stroke", async ({
  page,
}) => {
  await page.goto("/src/digitalBrush.js");
  const result = await page.evaluate(async () => {
    const { createTextureLayer } = await import("/src/textureDocument.js");
    const { createTextureTeapotGeometry, AUTHORING_UV_SET } =
      await import("/src/textureSurfaceModel.js");
    const { normalizeDigitalBrush, digitalBrushDabs, digitalBrushLastDab } =
      await import("/src/digitalBrush.js");
    const { createDigitalPaintAtlas, createDigitalPaintAtlasSession } =
      await import("/src/digitalPaintRaster.js");
    const { DIGITAL_PAINT_SCHEMA } =
      await import("/src/textureDigitalPaint.js");
    const geometry = createTextureTeapotGeometry(20),
      brush = normalizeDigitalBrush({
        size: 24,
        hardness: 0.85,
        sizePressure: 0.4,
        scatter: 0.15,
        dabHue: 0.08,
        color: "#b5651d",
      });
    const fill = {
      id: "fill-1",
      kind: "fill",
      region: { kind: "object", partId: "lid" },
      brush,
      channels: { baseColor: "#808080", roughness: 0.2, metalness: 0.4 },
      points: [],
      seed: 42,
    };
    const points = (n) =>
      Array.from({ length: n }, (_, i) => [
        1.04 + i * 0.006,
        0.52 + Math.sin(i / 6) * 0.035,
        0.35 + 0.6 * Math.abs(Math.sin(i / 9)),
      ]);
    const stroke = (pts, extra = {}) => ({
      id: "stroke-1",
      kind: "paint",
      region: { kind: "island", partId: "lid", id: 25 },
      brush,
      channels: { baseColor: "#b5651d", roughness: 0.65, metalness: 0.1 },
      points: pts,
      seed: 777,
      ...extra,
    });
    const layerWith = (operations) =>
      createTextureLayer("paint", {
        channels: ["baseColor", "roughness", "metalness"],
        paint: {
          schema: DIGITAL_PAINT_SCHEMA,
          uvSet: AUTHORING_UV_SET,
          operations,
        },
      });
    const session = createDigitalPaintAtlasSession(geometry);
    const frames = [];
    const check = (label, operations) => {
      const layer = layerWith(operations),
        incremental = session.update(layer),
        reference = createDigitalPaintAtlas(layer, geometry),
        a = incremental.texture.image.data,
        b = reference.texture.image.data;
      let identical = a.length === b.length;
      for (let i = 0; identical && i < a.length; i++)
        if (a[i] !== b[i]) identical = false;
      frames.push({ label, identical });
      reference.texture.dispose();
    };
    check("fill", [fill]);
    for (let n = 2; n <= 60; n += 2)
      check(`grow ${n}`, [fill, stroke(points(n))]);
    const replaced = points(60);
    replaced[replaced.length - 1] = [1.395, 0.545, 0.9];
    check("endpoint replaced", [fill, stroke(replaced)]);
    check("undo", [fill]);
    const lastMatches =
      JSON.stringify(
        digitalBrushLastDab(points(40), brush, 777, 256, "#b5651d", true),
      ) ===
      JSON.stringify(
        digitalBrushDabs(points(40), brush, 777, 256, "#b5651d", true).at(-1),
      );
    session.dispose();
    geometry.dispose();
    return { frames, lastMatches };
  });
  for (const frame of result.frames)
    expect(frame.identical, frame.label).toBe(true);
  expect(result.lastMatches).toBe(true);
});
