import { test, expect } from "@playwright/test";
import {
  patternStarter,
  patternStarterCatalog,
  resolvePatternStarterName,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import {
  stitchTypes,
  stitchUnit,
  applyStitches,
} from "../src/patternStitches.js";
import {
  weaveFamilies,
  weaveVariants,
  weavePeriod,
  weaveDraft,
  weaveLayers,
  applyWeave,
} from "../src/patternWeaves.js";
import { replacePatternColor } from "../src/patternLibrary.js";
import { ornamentalCatalog } from "../src/ornamentalConstructions.js";
import { rugDesignCatalog } from "../src/rugDesigns.js";
import { referenceDesignCatalog } from "../src/referencePatterns.js";
import { collectionPatterns } from "../src/patternCollections.js";
import {
  geometricCatalog,
  geometricPattern,
} from "../src/geometricConstructions.js";

test("all rejected collections and named carpets are absent from cards and deep links", () => {
  const retired = [
    ...ornamentalCatalog,
    ...rugDesignCatalog,
    ...referenceDesignCatalog,
    ...collectionPatterns.filter((p) => p.name !== "Diamond Dissolve"),
    { name: "Medallion rug" },
    { name: "African Diamond Carpet" },
    { name: "Islamic Medallion Carpet" },
    { name: "Golden Cube Fade" },
  ];
  for (const { name } of retired) {
    expect(
      patternStarterCatalog.some((p) => p.name === name),
      name,
    ).toBe(false);
    expect(
      resolvePatternStarterName(name.toLowerCase().replaceAll(" ", "-")),
      name,
    ).toBeUndefined();
  }
  expect(
    patternStarterCatalog.filter((p) => p.group === "Stitch patterns"),
  ).toHaveLength(10);
  expect(
    patternStarterCatalog.filter((p) => p.group === "Fabric weaves"),
  ).toHaveLength(10);
});
test("stitch and weave palette-qualified legacy inputs are a single color construction, not variants", () => {
  for (const name of [...stitchTypes, ...weaveFamilies]) {
    const baseline = patternStarter(name);
    for (const palette of ["Indigo", "Earth", "Studio", "Mulberry"])
      expect(patternStarter(name + " - " + palette)).toEqual(baseline);
    const thread = baseline.layers.filter(
      (l) => l.weaveRole || ["thread", "highlight"].includes(l.stitchRole),
    );
    expect(new Set(thread.map((l) => l.color)).size, name).toBe(1);
    const recolored = replacePatternColor(baseline, thread[0].color, "#d85436");
    expect(
      recolored.layers
        .filter(
          (l) => l.weaveRole || ["thread", "highlight"].includes(l.stitchRole),
        )
        .every((l) => l.color === "#d85436"),
    ).toBe(true);
  }
});
test("needle entries are independent of bends and upper passes follow actual thread paths", () => {
  const chain = stitchUnit("Chain stitch");
  expect(chain.holes).toEqual([[-0.5, 0]]);
  expect(
    chain.body.filter((c) => c[0] === "C").map((c) => c.slice(-2)),
  ).toEqual([
    [0.57, 0],
    [-0.5, 0],
  ]);
  expect(
    stitchUnit("Backstitch").body.filter((c) => c[0] === "M"),
  ).toHaveLength(1);
  expect(stitchUnit("Cross stitch").holes).toEqual([
    [-0.32, -0.32],
    [0.32, 0.32],
    [-0.32, 0.32],
    [0.32, -0.32],
  ]);
  expect(stitchUnit("Blanket stitch").body.some((c) => c[0] === "Q")).toBe(
    true,
  );
  expect(stitchUnit("Blanket stitch").holes).toEqual([
    [-0.5, -0.32],
    [-0.5, 0.29],
  ]);
  expect(stitchUnit("Herringbone stitch").holes).toEqual([
    [-0.5, 0.3],
    [0.25, -0.3],
    [0, -0.3],
    [0.75, 0.3],
  ]);
  expect(stitchUnit("Feather stitch").holes.some(([, y]) => y < -0.3)).toBe(
    true,
  );
  expect(stitchUnit("Feather stitch").holes.some(([, y]) => y > 0.3)).toBe(
    true,
  );
  const couch = stitchUnit("Couching stitch");
  expect(couch.body).toEqual([
    ["M", -0.5, 0],
    ["L", 0.5, 0],
  ]);
  expect(couch.holes).toEqual([
    [-0.045, -0.22],
    [0.045, 0.22],
  ]);
  expect(stitchUnit("Satin stitch", 0.08).holes.length).toBeGreaterThan(20);
});
test("drawdowns repeat exactly, every yarn binds, satin has one binding per shaft", () => {
  for (const family of weaveFamilies) {
    const n = weavePeriod(family);
    for (let i = 0; i < n; i++) {
      const row = [],
        col = [];
      for (let j = 0; j < n; j++) {
        const v = weaveDraft(family, i, j);
        row.push(v);
        col.push(weaveDraft(family, j, i));
        expect(weaveDraft(family, i + n, j)).toBe(v);
        expect(weaveDraft(family, i, j + n)).toBe(v);
      }
      expect(new Set(row).size, family + " row " + i).toBe(2);
      expect(new Set(col).size, family + " col " + i).toBe(2);
      if (family.includes("shaft satin")) {
        expect(row.filter((v) => !v)).toHaveLength(1);
        expect(col.filter((v) => !v)).toHaveLength(1);
      }
    }
    for (const l of weaveLayers({ draft: family })) {
      expect(l.path.length).toBeLessThan(100000);
      expect(l.path).not.toMatch(/NaN|Infinity/);
    }
  }
  expect(
    Array.from({ length: 4 }, (_, x) => weaveDraft("Twill 3 over 1", x, 0)),
  ).toEqual([true, true, true, false]);
  expect(
    Array.from({ length: 4 }, (_, x) => weaveDraft("Twill 3 over 1", x, 1)),
  ).toEqual([false, true, true, true]);
});
test("draft edits preserve added artwork and stitches and survive validation", () => {
  const d = validatePattern(
    applyStitches(patternStarter("Basket weave"), { type: "Chain stitch" }),
  );
  const next = validatePattern(applyWeave(d, { draft: "Basket 3x3" }));
  expect(next.layers.filter((l) => !l.weaveRole)).toEqual(
    d.layers.filter((l) => !l.weaveRole),
  );
  expect(next.weave.draft).toBe("Basket 3x3");
  expect(validatePattern(JSON.parse(JSON.stringify(next)))).toEqual(next);
  expect(Object.keys(weaveVariants)).toHaveLength(10);
});
test("geometric constructions preserve complete vector paths and physical aspect", () => {
  for (const spec of geometricCatalog) {
    const raw = geometricPattern(spec.name),
      d = patternStarter(spec.name);
    expect(d.construction).toBe(spec.id);
    expect(d.layers.map((l) => l.path)).toEqual(raw.layers.map((l) => l.path));
    expect(validatePattern(JSON.parse(JSON.stringify(d)))).toEqual(d);
    expect(patternSVG(d)).not.toContain("<image");
  }
  expect(patternStarter("Hexagon-triangle tiling").designAspect).toBeCloseTo(
    2 / Math.sqrt(3),
  );
});
test("preview shadows never enter exported albedo SVG", () => {
  for (const name of ["Chain stitch", "Plain weave"]) {
    const doc = patternStarter(name);
    expect(patternSVG(doc)).not.toContain("feDropShadow");
    expect(patternSVG(doc, "preview")).toContain("feDropShadow");
    expect(patternSVG(doc, "params")).not.toContain("feDropShadow");
  }
});
test("public UI keeps retired rug filters absent, adds requested heritage collections and preserves yarn controls/draft choice", async ({
  page,
}) => {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(
    "/?material=natural-cotton&studio=pattern&pattern=truchet-circuits",
  );
  await expect(page.getByLabel("Pattern collection")).toHaveValue(
    "Geometric constructions",
  );
  await expect(page.locator(".pe-starters button")).toHaveCount(24);
  const options = await page
    .getByLabel("Pattern collection")
    .locator("option")
    .allTextContents();
  expect(options).toContain("Islamic geometry");
  expect(options).toContain("African traditional");
  expect(options.join(" ")).not.toMatch(
    /carpet|rug|200|Reference|Legacy|African inlay|Islamic networks/i,
  );
  await page.getByLabel("Pattern collection").selectOption("Fabric weaves");
  await page.getByRole("button", { name: "Basket weave", exact: true }).click();
  await expect(page.getByLabel("Textile family colorway")).toHaveCount(0);
  await page
    .getByLabel("Weave draft", { exact: true })
    .selectOption("Basket 3x3");
  await page.getByLabel("Weave yarn color").fill("#c46842");
  const dl = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await dl).createReadStream()) chunks.push(c);
  const doc = JSON.parse(Buffer.concat(chunks));
  expect(doc.weave).toEqual({ draft: "Basket 3x3", color: "#c46842" });
  expect(
    doc.layers.filter((l) => l.weaveRole).every((l) => l.color === "#c46842"),
  ).toBe(true);
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByLabel("Pattern preview object").selectOption("Panel");
  await page.getByLabel("Pattern preview zoom").fill("280");
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.screenshot({ path: ".playwright/v77/basket-3d.png" });
  expect(errors).toEqual([]);
});

test("fine satin paths split without truncation, remain one color and enforce available layers", () => {
  for (const layout of ["rows", "columns", "diagonal", "border"]) {
    const raw = applyStitches(patternStarter("Blank"), {
      type: "Satin stitch",
      spacing: 16,
      width: 1,
      layout,
    });
    const d = validatePattern(raw);
    expect(d.layers.map((l) => l.path)).toEqual(raw.layers.map((l) => l.path));
    expect(d.layers.every((l) => l.path.length <= 90000)).toBe(true);
    expect(
      new Set(
        d.layers.filter((l) => l.stitchRole !== "holes").map((l) => l.color),
      ).size,
    ).toBe(1);
    expect(validatePattern(applyStitches(d, {}))).toEqual(d);
  }
  expect(() =>
    applyStitches(
      { ...patternStarter("Blank"), layers: Array(61).fill({ kind: "rect" }) },
      { type: "Satin stitch", spacing: 16, width: 1 },
    ),
  ).toThrow(/free layers/);
});
