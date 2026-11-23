import { test, expect } from "@playwright/test";
import {
  patternLayer,
  patternStarter,
  validatePattern,
  patternSVG,
} from "../src/patternDocument.js";
import {
  pathNodeMode,
  automaticPathNodes,
  movedPathNode,
  movePathAnchors,
  movePathHandle,
  setPathPointMode,
  pathSegment,
  cubicPathPoint,
  nearestPathPoint,
  insertPathPoint,
  bendPathSegment,
  reversePathNodes,
  worldPathNodes,
  withWorldPathNodes,
  joinedPathLayers,
} from "../src/pathEditorGeometry.js";
import {
  worldPathLayer,
  layerWorldPoint,
  reframePatternPath,
} from "../src/shapeEditorGeometry.js";
const close = (a, b) => {
  expect(a.x).toBeCloseTo(b.x, 5);
  expect(a.y).toBeCloseTo(b.y, 5);
};
const curve = [
  { x: 0, y: 0, out: { x: 90, y: 140 }, mode: "smooth" },
  { x: 240, y: 20, in: { x: 170, y: -120 }, mode: "smooth" },
];

test("automatic curvature is finite, tangent continuous and stable with very uneven chords", () => {
  for (const closed of [true, false]) {
    const n = automaticPathNodes(
      [
        { x: 0, y: 0, mode: "auto" },
        { x: 0.001, y: 2, mode: "auto" },
        { x: 450, y: 310, mode: "auto" },
        { x: 440, y: 311, mode: "auto" },
      ],
      closed,
    );
    for (const p of n) {
      for (const q of [p, p.in, p.out].filter(Boolean)) {
        expect(Number.isFinite(q.x)).toBe(true);
        expect(Number.isFinite(q.y)).toBe(true);
      }
      if (p.in && p.out) {
        const a = { x: p.in.x - p.x, y: p.in.y - p.y },
          b = { x: p.out.x - p.x, y: p.out.y - p.y };
        expect(Math.abs(a.x * b.y - a.y * b.x)).toBeLessThan(1e-6);
      }
    }
  }
  const duplicates = automaticPathNodes([
    { x: 0, y: 0, mode: "auto" },
    { x: 0, y: 0, mode: "auto" },
    { x: 1, y: 0, mode: "auto" },
  ]);
  expect(JSON.stringify(duplicates)).not.toContain("null");
});
test("point modes preserve unequal smooth tangents, equal symmetric tangents and independent corners", () => {
  const p = [
    {
      x: 10,
      y: 20,
      in: { x: 0, y: 20 },
      out: { x: 40, y: 20 },
      mode: "smooth",
    },
  ];
  let n = movePathHandle(p, 0, "out", { x: 10, y: 60 });
  close(n[0].in, { x: 10, y: 10 });
  expect(pathNodeMode(n[0])).toBe("smooth");
  n = movePathHandle([{ ...p[0], mode: "symmetric" }], 0, "out", {
    x: 10,
    y: 60,
  });
  close(n[0].in, { x: 10, y: -20 });
  n = movePathHandle(p, 0, "out", { x: 10, y: 60 }, { independent: true });
  close(n[0].in, p[0].in);
  expect(n[0].mode).toBe("corner");
  n = movePathHandle(p, 0, "out", { x: 25, y: 24 }, { angle: true });
  expect(n[0].out.y).toBe(20);
});
test("arbitrary curve insertion preserves every point before and after subdivision", () => {
  for (const t of [0.15, 0.37, 0.8]) {
    const split = insertPathPoint(curve, 0, t),
      original = pathSegment(curve, 0),
      a = pathSegment(split.nodes, 0),
      b = pathSegment(split.nodes, 1);
    for (let i = 0; i <= 50; i++) {
      const u = i / 50;
      close(
        cubicPathPoint(original, u),
        u <= t
          ? cubicPathPoint(a, u / t)
          : cubicPathPoint(b, (u - t) / (1 - t)),
      );
    }
    expect(split.nodes[1].mode).toBe("smooth");
  }
});
test("nearest segment lookup handles curves, physical aspect and closing edges", () => {
  const s = pathSegment(curve, 0),
    p = cubicPathPoint(s, 0.32);
  const near = nearestPathPoint(curve, false, p, 2.4);
  expect(near.t).toBeCloseTo(0.32, 4);
  expect(near.distance).toBeLessThan(0.001);
  const loop = [
    { x: 0, y: 0 },
    { x: 100, y: 0 },
    { x: 100, y: 100 },
  ];
  expect(nearestPathPoint(loop, true, { x: 50, y: 50 }).index).toBe(2);
});
test("bending a straight or curved segment follows the grab point without moving endpoints", () => {
  for (const nodes of [
    curve,
    [
      { x: 0, y: 0 },
      { x: 240, y: 20 },
    ],
  ]) {
    const t = 0.34,
      delta = { x: 12, y: 50 },
      old = cubicPathPoint(pathSegment(nodes, 0), t),
      bent = bendPathSegment(nodes, 0, t, delta);
    close(bent[0], nodes[0]);
    close(bent[1], nodes[1]);
    close(cubicPathPoint(pathSegment(bent, 0), t), {
      x: old.x + 12,
      y: old.y + 50,
    });
  }
});
test("reversing twice restores exact anchors and handles with identical reversed curve geometry", () => {
  const reverse = reversePathNodes(curve);
  expect(reversePathNodes(reverse)).toEqual(curve);
  for (let i = 0; i <= 30; i++)
    close(
      cubicPathPoint(pathSegment(curve, 0), i / 30),
      cubicPathPoint(pathSegment(reverse, 0), 1 - i / 30),
    );
});
test("world-space point edits survive rotated and flipped anisotropic layer reframing", () => {
  const l = patternLayer("path", {
    nodes: curve,
    closed: false,
    paint: true,
    rotation: 38,
    flipX: true,
    width: 240,
    height: 90,
  });
  const world = worldPathNodes(l),
    moved = movePathAnchors(world, [0, 1], { x: 14, y: 10 }),
    edited = withWorldPathNodes(l, moved);
  worldPathNodes(edited).forEach((n, i) => {
    close(n, moved[i]);
    if (n.in) close(n.in, moved[i].in);
    if (n.out) close(n.out, moved[i].out);
  });
  expect(edited.rotation).toBe(38);
  expect(edited.flipX).toBe(true);
  expect(edited.nodes[0].mode).toBe("smooth");
});
test("world paths and reframing retain actual geometry when their bounds exceed the size or center limits", () => {
  const nodes = [
      { x: -500, y: 30, out: { x: 900, y: 400 }, mode: "smooth" },
      { x: 1000, y: 430, in: { x: -350, y: 300 }, mode: "smooth" },
    ],
    l = worldPathLayer(nodes, false, { paint: true });
  expect(l.width).toBeLessThanOrEqual(1024);
  worldPathNodes(l).forEach((n, i) => close(n, nodes[i]));
  const oversized = patternLayer("path", {
    nodes: [
      { x: 1000, y: 20, out: { x: 1800, y: 30 }, mode: "smooth" },
      { x: 2000, y: 200, in: { x: 1700, y: 220 }, mode: "smooth" },
    ],
    width: 300,
    height: 100,
    paint: true,
  });
  const before = worldPathNodes(oversized),
    reframed = reframePatternPath(oversized);
  expect(reframed.x).toBeLessThanOrEqual(1024);
  worldPathNodes(reframed).forEach((n, i) => close(n, before[i]));
});
test("automatic point modes and curve tension round-trip without changing channel coverage", () => {
  const nodes = automaticPathNodes([
      { x: 20, y: 20, mode: "auto" },
      { x: 80, y: 90, mode: "auto" },
      { x: 150, y: 20, mode: "corner" },
    ]),
    l = worldPathLayer(nodes, false, {
      paint: true,
      stroke: "#654321",
      strokeWidth: 5,
      curveTension: 0.75,
    });
  const doc = validatePattern({ ...patternStarter("Blank"), layers: [l] });
  expect(validatePattern(JSON.parse(JSON.stringify(doc)))).toEqual(doc);
  expect(doc.layers[0].nodes[0].mode).toBe("auto");
  expect(doc.layers[0].curveTension).toBe(0.75);
  for (const channel of ["color", "params", "finish"])
    expect(patternSVG(doc, channel)).toContain(" C");
});
test("joining chooses nearest endpoints, merges coincident anchors and preserves world-space curves", () => {
  const a = worldPathLayer(curve, false, {
      paint: true,
      name: "A",
      strokeWidth: 4,
    }),
    b = worldPathLayer(
      [
        { x: 400, y: 300 },
        { x: 240, y: 20, out: { x: 300, y: 60 } },
      ],
      false,
      { paint: true },
    );
  const joined = joinedPathLayers(a, b),
    n = worldPathNodes(joined);
  expect(n).toHaveLength(3);
  close(n[0], curve[0]);
  close(n[1], curve[1]);
  close(n[2], { x: 400, y: 300 });
  close(n[0].out, curve[0].out);
  expect(joined.strokeWidth).toBe(4);
  expect(() => joinedPathLayers({ ...a, closed: true }, b)).toThrow("open");
});
test("multi-anchor moves preserve spacing and respect boundaries as a rigid set", () => {
  const n = [
    { x: 990, y: 20, out: { x: 995, y: 40 } },
    { x: 1010, y: 60 },
  ];
  const moved = movePathAnchors(n, [0, 1], { x: 100, y: 8 });
  expect(moved[1].x).toBe(1024);
  expect(moved[1].x - moved[0].x).toBe(20);
  expect(moved[0].out.x - moved[0].x).toBe(5);
  const smooth = setPathPointMode(curve, [0], "symmetric");
  expect(smooth[0].mode).toBe("symmetric");
  expect(movedPathNode(n[0], { x: 1000, y: 40 }).out).toEqual({
    x: 1005,
    y: 60,
  });
});
