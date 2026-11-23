import { layerBounds } from "./shapeEditorGeometry.js";
import {
  patternPrimitiveNodes,
  patternPathContours,
} from "./patternGeometry.js";
import { layerWorldPoint } from "./shapeEditorGeometry.js";
import {
  nearestPathPoint,
  constrainPathAngle,
  pathSegment,
} from "./pathEditorGeometry.js";

export function createEditorSnapTargets(doc, excludeLayers = []) {
  const excluded = new Set(excludeLayers),
    points = [],
    curves = [],
    axes = { x: [0, 256, 512], y: [0, 256, 512] };
  doc.layers.forEach((layer, index) => {
    if (!layer.visible || excluded.has(index)) return;
    const b = layerBounds(layer);
    axes.x.push(b.minX, b.x, b.maxX);
    axes.y.push(b.minY, b.y, b.maxY);
    points.push({
      x: layer.x,
      y: layer.y,
      label: "Object center",
      layer: index,
    });
    const local = layer.nodes
      ? [{ nodes: layer.nodes, closed: !!layer.closed }]
      : ["path", "text"].includes(layer.kind)
        ? patternPathContours(layer.path)
        : [patternPrimitiveNodes(layer)].filter(Boolean);
    for (const c of local || []) {
      const nodes = c.nodes.map((n) => ({
        ...layerWorldPoint(layer, n),
        ...(n.in ? { in: layerWorldPoint(layer, n.in) } : {}),
        ...(n.out ? { out: layerWorldPoint(layer, n.out) } : {}),
      }));
      addEditorPathSnapTargets(
        { points, curves, axes },
        nodes,
        c.closed,
        [],
        index,
      );
    }
  });
  return { points, curves, axes };
}
export function addEditorPathSnapTargets(
  targets,
  nodes,
  closed = false,
  excludeNodes = [],
  layer = -1,
) {
  const excluded = new Set(excludeNodes);
  nodes.forEach((n, index) => {
    if (!excluded.has(index)) {
      targets.axes.x.push(n.x);
      targets.axes.y.push(n.y);
    }
    if (!excluded.has(index))
      targets.points.push({
        x: n.x,
        y: n.y,
        label:
          !closed && (index === 0 || index === nodes.length - 1)
            ? "Path endpoint"
            : "Anchor",
        layer,
        node: index,
      });
  });
  for (let i = 0; i < nodes.length - (closed ? 0 : 1); i++) {
    const j = (i + 1) % nodes.length;
    if (excluded.has(i) || excluded.has(j)) continue;
    const segment = pathSegment(nodes, i, closed);
    if (!segment) continue;
    const ps = [
      segment.a,
      segment.a.out || segment.a,
      segment.b.in || segment.b,
      segment.b,
    ];
    targets.curves.push({
      nodes: [segment.a, segment.b],
      layer,
      segment: i,
      minX: Math.min(...ps.map((p) => p.x)),
      maxX: Math.max(...ps.map((p) => p.x)),
      minY: Math.min(...ps.map((p) => p.y)),
      maxY: Math.max(...ps.map((p) => p.y)),
    });
  }
  return targets;
}
export function snapEditorPoint(
  raw,
  targets,
  {
    grid = 0,
    enabled = true,
    points = true,
    curves = false,
    guides = true,
    tolerance = 7,
    aspect = 1,
    origin,
    angle = 0,
  } = {},
) {
  const constrained = origin && angle > 0;
  const constrain = (q) => {
    const v = constrainPathAngle(
      { x: origin.x * aspect, y: origin.y },
      { x: q.x * aspect, y: q.y },
      (angle * Math.PI) / 180,
    );
    return { x: v.x / aspect, y: v.y };
  };
  const p = constrained ? constrain(raw) : { ...raw };
  const distance = (a, b) => Math.hypot((a.x - b.x) * aspect, a.y - b.y);
  const onAngle = (q) => !constrained || distance(q, constrain(q)) < 0.0001;
  let best;
  if (enabled && points)
    for (const q of targets.points) {
      const d = distance(p, q);
      if (d <= tolerance && onAngle(q) && (!best || d < best.distance))
        best = {
          point: { x: q.x, y: q.y },
          label: q.label,
          distance: d,
          guides: [
            { axis: "x", value: q.x },
            { axis: "y", value: q.y },
          ],
        };
    }
  if (best) return best;
  if (enabled && curves)
    for (const c of targets.curves) {
      if (
        p.x < c.minX - tolerance / aspect ||
        p.x > c.maxX + tolerance / aspect ||
        p.y < c.minY - tolerance ||
        p.y > c.maxY + tolerance
      )
        continue;
      const n = nearestPathPoint(c.nodes, false, p, aspect);
      if (
        n &&
        n.distance <= tolerance &&
        onAngle(n.point) &&
        (!best || n.distance < best.distance)
      )
        best = {
          point: n.point,
          label: "Curve / edge",
          distance: n.distance,
          guides: [],
        };
    }
  if (best) return best;
  let result = {
      x: grid ? Math.round(p.x / grid) * grid : p.x,
      y: grid ? Math.round(p.y / grid) * grid : p.y,
    },
    lines = [];
  if (constrained) result = constrain(result);
  if (enabled && guides) {
    for (const axis of ["x", "y"]) {
      let closest = tolerance + 1,
        value;
      for (const t of targets.axes[axis]) {
        const d = Math.abs(t - p[axis]) * (axis === "x" ? aspect : 1);
        if (d < closest) {
          closest = d;
          value = t;
        }
      }
      if (closest <= tolerance) {
        const candidate = { ...result, [axis]: value };
        if (onAngle(candidate)) {
          result = candidate;
          lines.push({ axis, value });
        }
      }
    }
  }
  return {
    point: result,
    label: lines.length ? "Alignment guide" : grid ? "Grid" : "",
    guides: lines,
    distance: distance(raw, result),
  };
}
