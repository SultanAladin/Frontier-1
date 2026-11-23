import { test, expect } from "@playwright/test";
import fs from "node:fs";
import opentype from "opentype.js";
import {
  patternLayer,
  patternStarter,
  validatePattern,
  patternShape,
  patternSVG,
  normalizePatternText,
} from "../src/patternDocument.js";
import {
  patternArcNodes,
  patternPathContours,
} from "../src/patternGeometry.js";
import {
  layerWorldPoint,
  worldPathLayer,
  convertPatternLayerToPath,
  drawnPatternLayer,
} from "../src/shapeEditorGeometry.js";
import {
  createEditorSnapTargets,
  addEditorPathSnapTargets,
  snapEditorPoint,
} from "../src/vectorEditorSnapping.js";
import {
  booleanPatternLayers,
  layerWorldContours,
} from "../src/vectorEditorGeometry.js";
import {
  outlinePatternText,
  updatePatternTextLayer,
} from "../src/patternTextGeometry.js";
import { pathSegment, cubicPathPoint } from "../src/pathEditorGeometry.js";
const font = (name = "dm-sans", weight = 400) => {
  const bytes = fs.readFileSync(
    `node_modules/@fontsource/${name}/files/${name}-latin-${weight}-normal.woff`,
  );
  return opentype.parse(
    bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength),
  );
};
const doc = (...layers) =>
  validatePattern({ ...patternStarter("Blank"), layers });
const rect = (x = 100, y = 100, w = 100, h = 100, extra = {}) =>
  patternLayer("rect", {
    x,
    y,
    width: w,
    height: h,
    paint: true,
    fillEnabled: true,
    visible: true,
    color: "#bd7444",
    stroke: "#263c48",
    strokeWidth: 2,
    ...extra,
  });

test("point snapping honors transformed anchors, physical tolerance and endpoint priority over grids", () => {
  const l = rect(250, 250, 180, 80, { rotation: 35, flipX: true }),
    document = doc(l),
    targets = createEditorSnapTargets(document),
    anchor = layerWorldPoint(document.layers[0], { x: 0, y: 0 });
  const result = snapEditorPoint(
    { x: anchor.x + 2, y: anchor.y + 3 },
    targets,
    { grid: 32, guides: false, aspect: 2, tolerance: 6 },
  );
  expect(result.point.x).toBeCloseTo(anchor.x, 6);
  expect(result.point.y).toBeCloseTo(anchor.y, 6);
  expect(result.label).toBe("Anchor");
  const far = snapEditorPoint({ x: anchor.x + 4, y: anchor.y }, targets, {
    grid: 0,
    guides: false,
    aspect: 2,
    tolerance: 6,
  });
  expect(far.label).toBe("");
});
test("curve snapping projects onto exact cubics and alignment targets without moving existing geometry", () => {
  const p = worldPathLayer(
      [
        { x: 20, y: 200, out: { x: 80, y: 50 } },
        { x: 220, y: 200, in: { x: 160, y: 350 } },
      ],
      false,
      { paint: true, fillEnabled: false, strokeWidth: 4 },
    ),
    document = doc(p),
    targets = createEditorSnapTargets(document),
    before = JSON.stringify(document);
  const position = cubicPathPoint(
    pathSegment(layerWorldContours(document.layers[0])[0].nodes, 0),
    0.5,
  );
  const result = snapEditorPoint(
    { x: position.x, y: position.y + 3 },
    targets,
    { curves: true, points: false, guides: false, tolerance: 6 },
  );
  expect(result.label).toBe("Curve / edge");
  expect(result.distance).toBeLessThan(4);
  expect(JSON.stringify(document)).toBe(before);
  const aligned = snapEditorPoint({ x: 258, y: 400 }, targets, {
    points: false,
    curves: false,
    guides: true,
    tolerance: 4,
  });
  expect(aligned.point.x).toBe(256);
  expect(aligned.guides).toEqual([{ axis: "x", value: 256 }]);
});
test("snap targets exclude selected anchors and adjacent segments but include locked visible references", () => {
  const document = doc(
      rect(50, 50, 40, 40, { locked: true }),
      rect(400, 400, 40, 40, { visible: false }),
    ),
    targets = createEditorSnapTargets(document);
  expect(targets.points.some((p) => p.layer === 0)).toBe(true);
  expect(targets.points.some((p) => p.layer === 1)).toBe(false);
  const base = createEditorSnapTargets(document, [0]),
    nodes = [
      { x: 30, y: 30 },
      { x: 100, y: 100 },
      { x: 300, y: 100 },
    ];
  addEditorPathSnapTargets(base, nodes, false, [1]);
  expect(base.points.some((p) => p.node === 1)).toBe(false);
  expect(base.curves).toHaveLength(0);
  expect(base.points).toHaveLength(2);
});
test("angle snapping remains independent of the magnet and never bends a constrained handle toward off-angle targets", () => {
  const targets = {
    points: [{ x: 100, y: 10, label: "Anchor" }],
    curves: [],
    axes: { x: [100], y: [10] },
  };
  const result = snapEditorPoint({ x: 99, y: 11 }, targets, {
    origin: { x: 0, y: 0 },
    angle: 45,
    tolerance: 15,
  });
  expect(result.point.y).toBeCloseTo(0, 6);
  expect(result.label).not.toBe("Anchor");
  const grid = snapEditorPoint({ x: 18, y: 35 }, targets, {
    enabled: false,
    grid: 16,
  });
  expect(grid.point).toEqual({ x: 16, y: 32 });
  const diagonal = snapEditorPoint({ x: 20, y: 37 }, targets, {
    enabled: false,
    angle: 45,
    origin: { x: 0, y: 0 },
    aspect: 2,
  });
  expect(diagonal.point.y).toBeCloseTo(diagonal.point.x * 2, 6);
});
test("editable arcs preserve signed sweeps, tangent continuity and chord or pie closure", () => {
  for (const sweep of [90, -90, 270, -270, 359.99]) {
    const arc = patternArcNodes(-30, sweep);
    expect(arc.closed).toBe(false);
    expect(arc.nodes.length).toBe(Math.ceil(Math.abs(sweep) / 90) + 1);
    for (const n of arc.nodes) {
      expect(Math.hypot(n.x - 50, n.y - 50)).toBeCloseTo(50, 6);
      for (const key of ["x", "y"]) expect(Number.isFinite(n[key])).toBe(true);
    }
    const end = ((sweep - 30) * Math.PI) / 180;
    expect(arc.nodes.at(-1).x).toBeCloseTo(50 + 50 * Math.cos(end), 6);
  }
  expect(patternArcNodes(0, 90, "pie").nodes.at(-1)).toEqual({ x: 50, y: 50 });
  expect(patternArcNodes(0, 90, "chord").closed).toBe(true);
  expect(
    drawnPatternLayer(
      "arc",
      { x: 0, y: 0 },
      { x: 100, y: 100 },
      { paint: true, fillEnabled: true },
    ).fillEnabled,
  ).toBe(false);
  const l = doc(
    patternLayer("arc", {
      paint: true,
      arcStart: 25,
      arcSweep: -245,
      arcClosure: "pie",
    }),
  ).layers[0];
  expect(patternShape(l, l.color)).toContain(" C");
  expect(convertPatternLayerToPath(l).closed).toBe(true);
});
test("text metadata and outlined multilingual Latin glyphs round-trip and render without any font dependency", () => {
  const outlined = outlinePatternText(font(), {
      value: "ALLØY & <SVG>\nCafé",
      fontSize: 32,
      align: "center",
      letterSpacing: 1.5,
    }),
    layer = patternLayer("text", {
      ...outlined,
      paint: true,
      fillEnabled: true,
      strokeWidth: 3,
      color: "#ffffff",
      stroke: "#000000",
    }),
    document = doc(layer);
  expect(outlined.unsupported).toHaveLength(0);
  expect(document.layers[0].text.value).toBe("ALLØY & <SVG>\nCafé");
  expect(document.layers[0].path).toContain("M");
  const svg = patternSVG(document);
  expect(svg).not.toContain("<text");
  expect(svg).not.toContain("font-family");
  expect(svg).not.toContain("<SVG>");
  expect(svg).toContain('stroke-width="3"');
  expect(validatePattern(JSON.parse(JSON.stringify(document)))).toEqual(
    document,
  );
  expect(outlinePatternText(font(), { value: "💎" }).unsupported).toEqual([
    "💎",
  ]);
});
test("text font-size and content edits preserve rotated resized geometry while outlining removes only text metadata", () => {
  const f = font(),
    g = outlinePatternText(f, { value: "AVATAR", fontSize: 32 }),
    layer = doc(
      patternLayer("text", {
        ...g,
        paint: true,
        width: g.width * 1.5,
        height: g.height * 0.8,
        rotation: 35,
        flipX: true,
      }),
    ).layers[0];
  const scaled = updatePatternTextLayer(layer, f, { fontSize: 64 });
  expect(scaled.width).toBeCloseTo(layer.width * 2, 4);
  expect(scaled.height).toBeCloseTo(layer.height * 2, 4);
  expect(scaled.rotation).toBe(35);
  expect(scaled.flipX).toBe(true);
  const converted = convertPatternLayerToPath(scaled);
  expect(converted.kind).toBe("path");
  expect(converted.text).toBeUndefined();
  expect(converted.path).toBe(scaled.path);
  expect(converted.width).toBe(scaled.width);
  const bold = font("space-grotesk", 600),
    changed = updatePatternTextLayer(
      layer,
      bold,
      { fontFamily: "Space Grotesk", fontWeight: 600 },
      f,
    );
  expect(changed.text.fontFamily).toBe("Space Grotesk");
  expect(changed.path).not.toBe(layer.path);
});
test("empty text and invalid typography are bounded, finite and still editable", () => {
  const text = normalizePatternText({
    value: "\u0001\n",
    fontSize: Infinity,
    fontFamily: "remote-font",
    letterSpacing: -100,
    lineHeight: 100,
  });
  expect(text.fontFamily).toBe("DM Sans");
  expect(text.fontSize).toBe(48);
  expect(text.letterSpacing).toBe(-10);
  expect(text.lineHeight).toBe(3);
  const empty = outlinePatternText(font(), { value: " \n", fontSize: 48 });
  expect(empty.path).toBe("");
  expect(empty.width).toBeGreaterThan(0);
  expect(empty.height).toBeGreaterThan(48);
  expect(() =>
    validatePattern({
      ...patternStarter("Blank"),
      layers: [patternLayer("text", { path: "M0 0<script>" })],
    }),
  ).toThrow("Invalid SVG path");
});
test("Bézier boolean union, difference, intersection and exclusion retain exact filled silhouettes", () => {
  const layers = doc(rect(), rect(150)).layers;
  const union = booleanPatternLayers(layers, "union"),
    difference = booleanPatternLayers(layers, "difference"),
    intersection = booleanPatternLayers(layers, "intersection"),
    exclude = booleanPatternLayers(layers, "exclude");
  expect(union.width).toBe(150);
  expect(union.height).toBe(100);
  expect(difference.width).toBe(50);
  expect(intersection.width).toBe(50);
  expect(patternPathContours(exclude.path)).toHaveLength(2);
  expect(exclude.color).toBe(layers[0].color);
  const curved = booleanPatternLayers(
    doc(
      patternLayer("ellipse", {
        x: 100,
        y: 100,
        width: 100,
        height: 100,
        paint: true,
        fillEnabled: true,
      }),
      rect(135),
    ).layers,
    "union",
  );
  expect(curved.path).toContain("C");
});
test("compound boolean holes, empty results and protected or open sources are handled atomically", () => {
  const layers = doc(rect(200, 200, 180, 180), rect(200, 200, 80, 80)).layers,
    before = JSON.stringify(layers),
    hole = booleanPatternLayers(layers, "difference");
  expect(patternPathContours(hole.path)).toHaveLength(2);
  expect(hole.nodes).toBeUndefined();
  expect(JSON.stringify(layers)).toBe(before);
  expect(() =>
    booleanPatternLayers(doc(rect(), rect(450, 450)).layers, "intersection"),
  ).toThrow("no filled result");
  expect(() =>
    booleanPatternLayers([{ ...layers[0], locked: true }, layers[1]], "union"),
  ).toThrow("unlocked");
  expect(() =>
    booleanPatternLayers(
      [{ ...layers[0], fillEnabled: false }, layers[1]],
      "union",
    ),
  ).toThrow("filled");
  expect(() =>
    booleanPatternLayers(
      [
        worldPathLayer(
          [
            { x: 0, y: 0 },
            { x: 100, y: 100 },
          ],
          false,
          { paint: true, fillEnabled: true, visible: true },
        ),
        layers[1],
      ],
      "union",
    ),
  ).toThrow("closed");
});
