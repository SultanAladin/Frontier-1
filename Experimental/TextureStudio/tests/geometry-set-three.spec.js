import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import {
  geometricCatalog,
  geometricPattern,
  geometricSetThree,
  descartesGapCircle,
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
const area = (p) =>
  Math.abs(
    p.reduce((v, [x, y], i) => {
      const q = p[(i + 1) % p.length];
      return v + x * q[1] - y * q[0];
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

test("Set 03 adds eight portable, distinct geometric constructions", () => {
  expect(geometricSetThree).toHaveLength(8);
  expect(geometricCatalog).toHaveLength(30);
  const ids = new Set(geometricCatalog.map((p) => p.id)),
    signatures = new Set();
  expect(ids.size).toBe(30);
  for (const spec of geometricSetThree) {
    const raw = geometricPattern(spec.id),
      doc = patternStarter(spec.name);
    expect(patternStarterCatalog.filter((p) => p.name === spec.name)).toHaveLength(
      1,
    );
    expect(resolvePatternStarterName(spec.id)).toBe(spec.name);
    expect(doc.construction).toBe(spec.id);
    expect(doc.layers.map((l) => l.path)).toEqual(raw.layers.map((l) => l.path));
    const signature = createHash("sha256")
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
    expect(signatures.has(signature), spec.name).toBe(false);
    signatures.add(signature);
    expect(doc.layers.length).toBeGreaterThan(0);
    expect(doc.layers.length).toBeLessThanOrEqual(64);
    expect(
      doc.layers.every(
        (l) => l.path.length < 100000 && !/NaN|Infinity/.test(l.path),
      ),
    ).toBe(true);
    expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    expect(patternSVG(doc)).not.toContain("<image");
  }
});

test("all fourteen previously approved geometric documents remain exact", async () => {
  const source = execFileSync(
    "git",
    ["show", "758623c:src/geometricConstructions.js"],
    { encoding: "utf8" },
  );
  const previous = await import(
    "data:text/javascript;base64," + Buffer.from(source).toString("base64")
  );
  for (const spec of previous.geometricCatalog)
    expect(geometricPattern(spec.id)).toEqual(previous.geometricPattern(spec.id));
});

test("elongated squares and equilateral triangles tile one rectangle without gaps", () => {
  const doc = patternStarter("Elongated triangle lattice"),
    aspect = doc.designAspect,
    square = polygons(doc.layers[0].path)[0],
    up = polygons(doc.layers[2].path)[0],
    down = polygons(doc.layers[3].path)[0];
  for (const p of [square, up, down]) {
    const edges = p.map(([x, y], i) => {
      const q = p[(i + 1) % p.length];
      return Math.hypot((q[0] - x) * aspect, q[1] - y);
    });
    expect(Math.max(...edges) - Math.min(...edges)).toBeLessThan(0.0001);
  }
  const baseCells = doc.layers.slice(0, 4).flatMap((l) => polygons(l.path));
  const cells = baseCells.flatMap((p) =>
    [-100, 0, 100].flatMap((dx) =>
      [-100, 0, 100].map((dy) => p.map(([x, y]) => [x + dx, y + dy])),
    ),
  );
  for (let y = 0; y < 64; y++)
    for (let x = 0; x < 64; x++)
      expect(
        cells.filter((p) =>
          inside(p, ((x + 0.371) * 100) / 64, ((y + 0.619) * 100) / 64),
        ),
      ).toHaveLength(1);
});

test("Descartes inserts are tangent to all three parent circles through two recursions", () => {
  const root = [
    { x: 0, y: 0, r: 1 },
    { x: 2, y: 0, r: 1 },
    { x: 1, y: Math.sqrt(3), r: 1 },
  ];
  const close = (parents, child) =>
    parents.forEach((p) =>
      expect(Math.hypot(child.x - p.x, child.y - p.y)).toBeCloseTo(
        child.r + p.r,
        10,
      ),
    );
  const first = descartesGapCircle(root);
  expect(first.r).toBeCloseTo(2 / Math.sqrt(3) - 1, 12);
  close(root, first);
  const second = descartesGapCircle([root[0], root[1], first]);
  close([root[0], root[1], first], second);
  const doc = patternStarter("Descartes circle packing");
  expect(doc.layers).toHaveLength(8);
  expect([doc.layers[2], doc.layers[4], doc.layers[6]].map((l) =>
    (l.path.match(/M/g) || []).length,
  )).toEqual([4, 12, 36]);
  expect(Math.max(...doc.layers.map((l) => l.path.length))).toBeLessThan(100000);
  const [, rx, ry] = doc.layers[0].path
    .match(/a([-\d.]+) ([-\d.]+) 0 1 0/)
    .map(Number);
  expect((rx * doc.designAspect) / ry).toBeCloseTo(1, 4);
  expect((doc.layers[0].path.match(/a/g) || []).length).toBe(4);
});

test("Pythagorean branch squares and right-angle joints remain disjoint", () => {
  const doc = patternStarter("Pythagorean branch lattice"),
    cells = doc.layers.slice(0, 9).flatMap((l) => polygons(l.path));
  expect(cells).toHaveLength(46);
  const rightTriangle = polygons(doc.layers[5].path)[0],
    triangleSides = rightTriangle
      .map(([x, y], i) => {
        const q = rightTriangle[(i + 1) % rightTriangle.length];
        return Math.hypot(q[0] - x, q[1] - y);
      })
      .sort((a, b) => a - b);
  expect(triangleSides[0]).toBeCloseTo(triangleSides[1], 3);
  expect(triangleSides[2] / triangleSides[0]).toBeCloseTo(Math.SQRT2, 3);
  expect(cells.reduce((s, p) => s + area(p), 0)).toBeCloseTo(
    (84 * 84 * (5 + 1)) / (5.12 * 5.12),
    1,
  );
  for (let y = 0; y < 80; y++)
    for (let x = 0; x < 80; x++)
      expect(
        cells.filter((p) =>
          inside(p, ((x + 0.371) * 100) / 80, ((y + 0.619) * 100) / 80),
        ).length,
      ).toBeLessThanOrEqual(1);
});

test("six Fibonacci squares partition four exact 13-by-8 rectangles", () => {
  const doc = patternStarter("Fibonacci square spiral"),
    squares = doc.layers.slice(0, 4).flatMap((l) => polygons(l.path));
  expect(squares).toHaveLength(24);
  const sideUnits = squares
    .map((p) =>
      Math.round(
        (Math.hypot(p[1][0] - p[0][0], p[1][1] - p[0][1]) * 5.12) / 18.5,
      ),
    )
    .sort((a, b) => a - b);
  const expectedSides = [1, 1, 2, 3, 5, 8]
    .flatMap((n) => Array(4).fill(n))
    .sort((a, b) => a - b);
  expect(sideUnits).toEqual(expectedSides);
  expect(squares.reduce((s, p) => s + area(p), 0)).toBeCloseTo(
    (4 * 13 * 8 * 18.5 * 18.5) / (5.12 * 5.12),
    1,
  );
  for (const cy of [128, 384])
    for (const cx of [128, 384]) {
      const left = (cx - (13 * 18.5) / 2) / 5.12,
        right = (cx + (13 * 18.5) / 2) / 5.12,
        top = (cy - (8 * 18.5) / 2) / 5.12,
        bottom = (cy + (8 * 18.5) / 2) / 5.12;
      for (let y = 0; y < 30; y++)
        for (let x = 0; x < 40; x++) {
          const px = left + ((x + 0.371) * (right - left)) / 40,
            py = top + ((y + 0.619) * (bottom - top)) / 30;
          expect(squares.filter((p) => inside(p, px, py)).length).toBe(1);
        }
    }
});

test("rhodonea and epicycloid profiles are closed analytic paths, not sampled images", () => {
  const rose = patternStarter("Rhodonea rose lattice"),
    gear = patternStarter("Epicycloid gear lattice"),
    rosePaths = polygons(rose.layers[0].path),
    gearPaths = polygons(gear.layers[0].path);
  expect(rosePaths).toHaveLength(4);
  expect(gearPaths).toHaveLength(4);
  for (const p of [...rosePaths, ...gearPaths]) {
    expect(p.length).toBeGreaterThan(500);
    expect(p[0][0]).toBeCloseTo(p.at(-1)[0], 4);
    expect(p[0][1]).toBeCloseTo(p.at(-1)[1], 4);
  }
  expect(rose.layers.every((l) => l.path.length < 100000)).toBe(true);
  expect(gear.layers.every((l) => l.path.length < 100000)).toBe(true);
});

test("harmonic wave families return to the same position at every repeat edge", () => {
  const doc = patternStarter("Harmonic wave lattice"),
    subpaths = (path) =>
      (path.match(/M[^M]+/g) || []).map((s) =>
        [...s.matchAll(/[ML]([-\d.]+) ([-\d.]+)/g)].map((m) => [+m[1], +m[2]]),
      );
  for (const layer of doc.layers) {
    const curves = subpaths(layer.path);
    expect(curves).toHaveLength(7);
    for (const curve of curves) {
      if (layer.name.startsWith("Horizontal")) {
        expect(curve.at(-1)[0] - curve[0][0]).toBeCloseTo(100, 4);
        expect(curve[0][1]).toBeCloseTo(curve.at(-1)[1], 4);
      } else {
        expect(curve[0][0]).toBeCloseTo(curve.at(-1)[0], 4);
        expect(curve.at(-1)[1] - curve[0][1]).toBeCloseTo(100, 4);
      }
    }
  }
});

test("harmonic-wave SVG repeat matches the continuous analytic field at all tile edges", async ({
  page,
}) => {
  const doc = patternStarter("Harmonic wave lattice"),
    period = 128,
    amplitude = 18,
    step = 4,
    curve = (points) =>
      points
        .map(([x, y], i) => `${i ? "L" : "M"}${x.toFixed(4)} ${y.toFixed(4)}`)
        .join(""),
    horizontal = [],
    vertical = [];
  for (let row = -1; row <= 5; row++) {
    const pts = [];
    for (let x = -32; x <= 544; x += step)
      pts.push([
        x,
        row * period + amplitude * Math.sin((2 * Math.PI * x) / period + (row * Math.PI) / 2),
      ]);
    horizontal.push(curve(pts));
  }
  for (let col = -1; col <= 5; col++) {
    const pts = [];
    for (let y = -32; y <= 544; y += step)
      pts.push([
        col * period + amplitude * Math.sin((2 * Math.PI * y) / period + (col * Math.PI) / 2),
        y,
      ]);
    vertical.push(curve(pts));
  }
  const raw = `<svg xmlns="http://www.w3.org/2000/svg" width="544" height="544" viewBox="-16 -16 544 544" preserveAspectRatio="none"><rect x="-16" y="-16" width="544" height="544" fill="${doc.background}"/><path d="${horizontal.join("")}" fill="none" stroke="#457e82" stroke-width="1.35" stroke-linecap="round"/><path d="${vertical.join("")}" fill="none" stroke="#d6b879" stroke-width="1.35" stroke-linecap="round"/></svg>`;
  const error = await page.evaluate(
    async ({ raw, tile, background }) => {
      const load = async (source) => {
        const image = new Image();
        image.src = "data:image/svg+xml," + encodeURIComponent(source);
        await image.decode();
        return image;
      };
      const rawImage = await load(raw),
        tileImage = await load(tile),
        canvas = document.createElement("canvas");
      canvas.width = canvas.height = 544;
      const ctx = canvas.getContext("2d");
      ctx.drawImage(rawImage, 0, 0, 544, 544);
      const expected = ctx.getImageData(0, 0, 544, 544).data;
      ctx.fillStyle = background;
      ctx.fillRect(0, 0, 544, 544);
      for (let y = -1; y <= 1; y++)
        for (let x = -1; x <= 1; x++)
          ctx.drawImage(tileImage, 16 + x * 512, 16 + y * 512, 512, 512);
      const repeated = ctx.getImageData(0, 0, 544, 544).data;
      let sum = 0,
        count = 0;
      for (let y = 4; y < 540; y++)
        for (let x = 4; x < 540; x++) {
          if (x > 44 && x < 500 && y > 44 && y < 500) continue;
          const i = (y * 544 + x) * 4;
          for (let channel = 0; channel < 3; channel++) {
            sum += Math.abs(expected[i + channel] - repeated[i + channel]);
            count++;
          }
        }
      return sum / count;
    },
    { raw, tile: patternSVG(doc), background: doc.background },
  );
  expect(error).toBeLessThan(4);
});

async function save(page) {
  const pending = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await pending).createReadStream())
    chunks.push(c);
  return JSON.parse(Buffer.concat(chunks));
}

test("Set 03 cards save/load, edit, undo and JSON import", async ({ page }) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=elongated-triangle-lattice",
  );
  await expect(page.getByLabel("Pattern collection")).toHaveValue(
    "Geometric constructions",
  );
  await page.getByLabel("Search patterns").fill("Set 03");
  await expect(page.locator(".pe-starters button")).toHaveCount(8);
  for (const spec of geometricSetThree) {
    await page.getByRole("button", { name: spec.name, exact: true }).click();
    await expect(page.getByLabel("Pattern name")).toHaveValue(spec.name);
    expect((await save(page)).construction).toBe(spec.id);
  }
  await page
    .getByRole("button", { name: "Rhodonea rose lattice", exact: true })
    .click();
  const before = await save(page);
  await page.getByLabel("Motif color", { exact: true }).fill("#c5774b");
  expect((await save(page)).layers[0].color).toBe("#c5774b");
  await page.getByTitle("Undo", { exact: true }).click();
  expect(await save(page)).toEqual(before);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "geometry-set-three.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(before)),
  });
  expect(await save(page)).toEqual(before);
  await page.screenshot({ path: ".playwright/v79/set-three-studio.png" });
  expect(errors).toEqual([]);
});

test("Rhodonea renders as a ready live material on the 3D panel", async ({
  page,
}) => {
  const webgl = await page.evaluate(() => {
    const canvas = document.createElement("canvas");
    return Boolean(canvas.getContext("webgl2") || canvas.getContext("webgl"));
  });
  test.skip(
    !webgl,
    "This headless browser does not expose WebGL, so a real 3D render cannot run here.",
  );
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=rhodonea-rose-lattice",
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
  await page.screenshot({ path: ".playwright/v79/rose-3d.png" });
});
