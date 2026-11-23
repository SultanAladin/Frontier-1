import { test, expect } from "@playwright/test";
import {
  patternStarter,
  patternLayer,
  validatePattern,
  patternShape,
  patternSVG,
} from "../src/patternDocument.js";
import {
  patternNodesPath,
  normalizePatternNodes,
  patternPrimitiveNodes,
  patternPathContours,
} from "../src/patternGeometry.js";
import {
  layerLocalPoint,
  layerWorldPoint,
  layerBounds,
  selectionBounds,
  translateSelection,
  snappedTranslation,
  resizeSelection,
  rotateSelection,
  alignSelection,
  distributeSelection,
  reorderSelection,
  clonePatternLayers,
  expandPatternSelection,
  editableSelection,
  svgPathNodes,
  convertPatternLayerToPath,
  pathNodesBounds,
  worldPathLayer,
  drawnPatternLayer,
  simplifyDrawnPoints,
  boundedDrawnPoints,
  patternCopies,
  reframePatternPath,
} from "../src/shapeEditorGeometry.js";
import {
  readPatternDraft,
  savePatternDraft,
  PATTERN_DRAFT_KEY,
} from "../src/patternDraft.js";
const documentWith = (...layers) =>
  validatePattern({ ...patternStarter("Blank"), layers });

test("editable shape schema round-trips and paint coverage agrees across material channels", () => {
  const doc = documentWith(
    patternLayer("rect", {
      paint: true,
      fillEnabled: true,
      strokeWidth: 2,
      stroke: "#123456",
      cornerRadius: 15,
      locked: true,
      group: "group-test",
    }),
    patternLayer("polygon", {
      sides: 8,
      paint: true,
      fillEnabled: false,
      strokeWidth: 3,
      stroke: "#abcdef",
    }),
    patternLayer("star", { starPoints: 7, innerRadius: 0.3 }),
    patternLayer("path", {
      paint: true,
      nodes: [
        { x: 5, y: 10, out: { x: 25, y: 95 } },
        { x: 95, y: 10, in: { x: 75, y: 95 } },
      ],
      closed: false,
    }),
  );
  expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
  expect(doc.layers[3].path).toContain(" C");
  expect(patternShape(doc.layers[0], "#ff0000")).toContain('stroke="#123456"');
  expect(patternShape(doc.layers[0], "rgb(128,0,255)", "params")).toContain(
    'stroke="rgb(128,0,255)"',
  );
  expect(patternShape(doc.layers[1], "#ff0000")).toContain('fill="none"');
  expect(patternSVG(doc, "finish")).not.toContain("#123456");
  expect(patternShape(doc.layers[0], "#ff0000")).toContain('rx="24"');
  expect(
    patternShape(
      patternLayer("path", { path: "M0 0L100 100", strokeWidth: 2 }),
      "#ff0000",
    ),
  ).toContain('fill="none" stroke="#ff0000"');
  expect(() =>
    normalizePatternNodes([
      { x: NaN, y: 0 },
      { x: 1, y: 2 },
    ]),
  ).toThrow("finite");
  expect(() => normalizePatternNodes(Array(401).fill({ x: 0, y: 0 }))).toThrow(
    "400",
  );
  expect(() =>
    documentWith(patternLayer("path", { path: "M1e900 0L100 100" })),
  ).toThrow("finite");
  expect(() =>
    documentWith(patternLayer("path", { path: "M0 0" + "L1 1".repeat(25001) })),
  ).toThrow("100,000");

  expect(() =>
    validatePattern({
      ...doc,
      layers: [{ kind: "path", path: 'M0 0"/><script>' }],
    }),
  ).toThrow("Invalid SVG path");
});

test("transforms keep anchored corners, group spacing and reflected local coordinates", () => {
  const l = patternLayer("rect", {
    x: 180,
    y: 190,
    width: 100,
    height: 60,
    rotation: 35,
    flipX: true,
  });
  const doc = documentWith(l),
    original = doc.layers[0],
    corner = layerWorldPoint(original, { x: 100, y: 0 });
  const input = { x: 14, y: 87 },
    world = layerWorldPoint(original, input),
    local = layerLocalPoint(original, world);
  expect(local.x).toBeCloseTo(input.x);
  expect(local.y).toBeCloseTo(input.y);
  const target = layerWorldPoint(original, { x: -50, y: 180 }),
    resized = resizeSelection(doc, [0], "se", target),
    next = resized.layers[0],
    anchored = layerWorldPoint(next, { x: 100, y: 0 });
  expect(anchored.x).toBeCloseTo(corner.x);
  expect(anchored.y).toBeCloseTo(corner.y);
  const pair = documentWith(
    patternLayer("rect", { x: 60, y: 100 }),
    patternLayer("ellipse", { x: 130, y: 160 }),
  );
  const moved = translateSelection(pair, [0, 1], 2000, -2000);
  expect(moved.layers[1].x).toBe(1024);
  expect(moved.layers[0].y).toBe(-512);
  expect(moved.layers[1].x - moved.layers[0].x).toBe(70);
  expect(moved.layers[1].y - moved.layers[0].y).toBe(60);
  const b = selectionBounds(pair, [0, 1]),
    turned = rotateSelection(
      pair,
      [0, 1],
      { x: b.x, y: b.y - 100 },
      { x: b.x + 100, y: b.y },
      true,
    );
  expect(turned.layers[0].rotation).toBe(90);
  expect(
    Math.hypot(
      turned.layers[1].x - turned.layers[0].x,
      turned.layers[1].y - turned.layers[0].y,
    ),
  ).toBeCloseTo(Math.hypot(70, 60));
  const rotatedPair = documentWith(
      l,
      patternLayer("ellipse", {
        x: 300,
        y: 300,
        width: 80,
        height: 100,
        rotation: 27,
      }),
    ),
    box = selectionBounds(rotatedPair, [0, 1]);
  const stretched = resizeSelection(rotatedPair, [0, 1], "se", {
    x: box.maxX + 80,
    y: box.maxY + 5,
  });
  expect(stretched.layers[0].width / l.width).toBeCloseTo(
    stretched.layers[0].height / l.height,
  );
});

test("alignment, equal-gap distribution and layer ordering work on selection units", () => {
  const doc = documentWith(
    patternLayer("rect", { x: 60, y: 90, width: 40, height: 40 }),
    patternLayer("ellipse", { x: 160, y: 180, width: 70, height: 40 }),
    patternLayer("triangle", { x: 410, y: 260, width: 100, height: 40 }),
  );
  const aligned = alignSelection(doc, [0, 1, 2], "top");
  expect(new Set(aligned.layers.map((l) => l.y)).size).toBe(1);
  const distributed = distributeSelection(doc, [0, 1, 2], "x"),
    b = distributed.layers.map(layerBounds);
  expect(b[1].minX - b[0].maxX).toBeCloseTo(b[2].minX - b[1].maxX);
  const front = reorderSelection(doc, [0, 2], "front");
  expect(front.doc.layers.map((l) => l.kind)).toEqual([
    "ellipse",
    "rect",
    "triangle",
  ]);
  expect(front.selected).toEqual([1, 2]);
  expect(reorderSelection(doc, [0, 1], "forward").selected).toEqual([1, 2]);
  const grouped = documentWith(
    { ...doc.layers[0], group: "original" },
    { ...doc.layers[1], group: "original" },
    doc.layers[2],
  );
  const centered = alignSelection(grouped, [0, 1], "center-x");
  expect(selectionBounds(centered, [0, 1]).x).toBe(256);
  expect(centered.layers[1].x - centered.layers[0].x).toBe(100);
  const clones = clonePatternLayers(grouped.layers.slice(0, 2));
  expect(clones[0].group).not.toBe("original");
  expect(clones[1].group).toBe(clones[0].group);
  expect(expandPatternSelection(grouped, 0)).toEqual([0, 1]);
  expect(
    editableSelection(
      documentWith(
        { ...doc.layers[0], locked: true },
        { ...doc.layers[1], visible: false },
      ),
      [0, 1],
    ),
  ).toEqual([]);
});

test("snapping preserves a selection as a rigid set and produces visible alignment guides", () => {
  const doc = documentWith(
    patternLayer("rect", { x: 100, y: 110, width: 30, height: 40 }),
    patternLayer("rect", { x: 150, y: 130, width: 30, height: 40 }),
    patternLayer("rect", { x: 256, y: 256, width: 100, height: 100 }),
  );
  const grid = snappedTranslation(
    doc,
    [0, 1],
    { x: 10, y: 12 },
    { grid: 16, guides: false },
  );
  expect(grid.delta).toEqual({ x: 10, y: 14 });
  const guide = snappedTranslation(
    doc,
    [0],
    { x: 154, y: 145 },
    { guides: true, tolerance: 3 },
  );
  expect(guide.delta).toEqual({ x: 156, y: 146 });
  expect(guide.guides).toHaveLength(2);
});

test("Bézier contours, relative SVG curves and arcs are portable and geometrically stable", () => {
  for (const path of [
    "M10 90 q40 -120 80 0",
    "M0 0c0 20 30 40 50 50s40 0 50 -50",
    "M0 20H30v30L0 50z",
    "M10 50 A40 30 25 0 1 90 50",
    "M0 0 Q20 30 40 0 T80 0",
  ]) {
    const contour = svgPathNodes(path);
    expect(contour).not.toBeNull();
    expect(patternNodesPath(contour.nodes, contour.closed)).toMatch(/^M/);
    expect(contour.nodes.length).toBeLessThan(400);
  }
  expect(svgPathNodes("M0 0L30 30Z M40 40L60 60Z")).toBeNull();
  const ellipse = convertPatternLayerToPath(patternLayer("ellipse"));
  expect(ellipse.nodes).toHaveLength(4);
  expect(ellipse.closed).toBe(true);
  const b = pathNodesBounds(ellipse.nodes, true);
  expect(b).toMatchObject({ minX: 0, maxX: 100, minY: 0, maxY: 100 });
  const nodes = [
      { x: 40, y: 80, out: { x: 70, y: -70 } },
      { x: 180, y: 80, in: { x: 150, y: -70 } },
    ],
    layer = worldPathLayer(nodes, false, { paint: true });
  layer.nodes.forEach((n, i) => {
    const p = layerWorldPoint(layer, n);
    expect(p.x).toBeCloseTo(nodes[i].x);
    expect(p.y).toBeCloseTo(nodes[i].y);
  });
  expect(layer.height).toBeCloseTo(112.5);
  expect(
    patternPrimitiveNodes(patternLayer("rect", { cornerRadius: 20 })).nodes,
  ).toHaveLength(8);
  const square = drawnPatternLayer(
    "rect",
    { x: 100, y: 100 },
    { x: 180, y: 140 },
    { paint: true },
    true,
    true,
  );
  expect(square).toMatchObject({ x: 100, y: 100, width: 160, height: 160 });
  expect(
    simplifyDrawnPoints(Array.from({ length: 30 }, (_, i) => ({ x: i, y: i }))),
  ).toHaveLength(2);
});

test("editor wrap copies follow repeat axes, mirrors and half-drop offsets", () => {
  const l = patternLayer("triangle", { x: 500, y: 200, width: 80, height: 90 });
  const straight = patternCopies({ tileAxes: "xy", repeat: "straight" }, l);
  expect(straight.some((c) => c.x === -12)).toBe(true);
  const mirror = patternCopies({ tileAxes: "xy", repeat: "mirror" }, l);
  expect(mirror.some((c) => c.sx === -1)).toBe(true);
  const drop = patternCopies({ tileAxes: "xy", repeat: "half-drop" }, l);
  expect(drop.some((c) => c.col === -1 && c.y === 456)).toBe(true);
  expect(
    patternCopies({ tileAxes: "none", repeat: "straight" }, l),
  ).toHaveLength(1);
  expect(
    patternCopies({ tileAxes: "x", repeat: "straight" }, l).every(
      (c) => c.row === 0,
    ),
  ).toBe(true);
});

test("draft recovery tolerates corrupt data and reports storage failures", () => {
  const items = new Map(),
    storage = {
      getItem: (k) => items.get(k),
      setItem: (k, v) => items.set(k, v),
    },
    doc = documentWith(patternLayer("star"));
  savePatternDraft(doc, storage);
  expect(readPatternDraft(storage).doc).toEqual(doc);
  items.set(PATTERN_DRAFT_KEY, "{broken");
  expect(readPatternDraft(storage)).toBeNull();
  expect(() =>
    savePatternDraft(doc, {
      setItem() {
        throw new Error("QuotaExceeded");
      },
    }),
  ).toThrow("QuotaExceeded");
});

test("path reframing preserves world-space points and handles after rotated edits", () => {
  const layer = {
    ...patternLayer("path", {
      x: 260,
      y: 210,
      width: 180,
      height: 120,
      rotation: 33,
      flipY: true,
    }),
    closed: false,
    nodes: [
      { x: -10, y: 100, out: { x: 15, y: -20 } },
      { x: 110, y: 5, in: { x: 130, y: 10 } },
    ],
  };
  const reframed = reframePatternPath(layer);
  layer.nodes.forEach((node, i) => {
    for (const key of ["anchor", "in", "out"]) {
      const original = key === "anchor" ? node : node[key],
        next = key === "anchor" ? reframed.nodes[i] : reframed.nodes[i][key];
      if (!original) continue;
      const a = layerWorldPoint(layer, original),
        b = layerWorldPoint(reframed, next);
      expect(b.x).toBeCloseTo(a.x);
      expect(b.y).toBeCloseTo(a.y);
    }
  });
  expect(reframed.width).not.toBe(layer.width);
});

test("compound contour sizing preserves holes and long freehand strokes keep their endpoints", () => {
  const compound = "M0 0H100V100H0Z m25 25h50v50h-50z";
  const contours = patternPathContours(compound);
  expect(contours).toHaveLength(2);
  expect(contours[1].nodes[0]).toMatchObject({ x: 25, y: 25 });
  expect(svgPathNodes(compound)).toBeNull();
  const l = patternLayer("path", {
    path: compound,
    paint: true,
    fillRule: "evenodd",
    fillEnabled: true,
    width: 200,
    height: 100,
    strokeWidth: 2,
    stroke: "#abcdef",
  });
  const svg = patternShape(l, "#ff0000");
  expect(svg).toContain('fill-rule="evenodd"');
  expect(svg).toContain("M-100 -50");
  expect(svg).toContain("M-50 -25");
  expect(svg).not.toContain("vector-effect");
  const input = Array.from({ length: 1200 }, (_, i) => ({
      x: i,
      y: i % 2 ? 20 : -20,
    })),
    result = boundedDrawnPoints(input, 0.1);
  expect(result).toHaveLength(400);
  expect(result[0]).toEqual(input[0]);
  expect(result.at(-1)).toEqual(input.at(-1));
});
