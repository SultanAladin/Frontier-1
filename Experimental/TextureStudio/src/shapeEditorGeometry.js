import { patternLayer } from "./patternDocument.js";
import {
  patternPrimitiveNodes,
  patternNodesPath,
  patternPathContours,
} from "./patternGeometry.js";

export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export const snap = (v, grid) => (grid ? Math.round(v / grid) * grid : v);
const radians = (v) => (v * Math.PI) / 180;
const rotate = (p, angle) => ({
  x: p.x * Math.cos(angle) - p.y * Math.sin(angle),
  y: p.x * Math.sin(angle) + p.y * Math.cos(angle),
});
export function layerLocalPoint(layer, world) {
  const p = rotate(
    { x: world.x - layer.x, y: world.y - layer.y },
    -radians(layer.rotation),
  );
  return {
    x: 50 + (p.x * 100) / (layer.width * (layer.flipX ? -1 : 1)),
    y: 50 + (p.y * 100) / (layer.height * (layer.flipY ? -1 : 1)),
  };
}
export function layerWorldPoint(layer, local) {
  const p = rotate(
    {
      x: ((local.x - 50) * layer.width * (layer.flipX ? -1 : 1)) / 100,
      y: ((local.y - 50) * layer.height * (layer.flipY ? -1 : 1)) / 100,
    },
    radians(layer.rotation),
  );
  return { x: layer.x + p.x, y: layer.y + p.y };
}
export function layerBounds(layer) {
  const corners = [
    { x: 0, y: 0 },
    { x: 100, y: 0 },
    { x: 100, y: 100 },
    { x: 0, y: 100 },
  ].map((p) => layerWorldPoint(layer, p));
  return pointsBounds(corners);
}
export function pointsBounds(points) {
  if (!points.length) return null;
  const minX = Math.min(...points.map((p) => p.x)),
    minY = Math.min(...points.map((p) => p.y)),
    maxX = Math.max(...points.map((p) => p.x)),
    maxY = Math.max(...points.map((p) => p.y));
  return {
    minX,
    minY,
    maxX,
    maxY,
    x: (minX + maxX) / 2,
    y: (minY + maxY) / 2,
    width: maxX - minX,
    height: maxY - minY,
  };
}
export function selectionBounds(doc, ids) {
  return pointsBounds(
    ids
      .filter((i) => doc.layers[i])
      .flatMap((i) => {
        const b = layerBounds(doc.layers[i]);
        return [
          { x: b.minX, y: b.minY },
          { x: b.maxX, y: b.maxY },
        ];
      }),
  );
}
export function editableSelection(doc, ids) {
  return [...new Set(ids)].filter(
    (i) => doc.layers[i] && !doc.layers[i].locked && doc.layers[i].visible,
  );
}
export function expandPatternSelection(doc, index) {
  const layer = doc.layers[index];
  if (!layer || layer.locked || !layer.visible) return [];
  return layer.group
    ? doc.layers.flatMap((l, i) =>
        l.group === layer.group && !l.locked && l.visible ? [i] : [],
      )
    : [index];
}
export function translateSelection(doc, ids, dx, dy) {
  const selected = new Set(ids),
    layers = ids.map((i) => doc.layers[i]).filter(Boolean);
  if (!layers.length) return doc;
  dx = clamp(
    dx,
    -512 - Math.min(...layers.map((l) => l.x)),
    1024 - Math.max(...layers.map((l) => l.x)),
  );
  dy = clamp(
    dy,
    -512 - Math.min(...layers.map((l) => l.y)),
    1024 - Math.max(...layers.map((l) => l.y)),
  );
  return {
    ...doc,
    layers: doc.layers.map((l, i) =>
      selected.has(i) ? { ...l, x: l.x + dx, y: l.y + dy } : l,
    ),
  };
}
export function snappedTranslation(
  doc,
  ids,
  delta,
  { grid = 0, guides = true, tolerance = 5 } = {},
) {
  const b = selectionBounds(doc, ids);
  if (!b) return { delta, guides: [] };
  // Snap the active object's center, not each object independently: group spacing survives.
  const primary = doc.layers[ids.at(-1)];
  let dx = snap(primary.x + delta.x, grid) - primary.x,
    dy = snap(primary.y + delta.y, grid) - primary.y;
  const lines = [];
  if (guides) {
    const selected = new Set(ids),
      targets = { x: [0, 256, 512], y: [0, 256, 512] };
    doc.layers.forEach((l, i) => {
      if (selected.has(i) || !l.visible) return;
      const t = layerBounds(l);
      targets.x.push(t.minX, t.x, t.maxX);
      targets.y.push(t.minY, t.y, t.maxY);
    });
    for (const axis of ["x", "y"]) {
      const offset = axis === "x" ? dx : dy,
        candidates =
          axis === "x" ? [b.minX, b.x, b.maxX] : [b.minY, b.y, b.maxY];
      let distance = tolerance + 1,
        correction = 0,
        line;
      for (const target of targets[axis])
        for (const value of candidates) {
          const difference = target - value - offset;
          if (Math.abs(difference) < distance) {
            distance = Math.abs(difference);
            correction = difference;
            line = target;
          }
        }
      if (distance <= tolerance) {
        if (axis === "x") dx += correction;
        else dy += correction;
        lines.push({ axis, value: line });
      }
    }
  }
  return { delta: { x: dx, y: dy }, guides: lines };
}
export function resizeSelection(
  doc,
  ids,
  handle,
  point,
  { aspect = false, center = false, grid = 0 } = {},
) {
  if (!ids.length) return doc;
  const single = ids.length === 1,
    original = single
      ? doc.layers[ids[0]]
      : { ...selectionBounds(doc, ids), rotation: 0 },
    angle = radians(original.rotation),
    local = rotate(
      {
        x: snap(point.x, grid) - original.x,
        y: snap(point.y, grid) - original.y,
      },
      -angle,
    ),
    hx = handle.includes("e") ? 1 : handle.includes("w") ? -1 : 0,
    hy = handle.includes("s") ? 1 : handle.includes("n") ? -1 : 0,
    fixedX = center ? 0 : (-hx * original.width) / 2,
    fixedY = center ? 0 : (-hy * original.height) / 2;
  let w = hx
      ? Math.max(1, (local.x - fixedX) * hx * (center ? 2 : 1))
      : original.width,
    h = hy
      ? Math.max(1, (local.y - fixedY) * hy * (center ? 2 : 1))
      : original.height;
  // A nonuniform group transform would shear rotated SVG motifs. Constrain it
  // rather than silently approximating a transform the portable schema cannot represent.
  const uniform =
    aspect ||
    (!single && ids.some((i) => Math.abs(doc.layers[i].rotation % 90) > 0.001));
  if (uniform) {
    let scale =
      hx && hy
        ? Math.max(w / original.width, h / original.height)
        : hx
          ? w / original.width
          : h / original.height;
    const maxScale = Math.min(
      ...ids.flatMap((i) => [
        1024 / doc.layers[i].width,
        1024 / doc.layers[i].height,
      ]),
    );
    scale = clamp(
      scale,
      Math.max(1 / original.width, 1 / original.height),
      maxScale,
    );
    w = original.width * scale;
    h = original.height * scale;
  }
  if (single) {
    w = clamp(w, 1, 1024);
    h = clamp(h, 1, 1024);
  } else {
    const maxW =
        Math.min(...ids.map((i) => 1024 / doc.layers[i].width)) *
        original.width,
      maxH =
        Math.min(...ids.map((i) => 1024 / doc.layers[i].height)) *
        original.height;
    w = Math.min(w, maxW);
    h = Math.min(h, maxH);
  }
  const offset = rotate(
      {
        x: center || !hx ? 0 : fixedX + (hx * w) / 2,
        y: center || !hy ? 0 : fixedY + (hy * h) / 2,
      },
      angle,
    ),
    newX = original.x + offset.x,
    newY = original.y + offset.y,
    selected = new Set(ids),
    sx = w / Math.max(1, original.width),
    sy = h / Math.max(1, original.height);
  return {
    ...doc,
    layers: doc.layers.map((l, i) =>
      !selected.has(i)
        ? l
        : single
          ? { ...l, x: newX, y: newY, width: w, height: h }
          : {
              ...l,
              x: newX + (l.x - original.x) * sx,
              y: newY + (l.y - original.y) * sy,
              width: Math.max(1, l.width * sx),
              height: Math.max(1, l.height * sy),
            },
    ),
  };
}
export function rotateSelection(doc, ids, start, point, constrained = false) {
  const b = ids.length === 1 ? doc.layers[ids[0]] : selectionBounds(doc, ids);
  if (!b) return doc;
  let delta =
    ((Math.atan2(point.y - b.y, point.x - b.x) -
      Math.atan2(start.y - b.y, start.x - b.x)) *
      180) /
    Math.PI;
  delta = ((delta + 540) % 360) - 180;
  const base = ids.length === 1 ? b.rotation : 0;
  delta = snap(base + delta, constrained ? 15 : 1) - base;
  const selected = new Set(ids);
  return {
    ...doc,
    layers: doc.layers.map((l, i) => {
      if (!selected.has(i)) return l;
      const p = rotate({ x: l.x - b.x, y: l.y - b.y }, radians(delta));
      return {
        ...l,
        x: b.x + p.x,
        y: b.y + p.y,
        rotation: ((l.rotation + delta + 540) % 360) - 180,
      };
    }),
  };
}
function selectionUnits(doc, ids) {
  const units = new Map();
  ids.forEach((i) => {
    const l = doc.layers[i],
      key = l.group || `index-${i}`;
    if (!units.has(key)) units.set(key, []);
    units.get(key).push(i);
  });
  return [...units.values()].map((indices) => ({
    indices,
    bounds: selectionBounds(doc, indices),
  }));
}
export function alignSelection(doc, ids, alignment) {
  const units = selectionUnits(doc, ids);
  if (!units.length) return doc;
  const b =
    units.length === 1
      ? { minX: 0, minY: 0, maxX: 512, maxY: 512, x: 256, y: 256 }
      : selectionBounds(doc, ids);
  const keys = {
      left: "minX",
      "center-x": "x",
      right: "maxX",
      top: "minY",
      "center-y": "y",
      bottom: "maxY",
    },
    key = keys[alignment];
  if (!key) return doc;
  return units.reduce((next, unit) => {
    const amount = b[key] - unit.bounds[key],
      horizontal = ["minX", "x", "maxX"].includes(key);
    return translateSelection(
      next,
      unit.indices,
      horizontal ? amount : 0,
      horizontal ? 0 : amount,
    );
  }, doc);
}
export function distributeSelection(doc, ids, axis) {
  const units = selectionUnits(doc, ids);
  if (units.length < 3) return doc;
  const horizontal = axis === "x",
    min = horizontal ? "minX" : "minY",
    max = horizontal ? "maxX" : "maxY",
    size = horizontal ? "width" : "height";
  units.sort((a, b) => a.bounds[min] - b.bounds[min]);
  const start = units[0].bounds[min],
    end = units.at(-1).bounds[max],
    gap =
      (end - start - units.reduce((sum, u) => sum + u.bounds[size], 0)) /
      (units.length - 1);
  let cursor = start,
    next = doc;
  for (const unit of units) {
    const delta = cursor - unit.bounds[min];
    next = translateSelection(
      next,
      unit.indices,
      horizontal ? delta : 0,
      horizontal ? 0 : delta,
    );
    cursor += unit.bounds[size] + gap;
  }
  return next;
}
export function reorderSelection(doc, ids, direction) {
  const selected = new Set(ids);
  let entries = doc.layers.map((l, i) => ({ layer: l, index: i }));
  if (direction === "front")
    entries = [
      ...entries.filter((e) => !selected.has(e.index)),
      ...entries.filter((e) => selected.has(e.index)),
    ];
  else if (direction === "back")
    entries = [
      ...entries.filter((e) => selected.has(e.index)),
      ...entries.filter((e) => !selected.has(e.index)),
    ];
  else if (direction === "forward") {
    for (let i = entries.length - 2; i >= 0; i--)
      if (selected.has(entries[i].index) && !selected.has(entries[i + 1].index))
        [entries[i], entries[i + 1]] = [entries[i + 1], entries[i]];
  } else {
    for (let i = 1; i < entries.length; i++)
      if (selected.has(entries[i].index) && !selected.has(entries[i - 1].index))
        [entries[i], entries[i - 1]] = [entries[i - 1], entries[i]];
  }
  return {
    doc: { ...doc, layers: entries.map((e) => e.layer) },
    selected: entries.flatMap((e, i) => (selected.has(e.index) ? [i] : [])),
  };
}
let groupCounter = 0;
export const newPatternGroup = () =>
  `group-${Date.now().toString(36)}-${++groupCounter}`;
export function clonePatternLayers(layers, offset = 18) {
  const groups = new Map(), LinkedInstances = new Map();
  return layers.map((l) => {
    if (l.group && !groups.has(l.group)) groups.set(l.group, newPatternGroup());
    if (l.LinkedInstance && !LinkedInstances.has(l.LinkedInstance)) LinkedInstances.set(l.LinkedInstance, newPatternGroup());
    return {
      ...structuredClone(l),
      EnclosureToken: undefined,
      StructureToken: undefined,
      ...(l.LinkedInstance ? { LinkedInstance: LinkedInstances.get(l.LinkedInstance) } : {}),
      name: (l.name + " copy").slice(0, 60),
      x: clamp(l.x + offset, -512, 1024),
      y: clamp(l.y + offset, -512, 1024),
      locked: false,
      ...(l.group ? { group: groups.get(l.group) } : {}),
    };
  });
}
export function patternCopies(doc, layer) {
  const copies = [],
    b =
      (layer.width + layer.height) / 2 +
      (layer.paint
        ? layer.strokeWidth *
          (layer.strokeLinejoin === "miter"
            ? 2
            : layer.strokeLinecap === "square"
              ? Math.SQRT1_2
              : 0.5)
        : ((layer.strokeWidth || 0) * Math.max(layer.width, layer.height)) /
          200);
  for (let col = -4; col <= 4; col++)
    for (let row = -4; row <= 4; row++) {
      if (
        (["y", "none"].includes(doc.tileAxes) && col !== 0) ||
        (["x", "none"].includes(doc.tileAxes) && row !== 0)
      )
        continue;
      const sx = doc.repeat === "mirror" && Math.abs(col % 2) ? -1 : 1,
        sy = doc.repeat === "mirror" && Math.abs(row % 2) ? -1 : 1,
        x = col * 512 + (sx < 0 ? 512 - layer.x : layer.x),
        y =
          row * 512 +
          (sy < 0 ? 512 - layer.y : layer.y) +
          (doc.repeat === "half-drop" && Math.abs(col % 2) ? 256 : 0);
      if (x + b < 0 || x - b > 512 || y + b < 0 || y - b > 512) continue;
      copies.push({ x, y, sx, sy, col, row });
    }
  return copies;
}
// Exact cubic extrema keep normalization tight, without trimming Bézier overshoot.
export function pathNodesBounds(nodes, closed = false) {
  const points = nodes.map(({ x, y }) => ({ x, y }));
  const cubic = (a, b, c, d, t) =>
    (1 - t) ** 3 * a +
    3 * (1 - t) ** 2 * t * b +
    3 * (1 - t) * t * t * c +
    t ** 3 * d;
  for (let i = 0; i < nodes.length - (closed ? 0 : 1); i++) {
    const a = nodes[i],
      d = nodes[(i + 1) % nodes.length],
      b = a.out || a,
      c = d.in || d;
    if (!a.out && !d.in) continue;
    for (const axis of ["x", "y"]) {
      const A = -a[axis] + 3 * b[axis] - 3 * c[axis] + d[axis],
        B = 2 * (a[axis] - 2 * b[axis] + c[axis]),
        C = b[axis] - a[axis],
        discriminant = B * B - 4 * A * C,
        roots =
          Math.abs(A) < 1e-8
            ? Math.abs(B) < 1e-8
              ? []
              : [-C / B]
            : discriminant < 0
              ? []
              : [
                  (-B + Math.sqrt(discriminant)) / (2 * A),
                  (-B - Math.sqrt(discriminant)) / (2 * A),
                ];
      for (const t of roots.filter((v) => v > 0 && v < 1))
        points.push({
          x: cubic(a.x, b.x, c.x, d.x, t),
          y: cubic(a.y, b.y, c.y, d.y, t),
        });
    }
  }
  return pointsBounds(points);
}
export function reframePatternPath(layer) {
  if (!layer.nodes?.length) return layer;
  const b = pathNodesBounds(layer.nodes, layer.closed),
    desired = layerWorldPoint(layer, b),
    center = {
      x: clamp(desired.x, -512, 1024),
      y: clamp(desired.y, -512, 1024),
    },
    origin =
      center.x === desired.x && center.y === desired.y
        ? b
        : layerLocalPoint(layer, center),
    width = clamp((layer.width * b.width) / 100, 1, 1024),
    height = clamp((layer.height * b.height) / 100, 1, 1024),
    local = (p) => ({
      x: 50 + ((p.x - origin.x) * layer.width) / width,
      y: 50 + ((p.y - origin.y) * layer.height) / height,
    }),
    nodes = layer.nodes.map((n) => ({
      ...local(n),
      ...(n.mode ? { mode: n.mode } : {}),
      ...(n.in ? { in: local(n.in) } : {}),
      ...(n.out ? { out: local(n.out) } : {}),
    }));
  return {
    ...layer,
    x: center.x,
    y: center.y,
    width,
    height,
    nodes,
    path: patternNodesPath(nodes, layer.closed),
  };
}
export function worldPathLayer(nodes, closed, style = {}) {
  const b = pathNodesBounds(nodes, closed),
    x = clamp(b.x, -512, 1024),
    y = clamp(b.y, -512, 1024),
    width = clamp(b.width, 1, 1024),
    height = clamp(b.height, 1, 1024),
    normalized = (p) => ({
      x: ((p.x - x) / width) * 100 + 50,
      y: ((p.y - y) / height) * 100 + 50,
    }),
    local = nodes.map((n) => ({
      ...normalized(n),
      ...(n.mode ? { mode: n.mode } : {}),
      ...(n.in ? { in: normalized(n.in) } : {}),
      ...(n.out ? { out: normalized(n.out) } : {}),
    }));
  return patternLayer("path", {
    ...style,
    x,
    y,
    width,
    height,
    nodes: local,
    closed,
    path: patternNodesPath(local, closed),
  });
}
export function drawnPatternLayer(
  tool,
  start,
  end,
  style,
  constrained = false,
  fromCenter = false,
) {
  if (tool === "line") {
    let p = { ...end };
    if (constrained) {
      const angle = snap(Math.atan2(p.y - start.y, p.x - start.x), Math.PI / 4),
        length = Math.hypot(p.x - start.x, p.y - start.y);
      p = {
        x: start.x + Math.cos(angle) * length,
        y: start.y + Math.sin(angle) * length,
      };
    }
    return worldPathLayer([start, p], false, {
      ...style,
      name: "Line",
      fillEnabled: false,
      strokeWidth: style.strokeWidth || 1.4,
      stroke: style.strokeWidth ? style.stroke : style.color,
    });
  }
  let dx = end.x - start.x,
    dy = end.y - start.y;
  if (constrained) {
    const side = Math.max(Math.abs(dx), Math.abs(dy));
    dx = side * (Math.sign(dx) || 1);
    dy = side * (Math.sign(dy) || 1);
  }
  const maxDelta = fromCenter ? 512 : 1024;
  dx = clamp(dx, -maxDelta, maxDelta);
  dy = clamp(dy, -maxDelta, maxDelta);
  const names = {
    rect: "Rectangle",
    ellipse: "Ellipse",
    diamond: "Diamond",
    triangle: "Triangle",
    polygon: "Polygon",
    star: "Star",
    flower: "Flower",
    arc: "Arc",
  };
  return patternLayer(tool, {
    ...style,
    name: names[tool] || tool,
    x: fromCenter ? start.x : start.x + dx / 2,
    y: fromCenter ? start.y : start.y + dy / 2,
    width: Math.max(1, Math.abs(dx) * (fromCenter ? 2 : 1)),
    height: Math.max(1, Math.abs(dy) * (fromCenter ? 2 : 1)),
    ...(tool === "arc"
      ? {
          arcStart: style.arcStart ?? -90,
          arcSweep: style.arcSweep ?? 180,
          arcClosure: style.arcClosure || "open",
          fillEnabled:
            (style.arcClosure || "open") !== "open" &&
            style.fillEnabled !== false,
          strokeWidth: style.strokeWidth || 1.4,
          stroke: style.strokeWidth ? style.stroke : style.color,
        }
      : {}),
    ...(tool === "polygon" ? { sides: style.sides || 6 } : {}),
    ...(tool === "star"
      ? {
          starPoints: style.starPoints || 5,
          innerRadius: style.innerRadius || 0.45,
        }
      : {}),
  });
}
export function simplifyDrawnPoints(points, tolerance = 1.2) {
  if (points.length <= 2) return points;
  const a = points[0],
    b = points.at(-1),
    dx = b.x - a.x,
    dy = b.y - a.y,
    length = dx * dx + dy * dy;
  let furthest = tolerance * tolerance,
    index = -1;
  for (let i = 1; i < points.length - 1; i++) {
    const t = length
        ? clamp(
            ((points[i].x - a.x) * dx + (points[i].y - a.y) * dy) / length,
            0,
            1,
          )
        : 0,
      distance =
        (points[i].x - a.x - t * dx) ** 2 + (points[i].y - a.y - t * dy) ** 2;
    if (distance > furthest) {
      furthest = distance;
      index = i;
    }
  }
  return index < 0
    ? [a, b]
    : [
        ...simplifyDrawnPoints(points.slice(0, index + 1), tolerance).slice(
          0,
          -1,
        ),
        ...simplifyDrawnPoints(points.slice(index), tolerance),
      ];
}
export function boundedDrawnPoints(points, tolerance = 1.2, limit = 400) {
  const simplified = simplifyDrawnPoints(points, tolerance);
  if (simplified.length <= limit) return simplified;
  // Keep the whole stroke, including its last point; never truncate its tail.
  return Array.from(
    { length: limit },
    (_, i) =>
      simplified[Math.round((i * (simplified.length - 1)) / (limit - 1))],
  );
}

// Read a single SVG contour, including relative commands, quadratic curves and
// elliptical arcs. Compound textile sources deliberately stay intact, not lossy.
export function svgPathNodes(path) {
  const contours = patternPathContours(path, 400);
  return contours?.length === 1 && contours[0].nodes.length >= 2
    ? contours[0]
    : null;
}
export function convertPatternLayerToPath(layer) {
  if (layer.kind === "text")
    return {
      ...layer,
      kind: "path",
      text: undefined,
      nodes: undefined,
      name: layer.name + " outlines",
      paint: true,
    };
  const contour =
    patternPrimitiveNodes(layer) ||
    (layer.kind === "path" ? svgPathNodes(layer.path) : null);
  if (!contour)
    throw new Error(
      "This is a compound or embedded source. Its SVG stays editable in the source panel; point editing supports single-contour paths and basic shapes.",
    );
  return {
    ...layer,
    kind: "path",
    ...contour,
    path: patternNodesPath(contour.nodes, contour.closed),
    paint: true,
    fillEnabled: layer.paint
      ? layer.fillEnabled
      : !(layer.kind === "path" && layer.strokeWidth),
    stroke: layer.paint ? layer.stroke : layer.color,
  };
}
