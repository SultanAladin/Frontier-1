import { test, expect } from "@playwright/test";
test("screen masks share projected alpha, restore their source on erase, and partially clipped/thin occluded surfaces use front depth", async ({
  page,
}) => {
  await page.goto("/src/screenPaintProjection.js");
  const r = await page.evaluate(async () => {
    const raw = await (await fetch("/src/screenPaintProjection.js")).text(),
      THREE = await import(raw.match(/from\s*"([^"]+three[^"\n]+)"/)[1]);
    const { projectScreenOperation } =
        await import("/src/screenPaintProjection.js"),
      { normalizeDigitalBrush } = await import("/src/digitalBrush.js"),
      { rasterizeMaskPaint, rasterizeMaskField } =
        await import("/src/maskPaintRaster.js"),
      { AUTHORING_UV_SET } = await import("/src/textureSurfaceModel.js");
    const projection = {
        version: 1,
        kind: "lit",
        size: [200, 200],
        matrix: new THREE.Matrix4().toArray(),
      },
      brush = normalizeDigitalBrush({
        size: 48,
        sizePressure: 0,
        hardness: 1,
        spacing: 0.1,
      }),
      op = {
        kind: "paint",
        region: { kind: "surface" },
        seed: 3,
        brush,
        points: [[100 / 256, 150 / 256, 1]],
        projection,
      };
    function geometry(near = false) {
      const g = new THREE.BufferGeometry(),
        pos = [],
        uv = [],
        part = [],
        chart = [];
      const quad = (u0, u1, z, side) => {
        const xyz = [
            [-1, -1, z(-1, -1)],
            [1, -1, z(1, -1)],
            [-1, 1, z(-1, 1)],
            [1, 1, z(1, 1)],
          ],
          coords = [
            [u0, 0.02],
            [u1, 0.02],
            [u0, 0.98],
            [u1, 0.98],
          ];
        for (const i of [0, 1, 2, 2, 1, 3]) {
          pos.push(...xyz[i]);
          uv.push(...coords[i]);
          part.push(side);
          chart.push(side === 1 ? 5 : 21);
        }
      };
      if (near) quad(0.02, 0.98, (x, y) => -y - 1, 1);
      else {
        quad(0.02, 0.98, (x) => 0.25 + 0.2 * x, 1);
        quad(1.02, 1.98, (x) => 0.2500005 + 0.2 * x, 2);
      }
      g.setAttribute("position", new THREE.Float32BufferAttribute(pos, 3));
      g.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
      g.setAttribute("uvAuthoring", g.attributes.uv);
      g.setAttribute("surfacePart", new THREE.Float32BufferAttribute(part, 1));
      g.setAttribute(
        "surfaceChart",
        new THREE.Float32BufferAttribute(chart, 1),
      );
      g.userData.authoringUVSet = AUTHORING_UV_SET;
      g.computeVertexNormals();
      return g;
    }
    const sample = (c, u, v) =>
      Array.from(
        c
          .getContext("2d")
          .getImageData(Math.floor(u * 256), Math.floor((1 - v) * 256), 1, 1)
          .data,
      );
    const near = geometry(true),
      thin = geometry(),
      visible = projectScreenOperation(op, near),
      occlusion = projectScreenOperation(
        { ...op, points: [[100 / 256, 100 / 256, 1]] },
        thin,
      ),
      selectedRear = projectScreenOperation(
        {
          ...op,
          region: { kind: "object", partId: "lid" },
          points: [[100 / 256, 100 / 256, 1]],
        },
        thin,
      );
    const record = {
        kind: "brush",
        region: op.region,
        value: 0,
        opacity: 1,
        radius: 48 / 512,
        hardness: 1,
        points: op.points.map((p) => p.slice(0, 2)),
        digital: {
          brush,
          seed: 3,
          pressure: [1],
          operation: "paint",
          projection,
        },
      },
      paint = {
        kind: "fill",
        fill: 0.7,
        enabled: true,
        strength: 100,
        paint: { operations: [record] },
      },
      erased = {
        ...paint,
        paint: {
          operations: [
            record,
            { ...record, digital: { ...record.digital, operation: "erase" } },
          ],
        },
      };
    const mask = rasterizeMaskPaint(paint, near),
      maskErase = rasterizeMaskPaint(erased, near),
      field = rasterizeMaskField(paint, near),
      restored = rasterizeMaskField(erased, near);
    const result = {
      near: sample(visible, 0.5, 0.26),
      clipped: sample(visible, 0.5, 0.75),
      front: sample(occlusion, 0.5, 0.5),
      rear: sample(occlusion, 1.5, 0.5),
      selectedRear: sample(selectedRear, 1.5, 0.5),
      mask: sample(mask, 0.5, 0.26),
      erased: sample(maskErase, 0.5, 0.26),
      field: sample(field, 0.5, 0.26),
      restored: sample(restored, 0.5, 0.26),
    };
    near.dispose();
    thin.dispose();
    return result;
  });
  expect(r.near[3]).toBeGreaterThan(240);
  expect(r.clipped[3]).toBe(0);
  expect(r.front[3]).toBeGreaterThan(240);
  expect(r.rear[3]).toBe(0);
  expect(r.selectedRear[3]).toBe(0);
  expect(r.mask[0]).toBe(0);
  expect(r.mask[3]).toBeGreaterThan(240);
  expect(r.erased[3]).toBe(0);
  expect(r.field[0]).toBeLessThan(2);
  expect(r.restored[0]).toBe(Math.round(0.7 * 255));
});
