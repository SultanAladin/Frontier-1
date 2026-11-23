import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdir } from "node:fs/promises";
import {
  geometricCatalog,
  geometricPattern,
  geometricSetFour,
  poincareGeodesic,
} from "../src/geometricConstructions.js";
import {
  patternStarter,
  patternStarterCatalog,
  patternSVG,
  resolvePatternStarterName,
  validatePattern,
} from "../src/patternDocument.js";

const polygons = (path) =>
  (path.match(/M[^Z]+Z/g) || []).map((s) =>
    [...s.matchAll(/[ML]([-\d.]+) ([-\d.]+)/g)].map((m) => [+m[1], +m[2]]),
  );
const subpaths = (path) =>
  (path.match(/M[^M]+/g) || []).map((s) =>
    [...s.matchAll(/[ML]([-\d.]+) ([-\d.]+)/g)].map((m) => [+m[1], +m[2]]),
  );
const area = (p) =>
  Math.abs(
    p.reduce((sum, [x, y], i) => {
      const q = p[(i + 1) % p.length];
      return sum + x * q[1] - y * q[0];
    }, 0) / 2,
  );
const inside = (p, x, y) => {
  let result = false;
  for (let i = 0, j = p.length - 1; i < p.length; j = i++) {
    const a = p[i],
      b = p[j];
    if (
      a[1] > y !== b[1] > y &&
      x < ((b[0] - a[0]) * (y - a[1])) / (b[1] - a[1]) + a[0]
    )
      result = !result;
  }
  return result;
};
const pathSignature = (doc) =>
  createHash("sha256")
    .update(
      JSON.stringify(
        doc.layers.map(({ path, strokeWidth, fillRule }) => ({
          path,
          strokeWidth,
          fillRule,
        })),
      ),
    )
    .digest("hex");

async function save(page) {
  const pending = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save document", exact: true }).click();
  const chunks = [];
  for await (const chunk of await (await pending).createReadStream())
    chunks.push(chunk);
  return JSON.parse(Buffer.concat(chunks));
}

test("Set 04 adds eight distinct, editable vector constructions", () => {
  expect(geometricSetFour).toHaveLength(8);
  expect(geometricCatalog).toHaveLength(30);
  expect(new Set(geometricCatalog.map((p) => p.id)).size).toBe(30);
  const signatures = new Set(
    geometricCatalog
      .filter((spec) => spec.set !== 4)
      .map((spec) => pathSignature(patternStarter(spec.name))),
  );
  for (const spec of geometricSetFour) {
    const raw = geometricPattern(spec.id),
      doc = patternStarter(spec.name);
    expect(patternStarterCatalog.filter((p) => p.name === spec.name)).toHaveLength(
      1,
    );
    expect(resolvePatternStarterName(spec.id)).toBe(spec.name);
    expect(doc.construction).toBe(spec.id);
    expect(doc.layers.map((layer) => layer.path)).toEqual(
      raw.layers.map((layer) => layer.path),
    );
    expect(signatures.has(pathSignature(doc)), spec.name).toBe(false);
    signatures.add(pathSignature(doc));
    expect(doc.layers.length).toBeGreaterThan(0);
    expect(doc.layers.length).toBeLessThanOrEqual(64);
    expect(
      doc.layers.every(
        (layer) =>
          layer.path.length < 100000 && !/NaN|Infinity/.test(layer.path),
      ),
    ).toBe(true);
    expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    expect(patternSVG(doc)).not.toContain("<image");
  }
});

test("all 22 previously published geometric constructions remain byte-for-byte identical", async () => {
  const source = execFileSync(
    "git",
    ["show", "613333d:src/geometricConstructions.js"],
    { encoding: "utf8" },
  );
  const previous = await import(
    "data:text/javascript;base64," + Buffer.from(source).toString("base64")
  );
  expect(previous.geometricCatalog).toHaveLength(22);
  for (const spec of previous.geometricCatalog)
    expect(geometricPattern(spec.id)).toEqual(previous.geometricPattern(spec.id));
});

test("the two flattened hexagon nets have unique centers and a 128-unit repeat", () => {
  const doc = patternStarter("Dual-hexagon overlay");
  for (const layer of doc.layers.slice(0, 4)) {
    const cells = polygons(layer.path);
    expect(cells).toHaveLength(32);
    expect(cells.every((cell) => cell.length === 6)).toBe(true);
    expect(cells.flat().every(([x, y]) => Number.isFinite(x) && Number.isFinite(y))).toBe(true);
  }
  for (const layer of doc.layers.slice(0, 2)) {
    const centers = polygons(layer.path).map((cell) => {
      expect(cell).toHaveLength(6);
      return [
        (cell.reduce((sum, p) => sum + p[0], 0) / cell.length) * 5.12,
        (cell.reduce((sum, p) => sum + p[1], 0) / cell.length) * 5.12,
      ];
    });
    const key = ([x, y]) => `${((x % 512) + 512) % 512},${((y % 512) + 512) % 512}`;
    const lookup = new Set(centers.map(key));
    for (const center of centers) {
      expect(lookup.has(key([center[0] + 128, center[1]]))).toBe(true);
      expect(lookup.has(key([center[0], center[1] + 128]))).toBe(true);
    }
  }
});

test("Poincaré arcs are orthogonal-circle geodesics contained in their disk", () => {
  const center = { x: 7, y: -3 },
    radius = 10,
    samples = 48;
  for (const [a, b] of [
    [0.1, 1.2],
    [0.5, 2.1],
    [2.3, 4.5],
    [5.8, 1.1],
    [5.8, 7.1],
  ]) {
    const arc = poincareGeodesic(center, radius, a, b, samples);
    expect(arc).toHaveLength(samples + 1);
    expect(Math.hypot(arc[0][0] - center.x, arc[0][1] - center.y)).toBeCloseTo(
      radius,
      10,
    );
    expect(Math.hypot(arc.at(-1)[0] - center.x, arc.at(-1)[1] - center.y)).toBeCloseTo(
      radius,
      10,
    );
    expect(
      Math.max(...arc.map(([x, y]) => Math.hypot(x - center.x, y - center.y))),
    ).toBeLessThanOrEqual(radius + 1e-8);
    const delta = ((b - a + Math.PI * 3) % (2 * Math.PI)) - Math.PI,
      mid = a + delta / 2,
      distance = radius / Math.cos(delta / 2),
      circleCenter = [
        center.x + distance * Math.cos(mid),
        center.y + distance * Math.sin(mid),
      ],
      circleRadius = radius * Math.abs(Math.tan(delta / 2));
    for (const [x, y] of arc)
      expect(Math.hypot(x - circleCenter[0], y - circleCenter[1])).toBeCloseTo(
        circleRadius,
        8,
      );
  }
  const diameter = poincareGeodesic({ x: 0, y: 0 }, 1, 0, Math.PI, 16);
  expect(diameter[8][0]).toBeCloseTo(0, 12);
  expect(diameter[8][1]).toBeCloseTo(0, 12);
  expect(poincareGeodesic({ x: 0, y: 0 }, 1, 0, Math.PI * 3 / 4)).toHaveLength(33);
});

test("the periodic Voronoi cells cover one tile exactly once, including repeat seams", () => {
  const doc = patternStarter("Periodic Voronoi mosaic"),
    cells = doc.layers.slice(0, 4).flatMap((layer) => polygons(layer.path));
  expect(cells).toHaveLength(16);
  expect(cells.reduce((sum, cell) => sum + area(cell), 0)).toBeCloseTo(10000, 1);
  for (let y = 0; y < 64; y++)
    for (let x = 0; x < 64; x++) {
      const px = ((x + 0.371) * 100) / 64,
        py = ((y + 0.619) * 100) / 64,
        hits = cells.flatMap((cell) =>
          [-100, 0, 100].flatMap((dx) =>
            [-100, 0, 100].map((dy) => inside(cell, px + dx, py + dy)),
          ),
        );
      expect(hits.filter(Boolean)).toHaveLength(1);
    }
});

test("Koch islands contain 192 equal segments at exactly three inflation levels", () => {
  const doc = patternStarter("Koch snowflake field"),
    flakes = polygons(doc.layers[2].path),
    expected = (65 * Math.sqrt(3)) / 27;
  expect(flakes).toHaveLength(4);
  for (const flake of flakes) {
    expect(flake).toHaveLength(192);
    const lengths = flake.map(([x, y], i) => {
      const next = flake[(i + 1) % flake.length];
      return Math.hypot((next[0] - x) * 5.12, (next[1] - y) * 5.12);
    });
    expect(Math.min(...lengths)).toBeCloseTo(expected, 2);
    expect(Math.max(...lengths)).toBeCloseTo(expected, 2);
  }
});

test("Archimedean counterspirals keep the linear radial law and finite orbit extent", () => {
  const doc = patternStarter("Archimedean counterspirals"),
    arms = subpaths(doc.layers[1].path),
    centers = [
      [128, 128],
      [384, 128],
      [128, 384],
      [384, 384],
    ];
  expect(arms).toHaveLength(8);
  arms.forEach((path, i) => {
    expect(path).toHaveLength(241);
    const center = centers[Math.floor(i / 2)],
      radii = path.map(([x, y]) => Math.hypot(x * 5.12 - center[0], y * 5.12 - center[1]));
    expect(radii[0]).toBeCloseTo(5, 2);
    expect(radii.at(-1)).toBeCloseTo(5 + 20 * Math.PI, 2);
    expect(radii.every((r, j) => j === 0 || r >= radii[j - 1] - 0.01)).toBe(true);
  });
});

test("Lamé contours satisfy their fourth- and sixth-power boundary equations", () => {
  const doc = patternStarter("Superellipse contour field"),
    contour = polygons(doc.layers[0].path)[0],
    rotated = polygons(doc.layers[1].path)[0];
  expect(contour).toHaveLength(257);
  expect(rotated).toHaveLength(257);
  for (const [x, y] of contour) {
    const u = (x * 5.12 - 128) / 68,
      v = (y * 5.12 - 128) / (68 * 0.82);
    expect(Math.abs(Math.abs(u) ** 4 + Math.abs(v) ** 4 - 1)).toBeLessThan(0.001);
  }
  for (const [x, y] of rotated) {
    const dx = x * 5.12 - 128,
      dy = y * 5.12 - 128,
      u = (dx * Math.cos(Math.PI / 4) + dy * Math.sin(Math.PI / 4)) / 59,
      v = (-dx * Math.sin(Math.PI / 4) + dy * Math.cos(Math.PI / 4)) / (59 * 0.82);
    expect(Math.abs(Math.abs(u) ** 6 + Math.abs(v) ** 6 - 1)).toBeLessThan(0.001);
  }
});

test("Bernoulli curves close at their two algebraic double points", () => {
  const doc = patternStarter("Bernoulli lemniscate field"),
    curves = polygons(doc.layers[1].path),
    centers = [
      [128, 128],
      [384, 128],
      [128, 384],
      [384, 384],
    ];
  expect(curves).toHaveLength(4);
  for (const [curveIndex, curve] of curves.entries()) {
    expect(curve).toHaveLength(513);
    expect(curve[0][0]).toBeCloseTo(curve.at(-1)[0], 4);
    expect(curve[0][1]).toBeCloseTo(curve.at(-1)[1], 4);
    const [cx, cy] = centers[curveIndex];
    for (const index of [128, 384]) {
      expect(curve[index][0] * 5.12).toBeCloseTo(cx, 3);
      expect(curve[index][1] * 5.12).toBeCloseTo(cy, 3);
    }
  }
});

test("the (2,3) torus projection is closed and its depth-colored branches are ordered", () => {
  const doc = patternStarter("Torus-knot projection"),
    knot = polygons(doc.layers[0].path);
  expect(knot).toHaveLength(4);
  for (const path of knot) {
    expect(path).toHaveLength(481);
    expect(path[0][0]).toBeCloseTo(path.at(-1)[0], 4);
    expect(path[0][1]).toBeCloseTo(path.at(-1)[1], 4);
  }
  expect(doc.layers[1].name).toContain("Negative-depth");
  expect(doc.layers[2].name).toContain("Positive-depth");
  expect((doc.layers[1].path.match(/M/g) || []).length).toBe(12);
  expect((doc.layers[2].path.match(/M/g) || []).length).toBe(12);
});

test("Set 04 cards search, save, edit, undo and accept a JSON round-trip", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=dual-hexagon-overlay",
  );
  await expect(page.getByLabel("Pattern collection")).toHaveValue(
    "Geometric constructions",
  );
  await page.getByLabel("Search patterns").fill("Set 04");
  await expect(page.locator(".pe-starters button")).toHaveCount(8);
  for (const spec of geometricSetFour) {
    await page.getByRole("button", { name: spec.name, exact: true }).click();
    await expect(page.getByLabel("Pattern name")).toHaveValue(spec.name);
    expect((await save(page)).construction).toBe(spec.id);
  }
  await page
    .getByRole("button", { name: "Periodic Voronoi mosaic", exact: true })
    .click();
  const before = await save(page);
  await page.getByLabel("Motif color", { exact: true }).fill("#c5774b");
  expect((await save(page)).layers[0].color).toBe("#c5774b");
  await page.getByTitle("Undo", { exact: true }).click();
  expect(await save(page)).toEqual(before);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "geometry-set-four.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(before)),
  });
  expect(await save(page)).toEqual(before);
  await mkdir(".playwright/v710", { recursive: true });
  await page.screenshot({ path: ".playwright/v710/set-four-studio.png" });
  expect(errors).toEqual([]);
});

test("Poincaré geodesic construction renders as a live 3D material", async ({
  page,
}) => {
  const webgl = await page.evaluate(() => {
    const canvas = document.createElement("canvas");
    return Boolean(canvas.getContext("webgl2") || canvas.getContext("webgl"));
  });
  test.skip(!webgl, "A real material preview requires WebGL.");
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=poincare-geodesic-lattice",
  );
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByLabel("Pattern preview object").selectOption("Panel");
  await page.getByLabel("Pattern preview zoom").fill("240");
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await mkdir(".playwright/v710", { recursive: true });
  await page.screenshot({ path: ".playwright/v710/geodesic-3d.png" });
});
