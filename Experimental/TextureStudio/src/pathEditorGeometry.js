// Editing is performed in document space. Stored nodes remain in the motif's
// local box, so rotated/flipped/anisotropic paths retain their original geometry.
import { patternNodesPath } from "./patternGeometry.js";
import {
  layerLocalPoint,
  layerWorldPoint,
  reframePatternPath,
  worldPathLayer,
  clamp,
} from "./shapeEditorGeometry.js";

export const pointModes = ["corner", "smooth", "symmetric", "auto"];
const copy = (v) => structuredClone(v);
const length = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
const lerp = (a, b, t) => ({
  x: a.x + (b.x - a.x) * t,
  y: a.y + (b.y - a.y) * t,
});
export function pathNodeMode(n) {
  if (pointModes.includes(n.mode)) return n.mode;
  if (!n.in || !n.out) return "corner";
  const a = { x: n.in.x - n.x, y: n.in.y - n.y },
    b = { x: n.out.x - n.x, y: n.out.y - n.y },
    la = Math.hypot(a.x, a.y),
    lb = Math.hypot(b.x, b.y);
  if (
    la < 1e-7 ||
    lb < 1e-7 ||
    a.x * b.x + a.y * b.y >= 0 ||
    Math.abs(a.x * b.y - a.y * b.x) > la * lb * 1e-5
  )
    return "corner";
  return Math.abs(la - lb) < 1e-5 * Math.max(la, lb) ? "symmetric" : "smooth";
}
export function worldPathNodes(layer) {
  return (layer.nodes || []).map((n) => ({
    ...layerWorldPoint(layer, n),
    ...(n.mode ? { mode: n.mode } : {}),
    ...(n.in ? { in: layerWorldPoint(layer, n.in) } : {}),
    ...(n.out ? { out: layerWorldPoint(layer, n.out) } : {}),
  }));
}
export function withWorldPathNodes(layer, nodes, reframe = true) {
  const local = nodes.map((n) => ({
    ...layerLocalPoint(layer, n),
    ...(n.mode ? { mode: n.mode } : {}),
    ...(n.in ? { in: layerLocalPoint(layer, n.in) } : {}),
    ...(n.out ? { out: layerLocalPoint(layer, n.out) } : {}),
  }));
  const next = {
    ...layer,
    nodes: local,
    path: patternNodesPath(local, layer.closed),
  };
  return reframe ? reframePatternPath(next) : next;
}
// Centripetal Catmull–Rom derivatives, expressed as cubic Bézier controls. The
// square-root chord parameter handles uneven spacing; duplicate points cannot
// cause divisions by zero. Only automatic points are regenerated.
export function automaticPathNodes(input, closed = false, tension = 1) {
  const nodes = copy(input),
    strength = clamp(Number(tension) || 0, 0, 1);
  if (nodes.length < 2) return nodes;
  nodes.forEach((n, i) => {
    if (n.mode !== "auto") return;
    const prev =
      nodes[i - 1] ||
      (closed
        ? nodes.at(-1)
        : { x: 2 * n.x - nodes[1].x, y: 2 * n.y - nodes[1].y });
    const next =
      nodes[i + 1] ||
      (closed
        ? nodes[0]
        : { x: 2 * n.x - nodes.at(-2).x, y: 2 * n.y - nodes.at(-2).y });
    const d0 = Math.max(1e-6, Math.sqrt(length(prev, n))),
      d1 = Math.max(1e-6, Math.sqrt(length(n, next)));
    const tangent = {
      x: (((n.x - prev.x) * d1) / d0 + ((next.x - n.x) * d0) / d1) / (d0 + d1),
      y: (((n.y - prev.y) * d1) / d0 + ((next.y - n.y) * d0) / d1) / (d0 + d1),
    };
    if (closed || i > 0)
      n.in = {
        x: n.x - (tangent.x * d0 * strength) / 3,
        y: n.y - (tangent.y * d0 * strength) / 3,
      };
    else delete n.in;
    if (closed || i < nodes.length - 1)
      n.out = {
        x: n.x + (tangent.x * d1 * strength) / 3,
        y: n.y + (tangent.y * d1 * strength) / 3,
      };
    else delete n.out;
  });
  return nodes;
}
export function movedPathNode(n, p) {
  const dx = p.x - n.x,
    dy = p.y - n.y;
  return {
    ...n,
    ...p,
    ...(n.in ? { in: { x: n.in.x + dx, y: n.in.y + dy } } : {}),
    ...(n.out ? { out: { x: n.out.x + dx, y: n.out.y + dy } } : {}),
  };
}
export function movePathAnchors(
  nodes,
  indices,
  delta,
  closed = false,
  tension = 1,
) {
  const chosen = new Set(indices),
    selected = nodes.filter((_, i) => chosen.has(i));
  if (!selected.length) return copy(nodes);
  const dx = clamp(
      delta.x,
      -512 - Math.min(...selected.map((n) => n.x)),
      1024 - Math.max(...selected.map((n) => n.x)),
    ),
    dy = clamp(
      delta.y,
      -512 - Math.min(...selected.map((n) => n.y)),
      1024 - Math.max(...selected.map((n) => n.y)),
    );
  return automaticPathNodes(
    nodes.map((n, i) =>
      chosen.has(i) ? movedPathNode(n, { x: n.x + dx, y: n.y + dy }) : n,
    ),
    closed,
    tension,
  );
}
export function constrainPathAngle(anchor, p, increment = Math.PI / 4) {
  const a =
      Math.round(Math.atan2(p.y - anchor.y, p.x - anchor.x) / increment) *
      increment,
    d = length(anchor, p);
  return { x: anchor.x + Math.cos(a) * d, y: anchor.y + Math.sin(a) * d };
}
export function movePathHandle(
  input,
  index,
  side,
  p,
  { independent = false, angle = false } = {},
) {
  const nodes = copy(input),
    n = nodes[index];
  if (!n || !["in", "out"].includes(side)) return nodes;
  const other = side === "in" ? "out" : "in",
    previousLength = n[other] ? length(n, n[other]) : 0;
  let mode = pathNodeMode(n);
  if (independent) mode = "corner";
  else if (mode === "auto") mode = "smooth";
  p = angle ? constrainPathAngle(n, p) : p;
  n[side] = { x: p.x, y: p.y };
  n.mode = mode;
  const d = length(n, p);
  if (mode === "symmetric") n[other] = { x: 2 * n.x - p.x, y: 2 * n.y - p.y };
  else if (mode === "smooth" && previousLength && d > 1e-7)
    n[other] = {
      x: n.x - ((p.x - n.x) * previousLength) / d,
      y: n.y - ((p.y - n.y) * previousLength) / d,
    };
  return nodes;
}
export function setPathPointMode(
  input,
  indices,
  mode,
  closed = false,
  tension = 1,
  collapse = false,
) {
  const nodes = copy(input);
  if (!pointModes.includes(mode)) return nodes;
  for (const i of indices) {
    const n = nodes[i];
    if (!n) continue;
    n.mode = mode;
    if (mode === "corner") {
      if (collapse) {
        delete n.in;
        delete n.out;
      }
      continue;
    }
    if (mode === "auto") continue;
    const prev = nodes[i - 1] || (closed ? nodes.at(-1) : n),
      next = nodes[i + 1] || (closed ? nodes[0] : n);
    let vector = n.out
      ? { x: n.out.x - n.x, y: n.out.y - n.y }
      : n.in
        ? { x: n.x - n.in.x, y: n.y - n.in.y }
        : { x: next.x - prev.x, y: next.y - prev.y };
    const d = Math.hypot(vector.x, vector.y) || 1;
    const l0 = n.in ? length(n, n.in) : length(n, prev) / 3,
      l1 = n.out ? length(n, n.out) : length(n, next) / 3;
    const common = (l0 && l1 ? (l0 + l1) / 2 : l0 || l1) || 20,
      a = mode === "symmetric" ? common : l0,
      b = mode === "symmetric" ? common : l1;
    if (closed || i > 0 || n.in)
      n.in = { x: n.x - (vector.x / d) * a, y: n.y - (vector.y / d) * a };
    if (closed || i < nodes.length - 1 || n.out)
      n.out = { x: n.x + (vector.x / d) * b, y: n.y + (vector.y / d) * b };
  }
  return automaticPathNodes(nodes, closed, tension);
}
export function pathSegment(nodes, index, closed = false) {
  if (index < 0 || index >= nodes.length - (closed ? 0 : 1)) return null;
  const a = nodes[index],
    b = nodes[(index + 1) % nodes.length];
  return {
    a,
    b,
    p0: a,
    p1: a.out || a,
    p2: b.in || b,
    p3: b,
    curved: !!(a.out || b.in),
  };
}
export function cubicPathPoint(segment, t) {
  const { p0, p1, p2, p3 } = segment,
    u = 1 - t;
  if (!segment.curved) return lerp(p0, p3, t);
  return {
    x:
      u * u * u * p0.x +
      3 * u * u * t * p1.x +
      3 * u * t * t * p2.x +
      t * t * t * p3.x,
    y:
      u * u * u * p0.y +
      3 * u * u * t * p1.y +
      3 * u * t * t * p2.y +
      t * t * t * p3.y,
  };
}
export function nearestPathPoint(nodes, closed, point, aspect = 1) {
  let best = null;
  const distance = (p) =>
    ((p.x - point.x) * aspect) ** 2 + (p.y - point.y) ** 2;
  for (let index = 0; index < nodes.length - (closed ? 0 : 1); index++) {
    const segment = pathSegment(nodes, index, closed);
    let t = 0,
      error = Infinity;
    if (!segment.curved) {
      const dx = (segment.p3.x - segment.p0.x) * aspect,
        dy = segment.p3.y - segment.p0.y;
      t = clamp(
        ((point.x - segment.p0.x) * aspect * dx +
          (point.y - segment.p0.y) * dy) /
          (dx * dx + dy * dy || 1),
        0,
        1,
      );
    } else {
      // Bracket every local minimum, not just a single Newton seed. Loops and
      // S-curves may have several nearest candidates.
      const samples = 32,
        values = Array.from({ length: samples + 1 }, (_, i) =>
          distance(cubicPathPoint(segment, i / samples)),
        );
      for (let i = 0; i <= samples; i++) {
        if (
          (i > 0 && values[i] > values[i - 1]) ||
          (i < samples && values[i] > values[i + 1])
        )
          continue;
        let lo = Math.max(0, (i - 1) / samples),
          hi = Math.min(1, (i + 1) / samples);
        for (let j = 0; j < 25; j++) {
          const x = lo + (hi - lo) / 3,
            y = hi - (hi - lo) / 3;
          if (
            distance(cubicPathPoint(segment, x)) <
            distance(cubicPathPoint(segment, y))
          )
            hi = y;
          else lo = x;
        }
        const candidate = (lo + hi) / 2,
          e = distance(cubicPathPoint(segment, candidate));
        if (e < error) {
          t = candidate;
          error = e;
        }
      }
    }
    const p = cubicPathPoint(segment, t);
    error = distance(p);
    if (!best || error < best.error)
      best = { index, t, point: p, distance: Math.sqrt(error), error };
  }
  return best;
}
export function insertPathPoint(input, index, t = 0.5, closed = false) {
  if (input.length >= 400)
    throw new Error("An editable path supports up to 400 points.");
  const nodes = copy(input),
    s = pathSegment(nodes, index, closed);
  if (!s) throw new Error("Select a path segment to insert a point.");
  t = clamp(t, 0.0001, 0.9999);
  const q0 = lerp(s.p0, s.p1, t),
    q1 = lerp(s.p1, s.p2, t),
    q2 = lerp(s.p2, s.p3, t),
    r0 = lerp(q0, q1, t),
    r1 = lerp(q1, q2, t),
    m = s.curved ? lerp(r0, r1, t) : lerp(s.a, s.b, t);
  if (s.curved) {
    s.a.out = q0;
    s.b.in = q2;
    if (s.a.mode === "auto") s.a.mode = "smooth";
    if (s.b.mode === "auto") s.b.mode = "smooth";
  }
  nodes.splice(index + 1, 0, {
    ...m,
    mode: s.curved ? "smooth" : "corner",
    ...(s.curved ? { in: r0, out: r1 } : {}),
  });
  return { nodes, index: index + 1 };
}
export function bendPathSegment(input, index, t, delta, closed = false) {
  const nodes = copy(input),
    s = pathSegment(nodes, index, closed);
  if (!s) return nodes;
  // Moving both controls by d/[3t(1-t)] moves the point at t by exactly d,
  // without moving either endpoint. Clamp near endpoints to avoid instability.
  t = clamp(t, 0.1, 0.9);
  const weight = 3 * t * (1 - t);
  const p1 = s.curved ? s.p1 : lerp(s.p0, s.p3, 1 / 3),
    p2 = s.curved ? s.p2 : lerp(s.p0, s.p3, 2 / 3);
  s.a.out = { x: p1.x + delta.x / weight, y: p1.y + delta.y / weight };
  s.b.in = { x: p2.x + delta.x / weight, y: p2.y + delta.y / weight };
  s.a.mode = "corner";
  s.b.mode = "corner";
  return nodes;
}
export function reversePathNodes(input) {
  return copy(input)
    .reverse()
    .map((n) => {
      const result = { x: n.x, y: n.y, ...(n.mode ? { mode: n.mode } : {}) };
      if (n.out) result.in = n.out;
      if (n.in) result.out = n.in;
      return result;
    });
}
export function pathGeometryKey(layer) {
  if (!layer) return "";
  return JSON.stringify([
    layer.kind,
    layer.nodes,
    layer.path,
    layer.closed,
    layer.x,
    layer.y,
    layer.width,
    layer.height,
    layer.rotation,
    layer.flipX,
    layer.flipY,
    layer.locked,
    layer.visible,
  ]);
}
export function joinedPathLayers(a, b, tolerance = 0.01) {
  if (!a.nodes || !b.nodes || a.closed || b.closed)
    throw new Error("Join requires two open, editable paths.");
  let x = worldPathNodes(a),
    y = worldPathNodes(b),
    best;
  for (const ra of [false, true])
    for (const rb of [false, true]) {
      const d = length(ra ? x[0] : x.at(-1), rb ? y.at(-1) : y[0]);
      if (!best || d < best.d) best = { ra, rb, d };
    }
  if (best.ra) x = reversePathNodes(x);
  if (best.rb) y = reversePathNodes(y);
  if (best.d <= tolerance) {
    const end = x.at(-1),
      start = y[0];
    if (start.out) end.out = start.out;
    else delete end.out;
    end.mode = "corner";
    y.shift();
  }
  if (x.length + y.length > 400)
    throw new Error("Joined paths cannot exceed 400 points.");
  return worldPathLayer([...x, ...y], false, {
    ...a,
    rotation: 0,
    flipX: false,
    flipY: false,
    name: `${a.name} joined`.slice(0, 60),
    paint: true,
    fillEnabled: false,
  });
}
