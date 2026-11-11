import { fibonacciSquareSpiral } from "./patternProcedural.js";

// Set 02 consists of independent constructions, not palette/rotation variants.
export const geometricSetTwo = [
  [
    "Linked racetrack loops",
    "linked-racetrack-loops",
    "Two closed stadium paths interlace at four alternating over/under crossings.",
  ],
  [
    "Hexagon-square junctions",
    "hexagon-square-junctions",
    "Regular hexagons, squares and triangular gaps meet in a 3.4.6.4 tessellation.",
  ],
  [
    "Pinwheel square tessellation",
    "pinwheel-square-tessellation",
    "Large and small squares fill a Pythagorean lattice with a 3:1 edge ratio.",
  ],
  [
    "Tangram mosaic",
    "tangram-mosaic",
    "A complete seven-piece square dissection: five triangles, a square and a parallelogram.",
  ],
  [
    "Sierpinski lace",
    "sierpinski-lace",
    "Three recursive subdivisions leave triangular cutouts inside an equilateral-triangle field.",
  ],
  [
    "Vesica net",
    "vesica-net",
    "Intersecting equal-radius circles define joined horizontal and vertical lens regions.",
  ],
  [
    "Stepped corner inlay",
    "stepped-corner-inlay",
    "Eight alternating L-shaped courses grow around a square core, covering each block without overlap.",
  ],
  [
    "Ruled saddle lattice",
    "ruled-saddle-lattice",
    "Four opposed fans of straight lines form curved envelopes around open centers.",
  ],
].map(([name, id, description]) => ({
  name,
  id,
  description: "Set 02 — " + description,
  set: 2,
}));

// Set 03 adds eight constructions with independent geometric rules.
export const geometricSetThree = [
  [
    "Elongated triangle lattice",
    "elongated-triangle-lattice",
    "Alternating square courses and exact equilateral-triangle bands tile the plane.",
  ],
  [
    "Descartes circle packing",
    "descartes-circle-packing",
    "A triangular lattice of tangent circles with three recursive Descartes insertion levels.",
  ],
  [
    "Pythagorean branch lattice",
    "pythagorean-branch-lattice",
    "Right-isosceles joints spawn paired squares whose areas obey the Pythagorean relation.",
  ],
  [
    "Fibonacci square spiral",
    "fibonacci-square-spiral",
    "Six consecutive Fibonacci squares exactly pack a 13-by-8 rectangle; four copies form the tile.",
  ],
  [
    "Rhodonea rose lattice",
    "rhodonea-rose-lattice",
    "Eight-petal harmonic roses repeat on a square lattice with concentric construction rings.",
  ],
  [
    "Epicycloid gear lattice",
    "epicycloid-gear-lattice",
    "Five-cusp rolling-circle curves nest within a periodic field of analytic gear profiles.",
  ],
  [
    "Decagram chord lattice",
    "decagram-chord-lattice",
    "Ten-vertex star polygons, decagon chords and a measured inner ring form each orbit cell.",
  ],
  [
    "Harmonic wave lattice",
    "harmonic-wave-lattice",
    "Orthogonal sinusoidal families meet with periodic phase and continuous repeat edges.",
  ],
].map(([name, id, description]) => ({
  name,
  id,
  description: "Set 03 — " + description,
  set: 3,
}));

// Set 04 adds eight distinct constructions: cellular, hyperbolic, fractal,
// polar, algebraic, and space-curve geometry.
export const geometricSetFour = [
  [
    "Dual-hexagon overlay",
    "dual-hexagon-overlay",
    "Quarter-turned, phase-offset flattened hexagon nets make a continuous crossing lattice.",
  ],
  [
    "Poincaré geodesic lattice",
    "poincare-geodesic-lattice",
    "Orthogonal-circle geodesics cross four disk cells; every arc stays inside its ideal boundary.",
  ],
  [
    "Periodic Voronoi mosaic",
    "periodic-voronoi-mosaic",
    "Sixteen fixed, displaced sites generate a clipped periodic Voronoi tiling with exact cell coverage.",
  ],
  [
    "Koch snowflake field",
    "koch-snowflake-field",
    "Four level-three Koch islands retain equal segment lengths and nested triangular construction lines.",
  ],
  [
    "Archimedean counterspirals",
    "archimedean-counterspirals",
    "Opposed r = a + bθ arms wind through paired orbit cells with concentric calibration rings.",
  ],
  [
    "Superellipse contour field",
    "superellipse-contour-field",
    "Nested Lamé curves transition between rounded-square and diamond axes in four repeat cells.",
  ],
  [
    "Bernoulli lemniscate field",
    "bernoulli-lemniscate-field",
    "Analytic figure-eight curves meet at their algebraic double points, with nested orbit guides.",
  ],
  [
    "Torus-knot projection",
    "torus-knot-projection",
    "A closed (2,3) torus-knot projection separates positive and negative depth branches at crossings.",
  ],
].map(([name, id, description]) => ({
  name,
  id,
  description: "Set 04 — " + description,
  set: 4,
}));

// Small, explicit geometric constructions. No rug borders or cultural labels.
export const geometricCatalog = [
  {
    name: "Truchet circuits",
    id: "truchet-circuits",
    description:
      "Quarter-circle pairs meet at cell-edge midpoints, forming connected circuits.",
  },
  {
    name: "Octagon-square tiling",
    id: "octagon-square-tiling",
    description:
      "Regular octagons and square infills: the 4.8.8 vertex arrangement.",
  },
  {
    name: "Hexagon-triangle tiling",
    id: "hexagon-triangle-tiling",
    description:
      "Corner-touching regular hexagons and equilateral triangular gaps: 3.6.3.6.",
  },
  {
    name: "Dodecagon-triangle tiling",
    id: "dodecagon-triangle-tiling",
    description:
      "Edge-sharing regular dodecagons with triangular infills: 3.12.12.",
  },
  {
    name: "Herringbone parquet",
    id: "herringbone-parquet",
    description:
      "A complete plane tiling of 2:1 rectangles, meeting end-to-side in staggered courses.",
  },
  {
    name: "Hilbert meander",
    id: "hilbert-meander",
    description:
      "A third-order continuous square-grid curve with nested turns and no self-crossings.",
  },
  ...geometricSetTwo,
  ...geometricSetThree,
  ...geometricSetFour,
].map((p) => ({ ...p, group: "Geometric constructions" }));
export function geometricPattern(name) {
  const spec = geometricCatalog.find((p) => p.name === name || p.id === name);
  if (!spec) return null;
  if (spec.set === 2) return geometricSetTwoPattern(spec);
  if (spec.set === 3) return geometricSetThreePattern(spec);
  if (spec.set === 4) return geometricSetFourPattern(spec);
  const d = {
    schema: "alloy.pattern.v1",
    name: spec.name,
    construction: spec.id,
    collection: spec.group,
    background: "#d6b879",
    repeats: 1,
    repeat: "straight",
    mapping: "uv",
    layers: [],
  };
  const n = (v) => Number((v / 5.12).toFixed(4));
  const poly = (p) =>
    p.map(([x, y], i) => (i ? "L" : "M") + n(x) + " " + n(y)).join("") + "Z";
  const add = (path, color, label, strokeWidth = 0) =>
    d.layers.push({
      kind: "path",
      name: label,
      path,
      color,
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      finish: "cotton",
      relief: 0.03,
      strokeWidth: strokeWidth / 5.12,
    });
  if (spec.id === "truchet-circuits") {
    d.background = "#233b44";
    let path = "";
    const orientations = [
      0, 0, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1,
      1, 0, 1, 0, 0, 1, 0,
    ];
    for (let y = 0; y < 8; y++)
      for (let x = 0; x < 8; x++) {
        const X = x * 64,
          Y = y * 64;
        if (orientations[(y * 8 + x) % orientations.length])
          path += `M${n(X + 32)} ${n(Y)}a${n(32)} ${n(32)} 0 0 0 ${n(32)} ${n(32)}M${n(X)} ${n(Y + 32)}a${n(32)} ${n(32)} 0 0 1 ${n(32)} ${n(32)}`;
        else
          path += `M${n(X)} ${n(Y + 32)}a${n(32)} ${n(32)} 0 0 0 ${n(32)} ${n(-32)}M${n(X + 32)} ${n(Y + 64)}a${n(32)} ${n(32)} 0 0 1 ${n(32)} ${n(-32)}`;
      }
    add(path, "#d6b879", "Circuit ribbons", 12);
    add(path, "#eddfc3", "Ribbon centerline", 3);
  } else if (spec.id === "octagon-square-tiling") {
    const s = 128,
      t = s / (2 + Math.SQRT2),
      paths = ["", ""];
    let outline = "";
    for (let y = 0; y < 4; y++)
      for (let x = 0; x < 4; x++) {
        const points = [
          [t, 0],
          [s - t, 0],
          [s, t],
          [s, s - t],
          [s - t, s],
          [t, s],
          [0, s - t],
          [0, t],
        ].map(([a, b]) => [a + x * s, b + y * s]);
        paths[(x + y) % 2] += poly(points);
        outline += poly(
          points.map(([a, b]) => [
            (x + 0.5) * s + (a - (x + 0.5) * s) * 0.91,
            (y + 0.5) * s + (b - (y + 0.5) * s) * 0.91,
          ]),
        );
      }
    add(paths[0], "#244957", "Octagons A");
    add(paths[1], "#8d483d", "Octagons B");
    add(outline, "#dec89e", "Inset octagon line", 1.5);
  } else if (
    ["hexagon-triangle-tiling", "dodecagon-triangle-tiling"].includes(spec.id)
  ) {
    // A rectangular repeat of a triangular lattice. The physical aspect ratio
    // restores regular polygons, rather than stretching them to fit a square.
    d.designAspect = 2 / Math.sqrt(3);
    d.background = "#d7bc87";
    const twelve = spec.id.startsWith("dodecagon"),
      sides = twelve ? 12 : 6,
      r = twelve ? 64 / Math.cos(Math.PI / 12) : 64,
      paths = ["", ""];
    let outline = "";
    for (let row = -1; row <= 4; row++)
      for (let col = -1; col <= 4; col++) {
        const cx = col * 128 + (((row % 2) + 2) % 2) * 64,
          cy = row * 128;
        const points = Array.from({ length: sides }, (_, i) => {
          const a = (i * Math.PI * 2) / sides + (twelve ? Math.PI / 12 : 0);
          return [
            cx + r * Math.cos(a),
            cy + (r * Math.sin(a) * 2) / Math.sqrt(3),
          ];
        });
        paths[(((col + row) % 2) + 2) % 2] += poly(points);
        outline += poly(
          points.map(([x, y]) => [cx + (x - cx) * 0.88, cy + (y - cy) * 0.88]),
        );
      }
    add(paths[0], "#253e4a", "Polygon field A");
    add(paths[1], "#457e82", "Polygon field B");
    add(outline, "#dcc89e", "Inset polygon lines", 1.4);
  } else if (spec.id === "herringbone-parquet") {
    d.background = "#1d3039";
    const paths = ["", ""];
    for (let y = -2; y < 8; y++)
      for (let x = -2; x < 8; x++) {
        const m = (((x + y) % 4) + 4) % 4;
        if (m !== 0 && m !== 2) continue;
        const w = m === 0 ? 128 : 64,
          h = m === 0 ? 64 : 128,
          a = x * 64 + 3,
          b = y * 64 + 3;
        paths[m === 0 ? 0 : 1] += poly([
          [a, b],
          [a + w - 6, b],
          [a + w - 6, b + h - 6],
          [a, b + h - 6],
        ]);
      }
    add(paths[0], "#b9664d", "Horizontal blocks");
    add(paths[1], "#d6bc83", "Vertical blocks");
  } else {
    d.background = "#263e4a";
    const xy = (d) => {
      let x = 0,
        y = 0,
        t = d;
      for (let s = 1; s < 8; s *= 2) {
        const rx = 1 & (t >> 1),
          ry = 1 & (t ^ rx);
        if (!ry) {
          if (rx) {
            x = s - 1 - x;
            y = s - 1 - y;
          }
          [x, y] = [y, x];
        }
        x += s * rx;
        y += s * ry;
        t >>= 2;
      }
      return [x, y];
    };
    const path = Array.from({ length: 64 }, (_, i) => {
      const [x, y] = xy(i);
      return (i ? "L" : "M") + n(32 + x * 64) + " " + n(32 + y * 64);
    }).join("");
    add(path, "#d0aa67", "Continuous meander", 24);
    add(path, "#e6d6b5", "Meander inset", 6);
  }
  return d;
}

// Coordinates in a four-unit square; areas are 4,4,2,1,2,1,2.
export const tangramDissection = [
  [
    [0, 0],
    [4, 0],
    [2, 2],
  ],
  [
    [0, 0],
    [2, 2],
    [0, 4],
  ],
  [
    [4, 4],
    [2, 4],
    [4, 2],
  ],
  [
    [4, 0],
    [4, 2],
    [3, 1],
  ],
  [
    [2, 2],
    [3, 1],
    [4, 2],
    [3, 3],
  ],
  [
    [2, 2],
    [3, 3],
    [1, 3],
  ],
  [
    [0, 4],
    [1, 3],
    [3, 3],
    [2, 4],
  ],
];

function geometricSetTwoPattern(spec) {
  const d = {
    schema: "alloy.pattern.v1",
    name: spec.name,
    construction: spec.id,
    collection: "Geometric constructions",
    background: "#233b44",
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: [],
  };
  const n = (v) => Number((v / 5.12).toFixed(4));
  const poly = (pts) =>
    pts.map(([x, y], i) => (i ? "L" : "M") + n(x) + " " + n(y)).join("") + "Z";
  const line = (pts) =>
    pts.map(([x, y], i) => (i ? "L" : "M") + n(x) + " " + n(y)).join("");
  const square = (x, y, w) => [
    [x, y],
    [x + w, y],
    [x + w, y + w],
    [x, y + w],
  ];
  const inset = (pts, t) => {
    const cx = pts.reduce((s, p) => s + p[0], 0) / pts.length,
      cy = pts.reduce((s, p) => s + p[1], 0) / pts.length;
    return pts.map(([x, y]) => [cx + (x - cx) * t, cy + (y - cy) * t]);
  };
  const add = (path, color, name, width = 0) => {
    if (path)
      d.layers.push({
        kind: "path",
        path,
        color,
        name,
        x: 256,
        y: 256,
        width: 512,
        height: 512,
        finish: "cotton",
        roughness: 0.83,
        metalness: 0,
        relief: 0.03,
        strokeWidth: width / 5.12,
      });
  };
  const cream = "#eddfc3",
    gold = "#d6b879",
    teal = "#457e82",
    coral = "#b9664d",
    dark = "#233b44";
  if (spec.id === "linked-racetrack-loops") {
    let horizontal = "",
      vertical = "",
      bridges = "",
      bridgeCuts = "",
      bridgeInlays = "";
    const a = 65,
      b = 30;
    for (let row = 0; row < 2; row++)
      for (let col = 0; col < 2; col++) {
        const x = 128 + col * 256,
          y = 128 + row * 256;
        horizontal += `M${n(x - a)} ${n(y - b)}H${n(x + a)}a${n(b)} ${n(b)} 0 0 1 0 ${n(2 * b)}H${n(x - a)}a${n(b)} ${n(b)} 0 0 1 0 ${n(-2 * b)}Z`;
        vertical += `M${n(x - b)} ${n(y - a)}a${n(b)} ${n(b)} 0 0 1 ${n(2 * b)} 0V${n(y + a)}a${n(b)} ${n(b)} 0 0 1 ${n(-2 * b)} 0Z`;
        for (const s of [-1, 1]) {
          bridgeInlays += line([
            [x + s * b - 27, y + s * b],
            [x + s * b + 27, y + s * b],
          ]);
          bridgeCuts += poly([
            [x + s * b - 15, y + s * b - 13.5],
            [x + s * b + 15, y + s * b - 13.5],
            [x + s * b + 15, y + s * b + 13.5],
            [x + s * b - 15, y + s * b + 13.5],
          ]);
          bridges += line([
            [x + s * b - 15, y + s * b],
            [x + s * b + 15, y + s * b],
          ]);
        }
      }
    // Dark sleeves separate the crossing, followed by the upper colored path.
    // B is on top at two crossings; the explicit A bridges reverse the other two.
    add(horizontal, gold, "Horizontal closed loops", 19);
    add(horizontal, cream, "Horizontal inlaid centerline", 3);
    add(vertical, dark, "Vertical crossing clearance", 27);
    add(vertical, teal, "Vertical closed loops", 19);
    add(vertical, cream, "Vertical inlaid centerline", 3);
    add(bridgeCuts, dark, "Alternating bridge clearance");
    add(bridges, gold, "Horizontal upper bridges", 19);
    add(bridgeInlays, cream, "Bridge centerline", 3);
  } else if (spec.id === "hexagon-square-junctions") {
    d.designAspect = 2 / Math.sqrt(3);
    d.background = cream;
    const D = 128,
      s = D / (1 + Math.sqrt(3)),
      q = 2 / Math.sqrt(3),
      hex = ["", ""],
      squares = [];
    let outlines = "";
    for (let row = -2; row <= 5; row++)
      for (let col = -2; col <= 5; col++) {
        const cx = col * D + ((((row % 2) + 2) % 2) * D) / 2,
          cy = (row * Math.sqrt(3) * D) / 2;
        const p = Array.from({ length: 6 }, (_, i) => {
          const a = Math.PI / 6 + (i * Math.PI) / 3;
          return [cx + s * Math.cos(a), (cy + s * Math.sin(a)) * q];
        });
        hex[(((row + col) % 2) + 2) % 2] += poly(p);
        outlines += poly(inset(p, 0.84));
        for (let k = 0; k < 3; k++) {
          const a = (k * Math.PI) / 3,
            nx = Math.cos(a),
            ny = Math.sin(a),
            mx = cx + (nx * D) / 2,
            my = cy + (ny * D) / 2;
          const p = [
            [-1, -1],
            [1, -1],
            [1, 1],
            [-1, 1],
          ].map(([u, v]) => [
            mx + (s / 2) * (u * nx - v * ny),
            (my + (s / 2) * (u * ny + v * nx)) * q,
          ]);
          squares.push(poly(p));
          outlines += poly(inset(p, 0.77));
        }
      }
    add(hex[0], dark, "Hexagonal cells A");
    add(hex[1], teal, "Hexagonal cells B");
    add(squares.join(""), coral, "Square connectors");
    add(outlines, gold, "Polygon inlay lines", 1.3);
  } else if (spec.id === "pinwheel-square-tessellation") {
    d.background = gold;
    const paths = ["", ""];
    let small = "",
      borders = "";
    const u = 51.2;
    for (let y = -3; y <= 10; y++)
      for (let x = -3; x <= 10; x++) {
        if ((((3 * x + y) % 10) + 10) % 10 !== 0) continue;
        const parity = ((3 * x + y) / 10 + (-x + 3 * y) / 10) % 2;
        const big = square(x * u, y * u, 3 * u),
          little = square((x + 2) * u, (y + 3) * u, u);
        paths[(parity + 2) % 2] += poly(inset(big, 0.967));
        small += poly(inset(little, 0.9));
        borders += poly(inset(big, 0.84));
      }
    add(paths[0], dark, "Large squares A");
    add(paths[1], teal, "Large squares B");
    add(small, coral, "Small square infills");
    add(borders, cream, "Large-square inset lines", 1.3);
  } else if (spec.id === "tangram-mosaic") {
    d.background = dark;
    const paths = Array(7).fill("");
    let outlines = "";
    for (let row = 0; row < 2; row++)
      for (let col = 0; col < 2; col++)
        for (let k = 0; k < 7; k++) {
          const p = tangramDissection[k].map(([x, y]) => {
            if ((row + col) % 2) [x, y] = [4 - x, 4 - y];
            return [col * 256 + x * 64, row * 256 + y * 64];
          });
          paths[k] += poly(inset(p, 0.972));
          outlines += poly(inset(p, 0.85));
        }
    paths.forEach((p, i) =>
      add(
        p,
        [teal, coral, gold, cream, dark, gold, teal][i],
        "Tangram piece " + (i + 1),
      ),
    );
    add(outlines, cream, "Piece inset lines", 1.15);
  } else if (spec.id === "sierpinski-lace") {
    d.designAspect = 2 / Math.sqrt(3);
    const paths = ["", ""];
    let outlines = "";
    const midpoint = (a, b) => [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
    const gasket = (p, depth, k) => {
      if (!depth) {
        paths[k] += poly(p);
        return;
      }
      const [a, b, c] = p,
        ab = midpoint(a, b),
        bc = midpoint(b, c),
        ca = midpoint(c, a);
      gasket([a, ab, ca], depth - 1, k);
      gasket([ab, b, bc], depth - 1, k);
      gasket([ca, bc, c], depth - 1, k);
    };
    for (let row = 0; row < 2; row++)
      for (let col = -1; col <= 2; col++) {
        const x = col * 256 + (row % 2) * 128,
          y = row * 256;
        const a = [
            [x, y],
            [x + 256, y],
            [x + 128, y + 256],
          ],
          b = [
            [x + 128, y + 256],
            [x + 256, y],
            [x + 384, y + 256],
          ];
        gasket(a, 3, 0);
        gasket(b, 3, 1);
        outlines += poly(a) + poly(b);
      }
    add(paths[0], cream, "Recursive triangles A");
    add(paths[1], teal, "Recursive triangles B");
    add(outlines, gold, "Triangular field edges", 1.25);
  } else if (spec.id === "vesica-net") {
    const s = 128,
      r = s / Math.sqrt(2);
    let h = "",
      v = "",
      circles = "";
    for (let row = -1; row <= 4; row++)
      for (let col = -1; col <= 4; col++) {
        const x = col * s,
          y = row * s;
        h += `M${n(x + 64)} ${n(y - 64)}a${n(r)} ${n(r)} 0 0 1 0 ${n(128)}a${n(r)} ${n(r)} 0 0 1 0 ${n(-128)}Z`;
        v += `M${n(x + 64)} ${n(y + 64)}a${n(r)} ${n(r)} 0 0 1 ${n(-128)} 0a${n(r)} ${n(r)} 0 0 1 ${n(128)} 0Z`;
        circles += `M${n(x - r)} ${n(y)}a${n(r)} ${n(r)} 0 1 0 ${n(2 * r)} 0a${n(r)} ${n(r)} 0 1 0 ${n(-2 * r)} 0Z`;
      }
    add(h, teal, "Horizontal lens regions");
    add(v, coral, "Vertical lens regions");
    add(circles, gold, "Shared circular boundaries", 2);
  } else if (spec.id === "stepped-corner-inlay") {
    const paths = Array(9).fill("");
    let seams = "";
    for (let row = 0; row < 2; row++)
      for (let col = 0; col < 2; col++) {
        let x0 = 112,
          y0 = 112,
          x1 = 144,
          y1 = 144;
        const place = (p) =>
          p.map(([x, y]) => {
            if ((row + col) % 2) [x, y] = [256 - x, 256 - y];
            return [x + col * 256, y + row * 256];
          });
        const core = place(square(x0, y0, 32));
        paths[0] += poly(core);
        seams += poly(core);
        for (let k = 0; k < 8; k++) {
          const t = 28;
          let p;
          if (k % 2 === 0) {
            p = [
              [x0, y0 - t],
              [x1 + t, y0 - t],
              [x1 + t, y1],
              [x1, y1],
              [x1, y0],
              [x0, y0],
            ];
            y0 -= t;
            x1 += t;
          } else {
            p = [
              [x0 - t, y0],
              [x0, y0],
              [x0, y1],
              [x1, y1],
              [x1, y1 + t],
              [x0 - t, y1 + t],
            ];
            x0 -= t;
            y1 += t;
          }
          p = place(p);
          paths[k + 1] += poly(p);
          seams += poly(p);
        }
      }
    paths.forEach((p, i) =>
      add(
        p,
        [gold, teal, coral, cream, dark, gold, teal, coral, cream][i],
        i ? "Elbow course " + i : "Square cores",
      ),
    );
    add(seams, dark, "Course joints", 2.5);
  } else if (spec.id === "ruled-saddle-lattice") {
    const paths = ["", ""];
    for (let row = 0; row < 2; row++)
      for (let col = 0; col < 2; col++) {
        const cx = 128 + col * 256,
          cy = 128 + row * 256;
        for (const sx of [-1, 1])
          for (const sy of [-1, 1])
            for (let i = 0; i <= 24; i++) {
              const t = i / 24;
              paths[sx === sy ? 0 : 1] += line([
                [cx + sx * t * 128, cy],
                [cx, cy + sy * (1 - t) * 128],
              ]);
            }
      }
    add(paths[0], gold, "Opposed gold line fans", 1.35);
    add(paths[1], cream, "Opposed ivory line fans", 1.35);
  }
  return d;
}

// The positive Descartes root inserts the circle inside a bounded gap between
// three pairwise externally tangent circles. Coordinates share their units.
export function descartesGapCircle(triple) {
  if (
    !Array.isArray(triple) ||
    triple.length !== 3 ||
    triple.some(
      (c) =>
        !Number.isFinite(c?.x) ||
        !Number.isFinite(c?.y) ||
        !Number.isFinite(c?.r) ||
        c.r <= 0,
    )
  )
    throw new Error("A Descartes gap needs three finite positive circles.");
  const [a, b, c] = triple,
    ka = 1 / a.r,
    kb = 1 / b.r,
    kc = 1 / c.r,
    k = ka + kb + kc + 2 * Math.sqrt(ka * kb + kb * kc + kc * ka),
    r = 1 / k;
  const dx1 = b.x - a.x,
    dy1 = b.y - a.y,
    dx2 = c.x - a.x,
    dy2 = c.y - a.y,
    rhs1 =
      b.x * b.x +
      b.y * b.y -
      a.x * a.x -
      a.y * a.y -
      (b.r * b.r - a.r * a.r) -
      2 * r * (b.r - a.r),
    rhs2 =
      c.x * c.x +
      c.y * c.y -
      a.x * a.x -
      a.y * a.y -
      (c.r * c.r - a.r * a.r) -
      2 * r * (c.r - a.r),
    det = 4 * (dx1 * dy2 - dy1 * dx2);
  if (Math.abs(det) < 1e-12)
    throw new Error("The three Descartes circles must not be collinear.");
  const x = (2 * dy2 * rhs1 - 2 * dy1 * rhs2) / det,
    y = (2 * dx1 * rhs2 - 2 * dx2 * rhs1) / det;
  return { x, y, r };
}

function geometricSetThreePattern(spec) {
  const d = {
    schema: "alloy.pattern.v1",
    name: spec.name,
    construction: spec.id,
    collection: "Geometric constructions",
    background: "#233b44",
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: [],
  };
  const n = (v) => Number((v / 5.12).toFixed(4));
  const xy = (x, y) => [n(x), n(y)];
  const poly = (pts, project = xy) =>
    pts
      .map(([x, y], i) => {
        const [X, Y] = project(x, y);
        return (i ? "L" : "M") + X + " " + Y;
      })
      .join("") + "Z";
  const line = (pts, project = xy) =>
    pts
      .map(([x, y], i) => {
        const [X, Y] = project(x, y);
        return (i ? "L" : "M") + X + " " + Y;
      })
      .join("");
  const add = (path, color, name, width = 0, fillRule) => {
    if (path)
      d.layers.push({
        kind: "path",
        path,
        color,
        name,
        x: 256,
        y: 256,
        width: 512,
        height: 512,
        finish: "cotton",
        roughness: 0.83,
        metalness: 0,
        relief: 0.03,
        strokeWidth: width / 5.12,
        ...(fillRule ? { fillRule } : {}),
      });
  };
  const dark = "#233b44",
    cream = "#eddfc3",
    gold = "#d6b879",
    teal = "#457e82",
    coral = "#b9664d";
  if (spec.id === "elongated-triangle-lattice") {
    const a = 112,
      h = (a * Math.sqrt(3)) / 2,
      W = 2 * a,
      H = 2 * (a + h),
      project = (x, y) => [n((x * 512) / W), n((y * 512) / H)];
    d.designAspect = W / H;
    d.background = dark;
    const blocks = ["", ""];
    let up = "",
      down = "",
      joints = "";
    const insetPath = (p, t = 0.91) => {
      const cx = p.reduce((s, v) => s + v[0], 0) / p.length,
        cy = p.reduce((s, v) => s + v[1], 0) / p.length;
      return poly(p.map(([x, y]) => [cx + (x - cx) * t, cy + (y - cy) * t]));
    };
    const inTile = (cx, cy) => cx >= 0 && cx < W && cy >= 0 && cy < H;
    for (let row = 0; row < 2; row++) {
      const phase = row * (a / 2),
        y = row * (a + h);
      for (let col = -2; col <= 3; col++) {
        const x = phase + col * a,
          square = [
            [x, y],
            [x + a, y],
            [x + a, y + a],
            [x, y + a],
          ],
          lo = y + a,
          hi = lo + h,
          upTri = [
            [x, lo],
            [x + a, lo],
            [x + a / 2, hi],
          ],
          downTri = [
            [x + a / 2, hi],
            [x + 1.5 * a, hi],
            [x + a, lo],
          ];
        if (inTile(x + a / 2, y + a / 2)) {
          blocks[((row + col) % 2 + 2) % 2] += poly(square, project);
          joints += insetPath(square, 0.91);
        }
        if (inTile(x + a / 2, lo + h / 3)) {
          up += poly(upTri, project);
          joints += insetPath(upTri, 0.91);
        }
        if (inTile(x + a, lo + (2 * h) / 3)) {
          down += poly(downTri, project);
          joints += insetPath(downTri, 0.91);
        }
      }
    }
    add(blocks[0], teal, "Square course A");
    add(blocks[1], coral, "Square course B");
    add(up, gold, "Upward equilateral triangles");
    add(down, cream, "Downward equilateral triangles");
    add(joints, dark, "Individual tile joints", 1.35);
  } else if (spec.id === "descartes-circle-packing") {
    const R = 1,
      W = 2 * R,
      H = 2 * Math.sqrt(3) * R,
      project = (x, y) => [n((x * 512) / W), n((y * 512) / H)];
    d.designAspect = W / H;
    d.background = "#f0e7d4";
    const layers = ["", "", "", ""],
      inTile = (x, y) => x >= 0 && x < W && y >= 0 && y < H;
    const circlePath = ({ x, y, r }) => {
      const [X, Y] = project(x, y),
        rx = n((r * 512) / W),
        ry = n((r * 512) / H);
      return `M${(X + rx).toFixed(4)} ${Y}a${rx} ${ry} 0 1 0 ${(-2 * rx).toFixed(4)} 0a${rx} ${ry} 0 1 0 ${(2 * rx).toFixed(4)} 0Z`;
    };
    const fill = (triple, depth) => {
      const circle = descartesGapCircle(triple);
      layers[depth] += circlePath(circle);
      if (depth < 3)
        for (let i = 0; i < 3; i++)
          fill([triple[i], triple[(i + 1) % 3], circle], depth + 1);
    };
    const center = (i, j) => ({
      x: 2 * i + j,
      y: Math.sqrt(3) * j,
      r: R,
    });
    // A 2-by-2√3 rectangle is a fundamental domain of the triangular circle
    // lattice; include each circle/gap once and let SVG repetition close it.
    for (let row = -1; row <= 2; row++)
      for (let col = -2; col <= 2; col++) {
        const c = center(col, row);
        if (inTile(c.x, c.y)) layers[0] += circlePath(c);
      }
    for (let row = -2; row <= 3; row++)
      for (let col = -3; col <= 3; col++) {
        const a = center(col, row),
          b = center(col + 1, row),
          c = center(col, row + 1),
          e = center(col + 1, row + 1);
        for (const triple of [
          [a, b, c],
          [b, e, c],
        ]) {
          const x = triple.reduce((s, p) => s + p.x, 0) / 3,
            y = triple.reduce((s, p) => s + p.y, 0) / 3;
          if (inTile(x, y)) fill(triple, 1);
        }
      }
    add(layers[0], "#31545c", "Triangular lattice circle faces");
    add(layers[0], dark, "Tangent circle boundaries", 1.05);
    add(layers[1], teal, "First Descartes fills");
    add(layers[1], dark, "First circle inlays", 0.85);
    add(layers[2], gold, "Second Descartes fills");
    add(layers[2], dark, "Second circle inlays", 0.7);
    add(layers[3], coral, "Third Descartes fills");
    add(layers[3], dark, "Third circle inlays", 0.55);
  } else if (spec.id === "pythagorean-branch-lattice") {
    d.background = dark;
    const squares = Array.from({ length: 5 }, () => ""),
      triangles = Array.from({ length: 4 }, () => "");
    let joints = "";
    const length = (v) => Math.hypot(v[0], v[1]),
      scale = (v, t) => [v[0] * t, v[1] * t],
      addv = (a, b) => [a[0] + b[0], a[1] + b[1]],
      sub = (a, b) => [a[0] - b[0], a[1] - b[1]],
      away = (a, b, reference) => {
        const v = sub(b, a),
          l = length(v),
          m = [(a[0] + b[0]) / 2, (a[1] + b[1]) / 2];
        let normal = [-v[1] / l, v[0] / l];
        if (
          normal[0] * (reference[0] - m[0]) +
            normal[1] * (reference[1] - m[1]) >
          0
        )
          normal = scale(normal, -1);
        return normal;
      };
    const grow = (a, b, interior, depth, root = false) => {
      const v = sub(b, a),
        side = length(v),
        squareA = addv(a, scale(interior, side)),
        squareB = addv(b, scale(interior, side)),
        square = [a, b, squareB, squareA];
      squares[depth] += poly(square);
      const cx = square.reduce((s, p) => s + p[0], 0) / 4,
        cy = square.reduce((s, p) => s + p[1], 0) / 4;
      joints += poly(
        square.map(([x, y]) => [cx + (x - cx) * 0.94, cy + (y - cy) * 0.94]),
      );
      if (!depth) return;
      // The root's triangle attaches directly to the square's upper edge.
      // Descendant squares attach on a triangle leg, so their next triangle
      // grows from the far edge, continuing away from the preceding joint.
      const triA = root ? a : squareA,
        triB = root ? b : squareB,
        out = scale(interior, root ? -1 : 1),
        unit = scale(sub(triB, triA), 1 / side),
        apex = addv(triA, addv(scale(unit, side / 2), scale(out, side / 2))),
        tri = [triA, triB, apex],
        centroid = [
          tri.reduce((s, p) => s + p[0], 0) / 3,
          tri.reduce((s, p) => s + p[1], 0) / 3,
        ];
      triangles[depth - 1] += poly(tri);
      grow(triA, apex, away(triA, apex, centroid), depth - 1);
      grow(apex, triB, away(apex, triB, centroid), depth - 1);
    };
    grow([214, 402], [298, 402], [0, 1], 4, true);
    squares.forEach((path, i) =>
      add(path, [gold, teal, coral, cream, gold][i], `Branch squares · level ${i + 1}`),
    );
    triangles.forEach((path, i) =>
      add(path, [coral, gold, teal, cream][i], `Right-angle joints · level ${i + 1}`),
    );
    add(joints, dark, "Square inset joints", 1.2);
  } else if (spec.id === "fibonacci-square-spiral") {
    d.background = "#1e343e";
    const squares = ["", "", "", ""];
    let joints = "",
      progressions = "";
    // 📝 Fibonacci square spiral tool: the six squares come from the recurrence/rotating-attachment
    // rule, not a transcribed list of positions.
    const tiles = fibonacciSquareSpiral(6);
    const scale = 18.5;
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const project = (x, y) => [
          n(cx - (13 * scale) / 2 + (x + 3) * scale),
          n(cy - (8 * scale) / 2 + (y + 2) * scale),
        ];
        const centers = [];
        tiles.forEach(([x, y, side], i) => {
          const p = [
            [x, y],
            [x + side, y],
            [x + side, y + side],
            [x, y + side],
          ];
          squares[i % 4] += poly(p, project);
          const mx = x + side / 2,
            my = y + side / 2;
          centers.push([mx, my]);
          joints += poly(
            p.map(([px, py]) => [mx + (px - mx) * 0.91, my + (py - my) * 0.91]),
            project,
          );
        });
        progressions += line(centers, project);
      }
    squares.forEach((path, i) =>
      add(path, [teal, coral, gold, "#39545d"][i], `Fibonacci square class ${i + 1}`),
    );
    add(joints, cream, "Twenty-four inset square outlines", 1.2);
    add(progressions, gold, "Square-center construction line", 1.35);
  } else if (spec.id === "rhodonea-rose-lattice") {
    d.background = "#243c45";
    let outer = "",
      inner = "",
      rays = "",
      rings = "";
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const rose = (radius, phase = 0) => {
          const pts = Array.from({ length: 513 }, (_, i) => {
            const t = (i * Math.PI * 2) / 512,
              r = radius * Math.cos(4 * t + phase);
            return [cx + r * Math.cos(t), cy + r * Math.sin(t)];
          });
          return line(pts) + "Z";
        };
        outer += rose(70);
        inner += rose(44, Math.PI / 8);
        for (let k = 0; k < 8; k++) {
          const t = (k * Math.PI) / 4,
            r = 70 * Math.cos(4 * t);
          rays += line([
            [cx, cy],
            [cx + r * Math.cos(t), cy + r * Math.sin(t)],
          ]);
        }
        rings += `M${n(cx + 13)} ${n(cy)}a${n(13)} ${n(13)} 0 1 0 ${n(-26)} 0a${n(13)} ${n(13)} 0 1 0 ${n(26)} 0Z`;
      }
    add(outer, teal, "Eight-lobed rhodonea curves", 2.25);
    add(inner, gold, "Offset inner harmonic", 1.5);
    add(rays, cream, "Petal construction radii", 1.05);
    add(rings, coral, "Central orbit rings", 1.2);
  } else if (spec.id === "epicycloid-gear-lattice") {
    d.background = "#203741";
    let faces = "",
      teeth = "",
      hubs = "",
      spokes = "";
    const q = 5,
      count = q * 160;
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const gear = (radius) => {
          const pts = Array.from({ length: count + 1 }, (_, i) => {
            const t = (i * Math.PI * 2) / count,
              s = radius / q,
              x = s * ((q + 1) * Math.cos(t) - Math.cos((q + 1) * t)),
              y = s * ((q + 1) * Math.sin(t) - Math.sin((q + 1) * t));
            return [cx + x, cy + y];
          });
          return line(pts) + "Z";
        };
        faces += gear(71);
        teeth += gear(71);
        hubs += `M${n(cx + 15)} ${n(cy)}a${n(15)} ${n(15)} 0 1 0 ${n(-30)} 0a${n(15)} ${n(15)} 0 1 0 ${n(30)} 0Z`;
        for (let k = 0; k < q; k++) {
          const t = (2 * Math.PI * k) / q,
            radius = 70;
          spokes += line([
            [cx, cy],
            [cx + radius * Math.cos(t), cy + radius * Math.sin(t)],
          ]);
        }
      }
    add(faces, teal, "Five-cusp rolling-circle profiles");
    add(teeth, gold, "Profile edge", 1.6);
    add(spokes, cream, "Five radial tooth axes", 1.1);
    add(hubs, coral, "Gear hubs", 1.35);
  } else if (spec.id === "decagram-chord-lattice") {
    d.background = dark;
    let faces = "",
      decagons = "",
      star = "",
      pentagrams = "",
      rings = "";
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const v = Array.from({ length: 10 }, (_, i) => {
          const a = -Math.PI / 2 + (2 * Math.PI * i) / 10;
          return [cx + 70 * Math.cos(a), cy + 70 * Math.sin(a)];
        });
        faces += poly(v);
        decagons += poly(v);
        const cycle = [];
        let index = 0;
        do {
          cycle.push(v[index]);
          index = (index + 3) % 10;
        } while (index !== 0);
        star += line(cycle) + "Z";
        for (let start = 0; start < 2; start++) {
          const p = [];
          for (let j = 0; j < 5; j++) p.push(v[(start + j * 2) % 10]);
          pentagrams += line(p) + "Z";
        }
        rings += `M${n(cx + 21)} ${n(cy)}a${n(21)} ${n(21)} 0 1 0 ${n(-42)} 0a${n(21)} ${n(21)} 0 1 0 ${n(42)} 0Z`;
      }
    add(faces, "#36545b", "Decagon fields");
    add(decagons, gold, "Ten equal chord intervals", 1.25);
    add(star, cream, "Regular decagram chords", 1.7);
    add(pentagrams, coral, "Crossed pentagram chords", 1.15);
    add(rings, teal, "Inner tenfold rings", 1.25);
  } else if (spec.id === "harmonic-wave-lattice") {
    d.background = "#203741";
    let horizontal = "",
      vertical = "";
    const period = 128,
      amplitude = 18,
      step = 4;
    for (let row = -1; row <= 5; row++) {
      const phase = (row * Math.PI) / 2,
        pts = [];
      for (let x = 0; x <= 512; x += step)
        pts.push([
          x,
          row * period + amplitude * Math.sin((2 * Math.PI * x) / period + phase),
        ]);
      horizontal += line(pts);
    }
    for (let col = -1; col <= 5; col++) {
      const phase = (col * Math.PI) / 2,
        pts = [];
      for (let y = 0; y <= 512; y += step)
        pts.push([
          col * period + amplitude * Math.sin((2 * Math.PI * y) / period + phase),
          y,
        ]);
      vertical += line(pts);
    }
    add(horizontal, teal, "Horizontal harmonic rows", 1.35);
    add(vertical, gold, "Vertical harmonic columns", 1.35);
  }
  return d;
}


// A Poincaré-disk geodesic between two ideal boundary points. Except for
// diameters, the geodesic is the inner arc of a circle orthogonal to the disk.
export function poincareGeodesic(center, radius, startAngle, endAngle, samples = 32) {
  if (
    !center ||
    !Number.isFinite(center.x) ||
    !Number.isFinite(center.y) ||
    !Number.isFinite(radius) ||
    radius <= 0 ||
    !Number.isFinite(startAngle) ||
    !Number.isFinite(endAngle) ||
    !Number.isInteger(samples) ||
    samples < 2 ||
    samples > 1024
  )
    throw new Error("A Poincaré geodesic needs finite disk and sampling values.");
  const wrap = (a) => ((a % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI),
    delta = wrap(endAngle - startAngle + Math.PI) - Math.PI,
    point = (a) => [center.x + radius * Math.cos(a), center.y + radius * Math.sin(a)],
    start = point(startAngle),
    end = point(endAngle);
  if (Math.abs(delta) < 1e-10)
    throw new Error("A Poincaré geodesic needs distinct ideal endpoints.");
  if (Math.abs(Math.abs(delta) - Math.PI) < 1e-9)
    return Array.from({ length: samples + 1 }, (_, i) => {
      const t = i / samples;
      return [start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t];
    });
  const middle = startAngle + delta / 2,
    centerDistance = radius / Math.cos(delta / 2),
    circleRadius = radius * Math.abs(Math.tan(delta / 2)),
    cx = center.x + centerDistance * Math.cos(middle),
    cy = center.y + centerDistance * Math.sin(middle),
    from = Math.atan2(start[1] - cy, start[0] - cx),
    to = Math.atan2(end[1] - cy, end[0] - cx),
    ccw = wrap(to - from),
    candidates = [ccw, ccw - 2 * Math.PI];
  const insideSweep = candidates.find((sweep) => {
    const a = from + sweep / 2,
      x = cx + circleRadius * Math.cos(a),
      y = cy + circleRadius * Math.sin(a);
    return Math.hypot(x - center.x, y - center.y) < radius + 1e-8;
  });
  if (insideSweep === undefined)
    throw new Error("Could not resolve the inner Poincaré arc.");
  const points = Array.from({ length: samples + 1 }, (_, i) => {
    const a = from + (insideSweep * i) / samples;
    return [cx + circleRadius * Math.cos(a), cy + circleRadius * Math.sin(a)];
  });
  points[0] = start;
  points[samples] = end;
  return points;
}

function geometricSetFourPattern(spec) {
  const d = {
    schema: "alloy.pattern.v1",
    name: spec.name,
    construction: spec.id,
    collection: "Geometric constructions",
    background: "#203741",
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: [],
  };
  const n = (v) => Number((v / 5.12).toFixed(4));
  const xy = (x, y) => [n(x), n(y)];
  const poly = (points) =>
    points
      .map(([x, y], i) => {
        const [X, Y] = xy(x, y);
        return (i ? "L" : "M") + X + " " + Y;
      })
      .join("") + "Z";
  const line = (points) =>
    points
      .map(([x, y], i) => {
        const [X, Y] = xy(x, y);
        return (i ? "L" : "M") + X + " " + Y;
      })
      .join("");
  const add = (path, color, name, width = 0, opacity = 1) => {
    if (path)
      d.layers.push({
        kind: "path",
        path,
        color,
        name,
        x: 256,
        y: 256,
        width: 512,
        height: 512,
        finish: "cotton",
        roughness: 0.83,
        metalness: 0,
        relief: 0.035,
        strokeWidth: width / 5.12,
        opacity,
      });
  };
  const circle = (cx, cy, radius) =>
    `M${n(cx + radius)} ${n(cy)}a${n(radius)} ${n(radius)} 0 1 0 ${n(-2 * radius)} 0a${n(radius)} ${n(radius)} 0 1 0 ${n(2 * radius)} 0Z`;
  const inset = (points, cx, cy, scale) =>
    points.map(([x, y]) => [cx + (x - cx) * scale, cy + (y - cy) * scale]);
  const dark = "#203741",
    cream = "#eee2c7",
    gold = "#d5b36b",
    teal = "#4d9292",
    coral = "#bd7056";

  if (spec.id === "dual-hexagon-overlay") {
    d.background = "#1b3039";
    const radius = 128 / 3,
      root3 = Math.sqrt(3),
      grids = ["", ""],
      inlays = ["", ""],
      hubs = ["", ""],
      seenCenters = [new Set(), new Set()];
    const project = (x, y, grid) =>
      grid === 0 ? [x, root3 * y] : [-root3 * y + 64, x + 64];
    const wrap = (v) => {
      const q = ((v % 512) + 512) % 512;
      return q < 1e-7 || 512 - q < 1e-7 ? 0 : q;
    };
    for (let grid = 0; grid < 2; grid++)
      for (let i = -10; i <= 10; i++)
        for (let j = -10; j <= 10; j++) {
          const base = [
              1.5 * radius * (i + j),
              (root3 / 2) * radius * (i - j),
            ],
            rawCenter = project(base[0], base[1], grid),
            center = [wrap(rawCenter[0]), wrap(rawCenter[1])],
            key = center.map((v) => v.toFixed(6)).join(":");
          if (seenCenters[grid].has(key)) continue;
          seenCenters[grid].add(key);
          const shift = [center[0] - rawCenter[0], center[1] - rawCenter[1]],
            points = Array.from({ length: 6 }, (_, k) => {
              const a = (k * Math.PI) / 3,
                p = project(
                  base[0] + radius * Math.cos(a),
                  base[1] + radius * Math.sin(a),
                  grid,
                );
              return [p[0] + shift[0], p[1] + shift[1]];
            });
          grids[grid] += poly(points);
          inlays[grid] += poly(inset(points, center[0], center[1], 0.82));
          hubs[grid] += circle(center[0], center[1], 2.8);
        }
    add(grids[0], gold, "Flattened hexagon net A", 1.7);
    add(grids[1], teal, "Quarter-turned hexagon net B", 1.7);
    add(inlays[0], cream, "Inner edge tracery A", 0.65);
    add(inlays[1], coral, "Inner edge tracery B", 0.65);
    add(hubs[0] + hubs[1], cream, "Hexagon centers");
  } else if (spec.id === "poincare-geodesic-lattice") {
    d.background = "#1a303b";
    let warmPaths = "",
      coolPaths = "",
      boundaries = "",
      meridians = "";
    const centers = [128, 384];
    for (const cy of centers)
      for (const cx of centers) {
        const center = { x: cx, y: cy },
          radius = 76,
          count = 16;
        for (const [separation, target] of [
          [3, "cool"],
          [5, "warm"],
        ])
          for (let i = 0; i < count; i++) {
            const j = (i + separation) % count,
              arc = poincareGeodesic(
                center,
                radius,
                (2 * Math.PI * i) / count,
                (2 * Math.PI * j) / count,
                32,
              );
            if (target === "warm") warmPaths += line(arc);
            else coolPaths += line(arc);
          }
        boundaries += circle(cx, cy, radius);
        boundaries += circle(cx, cy, radius * 0.91);
        boundaries += circle(cx, cy, radius * 0.72);
        for (let k = 0; k < 8; k++) {
          const a = (k * Math.PI) / 4;
          meridians += line([
            [cx, cy],
            [cx + radius * Math.cos(a), cy + radius * Math.sin(a)],
          ]);
        }
      }
    add(coolPaths, teal, "Inner geodesic family", 1.35);
    add(warmPaths, gold, "Crossing geodesic family", 1.35);
    add(boundaries, cream, "Ideal-circle and orbit rings", 1.2);
    add(meridians, coral, "Eight radial construction diameters", 0.8);
  } else if (spec.id === "periodic-voronoi-mosaic") {
    d.background = "#e9dfc9";
    const period = 512,
      sites = [];
    for (let row = 0; row < 4; row++)
      for (let col = 0; col < 4; col++) {
        const phase = (col + 1) * 1.71 + (row + 2) * 2.13;
        sites.push({
          row,
          col,
          x: col * 128 + 64 + 13 * Math.sin(phase),
          y: row * 128 + 64 + 11 * Math.cos(phase * 0.83),
        });
      }
    const clipHalfPlane = (points, ax, ay, limit) => {
      const out = [];
      for (let i = 0; i < points.length; i++) {
        const a = points[i],
          b = points[(i + 1) % points.length],
          va = ax * a[0] + ay * a[1] - limit,
          vb = ax * b[0] + ay * b[1] - limit,
          insideA = va <= 1e-9,
          insideB = vb <= 1e-9;
        if (insideA && insideB) out.push(b);
        else if (insideA && !insideB) {
          const t = va / (va - vb);
          out.push([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]);
        } else if (!insideA && insideB) {
          const t = va / (va - vb);
          out.push([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]);
          out.push(b);
        }
      }
      return out;
    };
    const polygonArea = (points) =>
      Math.abs(
        points.reduce((sum, [x, y], i) => {
          const q = points[(i + 1) % points.length];
          return sum + x * q[1] - y * q[0];
        }, 0) / 2,
      );
    const colorPaths = ["", "", "", ""],
      outlines = [],
      markers = [];
    for (const site of sites) {
      // Keep the whole cell, including pieces that cross a repeat edge. The
      // pattern's neighbor tiles supply those wrapped fragments seamlessly.
      let cell = [
        [site.x - period / 2, site.y - period / 2],
        [site.x + period / 2, site.y - period / 2],
        [site.x + period / 2, site.y + period / 2],
        [site.x - period / 2, site.y + period / 2],
      ];
      for (const other of sites)
        for (let dx = -1; dx <= 1; dx++)
          for (let dy = -1; dy <= 1; dy++) {
            if (other === site && dx === 0 && dy === 0) continue;
            const ox = other.x + dx * period,
              oy = other.y + dy * period,
              ax = 2 * (ox - site.x),
              ay = 2 * (oy - site.y),
              limit = ox * ox + oy * oy - site.x * site.x - site.y * site.y;
            cell = clipHalfPlane(cell, ax, ay, limit);
            if (cell.length < 3) break;
          }
      if (cell.length < 3 || polygonArea(cell) < 1e-6) continue;
      const tint = (site.col * 7 + site.row * 3 + (site.col * site.row) % 3) % 4;
      colorPaths[tint] += poly(cell);
      outlines.push(cell);
      markers.push(
        poly([
          [site.x, site.y - 8],
          [site.x + 8, site.y],
          [site.x, site.y + 8],
          [site.x - 8, site.y],
        ]),
      );
    }
    [teal, coral, gold, "#53717a"].forEach((color, i) =>
      add(colorPaths[i], color, `Voronoi cell family ${i + 1}`),
    );
    add(
      outlines.map((points) => poly(points)).join(""),
      "#263c43",
      "Voronoi cell joints",
      1.2,
    );
    add(markers.join(""), cream, "Displaced site diamonds");
    add(
      sites.map(({ x, y }) => circle(x, y, 2.2)).join(""),
      dark,
      "Voronoi site centers",
    );
  } else if (spec.id === "koch-snowflake-field") {
    d.background = "#1b3039";
    const snowflakes = ["", ""];
    let outlines = "",
      constructions = "";
    const rotate = ([x, y], a) => [
      x * Math.cos(a) - y * Math.sin(a),
      x * Math.sin(a) + y * Math.cos(a),
    ];
    const kochSegment = (a, b, depth) => {
      if (!depth) return [a, b];
      const v = [(b[0] - a[0]) / 3, (b[1] - a[1]) / 3],
        p = [a[0] + v[0], a[1] + v[1]],
        q = [a[0] + 2 * v[0], a[1] + 2 * v[1]],
        rotated = rotate(v, Math.PI / 3),
        r = [p[0] + rotated[0], p[1] + rotated[1]],
        parts = [
          kochSegment(a, p, depth - 1),
          kochSegment(p, r, depth - 1),
          kochSegment(r, q, depth - 1),
          kochSegment(q, b, depth - 1),
        ];
      return parts
        .slice(1)
        .reduce((all, part) => all.concat(part.slice(1)), parts[0]);
    };
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const radius = 65,
          base = [
            [cx, cy - radius],
            [cx + (radius * Math.sqrt(3)) / 2, cy + radius / 2],
            [cx - (radius * Math.sqrt(3)) / 2, cy + radius / 2],
          ],
          points = [];
        for (let i = 0; i < 3; i++) {
          const side = kochSegment(base[i], base[(i + 1) % 3], 3);
          points.push(...side.slice(0, -1));
        }
        const path = line(points) + "Z",
          colorIndex = ((cx + cy) / 256 - 1) % 2;
        snowflakes[colorIndex] += path;
        outlines += path;
        constructions += poly(base);
        constructions += circle(cx, cy, 5);
      }
    add(snowflakes[0], teal, "Koch island fields A");
    add(snowflakes[1], coral, "Koch island fields B");
    add(outlines, dark, "Level-three snowflake edges", 1.15);
    add(constructions, gold, "Equilateral parent triangles and centers", 0.95);
  } else if (spec.id === "archimedean-counterspirals") {
    d.background = "#203741";
    let spirals = "",
      underlay = "",
      orbitRings = "",
      ticks = "";
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        for (const direction of [1, -1]) {
          const points = [];
          for (let i = 0; i <= 240; i++) {
            const t = (i / 240) * Math.PI * 4,
              r = 5 + 5 * t,
              a = direction * t + (direction < 0 ? Math.PI / 2 : 0);
            points.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]);
          }
          const path = line(points);
          spirals += path;
          underlay += path;
        }
        for (const r of [20, 40, 60]) orbitRings += circle(cx, cy, r);
        for (let k = 0; k < 16; k++) {
          const a = (k * Math.PI) / 8,
            r0 = 62,
            r1 = k % 2 ? 66 : 70;
          ticks += line([
            [cx + r0 * Math.cos(a), cy + r0 * Math.sin(a)],
            [cx + r1 * Math.cos(a), cy + r1 * Math.sin(a)],
          ]);
        }
      }
    add(underlay, dark, "Spiral ribbon underlay", 3.4);
    add(spirals, gold, "Counter-rotating Archimedean arms", 1.55);
    add(orbitRings, teal, "Concentric orbit guides", 0.95);
    add(ticks, cream, "Peripheral scale marks", 1.05);
  } else if (spec.id === "superellipse-contour-field") {
    d.background = "#e9dfca";
    let squareFamily = "",
      rotatedFamily = "",
      cardinal = "",
      centers = "";
    const superellipse = (cx, cy, radius, exponent, rotation, samples = 256) => {
      const signPow = (v, p) => Math.sign(v) * Math.pow(Math.abs(v), p),
        points = Array.from({ length: samples + 1 }, (_, i) => {
          const t = (i * Math.PI * 2) / samples,
            x = radius * signPow(Math.cos(t), 2 / exponent),
            y = radius * 0.82 * signPow(Math.sin(t), 2 / exponent);
          return [
            cx + x * Math.cos(rotation) - y * Math.sin(rotation),
            cy + x * Math.sin(rotation) + y * Math.cos(rotation),
          ];
        });
      return line(points) + "Z";
    };
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        for (const r of [68, 52, 36, 20])
          squareFamily += superellipse(cx, cy, r, 4, 0);
        for (const r of [59, 43, 27])
          rotatedFamily += superellipse(cx, cy, r, 6, Math.PI / 4);
        for (let k = 0; k < 8; k++) {
          const a = (k * Math.PI) / 4;
          cardinal += line([
            [cx + 68 * Math.cos(a), cy + 68 * Math.sin(a)],
            [cx + 74 * Math.cos(a), cy + 74 * Math.sin(a)],
          ]);
        }
        centers += poly([
          [cx, cy - 7],
          [cx + 7, cy],
          [cx, cy + 7],
          [cx - 7, cy],
        ]);
      }
    add(squareFamily, teal, "Fourth-power Lamé contours", 1.5);
    add(rotatedFamily, gold, "Rotated sixth-power contours", 1.25);
    add(cardinal, coral, "Eight contour axes", 1.05);
    add(centers, dark, "Contour center diamonds");
  } else if (spec.id === "bernoulli-lemniscate-field") {
    d.background = "#1b3039";
    let lemniscates = "",
      underlay = "",
      insetPaths = "",
      centers = "";
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const trace = (a, phase = 0) => {
          const points = Array.from({ length: 513 }, (_, i) => {
            const t = (i * Math.PI * 2) / 512 + phase,
              den = 1 + Math.sin(t) ** 2,
              x = (a * Math.SQRT2 * Math.cos(t)) / den,
              y = (a * Math.SQRT2 * Math.cos(t) * Math.sin(t)) / den;
            return [cx + x, cy + y];
          });
          return line(points) + "Z";
        };
        const path = trace(48);
        lemniscates += path;
        underlay += path;
        insetPaths += trace(30, Math.PI / 2);
        centers += circle(cx, cy, 4);
      }
    add(underlay, dark, "Figure-eight ribbon underlay", 4.2);
    add(lemniscates, gold, "Bernoulli double-point curves", 2.05);
    add(insetPaths, teal, "Quarter-turned inner orbits", 1.2);
    add(centers, coral, "Lemniscate crossing jewels");
  } else if (spec.id === "torus-knot-projection") {
    d.background = "#1c333d";
    let full = "",
      low = "",
      high = "",
      rings = "";
    const p = 2,
      q = 3,
      major = 36,
      minor = 15,
      samples = 480;
    for (const cy of [128, 384])
      for (const cx of [128, 384]) {
        const trace = Array.from({ length: samples + 1 }, (_, i) => {
          const t = (i * Math.PI * 2) / samples,
            z = minor * Math.sin(q * t),
            r = major + minor * Math.cos(q * t);
          return {
            t,
            z,
            point: [cx + r * Math.cos(p * t), cy + r * Math.sin(p * t)],
          };
        });
        full += line(trace.map((v) => v.point)) + "Z";
        let branch = [],
          branchSign = 0;
        for (let i = 0; i < samples; i++) {
          const a = trace[i],
            b = trace[i + 1],
            sign = Math.sin((q * (a.t + b.t)) / 2) >= 0 ? 1 : -1;
          if (branchSign && branchSign !== sign) {
            const path = line(branch);
            if (branchSign > 0) high += path;
            else low += path;
            branch = [a.point];
          } else if (!branch.length) branch = [a.point];
          branch.push(b.point);
          branchSign = sign;
          if (i === samples - 1) {
            const path = line(branch);
            if (branchSign > 0) high += path;
            else low += path;
          }
        }
        rings += circle(cx, cy, major + minor + 5);
        rings += circle(cx, cy, major - minor - 5);
      }
    add(full, dark, "Projected knot backing", 4.2);
    add(low, teal, "Negative-depth passages", 2.05);
    add(high, gold, "Positive-depth overpasses", 2.05);
    add(rings, cream, "Torus tube bounds", 0.95);
  }
  return d;
}
