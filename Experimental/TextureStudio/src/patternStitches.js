// Surface embroidery: explicit needle points, laid paths and upper passes.
// The reverse of the cloth and actual pierced mesh geometry are not modelled.
export const stitchTypes = [
  "Running stitch",
  "Backstitch",
  "Chain stitch",
  "Cross stitch",
  "Zigzag stitch",
  "Blanket stitch",
  "Herringbone stitch",
  "Feather stitch",
  "Couching stitch",
  "Satin stitch",
];
export const stitchDescriptions = {
  "Running stitch":
    "Separate forward stitches with equal exposed and hidden lengths.",
  Backstitch:
    "End-to-end surface stitches; each new stitch returns to the preceding needle point.",
  "Chain stitch":
    "Connected teardrop loops. Each following loop catches the nose of the previous loop.",
  "Cross stitch":
    "Square crosses, with the same diagonal uppermost throughout the row.",
  "Zigzag stitch":
    "A continuous zigzag, with needle entries at alternating peaks and troughs.",
  "Blanket stitch":
    "Looped L-shaped stitches: an edge thread caught by the following upright, not a row of T-bars.",
  "Herringbone stitch":
    "Staggered diagonals crossing near their ends, with short returns on the reverse.",
  "Feather stitch":
    "Open fly-stitch loops with holding stitches, alternating to either side; no separate straight spine.",
  "Couching stitch":
    "One continuous laid cord, held down by short transverse stitches of the same color.",
  "Satin stitch":
    "Closely packed parallel stitches between two needle lines, forming a continuous filled band.",
};
const stitchBound = (v, d, a, b) =>
  Number.isFinite(Number(v)) ? Math.max(a, Math.min(b, Number(v))) : d;
export function normalizeStitches(value = {}) {
  const s = value && typeof value === "object" ? value : {};
  return {
    enabled: s.enabled !== false,
    type:
      s.type === "Satin bars"
        ? "Satin stitch"
        : stitchTypes.includes(s.type)
          ? s.type
          : stitchTypes[0],
    layout: ["rows", "columns", "border", "diagonal"].includes(s.layout)
      ? s.layout
      : "rows",
    spacing: stitchBound(s.spacing, 40, 16, 64),
    width: stitchBound(s.width, 3.5, 1, 8),
    inset: stitchBound(s.inset, 24, 12, 96),
    relief: stitchBound(s.relief, 0.55, 0, 1),
    color: /^#[a-f\d]{6}$/i.test(s.color || "") ? s.color : "#ded3bb",
  };
}
export function stitchTint(hex, amount) {
  return (
    "#" +
    [1, 3, 5]
      .map((i) =>
        Math.max(
          0,
          Math.min(255, Math.round(parseInt(hex.slice(i, i + 2), 16) * amount)),
        )
          .toString(16)
          .padStart(2, "0"),
      )
      .join("")
  );
}
// Coordinates are in repeat units. Holes are specified independently: a curve
// control point, a loop nose or an over/under crossing is NOT a needle hole.
export function stitchUnit(type, width = 0.085) {
  const body = [],
    upper = [],
    holes = [];
  const line = (a, b, target = body) => target.push(["M", ...a], ["L", ...b]);
  const needle = (...points) => holes.push(...points);
  const gap = Math.min(0.13, width * 0.64);
  switch (type) {
    case "Backstitch":
      body.push(["M", -0.5, 0], ["C", -0.2, -0.025, 0.2, -0.025, 0.5, 0]);
      needle([-0.5, 0], [0.5, 0]);
      break;
    case "Chain stitch": {
      const root = [-0.5, 0],
        nose = [0.57, 0];
      // A tapered root and a round nose (matching vertical tangents), rather
      // than two pointed ends of an isolated oval.
      const prefix = (p0, p1, p2, p3) => {
        const lerp = (a, b) => a.map((v, i) => v + (b[i] - v) * 0.2);
        const a = lerp(p0, p1),
          b = lerp(p1, p2),
          c = lerp(p2, p3),
          d = lerp(a, b),
          e = lerp(b, c);
        return ["C", ...a, ...d, ...lerp(d, e)];
      };
      body.push(
        ["M", ...root],
        ["C", -0.05, -0.29, 0.57, -0.23, ...nose],
        ["C", 0.57, 0.23, -0.05, 0.29, ...root],
      );
      // Exact de Casteljau prefixes: upper passes cannot drift off the body.
      upper.push(
        ["M", ...root],
        prefix(root, [-0.05, -0.29], [0.57, -0.23], nose),
        ["M", ...root],
        prefix(root, [-0.05, 0.29], [0.57, 0.23], nose),
      );
      needle([-0.5, 0]);
      break;
    }
    case "Cross stitch":
      line([-0.32, -0.32], [-gap, -gap]);
      line([gap, gap], [0.32, 0.32]);
      line([-0.32, 0.32], [0.32, -0.32]);
      line([-0.17, 0.17], [0.17, -0.17], upper);
      needle([-0.32, -0.32], [0.32, 0.32], [-0.32, 0.32], [0.32, -0.32]);
      break;
    case "Zigzag stitch":
      body.push(["M", -0.5, 0.27], ["L", 0, -0.27], ["L", 0.5, 0.27]);
      needle([-0.5, 0.27], [0, -0.27]);
      break;
    case "Blanket stitch":
      // Upright turns into the edge loop. The following upright catches it.
      body.push(
        ["M", -0.5, -0.32],
        ["L", -0.5, 0.18],
        ["Q", -0.5, 0.29, -0.37, 0.29],
        ["L", 0.5, 0.29],
      );
      line([-0.5, 0.12], [-0.5, 0.29], upper);
      needle([-0.5, -0.32], [-0.5, 0.29]);
      break;
    case "Herringbone stitch":
      // Two unequal-offset legs: crossings at x=.125 near the upper needle
      // line, and x=.625 near the lower, NOT centered symmetrical X marks.
      line([-0.5, 0.3], [0.25, -0.3]);
      line([0, -0.3], [0.75, 0.3]);
      line([0.02, -0.284], [0.23, -0.116], upper);
      line([0.53, 0.276], [0.73, 0.116], upper);
      needle([-0.5, 0.3], [0.25, -0.3], [0, -0.3], [0.75, 0.3]);
      break;
    case "Feather stitch":
      // Two alternating open fly loops per repeat; holding stitches lead into
      // the next loop. No straight axis bar is added.
      body.push(
        ["M", -0.5, 0],
        ["Q", -0.32, -0.01, -0.15, -0.14],
        ["Q", -0.3, -0.35, -0.5, -0.36],
      );
      line([-0.15, -0.14], [0, 0]);
      body.push(
        ["M", 0, 0],
        ["Q", 0.18, 0.01, 0.35, 0.14],
        ["Q", 0.2, 0.35, 0, 0.36],
      );
      line([0.35, 0.14], [0.5, 0]);
      line([-0.15, -0.14], [-0.05, -0.047], upper);
      line([0.35, 0.14], [0.45, 0.047], upper);
      needle(
        [-0.5, 0],
        [-0.5, -0.36],
        [-0.15, -0.14],
        [0, 0],
        [0, 0.36],
        [0.35, 0.14],
      );
      break;
    case "Couching stitch":
      line([-0.5, 0], [0.5, 0]);
      line([-0.045, -0.22], [0.045, 0.22], upper);
      needle([-0.045, -0.22], [0.045, 0.22]);
      break;
    case "Satin stitch": {
      const n = Math.max(2, Math.round(1 / (width * 1.08)));
      for (let i = 0; i < n; i++) {
        const x = -0.5 + (i + 0.5) / n;
        body.push(["M", x, -0.32], ["Q", x + 0.012, 0, x, 0.32]);
        needle([x, -0.32], [x, 0.32]);
      }
      break;
    }
    default:
      line([-0.25, 0], [0.25, 0]);
      needle([-0.25, 0], [0.25, 0]);
  }
  return { body, upper, holes };
}
export function stitchLayers(value) {
  const s = normalizeStitches(value);
  if (!s.enabled) return [];
  const paths = { thread: "", holes: "", highlight: "" },
    seen = new Set();
  function unit(cx, cy, size, angle = 0, borderSide = -1) {
    const co = Math.cos(angle),
      si = Math.sin(angle);
    const point = (x, y) => {
      let px = cx + (x * co - y * si) * size,
        py = cy + (x * si + y * co) * size;
      if (borderSide >= 0) {
        // Miter the offset rectangle at each corner. In particular the
        // blanket edge line must meet, not leave four corner gaps.
        const span = 512 - 2 * s.inset;
        const center = [cx, cy, 512 - cx, 512 - cy][borderSide] - s.inset;
        const distance = center + x * size;
        const clamp = (v) => Math.max(0, Math.min(1, v));
        // Localize the miter to the first/last repeat. The middle uprights
        // remain perpendicular, not a progressively fanned border.
        const along =
          s.inset +
          distance +
          y *
            size *
            (clamp(1 - distance / size) - clamp(1 - (span - distance) / size));
        const across = s.inset + y * size;
        [px, py] = [
          [along, across],
          [512 - across, along],
          [512 - along, 512 - across],
          [across, 512 - along],
        ][borderSide];
      }
      return [
        Number((px / 5.12).toFixed(2)),
        Number((py / 5.12).toFixed(2)),
      ].join(" ");
    };
    const u = stitchUnit(s.type, s.width / size);
    const encode = (commands) =>
      commands
        .map(
          ([cmd, ...v]) =>
            cmd +
            Array.from({ length: v.length / 2 }, (_, i) =>
              point(v[i * 2], v[i * 2 + 1]),
            ).join(" "),
        )
        .join("");
    paths.thread += encode(u.body);
    // Upper passes are actual crossing threads, never differently colored
    // copies translated off the needle path.
    paths.highlight += encode(u.upper.length ? u.upper : u.body);
    for (const [x, y] of u.holes) {
      const p = point(x, y);
      if (!seen.has(p)) {
        seen.add(p);
        paths.holes += "M" + p + "h0.001";
      }
    }
  }
  const count = Math.round(512 / s.spacing),
    step = 512 / count,
    rows = Math.max(2, Math.round(count / 2));
  if (s.layout === "border") {
    const span = 512 - 2 * s.inset,
      n = Math.max(2, Math.round(span / s.spacing)),
      size = span / n;
    for (let i = 0; i < n; i++) {
      const t = s.inset + (i + 0.5) * size;
      unit(t, s.inset, size, 0, 0);
      unit(512 - s.inset, t, size, Math.PI / 2, 1);
      unit(512 - t, 512 - s.inset, size, Math.PI, 2);
      unit(s.inset, 512 - t, size, -Math.PI / 2, 3);
    }
  } else
    for (let j = 0; j < rows; j++)
      for (let i = 0; i < count; i++) {
        const x = (i + 0.5) * step,
          y = ((j + 0.5) * 512) / rows;
        if (s.layout === "columns") unit(y, x, step, Math.PI / 2);
        // Along a 45° row the advance is sqrt(2) times the horizontal pitch.
        else if (s.layout === "diagonal")
          unit(x, (y + i * step) % 512, step * Math.SQRT2, Math.PI / 4);
        else unit(x, y, step);
      }
  const crossing = stitchUnit(s.type).upper.length > 0;
  const layers = ["holes", "thread", "highlight"].map((role) => ({
    kind: "path",
    name:
      role === "holes"
        ? "Needle entries"
        : role === "thread"
          ? "Thread paths"
          : crossing
            ? "Upper thread passes"
            : "Thread crown",
    stitchRole: role,
    path: paths[role],
    x: 256,
    y: 256,
    width: 512,
    height: 512,
    color: role === "holes" ? value?.ground || "#353b3e" : s.color,
    finish: "cotton",
    relief:
      role === "holes"
        ? -0.12
        : Math.min(1, s.relief * (role === "highlight" ? 1 : 0.72)),
    roughness: 0.84,
    metalness: 0,
    opacity: 1,
    strokeWidth:
      (s.width *
        (role === "holes"
          ? 1.25
          : role === "highlight"
            ? crossing
              ? 0.9
              : 0.48
            : 1)) /
      5.12,
  }));
  // Split only between complete subpaths. Dense satin bands can contain
  // thousands of stitches; validation must never truncate a needle/path.
  return layers.flatMap((layer) => {
    const chunks = [];
    let chunk = "";
    for (const subpath of layer.path.match(/M[^M]*/g) || []) {
      if (chunk.length + subpath.length > 90000) {
        chunks.push(chunk);
        chunk = "";
      }
      chunk += subpath;
    }
    if (chunk) chunks.push(chunk);
    return chunks.map((path, i) => ({
      ...layer,
      path,
      name:
        layer.name + (chunks.length > 1 ? ` ${i + 1}/${chunks.length}` : ""),
    }));
  });
}
export function applyStitches(doc, patch = {}) {
  const stitch = normalizeStitches({ ...doc.stitch, ...patch });
  const generated = stitchLayers({ ...stitch, ground: doc.background });
  const layers = [...doc.layers.filter((l) => !l.stitchRole), ...generated];
  if (layers.length > 64)
    throw new Error(
      `Stitching needs ${generated.length} free layers. Remove some motifs before adding stitches.`,
    );
  return { ...doc, stitch, layers };
}
