import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import {
  ornamentalCatalog,
  ornamentalPattern,
} from "../src/ornamentalConstructions.js";
import {
  patternStarterCatalog,
  patternStarter,
  patternStructureManifest,
  patternInventory,
  patternSVG,
  validatePattern,
} from "../src/patternDocument.js";
import { applyStitches } from "../src/patternStitches.js";
const geometry = (d) =>
  JSON.stringify(
    d.layers.map(({ path, kind, x, y, width, height, rotation, fillRule }) => ({
      path,
      kind,
      x,
      y,
      width,
      height,
      rotation,
      fillRule,
    })),
  );

test("rejected collections have no public cards or quota claim", () => {
  expect(patternInventory).toEqual({
    catalogEntries: 102,
    stitches: 10,
    weaves: 10,
  });
  expect(patternStructureManifest).toHaveLength(102);
  for (const p of ornamentalCatalog)
    expect(patternStarterCatalog.some((c) => c.name === p.name)).toBe(false);
  expect(patternStarterCatalog.some((p) => p.countedDesign)).toBe(false);
});

test("Retired construction factories preserve complete bounded paths, construction identity and added stitches", () => {
  expect(ornamentalCatalog).toHaveLength(63);
  for (const group of [
    "African inlay",
    "Islamic networks",
    "Ornamental carpets",
  ])
    expect(ornamentalCatalog.filter((p) => p.group === group)).toHaveLength(21);
  for (const p of ornamentalCatalog) {
    const raw = ornamentalPattern(p.name),
      doc = patternStarter(p.name);
    expect(doc.construction).toBe(p.id);
    expect(doc.layers.length).toBeLessThanOrEqual(61);
    expect(
      doc.layers.every(
        (l) => l.path.length < 100000 && !/NaN|Infinity/.test(l.path),
      ),
    ).toBe(true);
    expect(doc.layers.map((l) => l.path)).toEqual(
      raw.layers.map((l) => l.path),
    );
    expect(JSON.stringify(doc).length).toBeLessThan(12000000);
    expect(patternSVG(doc)).not.toContain("<image");
    const stitched = validatePattern(
      applyStitches(doc, {
        enabled: true,
        type: "Chain stitch",
        layout: "border",
      }),
    );
    expect(stitched.layers.filter((l) => l.stitchRole)).toHaveLength(3);
    expect(stitched.construction).toBe(p.id);
  }
});
