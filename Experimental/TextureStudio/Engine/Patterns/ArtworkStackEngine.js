/*==========================================================================================================================================
                                                          ARTWORKSTACKENGINE.JS
==========================================================================================================================================*/
/* Unified non-destructive Layer-Stack + Modifier-Stack authoring and multi-channel PBR evaluator for Pattern Studio & Stamp Studio. */

import { evaluateTileNoise } from "../NoiseField.js";
import {
  ALL_PAINT_CHANNELS,
  ALL_PAINT_CHANNEL_ROWS,
  ALL_PAINT_CHANNEL_MAP,
  ALL_EXTENDED_PBR_KEYS,
  isRgbChannelKey,
  createDefaultChannelMap,
  createDefaultExtendedValues,
  encodeScalarChannelByte,
  defaultByteForExtChannel,
} from "../PbrChannelDefinitions.js";

export {
  ALL_PAINT_CHANNELS as ARTWORK_PBR_CHANNELS,
  ALL_PAINT_CHANNEL_ROWS as ARTWORK_PBR_CHANNEL_ROWS,
  ALL_PAINT_CHANNEL_MAP as ARTWORK_PBR_CHANNEL_MAP,
  ALL_EXTENDED_PBR_KEYS as ARTWORK_EXTENDED_PBR_KEYS,
};

export const ARTWORK_SIZE = 512;

export const ARTWORK_FINISHES = {
  ink: { name: "Printed Dye", label: "Printed Dye", roughness: 0.62, metalness: 0.0, height: 0.02 },
  cotton: { name: "Woven Cotton", label: "Woven Cotton", roughness: 0.84, metalness: 0.0, height: 0.14 },
  wool: { name: "Cut-Pile Wool", label: "Cut-Pile Wool", roughness: 0.94, metalness: 0.0, height: 0.55 },
  ceramic: { name: "Glazed Ceramic", label: "Glazed Ceramic", roughness: 0.18, metalness: 0.0, height: 0.24 },
  foil: { name: "Metal Inlay", label: "Metal Inlay", roughness: 0.24, metalness: 1.0, height: 0.10 },
  rubber: { name: "Embossed Rubber", label: "Embossed Rubber", roughness: 0.78, metalness: 0.0, height: 0.36 },
};

export const MOTIF_TYPES = [
  { id: "circle", label: "Circle" },
  { id: "ring", label: "Ring" },
  { id: "square", label: "Square" },
  { id: "roundedRect", label: "Rounded Box" },
  { id: "diamond", label: "Diamond" },
  { id: "hexagon", label: "Hexagon" },
  { id: "triangle", label: "Triangle" },
  { id: "star", label: "Star" },
  { id: "cross", label: "Cross" },
  { id: "chevron", label: "Chevron" },
  { id: "stripe", label: "Stripe / Bar" },
  { id: "arc", label: "Arc" },
  { id: "teardrop", label: "Teardrop" },
  { id: "leaf", label: "Botanical Leaf" },
  { id: "flower", label: "5-Petal Flower" },
  { id: "gear", label: "Cog / Gear" },
  { id: "line", label: "Line / Stroke" },
  { id: "svg-path", label: "Vector Path" },
];

export const MODIFIER_DEFINITIONS = [
  { type: "noise-scale", label: "Noise Scale", badge: "NOISE", description: "Modulates instance scale, opacity, or color smoothly using coherent seamless noise." },
  { type: "array-grid", label: "Grid / Hex Array", badge: "ARRAY", description: "Repeats the motif into Grid, Brick, Half-Drop, or Hexagonal lattices." },
  { type: "array-radial", label: "Radial Array", badge: "RADIAL", description: "Distributes copies in concentric rings or mandala petals around a pivot." },
  { type: "mirror", label: "Mirror Symmetry", badge: "MIRROR", description: "Reflects instances cleanly across X, Y, Quad, or Diagonal axes." },
  { type: "inset-echo", label: "Inset / Echo", badge: "ECHO", description: "Creates nested concentric inner/outer rings with color and relief stepping." },
  { type: "noise-jitter", label: "Scatter & Dropout", badge: "SCATTER", description: "Deterministic random offset, rotation, scale jitter, and organic thinning." },
  { type: "warp", label: "Domain Warp", badge: "WARP", description: "Non-destructive wave, swirl, or curl distortion across motif positions." },
  { type: "edge-wear", label: "Bevel & Edge Wear", badge: "WEAR", description: "Adds contour stroke, bevel height boost, and distressed noise chipping." },
];

let uidSeq = 1;
export function nextArtworkId(prefix = "layer") {
  return `${prefix}-${Date.now().toString(36)}-${(uidSeq++).toString(36)}`;
}

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function hexToRgb(hex) {
  const m = /^#?([a-f\d]{6})$/i.exec(String(hex || "#ffffff"));
  if (!m) return [255, 255, 255];
  return [
    parseInt(m[1].slice(0, 2), 16),
    parseInt(m[1].slice(2, 4), 16),
    parseInt(m[1].slice(4, 6), 16),
  ];
}

function lerpHex(hexA, hexB, t) {
  const [r1, g1, b1] = hexToRgb(hexA);
  const [r2, g2, b2] = hexToRgb(hexB);
  const u = clamp(t, 0, 1);
  const r = Math.round(r1 + (r2 - r1) * u);
  const g = Math.round(g1 + (g2 - g1) * u);
  const b = Math.round(b1 + (b2 - b1) * u);
  return `#${r.toString(16).padStart(2, "0")}${g.toString(16).padStart(2, "0")}${b.toString(16).padStart(2, "0")}`;
}

function detHash(a, b, seed) {
  let h = (a * 374761393 + b * 668265263 + seed * 1442695041) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const srgbChToLin = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
const linChToSrgb = (v) => {
  const c = clamp(v, 0, 1);
  return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
};

export function createDefaultPointGradient2D(colorA = "#eff4cf", colorB = "#0b0b0b", size = ARTWORK_SIZE) {
  return {
    schema: "alloy.point-gradient.v1",
    space: "scene-2d",
    falloff: 1.0,
    background: colorB || "#0b0b0b",
    points: [
      { id: "pg_1", x: size * 0.34, y: size * 0.34, color: colorA || "#eff4cf", radius: size * 0.42, weight: 1.0 },
      { id: "pg_2", x: size * 0.68, y: size * 0.68, color: colorB || "#0b0b0b", radius: size * 0.44, weight: 1.0 },
    ],
  };
}

export function normalizePointGradient2D(raw, fallbackA = "#eff4cf", fallbackB = "#0b0b0b", size = ARTWORK_SIZE) {
  const src = raw && typeof raw === "object" ? raw : null;
  const rawPts = Array.isArray(src?.points) ? src.points : [];
  const points = (rawPts.length ? rawPts : createDefaultPointGradient2D(fallbackA, fallbackB, size).points)
    .slice(0, 12)
    .map((p, idx) => ({
      id: typeof p?.id === "string" && p.id ? p.id : `pg_${idx + 1}`,
      x: clamp(Number(p?.x ?? size * 0.5), -size * 0.5, size * 1.5),
      y: clamp(Number(p?.y ?? size * 0.5), -size * 0.5, size * 1.5),
      color: typeof p?.color === "string" && /^#[0-9a-f]{6}$/i.test(p.color) ? p.color.toLowerCase() : (fallbackA || "#eff4cf"),
      radius: clamp(Number(p?.radius ?? size * 0.42), 16, size * 2.0),
      weight: clamp(Number(p?.weight ?? 1.0), 0.02, 1.0),
    }));
  return {
    schema: "alloy.point-gradient.v1",
    space: "scene-2d",
    falloff: clamp(Number(src?.falloff ?? 1.0), 0.2, 3.0),
    background: typeof src?.background === "string" && /^#[0-9a-f]{6}$/i.test(src.background) ? src.background.toLowerCase() : (fallbackB || "#0b0b0b"),
    points,
  };
}

export function evaluatePointGradient2DAt(x, y, rawGrad, { size = ARTWORK_SIZE, seamless = false } = {}) {
  const g = rawGrad?.schema === "alloy.point-gradient.v1" ? rawGrad : normalizePointGradient2D(rawGrad, "#eff4cf", "#0b0b0b", size);
  const bg = hexToRgb(g.background).map((v) => srgbChToLin(v / 255));
  const falloff = clamp(g.falloff ?? 1.0, 0.2, 3.0);
  const half = size * 0.5;

  let sr = 0, sg = 0, sb = 0, tw = 0, pw = 0;
  for (let i = 0; i < g.points.length; i++) {
    const pt = g.points[i];
    if (pt.weight <= 0 || pt.radius <= 0) continue;
    let dx = x - pt.x;
    let dy = y - pt.y;
    if (seamless) {
      dx = ((((dx + half) % size) + size) % size) - half;
      dy = ((((dy + half) % size) + size) % size) - half;
    }
    const r = Math.max(8, pt.radius * falloff);
    const w = pt.weight * Math.exp(-(dx * dx + dy * dy) / (r * r));
    if (w <= 1e-4) continue;
    const [pr, pg, pb] = hexToRgb(pt.color).map((v) => srgbChToLin(v / 255));
    sr += pr * w;
    sg += pg * w;
    sb += pb * w;
    tw += w;
    if (w > pw) pw = w;
  }

  let lr = bg[0], lg = bg[1], lb = bg[2];
  if (tw > 1e-5) {
    const blend = clamp(pw, 0, 1);
    lr = bg[0] * (1 - blend) + (sr / tw) * blend;
    lg = bg[1] * (1 - blend) + (sg / tw) * blend;
    lb = bg[2] * (1 - blend) + (sb / tw) * blend;
  }
  return [
    Math.round(linChToSrgb(lr) * 255),
    Math.round(linChToSrgb(lg) * 255),
    Math.round(linChToSrgb(lb) * 255),
  ];
}

export function rasterizePointGradient2D(rawGrad, { res = 128, size = ARTWORK_SIZE, seamless = false } = {}) {
  const g = normalizePointGradient2D(rawGrad, rawGrad?.background || "#eff4cf", "#5a7692", size);
  const out = new Uint8ClampedArray(res * res * 4);
  const bg = hexToRgb(g.background).map((v) => srgbChToLin(v / 255));
  const falloff = clamp(g.falloff ?? 1.0, 0.2, 3.0);
  const half = size * 0.5;
  const pre = g.points.map((pt) => {
    const rgb = hexToRgb(pt.color).map((v) => srgbChToLin(v / 255));
    const r = Math.max(8, pt.radius * falloff);
    return { x: pt.x, y: pt.y, r: rgb[0], g: rgb[1], b: rgb[2], invR2: 1 / (r * r), weight: pt.weight };
  });

  for (let py = 0; py < res; py++) {
    const sy = ((py + 0.5) / res) * size;
    for (let px = 0; px < res; px++) {
      const sx = ((px + 0.5) / res) * size;
      let sr = 0, sg = 0, sb = 0, tw = 0, pw = 0;
      for (let k = 0; k < pre.length; k++) {
        const pt = pre[k];
        let dx = sx - pt.x;
        let dy = sy - pt.y;
        if (seamless) {
          dx = ((((dx + half) % size) + size) % size) - half;
          dy = ((((dy + half) % size) + size) % size) - half;
        }
        const w = pt.weight * Math.exp(-(dx * dx + dy * dy) * pt.invR2);
        if (w <= 1e-4) continue;
        sr += pt.r * w; sg += pt.g * w; sb += pt.b * w;
        tw += w;
        if (w > pw) pw = w;
      }
      let lr = bg[0], lg = bg[1], lb = bg[2];
      if (tw > 1e-5) {
        const blend = clamp(pw, 0, 1);
        lr = bg[0] * (1 - blend) + (sr / tw) * blend;
        lg = bg[1] * (1 - blend) + (sg / tw) * blend;
        lb = bg[2] * (1 - blend) + (sb / tw) * blend;
      }
      const o = (py * res + px) * 4;
      out[o] = Math.round(linChToSrgb(lr) * 255);
      out[o + 1] = Math.round(linChToSrgb(lg) * 255);
      out[o + 2] = Math.round(linChToSrgb(lb) * 255);
      out[o + 3] = 255;
    }
  }
  return out;
}

const ptGradCache = new Map();
function getPointGradientCanvasAndUrl(rawGrad, { res = 128, size = ARTWORK_SIZE, seamless = false } = {}) {
  const g = normalizePointGradient2D(rawGrad, "#eff4cf", "#5a7692", size);
  const sig = `${res}|${size}|${seamless ? 1 : 0}|${JSON.stringify(g)}`;
  if (ptGradCache.has(sig)) return ptGradCache.get(sig);
  const rgba = rasterizePointGradient2D(g, { res, size, seamless });
  let canvas = null;
  let dataUrl = null;
  try {
    if (typeof document !== "undefined") {
      canvas = document.createElement("canvas");
      canvas.width = res;
      canvas.height = res;
      const ctx = canvas.getContext?.("2d");
      if (ctx && typeof ctx.createImageData === "function" && typeof ctx.putImageData === "function") {
        const img = ctx.createImageData(res, res);
        img.data.set(rgba);
        ctx.putImageData(img, 0, 0);
        if (typeof canvas.toDataURL === "function") {
          const url = canvas.toDataURL("image/png");
          if (typeof url === "string" && url.startsWith("data:image/")) dataUrl = url;
        }
      }
    }
  } catch {
    canvas = null;
    dataUrl = null;
  }
  const entry = { canvas, dataUrl, grad: g };
  if (ptGradCache.size > 48) ptGradCache.delete(ptGradCache.keys().next().value);
  ptGradCache.set(sig, entry);
  return entry;
}

export function createDefaultModifier(type = "noise-scale") {
  const id = nextArtworkId("mod");
  switch (type) {
    case "noise-scale":
      return {
        id,
        type,
        enabled: true,
        noiseType: "fbm",
        target: "scale-color", // "uniform" | "width" | "height" | "scale-opacity" | "scale-color"
        minScale: 0.25,
        maxScale: 1.85,
        frequency: 3,
        octaves: 3,
        seed: 1337,
        contrast: 1.15,
        bias: 0.0,
        curve: "smoothstep",
        invert: false,
        stretchX: 1,
        stretchY: 1,
        offsetX: 0,
        offsetY: 0,
        seamless: true,
        colorB: "#7aa6d9",
        dropoutThreshold: 0.0,
      };
    case "array-grid":
      return {
        id,
        type,
        enabled: true,
        layout: "hex", // "grid" | "brick" | "half-drop" | "hex"
        columns: 8,
        rows: 8,
        spanX: 512,
        spanY: 512,
        center: true,
        alternateColor: false,
        colorB: "#5a7c8c",
        alternateRotate: 0,
      };
    case "array-radial":
      return {
        id,
        type,
        enabled: true,
        count: 8,
        radius: 130,
        rings: 1,
        ringStep: 56,
        startAngle: 0,
        sweep: 360,
        alignTangent: true,
        scaleDecay: 0.85,
      };
    case "mirror":
      return {
        id,
        type,
        enabled: true,
        axis: "xy", // "x" | "y" | "xy" | "diag"
        pivotX: 256,
        pivotY: 256,
      };
    case "inset-echo":
      return {
        id,
        type,
        enabled: true,
        copies: 4,
        stepPx: 26,
        alternateColor: true,
        colorB: "#1e2228",
        opacityDecay: 0.12,
        heightDecay: 0.15,
      };
    case "noise-jitter":
      return {
        id,
        type,
        enabled: true,
        posJitter: 12,
        rotJitter: 35,
        scaleJitter: 0.2,
        dropout: 0.0,
        seed: 404,
      };
    case "warp":
      return {
        id,
        type,
        enabled: true,
        warpType: "wave", // "wave" | "swirl" | "curl" | "pinch"
        amplitude: 22,
        frequency: 2,
        seed: 901,
      };
    case "edge-wear":
      return {
        id,
        type,
        enabled: true,
        outlineWidth: 0,
        outlineColor: "#141619",
        chipAmount: 0.38,
        chipScale: 10,
        bevelHeight: 0.25,
        seed: 707,
      };
    default:
      return createDefaultModifier("noise-scale");
  }
}

export function normalizeSurfaceState(raw) {
  const s = raw && typeof raw === "object" ? { ...raw } : {};
  s.finish = s.finish || "ink";
  s.roughness = clamp(Number(s.roughness ?? 0.55), 0.02, 1);
  s.metalness = clamp(Number(s.metalness ?? 0.0), 0, 1);
  s.height = clamp(Number(s.height ?? 0.18), -1, 1);

  const rawCh = s.channels && typeof s.channels === "object" ? s.channels : null;
  const baseDefaults = createDefaultChannelMap(false, {
    albedo: s.enableAlbedo !== false,
    roughness: s.enableRoughness !== false,
    metalness: s.enableMetalness !== false,
    height: s.enableHeight !== false,
  });
  const channels = {};
  for (const chDef of ALL_PAINT_CHANNELS) {
    const k = chDef.key;
    if (rawCh && Object.hasOwn(rawCh, k)) {
      channels[k] = rawCh[k] === true;
    } else {
      channels[k] = !!baseDefaults[k];
    }
  }
  s.channels = channels;
  s.enableAlbedo = channels.albedo;
  s.enableRoughness = channels.roughness;
  s.enableMetalness = channels.metalness;
  s.enableHeight = channels.height;

  const baseVals = createDefaultExtendedValues();
  const prevVals = s.values && typeof s.values === "object" ? s.values : {};
  const values = { ...baseVals, ...prevVals };
  for (const chDef of ALL_PAINT_CHANNELS) {
    if (!chDef.extended) continue;
    const k = chDef.key;
    if (chDef.isColor) {
      const hex = values[k];
      values[k] = typeof hex === "string" && /^#[0-9a-f]{6}$/i.test(hex) ? hex.toLowerCase() : (chDef.defaultVal || chDef.default);
    } else if (channels[k] && values[k] == null) {
      values[k] = chDef.defaultVal ?? chDef.default ?? 0;
    } else if (values[k] == null) {
      values[k] = baseVals[k] ?? chDef.defaultVal ?? 0;
    } else {
      values[k] = clamp(Number(values[k]), chDef.min ?? 0, chDef.max ?? 1);
    }
  }
  s.values = values;
  if (s._edited && typeof s._edited === "object") s._edited = { ...s._edited };
  return s;
}

export function ensureArtworkLayerSurface(layer) {
  if (!layer || layer.kind === "folder") return null;
  layer.surface = normalizeSurfaceState(layer.surface);
  return layer.surface;
}

export function getArtworkLayerPbrMask(layer) {
  const s = ensureArtworkLayerSurface(layer);
  if (!s) return createDefaultChannelMap(false);
  return { ...s.channels };
}

export function setArtworkLayerPbrMask(layer, nextMap) {
  const s = ensureArtworkLayerSurface(layer);
  if (!s) return createDefaultChannelMap(false);
  const prev = { ...s.channels };
  const normalized = {};
  for (const chDef of ALL_PAINT_CHANNELS) {
    const k = chDef.key;
    const on = nextMap?.[k] === true;
    normalized[k] = on;
    if (on && !prev[k] && chDef.extended) {
      if (!s._edited?.[k]) {
        s.values[k] = chDef.defaultVal ?? chDef.default ?? s.values[k];
      }
    }
  }
  s.channels = normalized;
  s.enableAlbedo = normalized.albedo;
  s.enableRoughness = normalized.roughness;
  s.enableMetalness = normalized.metalness;
  s.enableHeight = normalized.height;
  return { ...s.channels };
}

export function getArtworkChannelValue(layer, key) {
  const s = ensureArtworkLayerSurface(layer);
  if (key === "albedo") return layer?.shape?.color || "#e8c27a";
  if (key === "roughness") return s?.roughness ?? 0.55;
  if (key === "metalness") return s?.metalness ?? 0.0;
  if (key === "height") return s?.height ?? 0.18;
  const chDef = ALL_PAINT_CHANNEL_MAP[key];
  if (!s) return chDef?.defaultVal ?? chDef?.default ?? 0;
  return s.values?.[key] ?? chDef?.defaultVal ?? chDef?.default ?? 0;
}

export function setArtworkChannelValue(layer, key, val) {
  const s = ensureArtworkLayerSurface(layer);
  if (!layer || !s) return;
  s._edited = s._edited || {};
  if (key === "albedo") {
    const hex = typeof val === "string" && /^#[0-9a-f]{6}$/i.test(val) ? val : "#e8c27a";
    if (!layer.shape) layer.shape = {};
    layer.shape.color = hex;
    if (!Array.isArray(layer.shape.fillStops) || !layer.shape.fillStops.length) {
      layer.shape.fillStops = [{ color: hex, offset: 0 }, { color: layer.shape.colorB || "#5a7692", offset: 1 }];
    } else {
      layer.shape.fillStops[0] = { ...layer.shape.fillStops[0], color: hex };
    }
    s._edited.albedo = true;
    return hex;
  }
  if (key === "roughness") {
    s.roughness = clamp(Number(val ?? 0.55), 0.02, 1);
    s._edited.roughness = true;
    return s.roughness;
  }
  if (key === "metalness") {
    s.metalness = clamp(Number(val ?? 0), 0, 1);
    s._edited.metalness = true;
    return s.metalness;
  }
  if (key === "height") {
    s.height = clamp(Number(val ?? 0), -1, 1);
    s._edited.height = true;
    return s.height;
  }
  const chDef = ALL_PAINT_CHANNEL_MAP[key];
  if (chDef?.isColor || isRgbChannelKey(key)) {
    const hex = typeof val === "string" && /^#[0-9a-f]{6}$/i.test(val) ? val.toLowerCase() : (chDef?.defaultVal || "#ffffff");
    s.values[key] = hex;
    s._edited[key] = true;
    return hex;
  }
  const num = clamp(Number(val ?? chDef?.defaultVal ?? 0), chDef?.min ?? 0, chDef?.max ?? 1);
  s.values[key] = num;
  s._edited[key] = true;
  return num;
}

export function formatArtworkChannelValue(key, val) {
  const chDef = ALL_PAINT_CHANNEL_MAP[key];
  if (chDef?.isColor || isRgbChannelKey(key)) {
    return String(val || "#FFFFFF").toUpperCase();
  }
  const n = Number(val ?? 0);
  if (key === "emissiveIntensity") return `${n.toFixed(2)}×`;
  return n.toFixed(2);
}

export function collectDocumentSurfaceInfo(doc, requestedExtra = []) {
  const layers = doc?.layers || [];
  const folderMap = new Map(layers.filter((l) => l.kind === "folder").map((f) => [f.id, f]));
  const isVisible = (lyr) => {
    if (!lyr.visible) return false;
    if (lyr.parentId && folderMap.has(lyr.parentId)) return isVisible(folderMap.get(lyr.parentId));
    return true;
  };
  const mergedChannels = createDefaultChannelMap(false, {
    albedo: false,
    roughness: false,
    metalness: false,
    height: false,
  });
  const mergedValues = createDefaultExtendedValues();
  let normalSum = 0;
  let normalCount = 0;

  for (const lyr of layers) {
    if (lyr.kind === "folder" || !isVisible(lyr)) continue;
    const s = ensureArtworkLayerSurface(lyr);
    for (const chDef of ALL_PAINT_CHANNELS) {
      const k = chDef.key;
      if (s.channels[k]) {
        mergedChannels[k] = true;
        if (chDef.extended) mergedValues[k] = s.values[k];
      }
    }
    if (s.channels.normal) {
      normalSum += Number(s.values.normal ?? 1.0);
      normalCount++;
    }
  }
  const extSet = new Set(ALL_EXTENDED_PBR_KEYS.filter((k) => mergedChannels[k]));
  for (const rk of requestedExtra || []) {
    if (ALL_EXTENDED_PBR_KEYS.includes(rk)) extSet.add(rk);
  }
  const activeChannels = ALL_PAINT_CHANNELS.map((c) => c.key).filter((k) => mergedChannels[k]);
  return {
    activeExtKeys: [...extSet],
    activeChannels,
    normalGain: normalCount > 0 ? Math.max(0, normalSum / normalCount) : 1.0,
    surfaceSummary: {
      channels: mergedChannels,
      values: mergedValues,
    },
  };
}

export function createArtworkLayer(kind = "motif", overrides = {}) {
  const id = overrides.id || nextArtworkId("lyr");
  const isFolder = kind === "folder";
  return {
    id,
    name: overrides.name || (isFolder ? "Layer Group" : kind === "text" ? "Typography" : kind === "gradient" ? "Gradient Wash" : kind === "image" ? "Image / SVG" : "Motif Shape"),
    kind,
    parentId: overrides.parentId || null,
    visible: overrides.visible !== false,
    locked: overrides.locked === true,
    blend: overrides.blend || "Normal",
    opacity: clamp(overrides.opacity ?? 1, 0, 1),
    transform: {
      x: 256,
      y: 256,
      width: 64,
      height: 64,
      rotation: 0,
      ...(overrides.transform || {}),
    },
    shape: {
      motifType: "circle",
      sides: 6,
      points: 5,
      innerRatio: 0.46,
      cornerRadius: 0.18,
      strokeWidth: 0,
      fillEnabled: true,
      fillRule: "evenodd",
      flipX: false,
      flipY: false,
      fillMode: "solid", // "solid" | "linear" | "radial" | "point"
      color: "#e8c27a",
      colorB: "#5a7692",
      fillStops: Array.isArray(overrides.shape?.fillStops)
        ? structuredClone(overrides.shape.fillStops)
        : [
            { color: overrides.shape?.color || "#e8c27a", offset: 0 },
            { color: overrides.shape?.colorB || "#5a7692", offset: 1 },
          ],
      fillPointGradient: overrides.shape?.fillPointGradient
        ? normalizePointGradient2D(overrides.shape.fillPointGradient, overrides.shape?.color || "#e8c27a", overrides.shape?.colorB || "#5a7692")
        : null,
      gradientAngle: 45,
      strokeColor: "#16181c",
      strokeMode: "solid", // "solid" | "linear" | "radial" | "point"
      strokeColorB: "#eff4cf",
      strokeStops: Array.isArray(overrides.shape?.strokeStops)
        ? structuredClone(overrides.shape.strokeStops)
        : [
            { color: overrides.shape?.strokeColor || "#16181c", offset: 0 },
            { color: overrides.shape?.strokeColorB || "#eff4cf", offset: 1 },
          ],
      strokePointGradient: overrides.shape?.strokePointGradient
        ? normalizePointGradient2D(overrides.shape.strokePointGradient, overrides.shape?.strokeColor || "#16181c", overrides.shape?.strokeColorB || "#eff4cf")
        : null,
      strokeGradientAngle: 45,
      svgPath: "",
      pathNodes: Array.isArray(overrides.shape?.pathNodes) ? structuredClone(overrides.shape.pathNodes) : null,
      closedPath: overrides.shape?.closedPath ?? true,
      curveTension: overrides.shape?.curveTension ?? 0.5,
      ...(overrides.shape || {}),
    },
    text: {
      value: "FRONTIER",
      fontFamily: "DM Sans",
      fontWeight: 600,
      fontSize: 64,
      letterSpacing: 4,
      align: "center",
      ...(overrides.text || {}),
    },
    media: {
      svgMarkup: "",
      imageUrl: "",
      ...(overrides.media || {}),
    },
    surface: (() => {
      const s = {
        finish: "ink",
        roughness: 0.55,
        metalness: 0.0,
        height: 0.18,
        enableAlbedo: true,
        enableRoughness: true,
        enableMetalness: true,
        enableHeight: true,
        ...(overrides.surface || {}),
      };
      return normalizeSurfaceState(s);
    })(),
    mask: {
      enabled: false,
      invert: false,
      mode: "noise", // "noise" | "radial" | "stripe"
      frequency: 4,
      threshold: 0.45,
      softness: 0.25,
      seed: 211,
      ...(overrides.mask || {}),
    },
    modifiers: Array.isArray(overrides.modifiers)
      ? overrides.modifiers.map((m) => structuredClone(m))
      : [],
  };
}

export function createArtworkDocument(mode = "pattern") {
  if (mode === "stamp") {
    return {
      schema: "frontier.artwork.v2",
      mode: "stamp",
      name: "Industrial Warning Seal",
      size: ARTWORK_SIZE,
      background: { enabled: false, color: "#1d2024", opacity: 0, finish: "ink", roughness: 0.8, metalness: 0, height: 0 },
      tiling: { mode: "straight", repeats: 1, showGrid: false },
      layers: [
        createArtworkLayer("motif", {
          name: "Outer Octagon Ring",
          transform: { x: 256, y: 256, width: 380, height: 380, rotation: 0 },
          shape: { motifType: "hexagon", color: "#e8c27a", fillMode: "solid", strokeWidth: 0 },
          surface: { finish: "foil", roughness: 0.28, metalness: 0.85, height: 0.35, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
          modifiers: [
            { ...createDefaultModifier("inset-echo"), copies: 2, stepPx: 44, alternateColor: true, colorB: "#181b1f" },
            { ...createDefaultModifier("edge-wear"), chipAmount: 0.32, chipScale: 12, bevelHeight: 0.22 },
          ],
        }),
        createArtworkLayer("motif", {
          name: "Perimeter Rivets",
          transform: { x: 256, y: 256, width: 18, height: 18, rotation: 0 },
          shape: { motifType: "circle", color: "#cfd6df" },
          surface: { finish: "foil", roughness: 0.22, metalness: 1.0, height: 0.52, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
          modifiers: [
            { ...createDefaultModifier("array-radial"), count: 12, radius: 158, rings: 1, alignTangent: true },
          ],
        }),
        createArtworkLayer("text", {
          name: "Stencil Mark",
          transform: { x: 256, y: 256, width: 260, height: 90, rotation: 0 },
          text: { value: "UNIT 04", fontFamily: "DM Sans", fontWeight: 600, fontSize: 54, letterSpacing: 6, align: "center" },
          shape: { color: "#eff4cf" },
          surface: { finish: "ink", roughness: 0.45, metalness: 0.1, height: 0.24, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
          modifiers: [
            { ...createDefaultModifier("edge-wear"), chipAmount: 0.22, chipScale: 14, bevelHeight: 0.15 },
          ],
        }),
      ],
    };
  }

  return {
    schema: "frontier.artwork.v2",
    mode: "pattern",
    name: "Noise-Scaled Hex Matrix",
    size: ARTWORK_SIZE,
    background: { enabled: true, color: "#171a1f", colorB: "#0b0b0b", opacity: 1, finish: "cotton", roughness: 0.85, metalness: 0.0, height: 0.0 },
    tiling: { mode: "straight", repeats: 2, showGrid: false },
    layers: [
      createArtworkLayer("motif", {
        name: "Noise-Scaled Dots",
        transform: { x: 256, y: 256, width: 42, height: 42, rotation: 0 },
        shape: { motifType: "hexagon", color: "#e8c27a", colorB: "#7cb4d8" },
        surface: { finish: "ceramic", roughness: 0.24, metalness: 0.15, height: 0.36, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
        modifiers: [
          { ...createDefaultModifier("array-grid"), layout: "hex", columns: 10, rows: 10, spanX: 512, spanY: 512, center: true },
          { ...createDefaultModifier("noise-scale"), noiseType: "fbm", target: "scale-color", minScale: 0.22, maxScale: 1.55, frequency: 3, octaves: 3, colorB: "#76b2d8", seamless: true },
        ],
      }),
      createArtworkLayer("motif", {
        name: "Fine Accent Rings",
        transform: { x: 256, y: 256, width: 16, height: 16, rotation: 0 },
        shape: { motifType: "ring", color: "#eff4cf" },
        surface: { finish: "foil", roughness: 0.26, metalness: 0.92, height: 0.45, enableAlbedo: true, enableRoughness: true, enableMetalness: true, enableHeight: true },
        opacity: 0.72,
        modifiers: [
          { ...createDefaultModifier("array-grid"), layout: "grid", columns: 5, rows: 5, spanX: 512, spanY: 512, center: true },
          { ...createDefaultModifier("noise-scale"), noiseType: "worley", target: "scale-opacity", minScale: 0.4, maxScale: 1.6, frequency: 2, seamless: true },
        ],
      }),
    ],
  };
}

export function normalizeGradientStops(stops, colorA = "#e8c27a", colorB = "#5a7692") {
  if (!Array.isArray(stops) || stops.length < 2) {
    return [
      { color: colorA || "#e8c27a", offset: 0 },
      { color: colorB || "#5a7692", offset: 1 },
    ];
  }
  return stops
    .slice(0, 12)
    .map((s, idx, arr) => ({
      color: s?.color || (idx === 0 ? colorA : colorB) || "#e8c27a",
      offset: clamp(Number(s?.offset ?? (arr.length > 1 ? idx / (arr.length - 1) : 0)), 0, 1),
    }))
    .sort((a, b) => a.offset - b.offset);
}

/**
 * Centripetal Catmull-Rom cubic Bezier tangent fitting (matching AlloyStudio pathEditorGeometry).
 * Fits smooth `in`/`out` tangent handles on `"auto"` nodes while respecting manually sculpted handles.
 */
export function automaticPathNodes(nodes, closed = false, tension = 0.5) {
  const list = Array.isArray(nodes) ? nodes.map((n) => ({
    x: Number(n.x || 0),
    y: Number(n.y || 0),
    mode: n.mode || "auto",
    in: n.in ? { x: Number(n.in.x), y: Number(n.in.y) } : null,
    out: n.out ? { x: Number(n.out.x), y: Number(n.out.y) } : null,
  })) : [];
  const count = list.length;
  if (count < 2) return list;
  const weight = clamp(Number(tension ?? 0.5), 0.05, 1.0);
  const scale = weight / 3;
  const dist = (a, b) => Math.hypot(b.x - a.x, b.y - a.y);

  for (let i = 0; i < count; i++) {
    const cur = list[i];
    if (cur.mode !== "auto") continue;
    const hasPrev = closed || i > 0;
    const hasNext = closed || i < count - 1;
    if (!hasPrev && !hasNext) {
      cur.in = null;
      cur.out = null;
      continue;
    }
    const prev = list[(i - 1 + count) % count];
    const next = list[(i + 1) % count];
    if (!hasPrev && hasNext) {
      cur.in = null;
      cur.out = { x: (next.x - cur.x) * (scale * 0.6), y: (next.y - cur.y) * (scale * 0.6) };
      continue;
    }
    if (hasPrev && !hasNext) {
      cur.in = { x: (prev.x - cur.x) * (scale * 0.6), y: (prev.y - cur.y) * (scale * 0.6) };
      cur.out = null;
      continue;
    }
    const d1 = Math.max(0.001, Math.pow(dist(prev, cur), 0.5));
    const d2 = Math.max(0.001, Math.pow(dist(cur, next), 0.5));
    const dx = next.x - prev.x;
    const dy = next.y - prev.y;
    const len = Math.hypot(dx, dy);
    if (len < 1e-4) {
      cur.in = null;
      cur.out = null;
      continue;
    }
    const tx = dx / len;
    const ty = dy / len;
    const dPrev = dist(prev, cur);
    const dNext = dist(cur, next);
    const handleInLen = dPrev * scale * (d2 / (d1 + d2)) * 1.4;
    const handleOutLen = dNext * scale * (d1 / (d1 + d2)) * 1.4;
    cur.in = { x: -tx * handleInLen, y: -ty * handleInLen };
    cur.out = { x: tx * handleOutLen, y: ty * handleOutLen };
  }
  return list;
}

export function pathNodesToSvgD(nodes, closed = false, tension = 0.5) {
  const fitted = automaticPathNodes(nodes, closed, tension);
  if (fitted.length === 0) return "";
  const f = (n) => Number(n.toFixed(2));
  let d = `M ${f(fitted[0].x)} ${f(fitted[0].y)}`;
  const segCount = closed ? fitted.length : fitted.length - 1;
  for (let i = 0; i < segCount; i++) {
    const a = fitted[i];
    const b = fitted[(i + 1) % fitted.length];
    const c1x = a.x + (a.out?.x || 0);
    const c1y = a.y + (a.out?.y || 0);
    const c2x = b.x + (b.in?.x || 0);
    const c2y = b.y + (b.in?.y || 0);
    if (!a.out && !b.in) {
      d += ` L ${f(b.x)} ${f(b.y)}`;
    } else {
      d += ` C ${f(c1x)} ${f(c1y)}, ${f(c2x)} ${f(c2y)}, ${f(b.x)} ${f(b.y)}`;
    }
  }
  if (closed && fitted.length > 2) d += " Z";
  return d;
}

export function nodesToNormalizedCurveData(canvasNodes, closed = false, tension = 0.5) {
  const fitted = automaticPathNodes(canvasNodes, closed, tension);
  if (fitted.length < 2) return null;
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const n of fitted) {
    minX = Math.min(minX, n.x, n.x + (n.in?.x || 0) * 0.5, n.x + (n.out?.x || 0) * 0.5);
    maxX = Math.max(maxX, n.x, n.x + (n.in?.x || 0) * 0.5, n.x + (n.out?.x || 0) * 0.5);
    minY = Math.min(minY, n.y, n.y + (n.in?.y || 0) * 0.5, n.y + (n.out?.y || 0) * 0.5);
    maxY = Math.max(maxY, n.y, n.y + (n.in?.y || 0) * 0.5, n.y + (n.out?.y || 0) * 0.5);
  }
  const w = Math.max(12, maxX - minX);
  const h = Math.max(12, maxY - minY);
  const cx = (minX + maxX) * 0.5;
  const cy = (minY + maxY) * 0.5;
  const x0 = cx - w * 0.5;
  const y0 = cy - h * 0.5;
  const normNodes = fitted.map((n) => ({
    x: ((n.x - x0) / w) * 100,
    y: ((n.y - y0) / h) * 100,
    mode: n.mode || "auto",
    in: n.in ? { x: (n.in.x / w) * 100, y: (n.in.y / h) * 100 } : null,
    out: n.out ? { x: (n.out.x / w) * 100, y: (n.out.y / h) * 100 } : null,
  }));
  return {
    transform: { x: cx, y: cy, width: w, height: h, rotation: 0 },
    pathNodes: normNodes,
    closedPath: !!closed,
    curveTension: clamp(Number(tension ?? 0.5), 0.05, 1),
    svgPath: pathNodesToSvgD(normNodes, closed, tension),
  };
}

export function normalizedCurveToCanvasNodes(pathNodes, transform) {
  if (!Array.isArray(pathNodes) || !transform) return [];
  const w = Math.max(12, transform.width || 100);
  const h = Math.max(12, transform.height || 100);
  const x0 = (transform.x || 256) - w * 0.5;
  const y0 = (transform.y || 256) - h * 0.5;
  const rot = ((transform.rotation || 0) * Math.PI) / 180;
  const cos = Math.cos(rot), sin = Math.sin(rot);
  const rotVec = (dx, dy) => ({ x: dx * cos - dy * sin, y: dx * sin + dy * cos });
  return pathNodes.map((n) => {
    const lx = (n.x / 100) * w - w * 0.5;
    const ly = (n.y / 100) * h - h * 0.5;
    const rp = rotVec(lx, ly);
    const ri = n.in ? rotVec((n.in.x / 100) * w, (n.in.y / 100) * h) : null;
    const ro = n.out ? rotVec((n.out.x / 100) * w, (n.out.y / 100) * h) : null;
    return {
      x: (transform.x || 256) + rp.x,
      y: (transform.y || 256) + rp.y,
      mode: n.mode || "auto",
      in: ri,
      out: ro,
    };
  });
}

/**
 * Evaluate a single layer's non-destructive modifier stack into concrete draw instances.
 * Each instance tracks { x, y, width, height, rotation, color, colorB, fillMode, fillStops, gradientAngle, opacity, heightValue, roughness, metalness, outlineWidth, outlineColor, strokeMode, strokeStops, strokeGradientAngle, chipAmount, chipScale, seed, motifType, innerRatio, cornerRadius, sides, points, strokeWidth, strokeColor, svgPath, text }.
 */
export function evaluateLayerInstances(layer, docSize = ARTWORK_SIZE, mode = "pattern") {
  const seamless = mode === "pattern";
  const fillStops = normalizeGradientStops(layer.shape.fillStops, layer.shape.color, layer.shape.colorB);
  const strokeStops = normalizeGradientStops(layer.shape.strokeStops, layer.shape.strokeColor, layer.shape.strokeColorB);
  const baseInstance = {
    x: layer.transform.x,
    y: layer.transform.y,
    width: layer.transform.width,
    height: layer.transform.height,
    rotation: layer.transform.rotation || 0,
    color: layer.shape.fillMode && layer.shape.fillMode !== "solid" ? fillStops[0].color : (layer.shape.color || "#e8c27a"),
    colorB: layer.shape.fillMode && layer.shape.fillMode !== "solid" ? fillStops[fillStops.length - 1].color : (layer.shape.colorB || "#5a7692"),
    fillMode: layer.shape.fillMode || "solid",
    fillStops,
    fillPointGradient: layer.shape.fillPointGradient || null,
    gradientAngle: layer.shape.gradientAngle ?? 45,
    opacity: 1,
    heightValue: layer.surface.height ?? 0.2,
    roughness: layer.surface.roughness ?? 0.5,
    metalness: layer.surface.metalness ?? 0.0,
    outlineWidth: layer.shape.strokeWidth || 0,
    outlineColor: layer.shape.strokeMode && layer.shape.strokeMode !== "solid" ? strokeStops[0].color : (layer.shape.strokeColor || "#16181c"),
    strokeMode: layer.shape.strokeMode || "solid",
    strokeStops,
    strokePointGradient: layer.shape.strokePointGradient || null,
    strokeGradientAngle: layer.shape.strokeGradientAngle ?? 45,
    fillEnabled: layer.shape.fillEnabled !== false,
    fillRule: layer.shape.fillRule || "evenodd",
    flipX: !!layer.shape.flipX,
    flipY: !!layer.shape.flipY,
    chipAmount: 0,
    chipScale: 10,
    chipSeed: 707,
    motifType: layer.shape.motifType || "circle",
    sides: layer.shape.sides || 6,
    points: layer.shape.points || 5,
    innerRatio: layer.shape.innerRatio ?? 0.46,
    cornerRadius: layer.shape.cornerRadius ?? 0.18,
    svgPath: layer.shape.svgPath || "",
    text: layer.kind === "text" ? { ...layer.text } : null,
  };

  let instances = [baseInstance];
  const modifiers = layer.modifiers || [];

  for (const mod of modifiers) {
    if (!mod || mod.enabled === false) continue;

    if (mod.type === "array-grid") {
      const cols = Math.max(1, Math.min(28, Math.round(mod.columns || 6)));
      const rows = Math.max(1, Math.min(28, Math.round(mod.rows || 6)));
      const spanX = Number(mod.spanX ?? docSize);
      const spanY = Number(mod.spanY ?? docSize);
      const stepX = spanX / cols;
      const stepY = spanY / rows;
      const layout = mod.layout || "grid";
      const next = [];
      for (const inst of instances) {
        const originX = mod.center !== false ? (docSize - spanX) * 0.5 + stepX * 0.5 : inst.x;
        const originY = mod.center !== false ? (docSize - spanY) * 0.5 + stepY * 0.5 : inst.y;
        for (let r = 0; r < rows; r++) {
          for (let c = 0; c < cols; c++) {
            let px = originX + c * stepX;
            let py = originY + r * stepY;
            if (layout === "brick" || layout === "hex") {
              if (r % 2 === 1) px += stepX * 0.5;
            } else if (layout === "half-drop") {
              if (c % 2 === 1) py += stepY * 0.5;
            }
            if (seamless) {
              px = ((px % docSize) + docSize) % docSize;
              py = ((py % docSize) + docSize) % docSize;
            }
            const alt = (r + c) % 2 === 1;
            next.push({
              ...inst,
              x: px,
              y: py,
              color: mod.alternateColor && alt ? (mod.colorB || inst.color) : inst.color,
              rotation: inst.rotation + (mod.alternateRotate && alt ? Number(mod.alternateRotate) : 0),
            });
          }
        }
      }
      instances = next;
    } else if (mod.type === "array-radial") {
      const count = Math.max(2, Math.min(48, Math.round(mod.count || 8)));
      const rings = Math.max(1, Math.min(6, Math.round(mod.rings || 1)));
      const baseR = Number(mod.radius ?? 120);
      const ringStep = Number(mod.ringStep ?? 48);
      const startDeg = Number(mod.startAngle ?? 0);
      const sweepDeg = Number(mod.sweep ?? 360);
      const decay = Number(mod.scaleDecay ?? 0.9);
      const next = [];
      for (const inst of instances) {
        for (let ring = 0; ring < rings; ring++) {
          const r = baseR + ring * ringStep;
          const rScale = Math.pow(decay, ring);
          const denom = sweepDeg >= 359.5 ? count : Math.max(1, count - 1);
          for (let k = 0; k < count; k++) {
            const angDeg = startDeg + (sweepDeg * k) / denom;
            const rad = (angDeg * Math.PI) / 180;
            next.push({
              ...inst,
              x: inst.x + Math.cos(rad) * r,
              y: inst.y + Math.sin(rad) * r,
              width: inst.width * rScale,
              height: inst.height * rScale,
              rotation: inst.rotation + (mod.alignTangent !== false ? angDeg + 90 : 0),
            });
          }
        }
      }
      instances = next;
    } else if (mod.type === "mirror") {
      const axis = mod.axis || "xy";
      const px = Number(mod.pivotX ?? docSize * 0.5);
      const py = Number(mod.pivotY ?? docSize * 0.5);
      const next = [];
      for (const inst of instances) {
        next.push(inst);
        if (axis === "x" || axis === "xy") {
          next.push({ ...inst, x: 2 * px - inst.x, rotation: -inst.rotation });
        }
        if (axis === "y" || axis === "xy") {
          next.push({ ...inst, y: 2 * py - inst.y, rotation: -inst.rotation });
        }
        if (axis === "xy") {
          next.push({ ...inst, x: 2 * px - inst.x, y: 2 * py - inst.y, rotation: inst.rotation + 180 });
        }
        if (axis === "diag") {
          const dx = inst.x - px;
          const dy = inst.y - py;
          next.push({ ...inst, x: px + dy, y: py + dx, rotation: 90 - inst.rotation });
        }
      }
      instances = next;
    } else if (mod.type === "inset-echo") {
      const copies = Math.max(2, Math.min(16, Math.round(mod.copies || 4)));
      const step = Number(mod.stepPx ?? 24);
      const opDecay = clamp(Number(mod.opacityDecay ?? 0.1), 0, 0.9);
      const hDecay = clamp(Number(mod.heightDecay ?? 0.12), 0, 0.9);
      const next = [];
      for (const inst of instances) {
        for (let i = 0; i < copies; i++) {
          const w = Math.max(2, inst.width - step * i);
          const h = Math.max(2, inst.height - step * i);
          next.push({
            ...inst,
            width: w,
            height: h,
            color: mod.alternateColor && i % 2 === 1 ? (mod.colorB || "#1d2025") : inst.color,
            opacity: clamp(inst.opacity * Math.pow(1 - opDecay, i), 0.05, 1),
            heightValue: clamp(inst.heightValue * Math.pow(1 - hDecay, i), -1, 1),
          });
        }
      }
      instances = next;
    } else if (mod.type === "noise-scale") {
      const minS = Number(mod.minScale ?? 0.25);
      const maxS = Number(mod.maxScale ?? 1.85);
      const target = mod.target || "uniform";
      const dropThresh = clamp(Number(mod.dropoutThreshold ?? 0), 0, 0.95);
      const next = [];
      for (const inst of instances) {
        const u = ((inst.x / docSize) % 1 + 1) % 1;
        const v = ((inst.y / docSize) % 1 + 1) % 1;
        const n = evaluateTileNoise(u, v, {
          noiseType: mod.noiseType || "fbm",
          frequency: mod.frequency ?? 3,
          octaves: mod.octaves ?? 3,
          seed: mod.seed ?? 1337,
          contrast: mod.contrast ?? 1.0,
          bias: mod.bias ?? 0.0,
          curve: mod.curve || "smoothstep",
          invert: !!mod.invert,
          stretchX: mod.stretchX ?? 1,
          stretchY: mod.stretchY ?? 1,
          offsetX: mod.offsetX ?? 0,
          offsetY: mod.offsetY ?? 0,
          seamless: mod.seamless !== false && seamless,
        });
        if (dropThresh > 0 && n < dropThresh) continue;
        const factor = minS + (maxS - minS) * n;
        const out = { ...inst };
        if (target === "width") {
          out.width = Math.max(1, inst.width * factor);
        } else if (target === "height") {
          out.height = Math.max(1, inst.height * factor);
        } else {
          out.width = Math.max(1, inst.width * factor);
          out.height = Math.max(1, inst.height * factor);
        }
        if (target === "scale-opacity") {
          out.opacity = clamp(inst.opacity * (0.15 + 0.85 * n), 0.05, 1);
        }
        if (target === "scale-color") {
          out.color = lerpHex(inst.color, mod.colorB || "#7aa6d9", n);
        }
        next.push(out);
      }
      instances = next;
    } else if (mod.type === "noise-jitter") {
      const pJ = Number(mod.posJitter ?? 12);
      const rJ = Number(mod.rotJitter ?? 35);
      const sJ = Number(mod.scaleJitter ?? 0.2);
      const drop = clamp(Number(mod.dropout ?? 0), 0, 0.95);
      const seed = Number(mod.seed ?? 404);
      const next = [];
      instances.forEach((inst, idx) => {
        if (drop > 0 && detHash(idx, 11, seed) < drop) return;
        const jx = (detHash(idx, 1, seed) - 0.5) * 2 * pJ;
        const jy = (detHash(idx, 2, seed) - 0.5) * 2 * pJ;
        const jr = (detHash(idx, 3, seed) - 0.5) * 2 * rJ;
        const js = 1 + (detHash(idx, 4, seed) - 0.5) * 2 * sJ;
        next.push({
          ...inst,
          x: seamless ? (((inst.x + jx) % docSize) + docSize) % docSize : inst.x + jx,
          y: seamless ? (((inst.y + jy) % docSize) + docSize) % docSize : inst.y + jy,
          rotation: inst.rotation + jr,
          width: Math.max(1, inst.width * js),
          height: Math.max(1, inst.height * js),
        });
      });
      instances = next;
    } else if (mod.type === "warp") {
      const amp = Number(mod.amplitude ?? 22);
      const freq = Math.max(1, Number(mod.frequency ?? 2));
      const wType = mod.warpType || "wave";
      const seed = Number(mod.seed ?? 901);
      instances = instances.map((inst) => {
        const u = inst.x / docSize;
        const v = inst.y / docSize;
        let dx = 0, dy = 0, drot = 0;
        if (wType === "wave") {
          dx = Math.sin(v * Math.PI * 2 * freq) * amp;
          dy = Math.cos(u * Math.PI * 2 * freq) * amp * 0.5;
        } else if (wType === "swirl") {
          const cx = inst.x - docSize * 0.5;
          const cy = inst.y - docSize * 0.5;
          const dist = Math.hypot(cx, cy) / (docSize * 0.5);
          const ang = (1 - clamp(dist, 0, 1)) * (amp * 0.04);
          const c = Math.cos(ang), s = Math.sin(ang);
          return {
            ...inst,
            x: docSize * 0.5 + cx * c - cy * s,
            y: docSize * 0.5 + cx * s + cy * c,
            rotation: inst.rotation + (ang * 180) / Math.PI,
          };
        } else if (wType === "curl") {
          const n1 = evaluateTileNoise(u, v, { noiseType: "fbm", frequency: freq, seed, seamless });
          const n2 = evaluateTileNoise(u + 0.17, v + 0.31, { noiseType: "fbm", frequency: freq, seed: seed + 77, seamless });
          dx = (n1 - 0.5) * 2 * amp;
          dy = (n2 - 0.5) * 2 * amp;
        } else {
          const cx = inst.x - docSize * 0.5;
          const cy = inst.y - docSize * 0.5;
          const len = Math.max(1, Math.hypot(cx, cy));
          const push = (1 - clamp(len / (docSize * 0.5), 0, 1)) * amp;
          dx = (cx / len) * push;
          dy = (cy / len) * push;
        }
        return {
          ...inst,
          x: seamless ? (((inst.x + dx) % docSize) + docSize) % docSize : inst.x + dx,
          y: seamless ? (((inst.y + dy) % docSize) + docSize) % docSize : inst.y + dy,
          rotation: inst.rotation + drot,
        };
      });
    } else if (mod.type === "edge-wear") {
      instances = instances.map((inst) => ({
        ...inst,
        outlineWidth: Math.max(inst.outlineWidth, Number(mod.outlineWidth || 0)),
        outlineColor: mod.outlineColor || inst.outlineColor,
        chipAmount: Math.max(inst.chipAmount, Number(mod.chipAmount || 0)),
        chipScale: Number(mod.chipScale || 10),
        chipSeed: Number(mod.seed || 707),
        heightValue: clamp(inst.heightValue + Number(mod.bevelHeight || 0), -1, 1),
      }));
    }
  }
  return instances;
}

function drawMotifPath(ctx, inst) {
  const w = inst.width;
  const h = inst.height;
  const hw = w * 0.5;
  const hh = h * 0.5;
  const t = inst.motifType || "circle";

  ctx.beginPath();
  if (t === "circle") {
    ctx.ellipse(0, 0, hw, hh, 0, 0, Math.PI * 2);
  } else if (t === "ring") {
    const inner = clamp(inst.innerRatio ?? 0.5, 0.1, 0.92);
    ctx.ellipse(0, 0, hw, hh, 0, 0, Math.PI * 2);
    ctx.moveTo(hw * inner, 0);
    ctx.ellipse(0, 0, hw * inner, hh * inner, 0, 0, Math.PI * 2, true);
  } else if (t === "square") {
    ctx.rect(-hw, -hh, w, h);
  } else if (t === "roundedRect") {
    const r = Math.min(hw, hh) * clamp(inst.cornerRadius ?? 0.22, 0, 1);
    ctx.roundRect(-hw, -hh, w, h, r);
  } else if (t === "diamond") {
    ctx.moveTo(0, -hh);
    ctx.lineTo(hw, 0);
    ctx.lineTo(0, hh);
    ctx.lineTo(-hw, 0);
    ctx.closePath();
  } else if (t === "triangle") {
    ctx.moveTo(0, -hh);
    ctx.lineTo(hw, hh);
    ctx.lineTo(-hw, hh);
    ctx.closePath();
  } else if (t === "hexagon") {
    const sides = Math.max(3, Math.min(12, Math.round(inst.sides || 6)));
    for (let i = 0; i < sides; i++) {
      const a = (i / sides) * Math.PI * 2 - Math.PI * 0.5;
      const x = Math.cos(a) * hw;
      const y = Math.sin(a) * hh;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.closePath();
  } else if (t === "star") {
    const pts = Math.max(3, Math.min(16, Math.round(inst.points || 5)));
    const inner = clamp(inst.innerRatio ?? 0.45, 0.15, 0.85);
    for (let i = 0; i < pts * 2; i++) {
      const a = (i / (pts * 2)) * Math.PI * 2 - Math.PI * 0.5;
      const r = i % 2 === 0 ? 1 : inner;
      const x = Math.cos(a) * hw * r;
      const y = Math.sin(a) * hh * r;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.closePath();
  } else if (t === "cross") {
    const arm = clamp(inst.innerRatio ?? 0.34, 0.1, 0.8);
    const aw = hw * arm, ah = hh * arm;
    ctx.moveTo(-aw, -hh);
    ctx.lineTo(aw, -hh);
    ctx.lineTo(aw, -ah);
    ctx.lineTo(hw, -ah);
    ctx.lineTo(hw, ah);
    ctx.lineTo(aw, ah);
    ctx.lineTo(aw, hh);
    ctx.lineTo(-aw, hh);
    ctx.lineTo(-aw, ah);
    ctx.lineTo(-hw, ah);
    ctx.lineTo(-hw, -ah);
    ctx.lineTo(-aw, -ah);
    ctx.closePath();
  } else if (t === "chevron") {
    const th = hh * 0.48;
    ctx.moveTo(-hw, th);
    ctx.lineTo(0, -hh);
    ctx.lineTo(hw, th);
    ctx.lineTo(hw, hh);
    ctx.lineTo(0, -hh + (hh * 2 - th));
    ctx.lineTo(-hw, hh);
    ctx.closePath();
  } else if (t === "stripe") {
    ctx.rect(-hw, -hh * 0.28, w, h * 0.56);
  } else if (t === "arc") {
    const inner = clamp(inst.innerRatio ?? 0.55, 0.2, 0.85);
    ctx.arc(0, 0, hw, Math.PI, 0, false);
    ctx.arc(0, 0, hw * inner, 0, Math.PI, true);
    ctx.closePath();
  } else if (t === "teardrop") {
    ctx.moveTo(0, -hh);
    ctx.bezierCurveTo(hw * 0.85, -hh * 0.15, hw, hh * 0.55, 0, hh);
    ctx.bezierCurveTo(-hw, hh * 0.55, -hw * 0.85, -hh * 0.15, 0, -hh);
    ctx.closePath();
  } else if (t === "leaf") {
    ctx.moveTo(0, -hh);
    ctx.quadraticCurveTo(hw, -hh * 0.2, 0, hh);
    ctx.quadraticCurveTo(-hw, -hh * 0.2, 0, -hh);
    ctx.closePath();
  } else if (t === "flower") {
    for (let p = 0; p < 5; p++) {
      const a = (p * 72 * Math.PI) / 180;
      const cx = Math.sin(a) * hh * 0.46;
      const cy = -Math.cos(a) * hh * 0.46;
      ctx.ellipse(cx, cy, hw * 0.36, hh * 0.54, a, 0, Math.PI * 2);
      ctx.moveTo(0, 0);
    }
  } else if (t === "line") {
    ctx.moveTo(-hw, 0);
    ctx.lineTo(hw, 0);
  } else if (t === "gear") {
    const teeth = Math.max(6, Math.min(20, Math.round(inst.points || 8)));
    const innerR = hw * clamp(inst.innerRatio ?? 0.72, 0.45, 0.9);
    const holeR = hw * 0.32;
    for (let i = 0; i < teeth * 2; i++) {
      const a0 = (i / (teeth * 2)) * Math.PI * 2;
      const a1 = ((i + 0.65) / (teeth * 2)) * Math.PI * 2;
      const r = i % 2 === 0 ? hw : innerR;
      const x0 = Math.cos(a0) * r, y0 = Math.sin(a0) * r;
      const x1 = Math.cos(a1) * r, y1 = Math.sin(a1) * r;
      if (i === 0) ctx.moveTo(x0, y0);
      else ctx.lineTo(x0, y0);
      ctx.lineTo(x1, y1);
    }
    ctx.closePath();
    ctx.moveTo(holeR, 0);
    ctx.arc(0, 0, holeR, 0, Math.PI * 2, true);
  } else {
    ctx.ellipse(0, 0, hw, hh, 0, 0, Math.PI * 2);
  }
}

function resolveCanvasGradient(ctx, mode, stops, angle, w, h, fallbackColor, fallbackColorB, cx = 0, cy = 0) {
  const norm = normalizeGradientStops(stops, fallbackColor, fallbackColorB);
  if (mode === "radial") {
    const grad = ctx.createRadialGradient(cx, cy, 0, cx, cy, Math.max(w, h) * 0.5);
    for (const s of norm) grad.addColorStop(s.offset, s.color);
    return grad;
  }
  if (mode === "linear") {
    const rad = ((angle ?? 45) * Math.PI) / 180;
    const dx = Math.cos(rad) * w * 0.5;
    const dy = Math.sin(rad) * h * 0.5;
    const grad = ctx.createLinearGradient(cx - dx, cy - dy, cx + dx, cy + dy);
    for (const s of norm) grad.addColorStop(s.offset, s.color);
    return grad;
  }
  return fallbackColor;
}

function resolveFillStyle(ctx, inst, overrideColor) {
  if (overrideColor) return overrideColor;
  if (inst.fillMode === "radial" || inst.fillMode === "linear") {
    return resolveCanvasGradient(
      ctx,
      inst.fillMode,
      inst.fillStops,
      inst.gradientAngle,
      inst.width,
      inst.height,
      inst.color,
      inst.colorB || "#1a1e24",
    );
  }
  return inst.color;
}

function resolveStrokeStyle(ctx, inst, isAlbedo) {
  if (!isAlbedo) return inst.color;
  if (inst.strokeMode === "radial" || inst.strokeMode === "linear") {
    return resolveCanvasGradient(
      ctx,
      inst.strokeMode,
      inst.strokeStops,
      inst.strokeGradientAngle,
      inst.width,
      inst.height,
      inst.outlineColor || "#141619",
      inst.strokeStops?.[inst.strokeStops.length - 1]?.color || "#eff4cf",
    );
  }
  return inst.outlineColor || "#141619";
}

function canvasBlendOp(blend) {
  switch (blend) {
    case "Multiply": return "multiply";
    case "Screen": return "screen";
    case "Overlay": return "overlay";
    case "Add": return "lighter";
    case "Darken": return "darken";
    default: return "source-over";
  }
}

function makeScratchCanvas(size) {
  const c = typeof OffscreenCanvas !== "undefined" ? new OffscreenCanvas(size, size) : document.createElement("canvas");
  c.width = size;
  c.height = size;
  return c;
}

/**
 * Render an entire layer stack + modifier stack + masks + tiling into 4 PBR canvases:
 * { albedo, roughness, metalness, height, normalData } (and single-layer preview if desired).
 */
export function renderArtworkDocument(doc, targetSize = 512, { tileMode = "single", channel = "composite", extraChannels = [] } = {}) {
  const size = doc.size || ARTWORK_SIZE;
  const scale = targetSize / size;
  const surfInfo = collectDocumentSurfaceInfo(doc, extraChannels);
  const activeExtKeys = surfInfo.activeExtKeys;

  const albedo = makeScratchCanvas(targetSize);
  const roughness = makeScratchCanvas(targetSize);
  const metalness = makeScratchCanvas(targetSize);
  const height = makeScratchCanvas(targetSize);
  const coverageCanvas = makeScratchCanvas(targetSize);

  const aCtx = albedo.getContext("2d", { willReadFrequently: true });
  const rCtx = roughness.getContext("2d", { willReadFrequently: true });
  const mCtx = metalness.getContext("2d", { willReadFrequently: true });
  const hCtx = height.getContext("2d", { willReadFrequently: true });
  const covCtx = coverageCanvas.getContext("2d", { willReadFrequently: true });

  const extTargets = {};
  const extScratches = {};
  const extCtxs = {};
  const extScratchCtxs = {};
  for (const k of activeExtKeys) {
    extTargets[k] = makeScratchCanvas(targetSize);
    extScratches[k] = makeScratchCanvas(targetSize);
    extCtxs[k] = extTargets[k].getContext("2d", { willReadFrequently: true });
    extScratchCtxs[k] = extScratches[k].getContext("2d", { willReadFrequently: true });
  }

  const bg = doc.background || {};
  if (bg.enabled) {
    aCtx.save();
    aCtx.globalAlpha = clamp(bg.opacity ?? 1, 0, 1);
    if (bg.fillMode === "point") {
      const ptRes = Math.min(256, Math.max(64, Math.round(targetSize * 0.5)));
      const { canvas: pgCanvas } = getPointGradientCanvasAndUrl(
        bg.fillPointGradient,
        { res: ptRes, size, seamless: doc.mode === "pattern" },
      );
      if (pgCanvas) {
        aCtx.drawImage(pgCanvas, 0, 0, targetSize, targetSize);
      } else {
        aCtx.fillStyle = bg.fillPointGradient?.background || bg.color || "#171a1f";
        aCtx.fillRect(0, 0, targetSize, targetSize);
      }
    } else if (bg.fillMode === "linear" || bg.fillMode === "radial") {
      aCtx.translate(targetSize * 0.5, targetSize * 0.5);
      aCtx.fillStyle = resolveCanvasGradient(
        aCtx,
        bg.fillMode,
        bg.fillStops,
        bg.gradientAngle ?? 45,
        targetSize,
        targetSize,
        bg.color || "#171a1f",
        bg.colorB || "#2b3340",
      );
      aCtx.fillRect(-targetSize * 0.5, -targetSize * 0.5, targetSize, targetSize);
    } else {
      aCtx.fillStyle = bg.color || "#171a1f";
      aCtx.fillRect(0, 0, targetSize, targetSize);
    }
    aCtx.restore();

    covCtx.save();
    covCtx.globalAlpha = clamp(bg.opacity ?? 1, 0, 1);
    covCtx.fillStyle = "#ffffff";
    covCtx.fillRect(0, 0, targetSize, targetSize);
    covCtx.restore();

    const rV = Math.round(clamp(bg.roughness ?? 0.85, 0, 1) * 255);
    const mV = Math.round(clamp(bg.metalness ?? 0, 0, 1) * 255);
    const hV = Math.round(clamp((bg.height ?? 0) * 0.5 + 0.5, 0, 1) * 255);
    rCtx.fillStyle = `rgb(${rV},${rV},${rV})`;
    rCtx.fillRect(0, 0, targetSize, targetSize);
    mCtx.fillStyle = `rgb(${mV},${mV},${mV})`;
    mCtx.fillRect(0, 0, targetSize, targetSize);
    hCtx.fillStyle = `rgb(${hV},${hV},${hV})`;
    hCtx.fillRect(0, 0, targetSize, targetSize);
  } else {
    const rV = 200, mV = 0, hV = 128;
    rCtx.fillStyle = `rgb(${rV},${rV},${rV})`;
    rCtx.fillRect(0, 0, targetSize, targetSize);
    mCtx.fillStyle = `rgb(${mV},${mV},${mV})`;
    mCtx.fillRect(0, 0, targetSize, targetSize);
    hCtx.fillStyle = `rgb(${hV},${hV},${hV})`;
    hCtx.fillRect(0, 0, targetSize, targetSize);
  }

  for (const k of activeExtKeys) {
    const c = extCtxs[k];
    if (isRgbChannelKey(k)) {
      const bgHex = bg.surfaceValues?.[k] || (k === "emissive" ? "#000000" : "#ffffff");
      c.fillStyle = bgHex;
      c.fillRect(0, 0, targetSize, targetSize);
    } else {
      let b = bg.surfaceValues?.[k] != null
        ? encodeScalarChannelByte(k, Number(bg.surfaceValues[k]))
        : (k === "emissiveIntensity" ? 0 : defaultByteForExtChannel(k));
      if (!bg.enabled && k === "opacity") b = 0;
      c.fillStyle = `rgb(${b},${b},${b})`;
      c.fillRect(0, 0, targetSize, targetSize);
    }
  }

  // Evaluate layers bottom-to-top
  const layerScratchA = makeScratchCanvas(targetSize);
  const layerScratchR = makeScratchCanvas(targetSize);
  const layerScratchM = makeScratchCanvas(targetSize);
  const layerScratchH = makeScratchCanvas(targetSize);
  const layerScratchCov = makeScratchCanvas(targetSize);
  const maskScratch = makeScratchCanvas(targetSize);

  const lACtx = layerScratchA.getContext("2d", { willReadFrequently: true });
  const lRCtx = layerScratchR.getContext("2d", { willReadFrequently: true });
  const lMCtx = layerScratchM.getContext("2d", { willReadFrequently: true });
  const lHCtx = layerScratchH.getContext("2d", { willReadFrequently: true });
  const lCovCtx = layerScratchCov.getContext("2d", { willReadFrequently: true });

  const folderMap = new Map(doc.layers.filter((l) => l.kind === "folder").map((f) => [f.id, f]));
  const isEffectiveVisible = (lyr) => {
    if (!lyr.visible) return false;
    if (lyr.parentId && folderMap.has(lyr.parentId)) return isEffectiveVisible(folderMap.get(lyr.parentId));
    return true;
  };

  for (const layer of doc.layers) {
    if (layer.kind === "folder" || !isEffectiveVisible(layer)) continue;
    const surf = ensureArtworkLayerSurface(layer);
    const layerExtKeys = activeExtKeys.filter((k) => surf.channels[k]);

    lACtx.clearRect(0, 0, targetSize, targetSize);
    lRCtx.clearRect(0, 0, targetSize, targetSize);
    lMCtx.clearRect(0, 0, targetSize, targetSize);
    lHCtx.clearRect(0, 0, targetSize, targetSize);
    lCovCtx.clearRect(0, 0, targetSize, targetSize);
    for (const k of layerExtKeys) extScratchCtxs[k].clearRect(0, 0, targetSize, targetSize);

    const instances = layer.kind === "gradient"
      ? [{
          x: size * 0.5,
          y: size * 0.5,
          width: size,
          height: size,
          rotation: 0,
          motifType: "square",
          fillMode: layer.shape.fillMode || "linear",
          fillStops: normalizeGradientStops(layer.shape.fillStops, layer.shape.color, layer.shape.colorB),
          fillPointGradient: layer.shape.fillPointGradient || null,
          color: layer.shape.color,
          colorB: layer.shape.colorB,
          gradientAngle: layer.shape.gradientAngle ?? 45,
          opacity: 1,
          heightValue: layer.surface.height ?? 0.05,
          roughness: layer.surface.roughness ?? 0.6,
          metalness: layer.surface.metalness ?? 0.0,
          outlineWidth: 0,
          chipAmount: 0,
        }]
      : evaluateLayerInstances(layer, size, doc.mode);

    const resolveScenePointPattern = (ctx, pointGrad, fallbackHex) => {
      const ptRes = Math.min(256, Math.max(64, Math.round(targetSize * 0.5)));
      const { canvas: pgCanvas } = getPointGradientCanvasAndUrl(pointGrad, {
        res: ptRes,
        size,
        seamless: doc.mode === "pattern",
      });
      if (!pgCanvas || typeof ctx.createPattern !== "function") return fallbackHex;
      const pat = ctx.createPattern(pgCanvas, "repeat");
      if (!pat) return fallbackHex;
      if (typeof DOMMatrix !== "undefined" && typeof pat.setTransform === "function" && typeof ctx.getTransform === "function") {
        try {
          pat.setTransform(ctx.getTransform().inverse().scale((scale * size) / ptRes));
        } catch {}
      }
      return pat;
    };

    // Draw each instance; wrap across tile edges if pattern mode
    const wraps = doc.mode === "pattern"
      ? [[0, 0], [-size, 0], [size, 0], [0, -size], [0, size], [-size, -size], [size, -size], [-size, size], [size, size]]
      : [[0, 0]];

    for (const inst of instances) {
      const rV = Math.round(clamp(inst.roughness ?? 0.5, 0, 1) * 255);
      const mV = Math.round(clamp(inst.metalness ?? 0, 0, 1) * 255);
      const hV = Math.round(clamp((inst.heightValue ?? 0.2) * 0.5 + 0.5, 0, 1) * 255);
      const rCol = `rgb(${rV},${rV},${rV})`;
      const mCol = `rgb(${mV},${mV},${mV})`;
      const hCol = `rgb(${hV},${hV},${hV})`;

      for (const [wx, wy] of wraps) {
        const px = inst.x + wx;
        const py = inst.y + wy;
        const margin = Math.max(inst.width, inst.height) * 0.8 + 40;
        if (px < -margin || px > size + margin || py < -margin || py > size + margin) continue;

        const drawToChannel = (ctx, fillStyle, isAlbedo = false) => {
          ctx.save();
          ctx.scale(scale, scale);
          ctx.translate(px, py);
          ctx.rotate((inst.rotation * Math.PI) / 180);
          ctx.scale(inst.flipX ? -1 : 1, inst.flipY ? -1 : 1);
          ctx.globalAlpha = clamp(inst.opacity, 0, 1);

          if (layer.kind === "text" && inst.text) {
            const t = inst.text;
            ctx.font = `${t.fontWeight || 600} ${t.fontSize || 48}px "${t.fontFamily || "DM Sans"}", sans-serif`;
            ctx.textAlign = t.align || "center";
            ctx.textBaseline = "middle";
            ctx.fillStyle = isAlbedo && inst.fillMode === "point"
              ? resolveScenePointPattern(ctx, inst.fillPointGradient, inst.color)
              : fillStyle;
            ctx.fillText(t.value || "", 0, 0);
            if (isAlbedo && inst.outlineWidth > 0) {
              ctx.lineWidth = inst.outlineWidth;
              ctx.strokeStyle = inst.strokeMode === "point"
                ? resolveScenePointPattern(ctx, inst.strokePointGradient, inst.outlineColor)
                : resolveStrokeStyle(ctx, inst, true);
              ctx.strokeText(t.value || "", 0, 0);
            }
          } else if (inst.motifType === "svg-path" && inst.svgPath && typeof Path2D !== "undefined") {
            ctx.save();
            const sx = Math.max(0.01, inst.width / 100);
            const sy = Math.max(0.01, inst.height / 100);
            ctx.scale(sx, sy);
            ctx.translate(-50, -50);
            const p2d = new Path2D(inst.svgPath);
            if (inst.fillEnabled !== false) {
              ctx.fillStyle = isAlbedo && inst.fillMode === "point"
                ? resolveScenePointPattern(ctx, inst.fillPointGradient, inst.color)
                : isAlbedo && (inst.fillMode === "linear" || inst.fillMode === "radial")
                  ? resolveCanvasGradient(ctx, inst.fillMode, inst.fillStops, inst.gradientAngle, 100, 100, inst.color, inst.colorB, 50, 50)
                  : fillStyle;
              ctx.fill(p2d, inst.fillRule === "nonzero" ? "nonzero" : "evenodd");
            }
            if (inst.outlineWidth > 0) {
              ctx.lineCap = "round";
              ctx.lineJoin = "round";
              ctx.lineWidth = inst.outlineWidth / Math.max(0.15, (sx + sy) * 0.5);
              ctx.strokeStyle = isAlbedo
                ? (inst.strokeMode === "point"
                    ? resolveScenePointPattern(ctx, inst.strokePointGradient, inst.outlineColor)
                    : inst.strokeMode === "linear" || inst.strokeMode === "radial"
                      ? resolveCanvasGradient(ctx, inst.strokeMode, inst.strokeStops, inst.strokeGradientAngle, 100, 100, inst.outlineColor, inst.colorB, 50, 50)
                      : (inst.outlineColor || inst.color || "#141619"))
                : fillStyle;
              ctx.stroke(p2d);
            }
            ctx.restore();
          } else {
            drawMotifPath(ctx, inst);
            if (inst.fillEnabled !== false && inst.motifType !== "line") {
              ctx.fillStyle = isAlbedo && inst.fillMode === "point"
                ? resolveScenePointPattern(ctx, inst.fillPointGradient, inst.color)
                : fillStyle;
              ctx.fill("evenodd");
            }
            if (inst.outlineWidth > 0 || inst.motifType === "line") {
              ctx.lineCap = "round";
              ctx.lineJoin = "round";
              ctx.lineWidth = Math.max(1, inst.outlineWidth || 4);
              ctx.strokeStyle = isAlbedo && inst.strokeMode === "point"
                ? resolveScenePointPattern(ctx, inst.strokePointGradient, inst.outlineColor)
                : resolveStrokeStyle(ctx, inst, isAlbedo);
              ctx.stroke();
            }
          }
          ctx.restore();
        };

        if (surf.channels.albedo) drawToChannel(lACtx, resolveFillStyle(lACtx, inst), true);
        if (surf.channels.roughness) drawToChannel(lRCtx, rCol);
        if (surf.channels.metalness) drawToChannel(lMCtx, mCol);
        if (surf.channels.height) drawToChannel(lHCtx, hCol);
        drawToChannel(lCovCtx, "#ffffff");
        for (const k of layerExtKeys) {
          const extCol = isRgbChannelKey(k)
            ? (surf.values[k] || (k === "emissive" ? "#ff7a3b" : "#ffffff"))
            : (() => {
                const b = encodeScalarChannelByte(k, Number(surf.values[k] ?? (ALL_PAINT_CHANNEL_MAP[k]?.defaultVal ?? 0)));
                return `rgb(${b},${b},${b})`;
              })();
          drawToChannel(extScratchCtxs[k], extCol);
        }
      }
    }

    const activeLayerExtCtxs = layerExtKeys.map((k) => extScratchCtxs[k]);

    // Apply optional Edge Wear chipping if any modifier requested chipAmount > 0
    const maxChip = instances.reduce((m, i) => Math.max(m, i.chipAmount || 0), 0);
    if (maxChip > 0.01) {
      const chipRef = instances.find((i) => i.chipAmount > 0.01);
      applyNoiseMaskCutout(lACtx, lRCtx, lMCtx, lHCtx, targetSize, {
        threshold: 0.25 + maxChip * 0.45,
        softness: 0.16,
        frequency: chipRef?.chipScale || 10,
        seed: chipRef?.chipSeed || 707,
        seamless: doc.mode === "pattern",
        invert: true,
      }, [lCovCtx, ...activeLayerExtCtxs]);
    }

    // Apply per-layer Mask if enabled
    if (layer.mask?.enabled) {
      applyLayerMask(lACtx, lRCtx, lMCtx, lHCtx, maskScratch, targetSize, layer.mask, doc.mode === "pattern", [lCovCtx, ...activeLayerExtCtxs]);
    }

    // Composite layer onto target channels
    const opChanMul = surf.channels.opacity ? clamp(surf.values.opacity ?? 1, 0, 1) : 1;
    const op = clamp((layer.opacity ?? 1) * opChanMul, 0, 1);
    const blend = canvasBlendOp(layer.blend);

    covCtx.save();
    covCtx.globalAlpha = op;
    covCtx.drawImage(layerScratchCov, 0, 0);
    covCtx.restore();

    if (surf.channels.albedo) {
      aCtx.save();
      aCtx.globalAlpha = op;
      aCtx.globalCompositeOperation = blend;
      aCtx.drawImage(layerScratchA, 0, 0);
      aCtx.restore();
    }
    if (surf.channels.roughness) {
      rCtx.save();
      rCtx.globalAlpha = op;
      rCtx.drawImage(layerScratchR, 0, 0);
      rCtx.restore();
    }
    if (surf.channels.metalness) {
      mCtx.save();
      mCtx.globalAlpha = op;
      mCtx.drawImage(layerScratchM, 0, 0);
      mCtx.restore();
    }
    if (surf.channels.height) {
      hCtx.save();
      hCtx.globalAlpha = op;
      hCtx.drawImage(layerScratchH, 0, 0);
      hCtx.restore();
    }
    for (const k of layerExtKeys) {
      const dstCtx = extCtxs[k];
      dstCtx.save();
      dstCtx.globalAlpha = op;
      dstCtx.drawImage(extScratches[k], 0, 0);
      dstCtx.restore();
    }
  }

  return {
    albedo,
    roughness,
    metalness,
    height,
    coverageCanvas,
    extraCanvases: extTargets,
    normalGain: surfInfo.normalGain,
    activeChannels: surfInfo.activeChannels,
    surfaceSummary: surfInfo.surfaceSummary,
    width: targetSize,
    heightPx: targetSize,
  };
}

function applyNoiseMaskCutout(aCtx, rCtx, mCtx, hCtx, size, opts, extraCtxs = []) {
  const img = aCtx.getImageData(0, 0, size, size);
  const rImg = rCtx.getImageData(0, 0, size, size);
  const mImg = mCtx.getImageData(0, 0, size, size);
  const hImg = hCtx.getImageData(0, 0, size, size);
  const extImgs = extraCtxs.map((ctx) => ctx.getImageData(0, 0, size, size));
  const d = img.data;
  const rd = rImg.data;
  const md = mImg.data;
  const hd = hImg.data;
  const step = size > 256 ? 2 : 1;

  for (let y = 0; y < size; y += step) {
    for (let x = 0; x < size; x += step) {
      const idx0 = (y * size + x) * 4;
      if (d[idx0 + 3] === 0 && hd[idx0 + 3] === 0 && !extImgs.some((ei) => ei.data[idx0 + 3] > 0)) continue;
      const n = evaluateTileNoise(x / size, y / size, {
        noiseType: "fbm",
        frequency: opts.frequency || 10,
        octaves: 3,
        seed: opts.seed || 707,
        seamless: opts.seamless,
      });
      const keep = n > (1 - opts.threshold) ? 1 : clamp((n - (1 - opts.threshold - opts.softness)) / Math.max(0.01, opts.softness), 0, 1);
      for (let sy = 0; sy < step && y + sy < size; sy++) {
        for (let sx = 0; sx < step && x + sx < size; sx++) {
          const k = ((y + sy) * size + (x + sx)) * 4 + 3;
          d[k] = Math.round(d[k] * keep);
          rd[k] = Math.round(rd[k] * keep);
          md[k] = Math.round(md[k] * keep);
          hd[k] = Math.round(hd[k] * keep);
          for (const ei of extImgs) ei.data[k] = Math.round(ei.data[k] * keep);
        }
      }
    }
  }
  aCtx.putImageData(img, 0, 0);
  rCtx.putImageData(rImg, 0, 0);
  mCtx.putImageData(mImg, 0, 0);
  hCtx.putImageData(hImg, 0, 0);
  for (let i = 0; i < extraCtxs.length; i++) extraCtxs[i].putImageData(extImgs[i], 0, 0);
}

function applyLayerMask(aCtx, rCtx, mCtx, hCtx, maskCanvas, size, mask, seamless, extraCtxs = []) {
  const img = aCtx.getImageData(0, 0, size, size);
  const rImg = rCtx.getImageData(0, 0, size, size);
  const mImg = mCtx.getImageData(0, 0, size, size);
  const hImg = hCtx.getImageData(0, 0, size, size);
  const extImgs = extraCtxs.map((ctx) => ctx.getImageData(0, 0, size, size));
  const d = img.data, rd = rImg.data, md = mImg.data, hd = hImg.data;
  const freq = mask.frequency ?? 4;
  const thr = clamp(mask.threshold ?? 0.45, 0, 1);
  const soft = Math.max(0.02, mask.softness ?? 0.25);

  for (let y = 0; y < size; y++) {
    const v = y / size;
    for (let x = 0; x < size; x++) {
      const k = (y * size + x) * 4 + 3;
      if (d[k] === 0 && hd[k] === 0 && !extImgs.some((ei) => ei.data[k] > 0)) continue;
      const u = x / size;
      let val = 1;
      if (mask.mode === "radial") {
        val = 1 - clamp(Math.hypot(u - 0.5, v - 0.5) * 2, 0, 1);
      } else if (mask.mode === "stripe") {
        val = 0.5 + 0.5 * Math.sin(u * Math.PI * 2 * Math.max(1, Math.round(freq)));
      } else {
        val = evaluateTileNoise(u, v, {
          noiseType: "fbm",
          frequency: freq,
          octaves: 3,
          seed: mask.seed || 211,
          seamless,
        });
      }
      if (mask.invert) val = 1 - val;
      const alpha = clamp((val - (thr - soft * 0.5)) / soft, 0, 1);
      d[k] = Math.round(d[k] * alpha);
      rd[k] = Math.round(rd[k] * alpha);
      md[k] = Math.round(md[k] * alpha);
      hd[k] = Math.round(hd[k] * alpha);
      for (const ei of extImgs) ei.data[k] = Math.round(ei.data[k] * alpha);
    }
  }
  aCtx.putImageData(img, 0, 0);
  rCtx.putImageData(rImg, 0, 0);
  mCtx.putImageData(mImg, 0, 0);
  hCtx.putImageData(hImg, 0, 0);
  for (let i = 0; i < extraCtxs.length; i++) extraCtxs[i].putImageData(extImgs[i], 0, 0);
}

/**
 * Derive tangent-space RGB Normal map ImageData from a height canvas via Sobel filter.
 */
export function buildNormalFromHeightCanvas(heightCanvas, strength = 2.6) {
  const w = heightCanvas.width;
  const h = heightCanvas.height;
  const src = heightCanvas.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, w, h).data;
  const outCanvas = makeScratchCanvas(w);
  const oCtx = outCanvas.getContext("2d", { willReadFrequently: true });
  const out = oCtx.createImageData(w, h);
  const dst = out.data;

  const sample = (x, y) => {
    const px = ((x % w) + w) % w;
    const py = ((y % h) + h) % h;
    return src[(py * w + px) * 4] / 255;
  };

  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const tl = sample(x - 1, y - 1), t = sample(x, y - 1), tr = sample(x + 1, y - 1);
      const l = sample(x - 1, y), r = sample(x + 1, y);
      const bl = sample(x - 1, y + 1), b = sample(x, y + 1), br = sample(x + 1, y + 1);
      const dx = (tr + 2 * r + br) - (tl + 2 * l + bl);
      const dy = (bl + 2 * b + br) - (tl + 2 * t + tr);
      let nx = -dx * strength;
      let ny = -dy * strength;
      let nz = 1.0;
      const len = Math.hypot(nx, ny, nz);
      nx /= len; ny /= len; nz /= len;
      const idx = (y * w + x) * 4;
      dst[idx] = Math.round((nx * 0.5 + 0.5) * 255);
      dst[idx + 1] = Math.round((ny * 0.5 + 0.5) * 255);
      dst[idx + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      dst[idx + 3] = 255;
    }
  }
  oCtx.putImageData(out, 0, 0);
  return outCanvas;
}

/**
 * Evaluate document into flat Uint8ClampedArray PBR buffers + instance stats (browser Canvas OR pure-JS Node fallback).
 */
export function evaluateArtworkDocument(doc, targetSize = 384, opts = {}) {
  const docSize = doc.size || ARTWORK_SIZE;
  const surfInfo = collectDocumentSurfaceInfo(doc, opts.extraChannels || []);
  let totalInstances = 0;
  let minScale = Infinity;
  let maxScale = -Infinity;

  for (const lyr of doc.layers || []) {
    if (!lyr.visible || lyr.kind === "folder") continue;
    const insts = evaluateLayerInstances(lyr, docSize, doc.mode);
    totalInstances += insts.length;
    const baseW = Math.max(1, lyr.transform?.width || 48);
    for (const inst of insts) {
      const s = inst.width / baseW;
      if (s < minScale) minScale = s;
      if (s > maxScale) maxScale = s;
    }
  }
  if (!Number.isFinite(minScale)) { minScale = 1; maxScale = 1; }

  const canCanvas = !opts.headless && (typeof document !== "undefined" || typeof OffscreenCanvas !== "undefined");
  if (canCanvas) {
    const rendered = renderArtworkDocument(doc, targetSize, { extraChannels: surfInfo.activeExtKeys });
    const normCanvas = buildNormalFromHeightCanvas(rendered.height, 2.6 * (rendered.normalGain ?? 1.0));
    const albedo = rendered.albedo.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const roughness = rendered.roughness.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const metalness = rendered.metalness.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const height = rendered.height.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const normal = normCanvas.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const covRgba = rendered.coverageCanvas.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    const n = targetSize * targetSize;
    const coverage = new Uint8ClampedArray(n);
    for (let i = 0; i < n; i++) coverage[i] = covRgba[i * 4 + 3];

    const extraChannels = {};
    for (const k of surfInfo.activeExtKeys) {
      const c = rendered.extraCanvases?.[k];
      if (c) extraChannels[k] = c.getContext("2d", { willReadFrequently: true }).getImageData(0, 0, targetSize, targetSize).data;
    }

    let opaque = 0;
    for (let i = 0; i < n; i++) if (Math.max(albedo[i * 4 + 3], coverage[i]) > 18) opaque++;
    return {
      width: targetSize,
      heightPx: targetSize,
      albedo,
      roughness,
      metalness,
      height,
      normal,
      coverage,
      extraChannels,
      activeChannels: surfInfo.activeChannels,
      surfaceSummary: surfInfo.surfaceSummary,
      stats: {
        instances: totalInstances,
        minScale,
        maxScale,
        coveragePct: (opaque / n) * 100,
      },
    };
  }

  // Headless Node rasterizer fallback (no DOM Canvas)
  const n = targetSize * targetSize;
  const albedo = new Uint8ClampedArray(n * 4);
  const roughness = new Uint8ClampedArray(n * 4);
  const metalness = new Uint8ClampedArray(n * 4);
  const height = new Uint8ClampedArray(n * 4);
  const normal = new Uint8ClampedArray(n * 4);
  const coverage = new Uint8ClampedArray(n);
  const extraChannels = {};

  const bg = doc.background || {};
  const [bgr, bgg, bgb] = bg.enabled ? hexToRgb(bg.color || "#171a1f") : [0, 0, 0];
  const bga = bg.enabled ? Math.round(clamp(bg.opacity ?? 1, 0, 1) * 255) : 0;
  const bgrV = Math.round(clamp(bg.roughness ?? 0.85, 0, 1) * 255);
  const bgmV = Math.round(clamp(bg.metalness ?? 0, 0, 1) * 255);
  const bghV = Math.round(clamp((bg.height ?? 0) * 0.5 + 0.5, 0, 1) * 255);
  for (let i = 0; i < n; i++) {
    const i4 = i * 4;
    albedo[i4] = bgr; albedo[i4 + 1] = bgg; albedo[i4 + 2] = bgb; albedo[i4 + 3] = bga;
    coverage[i] = bga;
    roughness[i4] = roughness[i4 + 1] = roughness[i4 + 2] = bgrV; roughness[i4 + 3] = 255;
    metalness[i4] = metalness[i4 + 1] = metalness[i4 + 2] = bgmV; metalness[i4 + 3] = 255;
    height[i4] = height[i4 + 1] = height[i4 + 2] = bghV; height[i4 + 3] = 255;
    normal[i4] = 128; normal[i4 + 1] = 128; normal[i4 + 2] = 255; normal[i4 + 3] = 255;
  }

  for (const k of surfInfo.activeExtKeys) {
    const buf = new Uint8ClampedArray(n * 4);
    if (isRgbChannelKey(k)) {
      const [er, eg, eb] = hexToRgb(bg.surfaceValues?.[k] || (k === "emissive" ? "#000000" : "#ffffff"));
      for (let i = 0; i < n; i++) {
        const i4 = i * 4;
        buf[i4] = er; buf[i4 + 1] = eg; buf[i4 + 2] = eb; buf[i4 + 3] = 255;
      }
    } else {
      let b = bg.surfaceValues?.[k] != null
        ? encodeScalarChannelByte(k, Number(bg.surfaceValues[k]))
        : (k === "emissiveIntensity" ? 0 : defaultByteForExtChannel(k));
      const ba = !bg.enabled && k === "opacity" ? 0 : 255;
      if (!bg.enabled && k === "opacity") b = 0;
      for (let i = 0; i < n; i++) {
        const i4 = i * 4;
        buf[i4] = buf[i4 + 1] = buf[i4 + 2] = b; buf[i4 + 3] = ba;
      }
    }
    extraChannels[k] = buf;
  }

  const scale = targetSize / docSize;
  const folderMapH = new Map((doc.layers || []).filter((l) => l.kind === "folder").map((f) => [f.id, f]));
  const isVisH = (lyr) => {
    if (!lyr.visible) return false;
    if (lyr.parentId && folderMapH.has(lyr.parentId)) return isVisH(folderMapH.get(lyr.parentId));
    return true;
  };
  for (const layer of doc.layers || []) {
    if (layer.kind === "folder" || !isVisH(layer)) continue;
    const surf = ensureArtworkLayerSurface(layer);
    const layerExtKeys = surfInfo.activeExtKeys.filter((k) => surf.channels[k]);
    const layerExtSamples = layerExtKeys.map((k) => {
      const buf = extraChannels[k];
      if (isRgbChannelKey(k)) {
        const [er, eg, eb] = hexToRgb(surf.values[k] || (k === "emissive" ? "#ff7a3b" : "#ffffff"));
        return { buf, r: er, g: eg, b: eb };
      }
      const bv = encodeScalarChannelByte(k, Number(surf.values[k] ?? (ALL_PAINT_CHANNEL_MAP[k]?.defaultVal ?? 0)));
      return { buf, r: bv, g: bv, b: bv };
    });

    const insts = layer.kind === "gradient"
      ? [{ x: docSize * 0.5, y: docSize * 0.5, width: docSize, height: docSize, color: layer.shape?.color || "#e8c27a", roughness: surf.roughness, metalness: surf.metalness, heightValue: surf.height, opacity: 1 }]
      : evaluateLayerInstances(layer, docSize, doc.mode);
    const opChanMul = surf.channels.opacity ? clamp(surf.values.opacity ?? 1, 0, 1) : 1;
    const lOp = clamp((layer.opacity ?? 1) * opChanMul, 0, 1);

    for (const inst of insts) {
      const [cr, cg, cb] = hexToRgb(inst.color || "#e8c27a");
      const rV = Math.round(clamp(inst.roughness ?? 0.5, 0, 1) * 255);
      const mV = Math.round(clamp(inst.metalness ?? 0, 0, 1) * 255);
      const hV = Math.round(clamp((inst.heightValue ?? 0.2) * 0.5 + 0.5, 0, 1) * 255);
      const cx = inst.x * scale, cy = inst.y * scale;
      const rx = Math.max(1, inst.width * scale * 0.5);
      const ry = Math.max(1, inst.height * scale * 0.5);
      const x0 = Math.max(0, Math.floor(cx - rx)), x1 = Math.min(targetSize - 1, Math.ceil(cx + rx));
      const y0 = Math.max(0, Math.floor(cy - ry)), y1 = Math.min(targetSize - 1, Math.ceil(cy + ry));
      const a = clamp((inst.opacity ?? 1) * lOp, 0, 1);
      for (let py = y0; py <= y1; py++) {
        for (let px = x0; px <= x1; px++) {
          if (layer.kind !== "gradient") {
            const dx = (px - cx) / rx, dy = (py - cy) / ry;
            if (dx * dx + dy * dy > 1.0) continue;
          }
          const idx = py * targetSize + px;
          const i4 = idx * 4;
          coverage[idx] = Math.min(255, Math.round(coverage[idx] + (255 - coverage[idx]) * a));
          if (surf.channels.albedo) {
            albedo[i4] = Math.round(albedo[i4] * (1 - a) + cr * a);
            albedo[i4 + 1] = Math.round(albedo[i4 + 1] * (1 - a) + cg * a);
            albedo[i4 + 2] = Math.round(albedo[i4 + 2] * (1 - a) + cb * a);
            albedo[i4 + 3] = Math.min(255, Math.round(albedo[i4 + 3] + (255 - albedo[i4 + 3]) * a));
          }
          if (surf.channels.roughness) {
            const nr = Math.round(roughness[i4] * (1 - a) + rV * a);
            roughness[i4] = roughness[i4 + 1] = roughness[i4 + 2] = nr;
          }
          if (surf.channels.metalness) {
            const nm = Math.round(metalness[i4] * (1 - a) + mV * a);
            metalness[i4] = metalness[i4 + 1] = metalness[i4 + 2] = nm;
          }
          if (surf.channels.height) {
            const nh = Math.round(height[i4] * (1 - a) + hV * a);
            height[i4] = height[i4 + 1] = height[i4 + 2] = nh;
          }
          for (const es of layerExtSamples) {
            es.buf[i4] = Math.round(es.buf[i4] * (1 - a) + es.r * a);
            es.buf[i4 + 1] = Math.round(es.buf[i4 + 1] * (1 - a) + es.g * a);
            es.buf[i4 + 2] = Math.round(es.buf[i4 + 2] * (1 - a) + es.b * a);
            es.buf[i4 + 3] = Math.min(255, Math.round(es.buf[i4 + 3] + (255 - es.buf[i4 + 3]) * a));
          }
        }
      }
    }
  }
  const nStrength = 2.6 * surfInfo.normalGain;
  const sampleH = (x, y) => {
    const px = ((x % targetSize) + targetSize) % targetSize;
    const py = ((y % targetSize) + targetSize) % targetSize;
    return height[(py * targetSize + px) * 4] / 255;
  };
  for (let y = 0; y < targetSize; y++) {
    for (let x = 0; x < targetSize; x++) {
      const dx = (sampleH(x + 1, y) - sampleH(x - 1, y)) * 2.0;
      const dy = (sampleH(x, y + 1) - sampleH(x, y - 1)) * 2.0;
      let nx = -dx * nStrength, ny = -dy * nStrength, nz = 1.0;
      const len = Math.hypot(nx, ny, nz);
      nx /= len; ny /= len; nz /= len;
      const i4 = (y * targetSize + x) * 4;
      normal[i4] = Math.round((nx * 0.5 + 0.5) * 255);
      normal[i4 + 1] = Math.round((ny * 0.5 + 0.5) * 255);
      normal[i4 + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      normal[i4 + 3] = 255;
    }
  }
  let opaque = 0;
  for (let i = 0; i < n; i++) if (Math.max(albedo[i * 4 + 3], coverage[i]) > 18) opaque++;
  return {
    width: targetSize,
    heightPx: targetSize,
    albedo,
    roughness,
    metalness,
    height,
    normal,
    coverage,
    extraChannels,
    activeChannels: surfInfo.activeChannels,
    surfaceSummary: surfInfo.surfaceSummary,
    stats: {
      instances: totalInstances,
      minScale,
      maxScale,
      coveragePct: (opaque / n) * 100,
    },
  };
}

function escapeXml(s) {
  return String(s || "").replace(/[&<>'"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", "\"": "&quot;" }[c]));
}

function pushSvgGradientDef(gradDefs, id, mode, stops, angle = 45, fallbackA = "#e8c27a", fallbackB = "#1a1e24") {
  const norm = normalizeGradientStops(stops, fallbackA, fallbackB);
  const stopTags = norm
    .map((s) => `<stop offset="${(s.offset * 100).toFixed(1)}%" stop-color="${escapeXml(s.color)}"/>`)
    .join("");
  if (mode === "radial") {
    gradDefs.push(`<radialGradient id="${id}">${stopTags}</radialGradient>`);
  } else {
    const a = ((angle ?? 45) * Math.PI) / 180;
    const x1 = (50 - Math.cos(a) * 50).toFixed(1);
    const y1 = (50 - Math.sin(a) * 50).toFixed(1);
    const x2 = (50 + Math.cos(a) * 50).toFixed(1);
    const y2 = (50 + Math.sin(a) * 50).toFixed(1);
    gradDefs.push(`<linearGradient id="${id}" x1="${x1}%" y1="${y1}%" x2="${x2}%" y2="${y2}%">${stopTags}</linearGradient>`);
  }
}

function pushSvgPointGradientPatternDef(gradDefs, id, rawGrad, {
  size = ARTWORK_SIZE,
  seamless = false,
  patternTransform = "",
  res = 128,
} = {}) {
  const { dataUrl, grad } = getPointGradientCanvasAndUrl(rawGrad, { res, size, seamless });
  const xfAttr = patternTransform ? ` patternTransform="${patternTransform}"` : "";
  if (dataUrl) {
    gradDefs.push(`<pattern id="${id}" patternUnits="userSpaceOnUse" x="0" y="0" width="${size}" height="${size}"${xfAttr}><image x="0" y="0" width="${size}" height="${size}" preserveAspectRatio="none" href="${dataUrl}"/></pattern>`);
    return;
  }
  // Headless Node SVG fallback when Canvas2D image encoder isn't present
  const bg = escapeXml(grad.background || "#1d222b");
  const wraps = seamless
    ? [[0, 0], [-size, 0], [size, 0], [0, -size], [0, size], [-size, -size], [size, -size], [-size, size], [size, size]]
    : [[0, 0]];
  const radDefs = [];
  const blobs = [];
  grad.points.forEach((pt, idx) => {
    const rid = `${id}-r${idx}`;
    const r = Math.max(8, pt.radius * (grad.falloff || 1));
    const col = escapeXml(pt.color);
    const op = clamp(pt.weight ?? 1, 0, 1).toFixed(3);
    radDefs.push(`<radialGradient id="${rid}"><stop offset="0%" stop-color="${col}" stop-opacity="${op}"/><stop offset="100%" stop-color="${col}" stop-opacity="0"/></radialGradient>`);
    for (const [wx, wy] of wraps) {
      blobs.push(`<circle cx="${(pt.x + wx).toFixed(1)}" cy="${(pt.y + wy).toFixed(1)}" r="${r.toFixed(1)}" fill="url(#${rid})"/>`);
    }
  });
  gradDefs.push(...radDefs, `<pattern id="${id}" patternUnits="userSpaceOnUse" x="0" y="0" width="${size}" height="${size}"${xfAttr}><rect width="${size}" height="${size}" fill="${bg}"/>${blobs.join("")}</pattern>`);
}

function buildSvgMotifMarkup(inst, fillAttr, strokeAttr, strokeW, gradDefs, gid, sceneCtx = null) {
  const w = Math.max(1, inst.width), h = Math.max(1, inst.height);
  const hw = w * 0.5, hh = h * 0.5;
  const t = inst.motifType || "circle";
  const sceneSize = sceneCtx?.size || ARTWORK_SIZE;
  const isSeamless = !!sceneCtx?.seamless;
  const buildInverseSceneTransform = (forSvgPath = false) => {
    if (!sceneCtx) return "";
    const px = Number(sceneCtx.px ?? inst.x ?? 256);
    const py = Number(sceneCtx.py ?? inst.y ?? 256);
    const rot = Number(inst.rotation || 0);
    const fx = inst.flipX ? -1 : 1;
    const fy = inst.flipY ? -1 : 1;
    const base = `scale(${fx} ${fy}) rotate(${(-rot).toFixed(3)}) translate(${(-px).toFixed(3)} ${(-py).toFixed(3)})`;
    if (forSvgPath) {
      return `translate(50 50) scale(${(100 / Math.max(1, w)).toFixed(5)} ${(100 / Math.max(1, h)).toFixed(5)}) ${base}`;
    }
    return base;
  };

  let effectiveFill = inst.fillEnabled === false || t === "line" ? "none" : fillAttr;
  if (effectiveFill !== "none") {
    if (inst.fillMode === "point") {
      const id = `${gid}-pg`;
      pushSvgPointGradientPatternDef(gradDefs, id, inst.fillPointGradient, {
        size: sceneSize,
        seamless: isSeamless,
        patternTransform: buildInverseSceneTransform(t === "svg-path" && !!inst.svgPath),
        res: sceneCtx?.pointRes || 128,
      });
      effectiveFill = `url(#${id})`;
    } else if (inst.fillMode === "linear" || inst.fillMode === "radial") {
      const id = `${gid}-g`;
      pushSvgGradientDef(gradDefs, id, inst.fillMode, inst.fillStops, inst.gradientAngle, inst.color, inst.colorB || "#1a1e24");
      effectiveFill = `url(#${id})`;
    }
  }
  let effectiveStroke = strokeAttr;
  if (strokeW > 0 || t === "line") {
    if (inst.strokeMode === "point") {
      const sid = `${gid}-spg`;
      pushSvgPointGradientPatternDef(gradDefs, sid, inst.strokePointGradient, {
        size: sceneSize,
        seamless: isSeamless,
        patternTransform: buildInverseSceneTransform(t === "svg-path" && !!inst.svgPath),
        res: sceneCtx?.pointRes || 128,
      });
      effectiveStroke = `url(#${sid})`;
    } else if (inst.strokeMode === "linear" || inst.strokeMode === "radial") {
      const sid = `${gid}-sg`;
      pushSvgGradientDef(gradDefs, sid, inst.strokeMode, inst.strokeStops, inst.strokeGradientAngle, strokeAttr, inst.strokeStops?.[inst.strokeStops.length - 1]?.color || "#eff4cf");
      effectiveStroke = `url(#${sid})`;
    }
  }
  const common = `fill="${effectiveFill}" fill-rule="${inst.fillRule === "nonzero" ? "nonzero" : "evenodd"}" stroke="${strokeW > 0 || t === "line" ? effectiveStroke : "none"}" stroke-width="${t === "line" ? Math.max(1, strokeW || 4) : strokeW}" stroke-linecap="round" stroke-linejoin="round"`;

  if (t === "svg-path" && inst.svgPath) {
    return `<path d="${escapeXml(inst.svgPath)}" transform="scale(${(w / 100).toFixed(4)} ${(h / 100).toFixed(4)}) translate(-50 -50)" vector-effect="non-scaling-stroke" ${common}/>`;
  }
  if (t === "circle") return `<ellipse cx="0" cy="0" rx="${hw}" ry="${hh}" ${common}/>`;
  if (t === "ring") {
    const inner = clamp(inst.innerRatio ?? 0.5, 0.1, 0.92);
    const rxI = hw * inner, ryI = hh * inner;
    const d = `M${-hw} 0 A${hw} ${hh} 0 1 0 ${hw} 0 A${hw} ${hh} 0 1 0 ${-hw} 0 Z M${-rxI} 0 A${rxI} ${ryI} 0 1 1 ${rxI} 0 A${rxI} ${ryI} 0 1 1 ${-rxI} 0 Z`;
    return `<path d="${d}" ${common}/>`;
  }
  if (t === "square") return `<rect x="${-hw}" y="${-hh}" width="${w}" height="${h}" ${common}/>`;
  if (t === "roundedRect") {
    const r = Math.min(hw, hh) * clamp(inst.cornerRadius ?? 0.22, 0, 1);
    return `<rect x="${-hw}" y="${-hh}" width="${w}" height="${h}" rx="${r}" ry="${r}" ${common}/>`;
  }
  if (t === "diamond") return `<polygon points="0,${-hh} ${hw},0 0,${hh} ${-hw},0" ${common}/>`;
  if (t === "triangle") return `<polygon points="0,${-hh} ${hw},${hh} ${-hw},${hh}" ${common}/>`;
  if (t === "hexagon") {
    const sides = Math.max(3, Math.min(12, Math.round(inst.sides || 6)));
    const pts = Array.from({ length: sides }, (_, i) => {
      const a = (i / sides) * Math.PI * 2 - Math.PI * 0.5;
      return `${(Math.cos(a) * hw).toFixed(2)},${(Math.sin(a) * hh).toFixed(2)}`;
    }).join(" ");
    return `<polygon points="${pts}" ${common}/>`;
  }
  if (t === "star") {
    const n = Math.max(3, Math.min(16, Math.round(inst.points || 5)));
    const inner = clamp(inst.innerRatio ?? 0.45, 0.15, 0.85);
    const pts = Array.from({ length: n * 2 }, (_, i) => {
      const a = (i / (n * 2)) * Math.PI * 2 - Math.PI * 0.5;
      const r = i % 2 === 0 ? 1 : inner;
      return `${(Math.cos(a) * hw * r).toFixed(2)},${(Math.sin(a) * hh * r).toFixed(2)}`;
    }).join(" ");
    return `<polygon points="${pts}" ${common}/>`;
  }
  if (t === "cross") {
    const arm = clamp(inst.innerRatio ?? 0.34, 0.1, 0.8);
    const aw = hw * arm, ah = hh * arm;
    return `<polygon points="${-aw},${-hh} ${aw},${-hh} ${aw},${-ah} ${hw},${-ah} ${hw},${ah} ${aw},${ah} ${aw},${hh} ${-aw},${hh} ${-aw},${ah} ${-hw},${ah} ${-hw},${-ah} ${-aw},${-ah}" ${common}/>`;
  }
  if (t === "chevron") {
    const th = hh * 0.48;
    const midY = -hh + (hh * 2 - th);
    return `<polygon points="${-hw},${th} 0,${-hh} ${hw},${th} ${hw},${hh} 0,${midY} ${-hw},${hh}" ${common}/>`;
  }
  if (t === "stripe") return `<rect x="${-hw}" y="${(-hh * 0.28).toFixed(2)}" width="${w}" height="${(h * 0.56).toFixed(2)}" ${common}/>`;
  if (t === "line") return `<line x1="${-hw}" y1="0" x2="${hw}" y2="0" ${common}/>`;
  if (t === "flower") {
    return [0, 72, 144, 216, 288].map((a) => `<ellipse cx="0" cy="${(-hh * 0.46).toFixed(2)}" rx="${(hw * 0.36).toFixed(2)}" ry="${(hh * 0.54).toFixed(2)}" transform="rotate(${a})" ${common}/>`).join("");
  }
  if (t === "arc") {
    const inner = clamp(inst.innerRatio ?? 0.55, 0.2, 0.85);
    const ri = hw * inner;
    const d = `M${-hw} 0 A${hw} ${hw} 0 0 1 ${hw} 0 L${ri} 0 A${ri} ${ri} 0 0 0 ${-ri} 0 Z`;
    return `<path d="${d}" ${common}/>`;
  }
  if (t === "teardrop") {
    const d = `M0 ${-hh} C${hw * 0.85} ${-hh * 0.15} ${hw} ${hh * 0.55} 0 ${hh} C${-hw} ${hh * 0.55} ${-hw * 0.85} ${-hh * 0.15} 0 ${-hh} Z`;
    return `<path d="${d}" ${common}/>`;
  }
  if (t === "leaf") {
    const d = `M0 ${-hh} Q${hw} ${-hh * 0.2} 0 ${hh} Q${-hw} ${-hh * 0.2} 0 ${-hh} Z`;
    return `<path d="${d}" ${common}/>`;
  }
  if (t === "gear") {
    const teeth = Math.max(6, Math.min(20, Math.round(inst.points || 8)));
    const innerR = hw * clamp(inst.innerRatio ?? 0.72, 0.45, 0.9);
    const holeR = hw * 0.32;
    let d = "";
    for (let i = 0; i < teeth * 2; i++) {
      const a0 = (i / (teeth * 2)) * Math.PI * 2;
      const a1 = ((i + 0.65) / (teeth * 2)) * Math.PI * 2;
      const r = i % 2 === 0 ? hw : innerR;
      d += `${i === 0 ? "M" : "L"}${(Math.cos(a0) * r).toFixed(2)} ${(Math.sin(a0) * r).toFixed(2)} L${(Math.cos(a1) * r).toFixed(2)} ${(Math.sin(a1) * r).toFixed(2)} `;
    }
    d += `Z M${holeR} 0 A${holeR} ${holeR} 0 1 0 ${-holeR} 0 A${holeR} ${holeR} 0 1 0 ${holeR} 0 Z`;
    return `<path d="${d}" ${common}/>`;
  }
  return `<ellipse cx="0" cy="0" rx="${hw}" ry="${hh}" ${common}/>`;
}

export function artworkDocumentLayersSvg(doc, { idPrefix = "art", pointRes = 128 } = {}) {
  const size = doc.size || ARTWORK_SIZE;
  const gradDefs = [];
  const groups = [];
  const folderMap = new Map((doc.layers || []).filter((l) => l.kind === "folder").map((f) => [f.id, f]));
  const isVisible = (lyr) => {
    if (!lyr.visible) return false;
    if (lyr.parentId && folderMap.has(lyr.parentId)) return isVisible(folderMap.get(lyr.parentId));
    return true;
  };
  const wraps = doc.mode === "pattern"
    ? [[0, 0], [-size, 0], [size, 0], [0, -size], [0, size], [-size, -size], [size, -size], [-size, size], [size, size]]
    : [[0, 0]];

  let instSeq = 0;
  for (const layer of doc.layers || []) {
    if (layer.kind === "folder" || !isVisible(layer)) continue;
    const instances = layer.kind === "gradient"
      ? [{
          x: size * 0.5, y: size * 0.5, width: size, height: size, rotation: 0,
          motifType: "square", fillMode: layer.shape.fillMode || "linear",
          fillStops: normalizeGradientStops(layer.shape.fillStops, layer.shape.color, layer.shape.colorB),
          fillPointGradient: layer.shape.fillPointGradient || null,
          color: layer.shape.color, colorB: layer.shape.colorB, gradientAngle: layer.shape.gradientAngle ?? 45,
          opacity: 1, fillEnabled: true, outlineWidth: 0,
        }]
      : evaluateLayerInstances(layer, size, doc.mode);

    const parts = [];
    for (const inst of instances) {
      for (const [wx, wy] of wraps) {
        const px = inst.x + wx, py = inst.y + wy;
        const margin = Math.max(inst.width || 64, inst.height || 64) * 0.8 + 40;
        if (px < -margin || px > size + margin || py < -margin || py > size + margin) continue;
        const xf = `translate(${px.toFixed(2)} ${py.toFixed(2)}) rotate(${(inst.rotation || 0).toFixed(2)}) scale(${inst.flipX ? -1 : 1} ${inst.flipY ? -1 : 1})`;
        if (layer.kind === "text" && inst.text) {
          const t = inst.text;
          const anchor = t.align === "left" ? "start" : t.align === "right" ? "end" : "middle";
          let textFill = inst.color || "#e8c27a";
          if (inst.fillMode === "point") {
            const tid = `${idPrefix}-${instSeq++}-tpg`;
            const inv = `scale(${inst.flipX ? -1 : 1} ${inst.flipY ? -1 : 1}) rotate(${(-(inst.rotation || 0)).toFixed(3)}) translate(${(-px).toFixed(3)} ${(-py).toFixed(3)})`;
            pushSvgPointGradientPatternDef(gradDefs, tid, inst.fillPointGradient, {
              size,
              seamless: doc.mode === "pattern",
              patternTransform: inv,
              res: pointRes,
            });
            textFill = `url(#${tid})`;
          } else if (inst.fillMode === "linear" || inst.fillMode === "radial") {
            const tid = `${idPrefix}-${instSeq++}-tg`;
            pushSvgGradientDef(gradDefs, tid, inst.fillMode, inst.fillStops, inst.gradientAngle, inst.color, inst.colorB);
            textFill = `url(#${tid})`;
          }
          parts.push(`<text transform="${xf}" opacity="${clamp(inst.opacity, 0, 1)}" text-anchor="${anchor}" dominant-baseline="central" font-family="${escapeXml(t.fontFamily || "DM Sans")}, sans-serif" font-weight="${t.fontWeight || 600}" font-size="${t.fontSize || 48}" letter-spacing="${t.letterSpacing || 0}" fill="${textFill}" ${inst.outlineWidth > 0 ? `stroke="${inst.outlineColor || "#141619"}" stroke-width="${inst.outlineWidth}"` : ""}>${escapeXml(t.value || "")}</text>`);
        } else {
          const markup = buildSvgMotifMarkup(
            inst,
            inst.color || "#e8c27a",
            inst.outlineColor || inst.color || "#141619",
            inst.outlineWidth || 0,
            gradDefs,
            `${idPrefix}-${instSeq++}`,
            { px, py, size, seamless: doc.mode === "pattern", pointRes },
          );
          parts.push(`<g transform="${xf}" opacity="${clamp(inst.opacity, 0, 1)}">${markup}</g>`);
        }
      }
    }
    groups.push(`<g data-layer-id="${escapeXml(layer.id)}" opacity="${clamp(layer.opacity ?? 1, 0, 1)}" style="mix-blend-mode:${(layer.blend || "Normal").toLowerCase()};cursor:${layer.locked ? "default" : "move"}">${parts.join("")}</g>`);
  }
  return { defs: gradDefs.join(""), body: groups.join("") };
}

export function artworkDocumentBackgroundSvg(doc, { id = "doc-bg-grad", pointRes = 128 } = {}) {
  const size = doc.size || ARTWORK_SIZE;
  const bg = doc.background || {};
  if (!bg.enabled) return { defs: "", rect: "" };
  const extraDefs = [];
  let bgFill = bg.color || "#171a1f";
  if (bg.fillMode === "point") {
    pushSvgPointGradientPatternDef(extraDefs, id, bg.fillPointGradient, {
      size,
      seamless: doc.mode === "pattern",
      res: pointRes,
    });
    bgFill = `url(#${id})`;
  } else if (bg.fillMode === "linear" || bg.fillMode === "radial") {
    pushSvgGradientDef(extraDefs, id, bg.fillMode, bg.fillStops, bg.gradientAngle ?? 45, bg.color || "#171a1f", bg.colorB || "#2b3340");
    bgFill = `url(#${id})`;
  }
  const rect = `<rect x="0" y="0" width="${size}" height="${size}" fill="${bgFill}" opacity="${clamp(bg.opacity ?? 1, 0, 1)}"/>`;
  return { defs: extraDefs.join(""), rect };
}

export function artworkDocumentToSvgString(doc, { width = 512, height = 512, showBg = true } = {}) {
  const size = doc.size || ARTWORK_SIZE;
  const { defs, body } = artworkDocumentLayersSvg(doc);
  const bg = showBg ? artworkDocumentBackgroundSvg(doc, { id: "doc-bg-grad" }) : { defs: "", rect: "" };
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${size} ${size}"><defs>${bg.defs}${defs}</defs>${bg.rect}${body}</svg>`;
}

