import { richRugPattern } from "./rugCompositions.js";
import { applyStitches } from "./patternStitches.js";
// Original vector constructions inspired by the user's references. No reference
// raster or stock watermark is embedded. This module is also in shader exports.
export const collectionPatterns = [
  { name: "Islamic Star Lattice", group: "Islamic geometry" },
  { name: "Islamic Rosette Mosaic", group: "Islamic geometry" },
  { name: "Islamic Garden Carpet", group: "Islamic geometry", rug: true },
  { name: "Islamic Medallion Carpet", group: "Islamic geometry", rug: true },
  { name: "African Diamond Carpet", group: "African-inspired", rug: true },
  { name: "African Chevron Weave", group: "African-inspired", rug: true },
  { name: "Indigo Diamond Rug", group: "African-inspired", rug: true },
  { name: "Golden Cube Fade", group: "Fading" },
  { name: "Ink Cube Fade", group: "Fading" },
  { name: "Diamond Dissolve", group: "Fading" },
];
const collectionClamp = (v, d, a, b) =>
  Number.isFinite(Number(v)) ? Math.max(a, Math.min(b, Number(v))) : d;
export function normalizeCollectionFade(value = {}) {
  value = value && typeof value === "object" ? value : {};
  const preset = [
    "Golden Cube Fade",
    "Ink Cube Fade",
    "Diamond Dissolve",
  ].includes(value.preset)
    ? value.preset
    : "Golden Cube Fade";
  const start = collectionClamp(value.start, 0.02, 0, 0.9);
  return {
    preset,
    direction: ["down", "up", "left", "right"].includes(value.direction)
      ? value.direction
      : "down",
    columns: Math.round(collectionClamp(value.columns, 12, 4, 22)),
    strength: collectionClamp(value.strength, 1.3, 0.3, 3),
    minimum: collectionClamp(value.minimum, 0.02, 0, 0.6),
    gap: collectionClamp(value.gap, 0.06, 0.01, 0.35),
    start,
    end: Math.max(start + 0.05, collectionClamp(value.end, 0.97, 0.1, 1)),
    loop: value.loop === true,
  };
}
export function collectionFadeAmount(x, y, value) {
  const f = normalizeCollectionFade(value);
  let t =
    f.direction === "left"
      ? 1 - x
      : f.direction === "right"
        ? x
        : f.direction === "up"
          ? 1 - y
          : y;
  if (f.loop) t = 1 - Math.abs(2 * t - 1);
  t = Math.max(0, Math.min(1, (t - f.start) / (f.end - f.start)));
  t = t * t * (3 - 2 * t);
  return (f.minimum + (1 - f.minimum) * Math.pow(t, f.strength)) * (1 - f.gap);
}
const collectionPolygon = (points) =>
  points
    .map(([x, y], i) => (i ? "L" : "M") + x.toFixed(3) + " " + y.toFixed(3))
    .join("") + "Z";
const collectionStar = (points = 8, inner = 0.48) =>
  collectionPolygon(
    Array.from({ length: points * 2 }, (_, i) => {
      const a = -Math.PI / 2 + (i * Math.PI) / points,
        r = i % 2 ? 50 * inner : 50;
      return [50 + Math.cos(a) * r, 50 + Math.sin(a) * r];
    }),
  );
const collectionLayer = (kind, color, finish, props = {}) => ({
  kind,
  color,
  finish,
  x: 256,
  y: 256,
  width: 512,
  height: 512,
  opacity: 1,
  rotation: 0,
  name: kind,
  ...props,
});

export function collectionPattern(name, settings) {
  const spec = collectionPatterns.find((p) => p.name === name);
  if (!spec) return null;
  if (name === "Islamic Medallion Carpet")
    return {
      ...richRugPattern("Azure Arabesque Medallion"),
      name,
      collection: spec.group,
    };
  if (name === "Islamic Garden Carpet")
    return {
      ...richRugPattern("Ivory Palmette Garden"),
      name,
      collection: spec.group,
    };
  if (name === "African Diamond Carpet")
    return {
      ...richRugPattern("Chromatic Diamond Tapestry"),
      name,
      collection: spec.group,
    };
  const d = {
    schema: "alloy.pattern.v1",
    name,
    collection: spec.group,
    presentation: spec.rug ? "rug" : "surface",
    background: "#eee4cb",
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: [],
  };
  const finish = spec.rug ? "wool" : "ceramic";
  const add = (kind, color, props = {}) =>
    d.layers.push(collectionLayer(kind, color, finish, props));
  const path = (p, color, props = {}) =>
    add("path", color, { path: p, ...props });
  const rect = (size, color, props = {}) =>
    add("rect", color, { width: size, height: size, ...props });
  const diamond = (size, color, props = {}) =>
    add("diamond", color, { width: size, height: size, ...props });
  const border = (colors) =>
    colors.forEach((c, i) =>
      rect(512 - i * 13, c, {
        name: "Guard border " + (i + 1),
        relief: 0.28 + i * 0.035,
      }),
    );
  const star = (size, color, n = 8, props = {}) =>
    path(
      collectionStar(n, n === 8 ? Math.SQRT1_2 / Math.cos(Math.PI / 8) : 0.66),
      color,
      {
        width: size,
        height: size,
        ...props,
      },
    );
  const ticks = (color, inset = 21) => {
    let p = "";
    for (let i = 0; i < 30; i++) {
      const t = 17 + i * 16.5;
      for (const [x, y] of [
        [t, inset],
        [t, 512 - inset],
        [inset, t],
        [512 - inset, t],
      ])
        p += collectionPolygon(
          [
            [x - 2, y - 2],
            [x + 2, y - 2],
            [x + 2, y + 2],
            [x - 2, y + 2],
          ].map((a) => a.map((v) => v / 5.12)),
        );
    }
    path(p, color, { name: "Border stitch band", relief: 0.65 });
  };
  if (spec.group === "Fading") {
    const f = normalizeCollectionFade({ ...settings, preset: name });
    d.fade = f;
    d.tileAxes = f.loop
      ? "xy"
      : ["left", "right"].includes(f.direction)
        ? "y"
        : "x";
    d.background = "#fffdf6";
    const cols = f.columns,
      rows = Math.max(4, 2 * Math.round(cols * 0.58)),
      dx = 512 / cols,
      dy = 512 / rows;
    const colors =
      name === "Golden Cube Fade"
        ? ["#c89543", "#e6be76", "#b77b2d"]
        : name === "Ink Cube Fade"
          ? ["#161b21", "#161b21", "#161b21"]
          : ["#194f62", "#347b88", "#8babb0"];
    const paths = ["", "", ""];
    for (let row = 0; row <= rows; row++)
      for (let col = 0; col <= cols; col++) {
        // Boundary rows/columns are included only on the non-repeating axis.
        if (row === rows && d.tileAxes !== "x") continue;
        if (col === cols && d.tileAxes !== "y") continue;
        const x = col * dx + (row % 2) * dx * 0.5,
          y = row * dy;
        const scale = collectionFadeAmount(x / 512, y / 512, f);
        if (scale < 0.0001) continue;
        const v = [
          [0, (-dy * 2) / 3],
          [dx / 2, -dy / 3],
          [dx / 2, dy / 3],
          [0, (dy * 2) / 3],
          [-dx / 2, dy / 3],
          [-dx / 2, -dy / 3],
        ];
        for (let face = 0; face < 3; face++) {
          const poly =
            name === "Diamond Dissolve"
              ? [
                  [0, -dy * 0.3],
                  [dx * 0.18, 0],
                  [0, dy * 0.3],
                  [-dx * 0.18, 0],
                ]
              : [
                  [0, 0],
                  v[face * 2],
                  v[(face * 2 + 1) % 6],
                  v[(face * 2 + 2) % 6],
                ];
          if (name === "Diamond Dissolve" && face) continue;
          const center = poly.reduce(
            (a, p) => [a[0] + p[0] / 4, a[1] + p[1] / 4],
            [0, 0],
          );
          paths[face] += collectionPolygon(
            poly.map((p) => [
              (x + center[0] + (p[0] - center[0]) * scale) / 5.12,
              (y + center[1] + (p[1] - center[1]) * scale) / 5.12,
            ]),
          );
        }
      }
    paths.forEach((p, i) => {
      if (p)
        d.layers.push(
          collectionLayer(
            "path",
            colors[i],
            name === "Golden Cube Fade" ? "foil" : "ink",
            {
              path: p,
              name: ["Upper facets", "Light facets", "Dark facets"][i],
              relief: 0.025,
            },
          ),
        );
    });
    return d;
  }
  if (name === "Islamic Star Lattice") {
    d.background = "#eee4cb";
    rect(512, "#eee4cb", { name: "Ivory ground", relief: 0 });
    for (let y = 0; y < 2; y++)
      for (let x = 0; x < 2; x++) {
        const pos = { x: 128 + x * 256, y: 128 + y * 256 };
        star(244, "#153e50", 8, { ...pos, name: "Eight-point star" });
        star(209, "#d6ab58", 8, pos);
        star(190, "#237d86", 8, pos);
        star(105, "#eee4cb", 8, pos);
        star(69, "#ad4d3b", 8, pos);
        diamond(174, "#d6ab58", {
          x: x * 256,
          y: y * 256,
          name: "Connecting lozenge",
        });
        diamond(143, "#153e50", { x: x * 256, y: y * 256 });
      }
  } else if (name === "Islamic Rosette Mosaic") {
    d.background = "#143e56";
    rect(512, d.background, { name: "Cobalt ground", relief: 0 });
    for (let y = 0; y < 2; y++)
      for (let x = 0; x < 2; x++) {
        const pos = { x: 128 + x * 256, y: 128 + y * 256 };
        for (let j = 0; j < 4; j++)
          star(
            251 - j * 33,
            ["#d7b36c", "#f0e4c9", "#147a80", "#173952"][j],
            12,
            { ...pos, name: "Twelve-point rosette " + (j + 1) },
          );
        star(76, "#d7b36c", 6, pos);
        let lines = "";
        for (let k = 0; k < 12; k++) {
          const a = (k * Math.PI) / 6;
          lines += `M50 50L${50 + 48 * Math.cos(a)} ${50 + 48 * Math.sin(a)}`;
        }
        path(lines, "#ead9ac", {
          ...pos,
          width: 198,
          height: 198,
          strokeWidth: 0.7,
          name: "Radial tracery",
        });
      }
  } else if (name === "Islamic Garden Carpet") {
    border(["#193d4f", "#d6b981", "#883f37", "#d6b981", "#193d4f"]);
    ticks("#eee0b9", 20);
    for (let y = 0; y < 3; y++)
      for (let x = 0; x < 3; x++) {
        const pos = { x: 126 + x * 130, y: 126 + y * 130 };
        star(121, "#e2c38b", 8, { ...pos, name: "Garden star" });
        star(94, (x + y) % 2 ? "#376b69" : "#9a493b", 8, pos);
        star(45, "#e9dbb9", 8, pos);
      }
  } else if (name === "Islamic Medallion Carpet") {
    border(["#213e51", "#d7b67a", "#a4473b", "#d7b67a", "#213e51", "#a4473b"]);
    ticks("#eeddb5", 20);
    for (let j = 0; j < 6; j++)
      star(
        362 - j * 43,
        ["#ecd9ad", "#183b51", "#d0a159", "#377d7f", "#ebdfbe", "#9b4236"][j],
        12,
        { name: "Medallion ring " + (j + 1), relief: 0.45 + j * 0.07 },
      );
    for (const x of [100, 412])
      for (const y of [100, 412]) {
        star(85, "#e3ca94", 8, { x, y });
        star(54, "#173f50", 8, { x, y });
      }
  } else if (name === "African Diamond Carpet") {
    border(["#122237", "#eed6a2", "#1b9468", "#172235"]);
    ticks("#ebdbbb", 19);
    let triangles = "";
    for (let y = 0; y < 4; y++)
      for (let x = 0; x < 5; x++) {
        const cx = 54 + x * 101,
          cy = 61 + y * 126;
        triangles += collectionPolygon(
          [
            [cx - 45, cy + 40],
            [cx, cy - 40],
            [cx + 45, cy + 40],
          ].map((p) => p.map((v) => v / 5.12)),
        );
      }
    path(triangles, "#dcad38", { name: "Triangle field", relief: 0.35 });
    let chevrons = "";
    for (let y = 0; y < 4; y++)
      for (let x = 0; x < 5; x++) {
        const cx = 54 + x * 101,
          cy = 61 + y * 126;
        chevrons += `M${(cx - 44) / 5.12} ${(cy + 10) / 5.12}L${cx / 5.12} ${(cy + 42) / 5.12}L${(cx + 44) / 5.12} ${(cy + 10) / 5.12}`;
      }
    path(chevrons, "#ce4138", { strokeWidth: 3, name: "Red chevron bands" });
    for (const [x, size] of [
      [94, 161],
      [256, 355],
      [418, 161],
    ])
      for (let j = 0; j < 8; j++)
        diamond(
          size - j * size * 0.102,
          [
            "#101c2a",
            "#eee0bd",
            "#2767a0",
            "#101c2a",
            "#d64a38",
            "#e3b73d",
            "#168969",
            "#eddec0",
          ][j],
          {
            x,
            y: 256,
            name: "Diamond medallion " + j,
            relief: 0.35 + j * 0.055,
          },
        );
  } else if (name === "African Chevron Weave") {
    rect(512, "#112737", { name: "Dark woven ground", relief: 0.3 });
    const colors = [
      "#f0dec1",
      "#ce4437",
      "#2976a0",
      "#dfb340",
      "#138269",
      "#eee0c5",
    ];
    for (let band = 0; band < 6; band++) {
      let p = "";
      for (let i = 0; i < 8; i++) {
        const x = i * 64,
          y = band * 85.333;
        p += collectionPolygon(
          [
            [x, y + 6],
            [x + 32, y + 36],
            [x + 64, y + 6],
            [x + 64, y + 38],
            [x + 32, y + 68],
            [x, y + 38],
          ].map((a) => a.map((v) => v / 5.12)),
        );
      }
      path(p, colors[band], {
        name: "Chevron band " + (band + 1),
        relief: 0.6,
      });
      add("rect", "#e9d7b7", {
        x: 256,
        y: band * 85.333 + 78,
        width: 512,
        height: 3,
        name: "Fine guard stripe",
        finish: "cotton",
        relief: 0.1,
      });
    }
  } else if (name === "Indigo Diamond Rug") {
    border(["#1b344e", "#e4d6b5", "#1b344e", "#e4d6b5", "#1b344e"]);
    ticks("#ece2cd", 20);
    for (let y = 0; y < 3; y++)
      for (let x = 0; x < 3; x++) {
        const pos = { x: 124 + x * 132, y: 124 + y * 132 };
        diamond(123, "#e5d8b9", { ...pos, name: "Ivory diamond" });
        diamond(104, "#1b344e", pos);
        diamond(72, "#e5d8b9", pos);
        diamond(43, "#1b344e", pos);
      }
  }
  return d;
}

export function rebuildCollectionFade(doc, patch) {
  const f = normalizeCollectionFade({ ...doc.fade, ...patch }),
    next = collectionPattern(f.preset, f);
  // Preserve assigned finishes and palette while regenerating geometry.
  next.layers = next.layers.map((l) => {
    const old = doc.layers.find((p) => p.name === l.name);
    return old
      ? {
          ...l,
          color: old.color,
          finish: old.finish,
          roughness: old.roughness,
          metalness: old.metalness,
          relief: old.relief,
          opacity: old.opacity,
          visible: old.visible,
        }
      : l;
  });
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
