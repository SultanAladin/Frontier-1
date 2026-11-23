import { test, expect } from "@playwright/test";
import { createHash } from "node:crypto";
import {
  heritagePatternCatalog,
  islamicPatternCatalog,
  africanPatternCatalog,
  heritagePattern,
} from "../src/heritagePatterns.js";
import {
  patternStarter,
  patternStarterCatalog,
  patternInventory,
  patternStructureManifest,
  resolvePatternStarterName,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import { patternPathContours } from "../src/patternGeometry.js";
import { applyStitches } from "../src/patternStitches.js";
import { replacePatternColor } from "../src/patternLibrary.js";
import { ornamentalCatalog } from "../src/ornamentalConstructions.js";
import { rugDesignCatalog } from "../src/rugDesigns.js";

const signature = (d) =>
  createHash("sha256")
    .update(
      JSON.stringify(
        d.layers.map(
          ({
            kind,
            path,
            x,
            y,
            width,
            height,
            rotation,
            fillRule,
            strokeWidth,
            paint,
            fillEnabled,
          }) => ({
            kind,
            path,
            x,
            y,
            width,
            height,
            rotation,
            fillRule,
            strokeWidth,
            paint,
            fillEnabled,
          }),
        ),
      ),
    )
    .digest("hex");

test("exactly twenty public additions, ten per requested collection, without resurrecting retired rugs or padding with palettes", () => {
  expect(heritagePatternCatalog).toHaveLength(20);
  expect(islamicPatternCatalog).toHaveLength(10);
  expect(africanPatternCatalog).toHaveLength(10);
  expect(patternStarterCatalog).toHaveLength(103);
  expect(new Set(patternStarterCatalog.map((e) => e.name)).size).toBe(103);
  expect(new Set(heritagePatternCatalog.map((e) => e.id)).size).toBe(20);
  expect(patternInventory).toEqual({
    catalogEntries: 102,
    stitches: 10,
    weaves: 10,
  });
  expect(patternStructureManifest).toHaveLength(102);
  const existing = patternStarterCatalog.filter(
    (e) => !heritagePatternCatalog.some((h) => h.name === e.name),
  );
  expect(existing).toHaveLength(83);
  for (const [group, catalog] of [
    ["Islamic geometry", islamicPatternCatalog],
    ["African traditional", africanPatternCatalog],
  ]) {
    expect(patternStarterCatalog.filter((e) => e.group === group)).toEqual(
      catalog,
    );
  }
  for (const retired of [...ornamentalCatalog, ...rugDesignCatalog])
    expect(patternStarterCatalog.some((e) => e.name === retired.name)).toBe(
      false,
    );
  expect(heritagePattern("unrecognised artwork")).toBeNull();
});

for (const spec of heritagePatternCatalog) {
  test(`${spec.name}: deterministic native vectors, editable finish/colour/geometry, safe bounds and complete portable round-trip`, () => {
    const raw = heritagePattern(spec.name),
      doc = patternStarter(spec.name);
    expect(doc.name).toBe(spec.name);
    expect(doc.construction).toBe(spec.id);
    expect(doc.collection).toBe(spec.group);
    expect(doc.library).toEqual({ family: spec.name, palette: "Original" });
    expect(resolvePatternStarterName(spec.id)).toBe(spec.name);
    expect(heritagePattern(spec.id)).toEqual(raw);
    expect(patternStarter(spec.name)).toEqual(doc);
    expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
    expect(doc.layers.length).toBeGreaterThanOrEqual(4);
    expect(doc.layers.length).toBeLessThanOrEqual(61);
    expect(doc.layers.map((l) => l.path)).toEqual(
      raw.layers.map((l) => l.path),
    );
    expect(new Set(doc.layers.map((l) => l.name)).size).toBe(doc.layers.length);
    expect(JSON.stringify(doc).length).toBeLessThan(200000);
    for (const l of doc.layers) {
      expect(l.kind).toBe("path");
      expect(l.paint).toBe(true);
      expect(l.path.length).toBeLessThan(100000);
      expect(l.path).not.toMatch(/NaN|Infinity|<|javascript:|https?:/);
      const contours = patternPathContours(l.path);
      expect(contours.length).toBeGreaterThan(0);
      expect(
        contours.every((p) =>
          p.nodes.every((n) => Number.isFinite(n.x) && Number.isFinite(n.y)),
        ),
      ).toBe(true);
      expect(l.strokeWidth).toBeLessThanOrEqual(25);
    }
    for (const mode of ["color", "preview", "params", "finish"]) {
      const svg = patternSVG(doc, mode);
      expect(svg).toContain("<svg");
      expect(svg).toContain("<path");
      expect(svg).not.toMatch(/NaN|Infinity|<image|<script|href=|javascript:/);
    }
    const edited = validatePattern({
      ...doc,
      repeats: 3,
      layers: doc.layers.map((l, i) =>
        i === 1
          ? { ...l, x: l.x + 12, rotation: 9, relief: 0.23, visible: false }
          : l,
      ),
    });
    expect(edited.layers[1]).toMatchObject({
      x: 268,
      rotation: 9,
      relief: 0.23,
      visible: false,
    });
    const recoloured = validatePattern(
      replacePatternColor(doc, doc.layers[1].color, "#d73365"),
    );
    expect(recoloured.layers[1].color).toBe("#d73365");
    expect(signature(recoloured)).toBe(signature(doc));
    const stitched = validatePattern(
      applyStitches(doc, {
        type: "Running stitch",
        layout: "border",
        color: "#543c7a",
      }),
    );
    expect(stitched.layers.slice(0, doc.layers.length)).toEqual(doc.layers);
    expect(stitched.layers.filter((l) => l.stitchRole)).toHaveLength(3);
    expect(stitched.construction).toBe(spec.id);
    expect(stitched.layers.length).toBeLessThanOrEqual(64);
  });
}

test("all twenty new path structures differ from one another and the 83 retained cards; colourways do not duplicate their identities", () => {
  const signatures = new Set(
    patternStarterCatalog
      .filter((e) => !heritagePatternCatalog.some((h) => h.name === e.name))
      .map((e) => signature(patternStarter(e.name))),
  );
  for (const spec of heritagePatternCatalog) {
    const key = signature(patternStarter(spec.name));
    expect(signatures.has(key), spec.name).toBe(false);
    signatures.add(key);
    for (const colourway of ["Indigo", "Earth", "Studio", "Mulberry"])
      expect(patternStarter(spec.name, colourway)).toEqual(
        patternStarter(spec.name),
      );
  }
});

test("star point counts, girih petals, cross infills and true hexagonal edge lengths follow their stated construction", () => {
  const zellige = patternStarter("Zellige Eight-Point Stars"),
    girih = patternStarter("Girih Decagon Rosettes"),
    seljuk = patternStarter("Seljuk Star-and-Cross"),
    hex = patternStarter("Maghrebi Honeycomb Stars");
  const layer = (d, name) => d.layers.find((l) => l.name === name);
  const zStars = patternPathContours(layer(zellige, "Gold star rims").path);
  expect(zStars).toHaveLength(4);
  expect(zStars.every((p) => p.nodes.length === 16)).toBe(true);
  expect(
    patternPathContours(layer(girih, "Rhombic rosette petals").path),
  ).toHaveLength(40);
  const crosses = patternPathContours(layer(seljuk, "Ivory cross tiles").path);
  expect(crosses).toHaveLength(25);
  expect(crosses.every((p) => p.nodes.length === 12)).toBe(true);
  const nodes = patternPathContours(
    layer(hex, "Terracotta honeycomb faces").path,
  )[0].nodes;
  expect(nodes).toHaveLength(6);
  const lengths = nodes.map((p, i) => {
    const q = nodes[(i + 1) % nodes.length];
    return Math.hypot((p.x - q.x) * hex.designAspect, p.y - q.y);
  });
  expect(Math.max(...lengths) - Math.min(...lengths)).toBeLessThan(0.001);
  expect(hex.designAspect).toBeCloseTo(2 / Math.sqrt(3), 10);
});

test("African tradition and region labels stay specific, and the border and abstract non-text maze retain honest metadata", () => {
  for (const spec of africanPatternCatalog) {
    expect(spec.description).toMatch(/—/);
    expect(spec.description).toMatch(/inspired/);
  }
  expect(
    africanPatternCatalog.find((e) => e.id === "ndebele-painted-chevrons")
      .description,
  ).toContain("South Africa");
  expect(patternStarter("Ethiopian Tibeb Border").tileAxes).toBe("x");
  const maze = islamicPatternCatalog.find(
    (e) => e.id === "kufic-inspired-square-maze",
  );
  expect(maze.description).toContain("no readable lettering");
  expect(patternStarter(maze.name).layers.every((l) => l.kind === "path")).toBe(
    true,
  );
});
