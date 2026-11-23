import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import {
  geometricSetTwo,
  geometricCatalog,
  geometricPattern,
  tangramDissection,
} from "../src/geometricConstructions.js";
import {
  patternStarter,
  patternStarterCatalog,
  validatePattern,
  patternSVG,
  resolvePatternStarterName,
} from "../src/patternDocument.js";
import { applyStitches } from "../src/patternStitches.js";

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

test("eight new constructions, portable full paths and no duplicate geometry identities", () => {
  expect(geometricSetTwo).toHaveLength(8);
  expect(geometricCatalog).toHaveLength(30);
  const signatures = new Set();
  for (const p of geometricCatalog) {
    const raw = geometricPattern(p.name),
      d = patternStarter(p.name);
    expect(patternStarterCatalog.filter((c) => c.name === p.name)).toHaveLength(
      1,
    );
    expect(resolvePatternStarterName(p.id)).toBe(p.name);
    expect(d.layers.map((l) => l.path)).toEqual(raw.layers.map((l) => l.path));
    expect(
      d.layers.every(
        (l) => l.path.length < 100000 && !/NaN|Infinity/.test(l.path),
      ),
    ).toBe(true);
    expect(d.layers.length).toBeLessThanOrEqual(61);
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
    expect(
      validatePattern(
        applyStitches(d, { type: "Chain stitch", layout: "border" }),
      ).layers.filter((l) => l.stitchRole),
    ).toHaveLength(3);
    expect(patternSVG(d)).not.toContain("<image");
    const key = createHash("sha256")
      .update(
        JSON.stringify(
          d.layers.map(({ path, strokeWidth, fillRule }) => ({
            path,
            strokeWidth,
            fillRule,
          })),
        ),
      )
      .digest("hex");
    expect(signatures.has(key), p.name).toBe(false);
    signatures.add(key);
  }
});
test("the previously approved six constructions retain their exact documents", async () => {
  // Evaluate the prior release module, which has no imports/side effects.
  const source = execFileSync(
    "git",
    ["show", "1b9b992:src/geometricConstructions.js"],
    { encoding: "utf8" },
  );
  const previous = await import(
    "data:text/javascript;base64," + Buffer.from(source).toString("base64")
  );
  for (const p of previous.geometricCatalog)
    expect(geometricPattern(p.name)).toEqual(previous.geometricPattern(p.name));
});
test("tangram areas and every interior sample form one complete dissection", () => {
  expect(tangramDissection.map(area)).toEqual([4, 4, 2, 1, 2, 1, 2]);
  for (let y = 0; y < 60; y++)
    for (let x = 0; x < 60; x++)
      expect(
        tangramDissection.filter((p) =>
          inside(p, ((x + 0.371) * 4) / 60, ((y + 0.619) * 4) / 60),
        ),
      ).toHaveLength(1);
});
test("L-shaped courses cover the tile exactly once before their drawn joints", () => {
  const d = patternStarter("Stepped corner inlay"),
    pieces = d.layers
      .filter((l) => !l.strokeWidth)
      .flatMap((l) => polygons(l.path));
  expect(pieces.reduce((s, p) => s + area(p), 0)).toBeCloseTo(10000, 2);
  for (let y = 0; y < 70; y++)
    for (let x = 0; x < 70; x++)
      expect(
        pieces.filter((p) =>
          inside(p, ((x + 0.371) * 100) / 70, ((y + 0.619) * 100) / 70),
        ),
      ).toHaveLength(1);
});
test("hexagon-square cells are regular in physical aspect and have no overlapping interiors", () => {
  const d = patternStarter("Hexagon-square junctions");
  const hex = polygons(d.layers[0].path),
    sq = polygons(d.layers[2].path);
  for (const p of [hex[0], sq[0]]) {
    const lengths = p.map(([x, y], i) => {
      const q = p[(i + 1) % p.length];
      return Math.hypot((q[0] - x) * d.designAspect, q[1] - y);
    });
    expect(Math.max(...lengths) - Math.min(...lengths)).toBeLessThan(0.001);
  }
  const pieces = d.layers.slice(0, 3).flatMap((l) => polygons(l.path));
  let occupied = 0;
  for (let y = 0; y < 60; y++)
    for (let x = 0; x < 60; x++) {
      const count = pieces.filter((p) =>
        inside(p, ((x + 0.371) * 100) / 60, ((y + 0.619) * 100) / 60),
      ).length;
      expect(count).toBeLessThanOrEqual(1);
      occupied += count;
    }
  expect(occupied / 3600).toBeCloseTo(Math.sqrt(3) / 2, 1);
});
test("new vector repeat edges agree with the unwrapped analytic fields", async ({
  page,
}) => {
  await page.goto("/");
  const names = [
    "Hexagon-square junctions",
    "Pinwheel square tessellation",
    "Vesica net",
  ];
  const input = names.map((name) => {
    const doc = patternStarter(name);
    // These generators include overscan; compare the unwrapped construction at
    // the seam with a repeat of the exported tile, not a tile with itself.
    const raw = patternSVG({ ...doc, tileAxes: "none" })
      .replace(/viewBox="0 0 512 512"/, 'viewBox="-16 -16 544 544"')
      .replace(/width="[\d.]+" height="512"/, 'width="544" height="544"');
    return { name, bg: doc.background, raw, tile: patternSVG(doc) };
  });
  const results = await page.evaluate(async (input) => {
    async function image(svg) {
      const im = new Image();
      im.src = "data:image/svg+xml," + encodeURIComponent(svg);
      await im.decode();
      return im;
    }
    const results = [];
    for (const a of input) {
      const c = document.createElement("canvas");
      c.width = c.height = 544;
      const ctx = c.getContext("2d");
      ctx.fillStyle = a.bg;
      ctx.fillRect(0, 0, 544, 544);
      ctx.drawImage(await image(a.raw), 0, 0, 544, 544);
      const raw = ctx.getImageData(0, 0, 544, 544).data;
      ctx.fillStyle = a.bg;
      ctx.fillRect(0, 0, 544, 544);
      const tile = await image(a.tile);
      for (let y = -1; y <= 1; y++)
        for (let x = -1; x <= 1; x++)
          ctx.drawImage(tile, 16 + x * 512, 16 + y * 512, 512, 512);
      const repeat = ctx.getImageData(0, 0, 544, 544).data;
      let error = 0,
        samples = 0;
      for (let y = 4; y < 540; y++)
        for (let x = 4; x < 540; x++) {
          if (x > 28 && x < 516 && y > 28 && y < 516) continue;
          const i = (y * 544 + x) * 4;
          for (let k = 0; k < 3; k++)
            error += Math.abs(raw[i + k] - repeat[i + k]);
          samples += 3;
        }
      results.push({ name: a.name, error: error / samples });
    }
    return results;
  }, input);
  // A small Canvas/Skia antialias difference remains at coincident polygon seams.
  for (const r of results) expect(r.error, r.name).toBeLessThan(4);
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
test("Set 02 cards, editable layers, JSON, undo and actual 3D preview", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=linked-racetrack-loops",
  );
  await expect(page.getByLabel("Pattern collection")).toHaveValue(
    "Geometric constructions",
  );
  await page.getByLabel("Search patterns").fill("Set 02");
  await expect(page.locator(".pe-starters button")).toHaveCount(8);
  for (const p of geometricSetTwo) {
    await page.getByRole("button", { name: p.name, exact: true }).click();
    await expect(page.getByLabel("Pattern name")).toHaveValue(p.name);
    expect((await save(page)).construction).toBe(p.id);
  }
  await page
    .getByRole("button", { name: "Linked racetrack loops", exact: true })
    .click();
  const before = await save(page);
  await page.getByLabel("Motif color", { exact: true }).fill("#c5774b");
  expect((await save(page)).layers[0].color).toBe("#c5774b");
  await page.getByTitle("Undo", { exact: true }).click();
  expect(await save(page)).toEqual(before);
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "geometry.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(before)),
  });
  expect(await save(page)).toEqual(before);
  await page.screenshot({ path: ".playwright/v78/set-two-studio.png" });
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByLabel("Pattern preview object").selectOption("Panel");
  await page.getByLabel("Pattern preview zoom").fill("240");
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.screenshot({ path: ".playwright/v78/loops-3d.png" });
  expect(errors).toEqual([]);
});

test("loop inlays stay continuous and the four crossings alternate correctly", async ({
  page,
}) => {
  const svg = patternSVG(patternStarter("Linked racetrack loops"));
  const samples = await page.evaluate(async (svg) => {
    const im = new Image();
    im.src = "data:image/svg+xml," + encodeURIComponent(svg);
    await im.decode();
    const c = document.createElement("canvas");
    c.width = c.height = 512;
    const ctx = c.getContext("2d");
    ctx.drawImage(im, 0, 0, 512, 512);
    const sample = (x, y) =>
      Array.from(ctx.getImageData(x, y, 1, 1).data).slice(0, 3);
    return {
      line: [72, 80, 92, 104, 116, 124, 136].map((x) => sample(x, 98)),
      crossings: [
        [103, 98],
        [163, 98],
        [103, 158],
        [163, 158],
      ].map(([x, y]) => sample(x, y)),
    };
  }, svg);
  for (const pixel of samples.line) expect(pixel).toEqual([237, 223, 195]);
  expect(samples.crossings).toEqual([
    [237, 223, 195],
    [69, 126, 130],
    [69, 126, 130],
    [237, 223, 195],
  ]);
});

test("pinwheel square cells cover the plane once beneath the inset joints", () => {
  const d = patternStarter("Pinwheel square tessellation");
  const pieces = d.layers.slice(0, 3).flatMap((l, i) =>
    polygons(l.path).map((p) => {
      const cx = p.reduce((s, v) => s + v[0], 0) / p.length,
        cy = p.reduce((s, v) => s + v[1], 0) / p.length,
        t = i === 2 ? 0.9 : 0.967;
      return p.map(([x, y]) => [cx + (x - cx) / t, cy + (y - cy) / t]);
    }),
  );
  for (let y = 0; y < 80; y++)
    for (let x = 0; x < 80; x++)
      expect(
        pieces.filter((p) =>
          inside(p, ((x + 0.371) * 100) / 80, ((y + 0.619) * 100) / 80),
        ),
      ).toHaveLength(1);
});
