// Portable, DOM-free path data shared by the editor, SVG and shader exports.
// Node coordinates are local to a motif's 0–100 box; handles are absolute.
export function normalizePatternNodes(input) {
  if (!Array.isArray(input) || input.length < 2 || input.length > 400)
    throw new Error("An editable path needs 2–400 points.");
  const point = (p) => {
    if (!p || !Number.isFinite(Number(p.x)) || !Number.isFinite(Number(p.y)))
      throw new Error("Path points must have finite coordinates.");
    return {
      x: Math.max(-4096, Math.min(4096, Number(p.x))),
      y: Math.max(-4096, Math.min(4096, Number(p.y))),
    };
  };
  return input.map((n) => ({
    ...point(n),
    ...(["corner", "smooth", "symmetric", "auto"].includes(n.mode)
      ? { mode: n.mode }
      : {}),
    ...(n.in ? { in: point(n.in) } : {}),
    ...(n.out ? { out: point(n.out) } : {}),
  }));
}
export function patternNodesPath(nodes, closed = false) {
  if (!nodes?.length) return "";
  const f = (n) => Number(n.toFixed(4)),
    xy = (p) => `${f(p.x)} ${f(p.y)}`;
  let d = `M${xy(nodes[0])}`;
  const segment = (a, b) =>
    a.out || b.in
      ? ` C${xy(a.out || a)} ${xy(b.in || b)} ${xy(b)}`
      : ` L${xy(b)}`;
  for (let i = 1; i < nodes.length; i++) d += segment(nodes[i - 1], nodes[i]);
  if (closed) d += segment(nodes.at(-1), nodes[0]) + " Z";
  return d;
}
export function patternPolygonNodes(sides = 6, innerRadius = null) {
  const n = Math.max(3, Math.min(24, Math.round(sides))),
    count = innerRadius === null ? n : n * 2;
  return Array.from({ length: count }, (_, i) => {
    const a = (i * Math.PI * 2) / count - Math.PI / 2,
      r = innerRadius !== null && i % 2 ? 50 * innerRadius : 50;
    return { x: 50 + Math.cos(a) * r, y: 50 + Math.sin(a) * r };
  });
}
export function patternPrimitiveNodes(layer) {
  const p = (x, y, controls = {}) => ({ x, y, ...controls });
  if (layer.kind === "path")
    return layer.nodes ? { nodes: layer.nodes, closed: !!layer.closed } : null;
  if (layer.kind === "arc")
    return patternArcNodes(layer.arcStart, layer.arcSweep, layer.arcClosure);
  if (layer.kind === "ellipse") {
    const k = (4 * (Math.SQRT2 - 1) * 50) / 3;
    return {
      closed: true,
      nodes: [
        p(50, 0, { in: p(50 - k, 0), out: p(50 + k, 0) }),
        p(100, 50, { in: p(100, 50 - k), out: p(100, 50 + k) }),
        p(50, 100, { in: p(50 + k, 100), out: p(50 - k, 100) }),
        p(0, 50, { in: p(0, 50 + k), out: p(0, 50 - k) }),
      ],
    };
  }
  let nodes;
  if (layer.kind === "rect") {
    const r = Math.max(0, Math.min(50, layer.cornerRadius || 0)),
      k = (r * 4 * (Math.SQRT2 - 1)) / 3;
    nodes = r
      ? [
          p(r, 0, { in: p(r - k, 0) }),
          p(100 - r, 0, { out: p(100 - r + k, 0) }),
          p(100, r, { in: p(100, r - k) }),
          p(100, 100 - r, { out: p(100, 100 - r + k) }),
          p(100 - r, 100, { in: p(100 - r + k, 100) }),
          p(r, 100, { out: p(r - k, 100) }),
          p(0, 100 - r, { in: p(0, 100 - r + k) }),
          p(0, r, { out: p(0, r - k) }),
        ]
      : [p(0, 0), p(100, 0), p(100, 100), p(0, 100)];
  } else if (layer.kind === "diamond")
    nodes = [p(50, 0), p(100, 50), p(50, 100), p(0, 50)];
  else if (layer.kind === "triangle")
    nodes = [p(50, 0), p(100, 100), p(0, 100)];
  else if (layer.kind === "polygon")
    nodes = patternPolygonNodes(layer.sides || 6);
  else if (layer.kind === "star")
    nodes = patternPolygonNodes(
      layer.starPoints || 5,
      layer.innerRadius ?? 0.45,
    );
  return nodes ? { nodes, closed: true } : null;
}

// SVG contours are converted before motif scaling so native strokes stay in
// document units in the canvas, SVG images, raster exports and material maps.
export function patternPathContours(path, maxNodes = Infinity) {
  const contours = [];
  const finish = () => {
    if (!nodes.length) return;
    if (
      nodes.length > 2 &&
      Math.hypot(nodes[0].x - nodes.at(-1).x, nodes[0].y - nodes.at(-1).y) <
        0.00001
    ) {
      const last = nodes.pop();
      if (last.in) nodes[0].in = last.in;
      closed = true;
    }
    contours.push({ nodes, closed });
    nodes = [];
    closed = false;
    previous = "";
    quadratic = null;
  };
  const tokens =
    String(path).match(
      /[a-zA-Z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?/g,
    ) || [];
  let at = 0,
    command = "",
    current = { x: 0, y: 0 },
    nodes = [],
    closed = false,
    previous = "",
    quadratic = null;
  const count = { M: 2, L: 2, H: 1, V: 1, C: 6, S: 4, Q: 4, T: 2, A: 7 };
  const curve = (c1, c2, end) => {
    nodes.at(-1).out = c1;
    nodes.push({ ...end, in: c2 });
    current = end;
  };
  while (at < tokens.length) {
    if (/^[a-z]$/i.test(tokens[at])) command = tokens[at++];
    const kind = command.toUpperCase(),
      relative = command !== kind;
    if (kind === "Z") {
      closed = true;
      const first = nodes[0];
      finish();
      if (first) current = { x: first.x, y: first.y };
      command = "";
      continue;
    }
    if (
      !count[kind] ||
      at + count[kind] > tokens.length ||
      tokens.slice(at, at + count[kind]).some((t) => /^[a-z]$/i.test(t))
    )
      return null;
    const values = tokens.slice(at, (at += count[kind])).map(Number),
      point = (i) => ({
        x: values[i] + (relative ? current.x : 0),
        y: values[i + 1] + (relative ? current.y : 0),
      });
    if (kind === "M") {
      finish();
      current = point(0);
      nodes.push(current);
      command = relative ? "l" : "L";
    } else if (!nodes.length) {
      if (!contours.length) return null;
      nodes.push({ x: current.x, y: current.y });
      at -= count[kind];
      continue;
    } else if (kind === "L" || kind === "H" || kind === "V") {
      current =
        kind === "L"
          ? point(0)
          : kind === "H"
            ? { x: values[0] + (relative ? current.x : 0), y: current.y }
            : { x: current.x, y: values[0] + (relative ? current.y : 0) };
      nodes.push(current);
    } else if (kind === "C") curve(point(0), point(2), point(4));
    else if (kind === "S") {
      const last = nodes.at(-1),
        c1 =
          ["C", "S"].includes(previous) && last.in
            ? { x: 2 * last.x - last.in.x, y: 2 * last.y - last.in.y }
            : { ...current };
      curve(c1, point(0), point(2));
    } else if (kind === "Q" || kind === "T") {
      const q =
          kind === "Q"
            ? point(0)
            : ["Q", "T"].includes(previous) && quadratic
              ? {
                  x: 2 * current.x - quadratic.x,
                  y: 2 * current.y - quadratic.y,
                }
              : { ...current },
        end = point(kind === "Q" ? 2 : 0);
      curve(
        {
          x: current.x + ((q.x - current.x) * 2) / 3,
          y: current.y + ((q.y - current.y) * 2) / 3,
        },
        {
          x: end.x + ((q.x - end.x) * 2) / 3,
          y: end.y + ((q.y - end.y) * 2) / 3,
        },
        end,
      );
      quadratic = q;
    } else if (kind === "A") {
      const end = point(5),
        arcs = patternArcCubics(current, end, values);
      for (const arc of arcs) curve(arc[0], arc[1], arc[2]);
      if (!arcs.length && (end.x !== current.x || end.y !== current.y)) {
        current = end;
        nodes.push(end);
      }
    }
    previous = kind;
    if (nodes.length > maxNodes) return null;
  }
  finish();
  return contours.length ? contours : null;
}

function patternArcCubics(start, end, values) {
  const radians = (v) => (v * Math.PI) / 180,
    rotate = (p, a) => ({
      x: p.x * Math.cos(a) - p.y * Math.sin(a),
      y: p.x * Math.sin(a) + p.y * Math.cos(a),
    });
  let [rx, ry, angle, large, sweep] = values;
  rx = Math.abs(rx);
  ry = Math.abs(ry);
  if (!rx || !ry || Math.hypot(end.x - start.x, end.y - start.y) < 1e-10)
    return [];
  const phi = radians(angle),
    half = rotate({ x: (start.x - end.x) / 2, y: (start.y - end.y) / 2 }, -phi),
    scale = (half.x / rx) ** 2 + (half.y / ry) ** 2;
  if (scale > 1) {
    rx *= Math.sqrt(scale);
    ry *= Math.sqrt(scale);
  }
  const numerator = Math.max(
      0,
      rx * rx * ry * ry - rx * rx * half.y * half.y - ry * ry * half.x * half.x,
    ),
    denominator = rx * rx * half.y * half.y + ry * ry * half.x * half.x,
    sign = !!large === !!sweep ? -1 : 1,
    factor = sign * Math.sqrt(numerator / Math.max(1e-20, denominator)),
    c = { x: (factor * rx * half.y) / ry, y: (-factor * ry * half.x) / rx },
    rotated = rotate(c, phi),
    center = {
      x: rotated.x + (start.x + end.x) / 2,
      y: rotated.y + (start.y + end.y) / 2,
    },
    a = Math.atan2((half.y - c.y) / ry, (half.x - c.x) / rx),
    b = Math.atan2((-half.y - c.y) / ry, (-half.x - c.x) / rx);
  let delta = b - a;
  if (sweep && delta < 0) delta += Math.PI * 2;
  if (!sweep && delta > 0) delta -= Math.PI * 2;
  const pieces = Math.ceil(Math.abs(delta) / (Math.PI / 2)),
    result = [],
    world = (p) => {
      const r = rotate({ x: p.x * rx, y: p.y * ry }, phi);
      return { x: center.x + r.x, y: center.y + r.y };
    };
  for (let i = 0; i < pieces; i++) {
    const t1 = a + (delta * i) / pieces,
      t2 = a + (delta * (i + 1)) / pieces,
      alpha = (4 / 3) * Math.tan((t2 - t1) / 4);
    result.push([
      world({
        x: Math.cos(t1) - alpha * Math.sin(t1),
        y: Math.sin(t1) + alpha * Math.cos(t1),
      }),
      world({
        x: Math.cos(t2) + alpha * Math.sin(t2),
        y: Math.sin(t2) - alpha * Math.cos(t2),
      }),
      i === pieces - 1 ? end : world({ x: Math.cos(t2), y: Math.sin(t2) }),
    ]);
  }
  return result;
}

export function patternSizedPath(layer) {
  const contours = layer.nodes
    ? [{ nodes: layer.nodes, closed: !!layer.closed }]
    : ["path", "text"].includes(layer.kind)
      ? patternPathContours(layer.path)
      : [patternPrimitiveNodes(layer)].filter(Boolean);
  if (!contours) return null;
  const point = (p) => ({
    x: ((p.x - 50) * layer.width) / 100,
    y: ((p.y - 50) * layer.height) / 100,
  });
  return contours
    .map(({ nodes, closed }) =>
      patternNodesPath(
        nodes.map((n) => ({
          ...point(n),
          ...(n.in ? { in: point(n.in) } : {}),
          ...(n.out ? { out: point(n.out) } : {}),
        })),
        closed,
      ),
    )
    .join(" ");
}

export function patternArcNodes(
  startAngle = -90,
  sweep = 180,
  closed = "open",
) {
  const start = (Number(startAngle) * Math.PI) / 180,
    extent =
      (Math.max(-359.99, Math.min(359.99, Number(sweep))) * Math.PI) / 180;
  const steps = Math.max(1, Math.ceil(Math.abs(extent) / (Math.PI / 2))),
    step = extent / steps;
  const point = (a) => ({ x: 50 + 50 * Math.cos(a), y: 50 + 50 * Math.sin(a) }),
    nodes = [point(start)];
  for (let i = 0; i < steps; i++) {
    const a = start + i * step,
      b = a + step,
      k = (4 / 3) * Math.tan(step / 4),
      pa = point(a),
      pb = point(b);
    nodes.at(-1).out = {
      x: pa.x - k * 50 * Math.sin(a),
      y: pa.y + k * 50 * Math.cos(a),
    };
    nodes.push({
      ...pb,
      in: { x: pb.x + k * 50 * Math.sin(b), y: pb.y - k * 50 * Math.cos(b) },
    });
  }
  if (closed === "pie") nodes.push({ x: 50, y: 50 });
  return { nodes, closed: closed !== "open" };
}
