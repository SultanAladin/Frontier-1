// Monochrome yarn surfaces built directly from periodic over/under drawdowns.
export const weaveVariants = {
  "Plain weave": ["Plain weave"],
  "Basket weave": ["Basket 2x2", "Basket 3x3"],
  "Twill weave": [
    "Twill 2 over 1",
    "Twill 2 over 2",
    "Twill 3 over 1",
    "Reverse twill",
  ],
  "Herringbone weave": ["Herringbone weave"],
  "Broken twill": ["Broken twill"],
  "Diamond twill": ["Diamond twill"],
  "Point twill": ["Point twill"],
  "Satin weave": ["Five shaft satin", "Eight shaft satin"],
  "Rib weave": ["Warp rib", "Weft rib"],
  "Waffle weave": ["Waffle weave"],
};
export const weaveFamilies = Object.values(weaveVariants).flat();
export const weaveFamilyName = (draft) =>
  Object.keys(weaveVariants).find((k) => weaveVariants[k].includes(draft)) ||
  "Plain weave";
export function weavePeriod(family) {
  return (
    {
      "Plain weave": 2,
      "Basket 2x2": 4,
      "Basket 3x3": 6,
      "Twill 2 over 1": 3,
      "Five shaft satin": 5,
      "Eight shaft satin": 8,
      "Herringbone weave": 8,
      "Diamond twill": 6,
      "Point twill": 12,
      "Warp rib": 6,
      "Weft rib": 6,
      "Waffle weave": 8,
    }[family] || 4
  );
}
export function weaveDraft(family, x, y) {
  const mod = (v, n) => ((v % n) + n) % n;
  // Point threading must not duplicate its end shaft: 0,1,2,3,2,1.
  const point = (v) => [0, 1, 2, 3, 2, 1][mod(v, 6)];
  switch (family) {
    case "Basket 2x2":
      return mod(Math.floor(x / 2) + Math.floor(y / 2), 2) === 0;
    case "Basket 3x3":
      return mod(Math.floor(x / 3) + Math.floor(y / 3), 2) === 0;
    case "Twill 2 over 1":
      return mod(x - y, 3) < 2;
    case "Twill 3 over 1":
      return mod(x - y, 4) < 3;
    case "Reverse twill":
      return mod(x + y, 4) < 2;
    case "Herringbone weave":
      return mod([0, 1, 2, 3, 1, 0, 3, 2][mod(x, 8)] - y, 4) < 2;
    case "Broken twill":
      return mod(x - [0, 1, 3, 2][mod(y, 4)], 4) < 2;
    case "Diamond twill":
      return mod(point(x) - point(y), 4) < 2;
    case "Point twill":
      return mod(point(x) - y, 4) < 2;
    case "Five shaft satin":
      return mod(x - 2 * y, 5) !== 0;
    case "Eight shaft satin":
      return mod(x - 3 * y, 8) !== 0;
    case "Warp rib":
      return mod(x + Math.floor(y / 3), 2) === 0;
    case "Weft rib":
      return mod(Math.floor(x / 3) + y, 2) === 0;
    case "Waffle weave": {
      // Ordinary 8x8 honeycomb, transcribed from Fabric Structure Part 3,
      // slide 5, diagram F (black binding marks AND blue float fills).
      // https://www.slideshare.net/slideshow/fabric-structure-part-3/249464860
      const rows = [
        "01011101",
        "00101010",
        "00010100",
        "00001000",
        "00010100",
        "00101010",
        "01011101",
        "10111110",
      ];
      return rows[mod(y, 8)][mod(x, 8)] === "1";
    }
    case "Twill 2 over 2":
      return mod(x - y, 4) < 2;
    default:
      return mod(x + y, 2) === 0;
  }
}
export function normalizeWeave(value = {}) {
  const v = value || {};
  return {
    draft: weaveFamilies.includes(v.draft) ? v.draft : "Plain weave",
    color: /^#[a-f\d]{6}$/i.test(v.color || "") ? v.color : "#ded3bb",
  };
}
export function weaveLayers(value) {
  const s = normalizeWeave(value),
    period = weavePeriod(s.draft),
    n = Math.ceil(24 / period) * period,
    step = 512 / n;
  const num = (n) => Number((n / 5.12).toFixed(2));
  const rect = (x, y, w, h) =>
    `M${num(x)} ${num(y)}h${num(w)}v${num(h)}h${num(-w)}Z`;
  const bands = [[], [], [], [], []];
  const floatPath = (x, y, w, h, startRound, endRound, warp) => {
    const p = (u, v) =>
      warp ? `${num(x + u)} ${num(y + v)}` : `${num(y + v)} ${num(x + u)}`;
    return `M${p(0, startRound)}Q${p(0, 0)} ${p(w / 2, 0)}Q${p(w, 0)} ${p(w, startRound)}L${p(w, h - endRound)}Q${p(w, h)} ${p(w / 2, h)}Q${p(0, h)} ${p(0, h - endRound)}Z`;
  };
  // No flat full-width underlay hides the crossings. The visible upper yarn
  // at each cell runs along its float and dips at binding points. Three
  // concentric bands encode a rounded crown in relief, not painted glints.
  for (let y = 0; y < n; y++)
    for (let x = 0; x < n; x++) {
      const warp = weaveDraft(s.draft, x, y);
      const prev =
        weaveDraft(s.draft, x - (warp ? 0 : 1), y - (warp ? 1 : 0)) === warp;
      const next =
        weaveDraft(s.draft, x + (warp ? 0 : 1), y + (warp ? 1 : 0)) === warp;
      for (let k = 0; k < 5; k++) {
        const w = step * [0.8, 0.71, 0.55, 0.34, 0.12][k],
          inset = (step - w) / 2;
        // Full silhouette underneath, with progressive relief taper near the
        // two binding points. Floats crossing tile edges remain unbroken.
        const end =
          k === 0 ? 0.025 * step : step * [0.0, 0.055, 0.095, 0.13, 0.16][k];
        const a = prev ? 0 : end,
          b = next ? 0 : end;
        bands[k].push(
          floatPath(
            (warp ? x : y) * step + inset,
            (warp ? y : x) * step + a,
            w,
            step - a - b,
            prev ? 0 : step * 0.19,
            next ? 0 : step * 0.19,
            warp,
          ),
        );
      }
    }
  let under = "";
  for (let i = 0; i < n; i++) {
    under += rect((i + 0.14) * step, 0, 0.72 * step, 512);
    under += rect(0, (i + 0.14) * step, 512, 0.72 * step);
  }
  return [
    {
      kind: "path",
      name: "Recessed continuous yarns",
      weaveRole: "yarn",
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      path: under,
      color: s.color,
      finish: "cotton",
      roughness: 0.84,
      metalness: 0,
      relief: 0.025,
      opacity: 1,
    },
    ...bands.map((paths, i) => ({
      kind: "path",
      name: [
        "Yarn silhouette",
        "Yarn shoulders",
        "Yarn rise",
        "Yarn crown",
        "Yarn crest",
      ][i],
      weaveRole: "yarn",
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      path: paths.join(""),
      color: s.color,
      finish: "cotton",
      roughness: 0.84,
      metalness: 0,
      relief: [0.08, 0.18, 0.26, 0.31, 0.33][i],
      opacity: 1,
    })),
  ];
}
export function applyWeave(doc, patch = {}) {
  const weave = normalizeWeave({ ...doc.weave, ...patch });
  const layers = doc.layers.filter((l) => !l.weaveRole);
  const stitchAt = layers.findIndex((l) => l.stitchRole);
  layers.splice(
    stitchAt < 0 ? layers.length : stitchAt,
    0,
    ...weaveLayers(weave),
  );
  if (layers.length > 64) throw new Error("Weaving needs 6 free layers.");
  return { ...doc, weave, layers };
}
