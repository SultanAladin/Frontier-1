import { stitchTypes, applyStitches } from "./patternStitches.js";
import {
  weaveFamilies,
  weaveVariants,
  weaveFamilyName,
  weaveDraft,
  applyWeave,
} from "./patternWeaves.js";
export { weaveFamilies, weaveDraft } from "./patternWeaves.js";
// Palettes apply to printed geometry only, never to stitch/weave identities.
export const textilePalettes = {
  Indigo: ["#142c49", "#eee3ca", "#387594", "#87acb1", "#bc8252", "#19202a"],
  Earth: ["#593b31", "#ecd9ae", "#ad533c", "#c79448", "#788265", "#282c25"],
  Studio: ["#222832", "#f4eee2", "#dc5245", "#e3b84e", "#258879", "#3871ad"],
  Mulberry: ["#462f4f", "#f2dfda", "#a95c7d", "#d39890", "#8285ad", "#c8b97d"],
};
export const geometricFamilies = [
  "Polka dots",
  "Micro dots",
  "Concentric rings",
  "Hexagon cells",
  "Triangle field",
  "Chevron bands",
  "Zigzag ribbons",
  "Scallop fans",
  "Greek key",
  "Pinwheel tiles",
  "Interlocking squares",
  "Flower lattice",
  "Petal repeat",
  "Diamond grid",
  "Brick bond",
  "Wave lattice",
];
export const colorFamilies = [
  "Gingham check",
  "Tartan bands",
  "Windowpane check",
  "Madras check",
  "Pinstripes",
  "Awning stripes",
  "Candy stripes",
  "Color blocks",
  "Harlequin diamonds",
  "Houndstooth check",
  "Argyle diamonds",
  "Ombre bands",
];
export const textileLibraryEntries = [
  ...Object.keys(weaveVariants).map((family) => ({
    family,
    name: family,
    group: "Fabric weaves",
    palette: "Single yarn",
  })),
  ...stitchTypes.map((family) => ({
    family,
    name: family,
    group: "Stitch patterns",
    palette: "Single thread",
  })),
  ...[
    ...geometricFamilies.map((family) => ({
      family,
      group: "Geometric designs",
    })),
    ...colorFamilies.map((family) => ({ family, group: "Color patterns" })),
  ].flatMap((spec) =>
    Object.keys(textilePalettes).map((palette) => ({
      ...spec,
      palette,
      name: spec.family + " - " + palette,
    })),
  ),
];
const textileNum = (n) => Number(n.toFixed(3));
const textilePoly = (points) =>
  points
    .map(
      ([x, y], i) =>
        (i ? "L" : "M") + textileNum(x / 5.12) + " " + textileNum(y / 5.12),
    )
    .join("") + "Z";
const textileRect = (x, y, w, h) =>
  textilePoly([
    [x, y],
    [x + w, y],
    [x + w, y + h],
    [x, y + h],
  ]);
export function textilePattern(name) {
  // Old palette-qualified names remain factory inputs for exported projects,
  // but they all resolve to the same monochrome construction now.
  const legacy = name
    .replace(/ - (Indigo|Earth|Studio|Mulberry)$/, "")
    .replace("Satin bars", "Satin stitch");
  const draft = weaveFamilies.includes(legacy)
    ? legacy
    : weaveVariants[legacy]?.[0];
  const spec =
    textileLibraryEntries.find((p) => p.name === name) ||
    textileLibraryEntries.find(
      (p) => p.name === (draft ? weaveFamilyName(draft) : legacy),
    );
  if (!spec) return null;
  const mono = ["Fabric weaves", "Stitch patterns"].includes(spec.group);
  const c = mono ? ["#353b3e", "#ded3bb"] : textilePalettes[spec.palette],
    d = {
      schema: "alloy.pattern.v1",
      name: mono ? spec.family : name,
      collection: spec.group,
      library: { family: spec.family, palette: spec.palette },
      background: c[0],
      repeat: "straight",
      repeats: 1,
      mapping: "uv",
      layers: [],
    };
  const layer = (path, color, props = {}) =>
    d.layers.push({
      kind: "path",
      name: spec.family,
      path,
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      color,
      finish: "cotton",
      relief: 0.12,
      ...props,
    });
  const ground = () =>
    d.layers.push({
      kind: "rect",
      name: "Fabric ground",
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      color: c[0],
      finish: "cotton",
      relief: 0,
    });
  ground();
  if (spec.group === "Stitch patterns")
    return applyStitches(d, {
      type: spec.family,
      color: c[1],
      spacing: 40,
      width: 3.5,
      relief: 0.6,
    });
  if (spec.group === "Fabric weaves")
    return applyWeave(d, {
      draft: draft || weaveVariants[spec.family][0],
      color: c[1],
    });
  const paths = Array.from({ length: 6 }, () => []);
  const poly = (pts, k = 1) => paths[k].push(textilePoly(pts));
  const rect = (x, y, w, h, k = 1) => paths[k].push(textileRect(x, y, w, h));
  const ellipse = (x, y, r, k = 1) =>
    paths[k].push(
      `M${textileNum((x - r) / 5.12)} ${textileNum(y / 5.12)}a${textileNum(r / 5.12)} ${textileNum(r / 5.12)} 0 1 0 ${textileNum((2 * r) / 5.12)} 0a${textileNum(r / 5.12)} ${textileNum(r / 5.12)} 0 1 0 ${textileNum((-2 * r) / 5.12)} 0Z`,
    );
  const diamond = (x, y, r, k) =>
    poly(
      [
        [x, y - r],
        [x + r, y],
        [x, y + r],
        [x - r, y],
      ],
      k,
    );
  const f = spec.family;
  if (spec.group === "Color patterns") {
    if (
      [
        "Gingham check",
        "Tartan bands",
        "Windowpane check",
        "Madras check",
      ].includes(f)
    ) {
      for (let i = 0; i < 8; i++) {
        const p = i * 64;
        if (f === "Gingham check") {
          rect(p, 0, 32, 512, 2);
          rect(0, p, 512, 32, 3);
          for (let j = 0; j < 8; j++) rect(p, j * 64, 32, 32, 1);
        }
        if (f === "Windowpane check") {
          rect(p, 0, 3, 512, 1);
          rect(0, p, 512, 3, 1);
          rect(p + 7, 0, 1, 512, 4);
          rect(0, p + 7, 512, 1, 4);
        }
        if (f === "Tartan bands") {
          rect(p, 0, 22, 512, 2);
          rect(0, p, 512, 22, 3);
          rect(p + 36, 0, 4, 512, 1);
          rect(0, p + 36, 512, 4, 1);
          rect(p + 47, 0, 2, 512, 4);
          rect(0, p + 47, 512, 2, 4);
        }
        if (f === "Madras check") {
          rect(p, 0, i % 2 ? 39 : 19, 512, 2 + (i % 3));
          rect(0, p, 512, i % 2 ? 15 : 29, 1 + (i % 3));
        }
      }
    } else if (
      ["Pinstripes", "Awning stripes", "Candy stripes", "Ombre bands"].includes(
        f,
      )
    ) {
      const n = f === "Ombre bands" ? 32 : f === "Pinstripes" ? 16 : 8;
      for (let i = 0; i < n; i++) {
        if (f === "Ombre bands") {
          const t = (1 - Math.cos((2 * Math.PI * i) / n)) / 2;
          const color =
            "#" +
            [1, 3, 5]
              .map((j) =>
                Math.round(
                  parseInt(c[0].slice(j, j + 2), 16) * (1 - t) +
                    parseInt(c[1].slice(j, j + 2), 16) * t,
                )
                  .toString(16)
                  .padStart(2, "0"),
              )
              .join("");
          layer(textileRect(0, (i * 512) / n, 512, 512 / n), color, {
            name: "Tone band " + (i + 1),
            relief: 0,
          });
        } else if (f === "Candy stripes")
          poly(
            [
              [i * 64, 0],
              [i * 64 + 24, 0],
              [i * 64 + 536, 512],
              [i * 64 + 512, 512],
            ],
            1 + (i % 4),
          );
        else rect((i * 512) / n, 0, f === "Pinstripes" ? 2 : 32, 512, 1);
      }
    } else
      for (let y = 0; y < 8; y++)
        for (let x = 0; x < 8; x++) {
          const px = x * 64,
            py = y * 64,
            k = 1 + ((x + y * 3) % 4);
          if (f === "Color blocks") rect(px, py, 64, 64, k);
          if (f === "Harlequin diamonds") diamond(px + 32, py + 32, 31, k);
          if (f === "Houndstooth check" && (x + y) % 2 === 0)
            poly(
              [
                [px, py],
                [px + 32, py],
                [px + 64, py - 32],
                [px + 64, py],
                [px + 32, py + 32],
                [px + 64, py + 32],
                [px + 32, py + 64],
                [px, py + 64],
                [px, py + 32],
                [px - 32, py + 64],
                [px - 32, py + 32],
                [px, py],
              ],
              1,
            );
          if (f === "Argyle diamonds") {
            diamond(px + 32, py + 32, 30, (x + y) % 2 ? 2 : 3);
            poly(
              [
                [px - 1, py],
                [px + 1, py],
                [px + 65, py + 64],
                [px + 63, py + 64],
              ],
              1,
            );
            poly(
              [
                [px + 63, py],
                [px + 65, py],
                [px + 1, py + 64],
                [px - 1, py + 64],
              ],
              1,
            );
          }
        }
  } else {
    const n = f === "Micro dots" ? 16 : 4,
      step = 512 / n;
    for (let y = 0; y < n; y++)
      for (let x = 0; x < n; x++) {
        const px = x * step,
          py = y * step,
          cx = px + step / 2,
          cy = py + step / 2,
          k = 1 + ((x + y) % 4);
        switch (f) {
          case "Polka dots":
          case "Micro dots":
            ellipse(cx, cy, step * (f === "Micro dots" ? 0.13 : 0.22), 1);
            break;
          case "Concentric rings":
            for (let j = 0; j < 5; j++)
              ellipse(cx, cy, step * (0.44 - j * 0.075), j % 2 ? 0 : 1);
            break;
          case "Hexagon cells": {
            const pts = Array.from({ length: 6 }, (_, i) => [
              cx + Math.cos((i * Math.PI) / 3) * step * 0.46,
              cy + Math.sin((i * Math.PI) / 3) * step * 0.46,
            ]);
            poly(pts, k);
            break;
          }
          case "Triangle field":
            poly(
              [
                [px + 4, py + step - 4],
                [cx, py + 4],
                [px + step - 4, py + step - 4],
              ],
              k,
            );
            break;
          case "Chevron bands":
            poly(
              [
                [px, py + 15],
                [cx, py + 55],
                [px + step, py + 15],
                [px + step, py + 65],
                [cx, py + 105],
                [px, py + 65],
              ],
              k,
            );
            break;
          case "Zigzag ribbons":
            poly(
              [
                [px, py + 12],
                [px + 32, py + 12],
                [px + 96, py + 76],
                [px + 128, py + 76],
                [px + 128, py + 94],
                [px + 88, py + 94],
                [px + 24, py + 30],
                [px, py + 30],
              ],
              1,
            );
            break;
          case "Scallop fans":
            for (let j = 0; j < 5; j++) {
              const r = 60 - j * 10;
              const pts = [
                [cx, py + 110],
                ...Array.from({ length: 17 }, (_, i) => [
                  cx + Math.cos(Math.PI + (i * Math.PI) / 16) * r,
                  py + 110 + Math.sin(Math.PI + (i * Math.PI) / 16) * r,
                ]),
              ];
              poly(pts, j % 2 ? 0 : 1);
            }
            break;
          case "Greek key":
            poly(
              [
                [px + 8, py + 8],
                [px + 120, py + 8],
                [px + 120, py + 120],
                [px + 24, py + 120],
                [px + 24, py + 40],
                [px + 88, py + 40],
                [px + 88, py + 88],
                [px + 56, py + 88],
                [px + 56, py + 72],
                [px + 72, py + 72],
                [px + 72, py + 56],
                [px + 40, py + 56],
                [px + 40, py + 104],
                [px + 104, py + 104],
                [px + 104, py + 24],
                [px + 8, py + 24],
              ],
              1,
            );
            break;
          case "Pinwheel tiles":
            for (let i = 0; i < 4; i++) {
              const a = (i * Math.PI) / 2,
                rot = (u, v) => [
                  cx + u * Math.cos(a) - v * Math.sin(a),
                  cy + u * Math.sin(a) + v * Math.cos(a),
                ];
              poly([rot(0, 0), rot(58, 0), rot(58, -58)], 1 + i);
            }
            break;
          case "Interlocking squares":
            paths[1].push(
              textileRect(px + 8, py + 8, 82, 82) +
                textileRect(px + 18, py + 18, 62, 62),
            );
            paths[2].push(
              textileRect(px + 42, py + 42, 80, 80) +
                textileRect(px + 52, py + 52, 60, 60),
            );
            break;
          case "Flower lattice":
            for (let i = 0; i < 6; i++)
              ellipse(
                cx + Math.cos((i * Math.PI) / 3) * 27,
                cy + Math.sin((i * Math.PI) / 3) * 27,
                18,
                2,
              );
            ellipse(cx, cy, 12, 4);
            break;
          case "Petal repeat":
            for (let i = 0; i < 4; i++) {
              const a = (i * Math.PI) / 2,
                rot = (u, v) => [
                  cx + u * Math.cos(a) - v * Math.sin(a),
                  cy + u * Math.sin(a) + v * Math.cos(a),
                ];
              poly(
                [rot(0, 0), rot(24, -44), rot(0, -59), rot(-24, -44)],
                1 + i,
              );
            }
            break;
          case "Diamond grid":
            diamond(cx, cy, 57, 1);
            diamond(cx, cy, 46, 0);
            diamond(cx, cy, 14, 2);
            break;
          case "Brick bond":
            rect(px + (y % 2) * 64 + 3, py + 3, 122, 122, k);
            break;
          case "Wave lattice": {
            const pts = [];
            for (let i = 0; i <= 24; i++)
              pts.push([
                px + (i * 128) / 24,
                py + 56 + Math.sin((i * Math.PI * 2) / 24) * 25,
              ]);
            for (let i = 24; i >= 0; i--)
              pts.push([
                px + (i * 128) / 24,
                py + 68 + Math.sin((i * Math.PI * 2) / 24) * 25,
              ]);
            poly(pts, k);
            break;
          }
        }
      }
  }
  // Cut out nested interiors with even-odd compound paths; keep them editable
  // without spending a separate document layer on each repeated cell.
  if (
    [
      "Concentric rings",
      "Scallop fans",
      "Interlocking squares",
      "Diamond grid",
    ].includes(f)
  ) {
    // The generator records paths per color; nested interiors are cut out using
    // the even-odd fill rule, leaving the material ground visible.
    for (let k = 1; k < 6; k++)
      if (paths[k].length)
        layer(paths[k].join("") + (k === 1 ? paths[0].join("") : ""), c[k], {
          name: f + " motif " + k,
          fillRule: "evenodd",
        });
  } else
    for (const k of f === "Gingham check"
      ? [0, 2, 3, 4, 5, 1]
      : [0, 1, 2, 3, 4, 5])
      if (paths[k].length)
        layer(paths[k].join(""), c[k], { name: f + " color " + (k + 1) });
  return d;
}

export function replacePatternColor(doc, from, to) {
  if (!/^#[a-f\d]{6}$/i.test(to)) return doc;
  const next = {
    ...doc,
    background: doc.background === from ? to : doc.background,
    layers: doc.layers.map((l) =>
      l.locked
        ? l
        : {
            ...l,
            ...(l.color === from ? { color: to } : {}),
            ...(l.paint && l.stroke === from ? { stroke: to } : {}),
          },
    ),
  };
  if (doc.stitch?.color === from) {
    next.stitch = { ...doc.stitch, color: to };
    next.layers = next.layers.map((l) =>
      !l.locked && ["thread", "highlight"].includes(l.stitchRole)
        ? { ...l, color: to }
        : l,
    );
  }
  if (doc.weave?.color === from) next.weave = { ...doc.weave, color: to };
  return next;
}
