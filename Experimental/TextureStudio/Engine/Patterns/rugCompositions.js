import { referenceDiamondPattern } from "./referencePatterns.js";
import { rugDesignCatalog } from "./rugDesigns.js";
import { applyStitches } from "./patternStitches.js";

const rugBound = (v, d, a, b) =>
  Number.isFinite(Number(v)) ? Math.max(a, Math.min(b, Number(v))) : d;
export function normalizeRugComposition(value = {}) {
  const s = value && typeof value === "object" ? value : {};
  return {
    id: rugDesignCatalog.some((p) => p.id === s.id)
      ? s.id
      : rugDesignCatalog[0].id,
    detail: Math.round(rugBound(s.detail, 2, 1, 3)),
    borderWidth: rugBound(s.borderWidth, 48, 28, 76),
    field: s.field !== false,
    medallions: s.medallions !== false,
    borderOrnaments: s.borderOrnaments !== false,
    corners: s.corners !== false,
  };
}
const rugPalettes = {
  vivid: ["#14273d", "#f3dfab", "#d44c35", "#ddb33e", "#247bae", "#238666"],
  raffia: ["#c6a06a", "#30261d", "#765034", "#453124", "#efd8aa", "#8d6c42"],
  indigo: ["#172b45", "#e5dfca", "#6088a3", "#9ab3b9", "#314d70", "#bba778"],
  earth: ["#35271f", "#ead9af", "#a5653d", "#bf8e55", "#5b4332", "#928370"],
  navy: ["#152f43", "#e6c996", "#b8493d", "#c8974b", "#427c79", "#8ba99a"],
  ruby: ["#813e38", "#ecdab3", "#24445a", "#c9a366", "#487b76", "#bc7160"],
  ivory: ["#e7d9bb", "#254f65", "#a94942", "#bd9354", "#62988d", "#816c7f"],
  teal: ["#224f51", "#e7d6b2", "#b86f54", "#caac6d", "#799d8b", "#263a53"],
};
const rugN = (n) => (n / 5.12).toFixed(2);
const rugPath = (points) =>
  points
    .map(([x, y], i) => (i ? "L" : "M") + rugN(x) + " " + rugN(y))
    .join("") + "Z";
const rugBox = (x, y, w, h) => [
  [x, y],
  [x + w, y],
  [x + w, y + h],
  [x, y + h],
];
const rugRadial = (n, r = 1, inner = null, phase = -Math.PI / 2) =>
  Array.from({ length: inner ? 2 * n : n }, (_, i) => {
    const a = phase + (i * 2 * Math.PI) / (inner ? 2 * n : n),
      q = inner && i % 2 ? inner * r : r;
    return [Math.cos(a) * q, Math.sin(a) * q];
  });

export function richRugPattern(name, settings) {
  const spec = rugDesignCatalog.find((p) => p.name === name || p.id === name);
  if (!spec) return null;
  if (spec.name === "Chromatic Diamond Tapestry")
    return referenceDiamondPattern();
  const opts = normalizeRugComposition({ ...settings, id: spec.id });
  const colorKey =
    spec.study === "reference" || spec.study === "strip"
      ? "vivid"
      : ["raffia", "indigo", "earth"].includes(spec.study)
        ? spec.study
        : spec.name.match(/Ivory|Pearl|Sandstone/)
          ? "ivory"
          : spec.name.match(/Ruby|Crimson|Madder|Red|Rust|Terracotta/)
            ? "ruby"
            : spec.name.match(/Celadon|Emerald|Moss|Juniper|Teal/)
              ? "teal"
              : "navy";
  const colors = rugPalettes[colorKey],
    buckets = new Map();
  let elements = 0;
  function add(z, k, points) {
    if (points.length < 3) return;
    const key = z + ":" + k;
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(rugPath(points));
    elements++;
  }
  function box(z, k, x, y, w, h) {
    add(z, k, rugBox(x, y, w, h));
  }
  function stroke(z, k, pts, width) {
    const left = [],
      right = [];
    for (let i = 0; i < pts.length; i++) {
      const a = pts[Math.max(0, i - 1)],
        b = pts[Math.min(pts.length - 1, i + 1)],
        len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1,
        nx = ((-(b[1] - a[1]) / len) * width) / 2,
        ny = (((b[0] - a[0]) / len) * width) / 2;
      left.push([pts[i][0] + nx, pts[i][1] + ny]);
      right.push([pts[i][0] - nx, pts[i][1] - ny]);
    }
    add(z, k, [...left, ...right.reverse()]);
  }
  function disk(z, k, x, y, r, n = 12) {
    add(
      z,
      k,
      rugRadial(n, r).map(([a, b]) => [x + a, y + b]),
    );
  }
  function frame(z, k, inset, width) {
    const outer = rugBox(inset, inset, 512 - 2 * inset, 512 - 2 * inset),
      inner = rugBox(
        inset + width,
        inset + width,
        512 - 2 * (inset + width),
        512 - 2 * (inset + width),
      ).reverse();
    const key = z + ":" + k;
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(rugPath(outer) + rugPath(inner));
    elements++;
  }
  function motif(type, x, y, r, k = 1, bg = 0, z = 2, angle = 0) {
    const co = Math.cos(angle),
      si = Math.sin(angle),
      tr = ([a, b]) => [x + r * (a * co - b * si), y + r * (a * si + b * co)];
    const p = (s, c, pts) => add(z + s, c, pts.map(tr)),
      line = (s, c, pts, w = 0.05) => stroke(z + s, c, pts.map(tr), w * r);
    const circle = (s, c, a, b, size, n = 14) =>
      p(
        s,
        c,
        rugRadial(n, size).map(([u, v]) => [a + u, b + v]),
      );
    const next = k === 1 ? 3 : 1;
    const petals = (s, c, n, radius = 0.66, size = 0.34) => {
      for (let j = 0; j < n; j++) {
        const a = (j * 2 * Math.PI) / n;
        const pts = Array.from({ length: r < 10 ? 8 : 16 }, (_, i) => {
          const t = (i * 2 * Math.PI) / (r < 10 ? 8 : 16),
            rad = size * (0.68 + 0.32 * Math.cos(t));
          return [
            Math.cos(a) * radius +
              Math.cos(t) * rad * Math.cos(a) -
              Math.sin(t) * rad * 0.5 * Math.sin(a),
            Math.sin(a) * radius +
              Math.cos(t) * rad * Math.sin(a) +
              Math.sin(t) * rad * 0.5 * Math.cos(a),
          ];
        });
        p(s, c, pts);
      }
    };
    if (type === "maze") {
      const pts = [
        [-0.9, 0.9],
        [-0.9, -0.9],
        [0.9, -0.9],
        [0.9, 0.9],
        [-0.63, 0.9],
        [-0.63, -0.62],
        [0.62, -0.62],
        [0.62, 0.62],
        [-0.35, 0.62],
        [-0.35, -0.34],
        [0.34, -0.34],
        [0.34, 0.34],
        [-0.08, 0.34],
        [-0.08, -0.07],
      ];
      const turned = pts.map(([u, v]) => (k % 2 ? [u, v] : [-u, v]));
      line(0, k, turned, 0.17);
      line(1, bg, turned, 0.055);
      if (opts.detail > 1) {
        line(
          2,
          next,
          [
            [-0.68, -0.74],
            [0.76, -0.74],
            [0.76, 0.75],
          ],
          0.025,
        );
      }
      return;
    }
    if (
      ["nested", "stepped", "hook", "quarters", "octagon", "maze"].includes(
        type,
      )
    ) {
      const shape =
        type === "octagon"
          ? rugRadial(8, 1, null, Math.PI / 8)
          : type === "stepped"
            ? [
                [0, -1],
                [0.24, -0.76],
                [0.4, -0.76],
                [0.4, -0.6],
                [0.6, -0.6],
                [0.6, -0.4],
                [0.76, -0.4],
                [0.76, -0.24],
                [1, 0],
                [0.76, 0.24],
                [0.76, 0.4],
                [0.6, 0.4],
                [0.6, 0.6],
                [0.4, 0.6],
                [0.4, 0.76],
                [0.24, 0.76],
                [0, 1],
                [-0.24, 0.76],
                [-0.4, 0.76],
                [-0.4, 0.6],
                [-0.6, 0.6],
                [-0.6, 0.4],
                [-0.76, 0.4],
                [-0.76, 0.24],
                [-1, 0],
                [-0.76, -0.24],
                [-0.76, -0.4],
                [-0.6, -0.4],
                [-0.6, -0.6],
                [-0.4, -0.6],
                [-0.4, -0.76],
                [-0.24, -0.76],
              ]
            : rugRadial(4);
      p(0, k, shape);
      p(
        1,
        bg,
        shape.map((a) => a.map((v) => v * 0.87)),
      );
      p(
        2,
        next,
        shape.map((a) => a.map((v) => v * 0.72)),
      );
      p(
        3,
        bg,
        shape.map((a) => a.map((v) => v * 0.57)),
      );
      if (type === "quarters")
        for (let j = 0; j < 4; j++) {
          const a = (j * Math.PI) / 2;
          p(4, j % 2 ? k : next, [
            [0, 0],
            [Math.cos(a) * 0.51, Math.sin(a) * 0.51],
            [
              Math.cos(a + Math.PI / 2) * 0.51,
              Math.sin(a + Math.PI / 2) * 0.51,
            ],
          ]);
        }
      else if (type === "maze")
        for (let q = 0; q < 4; q++) {
          const a = (q * Math.PI) / 2;
          const pts = [
            [0, -0.5],
            [0.38, -0.12],
            [0.15, 0.11],
            [-0.07, -0.11],
            [0.05, -0.23],
          ].map(([u, v]) => [
            u * Math.cos(a) - v * Math.sin(a),
            u * Math.sin(a) + v * Math.cos(a),
          ]);
          line(4, k, pts, 0.065);
        }
      else p(4, k, rugRadial(type === "octagon" ? 8 : 4, 0.34));
      if (type === "hook" || type === "stepped")
        for (let a = 0; a < 4; a++) {
          const co2 = Math.cos((a * Math.PI) / 2),
            si2 = Math.sin((a * Math.PI) / 2);
          line(
            4,
            k,
            [
              [0.08, -0.88],
              [0.32, -1.12],
              [0.48, -0.96],
              [0.35, -0.84],
            ].map(([u, v]) => [u * co2 - v * si2, u * si2 + v * co2]),
            0.085,
          );
        }
      if (opts.detail > 1)
        for (let j = 0; j < 4; j++)
          circle(
            4,
            next,
            Math.cos((j * Math.PI) / 2) * 0.48,
            Math.sin((j * Math.PI) / 2) * 0.48,
            0.035,
            6,
          );
    } else if (
      ["chevron", "comb", "stripe", "checks", "rings", "dots"].includes(type)
    ) {
      // Compound inlay / embroidery cells replace sparse dots and zigzag rows.
      // Retain the legacy recipe keys so saved composition controls still load.
      const diamond = (s, c, cx, cy, rr) =>
        p(s, c, [
          [cx, cy - rr],
          [cx + rr, cy],
          [cx, cy + rr],
          [cx - rr, cy],
        ]);
      if (type === "rings" || type === "dots") {
        const n = type === "rings" ? 8 : 4;
        p(0, k, rugRadial(n, 1, 0.72));
        p(1, bg, rugRadial(n, 0.86, 0.72));
        for (let q = 0; q < n; q++) {
          const a = (q * Math.PI * 2) / n,
            xx = Math.cos(a) * 0.55,
            yy = Math.sin(a) * 0.55;
          diamond(2, next, xx, yy, 0.26);
          diamond(3, bg, xx, yy, 0.16);
          line(
            4,
            k,
            [
              [xx - 0.11, yy],
              [xx, yy - 0.11],
              [xx + 0.11, yy],
              [xx, yy + 0.11],
              [xx - 0.11, yy],
            ],
            0.03,
          );
        }
        diamond(2, k, 0, 0, 0.31);
        diamond(3, bg, 0, 0, 0.22);
        diamond(4, next, 0, 0, 0.12);
      } else if (type === "checks") {
        for (let j = 0; j < 3; j++)
          for (let i = 0; i < 3; i++) {
            const xx = -0.64 + i * 0.64,
              yy = -0.64 + j * 0.64;
            p(0, k, rugBox(xx - 0.3, yy - 0.3, 0.6, 0.6));
            diamond(1, bg, xx, yy, 0.29);
            diamond(2, next, xx, yy, 0.2);
            p(3, bg, [
              [xx, yy],
              [xx + 0.17, yy],
              [xx, yy + 0.17],
            ]);
          }
      } else if (type === "stripe" || type === "comb") {
        const vertical = type === "comb",
          swap = (pts) => (vertical ? pts.map(([a, b]) => [b, a]) : pts);
        for (let j = 0; j < 3; j++) {
          const yy = -0.62 + j * 0.62;
          p(0, k, swap(rugBox(-0.98, yy - 0.26, 1.96, 0.52)));
          p(1, bg, swap(rugBox(-0.91, yy - 0.2, 1.82, 0.4)));
          for (let q = -1; q <= 1; q++) {
            const pts = [
              [q * 0.6, yy - 0.19],
              [q * 0.6 + 0.25, yy],
              [q * 0.6, yy + 0.19],
              [q * 0.6 - 0.25, yy],
            ];
            p(2, next, swap(pts));
            p(
              3,
              bg,
              swap(
                pts.map(([a, b]) => [
                  q * 0.6 + (a - q * 0.6) * 0.52,
                  yy + (b - yy) * 0.52,
                ]),
              ),
            );
          }
          for (let q = 0; q < 9; q++)
            line(
              4,
              k,
              swap([
                [-0.86 + q * 0.21, yy - 0.2],
                [-0.86 + q * 0.21, yy + 0.2],
              ]),
              0.015,
            );
        }
      } else {
        for (let q = 0; q < 4; q++) {
          const a = (q * Math.PI) / 2,
            rotate = (pts) =>
              pts.map(([u, v]) => [
                u * Math.cos(a) - v * Math.sin(a),
                u * Math.sin(a) + v * Math.cos(a),
              ]);
          p(
            0,
            k,
            rotate([
              [-0.98, -0.98],
              [0.98, -0.98],
              [0, 0],
            ]),
          );
          p(
            1,
            bg,
            rotate([
              [-0.77, -0.85],
              [0.77, -0.85],
              [0, -0.11],
            ]),
          );
          p(
            2,
            next,
            rotate([
              [-0.49, -0.73],
              [0.49, -0.73],
              [0, -0.24],
            ]),
          );
          line(
            3,
            bg,
            rotate([
              [-0.6, -0.79],
              [0, -0.39],
              [0.6, -0.79],
            ]),
            0.055,
          );
          line(
            4,
            k,
            rotate([
              [-0.95, -0.98],
              [-0.95, -0.6],
              [-0.6, -0.6],
              [-0.6, -0.3],
              [-0.3, -0.3],
            ]),
            0.035,
          );
        }
      }
    } else if (type === "star8" || type === "star12" || type === "knot") {
      const n = type === "star12" ? 12 : 8,
        shape = rugRadial(n, 1, n === 8 ? 0.765 : 0.78);
      p(0, k, shape);
      p(
        1,
        bg,
        shape.map((a) => a.map((v) => v * 0.87)),
      );
      p(
        2,
        next,
        shape.map((a) => a.map((v) => v * 0.69)),
      );
      p(
        3,
        bg,
        shape.map((a) => a.map((v) => v * 0.52)),
      );
      if (type === "knot")
        for (let j = 0; j < 4; j++) {
          const a = (j * Math.PI) / 4;
          const sq = rugRadial(4, 0.75, null, a);
          line(4, k, [...sq, sq[0]], 0.032);
        }
      else {
        petals(4, k, n, 0.26, 0.14);
        circle(4, next, 0, 0, 0.14, 12);
      }
      if (opts.detail > 1)
        for (let j = 0; j < n; j++) {
          const a = (j * 2 * Math.PI) / n;
          line(
            4,
            k,
            [
              [Math.cos(a) * 0.76, Math.sin(a) * 0.76],
              [Math.cos(a) * 0.95, Math.sin(a) * 0.95],
            ],
            0.018,
          );
        }
    } else if (type === "flower" || type === "rosette" || type === "lobed") {
      const n = type === "flower" ? 6 : type === "rosette" ? 10 : 8,
        samples = n * (r < 8 ? 2 : r < 16 ? 3 : 6),
        shape = Array.from({ length: samples }, (_, i) => {
          const a = (i * 2 * Math.PI) / samples,
            q =
              type === "lobed"
                ? 0.94 + 0.06 * Math.cos(a * n)
                : 0.86 + 0.14 * Math.cos(a * n);
          return [Math.cos(a) * q, Math.sin(a) * q];
        });
      p(0, k, shape);
      p(
        1,
        bg,
        shape.map((a) => a.map((v) => v * 0.91)),
      );
      p(
        2,
        next,
        shape.map((a) => a.map((v) => v * 0.8)),
      );
      p(
        3,
        bg,
        shape.map((a) => a.map((v) => v * 0.69)),
      );
      petals(4, k, type === "flower" ? 6 : 8, 0.43, 0.21);
      circle(4, next, 0, 0, 0.21, 16);
      circle(5, k, 0, 0, 0.09, 10);
      if (type !== "flower" && opts.detail > 1)
        for (let j = 0; j < n; j++) {
          const a = (j * 2 * Math.PI) / n;
          circle(4, 1, Math.cos(a) * 0.83, Math.sin(a) * 0.83, 0.025, 6);
        }
    } else if (type === "leaf" || type === "palmette") {
      if (type === "palmette") {
        for (let j = -3; j <= 3; j++) {
          const a = -Math.PI / 2 + j * 0.3;
          const pts = Array.from({ length: 16 }, (_, i) => {
            const t = (i * 2 * Math.PI) / 16;
            return [
              Math.cos(a) * 0.4 +
                Math.cos(t) * 0.52 * Math.cos(a) -
                Math.sin(t) * 0.18 * Math.sin(a),
              0.3 +
                Math.sin(a) * 0.4 +
                Math.cos(t) * 0.52 * Math.sin(a) +
                Math.sin(t) * 0.18 * Math.cos(a),
            ];
          });
          p(0, k, pts);
          line(
            1,
            bg,
            [
              [0, 0.8],
              [Math.cos(a) * 0.78, 0.3 + Math.sin(a) * 0.78],
            ],
            0.03,
          );
        }
        circle(2, next, 0, 0.27, 0.25);
      } else {
        const leaf = Array.from({ length: 26 }, (_, i) => {
          const t = (i * 2 * Math.PI) / 26;
          return [
            Math.sin(t) * 0.48 * (0.8 + 0.2 * Math.cos(t * 5)),
            Math.cos(t),
          ];
        });
        p(0, k, leaf);
        line(
          1,
          bg,
          [
            [0, -0.9],
            [0.05, 0],
            [0, 0.9],
          ],
          0.035,
        );
        if (opts.detail > 1)
          for (let j = -3; j <= 3; j++)
            for (let sign of [-1, 1])
              line(
                1,
                bg,
                [
                  [0, j * 0.21],
                  [sign * 0.36, j * 0.21 - 0.19],
                ],
                0.017,
              );
      }
    } else if (type === "paisley") {
      const pts = [
        [-0.05, -1],
        [0.3, -0.8],
        [0.6, -0.6],
        [0.78, -0.24],
        [0.76, 0.23],
        [0.53, 0.68],
        [0.12, 0.9],
        [-0.32, 0.85],
        [-0.62, 0.55],
        [-0.74, 0.16],
        [-0.63, -0.21],
        [-0.36, -0.44],
        [-0.02, -0.51],
        [0.21, -0.48],
        [0.1, -0.77],
      ];
      p(0, k, pts);
      p(
        1,
        bg,
        pts.map((a) => a.map((v) => v * 0.86)),
      );
      p(
        2,
        next,
        pts.map((a) => a.map((v) => v * 0.7)),
      );
      p(
        3,
        bg,
        pts.map((a) => a.map((v) => v * 0.58)),
      );
      petals(4, k, 5, 0.2, 0.14);
      circle(5, 1, 0, 0, 0.08, 8);
    } else if (type === "vine" || type === "tree") {
      const vine = Array.from({ length: 25 }, (_, i) => {
        const t = i / 24;
        return [
          type === "tree" ? 0 : Math.sin(t * Math.PI * 2) * 0.32,
          -0.96 + t * 1.92,
        ];
      });
      line(0, k, vine, 0.035);
      for (let j = 0; j < 5; j++)
        for (let sign of [-1, 1]) {
          const v = -0.7 + j * 0.32,
            u =
              type === "tree"
                ? 0
                : Math.sin(((v + 0.96) / 1.92) * Math.PI * 2) * 0.32,
            tip = [u + sign * (type === "tree" ? 0.64 : 0.48), v - 0.23];
          line(0, k, [[u, v], tip], 0.025);
          const a = Math.atan2(tip[1] - v, tip[0] - u);
          const pts = Array.from({ length: 12 }, (_, i) => {
            const t = (i * Math.PI) / 6;
            return [
              tip[0] +
                Math.cos(t) * 0.19 * Math.cos(a) -
                Math.sin(t) * 0.08 * Math.sin(a),
              tip[1] +
                Math.cos(t) * 0.19 * Math.sin(a) +
                Math.sin(t) * 0.08 * Math.cos(a),
            ];
          });
          p(1, j % 2 ? next : k, pts);
          if (opts.detail > 1)
            circle(2, 2, tip[0] + sign * 0.12, tip[1] - 0.07, 0.055, 8);
        }
    }
  }
  box(0, 0, 0, 0, 512, 512);
  const margin = opts.borderWidth,
    span = 512 - 2 * margin,
    delta = opts.detail - 2,
    nx = Math.max(3, spec.cols + delta),
    ny = Math.max(3, spec.rows + delta),
    cw = span / nx,
    ch = span / ny;
  const colorful = spec.study === "reference" || spec.study === "strip";
  function cell(type, cx, cy, r, ix, iy, bg = 0, z = 2) {
    motif(
      type,
      cx,
      cy,
      r,
      colorful ? 1 + ((ix + iy * 2) % 5) : 1 + ((ix + iy) % 3),
      bg,
      z,
      type === "paisley" && ix % 2 ? Math.PI : 0,
    );
  }
  if (opts.field) {
    if (
      ["strips", "bands", "compartments", "patchwork", "garden"].includes(
        spec.layout,
      )
    ) {
      for (let j = 0; j < ny; j++)
        for (let i = 0; i < nx; i++) {
          // Offset strip blocks and staggered patchwork are structural, not seed/color changes.
          const shift = spec.layout === "strips" && i % 2 ? 0.32 : 0;
          const weights = Array.from(
              { length: nx },
              (_, q) => [0.85, 1.3, 0.85][q % 3],
            ),
            heights = Array.from(
              { length: ny },
              (_, q) => [0.75, 1.15, 1.1][(q + i) % 3],
            );
          const totalW = weights.reduce((a, b) => a + b, 0),
            totalH = heights.reduce((a, b) => a + b, 0),
            patch = spec.layout === "patchwork";
          const pw = patch ? (span * weights[i]) / totalW : cw,
            ph = patch ? (span * heights[j]) / totalH : ch;
          const x =
              margin +
              (patch
                ? (span * weights.slice(0, i).reduce((a, b) => a + b, 0)) /
                  totalW
                : i * cw),
            y =
              margin +
              (patch
                ? (span * heights.slice(0, j).reduce((a, b) => a + b, 0)) /
                  totalH
                : (j + shift) * ch);
          if (y + ph > 512 - margin + 0.001) continue;
          const isPanel =
            spec.layout === "compartments" ||
            spec.layout === "patchwork" ||
            spec.layout === "garden";
          const bg = isPanel && colorful ? ((i + j) % 2 ? 4 : 0) : 0;
          if (isPanel) {
            box(1, 1, x + 1, y + 1, pw - 2, ph - 2);
            box(2, bg, x + 2.4, y + 2.4, pw - 4.8, ph - 4.8);
          }
          if (spec.layout === "strips") {
            const stripColor = colorful ? 1 + (i % 5) : 1;
            box(1, stripColor, x, y, 1.3, ph);
            box(1, stripColor, x + pw - 2, y, 1.3, ph);
          }
          const variant = (i + j) % 3 === 0 ? spec.secondary : spec.motif;
          cell(
            variant,
            x + pw / 2,
            y + ph / 2,
            Math.min(pw, ph) * (spec.study === "raffia" ? 0.49 : 0.43),
            i,
            j,
            bg,
            isPanel ? 3 : 2,
          );
          if (opts.detail > 1 && isPanel) {
            for (const [dx, dy] of [
              [0.14, 0.14],
              [0.86, 0.14],
              [0.14, 0.86],
              [0.86, 0.86],
            ])
              disk(7, 1, x + pw * dx, y + ph * dy, 1, 6);
          }
        }
      if (spec.layout === "garden") {
        box(7, 3, margin + span / 2 - 3, margin, 6, span);
        box(7, 3, margin, margin + span / 2 - 3, span, 6);
        for (let t = margin; t <= 512 - margin; t += 8) {
          disk(8, 1, 256, t, 1, 6);
          disk(8, 1, t, 256, 1, 6);
        }
      }
    } else {
      for (let j = 0; j < ny; j++)
        for (let i = 0; i < nx; i++) {
          let x = margin + (i + 0.5) * cw,
            y = margin + (j + 0.5) * ch;
          if (["diagonal", "ogival", "trellis"].includes(spec.layout))
            x += (j % 2) * cw * 0.5;
          if (x > 512 - margin - cw * 0.12) continue;
          const r = Math.min(cw, ch) * (spec.layout === "kilim" ? 0.57 : 0.43);
          cell((i + j) % 4 === 0 ? spec.secondary : spec.motif, x, y, r, i, j);
          if (["trellis", "ogival"].includes(spec.layout)) {
            if (spec.layout === "trellis")
              stroke(
                1,
                3,
                [
                  [x - cw / 2, y],
                  [x, y - ch / 2],
                  [x + cw / 2, y],
                  [x, y + ch / 2],
                  [x - cw / 2, y],
                ],
                0.8,
              );
            else {
              const points = Array.from({ length: 41 }, (_, q) => {
                const a = (q * 2 * Math.PI) / 40;
                return [
                  x + Math.sin(a) * cw * 0.49,
                  y + Math.cos(a) * ch * 0.52,
                ];
              });
              stroke(1, 3, points, 0.8);
            }
          }
          if (opts.detail === 3) {
            add(
              7,
              3,
              rugRadial(6, Math.min(cw, ch) * 0.085, 0.4).map(([u, v]) => [
                x - cw * 0.42 + u,
                y - ch * 0.4 + v,
              ]),
            );
          }
        }
    }
  }
  const floral = ["court", "garden", "floral", "ushak", "compartment"].includes(
    spec.study,
  );
  function fleuron(z, x, y, r, k = 2) {
    add(
      z,
      k,
      Array.from({ length: 24 }, (_, q) => {
        const a = (q * Math.PI) / 12,
          rad = r * (0.76 + 0.24 * Math.cos(a * 6));
        return [x + Math.cos(a) * rad, y + Math.sin(a) * rad];
      }),
    );
    disk(z + 1, 1, x, y, r * 0.22, 6);
  }
  if (opts.field && opts.detail > 1) {
    for (let j = 0; j < ny; j++)
      for (let i = 0; i < nx; i++) {
        const x = margin + (i + 0.5) * cw,
          y = margin + (j + 0.5) * ch;
        if (floral) {
          // Two interleaving stems with buds and pointed leaves between major ornaments.
          for (const sign of [-1, 1]) {
            const stem = Array.from({ length: 15 }, (_, q) => {
              const t = q / 14;
              return [
                x + sign * (cw * 0.3 + Math.sin(t * Math.PI * 2) * cw * 0.15),
                y - ch * 0.48 + t * ch * 0.96,
              ];
            });
            stroke(7, 3, stem, 0.55);
            for (const t of [0.12, 0.38, 0.68, 0.88]) {
              const xx =
                  x + sign * (cw * 0.3 + Math.sin(t * Math.PI * 2) * cw * 0.15),
                yy = y - ch * 0.48 + t * ch * 0.96;
              add(7, 3, [
                [xx, yy],
                [xx + sign * 3, yy - 5],
                [xx + sign * 7, yy - 6],
                [xx + sign * 4, yy - 1],
              ]);
              fleuron(
                7,
                xx - sign * 2.5,
                yy - 1.5,
                Math.min(cw, ch) * (opts.detail === 3 ? 0.052 : 0.043),
                (i + j) % 2 ? 2 : 1,
              );
            }
          }
        } else if (spec.group === "African compositions") {
          for (const [u, v] of [
            [-0.44, -0.4],
            [0.44, 0.4],
          ]) {
            const xx = x + u * cw,
              yy = y + v * ch;
            add(
              7,
              colorful ? 3 : 1,
              rugRadial(4, Math.min(cw, ch) * 0.095).map(([a, b]) => [
                xx + a,
                yy + b,
              ]),
            );
            add(
              8,
              0,
              rugRadial(4, Math.min(cw, ch) * 0.055).map(([a, b]) => [
                xx + a,
                yy + b,
              ]),
            );
          }
          if (spec.study === "reference")
            for (let q = 0; q < 4; q++)
              stroke(
                7,
                (i + j) % 2 ? 4 : 3,
                [
                  [x - cw * 0.35 + q * 2, y - ch * 0.48],
                  [x - cw * 0.15 + q * 2, y - ch * 0.31],
                ],
                0.65,
              );
        }
      }
  }
  if (opts.medallions) {
    const center = spec.center === "none" ? null : spec.center;
    function med(x, y, r, type = center) {
      if (!type) return;
      if (spec.study === "reference" && type === "stepped") {
        for (let ring = 0; ring < 6; ring++)
          add(
            9 + ring,
            [0, 1, 4, 2, 3, 5][ring],
            rugRadial(4, r * (1 - ring * 0.125)).map(([u, v]) => [
              x + u,
              y + v,
            ]),
          );
      } else motif(type, x, y, r, 1, 0, 9);
      if (r > 45) {
        if (floral) {
          add(
            15,
            3,
            rugRadial(12, r * 0.55, 0.92).map(([u, v]) => [x + u, y + v]),
          );
          for (let j = -3; j <= 3; j++)
            for (let i = -3; i <= 3; i++)
              if (i * i + j * j < 13) {
                const xx = x + i * r * 0.142,
                  yy = y + j * r * 0.142;
                const pts = Array.from({ length: 12 }, (_, q) => {
                  const t = q / 11;
                  return [
                    xx + Math.sin(t * Math.PI) * r * 0.035,
                    yy + (t - 0.5) * r * 0.15,
                  ];
                });
                stroke(16, 0, pts, Math.max(0.4, r * 0.006));
                fleuron(18, xx, yy, r * 0.042, (i + j) % 2 ? 2 : 4);
                add(18, 0, [
                  [xx, yy + r * 0.03],
                  [xx + r * 0.05, yy + r * 0.07],
                  [xx + r * 0.065, yy + r * 0.065],
                  [xx + r * 0.025, yy + r * 0.025],
                ]);
              }
          motif("flower", x, y, r * 0.11, 1, 3, 16);
        } else {
          for (let ring = 0; ring < 5; ring++) {
            const rad = r * (0.935 - ring * 0.125),
              corners = rugRadial(4, rad).map(([u, v]) => [x + u, y + v]);
            stroke(16, 0, [...corners, corners[0]], 0.6);
            for (let edge = 0; edge < 4; edge++)
              for (let q = 1; q < 18; q++) {
                const a = corners[edge],
                  b = corners[(edge + 1) % 4],
                  t = q / 18,
                  xx = a[0] * (1 - t) + b[0] * t,
                  yy = a[1] * (1 - t) + b[1] * t;
                disk(17, 1, xx, yy, 0.6, 6);
              }
          }
          motif("quarters", x, y, r * 0.18, 1, 0, 16);
        }
      }
    }
    if (spec.layout === "medallion" || spec.layout === "bordered") {
      med(256, 256, span * (spec.layout === "bordered" ? 0.29 : 0.36));
      for (const y of [256 - span * 0.43, 256 + span * 0.43])
        motif("palmette", 256, y, span * 0.08, 3, 0, 9, y > 256 ? Math.PI : 0);
    }
    if (spec.layout === "triple") {
      med(256, 256, span * 0.31);
      med(margin + span * 0.13, 256, span * 0.135);
      med(512 - margin - span * 0.13, 256, span * 0.135);
    }
    if (spec.layout === "vertical") {
      med(256, margin + span * 0.22, span * 0.18);
      med(256, 256, span * 0.215);
      med(256, margin + span * 0.78, span * 0.18);
    }
    if (spec.layout === "cross") {
      med(256, 256, span * 0.215);
      for (let i = 0; i < 4; i++)
        med(
          256 + Math.cos((i * Math.PI) / 2) * span * 0.32,
          256 + Math.sin((i * Math.PI) / 2) * span * 0.32,
          span * 0.11,
        );
    }
    if (spec.layout === "radial") {
      med(256, 256, span * 0.26);
      for (let i = 0; i < 12; i++) {
        const a = (i * Math.PI) / 6;
        motif(
          "lobed",
          256 + Math.cos(a) * span * 0.37,
          256 + Math.sin(a) * span * 0.37,
          span * 0.078,
          1 + (i % 3),
          0,
          9,
          a,
        );
      }
    }
    if (spec.layout === "tree") {
      motif("tree", 256, 256, span * 0.44, 3, 0, 9);
      for (let j = 0; j < 5; j++)
        for (const sign of [-1, 1])
          motif(
            "flower",
            256 + sign * span * (0.28 - j * 0.035),
            256 + span * (0.33 - j * 0.15),
            span * 0.043,
            2,
            0,
            15,
          );
    }
    if (spec.layout === "arch") {
      const x0 = margin + 12,
        x1 = 512 - margin - 12,
        y0 = margin + span * 0.31;
      const arch = [
        [x0, 512 - margin],
        [x0, y0],
        [x0 + span * 0.08, y0 - span * 0.11],
        [256, margin + 10],
        [x1 - span * 0.08, y0 - span * 0.11],
        [x1, y0],
        [x1, 512 - margin],
      ];
      stroke(9, 1, arch, 8);
      stroke(10, 3, arch, 3.6);
      motif("palmette", 256, margin + span * 0.22, span * 0.07, 1, 0, 11);
      if (center) med(256, margin + span * 0.63, span * 0.18);
    }
    if (spec.layout === "vases")
      for (let j = 0; j < 3; j++)
        for (let i = 0; i < 3; i++) {
          const x = margin + (span * (i + 0.5)) / 3,
            y = margin + (span * (j + 0.63)) / 3,
            r = span * 0.062;
          add(9, 3, [
            [x - r * 0.4, y - r * 0.2],
            [x - r * 0.6, y + r * 0.5],
            [x - r * 0.35, y + r],
            [x + r * 0.35, y + r],
            [x + r * 0.6, y + r * 0.5],
            [x + r * 0.4, y - r * 0.2],
          ]);
          motif("palmette", x, y - r * 0.6, r * 1.4, 1, 0, 10);
          motif("flower", x, y - r * 1.7, r * 0.45, 2, 0, 15);
        }
    if (center && ["garden", "trellis"].includes(spec.layout))
      med(256, 256, span * 0.19);
  }
  if (
    opts.corners &&
    [
      "medallion",
      "radial",
      "triple",
      "cross",
      "arch",
      "bordered",
      "vertical",
    ].includes(spec.layout)
  )
    for (const x of [margin, 512 - margin])
      for (const y of [margin, 512 - margin])
        motif(
          spec.group === "African compositions" ? "stepped" : "lobed",
          x,
          y,
          span * 0.15,
          3,
          0,
          9,
        );
  // Solid compound rings mask all field spill at the perimeter. Bordered rugs
  // are finite compositions: the runtime clamps both axes instead of wrapping.
  frame(22, 0, 0, margin);
  frame(22, 1, 4, 1.5);
  frame(22, 3, 8, 2);
  frame(22, 1, margin - 7, 1.5);
  frame(22, 3, margin - 3, 1.2);
  const borderCenter = (margin + 8) / 2,
    br = Math.max(5, (margin - 19) / 2);
  if (opts.borderOrnaments) {
    const n = Math.max(8, Math.round((512 - 2 * margin) / (br * 2.35))),
      step = (512 - 2 * margin) / n;
    const btype = {
      hooks: "hook",
      meander: "maze",
      diamonds: "nested",
      teeth: "chevron",
      checks: "quarters",
      dots: "rings",
      cartouches: "lobed",
      rosettes: "flower",
      stars: "star8",
      vine: "palmette",
    }[spec.border];
    for (let i = 0; i < n; i++)
      for (let side = 0; side < 4; side++) {
        const t = margin + (i + 0.5) * step,
          pts = [
            [t, borderCenter],
            [512 - borderCenter, t],
            [512 - t, 512 - borderCenter],
            [borderCenter, 512 - t],
          ][side];
        motif(
          btype,
          ...pts,
          Math.min(br, step * 0.44),
          i % 2 ? 1 : 3,
          0,
          23,
          (side * Math.PI) / 2,
        );
        if (opts.detail > 1) {
          const q = t - step * 0.48,
            ps = [
              [q, borderCenter],
              [512 - borderCenter, q],
              [512 - q, 512 - borderCenter],
              [borderCenter, 512 - q],
            ][side];
          disk(29, 2, ...ps, 1.1, 8);
        }
      }
    if (opts.detail > 1) {
      for (let side = 0; side < 4; side++) {
        const transform = (t, v) =>
          [
            [t, v],
            [512 - v, t],
            [512 - t, 512 - v],
            [v, 512 - t],
          ][side];
        const curve = Array.from({ length: n * 12 + 1 }, (_, q) =>
          transform(
            margin + (q * (512 - 2 * margin)) / (n * 12),
            borderCenter + Math.sin((q * Math.PI) / 6) * br * 0.32,
          ),
        );
        stroke(23, 3, curve, 0.55);
        for (let q = margin; q < 512 - margin; q += 4) {
          const pt = transform(q, margin - 10);
          add(29, 1, [
            [pt[0] - 0.55, pt[1] - 0.55],
            [pt[0] + 0.55, pt[1] - 0.55],
            [pt[0] + 0.55, pt[1] + 0.55],
            [pt[0] - 0.55, pt[1] + 0.55],
          ]);
        }
      }
    }
    for (let side = 0; side < 4; side++)
      for (let i = 10; i < 502; i += 5.5) {
        const pts = [
          [i, 12],
          [500, i],
          [512 - i, 500],
          [12, 512 - i],
        ][side];
        disk(29, 1, ...pts, 0.65, 6);
      }
    for (const x of [borderCenter, 512 - borderCenter])
      for (const y of [borderCenter, 512 - borderCenter])
        motif(
          spec.group === "African compositions" ? "quarters" : "rosette",
          x,
          y,
          br,
          1,
          0,
          23,
        );
  }
  const roleFor = (z) =>
    z === 0
      ? "Ground"
      : z < 9
        ? "Field"
        : z < 22
          ? "Medallion & corners"
          : "Border";
  const layers = [];
  for (const [key, paths] of [...buckets].sort(
    (a, b) => Number(a[0].split(":")[0]) - Number(b[0].split(":")[0]),
  )) {
    const [z, k] = key.split(":").map(Number);
    let chunk = "",
      part = 0;
    const flush = () => {
      if (!chunk) return;
      layers.push({
        kind: "path",
        name: `${roleFor(z)} / ${z} / pigment ${k + 1}`,
        ornamentRole: key + ":" + part++,
        path: chunk,
        x: 256,
        y: 256,
        width: 512,
        height: 512,
        color: colors[k],
        finish: "wool",
        roughness: 0.94,
        metalness: 0,
        relief: z === 0 ? 0.15 : 0.26 + (k % 3) * 0.055,
        opacity: 1,
      });
      chunk = "";
    };
    for (const path of paths) {
      if (chunk.length + path.length > 95000) flush();
      chunk += path;
    }
    flush();
  }
  if (layers.length > 61)
    throw new Error(
      "This composition exceeds the 61-layer design budget; lower its detail.",
    );
  return {
    schema: "alloy.pattern.v1",
    name: spec.name,
    collection: spec.group,
    presentation: "rug",
    library: { family: spec.name, palette: "Designed composition" },
    ornament: opts,
    tileAxes: "none",
    background: colors[0],
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers,
  };
}
export function rebuildRugComposition(doc, patch = {}) {
  const settings = normalizeRugComposition({ ...doc.ornament, ...patch }),
    next = richRugPattern(settings.id, settings);
  const old = new Map(
    doc.layers.filter((l) => l.ornamentRole).map((l) => [l.ornamentRole, l]),
  );
  next.layers = next.layers.map((l) => {
    const p = old.get(l.ornamentRole);
    if (!p) return l;
    const { color, finish, roughness, metalness, relief, opacity, visible } = p;
    return {
      ...l,
      color,
      finish,
      roughness,
      metalness,
      relief,
      opacity,
      visible,
    };
  });
  next.layers.push(
    ...doc.layers.filter((l) => !l.ornamentRole && !l.stitchRole),
  );
  if (next.layers.length > 64)
    throw new Error(
      "Rebuilding would exceed 64 layers. Remove extra motifs or reduce detail.",
    );
  const result = {
    ...next,
    name: doc.name,
    background: doc.background,
    backgroundOpacity: doc.backgroundOpacity,
    mapping: doc.mapping,
    rotation: doc.rotation,
    repeats: doc.repeats,
  };
  return doc.stitch ? applyStitches(result, doc.stitch) : result;
}
