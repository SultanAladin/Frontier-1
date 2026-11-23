import { test, expect } from "@playwright/test";
test.use({ deviceScaleFactor: 2 });
test("screen round coverage crosses distorted charts and UDIM parts, rejects hidden faces, respects selections and keeps an unclipped XY circle", async ({
  page,
}) => {
  await page.goto("/src/screenPaintProjection.js");
  const result = await page.evaluate(async () => {
    const raw = await (await fetch("/src/screenPaintProjection.js")).text(),
      THREE = await import(raw.match(/from\s*"([^"]+three[^"\n]+)"/)[1]);
    const { projectScreenOperation } =
      await import("/src/screenPaintProjection.js");
    const { normalizeDigitalBrush } = await import("/src/digitalBrush.js");
    const { digitalBrushCursor, drawScreenBrushCursor } =
      await import("/src/digitalBrushCursor.js");
    const { AUTHORING_UV_SET } = await import("/src/textureSurfaceModel.js");
    const brush = normalizeDigitalBrush({
        size: 64,
        sizePressure: 0,
        hardness: 1,
        spacing: 0.1,
        color: "#ff1030",
      }),
      p = {
        version: 1,
        kind: "lit",
        size: [200, 200],
        matrix: new THREE.Matrix4().toArray(),
      };
    function geometry(distorted = true, hidden = false) {
      const g = new THREE.BufferGeometry(),
        positions = [],
        uvs = [],
        parts = [],
        charts = [];
      const quad = (x0, x1, y0, y1, z, u0, u1, v0, v1, part, chart) => {
        const xyz = [
            [x0, y0, z],
            [x1, y0, z],
            [x0, y1, z],
            [x1, y1, z],
          ],
          uv = [
            [u0, v0],
            [u1, v0],
            [u0, v1],
            [u1, v1],
          ];
        for (const j of [0, 1, 2, 2, 1, 3]) {
          positions.push(...xyz[j]);
          uvs.push(...uv[j]);
          parts.push(part);
          charts.push(chart);
        }
      };
      quad(
        -1,
        0,
        -1,
        1,
        0,
        distorted ? 0.05 : 0,
        distorted ? 0.85 : 1,
        distorted ? 0.1 : 0,
        distorted ? 0.4 : 1,
        1,
        5,
      );
      quad(
        0,
        1,
        -1,
        1,
        0,
        distorted ? 1.15 : 1,
        distorted ? 1.35 : 2,
        distorted ? 0.15 : 0,
        distorted ? 0.95 : 1,
        2,
        21,
      );
      if (hidden) quad(-0.5, 0.5, -0.5, 0.5, 0.5, 2.1, 2.9, 0.1, 0.9, 3, 13);
      g.setAttribute(
        "position",
        new THREE.Float32BufferAttribute(positions, 3),
      );
      g.setAttribute("uv", new THREE.Float32BufferAttribute(uvs, 2));
      g.setAttribute("uvAuthoring", g.attributes.uv);
      g.setAttribute("surfacePart", new THREE.Float32BufferAttribute(parts, 1));
      g.setAttribute(
        "surfaceChart",
        new THREE.Float32BufferAttribute(charts, 1),
      );
      g.computeVertexNormals();
      g.userData.authoringUVSet = AUTHORING_UV_SET;
      return g;
    }
    const rgba = (c, u, v) =>
      Array.from(
        c
          .getContext("2d")
          .getImageData(Math.floor(u * 256), Math.floor((1 - v) * 256), 1, 1)
          .data,
      );
    const op = {
        id: "screen",
        kind: "paint",
        region: { kind: "surface" },
        seed: 42,
        brush,
        channels: { baseColor: brush.color },
        points: [[100 / 256, 100 / 256, 1]],
        projection: p,
      },
      g = geometry(true, true),
      lit = projectScreenOperation(op, g),
      part = projectScreenOperation(
        { ...op, region: { kind: "object", partId: "body" } },
        g,
      ),
      hidden = projectScreenOperation(
        { ...op, region: { kind: "object", partId: "handle" } },
        g,
      );
    const uvProjection = {
        version: 1,
        kind: "uv",
        size: [400, 200],
        matrix: [100, 0, 0, -100, 10, 150],
      },
      flat = geometry(false),
      uv = projectScreenOperation(
        {
          ...op,
          points: [[110 / 256, 100 / 256, 1]],
          projection: uvProjection,
        },
        flat,
      );
    const overlay = document.createElement("canvas");
    document.body.append(overlay);
    const c = digitalBrushCursor({
      geometry: g,
      hit: null,
      target: { kind: "surface" },
      brush,
      channels: op.channels,
      seed: 42,
      pressure: 1,
      projection: p,
      screenPosition: [100, 100],
    });
    drawScreenBrushCursor(overlay, { width: 200, height: 200 }, c);
    const data = overlay
      .getContext("2d")
      .getImageData(0, 0, overlay.width, overlay.height).data;
    let minX = overlay.width,
      maxX = -1,
      minY = overlay.height,
      maxY = -1;
    for (let y = 0; y < overlay.height; y++)
      for (let x = 0; x < overlay.width; x++)
        if (data[(y * overlay.width + x) * 4 + 3]) {
          minX = Math.min(minX, x);
          maxX = Math.max(maxX, x);
          minY = Math.min(minY, y);
          maxY = Math.max(maxY, y);
        }
    const filteredAlpha = (c, u, v) => {
      const x = u * 256 - 0.5,
        y = (1 - v) * 256 - 0.5,
        ix = Math.floor(x),
        iy = Math.floor(y),
        fx = x - ix,
        fy = y - iy,
        data = c.getContext("2d").getImageData(ix, iy, 2, 2).data;
      return (
        data[3] * (1 - fx) * (1 - fy) +
        data[7] * fx * (1 - fy) +
        data[11] * (1 - fx) * fy +
        data[15] * fx * fy
      );
    };
    const out = {
      seamLeft: filteredAlpha(lit, 0.85, 0.25),
      seamRight: filteredAlpha(lit, 1.15, 0.55),
      left: rgba(lit, 0.8, 0.25),
      right: rgba(lit, 1.1625, 0.55),
      rear: rgba(lit, 2.5, 0.5),
      body: rgba(part, 0.8, 0.25),
      excluded: rgba(part, 1.1625, 0.55),
      hidden: rgba(hidden, 2.5, 0.5),
      uvLeft: rgba(uv, 0.94, 0.5),
      uvRight: rgba(uv, 1.06, 0.5),
      outside: rgba(lit, 0.05, 0.1),
      bbox: [minX, maxX, minY, maxY],
      label: overlay.dataset.brushCursorLabel,
      tip: c.dab,
    };
    g.dispose();
    flat.dispose();
    return out;
  });
  for (const key of ["left", "right", "body", "uvLeft", "uvRight"])
    expect(result[key][3], key).toBeGreaterThan(240);
  for (const key of ["rear", "hidden", "excluded", "outside"])
    expect(result[key][3], key).toBe(0);
  const [x0, x1, y0, y1] = result.bbox;
  expect(x1 - x0).toBe(y1 - y0);
  expect((x0 + x1 + 1) / 2).toBeCloseTo(200, 0);
  expect((y0 + y1 + 1) / 2).toBeCloseTo(200, 0);
  expect(x1 - x0).toBeGreaterThan(120);
  expect(result.seamLeft).toBeGreaterThan(254);
  expect(result.seamRight).toBeGreaterThan(254);
  expect(result.label).toBe("Paint · 64.0 px");
  expect(result.tip.radius).toBe(32);
});
