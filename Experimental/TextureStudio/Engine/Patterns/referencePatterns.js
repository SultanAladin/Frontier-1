import { ornamentalInfill } from "./ornamentDrawing.js";
// Reference-led geometry. Four colorways never make four designs.
// The catalogue records topology + infill, not palette or seed variants.
const referencePlans = [
  ["Beaded Diamond Weave", "beads", "diamond-net", "nested"],
  ["Turquoise Faceted Vault", "vault", "fan-vault", "lotus"],
  ["Crimson Star and Cross Carpet", "islamic", "star-cross", "scroll"],
];
export const referenceDesignCatalog = referencePlans.map(
  ([name, style, topology, infill]) => ({
    name,
    style,
    topology,
    infill,
    id: name.toLowerCase().replaceAll(" ", "-"),
    group:
      style === "beads"
        ? "Beadwork"
        : style === "vault"
          ? "Vault ornament"
          : "Interlocking geometry",
    detailed: true,
    palette: "Design palette",
    description: `${topology.replaceAll("-", " ")} / ${infill} construction`,
  }),
);

export function referenceBuilder(width = 512, height = 512, palette) {
  const parts = new Map();
  let clipPolygon = null,
    clipPieces = null,
    pointTransform = null;
  const cross = (a, b, c) =>
    (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  function clipped(points, polygon = clipPolygon) {
    if (!polygon) return points;
    const clip = polygon,
      sign =
        Math.sign(
          clip.reduce((a, p, i) => {
            const q = clip[(i + 1) % clip.length];
            return a + p[0] * q[1] - q[0] * p[1];
          }, 0),
        ) || 1;
    let result = points;
    for (let j = 0; j < clip.length && result.length; j++) {
      const a = clip[j],
        b = clip[(j + 1) % clip.length],
        input = result;
      result = [];
      for (let i = 0; i < input.length; i++) {
        const p = input[i],
          q = input[(i + 1) % input.length],
          cp = sign * cross(a, b, p),
          cq = sign * cross(a, b, q);
        if (cp >= 0) result.push(p);
        if (cp >= 0 !== cq >= 0) {
          const t = cp / (cp - cq);
          result.push([p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])]);
        }
      }
    }
    return result;
  }
  function clipTriangles(polygon) {
    const area = polygon.reduce((s, p, i) => {
      const q = polygon[(i + 1) % polygon.length];
      return s + p[0] * q[1] - q[0] * p[1];
    }, 0);
    const points = area < 0 ? [...polygon].reverse() : [...polygon];
    if (
      points.every(
        (p, i) =>
          cross(
            p,
            points[(i + 1) % points.length],
            points[(i + 2) % points.length],
          ) >= -1e-8,
      )
    )
      return [polygon];
    const triangles = [];
    let guard = 0;
    while (points.length > 3 && guard++ < polygon.length * polygon.length) {
      let found = false;
      for (let i = 0; i < points.length; i++) {
        const a = points[(i + points.length - 1) % points.length],
          b = points[i],
          c = points[(i + 1) % points.length];
        if (cross(a, b, c) <= 1e-8) continue;
        if (
          points.some(
            (p) =>
              p !== a &&
              p !== b &&
              p !== c &&
              cross(a, b, p) > -1e-8 &&
              cross(b, c, p) > -1e-8 &&
              cross(c, a, p) > -1e-8,
          )
        )
          continue;
        triangles.push([a, b, c]);
        points.splice(i, 1);
        found = true;
        break;
      }
      if (!found) break;
    }
    if (points.length === 3) triangles.push(points);
    if (points.length > 3)
      throw new Error("Non-simple ornament clipping polygon.");
    return triangles;
  }
  const clip = (polygon, draw) => {
    const old = clipPolygon,
      oldPieces = clipPieces;
    clipPolygon = polygon;
    clipPieces = clipTriangles(polygon);
    try {
      draw();
    } finally {
      clipPolygon = old;
      clipPieces = oldPieces;
    }
  };
  const point = ([x, y]) =>
    `${((x / width) * 100).toFixed(2)} ${((y / height) * 100).toFixed(2)}`;
  const path = (pts) =>
    pts.map((p, i) => (i ? "L" : "M") + point(p)).join("") + "Z";
  const add = (z, k, pts) => {
    if (pointTransform) pts = pts.map(pointTransform);
    const fragments = clipPieces
      ? clipPieces.map((poly) => clipped(pts, poly))
      : [pts];
    for (const fragment of fragments) {
      if (
        fragment.length < 3 ||
        fragment.every((p) => p[0] < 0) ||
        fragment.every((p) => p[1] < 0) ||
        fragment.every((p) => p[0] > width) ||
        fragment.every((p) => p[1] > height)
      )
        continue;
      const key = z + ":" + k;
      if (!parts.has(key)) parts.set(key, []);
      parts.get(key).push(path(fragment));
    }
  };
  const rect = (z, k, x, y, w, h) =>
    add(z, k, [
      [x, y],
      [x + w, y],
      [x + w, y + h],
      [x, y + h],
    ]);
  const radial = (n, r, inner = null, a = -Math.PI / 2) =>
    Array.from({ length: inner ? 2 * n : n }, (_, i) => {
      const angle = a + (i * Math.PI * 2) / (inner ? 2 * n : n),
        rr = inner && i % 2 ? inner * r : r;
      return [Math.cos(angle) * rr, Math.sin(angle) * rr];
    });
  const star = (z, k, x, y, r, n = 8, inner = 0.765) =>
    add(
      z,
      k,
      radial(n, r, inner).map(([a, b]) => [x + a, y + b]),
    );
  const disk = (z, k, x, y, r) =>
    add(
      z,
      k,
      radial(12, r).map(([a, b]) => [x + a, y + b]),
    );
  const line = (z, k, pts, w = 1) => {
    const left = [],
      right = [];
    for (let i = 0; i < pts.length; i++) {
      const a = pts[Math.max(0, i - 1)],
        b = pts[Math.min(pts.length - 1, i + 1)],
        len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1,
        dx = ((-(b[1] - a[1]) / len) * w) / 2,
        dy = (((b[0] - a[0]) / len) * w) / 2;
      left.push([pts[i][0] + dx, pts[i][1] + dy]);
      right.push([pts[i][0] - dx, pts[i][1] - dy]);
    }
    add(z, k, [...left, ...right.reverse()]);
  };
  const outline = (z, k, pts, w = 1) => line(z, k, [...pts, pts[0]], w);
  const diamond = (z, k, x, y, rx, ry = rx) =>
    add(z, k, [
      [x, y - ry],
      [x + rx, y],
      [x, y + ry],
      [x - rx, y],
    ]);
  function spiral(z, k, cx, cy, r, angle = 0, turns = 1.4) {
    const pts = Array.from({ length: 32 }, (_, i) => {
      const t = i / 31,
        a = angle + t * Math.PI * 2 * turns,
        rr = r * (1 - t * 0.92);
      return [cx + Math.cos(a) * rr, cy + Math.sin(a) * rr];
    });
    line(z, k, pts, 0.7);
  }
  function flower(z, k, x, y, r, n = 8) {
    add(
      z,
      k,
      Array.from({ length: n * 4 }, (_, i) => {
        const a = (i * 2 * Math.PI) / (n * 4),
          rr = r * (0.78 + 0.22 * Math.cos(n * a));
        return [x + Math.cos(a) * rr, y + Math.sin(a) * rr];
      }),
    );
    disk(z + 1, 3, x, y, r * 0.26);
    disk(z + 2, 0, x, y, r * 0.1);
  }
  function leaf(z, k, x, y, r, a) {
    const pts = Array.from({ length: 12 }, (_, i) => {
      const t = (i * Math.PI) / 6;
      return [
        x +
          Math.cos(t) * r * Math.cos(a) -
          Math.sin(t) * r * 0.36 * Math.sin(a),
        y +
          Math.cos(t) * r * Math.sin(a) +
          Math.sin(t) * r * 0.36 * Math.cos(a),
      ];
    });
    add(z, k, pts);
    line(
      z + 1,
      3,
      [
        [x - Math.cos(a) * r * 0.8, y - Math.sin(a) * r * 0.8],
        [x + Math.cos(a) * r * 0.8, y + Math.sin(a) * r * 0.8],
      ],
      0.45,
    );
  }
  function ornament(kind, x, y, r, z = 5) {
    ornamentalInfill(
      { add, line, star, flower, disk, leaf },
      kind === "scroll" ? "vine" : kind === "lotus" ? "rosette" : kind,
      x,
      y,
      r,
      z,
    );
  }
  function layers(finish = "wool") {
    const result = [];
    for (const [key, paths] of [...parts].sort(
      (a, b) => +a[0].split(":")[0] - +b[0].split(":")[0],
    )) {
      const [z, k] = key.split(":").map(Number);
      let d = "",
        part = 0;
      const flush = () => {
        if (!d) return;
        result.push({
          kind: "path",
          name: `${z < 5 ? "Panels" : z < 20 ? "Infill" : "Guard bands"} / ${z} / ${k}`,
          ornamentRole: "ref:" + key + ":" + part++,
          path: d,
          x: 256,
          y: 256,
          width: 512,
          height: 512,
          color: palette[k],
          finish,
          roughness: finish === "ceramic" ? 0.22 : 0.86,
          metalness: 0,
          relief: finish === "ceramic" ? 0.04 : 0.09 + (z % 3) * 0.025,
          opacity: 1,
        });
        d = "";
      };
      for (const p of paths) {
        if (d.length + p.length > 96000) flush();
        d += p;
      }
      flush();
    }
    if (result.length > 61)
      throw Error(
        `Reference composition exceeds its layer budget (${result.length}).`,
      );
    return result;
  }
  return {
    clip,
    transform: (fn, draw) => {
      const prior = pointTransform;
      pointTransform = fn;
      try {
        draw();
      } finally {
        pointTransform = prior;
      }
    },
    add,
    rect,
    radial,
    star,
    disk,
    line,
    outline,
    diamond,
    spiral,
    flower,
    leaf,
    ornament,
    layers,
  };
}

// A close geometric reconstruction of the supplied woven diamond composition.
// Coordinates are authored in the reference's wide format, not a square rug
// decorated with sparse zigzags. No reference pixels are used as material maps.
export function referenceDiamondPattern() {
  const p = ["#10191c", "#eeeade", "#e23121", "#efc62e", "#1265b9", "#139756"],
    b = referenceBuilder(640, 360, p),
    W = 640,
    H = 360;
  b.rect(0, 0, 0, 0, W, H);
  const panel = (x, y, w, h, flip) => {
    const cx = x + w / 2,
      cy = y + h / 2;
    b.rect(1, 1, x + 1, y + 1, w - 2, h - 2);
    b.add(2, 2, [
      [x + 2, y + 2],
      [x + w - 2, y + 2],
      [x + 2, y + h * 0.5],
    ]);
    b.add(2, 4, [
      [x + w - 2, y + 2],
      [x + w - 2, y + h * 0.48],
      [x + 2, y + h - 2],
      [x + 2, y + h * 0.54],
    ]);
    b.add(2, 3, [
      [x + 2, y + h * 0.55],
      [x + w * 0.48, y + h * 0.77],
      [x + 2, y + h - 2],
    ]);
    b.line(
      3,
      0,
      [
        [x, y + h * 0.5],
        [x + w, y],
      ],
      2.5,
    );
    b.line(
      3,
      3,
      [
        [x + 3, y + h * 0.52],
        [x + w - 3, y + 5],
      ],
      2,
    );
    b.line(
      3,
      0,
      [
        [x, y + h],
        [x + w, y + h * 0.5],
      ],
      2.5,
    );
    if (flip) {
      for (let k = 0; k < 4; k++)
        b.add(3, [0, 3, 0, 5][k], [
          [cx - w * 0.36 + k * 4, cy + h * 0.1],
          [cx, cy - h * 0.16 + k * 5],
          [cx + w * 0.36 - k * 4, cy + h * 0.1],
          [cx, cy + h * 0.42 - k * 5],
        ]);
    }
  };
  for (let y of [10, 270])
    for (let i = 0; i < 8; i++) panel(25 + i * 73, y, 73, 80, i % 3 === 1);
  // Complete triangular field between the three main shields, including split
  // pinwheels, narrow checker columns and fine black/ivory separating lines.
  for (let j = 1; j < 3; j++)
    for (let i = 0; i < 8; i++) {
      const x = 25 + i * 73,
        y = 10 + j * 85,
        cx = x + 36.5,
        cy = y + 42.5;
      b.rect(1, 1, x, y, 73, 85);
      for (let a = 0; a < 4; a++) {
        const corners = [
          [x, y],
          [x + 73, y],
          [x + 73, y + 85],
          [x, y + 85],
        ];
        b.add(2, [4, 3, 2, 5][(a + i + j) % 4], [
          corners[a],
          corners[(a + 1) % 4],
          [cx, cy],
        ]);
        b.line(3, 0, [corners[a], [cx, cy]], 2);
      }
      b.line(
        3,
        1,
        [
          [x + 3, y + 4],
          [x + 70, y + 81],
        ],
        1.2,
      );
    }
  for (const x of [174, 446])
    for (let y = 12; y < 350; y += 22) {
      b.rect(4, 0, x - 13, y, 26, 22);
      b.diamond(5, 1, x, y + 11, 11, 10);
      b.diamond(6, 3, x, y + 11, 6, 5);
    }
  function shield(x, y, rx, ry, main) {
    const rings = main
      ? [
          [1, 0],
          [0.98, 2],
          [0.945, 3],
          [0.92, 0],
          [0.89, 1],
          [0.865, 4],
          [0.81, 0],
          [0.78, 1],
          [0.75, 4],
          [0.64, 0],
          [0.615, 1],
          [0.585, 2],
          [0.55, 0],
          [0.51, 1],
          [0.48, 4],
          [0.405, 3],
          [0.31, 5],
          [0.205, 0],
          [0.17, 1],
          [0.12, 2],
        ]
      : [
          [1, 0],
          [0.965, 2],
          [0.93, 4],
          [0.855, 0],
          [0.81, 1],
          [0.77, 4],
          [0.68, 0],
          [0.625, 5],
          [0.57, 3],
          [0.48, 0],
          [0.415, 2],
          [0.33, 0],
          [0.27, 3],
        ];
    // Each complete nested contour has its own depth order; never bucket colors
    // ahead of outlines or merge different bands into a single flattened fill.
    for (let q = 0; q < rings.length; q++) {
      const [s, k] = rings[q];
      b.diamond(7 + q * 0.32, k, x, y, rx * s, ry * s);
    }
    const corners = [
      [x, y - ry],
      [x + rx, y],
      [x, y + ry],
      [x - rx, y],
    ];
    if (main) {
      // White/blue staircase ring, with every tooth drawn perpendicular to its edge.
      for (let edge = 0; edge < 4; edge++) {
        const a = corners[edge],
          c = corners[(edge + 1) % 4];
        const points = [];
        for (let i = 0; i < 10; i++) {
          const t = i / 10,
            next = (i + 1) / 10;
          const px = x + (a[0] * (1 - t) + c[0] * t - x) * 0.84,
            py = y + (a[1] * (1 - t) + c[1] * t - y) * 0.84;
          const nx = x + (a[0] * (1 - next) + c[0] * next - x) * 0.84,
            ny = y + (a[1] * (1 - next) + c[1] * next - y) * 0.84;
          points.push([px, py], edge % 2 ? [px, ny] : [nx, py], [nx, ny]);
        }
        b.line(14, 0, points, 5);
        b.line(15, 1, points, 3.5);
      }
    }
    for (const radius of main ? [0.965, 0.5] : [0.82])
      for (let edge = 0; edge < 4; edge++) {
        const a = corners[edge],
          c = corners[(edge + 1) % 4],
          n = main ? 24 : 19;
        for (let i = 0; i < n; i++) {
          const t = (i + 0.5) / n,
            px = x + (a[0] * (1 - t) + c[0] * t - x) * radius,
            py = y + (a[1] * (1 - t) + c[1] * t - y) * radius;
          if (main && radius === 0.965) b.disk(16, 1, px, py, 1.1);
          else {
            b.diamond(16, 1, px, py, 3.3, 3.3);
            b.diamond(17, 0, px, py, 2.1, 2.1);
          }
        }
      }
    if (main)
      for (const yy of [y - ry * 0.79, y + ry * 0.79]) {
        b.diamond(16, 0, x, yy, 18, 17);
        b.diamond(17, 1, x, yy, 15, 14);
        b.diamond(18, 4, x, yy, 12, 11);
        b.diamond(19, 2, x, yy, 7, 7);
      }
  }
  shield(320, 180, 165, 170, true);
  shield(99, 180, 112, 100, false);
  shield(541, 180, 112, 100, false);
  // Narrow side sawteeth and green/black woven selvedges from the reference.
  for (const x of [0, 620]) {
    b.rect(23, 0, x, 8, 20, 344);
    for (let y = 9; y < 350; y += 18) {
      b.add(24, 2, [
        [x + 2, y],
        [x + 18, y + 9],
        [x + 2, y + 18],
      ]);
      b.add(24, 1, [
        [x + 18, y],
        [x + 3, y + 9],
        [x + 18, y + 18],
      ]);
    }
  }
  for (const y of [0, 351]) {
    b.rect(25, 0, 0, y, 640, 9);
    for (let x = 2; x < 640; x += 9) {
      b.rect(26, 5, x, y + 1, 5, 7);
      b.rect(27, 1, x, y + 3, 5, 0.75);
    }
  }
  return {
    schema: "alloy.pattern.v1",
    name: "Chromatic Diamond Tapestry",
    referenceDesign: "chromatic-diamond-tapestry",
    collection: "African compositions",
    presentation: "rug",
    designAspect: 640 / 360,
    tileAxes: "none",
    background: p[0],
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: b.layers("cotton"),
  };
}

export function referencePattern(name) {
  const d = referenceDesignCatalog.find(
    (p) => p.name === name || p.id === name,
  );
  if (!d) return null;
  if (d.style === "beads") return referenceBeadPattern(d);
  const vault = d.style === "vault";
  const palette = vault
    ? ["#102653", "#f3e5bd", "#159dab", "#d7a839", "#398645", "#dd662d"]
    : ["#722b27", "#f3e6bf", "#bd493b", "#d8ac43", "#244d45", "#3b7967"];
  const b = referenceBuilder(512, 512, palette);
  b.rect(0, 0, 0, 0, 512, 512);
  if (vault) {
    const n = 16,
      cx = 256,
      cy = -18,
      radii = [18, 63, 125, 191, 258, 352, 460, 610];
    const polar = (ring, j) => [
      cx +
        Math.cos((j * Math.PI * 2) / n + ((ring % 2) * Math.PI) / n) *
          radii[ring],
      cy +
        Math.sin((j * Math.PI * 2) / n + ((ring % 2) * Math.PI) / n) *
          radii[ring],
    ];
    for (let ring = 0; ring < radii.length - 1; ring++)
      for (let j = 0; j < n; j++) {
        const pts = [
          polar(ring, j),
          polar(ring + 1, j),
          polar(ring + 1, j + 1),
          polar(ring, j + 1),
        ];
        const x = pts.reduce((s, p) => s + p[0], 0) / 4,
          y = pts.reduce((s, p) => s + p[1], 0) / 4;
        if (x < -120 || x > 632 || y < -120 || y > 632) continue;
        b.add(1, 3, pts);
        const inset = pts.map(([a, c]) => [
          x + (a - x) * 0.95,
          y + (c - y) * 0.95,
        ]);
        b.add(
          2,
          ring < 3 ? 2 : ring === 3 ? 1 : [0, 0, 2, 0, 2, 1][(ring + j) % 6],
          inset,
        );
        b.outline(3, 1, inset, 0.7);
        b.clip(inset, () => {
          const r = (radii[ring + 1] - radii[ring]) * 0.65;
          b.ornament((ring + j) % 2 ? "scroll" : "palmette", x, y, r, 5);
          for (let q = 0; q < 1; q++) {
            const a = inset[q],
              c = inset[(q + 1) % 4],
              px = (a[0] + c[0] + x) / 3,
              py = (a[1] + c[1] + y) / 3;
            b.ornament("lotus", px, py, r * 0.44, 5);
          }
        });
      }
    for (let j = 0; j < 32; j++) {
      const a = (j * Math.PI) / 16;
      b.leaf(14, 3, cx + Math.cos(a) * 38, cy + Math.sin(a) * 38, 13, a);
    }
    b.star(16, 0, cx, cy, 33, 16, 0.82);
    b.ornament("lotus", cx, cy, 29, 17);
  } else {
    const n = 8,
      size = 128;
    for (let row = -1; row < 5; row++)
      for (let col = -1; col < 5; col++) {
        const x = (col + 0.5) * size,
          y = (row + 0.5) * size,
          r = size * 0.5;
        // Complete interstitial crosses + star surrounds fill the plane. No sparse motifs.
        const cross = [
          [-0.5, -0.18],
          [-0.18, -0.18],
          [-0.18, -0.5],
          [0.18, -0.5],
          [0.18, -0.18],
          [0.5, -0.18],
          [0.5, 0.18],
          [0.18, 0.18],
          [0.18, 0.5],
          [-0.18, 0.5],
          [-0.18, 0.18],
          [-0.5, 0.18],
        ].map(([a, c]) => [col * size + a * size, row * size + c * size]);
        b.add(1, 3, cross);
        b.add(
          2,
          4,
          cross.map(([a, c]) => [
            col * size + (a - col * size) * 0.93,
            row * size + (c - row * size) * 0.93,
          ]),
        );
        for (let q = 0; q < 4; q++) {
          const a = (q * Math.PI) / 2;
          b.spiral(
            5,
            3,
            col * size + Math.cos(a) * size * 0.27,
            row * size + Math.sin(a) * size * 0.27,
            size * 0.12,
            a,
          );
          b.flower(
            7,
            1,
            col * size + Math.cos(a) * size * 0.13,
            row * size + Math.sin(a) * size * 0.13,
            size * 0.045,
            5,
          );
        }
        const pts = b.radial(n, r, 0.765).map(([a, c]) => [x + a, y + c]);
        b.add(1, 3, pts);
        b.add(
          2,
          1,
          pts.map(([a, c]) => [x + (a - x) * 0.92, y + (c - y) * 0.92]),
        );
        b.add(
          3,
          0,
          pts.map(([a, c]) => [x + (a - x) * 0.7, y + (c - y) * 0.7]),
        );
        b.add(
          4,
          2,
          pts.map(([a, c]) => [x + (a - x) * 0.6, y + (c - y) * 0.6]),
        );
        b.ornament("palmette", x, y, r * 0.56, 5);
        b.star(12, 3, x, y, r * 0.25, 8, 0.76);
        b.star(13, 0, x, y, r * 0.21, 8, 0.76);
        b.flower(14, 1, x, y, r * 0.17, 8);
        const strap = [
          [x - r * 0.78, y - r * 0.78],
          [x + r * 0.78, y - r * 0.78],
          [x + r * 0.78, y + r * 0.78],
          [x - r * 0.78, y + r * 0.78],
        ];
        b.outline(15, 0, strap, 3.8);
        b.outline(16, 3, strap, 2.4);
        b.outline(17, 1, strap, 0.6);
        b.ornament("palmette", col * size, row * size, size * 0.24, 5);
        for (let q = 0; q < 8; q++) {
          const a = (q * Math.PI) / 4;
          b.flower(
            10,
            3,
            x + Math.cos(a) * r * 0.62,
            y + Math.sin(a) * r * 0.62,
            r * 0.1,
            7,
          );
        }

        for (let q = 0; q < 24; q++) {
          const a = (q * Math.PI) / 12;
          b.disk(
            12,
            3,
            x + Math.cos(a) * r * 0.73,
            y + Math.sin(a) * r * 0.73,
            0.9,
          );
        }
      }
  }
  // Finite framed composition; geometry is still independently editable.
  for (const [inset, w, k] of [
    [0, 7, 0],
    [7, 2, 3],
    [10, 1, 1],
    [13, 3, 0],
  ]) {
    b.rect(24, k, inset, inset, 512 - 2 * inset, w);
    b.rect(24, k, inset, 512 - inset - w, 512 - 2 * inset, w);
    b.rect(24, k, inset, inset, w, 512 - 2 * inset);
    b.rect(24, k, 512 - inset - w, inset, w, 512 - 2 * inset);
  }
  for (let i = 20; i < 492; i += 7)
    for (const [x, y] of [
      [i, 5],
      [i, 507],
      [5, i],
      [507, i],
    ])
      b.diamond(25, 3, x, y, 1.5);
  return {
    schema: "alloy.pattern.v1",
    name: d.name,
    collection: d.group,
    presentation: "rug",
    referenceDesign: d.id,
    designAspect: 1,
    tileAxes: "none",
    background: palette[0],
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: b.layers("wool"),
  };
}

// Thousands of actual oval bead bodies, highlight facets and pierced centers.
// Pattern colors are assigned per bead, not painted across continuous flat dots.
function referenceBeadPattern(spec) {
  const palette = [
    "#11171c",
    "#e7e8dc",
    "#d72c18",
    "#edbb17",
    "#133f9a",
    "#147f54",
    "#ef6817",
    "#666979",
  ];
  const b = referenceBuilder(512, 512, palette),
    columns = 84,
    rows = 64,
    dx = 512 / columns,
    dy = 512 / rows;
  b.rect(0, 0, 0, 0, 512, 512);
  for (let j = -1; j <= rows; j++)
    for (let i = -1; i <= columns; i++) {
      const x = (i + 0.5 + (j % 2) * 0.5) * dx,
        y = (j + 0.5) * dy;
      const centers = [
        [256, 255, 6],
        [13, 340, 5],
        [500, 302, 1],
        [57, -88, 4],
        [465, -84, 2],
      ];
      let closest = centers[0],
        distance = Infinity;
      for (const c of centers) {
        const t = Math.abs((x - c[0]) / 145) + Math.abs((y - c[1]) / 260);
        if (t < distance) {
          distance = t;
          closest = c;
        }
      }
      // Dotted black/white beads are a narrow outline, not an empty field filler.
      let k =
        distance < 0.2
          ? 0
          : distance < 0.44
            ? 1
            : distance < 0.7
              ? closest[2]
              : distance < 0.76
                ? (i + j) % 2
                  ? 0
                  : 1
                : distance < 0.84
                  ? 0
                  : distance < 1.02
                    ? 1
                    : distance < 1.28
                      ? y < 255
                        ? 4
                        : 5
                      : distance < 1.34
                        ? (i + j) % 2
                          ? 0
                          : 1
                        : 0;
      if (closest[0] === 500 && distance < 0.22) k = 3;
      if (closest[0] === 13 && distance < 0.18) k = 2;
      const wobble = Math.sin(i * 37 + j * 19),
        cx = x + wobble * 0.18,
        cy = y + Math.cos(i * 13 + j * 23) * 0.15;
      b.add(
        1,
        k,
        b
          .radial(12, 1)
          .map(([u, v]) => [cx + u * dx * 0.44, cy + v * dy * 0.46]),
      );
      // Bead edge darkening is geometry, not a baked photograph or random dot print.
      const arc = Array.from({ length: 7 }, (_, a) => {
        const th = 0.2 + a * Math.PI * 0.13;
        return [cx + Math.cos(th) * dx * 0.37, cy + Math.sin(th) * dy * 0.39];
      });
      b.line(2, 0, arc, 0.45);
      b.add(
        3,
        k === 1 ? 7 : 1,
        b
          .radial(8, 1)
          .map(([u, v]) => [
            cx - dx * 0.19 + u * dx * 0.055,
            cy - dy * 0.19 + v * dy * 0.16,
          ]),
      );
      b.add(
        4,
        0,
        b
          .radial(8, 1)
          .map(([u, v]) => [cx + u * dx * 0.095, cy + v * dy * 0.06]),
      );
    }
  return {
    schema: "alloy.pattern.v1",
    name: spec.name,
    collection: "Beadwork",
    referenceDesign: spec.id,
    presentation: "rug",
    designAspect: 1.55,
    tileAxes: "none",
    background: palette[0],
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    beadwork: { columns, rows, height: 0.004 },
    layers: b.layers("ceramic"),
  };
}
