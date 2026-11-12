/*==========================================================================================================================================
                                                         TEXTUREPAINTENGINE.JS
==========================================================================================================================================*/
/* Clean, seam-free 3D + 2D UV multi-channel PBR painting, layer stack compositing, procedural generators, masks, and stamp projection. */

import * as THREE from "three";
import { TeapotGeometry } from "../Vendor/addons/TeapotGeometry.js";
import { RoundedBoxGeometry } from "../Vendor/addons/RoundedBoxGeometry.js";
import { createDrapedClothGeometry } from "./Materials/clothGeometry.js";
import { fbm3D } from "./NoiseField.js";
import { nextArtworkId } from "./Patterns/ArtworkStackEngine.js";
import { materials as MATERIAL_CATALOG, normalizeMaterial } from "./Materials/materials.js";

export const PAINT_DEFAULT_RES = 512;

export const PAINT_MESH_PRESETS = [
  { id: "teapot", label: "Utah Teapot", parts: ["Body", "Lid", "Handle", "Spout"] },
  { id: "shaderball", label: "Shader Ball", parts: ["Shell", "Core"] },
  { id: "box", label: "Rounded Box", parts: ["All Faces"] },
  { id: "sphere", label: "UV Sphere", parts: ["Surface"] },
  { id: "cloth", label: "Draped Cloth", parts: ["Fabric"] },
];
export const PAINT_MESH_LIST = PAINT_MESH_PRESETS;

export const PAINT_BLENDS = ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken"];

/**
 * Full 19-channel PBR channel registry aligned with Frontier MaterialSpecification.js.
 */
export const PAINT_PBR_CHANNELS = [
  { key: "albedo", label: "Albedo", short: "Albedo", sub: "Base Colour", kind: "albedo", dot: "#9ea2aa", type: "color", default: "#e8c27a", defaultVal: "#e8c27a", extended: false, isColor: true, min: 0, max: 1, step: 0.01 },
  { key: "roughness", label: "Roughness", short: "Rough", sub: "Surface Gloss", kind: "rough", dot: "#9ea2aa", type: "scalar", default: 0.34, defaultVal: 0.34, extended: false, isColor: false, min: 0.02, max: 1, step: 0.01 },
  { key: "metalness", label: "Metalness", short: "Metal", sub: "Conductor Mask", kind: "metal", dot: "#9ea2aa", type: "scalar", default: 0.12, defaultVal: 0.12, extended: false, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "height", label: "Height", short: "Height", sub: "Relief / Bump", kind: "height", dot: "#9ea2aa", type: "scalar", default: 0.25, defaultVal: 0.25, extended: false, isColor: false, min: -1, max: 1, step: 0.02 },
  { key: "normal", label: "Normal Str", short: "Normal", sub: "Bump Intensity", kind: "normal", dot: "#9ea2aa", type: "scalar", default: 0.85, defaultVal: 0.85, extended: true, isColor: false, min: 0, max: 2, step: 0.05 },
  { key: "ao", label: "Occlusion", short: "AO", sub: "Ambient Cavity", kind: "ao", dot: "#9ea2aa", type: "scalar", default: 1.0, defaultVal: 1.0, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "opacity", label: "Opacity", short: "Opacity", sub: "Cutout / Alpha", kind: "opacity", dot: "#9ea2aa", type: "scalar", default: 1.0, defaultVal: 1.0, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "emissive", label: "Emissive", short: "Emissive", sub: "Glow Colour", kind: "emissive", dot: "#9ea2aa", type: "color", default: "#ff7a3b", defaultVal: "#ff7a3b", extended: true, isColor: true, min: 0, max: 1, step: 0.01 },
  { key: "emissiveIntensity", label: "Glow Power", short: "Glow", sub: "Emission Gain", kind: "emissive", dot: "#9ea2aa", type: "scalar", default: 1.0, defaultVal: 1.0, extended: true, isColor: false, min: 0, max: 4, step: 0.05 },
  { key: "clearcoat", label: "Clearcoat", short: "Coat", sub: "Varnish Coat", kind: "coat", dot: "#9ea2aa", type: "scalar", default: 0.8, defaultVal: 0.8, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "coatRoughness", label: "Coat Rough", short: "CoatRgh", sub: "Varnish Gloss", kind: "coat", dot: "#9ea2aa", type: "scalar", default: 0.08, defaultVal: 0.08, extended: true, isColor: false, min: 0.01, max: 1, step: 0.01 },
  { key: "sheen", label: "Sheen", short: "Sheen", sub: "Fabric Velour", kind: "sheen", dot: "#9ea2aa", type: "scalar", default: 0.65, defaultVal: 0.65, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "sheenRoughness", label: "Sheen Rough", short: "SheenRgh", sub: "Microfiber Soft", kind: "sheen", dot: "#9ea2aa", type: "scalar", default: 0.45, defaultVal: 0.45, extended: true, isColor: false, min: 0.01, max: 1, step: 0.01 },
  { key: "specular", label: "Specular", short: "Spec", sub: "Dielectric F0", kind: "spec", dot: "#9ea2aa", type: "scalar", default: 1.0, defaultVal: 1.0, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "anisotropy", label: "Anisotropy", short: "Aniso", sub: "Brushed Grain", kind: "aniso", dot: "#9ea2aa", type: "scalar", default: 0.5, defaultVal: 0.5, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "iridescence", label: "Iridescence", short: "Irid", sub: "Thin-Film Pearl", kind: "irid", dot: "#9ea2aa", type: "scalar", default: 0.7, defaultVal: 0.7, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "iridescenceThickness", label: "Irid Film", short: "IridFilm", sub: "Spectrum Shift", kind: "irid", dot: "#9ea2aa", type: "scalar", default: 0.45, defaultVal: 0.45, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "transmission", label: "Transmission", short: "Trans", sub: "Glass / Jelly", kind: "trans", dot: "#9ea2aa", type: "scalar", default: 0.65, defaultVal: 0.65, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
  { key: "thickness", label: "Subsurface", short: "Depth", sub: "Depth / Scatter", kind: "trans", dot: "#9ea2aa", type: "scalar", default: 0.4, defaultVal: 0.4, extended: true, isColor: false, min: 0, max: 1, step: 0.01 },
];

export const PAINT_PBR_CHANNEL_ROWS = PAINT_PBR_CHANNELS.map((c) => [c.key, c.label, c.sub, c.kind, c.dot]);
export const PAINT_PBR_CHANNEL_MAP = Object.fromEntries(PAINT_PBR_CHANNELS.map((c) => [c.key, c]));
export const EXTENDED_PBR_KEYS = PAINT_PBR_CHANNELS.slice(4).map((c) => c.key);

export const EXTRA_COLOR_CHANNELS = [
  { key: "specularColor", label: "Spec Colour", short: "SpecCol", sub: "Specular Tint", kind: "spec", dot: "#9ea2aa", type: "color", default: "#ffffff", extended: true, isColor: true },
  { key: "sheenColor", label: "Sheen Colour", short: "SheenCol", sub: "Fabric Velour Tint", kind: "sheen", dot: "#9ea2aa", type: "color", default: "#ffffff", extended: true, isColor: true },
  { key: "attenuationColor", label: "Subsurf Tint", short: "SubCol", sub: "Volume Attenuation", kind: "trans", dot: "#9ea2aa", type: "color", default: "#ffffff", extended: true, isColor: true },
];
export const EXTRA_COLOR_KEYS = EXTRA_COLOR_CHANNELS.map((c) => c.key);
export const EXTRA_COLOR_CHANNEL_ROWS = EXTRA_COLOR_CHANNELS.map((c) => [c.key, c.label, c.sub, c.kind, c.dot]);
export const ALL_PAINT_CHANNEL_ROWS = [...PAINT_PBR_CHANNEL_ROWS, ...EXTRA_COLOR_CHANNEL_ROWS];

export function isRgbChannelKey(key) {
  return key === "emissive" || key === "specularColor" || key === "sheenColor" || key === "attenuationColor";
}

export function createDefaultChannelMap(includeAll = false, overrides = {}) {
  const out = {};
  for (const c of PAINT_PBR_CHANNELS) {
    out[c.key] = includeAll ? true : (c.key === "albedo" || c.key === "roughness" || c.key === "metalness" || c.key === "height");
  }
  for (const c of EXTRA_COLOR_CHANNELS) out[c.key] = !!includeAll;
  return { ...out, ...overrides };
}

export function createDefaultChannelOpacities(overrides = {}) {
  const out = {};
  for (const c of PAINT_PBR_CHANNELS) out[c.key] = 1;
  return { ...out, ...overrides };
}

export function createDefaultChannelBlends(overrides = {}) {
  const out = {};
  for (const c of PAINT_PBR_CHANNELS) out[c.key] = "Inherit";
  return { ...out, ...overrides };
}

export function createDefaultExtendedValues(overrides = {}) {
  return {
    normal: 0.85,
    ao: 1.0,
    opacity: 1.0,
    emissive: "#ff7a3b",
    emissiveIntensity: 1.0,
    clearcoat: 0.0,
    coatRoughness: 0.08,
    sheen: 0.0,
    sheenRoughness: 0.45,
    specular: 1.0,
    anisotropy: 0.0,
    iridescence: 0.0,
    iridescenceThickness: 0.45,
    transmission: 0.0,
    thickness: 0.0,
    ...overrides,
  };
}

export function encodeScalarChannelByte(key, val) {
  if (key === "height") return Math.round(clamp(val * 0.5 + 0.5, 0, 1) * 255);
  if (key === "normal") return Math.round(clamp(val / 2, 0, 1) * 255);
  if (key === "emissiveIntensity") return Math.round(clamp(val / 4, 0, 1) * 255);
  return Math.round(clamp(val, 0, 1) * 255);
}

export function defaultByteForExtChannel(key) {
  if (key === "ao" || key === "opacity" || key === "specular") return 255;
  if (key === "normal") return 108; // ~0.85
  if (key === "coatRoughness") return 20; // ~0.08
  if (key === "sheenRoughness") return 115; // ~0.45
  if (key === "iridescenceThickness") return 115;
  return 0;
}

function clamp(v, lo = 0, hi = 1) {
  return Math.max(lo, Math.min(hi, v));
}

function hexToRgb01(hex) {
  const m = /^#?([a-f\d]{6})$/i.exec(String(hex || "#e8c27a"));
  if (!m) return [0.91, 0.76, 0.48];
  return [
    parseInt(m[1].slice(0, 2), 16) / 255,
    parseInt(m[1].slice(2, 4), 16) / 255,
    parseInt(m[1].slice(4, 6), 16) / 255,
  ];
}

function rgb01ToHex(r, g, b) {
  const toH = (v) => Math.round(clamp(v, 0, 1) * 255).toString(16).padStart(2, "0");
  return `#${toH(r)}${toH(g)}${toH(b)}`;
}

export const CURVE_PRESETS = {
  linear: [[0, 0], [1, 1]],
  flat: [[0, 1], [1, 1]],
  ease: [[0, 0], [0.35, 0.14], [0.7, 0.82], [1, 1]],
  punch: [[0, 0], [0.22, 0.72], [0.62, 0.94], [1, 1]],
  scurve: [[0, 0], [0.28, 0.08], [0.72, 0.92], [1, 1]],
  bell: [[0, 0], [0.22, 0.86], [0.5, 1], [0.78, 0.86], [1, 0]],
  inverse: [[0, 1], [1, 0]],
};

export function sampleCurve(points, t) {
  const x = clamp(t, 0, 1);
  if (!Array.isArray(points) || points.length === 0) return x;
  if (points.length === 1) return clamp(points[0][1], 0, 1);
  const pts = [...points].sort((a, b) => a[0] - b[0]);
  if (x <= pts[0][0]) return clamp(pts[0][1], 0, 1);
  if (x >= pts[pts.length - 1][0]) return clamp(pts[pts.length - 1][1], 0, 1);
  for (let i = 0; i < pts.length - 1; i++) {
    const [x0, y0] = pts[i];
    const [x1, y1] = pts[i + 1];
    if (x >= x0 && x <= x1) {
      const span = Math.max(1e-5, x1 - x0);
      const u = (x - x0) / span;
      // Smooth Catmull-Rom tangent estimate for responsive spline shape
      const pPrev = pts[Math.max(0, i - 1)];
      const pNext = pts[Math.min(pts.length - 1, i + 2)];
      const m0 = ((y1 - pPrev[1]) / Math.max(1e-4, x1 - pPrev[0])) * span * 0.5;
      const m1 = ((pNext[1] - y0) / Math.max(1e-4, pNext[0] - x0)) * span * 0.5;
      const u2 = u * u;
      const u3 = u2 * u;
      const h00 = 2 * u3 - 3 * u2 + 1;
      const h10 = u3 - 2 * u2 + u;
      const h01 = -2 * u3 + 3 * u2;
      const h11 = u3 - u2;
      return clamp(h00 * y0 + h10 * m0 + h01 * y1 + h11 * m1, 0, 1);
    }
  }
  return clamp(pts[pts.length - 1][1], 0, 1);
}

export function sampleCurveLut(points, count = 17) {
  const out = new Array(count);
  for (let i = 0; i < count; i++) {
    out[i] = sampleCurve(points, count <= 1 ? 1 : i / (count - 1));
  }
  return out;
}

export function getToolChannels(brush, toolOverride = null) {
  const t = toolOverride || brush?.tool || "paint";
  if (t === "erase") return brush?.eraseChannels || brush?.channels || { albedo: true, roughness: true, metalness: true, height: true };
  if (t === "gradient") return brush?.gradientChannels || brush?.fillChannels || brush?.channels || { albedo: true, roughness: true, metalness: true, height: true };
  if (t === "fill") return brush?.fillChannels || brush?.channels || { albedo: true, roughness: true, metalness: true, height: true };
  return brush?.paintChannels || brush?.channels || { albedo: true, roughness: true, metalness: true, height: true };
}

function rgbToHsl(r, g, b) {
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  let h = 0, s = 0;
  const l = (max + min) * 0.5;
  if (max !== min) {
    const d = max - min;
    s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    if (max === r) h = ((g - b) / d + (g < b ? 6 : 0)) / 6;
    else if (max === g) h = ((b - r) / d + 2) / 6;
    else h = ((r - g) / d + 4) / 6;
  }
  return [h, s, l];
}

function hslToRgb(h, s, l) {
  const hue = ((h % 1) + 1) % 1;
  if (s <= 1e-4) return [l, l, l];
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  const tc = (t) => {
    const x = ((t % 1) + 1) % 1;
    if (x < 1 / 6) return p + (q - p) * 6 * x;
    if (x < 0.5) return q;
    if (x < 2 / 3) return p + (q - p) * (2 / 3 - x) * 6;
    return p;
  };
  return [tc(hue + 1 / 3), tc(hue), tc(hue - 1 / 3)];
}

export function shiftHslHex(hex, hDelta = 0, sDelta = 0, lDelta = 0) {
  const [r, g, b] = hexToRgb01(hex);
  const [h, s, l] = rgbToHsl(r, g, b);
  const [nr, ng, nb] = hslToRgb(h + hDelta, clamp(s + sDelta, 0, 1), clamp(l + lDelta, 0, 1));
  return rgb01ToHex(nr, ng, nb);
}

function prngHash(seed, salt = 0) {
  let x = (Math.imul(Math.floor(seed) | 0, 374761393) + Math.imul(salt | 0, 668265263)) | 0;
  x = Math.imul(x ^ (x >>> 13), 1274126177);
  return ((x ^ (x >>> 16)) >>> 0) / 4294967295;
}

export const COLOUR_DYNAMIC_MODES = [
  { id: "solid", label: "Solid + H/S/V Jitter", sub: "Base ink with HSL jitter & hue curve", dot: "#9ea2aa" },
  { id: "stroke-gradient", label: "Stroke Gradient", sub: "Smooth multi-stop colour ramp along stroke", dot: "#9ea2aa" },
  { id: "multicolour", label: "Multicolour Scatter", sub: "Random swatch selection per dab", dot: "#9ea2aa" },
  { id: "multicolour-cycle", label: "Multicolour Cycle", sub: "Sequential palette stepping along stroke", dot: "#9ea2aa" },
  { id: "temperature", label: "Warm ↔ Cool Split", sub: "Alternating painterly temperature contrast", dot: "#9ea2aa" },
  { id: "rainbow", label: "Spectrum Sweep", sub: "Full 360° chromatic rotation per stroke", dot: "#9ea2aa" },
  { id: "velocity", label: "Speed & Velocity Shift", sub: "Hue & saturation react to stroke speed", dot: "#9ea2aa" },
];

export const OPACITY_DYNAMIC_MODES = [
  { id: "constant", label: "Constant + Jitter", sub: "Steady stroke with per-dab opacity jitter", dot: "#9ea2aa" },
  { id: "fade-out", label: "Fade Out Tail", sub: "Tapers smoothly toward stroke end", dot: "#9ea2aa" },
  { id: "fade-in", label: "Fade In Entry", sub: "Soft entry ramping up along stroke", dot: "#9ea2aa" },
  { id: "bell", label: "Soft Entry & Exit", sub: "Tapered both ends of stroke", dot: "#9ea2aa" },
  { id: "pulse", label: "Opacity Pulse Wave", sub: "Rhythmic opacity wave along path", dot: "#9ea2aa" },
  { id: "velocity", label: "Speed Reactive Opacity", sub: "Stroke velocity modulates dab opacity", dot: "#9ea2aa" },
  { id: "curve", label: "Opacity Curve Driven", sub: "Uses Opacity Over Stroke spline", dot: "#9ea2aa" },
];

export const COLOUR_GRADIENT_PRESETS = [
  { id: "ember", label: "Ember Glow", sub: "Hot coral to golden cream", stops: ["#ff3d2e", "#ff9d3b", "#ffe38e"] },
  { id: "oceanic", label: "Oceanic Mint", sub: "Deep cobalt to seafoam", stops: ["#0d4f8b", "#2fe6a7", "#c4fff0"] },
  { id: "iridescent", label: "Iridescent Oil", sub: "Violet, mint, coral & amber", stops: ["#7b2cbf", "#2fe6a7", "#ff6d6d", "#ffd166"] },
  { id: "sunset", label: "Sunset Neon", sub: "Magenta into luminous gold", stops: ["#ff2a85", "#ff8c42", "#ffe66d"] },
  { id: "bronze", label: "Oxidized Bronze", sub: "Verdigris patina to warm copper", stops: ["#2d5a4c", "#5ca4a9", "#d49b54"] },
  { id: "terracotta", label: "Earth & Indigo", sub: "Terracotta ochre, indigo & ivory", stops: ["#8f321e", "#e8a838", "#234a6b", "#f4ecd8"] },
  { id: "aurora", label: "Arctic Aurora", sub: "Midnight blue through cyan & lime", stops: ["#1e2a78", "#38b6ff", "#2fe6a7", "#f2ff9f"] },
  { id: "magma", label: "Volcanic Magma", sub: "Charcoal ember to white-hot gold", stops: ["#2a0a0d", "#a61c1c", "#ff6d28", "#fff08a"] },
  { id: "patina", label: "Gold Foil Patina", sub: "Antique brass to mineral jade", stops: ["#78591c", "#e8c27a", "#fff3cb", "#4c7c6f"] },
  { id: "cyber", label: "Cyber Berry", sub: "Neon magenta, violet & emerald", stops: ["#ff4ecd", "#7f5af0", "#2cb67d"] },
];

export function sampleHexGradient(stops, u = 0) {
  const list = Array.isArray(stops) && stops.length ? stops : ["#e8c27a", "#2fe6a7"];
  if (list.length === 1) return list[0];
  const t = clamp(u, 0, 1) * (list.length - 1);
  const idx = Math.min(list.length - 2, Math.floor(t));
  const f = t - idx;
  const [r0, g0, b0] = hexToRgb01(list[idx]);
  const [r1, g1, b1] = hexToRgb01(list[idx + 1]);
  return rgb01ToHex(r0 + (r1 - r0) * f, g0 + (g1 - g0) * f, b0 + (b1 - b0) * f);
}

/**
 * Resolve per-dab brush geometry, Colour Dynamics (Stroke Gradients, Multicolour, Temperature,
 * Rainbow, Velocity), Opacity Dynamics (Fade, Pulse, Velocity, Opacity Jitter) & Stroke Curves.
 */
export function resolveDabVariation(brush, strokeT = 0, seed = 1, followAngleDeg = null, strokeMeta = {}) {
  const j = brush.jitter || {};
  const cd = brush.colourDynamics || {};
  const curves = brush.curves || {};

  const t = clamp(strokeT, 0, 1);
  const dabIdx = Math.max(0, Number(strokeMeta.dabIndex ?? Math.round(t * 48)));
  const speed = clamp(Number(strokeMeta.speed ?? 0.35), 0, 1);
  const gradLen = Math.max(4, Number(cd.gradientLength ?? 36));
  const wrap = cd.gradientWrap || "pingpong";

  let uWrap = clamp(dabIdx / gradLen, 0, 1);
  if (wrap === "loop") uWrap = (dabIdx % gradLen) / gradLen;
  else if (wrap === "pingpong") {
    const cy = (dabIdx / gradLen) % 2;
    uWrap = cy <= 1 ? cy : 2 - cy;
  }

  const sizeCurve = curves.sizeOverStroke ? sampleCurve(curves.sizeOverStroke, t) : 1;
  const flowCurve = curves.opacityOverStroke ? sampleCurve(curves.opacityOverStroke, t) : 1;
  const hueCurveVal = curves.hueOverStroke ? sampleCurve(curves.hueOverStroke, t) : 0.5;

  const rSize = prngHash(seed, 1) * 2 - 1;
  const rAngle = prngHash(seed, 2) * 2 - 1;
  const rScatA = prngHash(seed, 3) * Math.PI * 2;
  const rScatR = Math.sqrt(prngHash(seed, 4));
  const rFlow = prngHash(seed, 5);
  const rHue = prngHash(seed, 6) * 2 - 1;
  const rSat = prngHash(seed, 7) * 2 - 1;
  const rVal = prngHash(seed, 8) * 2 - 1;
  const rPal = prngHash(seed, 9);
  const rOp = prngHash(seed, 10);

  const sizeScale = clamp(sizeCurve * (1 + rSize * (j.size || 0) * 0.75), 0.12, 2.4);

  // Opacity Dynamics (changing opacity along stroke / pulse / velocity / jitter)
  const opMode = cd.opacityMode || "constant";
  const opMin = clamp(cd.opacityMin ?? 0.18, 0.02, 1.0);
  let opMod = 1.0;
  if (opMode === "fade-out") opMod = 1 - (1 - opMin) * clamp(t, 0, 1);
  else if (opMode === "fade-in") opMod = opMin + (1 - opMin) * clamp(t, 0, 1);
  else if (opMode === "bell") opMod = opMin + (1 - opMin) * Math.sin(clamp(t, 0, 1) * Math.PI);
  else if (opMode === "pulse") {
    const freq = clamp(cd.opacityPulseFreq ?? 0.45, 0.08, 2.0);
    opMod = opMin + (1 - opMin) * (0.5 + 0.5 * Math.cos(dabIdx * freq));
  } else if (opMode === "velocity") {
    opMod = 1 - (1 - opMin) * clamp(speed, 0, 1);
  }
  const opJitterMul = 1 - rOp * clamp(cd.opacityJitter || 0, 0, 0.95);
  const flowScale = clamp(flowCurve * opMod * opJitterMul * (1 - rFlow * (j.flow || 0)), 0.02, 1.0);

  const baseAngle = (brush.angle || 0) + (followAngleDeg !== null && brush.followDirection ? followAngleDeg : 0);
  const angle = baseAngle + rAngle * (j.angle || 0);
  const scatterMag = rScatR * (j.scatter || 0);
  const scatterU = Math.cos(rScatA) * scatterMag;
  const scatterV = Math.sin(rScatA) * scatterMag;

  // Functional Base Colour resolution
  const mode = cd.mode || (cd.strokeGradient ? "stroke-gradient" : "solid");
  const stops = Array.isArray(cd.gradientStops) && cd.gradientStops.length >= 2
    ? cd.gradientStops
    : ["#ff3d2e", "#ff9d3b", "#ffe38e"];
  const palette = Array.isArray(cd.paletteColors) && cd.paletteColors.length >= 2
    ? cd.paletteColors
    : ["#ff3d2e", "#2fe6a7", "#63a8ff", "#ffd166"];

  let baseHex = brush.color || "#e8c27a";
  if (mode === "stroke-gradient") {
    baseHex = sampleHexGradient(stops, uWrap);
  } else if (mode === "multicolour") {
    const pIdx = Math.min(palette.length - 1, Math.floor(rPal * palette.length));
    baseHex = palette[pIdx];
  } else if (mode === "multicolour-cycle") {
    const pIdx = dabIdx % palette.length;
    baseHex = palette[pIdx];
  } else if (mode === "rainbow") {
    baseHex = shiftHslHex(baseHex, uWrap, 0.14, 0.02);
  } else if (mode === "temperature") {
    const warm = (dabIdx % 2 === 0 ? 1 : -1) * clamp(cd.temperatureShift ?? 0.35, 0, 1);
    baseHex = shiftHslHex(baseHex, warm * 0.11, warm * 0.15, warm * 0.09);
  } else if (mode === "velocity") {
    baseHex = shiftHslHex(baseHex, (speed - 0.35) * 0.42, (speed - 0.3) * 0.35, (speed - 0.5) * 0.16);
  }

  const hCurveShift = cd.strokeGradient ? (hueCurveVal - 0.5) * 1.0 : 0;
  const hDelta = hCurveShift + rHue * (cd.hueJitter || 0) * 0.5;
  const sDelta = rSat * (cd.satJitter || 0) * 0.75;
  const vDelta = rVal * (cd.valJitter || 0) * 0.65;
  const color = (hDelta !== 0 || sDelta !== 0 || vDelta !== 0)
    ? shiftHslHex(baseHex, hDelta, sDelta, vDelta)
    : baseHex;

  return {
    ...brush,
    color,
    angle,
    flow: clamp((brush.flow ?? 0.78) * flowScale, 0.02, 1.0),
    sizeScale,
    scatterU,
    scatterV,
  };
}

/**
 * Convert raw RGBA pixels from an uploaded PNG/JPG/WebP/SVG into multi-channel surface maps
 * (useful both as a Stamp Decal, Custom Brush Alpha Tip, or Fill Texture).
 */
export function rgbaPixelsToSurfaceMaps(rgba, width, heightPx, { name = "Uploaded Image", previewDataUrl = "" } = {}) {
  const n = width * heightPx;
  const albedo = new Uint8ClampedArray(n * 4);
  const tipAlpha = new Uint8ClampedArray(n * 4);
  const roughness = new Uint8ClampedArray(n * 4);
  const metalness = new Uint8ClampedArray(n * 4);
  const height = new Uint8ClampedArray(n * 4);

  let hasAlphaVariance = false;
  for (let i = 3; i < rgba.length; i += 4) {
    if (rgba[i] < 245) { hasAlphaVariance = true; break; }
  }

  for (let i = 0; i < n; i++) {
    const i4 = i * 4;
    const r = rgba[i4], g = rgba[i4 + 1], b = rgba[i4 + 2], a = rgba[i4 + 3];
    const lum = Math.round(r * 0.299 + g * 0.587 + b * 0.114);
    const alphaVal = hasAlphaVariance ? a : lum;

    albedo[i4] = r; albedo[i4 + 1] = g; albedo[i4 + 2] = b; albedo[i4 + 3] = a;
    tipAlpha[i4] = 255; tipAlpha[i4 + 1] = 255; tipAlpha[i4 + 2] = 255; tipAlpha[i4 + 3] = alphaVal;

    const rVal = clamp(Math.round(170 - lum * 0.45), 16, 245);
    roughness[i4] = rVal; roughness[i4 + 1] = rVal; roughness[i4 + 2] = rVal; roughness[i4 + 3] = 255;
    metalness[i4] = 18; metalness[i4 + 1] = 18; metalness[i4 + 2] = 18; metalness[i4 + 3] = 255;
    const hVal = clamp(Math.round(128 + (lum - 128) * 0.7), 0, 255);
    height[i4] = hVal; height[i4 + 1] = hVal; height[i4 + 2] = hVal; height[i4 + 3] = 255;
  }

  return {
    name,
    width,
    heightPx,
    albedo,
    tipAlpha,
    roughness,
    metalness,
    height,
    previewDataUrl,
    isCustomImage: true,
  };
}

/**
 * Build a clean non-overlapping [0..1]x[0..1] UV geometry for each target mesh,
 * tagging vertices with `surfacePart` (0..N-1) and UV island `chartId` so both
 * 3D painting, 2D UV painting, flood-fill, and High/Low baking work seamlessly together.
 */
export function buildPaintTargetGeometry(meshId = "teapot", customGeometry = null) {
  let geo;
  let partNames = ["Surface"];

  if (customGeometry) {
    geo = customGeometry.clone();
    geo.computeBoundingBox();
    const box = geo.boundingBox;
    const center = box.getCenter(new THREE.Vector3());
    const size = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z, 1e-4);
    const scale = 2.4 / maxDim;
    geo.translate(-center.x, -center.y, -center.z);
    geo.scale(scale, scale, scale);
    if (!geo.attributes.normal) geo.computeVertexNormals();
    if (!geo.attributes.uv) generateAutoBoxUVs(geo);
    clampAndSanitizeUVs(geo);
    if (geo.attributes.surfacePart && Array.isArray(customGeometry.userData?.partNames) && customGeometry.userData.partNames.length) {
      partNames = [...customGeometry.userData.partNames];
    } else {
      partNames = ["Imported Mesh"];
      const count = geo.attributes.position.count;
      geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(new Float32Array(count), 1));
    }
  } else if (meshId === "teapot") {
    const seg = 16;
    geo = new TeapotGeometry(1.0, seg);
    const uv = geo.attributes.uv;
    const countPerPatch = (seg + 1) ** 2;
    const partAttr = new Float32Array(uv.count);
    partNames = ["Body", "Lid", "Handle", "Spout"];

    // Pack the 4 named parts cleanly into 4 quadrants inside [0..1]x[0..1]
    // Quadrant layout: Body (0,0), Lid (0.5,0), Handle (0,0.5), Spout (0.5,0.5)
    for (let i = 0; i < uv.count; i++) {
      const patch = Math.floor(i / countPerPatch);
      let partIdx = 0;
      let local = 0;
      let cols = 4;
      let rows = 4;

      if (patch >= 20 && patch < 28) {
        partIdx = 1; // Lid (8 patches -> 4x2)
        local = patch - 20;
        cols = 4;
        rows = 2;
      } else if (patch >= 12 && patch < 16) {
        partIdx = 2; // Handle (4 patches -> 2x2)
        local = patch - 12;
        cols = 2;
        rows = 2;
      } else if (patch >= 16 && patch < 20) {
        partIdx = 3; // Spout (4 patches -> 2x2)
        local = patch - 16;
        cols = 2;
        rows = 2;
      } else {
        partIdx = 0; // Body (patches 0..11, 28..31 -> 16 patches -> 4x4)
        local = patch < 12 ? patch : patch - 16;
        cols = 4;
        rows = 4;
      }

      const qx = (partIdx % 2) * 0.5;
      const qy = Math.floor(partIdx / 2) * 0.5;
      const g = 0.06;
      const cellW = 0.48 / cols;
      const cellH = 0.48 / rows;
      const lu = (local % cols) * cellW + 0.01;
      const lv = Math.floor(local / cols) * cellH + 0.01;
      const u = qx + lu + (g + uv.getX(i) * (1 - 2 * g)) * cellW;
      const v = qy + lv + (g + uv.getY(i) * (1 - 2 * g)) * cellH;
      uv.setXY(i, u, v);
      partAttr[i] = partIdx;
    }
    uv.needsUpdate = true;
    geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(partAttr, 1));
  } else if (meshId === "shaderball") {
    const pts = [];
    for (let i = 0; i <= 180; i++) {
      const a = (Math.PI * i) / 180;
      const y = 1.25 * Math.cos(a);
      let r = 1.25 * Math.sin(a);
      const groove = (c, w, d) => d * Math.exp(-Math.pow((y - c) / w, 6));
      r -= groove(0.58, 0.02, 0.032) + groove(0.5, 0.015, 0.024);
      r -= groove(-0.68, 0.018, 0.028) + groove(-0.78, 0.018, 0.028);
      pts.push(new THREE.Vector2(Math.max(0.002, r), y));
    }
    geo = new THREE.LatheGeometry(pts.reverse(), 96);
    clampAndSanitizeUVs(geo);
    const pos = geo.attributes.position;
    const partAttr = new Float32Array(pos.count);
    for (let i = 0; i < pos.count; i++) {
      partAttr[i] = pos.getY(i) < -0.62 ? 1 : 0;
    }
    partNames = ["Upper Shell", "Base Band"];
    geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(partAttr, 1));
  } else if (meshId === "box") {
    geo = new RoundedBoxGeometry(1.9, 1.9, 1.9, 4, 0.14);
    generateAutoBoxUVs(geo);
    const count = geo.attributes.position.count;
    geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(new Float32Array(count), 1));
    partNames = ["All Faces"];
  } else if (meshId === "cloth") {
    geo = createDrapedClothGeometry();
    geo.scale(0.85, 0.85, 0.85);
    clampAndSanitizeUVs(geo);
    const count = geo.attributes.position.count;
    geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(new Float32Array(count), 1));
    partNames = ["Fabric"];
  } else {
    geo = new THREE.SphereGeometry(1.22, 64, 48);
    clampAndSanitizeUVs(geo);
    const count = geo.attributes.position.count;
    geo.setAttribute("surfacePart", new THREE.Float32BufferAttribute(new Float32Array(count), 1));
    partNames = ["Surface"];
  }

  geo.computeBoundingBox();
  geo.computeBoundingSphere();
  geo.userData.partNames = partNames;
  return geo;
}

function clampAndSanitizeUVs(geo) {
  const uv = geo.attributes.uv;
  if (!uv) return;
  for (let i = 0; i < uv.count; i++) {
    let u = uv.getX(i);
    let v = uv.getY(i);
    if (!Number.isFinite(u)) u = 0.5;
    if (!Number.isFinite(v)) v = 0.5;
    if (u > 1.0001) u = u - Math.floor(u - 1e-5);
    else if (u < -0.0001) u = u - Math.floor(u);
    if (v > 1.0001) v = v - Math.floor(v - 1e-5);
    else if (v < -0.0001) v = v - Math.floor(v);
    u = Math.max(0.002, Math.min(0.998, u));
    v = Math.max(0.002, Math.min(0.998, v));
    uv.setXY(i, u, v);
  }
  uv.needsUpdate = true;
}

function generateAutoBoxUVs(geo) {
  const pos = geo.attributes.position;
  const norm = geo.attributes.normal;
  const uvs = new Float32Array(pos.count * 2);
  geo.computeBoundingBox();
  const min = geo.boundingBox.min;
  const size = geo.boundingBox.getSize(new THREE.Vector3());
  const sx = Math.max(size.x, 1e-4), sy = Math.max(size.y, 1e-4), sz = Math.max(size.z, 1e-4);

  // Pack 6 box projections cleanly into 3x2 cells inside [0..1]x[0..1]
  for (let i = 0; i < pos.count; i++) {
    const nx = norm.getX(i), ny = norm.getY(i), nz = norm.getZ(i);
    const ax = Math.abs(nx), ay = Math.abs(ny), az = Math.abs(nz);
    const px = (pos.getX(i) - min.x) / sx;
    const py = (pos.getY(i) - min.y) / sy;
    const pz = (pos.getZ(i) - min.z) / sz;

    let cell = 0, u = 0, v = 0;
    if (ax >= ay && ax >= az) {
      cell = nx >= 0 ? 0 : 1;
      u = pz; v = py;
    } else if (ay >= ax && ay >= az) {
      cell = ny >= 0 ? 2 : 3;
      u = px; v = pz;
    } else {
      cell = nz >= 0 ? 4 : 5;
      u = px; v = py;
    }
    const cx = cell % 3;
    const cy = Math.floor(cell / 3);
    const pad = 0.05;
    uvs[i * 2] = (cx + pad + clamp(u, 0, 1) * (1 - 2 * pad)) / 3;
    uvs[i * 2 + 1] = (cy + pad + clamp(v, 0, 1) * (1 - 2 * pad)) / 2;
  }
  geo.setAttribute("uv", new THREE.BufferAttribute(uvs, 2));
}

/**
 * Spatial & UV index for fast 3D stroke projection, flood fill, and generator map caching.
 */
export function buildMeshPaintIndex(geo) {
  const pos = geo.attributes.position;
  const norm = geo.attributes.normal;
  const uv = geo.attributes.uv;
  const partAttr = geo.attributes.surfacePart;
  const idx = geo.index ? geo.index.array : null;
  const triCount = idx ? idx.length / 3 : pos.count / 3;

  const tris = new Array(triCount);
  // UV island clustering by shared UV vertices
  const parent = new Int32Array(triCount);
  const edgeToTri = new Map();
  const find = (a) => {
    while (parent[a] !== a) {
      parent[a] = parent[parent[a]];
      a = parent[a];
    }
    return a;
  };

  for (let t = 0; t < triCount; t++) {
    parent[t] = t;
    const i0 = idx ? idx[t * 3] : t * 3;
    const i1 = idx ? idx[t * 3 + 1] : t * 3 + 1;
    const i2 = idx ? idx[t * 3 + 2] : t * 3 + 2;

    const p0 = [pos.getX(i0), pos.getY(i0), pos.getZ(i0)];
    const p1 = [pos.getX(i1), pos.getY(i1), pos.getZ(i1)];
    const p2 = [pos.getX(i2), pos.getY(i2), pos.getZ(i2)];

    const n0 = [norm.getX(i0), norm.getY(i0), norm.getZ(i0)];
    const n1 = [norm.getX(i1), norm.getY(i1), norm.getZ(i1)];
    const n2 = [norm.getX(i2), norm.getY(i2), norm.getZ(i2)];

    const uv0 = [uv.getX(i0), uv.getY(i0)];
    const uv1 = [uv.getX(i1), uv.getY(i1)];
    const uv2 = [uv.getX(i2), uv.getY(i2)];

    const cx = (p0[0] + p1[0] + p2[0]) / 3;
    const cy = (p0[1] + p1[1] + p2[1]) / 3;
    const cz = (p0[2] + p1[2] + p2[2]) / 3;
    const r = Math.max(
      Math.hypot(p0[0] - cx, p0[1] - cy, p0[2] - cz),
      Math.hypot(p1[0] - cx, p1[1] - cy, p1[2] - cz),
      Math.hypot(p2[0] - cx, p2[1] - cy, p2[2] - cz),
    );

    const e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
    const e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
    let fnx = e1y * e2z - e1z * e2y;
    let fny = e1z * e2x - e1x * e2z;
    let fnz = e1x * e2y - e1y * e2x;
    const fl = Math.hypot(fnx, fny, fnz) || 1;
    fnx /= fl; fny /= fl; fnz /= fl;

    const minU = Math.min(uv0[0], uv1[0], uv2[0]);
    const maxU = Math.max(uv0[0], uv1[0], uv2[0]);
    const minV = Math.min(uv0[1], uv1[1], uv2[1]);
    const maxV = Math.max(uv0[1], uv1[1], uv2[1]);

    tris[t] = {
      t,
      p0, p1, p2,
      n0, n1, n2,
      uv0, uv1, uv2,
      center: [cx, cy, cz],
      radius: r,
      faceNormal: [fnx, fny, fnz],
      minU, maxU, minV, maxV,
      part: partAttr ? Math.round(partAttr.getX(i0)) : 0,
      island: 0,
    };

    const ukeys = [
      `${Math.round(uv0[0] * 10000)},${Math.round(uv0[1] * 10000)}`,
      `${Math.round(uv1[0] * 10000)},${Math.round(uv1[1] * 10000)}`,
      `${Math.round(uv2[0] * 10000)},${Math.round(uv2[1] * 10000)}`,
    ];
    for (let e = 0; e < 3; e++) {
      const a = ukeys[e], b = ukeys[(e + 1) % 3];
      const ek = a < b ? `${a}|${b}` : `${b}|${a}`;
      if (edgeToTri.has(ek)) {
        parent[find(t)] = find(edgeToTri.get(ek));
      } else {
        edgeToTri.set(ek, t);
      }
    }
  }

  const islandMap = new Map();
  for (let t = 0; t < triCount; t++) {
    const root = find(t);
    if (!islandMap.has(root)) islandMap.set(root, islandMap.size);
    tris[t].island = islandMap.get(root);
  }

  return { tris, triCount, islandCount: islandMap.size, partNames: geo.userData.partNames || ["Surface"] };
}

/**
 * Create a per-layer pixel buffer set for Paint layers & Masks.
 */
export function createLayerPixelBuffer(res) {
  const n = res * res;
  return {
    res,
    albedo: new Uint8ClampedArray(n * 4), // RGBA 0..255 (alpha = Albedo coverage)
    roughness: new Uint8ClampedArray(n).fill(140),
    metalness: new Uint8ClampedArray(n).fill(0),
    height: new Uint8ClampedArray(n).fill(128), // 128 = 0 displacement
    roughAlpha: new Uint8ClampedArray(n),
    metalAlpha: new Uint8ClampedArray(n),
    heightAlpha: new Uint8ClampedArray(n),
    extValues: {}, // Lazily populated per extended PBR key
    extAlphas: {}, // Lazily populated coverage alpha 0..255 per extended key
  };
}

export function ensureExtPixelChannel(px, key, resOverride = null) {
  const res = resOverride || px.res;
  const n = res * res;
  if (!px.extValues) px.extValues = {};
  if (!px.extAlphas) px.extAlphas = {};
  if (!px.extValues[key]) {
    if (isRgbChannelKey(key)) {
      px.extValues[key] = new Uint8ClampedArray(n * 3);
    } else {
      px.extValues[key] = new Uint8ClampedArray(n).fill(defaultByteForExtChannel(key));
    }
  }
  if (!px.extAlphas[key]) px.extAlphas[key] = new Uint8ClampedArray(n);
  return { values: px.extValues[key], alpha: px.extAlphas[key] };
}

export function createMaskBuffer(res, fillValue = 255) {
  return new Uint8ClampedArray(res * res).fill(fillValue);
}

/**
 * Look up 3D world position and normal at UV coordinate (u, v) from baked/geometric cues,
 * falling back to nearest covered texel so moving a 3D stamp in the 2D UV canvas stays anchored on mesh geometry.
 */
export function lookupSurfaceAtUv(cues, u, v) {
  if (!cues?.res) return { pos: [0, 0, 0], normal: [0, 1, 0], anchorPos: [0, 0, 0], anchorNormal: [0, 1, 0], covered: false };
  const res = cues.res;
  const cx = Math.min(res - 1, Math.max(0, Math.round(u * (res - 1))));
  const cy = Math.min(res - 1, Math.max(0, Math.round((1 - v) * (res - 1))));
  const readIdx = (idx) => {
    const pos = [cues.worldPos[idx * 3], cues.worldPos[idx * 3 + 1], cues.worldPos[idx * 3 + 2]];
    const normal = [cues.worldNormal[idx * 3], cues.worldNormal[idx * 3 + 1], cues.worldNormal[idx * 3 + 2]];
    return { pos, normal, anchorPos: pos, anchorNormal: normal, covered: true };
  };
  const pid0 = cy * res + cx;
  if (cues.covered[pid0]) return readIdx(pid0);
  for (let rad = 1; rad <= 10; rad++) {
    for (let dy = -rad; dy <= rad; dy++) {
      for (let dx = -rad; dx <= rad; dx++) {
        if (Math.abs(dx) !== rad && Math.abs(dy) !== rad) continue;
        const nx = cx + dx, ny = cy + dy;
        if (nx < 0 || nx >= res || ny < 0 || ny >= res) continue;
        const pid = ny * res + nx;
        if (cues.covered[pid]) return readIdx(pid);
      }
    }
  }
  return { pos: [0, 0, 0], normal: [0, 1, 0], anchorPos: [0, 0, 0], anchorNormal: [0, 1, 0], covered: false };
}

/**
 * Create a live movable, scalable, rotatable Stamp instance (used either as a Decal layer or as a Subcomponent inside any layer).
 */
export function createStampInstance(overrides = {}) {
  return {
    id: overrides.id || nextArtworkId("stmp"),
    name: overrides.name || overrides.stampName || "Stamp Decal",
    visible: overrides.visible !== false,
    projection: overrides.projection || "uv", // "uv" | "3d"
    u: clamp(overrides.u ?? 0.5, 0, 1),
    v: clamp(overrides.v ?? 0.5, 0, 1),
    anchorPos: overrides.anchorPos ? [...overrides.anchorPos] : [0, 0.2, 1.0],
    anchorNormal: overrides.anchorNormal ? [...overrides.anchorNormal] : [0, 0, 1],
    scale: clamp(overrides.scale ?? 0.28, 0.03, 1.5),
    scaleX: clamp(overrides.scaleX ?? 1.0, 0.15, 3.0),
    scaleY: clamp(overrides.scaleY ?? 1.0, 0.15, 3.0),
    rotation: Number(overrides.rotation ?? 0),
    opacity: clamp(overrides.opacity ?? 1.0, 0, 1),
    blend: overrides.blend || "Normal",
    heightStrength: clamp(overrides.heightStrength ?? 0.5, -1, 1),
    tint: overrides.tint || "#ffffff",
    roughness: overrides.roughness ?? null,
    metalness: overrides.metalness ?? null,
    stampData: overrides.stampData || overrides.stampMaps || null,
    channels: overrides.channels || null,
    extValues: overrides.extValues ? { ...overrides.extValues } : null,
  };
}

/**
 * Generate a 0..255 procedural mask buffer from mesh geometric/baked cues ("edge-wear", "crevice-dirt", "top-dust", "triplanar-grunge").
 */
export function generateProceduralMask(preset, cues, opts = {}) {
  const res = cues.res;
  const n = res * res;
  const out = new Uint8ClampedArray(n);
  const thr = clamp(opts.threshold ?? 0.44, 0.02, 0.98);
  const soft = Math.max(0.02, opts.softness ?? 0.24);
  const nMix = clamp(opts.noiseMix ?? 0.35, 0, 1);
  const nScale = Math.max(0.5, opts.noiseScale ?? 6.0);
  const invert = !!opts.invert;

  for (let i = 0; i < n; i++) {
    let metric = 0.5;
    if (preset === "edge-wear") metric = clamp((cues.curvature[i] - 0.5) * 2, 0, 1);
    else if (preset === "crevice-dirt") metric = clamp(1 - cues.ao[i], 0, 1);
    else if (preset === "top-dust") metric = clamp(cues.worldNormal[i * 3 + 1], 0, 1);
    else if (preset === "triplanar-grunge") {
      metric = fbm3D(
        cues.worldPos[i * 3] * nScale * 0.7 + 11.3,
        cues.worldPos[i * 3 + 1] * nScale * 0.7 + 5.1,
        cues.worldPos[i * 3 + 2] * nScale * 0.7 + 23.7,
        3,
        307,
      );
    }
    const nv = fbm3D(cues.worldPos[i * 3] * nScale, cues.worldPos[i * 3 + 1] * nScale, cues.worldPos[i * 3 + 2] * nScale, 3, 131);
    const comb = metric * (1 - nMix) + nv * nMix;
    let m = clamp((comb - (thr - soft * 0.5)) / soft, 0, 1);
    if (invert) m = 1 - m;
    out[i] = Math.round(m * 255);
  }
  return out;
}

export function featherMaskPixels(pixels, res, passes = 2) {
  if (!pixels || res <= 2) return pixels;
  let cur = new Uint8ClampedArray(pixels);
  const nxt = new Uint8ClampedArray(pixels.length);
  for (let p = 0; p < passes; p++) {
    for (let y = 0; y < res; y++) {
      for (let x = 0; x < res; x++) {
        let sum = 0, cnt = 0;
        for (let dy = -1; dy <= 1; dy++) {
          const yy = clamp(y + dy, 0, res - 1);
          for (let dx = -1; dx <= 1; dx++) {
            const xx = clamp(x + dx, 0, res - 1);
            sum += cur[yy * res + xx];
            cnt++;
          }
        }
        nxt[y * res + x] = Math.round(sum / cnt);
      }
    }
    cur.set(nxt);
  }
  pixels.set(cur);
  return pixels;
}

export function createMaterialLayerOverridesFromPreset(m, res = PAINT_DEFAULT_RES) {
  const norm = normalizeMaterial(m || {});
  const src = norm;
  const filmNm = src.filmThickness ?? 380;
  const iridThickNorm = Number.isFinite(filmNm) ? Math.max(0, Math.min(1, filmNm > 2 ? filmNm / 1000 : filmNm)) : 0.45;
  const extVals = createDefaultExtendedValues({
    normal: src.normalStrength ?? 0.85,
    ao: src.aoIntensity ?? 1.0,
    opacity: src.opacity ?? 1.0,
    emissive: src.emissionColor || "#000000",
    emissiveIntensity: src.emissionStrength ?? 0.0,
    clearcoat: src.coat ?? 0.0,
    coatRoughness: src.coatRoughness ?? 0.08,
    sheen: src.sheen ?? 0.0,
    sheenRoughness: src.sheenRoughness ?? 0.45,
    sheenColor: src.sheenColor || "#ffffff",
    specular: src.specular ?? 1.0,
    specularColor: src.specularColor || "#ffffff",
    anisotropy: src.anisotropy ?? 0.0,
    iridescence: src.iridescence ?? 0.0,
    iridescenceThickness: iridThickNorm,
    transmission: src.translucency ?? src.transmission ?? 0.0,
    thickness: Math.max(0, Math.min(1, src.depth ?? 0.0)),
    attenuationColor: src.attenuationColor || "#ffffff",
  });
  const defGrainScale = src.flakeScale ? Math.max(4, Math.min(32, src.flakeScale * 22)) : (src.grainScale ?? 10);
  const defGrainStrength = src.flakes ? Math.max(0.08, Math.min(0.35, src.flakes * 0.22)) : (src.grainStrength ?? 0.08);
  return {
    res,
    name: src.name || "Material Layer",
    channels: createDefaultChannelMap(true),
    extValues: { ...extVals },
    material: {
      presetId: src.id || "racing-green",
      presetName: src.name || "Racing Green",
      presetCategory: src.category || "Material",
      presetLabel: src.label || "PBR",
      presetParams: norm,
      color: src.color || "#345f42",
      roughness: src.roughness ?? 0.24,
      metalness: src.metalness ?? 0.87,
      height: src.reliefHeight ?? 0.0,
      extValues: extVals,
      ...extVals,
      grainScale: defGrainScale,
      grainStrength: defGrainStrength,
      baseGrainScale: defGrainScale,
      baseGrainStrength: defGrainStrength,
    },
  };
}

export function resolveMaterialLayerSpec(layer) {
  const mat = layer?.material || {};
  const catalogPreset = MATERIAL_CATALOG.find((m) => m.id === mat.presetId) || null;
  const ev = mat.extValues || layer?.extValues || {};
  const merged = {
    ...(catalogPreset || {}),
    ...(mat.presetParams || {}),
    id: mat.presetId || catalogPreset?.id || "custom",
    name: mat.presetName || catalogPreset?.name || layer?.name || "Material",
    color: mat.color || catalogPreset?.color || "#345f42",
    roughness: mat.roughness ?? catalogPreset?.roughness ?? 0.24,
    metalness: mat.metalness ?? catalogPreset?.metalness ?? 0.87,
  };
  if (ev.clearcoat !== undefined) merged.coat = ev.clearcoat;
  if (ev.coatRoughness !== undefined) merged.coatRoughness = ev.coatRoughness;
  if (ev.sheen !== undefined) merged.sheen = ev.sheen;
  if (ev.sheenRoughness !== undefined) merged.sheenRoughness = ev.sheenRoughness;
  if (ev.sheenColor !== undefined) merged.sheenColor = ev.sheenColor;
  if (ev.specular !== undefined) merged.specular = ev.specular;
  if (ev.specularColor !== undefined) merged.specularColor = ev.specularColor;
  if (ev.anisotropy !== undefined) merged.anisotropy = ev.anisotropy;
  if (ev.iridescence !== undefined) merged.iridescence = ev.iridescence;
  if (ev.iridescenceThickness !== undefined) merged.filmThickness = Math.max(0, ev.iridescenceThickness * 1000);
  if (ev.transmission !== undefined) merged.translucency = ev.transmission;
  if (ev.emissive !== undefined) merged.emissionColor = ev.emissive;
  if (ev.emissiveIntensity !== undefined) merged.emissionStrength = ev.emissiveIntensity;
  if (ev.ao !== undefined) merged.aoIntensity = ev.ao;
  if (ev.opacity !== undefined) merged.opacity = ev.opacity;

  const baseScale = mat.baseGrainScale ?? (merged.flakeScale ? Math.max(4, Math.min(32, merged.flakeScale * 22)) : 10);
  if (mat.grainScale && baseScale > 0 && Math.abs(mat.grainScale - baseScale) > 0.05) {
    const ratio = mat.grainScale / baseScale;
    merged.flakeScale = (merged.flakeScale ?? 1) * ratio;
    merged.detailScale = (merged.detailScale ?? 10) * ratio;
  }
  const baseStr = mat.baseGrainStrength ?? (merged.flakes ? Math.max(0.08, Math.min(0.35, merged.flakes * 0.22)) : 0.08);
  if (mat.grainStrength !== undefined && baseStr > 0 && Math.abs(mat.grainStrength - baseStr) > 0.005) {
    const sRatio = mat.grainStrength / baseStr;
    if ((merged.type ?? 0) === 0) merged.flakes = Math.max(0, Math.min(1, (merged.flakes ?? 0.5) * sRatio));
    merged.grain = Math.max(0, Math.min(1, (merged.grain ?? 0.25) * sRatio));
  }
  return normalizeMaterial(merged);
}

export function createPaintLayer(kind = "paint", overrides = {}) {
  const res = overrides.res || PAINT_DEFAULT_RES;
  const id = overrides.id || nextArtworkId("tp");
  const baseDecal = {
    stampName: "Industrial Warning Seal",
    stampMaps: null, // { albedo, roughness, metalness, height, width, heightPx }
    anchorPos: [0, 0.2, 1.0],
    anchorNormal: [0, 0, 1],
    radius: 0.45,
    rotation: 0,
    ...(overrides.decal || {}),
  };
  let stamps = Array.isArray(overrides.stamps) ? overrides.stamps.map((s) => createStampInstance(s)) : [];
  if (kind === "decal" && stamps.length === 0) {
    stamps = [
      createStampInstance({
        name: baseDecal.stampName,
        projection: overrides.decal?.projection || (overrides.decal?.anchorPos ? "3d" : "uv"),
        u: overrides.decal?.u ?? 0.5,
        v: overrides.decal?.v ?? 0.5,
        anchorPos: baseDecal.anchorPos,
        anchorNormal: baseDecal.anchorNormal,
        scale: baseDecal.radius ?? 0.35,
        rotation: baseDecal.rotation ?? 0,
        stampData: baseDecal.stampMaps,
      }),
    ];
  }
  const defaultCh = createDefaultChannelMap(kind === "material", overrides.channels || {});
  return {
    id,
    kind, // "paint" | "fill" | "material" | "decal" | "generator" | "folder"
    name: overrides.name || (
      kind === "paint" ? "Paint Layer" :
      kind === "fill" ? "Fill Layer" :
      kind === "material" ? "Material Layer" :
      kind === "decal" ? "Stamp Layer" :
      kind === "generator" ? "Weathering Generator" : "Folder"
    ),
    parentId: overrides.parentId || null,
    visible: overrides.visible !== false,
    locked: overrides.locked === true,
    blend: overrides.blend || "Normal",
    opacity: clamp(overrides.opacity ?? 1, 0, 1),
    partFilter: overrides.partFilter ?? -1, // -1 = all parts, 0..N = specific part
    channels: defaultCh,
    channelOpacity: createDefaultChannelOpacities(overrides.channelOpacity || {}),
    channelBlends: createDefaultChannelBlends(overrides.channelBlends || {}),
    extValues: createDefaultExtendedValues(overrides.extValues || {}),
    fill: {
      color: "#c89f68",
      roughness: 0.45,
      metalness: 0.0,
      height: 0.0,
      ...createDefaultExtendedValues(),
      patternRepeats: 3,
      patternRotation: 0,
      patternHeightStrength: 1.0,
      patternData: null, // Optional Pattern Studio raster maps
      ...(overrides.fill || {}),
    },
    material: {
      presetId: "racing-green",
      presetName: "Racing Green",
      color: "#345f42",
      roughness: 0.24,
      metalness: 0.87,
      height: 0.0,
      ...createDefaultExtendedValues(),
      grainScale: 10,
      grainStrength: 0.08,
      ...(overrides.material || {}),
    },
    decal: baseDecal,
    stamps,
    selectedStampId: overrides.selectedStampId || stamps[0]?.id || null,
    generator: {
      preset: "edge-wear", // "edge-wear" | "crevice-dirt" | "top-dust" | "triplanar-grunge"
      color: "#cad4de",
      roughness: 0.28,
      metalness: 0.82,
      height: -0.15,
      ...createDefaultExtendedValues(),
      threshold: 0.42,
      softness: 0.24,
      noiseMix: 0.35,
      noiseScale: 6.0,
      invert: false,
      ...(overrides.generator || {}),
    },
    pixels: kind === "paint" ? (overrides.pixels || createLayerPixelBuffer(res)) : null,
    mask: overrides.mask ? { opacity: 1.0, invert: false, enabled: true, ...overrides.mask } : null,
  };
}

export function createPaintDocument(res = PAINT_DEFAULT_RES) {
  const baseFill = createPaintLayer("fill", {
    res,
    name: "Base Ceramic Coat",
    fill: { color: "#cfd2d6", roughness: 0.34, metalness: 0.06, height: 0.0 },
  });
  const paintTop = createPaintLayer("paint", {
    res,
    name: "Hand Painted Layer",
  });
  return {
    schema: "frontier.texture-paint.v2",
    name: "Teapot Paint",
    resolution: res,
    meshId: "teapot",
    layers: [baseFill, paintTop], // Bottom-to-top compositing order
    selectedLayerId: paintTop.id,
    editingMask: false,
    bakedMaps: null, // Optional { normal, ao, curvature, height, worldNormal, res }
  };
}

/**
 * Rasterize geometric/baked curvature, AO, and world-normal cues per UV texel so Generators work out-of-the-box
 * even before running an explicit High/Low bake, and automatically upgrade when real Baked Maps are attached!
 */
export function buildDefaultMeshCues(geo, meshIndex, res) {
  const n = res * res;
  const worldPos = new Float32Array(n * 3);
  const worldNormal = new Float32Array(n * 3);
  const curvature = new Float32Array(n).fill(0.5);
  const ao = new Float32Array(n).fill(0.85);
  const covered = new Uint8Array(n);
  const triAt = new Int32Array(n).fill(-1);
  const partAt = new Int16Array(n).fill(-1);
  const islandAt = new Int32Array(n).fill(-1);

  for (let tIdx = 0; tIdx < meshIndex.tris.length; tIdx++) {
    const tri = meshIndex.tris[tIdx];
    const x0 = tri.uv0[0] * res, y0 = (1 - tri.uv0[1]) * res;
    const x1 = tri.uv1[0] * res, y1 = (1 - tri.uv1[1]) * res;
    const x2 = tri.uv2[0] * res, y2 = (1 - tri.uv2[1]) * res;

    const minX = Math.max(0, Math.floor(Math.min(x0, x1, x2)) - 1);
    const maxX = Math.min(res - 1, Math.ceil(Math.max(x0, x1, x2)) + 1);
    const minY = Math.max(0, Math.floor(Math.min(y0, y1, y2)) + 0);
    const maxY = Math.min(res - 1, Math.ceil(Math.max(y0, y1, y2)) + 1);

    const denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (Math.abs(denom) < 1e-6) continue;

    // Estimate local edge sharpness from vertex normals vs face normal for crisp instant curvature
    const d0 = 1 - Math.abs(tri.n0[0] * tri.faceNormal[0] + tri.n0[1] * tri.faceNormal[1] + tri.n0[2] * tri.faceNormal[2]);
    const d1 = 1 - Math.abs(tri.n1[0] * tri.faceNormal[0] + tri.n1[1] * tri.faceNormal[1] + tri.n1[2] * tri.faceNormal[2]);
    const d2 = 1 - Math.abs(tri.n2[0] * tri.faceNormal[0] + tri.n2[1] * tri.faceNormal[1] + tri.n2[2] * tri.faceNormal[2]);

    for (let py = minY; py <= maxY; py++) {
      for (let px = minX; px <= maxX; px++) {
        const sx = px + 0.5, sy = py + 0.5;
        const w0 = ((y1 - y2) * (sx - x2) + (x2 - x1) * (sy - y2)) / denom;
        const w1 = ((y2 - y0) * (sx - x2) + (x0 - x2) * (sy - y2)) / denom;
        const w2 = 1 - w0 - w1;
        if (w0 < -0.05 || w1 < -0.05 || w2 < -0.05) continue;

        const pid = py * res + px;
        if (covered[pid] && (w0 < 0 || w1 < 0 || w2 < 0)) continue;

        covered[pid] = 1;
        triAt[pid] = tIdx;
        partAt[pid] = tri.part;
        islandAt[pid] = tri.island;

        const wx = w0 * tri.p0[0] + w1 * tri.p1[0] + w2 * tri.p2[0];
        const wy = w0 * tri.p0[1] + w1 * tri.p1[1] + w2 * tri.p2[1];
        const wz = w0 * tri.p0[2] + w1 * tri.p1[2] + w2 * tri.p2[2];
        worldPos[pid * 3] = wx;
        worldPos[pid * 3 + 1] = wy;
        worldPos[pid * 3 + 2] = wz;

        let nx = w0 * tri.n0[0] + w1 * tri.n1[0] + w2 * tri.n2[0];
        let ny = w0 * tri.n0[1] + w1 * tri.n1[1] + w2 * tri.n2[1];
        let nz = w0 * tri.n0[2] + w1 * tri.n1[2] + w2 * tri.n2[2];
        const nl = Math.hypot(nx, ny, nz) || 1;
        nx /= nl; ny /= nl; nz /= nl;
        worldNormal[pid * 3] = nx;
        worldNormal[pid * 3 + 1] = ny;
        worldNormal[pid * 3 + 2] = nz;

        const sharp = clamp((w0 * d0 + w1 * d1 + w2 * d2) * 3.5, 0, 0.45);
        curvature[pid] = clamp(0.5 + sharp + (ny > 0 ? 0.08 : -0.08));
        ao[pid] = clamp(0.65 + 0.35 * (ny * 0.5 + 0.5));
      }
    }
  }

  geo.computeBoundingSphere();
  const bs = geo.boundingSphere;
  const sphereCenter = bs ? [bs.center.x, bs.center.y, bs.center.z] : [0, 0, 0];
  const sphereRadius = Math.max(0.25, bs?.radius || 1.45);
  return { res, worldPos, worldNormal, curvature, ao, covered, triAt, partAt, islandAt, sphereCenter, sphereRadius };
}

/**
 * Evaluate tip shape alpha for Brush presets (Round, Airbrush, Chisel, Chalk, Splatter, Sponge).
 */
function sampleBrushTip(dxNorm, dyNorm, brush, px, py) {
  let u = dxNorm;
  let v = dyNorm;
  if (brush.angle) {
    const rad = (-brush.angle * Math.PI) / 180;
    const c = Math.cos(rad), s = Math.sin(rad);
    const ru = u * c - v * s;
    const rv = u * s + v * c;
    u = ru;
    v = rv;
  }
  const roundness = clamp(brush.roundness ?? 1.0, 0.15, 1.0);
  v /= roundness;

  const tip = brush.tip || "round";
  if (tip === "custom" && brush.customTipData) {
    const su = u * 0.5 + 0.5;
    const sv = v * 0.5 + 0.5;
    if (su < 0 || su > 1 || sv < 0 || sv > 1) return 0;
    const ct = brush.customTipData;
    const ctH = ct.heightPx || ct.width;
    const ix = Math.min(ct.width - 1, Math.max(0, Math.floor(su * ct.width)));
    const iy = Math.min(ctH - 1, Math.max(0, Math.floor(sv * ctH)));
    const buf = ct.tipAlpha || ct.albedo || ct.rgba;
    const si = (iy * ct.width + ix) * 4;
    const tipA = (buf[si + 3] ?? 255) / 255;
    const dist = Math.hypot(u, v);
    const edgeFall = dist > 1 ? 0 : sampleCurve(brush.curves?.falloff || [[0, 0], [0.5, 0.55], [1, 1]], 1 - dist);
    return clamp(tipA * (brush.curves?.falloff ? edgeFall : 1) * (brush.flow ?? 1), 0, 1);
  }

  const dist = Math.hypot(u, v);
  if (dist > 1) return 0;

  const hardness = clamp(brush.hardness ?? 0.72, 0.02, 0.99);
  const linearEdge = dist <= hardness ? 1 : 1 - (dist - hardness) / Math.max(0.01, 1 - hardness);
  let falloff = brush.curves?.falloff
    ? sampleCurve(brush.curves.falloff, linearEdge)
    : linearEdge * linearEdge * (3 - 2 * linearEdge);

  if (tip === "chisel") {
    const boxD = Math.max(Math.abs(u), Math.abs(v) * 1.4);
    const cEdge = boxD <= 0.85 ? 1 : clamp((1 - boxD) / 0.15, 0, 1);
    falloff = brush.curves?.falloff ? sampleCurve(brush.curves.falloff, cEdge) : cEdge;
  } else if (tip === "chalk") {
    const grain = fbm3D(px * 0.25, py * 0.25, 1.3, 2, 17);
    falloff *= clamp(0.35 + grain * 1.15, 0, 1);
  } else if (tip === "splatter") {
    const dots = fbm3D(px * 0.19, py * 0.19, 4.7, 2, 91);
    falloff = dots > 0.54 ? falloff : 0;
  } else if (tip === "sponge") {
    const pores = fbm3D(px * 0.22, py * 0.22, 8.2, 3, 53);
    falloff *= clamp((pores - 0.3) * 2.1, 0, 1);
  }
  return clamp(falloff * (brush.flow ?? 1), 0, 1);
}

/**
 * Seam-free 3D world-space capsule/sphere stroke dab into an accumulation stroke buffer.
 */
export function applyBrushDab3D({
  strokeAccum,
  meshIndex,
  cues,
  hitPos,
  hitNormal,
  worldRadius,
  brush,
  activePartFilter = -1,
}) {
  const res = cues.res;
  const rMax = Math.max(0.005, worldRadius);
  const rSearch = rMax * 1.35;
  const px0 = hitPos.x, py0 = hitPos.y, pz0 = hitPos.z;
  const nx0 = hitNormal.x, ny0 = hitNormal.y, nz0 = hitNormal.z;

  // Candidate triangles near hit sphere
  for (let tIdx = 0; tIdx < meshIndex.tris.length; tIdx++) {
    const tri = meshIndex.tris[tIdx];
    if (activePartFilter >= 0 && tri.part !== activePartFilter) continue;
    const dxC = tri.center[0] - px0, dyC = tri.center[1] - py0, dzC = tri.center[2] - pz0;
    if (Math.hypot(dxC, dyC, dzC) > tri.radius + rSearch) continue;
    // Backface threshold so brush doesn't bleed onto opposite thin side
    const dotN = tri.faceNormal[0] * nx0 + tri.faceNormal[1] * ny0 + tri.faceNormal[2] * nz0;
    if (dotN < -0.25) continue;

    // Project UV bounding box (+2px dilation to cross UV seams cleanly)
    const minPx = Math.max(0, Math.floor(tri.minU * res) - 2);
    const maxPx = Math.min(res - 1, Math.ceil(tri.maxU * res) + 2);
    const minPy = Math.max(0, Math.floor((1 - tri.maxV) * res) - 2);
    const maxPy = Math.min(res - 1, Math.ceil((1 - tri.minV) * res) + 2);

    const x0 = tri.uv0[0] * res, y0 = (1 - tri.uv0[1]) * res;
    const x1 = tri.uv1[0] * res, y1 = (1 - tri.uv1[1]) * res;
    const x2 = tri.uv2[0] * res, y2 = (1 - tri.uv2[1]) * res;
    const denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (Math.abs(denom) < 1e-6) continue;

    // Build local tangent basis around hit normal for oriented tips (chisel/stamp)
    const upRef = Math.abs(ny0) < 0.9 ? [0, 1, 0] : [1, 0, 0];
    let tx = upRef[1] * nz0 - upRef[2] * ny0;
    let ty = upRef[2] * nx0 - upRef[0] * nz0;
    let tz = upRef[0] * ny0 - upRef[1] * nx0;
    const tl = Math.hypot(tx, ty, tz) || 1;
    tx /= tl; ty /= tl; tz /= tl;
    const bx = ny0 * tz - nz0 * ty;
    const by = nz0 * tx - nx0 * tz;
    const bz = nx0 * ty - ny0 * tx;

    for (let py = minPy; py <= maxPy; py++) {
      for (let px = minPx; px <= maxPx; px++) {
        const sx = px + 0.5, sy = py + 0.5;
        const w0 = ((y1 - y2) * (sx - x2) + (x2 - x1) * (sy - y2)) / denom;
        const w1 = ((y2 - y0) * (sx - x2) + (x0 - x2) * (sy - y2)) / denom;
        const w2 = 1 - w0 - w1;
        // Allow slight negative barycentric margin (-0.18) for clean seam bleed dilation
        if (w0 < -0.18 || w1 < -0.18 || w2 < -0.18) continue;

        const pid = py * res + px;
        if (activePartFilter >= 0 && cues.partAt[pid] >= 0 && cues.partAt[pid] !== activePartFilter) continue;

        const wx = w0 * tri.p0[0] + w1 * tri.p1[0] + w2 * tri.p2[0];
        const wy = w0 * tri.p0[1] + w1 * tri.p1[1] + w2 * tri.p2[1];
        const wz = w0 * tri.p0[2] + w1 * tri.p1[2] + w2 * tri.p2[2];

        const vx = wx - px0, vy = wy - py0, vz = wz - pz0;
        const d3 = Math.hypot(vx, vy, vz);
        if (d3 > rMax) continue;

        const uLocal = (vx * tx + vy * ty + vz * tz) / rMax;
        const vLocal = (vx * bx + vy * by + vz * bz) / rMax;
        const alpha = sampleBrushTip(uLocal, vLocal, brush, px, py);
        if (alpha <= 0.001) continue;

        if (brush.tool === "stamp" && brush.stampData) {
          // Sample stamp pixel at (uLocal*0.5+0.5, vLocal*0.5+0.5)
          let rotU = uLocal, rotV = vLocal;
          if (brush.angle) {
            const rad = (-brush.angle * Math.PI) / 180;
            const c = Math.cos(rad), s = Math.sin(rad);
            rotU = uLocal * c - vLocal * s;
            rotV = uLocal * s + vLocal * c;
          }
          const su = rotU * 0.5 + 0.5;
          const sv = 1 - (rotV * 0.5 + 0.5);
          if (su >= 0 && su <= 1 && sv >= 0 && sv <= 1) {
            const st = brush.stampData;
            const stH = st.heightPx || st.width;
            const stAlb = st.albedo || st.rgba;
            const ix = Math.min(st.width - 1, Math.max(0, Math.floor(su * st.width)));
            const iy = Math.min(stH - 1, Math.max(0, Math.floor(sv * stH)));
            const si = (iy * st.width + ix) * 4;
            const sa = (stAlb[si + 3] / 255) * (brush.flow ?? 1);
            if (sa > strokeAccum.alpha[pid]) {
              strokeAccum.hasPerPixelPbr = true;
              strokeAccum.alpha[pid] = sa;
              strokeAccum.color[pid * 3] = stAlb[si];
              strokeAccum.color[pid * 3 + 1] = stAlb[si + 1];
              strokeAccum.color[pid * 3 + 2] = stAlb[si + 2];
              strokeAccum.rough[pid] = st.roughness ? st.roughness[si] : Math.round(clamp(brush.roughness ?? 0.45) * 255);
              strokeAccum.metal[pid] = st.metalness ? st.metalness[si] : Math.round(clamp(brush.metalness ?? 0) * 255);
              strokeAccum.height[pid] = st.height ? st.height[si] : Math.round(clamp((brush.height ?? 0.25) * 0.5 + 0.5) * 255);
              strokeAccum.dirtyMinX = Math.min(strokeAccum.dirtyMinX, px);
              strokeAccum.dirtyMaxX = Math.max(strokeAccum.dirtyMaxX, px);
              strokeAccum.dirtyMinY = Math.min(strokeAccum.dirtyMinY, py);
              strokeAccum.dirtyMaxY = Math.max(strokeAccum.dirtyMaxY, py);
            }
          }
        } else {
          // Accumulation builds smoothly via flow without exceeding 1, and opacity applies at stroke commit/display!
          const next = clamp(strokeAccum.alpha[pid] + alpha * (1 - strokeAccum.alpha[pid] * 0.72));
          if (next > strokeAccum.alpha[pid]) {
            const [dr, dg, db] = hexToRgb01(brush.color || "#e8c27a").map((v) => Math.round(v * 255));
            const prevA = strokeAccum.alpha[pid];
            const dw = next > 1e-4 ? clamp(alpha / next, 0, 1) : 1;
            strokeAccum.hasPerPixelPbr = true;
            strokeAccum.alpha[pid] = next;
            if (prevA < 0.005) {
              strokeAccum.color[pid * 3] = dr;
              strokeAccum.color[pid * 3 + 1] = dg;
              strokeAccum.color[pid * 3 + 2] = db;
            } else {
              strokeAccum.color[pid * 3] = Math.round(strokeAccum.color[pid * 3] * (1 - dw) + dr * dw);
              strokeAccum.color[pid * 3 + 1] = Math.round(strokeAccum.color[pid * 3 + 1] * (1 - dw) + dg * dw);
              strokeAccum.color[pid * 3 + 2] = Math.round(strokeAccum.color[pid * 3 + 2] * (1 - dw) + db * dw);
            }
            strokeAccum.rough[pid] = Math.round(clamp(brush.roughness ?? 0.45) * 255);
            strokeAccum.metal[pid] = Math.round(clamp(brush.metalness ?? 0) * 255);
            strokeAccum.height[pid] = Math.round(clamp((brush.height ?? 0.25) * 0.5 + 0.5) * 255);
            strokeAccum.dirtyMinX = Math.min(strokeAccum.dirtyMinX, px);
            strokeAccum.dirtyMaxX = Math.max(strokeAccum.dirtyMaxX, px);
            strokeAccum.dirtyMinY = Math.min(strokeAccum.dirtyMinY, py);
            strokeAccum.dirtyMaxY = Math.max(strokeAccum.dirtyMaxY, py);
          }
        }
      }
    }
  }
}

/**
 * Direct 2D UV brush splat sharing exact accumulation contract as 3D splat.
 */
export function applyBrushDabUV({ strokeAccum, cues, u, v, pixelRadius, brush, activePartFilter = -1 }) {
  const res = cues.res;
  const cx = u * res;
  const cy = (1 - v) * res;
  const r = Math.max(1.5, pixelRadius);
  const minX = Math.max(0, Math.floor(cx - r));
  const maxX = Math.min(res - 1, Math.ceil(cx + r));
  const minY = Math.max(0, Math.floor(cy - r));
  const maxY = Math.min(res - 1, Math.ceil(cy + r));
  const [dr, dg, db] = hexToRgb01(brush.color || "#e8c27a").map((c) => Math.round(c * 255));
  const dRough = Math.round(clamp(brush.roughness ?? 0.45) * 255);
  const dMetal = Math.round(clamp(brush.metalness ?? 0) * 255);
  const dHeight = Math.round(clamp((brush.height ?? 0.25) * 0.5 + 0.5) * 255);

  for (let py = minY; py <= maxY; py++) {
    for (let px = minX; px <= maxX; px++) {
      const pid = py * res + px;
      if (activePartFilter >= 0 && cues.partAt[pid] >= 0 && cues.partAt[pid] !== activePartFilter) continue;
      const dx = (px + 0.5 - cx) / r;
      const dy = (py + 0.5 - cy) / r;
      const alpha = sampleBrushTip(dx, dy, brush, px, py);
      if (alpha <= 0.001) continue;

      if (brush.tool === "stamp" && brush.stampData) {
        let rotU = dx, rotV = dy;
        if (brush.angle) {
          const rad = (-brush.angle * Math.PI) / 180;
          const c = Math.cos(rad), s = Math.sin(rad);
          rotU = dx * c - dy * s;
          rotV = dx * s + dy * c;
        }
        const su = rotU * 0.5 + 0.5;
        const sv = rotV * 0.5 + 0.5;
        if (su >= 0 && su <= 1 && sv >= 0 && sv <= 1) {
          const st = brush.stampData;
          const stH = st.heightPx || st.width;
          const stAlb = st.albedo || st.rgba;
          const ix = Math.min(st.width - 1, Math.max(0, Math.floor(su * st.width)));
          const iy = Math.min(stH - 1, Math.max(0, Math.floor(sv * stH)));
          const si = (iy * st.width + ix) * 4;
          const sa = (stAlb[si + 3] / 255) * (brush.flow ?? 1);
          if (sa > strokeAccum.alpha[pid]) {
            strokeAccum.hasPerPixelPbr = true;
            strokeAccum.alpha[pid] = sa;
            strokeAccum.color[pid * 3] = stAlb[si];
            strokeAccum.color[pid * 3 + 1] = stAlb[si + 1];
            strokeAccum.color[pid * 3 + 2] = stAlb[si + 2];
            strokeAccum.rough[pid] = st.roughness ? st.roughness[si] : dRough;
            strokeAccum.metal[pid] = st.metalness ? st.metalness[si] : dMetal;
            strokeAccum.height[pid] = st.height ? st.height[si] : dHeight;
          }
        }
      } else {
        const next = clamp(strokeAccum.alpha[pid] + alpha * (1 - strokeAccum.alpha[pid] * 0.72));
        if (next > strokeAccum.alpha[pid]) {
          const prevA = strokeAccum.alpha[pid];
          const dw = next > 1e-4 ? clamp(alpha / next, 0, 1) : 1;
          strokeAccum.hasPerPixelPbr = true;
          strokeAccum.alpha[pid] = next;
          if (prevA < 0.005) {
            strokeAccum.color[pid * 3] = dr;
            strokeAccum.color[pid * 3 + 1] = dg;
            strokeAccum.color[pid * 3 + 2] = db;
          } else {
            strokeAccum.color[pid * 3] = Math.round(strokeAccum.color[pid * 3] * (1 - dw) + dr * dw);
            strokeAccum.color[pid * 3 + 1] = Math.round(strokeAccum.color[pid * 3 + 1] * (1 - dw) + dg * dw);
            strokeAccum.color[pid * 3 + 2] = Math.round(strokeAccum.color[pid * 3 + 2] * (1 - dw) + db * dw);
          }
          strokeAccum.rough[pid] = dRough;
          strokeAccum.metal[pid] = dMetal;
          strokeAccum.height[pid] = dHeight;
        }
      }
      strokeAccum.dirtyMinX = Math.min(strokeAccum.dirtyMinX, px);
      strokeAccum.dirtyMaxX = Math.max(strokeAccum.dirtyMaxX, px);
      strokeAccum.dirtyMinY = Math.min(strokeAccum.dirtyMinY, py);
      strokeAccum.dirtyMaxY = Math.max(strokeAccum.dirtyMaxY, py);
    }
  }
}

export function createStrokeAccumulator(res) {
  const n = res * res;
  return {
    res,
    hasPerPixelPbr: false,
    alpha: new Float32Array(n),
    color: new Uint8ClampedArray(n * 3),
    rough: new Uint8ClampedArray(n),
    metal: new Uint8ClampedArray(n),
    height: new Uint8ClampedArray(n),
    dirtyMinX: res,
    dirtyMaxX: -1,
    dirtyMinY: res,
    dirtyMaxY: -1,
  };
}

/**
 * Commit accumulated stroke buffer into a layer's pixel buffers or mask buffer.
 */
export function commitStrokeAccumulator(layer, strokeAccum, brush, editingMask = false) {
  if (strokeAccum.dirtyMaxX < strokeAccum.dirtyMinX) return false;
  const res = layer.pixels?.res || strokeAccum.res;
  const opacity = clamp(brush.opacity ?? 1, 0, 1);
  const [cr, cg, cb] = hexToRgb01(brush.color || "#e8c27a").map((v) => Math.round(v * 255));
  const roughByte = Math.round(clamp(brush.roughness ?? 0.45, 0, 1) * 255);
  const metalByte = Math.round(clamp(brush.metalness ?? 0, 0, 1) * 255);
  const heightByte = Math.round(clamp((brush.height ?? 0.25) * 0.5 + 0.5, 0, 1) * 255);

  const isErase = brush.tool === "erase";
  const useAccumPbr = strokeAccum.hasPerPixelPbr || (brush.tool === "stamp" && !!brush.stampData);

  if (editingMask && layer.mask?.pixels) {
    const maskPx = layer.mask.pixels;
    const lum = Math.round(clamp(0.299 * cr + 0.587 * cg + 0.114 * cb, 0, 255));
    const targetVal = isErase ? 0 : (brush.maskInk !== undefined ? Math.round(clamp(brush.maskInk, 0, 1) * 255) : lum);
    for (let y = strokeAccum.dirtyMinY; y <= strokeAccum.dirtyMaxY; y++) {
      for (let x = strokeAccum.dirtyMinX; x <= strokeAccum.dirtyMaxX; x++) {
        const pid = y * res + x;
        const sa = strokeAccum.alpha[pid] * opacity;
        if (sa <= 0.001) continue;
        maskPx[pid] = Math.round(maskPx[pid] * (1 - sa) + targetVal * sa);
      }
    }
    return true;
  }

  if (!layer.pixels) return false;
  const px = layer.pixels;
  if (!px.roughAlpha) px.roughAlpha = new Uint8ClampedArray(res * res);
  if (!px.metalAlpha) px.metalAlpha = new Uint8ClampedArray(res * res);
  if (!px.heightAlpha) px.heightAlpha = new Uint8ClampedArray(res * res);
  const ch = getToolChannels(brush);
  const activeExtKeys = [...EXTENDED_PBR_KEYS, ...EXTRA_COLOR_KEYS].filter((k) => ch[k]);
  const brushExtSrc = brush.extValues || {};
  const extData = activeExtKeys.map((k) => {
    const buf = ensureExtPixelChannel(px, k, res);
    if (isRgbChannelKey(k)) {
      const defCol = k === "emissive" ? "#ff7a3b" : "#ffffff";
      const [er, eg, eb] = hexToRgb01(brushExtSrc[k] || brush?.color || defCol).map((v) => Math.round(v * 255));
      return { key: k, isRgb: true, er, eg, eb, buf };
    }
    const val = brushExtSrc[k] ?? PAINT_PBR_CHANNELS.find((c) => c.key === k)?.default ?? 0;
    return { key: k, isRgb: false, byteVal: encodeScalarChannelByte(k, Number(val)), buf };
  });

  for (let y = strokeAccum.dirtyMinY; y <= strokeAccum.dirtyMaxY; y++) {
    for (let x = strokeAccum.dirtyMinX; x <= strokeAccum.dirtyMaxX; x++) {
      const pid = y * res + x;
      const sa = strokeAccum.alpha[pid] * opacity;
      if (sa <= 0.001) continue;
      const i4 = pid * 4;

      if (isErase) {
        if (ch.albedo) px.albedo[i4 + 3] = Math.round(px.albedo[i4 + 3] * (1 - sa));
        if (ch.roughness) px.roughAlpha[pid] = Math.round(px.roughAlpha[pid] * (1 - sa));
        if (ch.metalness) px.metalAlpha[pid] = Math.round(px.metalAlpha[pid] * (1 - sa));
        if (ch.height) px.heightAlpha[pid] = Math.round(px.heightAlpha[pid] * (1 - sa));
        for (const ed of extData) ed.buf.alpha[pid] = Math.round(ed.buf.alpha[pid] * (1 - sa));
        continue;
      }

      const sr = useAccumPbr ? strokeAccum.color[pid * 3] : cr;
      const sg = useAccumPbr ? strokeAccum.color[pid * 3 + 1] : cg;
      const sb = useAccumPbr ? strokeAccum.color[pid * 3 + 2] : cb;
      const sRough = useAccumPbr ? strokeAccum.rough[pid] : roughByte;
      const sMetal = useAccumPbr ? strokeAccum.metal[pid] : metalByte;
      const sHeight = useAccumPbr ? strokeAccum.height[pid] : heightByte;

      if (ch.albedo) {
        const oldA = px.albedo[i4 + 3] / 255;
        const outA = sa + oldA * (1 - sa);
        if (outA > 1e-4) {
          px.albedo[i4] = Math.round((sr * sa + px.albedo[i4] * oldA * (1 - sa)) / outA);
          px.albedo[i4 + 1] = Math.round((sg * sa + px.albedo[i4 + 1] * oldA * (1 - sa)) / outA);
          px.albedo[i4 + 2] = Math.round((sb * sa + px.albedo[i4 + 2] * oldA * (1 - sa)) / outA);
        }
        px.albedo[i4 + 3] = Math.round(clamp(outA, 0, 1) * 255);
      }
      if (ch.roughness) {
        const oldR = px.roughAlpha[pid] / 255;
        const outR = sa + oldR * (1 - sa);
        if (outR > 1e-4) px.roughness[pid] = Math.round((sRough * sa + px.roughness[pid] * oldR * (1 - sa)) / outR);
        px.roughAlpha[pid] = Math.round(clamp(outR, 0, 1) * 255);
      }
      if (ch.metalness) {
        const oldM = px.metalAlpha[pid] / 255;
        const outM = sa + oldM * (1 - sa);
        if (outM > 1e-4) px.metalness[pid] = Math.round((sMetal * sa + px.metalness[pid] * oldM * (1 - sa)) / outM);
        px.metalAlpha[pid] = Math.round(clamp(outM, 0, 1) * 255);
      }
      if (ch.height) {
        const oldH = px.heightAlpha[pid] / 255;
        const outH = sa + oldH * (1 - sa);
        if (outH > 1e-4) px.height[pid] = Math.round((sHeight * sa + px.height[pid] * oldH * (1 - sa)) / outH);
        px.heightAlpha[pid] = Math.round(clamp(outH, 0, 1) * 255);
      }
      for (const ed of extData) {
        const oldE = ed.buf.alpha[pid] / 255;
        const outE = sa + oldE * (1 - sa);
        if (outE > 1e-4) {
          if (ed.isRgb) {
            const p3 = pid * 3;
            ed.buf.values[p3] = Math.round((ed.er * sa + ed.buf.values[p3] * oldE * (1 - sa)) / outE);
            ed.buf.values[p3 + 1] = Math.round((ed.eg * sa + ed.buf.values[p3 + 1] * oldE * (1 - sa)) / outE);
            ed.buf.values[p3 + 2] = Math.round((ed.eb * sa + ed.buf.values[p3 + 2] * oldE * (1 - sa)) / outE);
          } else {
            ed.buf.values[pid] = Math.round((ed.byteVal * sa + ed.buf.values[pid] * oldE * (1 - sa)) / outE);
          }
        }
        ed.buf.alpha[pid] = Math.round(clamp(outE, 0, 1) * 255);
      }
    }
  }
  return true;
}

/**
 * Flood fill by scope: "all" | "part" | "island".
 */
export function applyFloodFill({ layer, cues, hitTriIndex, scope = "part", brush, editingMask = false }) {
  const res = cues.res;
  const hitPart = hitTriIndex >= 0 ? cues.partAt[hitTriIndex] : -1;
  const hitIsland = hitTriIndex >= 0 ? cues.islandAt[hitTriIndex] : -1;
  const accum = createStrokeAccumulator(res);

  for (let pid = 0; pid < res * res; pid++) {
    if (scope === "part" && hitPart >= 0 && cues.partAt[pid] !== hitPart && cues.partAt[pid] !== -1) continue;
    if (scope === "island" && hitIsland >= 0 && cues.islandAt[pid] !== hitIsland && cues.islandAt[pid] !== -1) continue;
    accum.alpha[pid] = 1.0;
    const x = pid % res, y = Math.floor(pid / res);
    accum.dirtyMinX = Math.min(accum.dirtyMinX, x);
    accum.dirtyMaxX = Math.max(accum.dirtyMaxX, x);
    accum.dirtyMinY = Math.min(accum.dirtyMinY, y);
    accum.dirtyMaxY = Math.max(accum.dirtyMaxY, y);
  }
  return commitStrokeAccumulator(layer, accum, { ...brush, tool: brush.tool === "erase" ? "erase" : "fill" }, editingMask);
}

function blendChannelByte(dst, src, blend) {
  const a = dst / 255, b = src / 255;
  let o = b;
  if (blend === "Multiply") o = a * b;
  else if (blend === "Screen") o = 1 - (1 - a) * (1 - b);
  else if (blend === "Overlay") o = a < 0.5 ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b);
  else if (blend === "Add") o = Math.min(1, a + b);
  else if (blend === "Darken") o = Math.min(a, b);
  return Math.round(clamp(o, 0, 1) * 255);
}

/**
 * Composite entire Texture Paint Layer Stack (+ optional live stroke preview!) into final Albedo/Roughness/Metalness/Height/Normal buffers.
 */
export function compositePaintDocument(doc, cues, { liveStroke = null, liveBrush = null, viewChannel = "lit" } = {}) {
  const res = doc.resolution || cues.res;
  const n = res * res;
  const outA = new Uint8ClampedArray(n * 4);
  const outR = new Uint8ClampedArray(n).fill(140);
  const outM = new Uint8ClampedArray(n).fill(0);
  const outH = new Uint8ClampedArray(n).fill(128);
  const outExt = {};
  for (const k of EXTENDED_PBR_KEYS) {
    outExt[k] = k === "emissive"
      ? new Uint8ClampedArray(n * 3)
      : new Uint8ClampedArray(n).fill(defaultByteForExtChannel(k));
  }
  for (const k of EXTRA_COLOR_KEYS) {
    if (doc.layers.some((l) => l.visible && (l.channels?.[k] || l.pixels?.extValues?.[k]))) {
      outExt[k] = new Uint8ClampedArray(n * 3).fill(255);
    }
  }

  // Initialize opaque neutral slate base
  for (let i = 0; i < n; i++) {
    outA[i * 4] = 48;
    outA[i * 4 + 1] = 52;
    outA[i * 4 + 2] = 58;
    outA[i * 4 + 3] = 255;
  }

  const folderMap = new Map(doc.layers.filter((l) => l.kind === "folder").map((f) => [f.id, f]));
  const isVisible = (l) => {
    if (!l.visible) return false;
    if (l.parentId && folderMap.has(l.parentId)) return isVisible(folderMap.get(l.parentId));
    return true;
  };
  const effectiveOpacity = (l) => {
    let op = l.opacity ?? 1;
    if (l.parentId && folderMap.has(l.parentId)) op *= effectiveOpacity(folderMap.get(l.parentId));
    return op;
  };
  const collectMaskChain = (l) => {
    const chain = [];
    let cur = l;
    while (cur) {
      if (cur.mask?.enabled && cur.mask.pixels) chain.push(cur.mask);
      cur = cur.parentId ? folderMap.get(cur.parentId) : null;
    }
    return chain;
  };
  const evalMaskChain = (chain, i) => {
    if (!chain.length) return 1;
    let m = 1;
    for (const msk of chain) {
      let mv = msk.pixels[i] / 255;
      if (msk.invert) mv = 1 - mv;
      const mop = clamp(msk.opacity ?? 1, 0, 1);
      m *= (1 - mop) + mv * mop;
    }
    return m;
  };

  const bakedRes = doc.bakedMaps?.res || res;
  const bakedAo = doc.bakedMaps?.ao;
  const bakedCurv = doc.bakedMaps?.curvature;
  const bakedNorm = doc.bakedMaps?.worldNormal;
  const bakedAoIsFloat = bakedAo instanceof Float32Array || bakedAo instanceof Float64Array;
  const bakedCurvIsFloat = bakedCurv instanceof Float32Array || bakedCurv instanceof Float64Array;
  const sampleBakedScalar = (arr, isFloat, pixelIndex) => {
    let bi = pixelIndex;
    if (bakedRes !== res) {
      const px = pixelIndex % res;
      const py = (pixelIndex - px) / res;
      const bx = Math.min(bakedRes - 1, Math.floor((px / res) * bakedRes));
      const by = Math.min(bakedRes - 1, Math.floor((py / res) * bakedRes));
      bi = by * bakedRes + bx;
    }
    const v = arr[bi] ?? 0;
    return isFloat ? v : v / 255;
  };

  for (const layer of doc.layers) {
    if (layer.kind === "folder" || !isVisible(layer)) continue;
    const op = effectiveOpacity(layer);
    if (op <= 0.001) continue;
    const ch = layer.channels || createDefaultChannelMap(false);
    const chOp = (k) => clamp(layer.channelOpacity?.[k] ?? 1, 0, 1);
    const chBlend = (key) => {
      const b = layer.channelBlends?.[key];
      return b && b !== "Inherit" ? b : layer.blend;
    };
    const maskChain = collectMaskChain(layer);
    const partFilter = layer.partFilter ?? -1;
    const activeExtLayerKeys = [...EXTENDED_PBR_KEYS, ...EXTRA_COLOR_KEYS].filter((k) => ch[k] && chOp(k) > 0.001 && outExt[k]);

    const resolveScalarExtSource = (srcObj) => {
      const out = [];
      for (const k of activeExtLayerKeys) {
        if (isRgbChannelKey(k)) {
          const [er, eg, eb] = hexToRgb01(srcObj?.extValues?.[k] ?? srcObj?.[k] ?? layer.extValues?.[k] ?? (k === "emissive" ? "#ff7a3b" : "#ffffff")).map((v) => Math.round(v * 255));
          out.push({ key: k, isRgb: true, er, eg, eb, w: chOp(k), b: chBlend(k) });
        } else {
          const v = srcObj?.extValues?.[k] ?? srcObj?.[k] ?? layer.extValues?.[k] ?? defaultByteForExtChannel(k) / 255;
          const bVal = encodeScalarChannelByte(k, Number(v));
          out.push({ key: k, isRgb: false, bVal, w: chOp(k), b: chBlend(k) });
        }
      }
      return out;
    };

    if (layer.kind === "paint" && layer.pixels) {
      const px = layer.pixels;
      const isLive = liveStroke && liveBrush && doc.selectedLayerId === layer.id && !doc.editingMask;
      const bch = isLive ? getToolChannels(liveBrush) : createDefaultChannelMap(false);
      const [lr, lg, lb] = isLive ? hexToRgb01(liveBrush.color).map((v) => Math.round(v * 255)) : [0, 0, 0];
      const lRough = isLive ? Math.round((liveBrush.roughness ?? 0.45) * 255) : 0;
      const lMetal = isLive ? Math.round((liveBrush.metalness ?? 0) * 255) : 0;
      const lHeight = isLive ? Math.round(((liveBrush.height ?? 0.25) * 0.5 + 0.5) * 255) : 128;
      const wOpA = chOp("albedo"), wOpR = chOp("roughness"), wOpM = chOp("metalness"), wOpH = chOp("height");
      const paintExtList = activeExtLayerKeys.filter((k) => px.extValues?.[k] || (isLive && bch[k])).map((k) => {
        const hasBuf = !!px.extValues?.[k];
        const rgbCh = isRgbChannelKey(k);
        const bVal = isLive ? (rgbCh ? hexToRgb01(liveBrush.extValues?.[k] || (k === "emissive" ? "#ff7a3b" : "#ffffff")).map((v) => Math.round(v * 255)) : encodeScalarChannelByte(k, Number(liveBrush.extValues?.[k] ?? defaultByteForExtChannel(k) / 255))) : null;
        return { key: k, isRgb: rgbCh, hasBuf, bufV: px.extValues?.[k], bufA: px.extAlphas?.[k], liveActive: !!(isLive && bch[k]), bVal, w: chOp(k), b: chBlend(k) };
      });

      for (let i = 0; i < n; i++) {
        if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
        const i4 = i * 4;
        let aAlb = px.albedo[i4 + 3] / 255;
        let aRough = px.roughAlpha ? (px.roughAlpha[i] / 255) : aAlb;
        let aMetal = px.metalAlpha ? (px.metalAlpha[i] / 255) : aAlb;
        let aHeight = px.heightAlpha ? (px.heightAlpha[i] / 255) : aAlb;
        let sr = px.albedo[i4], sg = px.albedo[i4 + 1], sb = px.albedo[i4 + 2];
        let sRough = px.roughness[i], sMetal = px.metalness[i], sHeight = px.height[i];
        const liveSa = (isLive && liveStroke.alpha[i] > 0.001) ? liveStroke.alpha[i] * (liveBrush.opacity ?? 1) : 0;

        if (liveSa > 0.001) {
          if (liveBrush.tool === "erase") {
            if (bch.albedo) aAlb *= (1 - liveSa);
            if (bch.roughness) aRough *= (1 - liveSa);
            if (bch.metalness) aMetal *= (1 - liveSa);
            if (bch.height) aHeight *= (1 - liveSa);
          } else {
            const useLiveBuf = liveStroke.hasPerPixelPbr || (liveBrush.tool === "stamp" && !!liveBrush.stampData);
            const brR = useLiveBuf ? liveStroke.color[i * 3] : lr;
            const brG = useLiveBuf ? liveStroke.color[i * 3 + 1] : lg;
            const brB = useLiveBuf ? liveStroke.color[i * 3 + 2] : lb;
            const brRough = useLiveBuf ? liveStroke.rough[i] : lRough;
            const brMetal = useLiveBuf ? liveStroke.metal[i] : lMetal;
            const brHeight = useLiveBuf ? liveStroke.height[i] : lHeight;

            if (bch.albedo) {
              const outA2 = liveSa + aAlb * (1 - liveSa);
              if (outA2 > 1e-4) {
                sr = Math.round((brR * liveSa + sr * aAlb * (1 - liveSa)) / outA2);
                sg = Math.round((brG * liveSa + sg * aAlb * (1 - liveSa)) / outA2);
                sb = Math.round((brB * liveSa + sb * aAlb * (1 - liveSa)) / outA2);
              }
              aAlb = outA2;
            }
            if (bch.roughness) {
              const outR2 = liveSa + aRough * (1 - liveSa);
              if (outR2 > 1e-4) sRough = Math.round((brRough * liveSa + sRough * aRough * (1 - liveSa)) / outR2);
              aRough = outR2;
            }
            if (bch.metalness) {
              const outM2 = liveSa + aMetal * (1 - liveSa);
              if (outM2 > 1e-4) sMetal = Math.round((brMetal * liveSa + sMetal * aMetal * (1 - liveSa)) / outM2);
              aMetal = outM2;
            }
            if (bch.height) {
              const outH2 = liveSa + aHeight * (1 - liveSa);
              if (outH2 > 1e-4) sHeight = Math.round((brHeight * liveSa + sHeight * aHeight * (1 - liveSa)) / outH2);
              aHeight = outH2;
            }
          }
        }

        const maskMul = evalMaskChain(maskChain, i);
        if (maskMul <= 0.001) continue;

        const effAlb = clamp(aAlb * op * maskMul * wOpA, 0, 1);
        const effRough = clamp(aRough * op * maskMul * wOpR, 0, 1);
        const effMetal = clamp(aMetal * op * maskMul * wOpM, 0, 1);
        const effHeight = clamp(aHeight * op * maskMul * wOpH, 0, 1);

        if (ch.albedo && effAlb > 0.001) {
          const bA = chBlend("albedo");
          outA[i4] = Math.round(outA[i4] * (1 - effAlb) + blendChannelByte(outA[i4], sr, bA) * effAlb);
          outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - effAlb) + blendChannelByte(outA[i4 + 1], sg, bA) * effAlb);
          outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - effAlb) + blendChannelByte(outA[i4 + 2], sb, bA) * effAlb);
        }
        if (ch.roughness && effRough > 0.001) outR[i] = Math.round(outR[i] * (1 - effRough) + blendChannelByte(outR[i], sRough, chBlend("roughness")) * effRough);
        if (ch.metalness && effMetal > 0.001) outM[i] = Math.round(outM[i] * (1 - effMetal) + blendChannelByte(outM[i], sMetal, chBlend("metalness")) * effMetal);
        if (ch.height && effHeight > 0.001) outH[i] = Math.round(outH[i] * (1 - effHeight) + blendChannelByte(outH[i], sHeight, chBlend("height")) * effHeight);

        for (const pe of paintExtList) {
          let ae = pe.hasBuf ? pe.bufA[i] / 255 : 0;
          if (pe.isRgb) {
            const p3 = i * 3;
            let vr = pe.hasBuf ? pe.bufV[p3] : 0, vg = pe.hasBuf ? pe.bufV[p3 + 1] : 0, vb = pe.hasBuf ? pe.bufV[p3 + 2] : 0;
            if (liveSa > 0.001 && pe.liveActive) {
              if (liveBrush.tool === "erase") ae *= (1 - liveSa);
              else {
                const outAe = liveSa + ae * (1 - liveSa);
                if (outAe > 1e-4) {
                  vr = Math.round((pe.bVal[0] * liveSa + vr * ae * (1 - liveSa)) / outAe);
                  vg = Math.round((pe.bVal[1] * liveSa + vg * ae * (1 - liveSa)) / outAe);
                  vb = Math.round((pe.bVal[2] * liveSa + vb * ae * (1 - liveSa)) / outAe);
                }
                ae = outAe;
              }
            }
            const we = clamp(ae * op * maskMul * pe.w, 0, 1);
            if (we > 0.001) {
              const dst = outExt[pe.key];
              dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], vr, pe.b) * we);
              dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], vg, pe.b) * we);
              dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], vb, pe.b) * we);
            }
          } else {
            let v = pe.hasBuf ? pe.bufV[i] : defaultByteForExtChannel(pe.key);
            if (liveSa > 0.001 && pe.liveActive) {
              if (liveBrush.tool === "erase") ae *= (1 - liveSa);
              else {
                const outAe = liveSa + ae * (1 - liveSa);
                if (outAe > 1e-4) v = Math.round((pe.bVal * liveSa + v * ae * (1 - liveSa)) / outAe);
                ae = outAe;
              }
            }
            const we = clamp(ae * op * maskMul * pe.w, 0, 1);
            if (we > 0.001) {
              const dst = outExt[pe.key];
              dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], v, pe.b) * we);
            }
          }
        }
      }
    } else if (layer.kind === "fill") {
      const f = layer.fill;
      const [fr, fg, fb] = hexToRgb01(f.color).map((v) => Math.round(v * 255));
      const fRough = Math.round(clamp(f.roughness ?? 0.5, 0, 1) * 255);
      const fMetal = Math.round(clamp(f.metalness ?? 0, 0, 1) * 255);
      const fHeight = Math.round(clamp((f.height ?? 0) * 0.5 + 0.5, 0, 1) * 255);
      const pat = f.patternData;
      const rep = Math.max(0.25, Number(f.patternRepeats || 2));
      const rot = ((f.patternRotation || 0) * Math.PI) / 180;
      const cRot = Math.cos(rot), sRot = Math.sin(rot);
      const patHStr = clamp(f.patternHeightStrength ?? 1.0, 0, 2);
      const extList = resolveScalarExtSource(f);
      const wOpA = chOp("albedo"), wOpR = chOp("roughness"), wOpM = chOp("metalness"), wOpH = chOp("height");

      for (let i = 0; i < n; i++) {
        if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
        const eff = op * evalMaskChain(maskChain, i);
        if (eff <= 0.001) continue;

        let sr = fr, sg = fg, sb = fb, sR = fRough, sM = fMetal, sH = fHeight;
        if (pat) {
          const x = i % res, y = Math.floor(i / res);
          const ux = (x / res - 0.5) * rep;
          const vy = (y / res - 0.5) * rep;
          const ru = (((ux * cRot - vy * sRot) + 0.5) % 1 + 1) % 1;
          const rv = (((ux * sRot + vy * cRot) + 0.5) % 1 + 1) % 1;
          const patH = pat.heightPx || pat.width;
          const ix = Math.floor(ru * pat.width);
          const iy = Math.floor(rv * patH);
          const si = (iy * pat.width + ix) * 4;
          const pa = pat.albedo[si + 3] / 255;
          sr = Math.round(fr * (1 - pa) + pat.albedo[si] * pa);
          sg = Math.round(fg * (1 - pa) + pat.albedo[si + 1] * pa);
          sb = Math.round(fb * (1 - pa) + pat.albedo[si + 2] * pa);
          sR = Math.round(fRough * (1 - pa) + pat.roughness[si] * pa);
          sM = Math.round(fMetal * (1 - pa) + pat.metalness[si] * pa);
          sH = clamp(Math.round(128 + ((pat.height[si] - 128) * pa) * patHStr + (fHeight - 128)), 0, 255);
        }
        const i4 = i * 4;
        const wA = eff * wOpA, wR = eff * wOpR, wM = eff * wOpM, wH = eff * wOpH;
        if (ch.albedo && wA > 0.001) {
          const bA = chBlend("albedo");
          outA[i4] = Math.round(outA[i4] * (1 - wA) + blendChannelByte(outA[i4], sr, bA) * wA);
          outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - wA) + blendChannelByte(outA[i4 + 1], sg, bA) * wA);
          outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - wA) + blendChannelByte(outA[i4 + 2], sb, bA) * wA);
        }
        if (ch.roughness && wR > 0.001) outR[i] = Math.round(outR[i] * (1 - wR) + blendChannelByte(outR[i], sR, chBlend("roughness")) * wR);
        if (ch.metalness && wM > 0.001) outM[i] = Math.round(outM[i] * (1 - wM) + blendChannelByte(outM[i], sM, chBlend("metalness")) * wM);
        if (ch.height && wH > 0.001) outH[i] = Math.round(outH[i] * (1 - wH) + blendChannelByte(outH[i], sH, chBlend("height")) * wH);
        for (const es of extList) {
          const we = eff * es.w;
          if (we <= 0.001) continue;
          const dst = outExt[es.key];
          if (es.isRgb) {
            const p3 = i * 3;
            dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], es.er, es.b) * we);
            dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], es.eg, es.b) * we);
            dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], es.eb, es.b) * we);
          } else {
            dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], es.bVal, es.b) * we);
          }
        }
      }
    } else if (layer.kind === "material") {
      const m = layer.material;
      const spec = resolveMaterialLayerSpec(layer);
      const isFlakePaint = (spec.type ?? 0) === 0 && (spec.flakes ?? 0) > 0.001;
      const depthAbs = (spec.type ?? 0) === 0 ? Math.exp(-(spec.depth ?? 0) * 0.28) : 1.0;
      const [mrBase, mgBase, mbBase] = hexToRgb01(spec.color || m.color).map((v) => v * 255);
      const mr = mrBase * depthAbs, mg = mgBase * depthAbs, mb = mbBase * depthAbs;
      const flakeStops = (spec.colors?.length ? spec.colors : [spec.color]).map((hex) =>
        hexToRgb01(hex).map((v) => v * 255),
      );
      const flakeFreq = Math.min(260, ((52000 / Math.max(1, spec.flakeSize || 450)) * (spec.flakeScale || 1)));
      const mRough = clamp(spec.roughness ?? m.roughness ?? 0.4, 0, 1) * 255;
      const mMetal = clamp(spec.metalness ?? m.metalness ?? 0, 0, 1) * 255;
      const mHeight = clamp((m.height ?? 0) * 0.5 + 0.5, 0, 1) * 255;
      const gScale = m.grainScale || spec.detailScale || 10;
      const gStr = m.grainStrength ?? 0.08;
      const extList = resolveScalarExtSource(m);
      const wOpA = chOp("albedo"), wOpR = chOp("roughness"), wOpM = chOp("metalness"), wOpH = chOp("height");

      for (let i = 0; i < n; i++) {
        if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
        const eff = op * evalMaskChain(maskChain, i);
        if (eff <= 0.001) continue;

        const wx0 = cues.worldPos[i * 3];
        const wy0 = cues.worldPos[i * 3 + 1];
        const wz0 = cues.worldPos[i * 3 + 2];
        const micro = cues.covered[i] ? (fbm3D(wx0 * gScale, wy0 * gScale, wz0 * gScale, 2, 71) - 0.5) * 2 * gStr : 0;

        let sr = clamp(mr * (1 + micro), 0, 255);
        let sg = clamp(mg * (1 + micro), 0, 255);
        let sb = clamp(mb * (1 + micro), 0, 255);
        let sRough = clamp(mRough + micro * 90, 0, 255);
        let sMetal = mMetal;

        if (isFlakePaint && cues.covered[i]) {
          const hA = Math.abs(Math.sin(wx0 * flakeFreq * 12.9898 + wy0 * flakeFreq * 78.233 + wz0 * flakeFreq * 37.719) * 43758.5453) % 1;
          const hB = Math.abs(Math.sin(wx0 * flakeFreq * 41.13 + wy0 * flakeFreq * 19.7 + wz0 * flakeFreq * 93.17) * 24634.6345) % 1;
          const occ = hA < (spec.flakes || 0) * 0.48 ? 1.0 : 0.0;
          if (occ > 0) {
            const stop = flakeStops[Math.min(flakeStops.length - 1, Math.floor(hB * flakeStops.length))];
            const fw = clamp((spec.FlakeWeight ?? 1) * 0.85, 0, 1);
            sr = sr * (1 - fw) + stop[0] * fw;
            sg = sg * (1 - fw) + stop[1] * fw;
            sb = sb * (1 - fw) + stop[2] * fw;
            const frR = ((spec.flakeRoughnessMin ?? 0.09) + hB * ((spec.flakeRoughnessMax ?? 0.28) - (spec.flakeRoughnessMin ?? 0.09))) * 255;
            const frM = ((spec.flakeMetalnessMin ?? 0.86) + hB * ((spec.flakeMetalnessMax ?? 0.99) - (spec.flakeMetalnessMin ?? 0.86))) * 255;
            sRough = sRough * (1 - fw) + frR * fw;
            sMetal = sMetal * (1 - fw) + frM * fw;
          }
        }

        const i4 = i * 4;
        const wA = eff * wOpA, wR = eff * wOpR, wM = eff * wOpM, wH = eff * wOpH;
        if (ch.albedo && wA > 0.001) {
          const bA = chBlend("albedo");
          outA[i4] = Math.round(outA[i4] * (1 - wA) + blendChannelByte(outA[i4], sr, bA) * wA);
          outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - wA) + blendChannelByte(outA[i4 + 1], sg, bA) * wA);
          outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - wA) + blendChannelByte(outA[i4 + 2], sb, bA) * wA);
        }
        if (ch.roughness && wR > 0.001) outR[i] = Math.round(outR[i] * (1 - wR) + blendChannelByte(outR[i], sRough, chBlend("roughness")) * wR);
        if (ch.metalness && wM > 0.001) outM[i] = Math.round(outM[i] * (1 - wM) + blendChannelByte(outM[i], sMetal, chBlend("metalness")) * wM);
        if (ch.height && wH > 0.001) outH[i] = Math.round(outH[i] * (1 - wH) + blendChannelByte(outH[i], clamp(mHeight + micro * 70, 0, 255), chBlend("height")) * wH);
        for (const es of extList) {
          const we = eff * es.w;
          if (we <= 0.001) continue;
          const dst = outExt[es.key];
          if (es.isRgb) {
            const p3 = i * 3;
            dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], es.er, es.b) * we);
            dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], es.eg, es.b) * we);
            dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], es.eb, es.b) * we);
          } else {
            dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], es.bVal, es.b) * we);
          }
        }
      }
    } else if (layer.kind === "generator") {
      const g = layer.generator;
      const [gr, gg, gb] = hexToRgb01(g.color).map((v) => v * 255);
      const gRough = clamp(g.roughness ?? 0.5, 0, 1) * 255;
      const gMetal = clamp(g.metalness ?? 0, 0, 1) * 255;
      const gHeight = clamp((g.height ?? 0) * 0.5 + 0.5, 0, 1) * 255;
      const thr = g.threshold ?? 0.42;
      const soft = Math.max(0.02, g.softness ?? 0.24);
      const nScale = g.noiseScale ?? 6;
      const nMix = g.noiseMix ?? 0.35;
      const genInvert = !!g.invert;
      const extList = resolveScalarExtSource(g);
      const wOpA = chOp("albedo"), wOpR = chOp("roughness"), wOpM = chOp("metalness"), wOpH = chOp("height");

      for (let i = 0; i < n; i++) {
        if (!cues.covered[i]) continue;
        if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;

        let metric = 0;
        if (g.preset === "edge-wear") {
          const curv = bakedCurv ? sampleBakedScalar(bakedCurv, bakedCurvIsFloat, i) : cues.curvature[i];
          metric = clamp((curv - 0.5) * 2.2, 0, 1);
        } else if (g.preset === "crevice-dirt") {
          const occ = bakedAo ? sampleBakedScalar(bakedAo, bakedAoIsFloat, i) : cues.ao[i];
          metric = clamp(1 - occ, 0, 1);
        } else if (g.preset === "top-dust") {
          let ny = cues.worldNormal[i * 3 + 1];
          if (bakedNorm) {
            let bi = i;
            if (bakedRes !== res) {
              const px = i % res;
              const py = (i - px) / res;
              bi = Math.min(bakedRes - 1, Math.floor((py / res) * bakedRes)) * bakedRes + Math.min(bakedRes - 1, Math.floor((px / res) * bakedRes));
            }
            ny = bakedNorm[bi * 3 + 1];
          }
          metric = clamp(ny, 0, 1);
        } else {
          metric = fbm3D(
            cues.worldPos[i * 3] * nScale * 0.7,
            cues.worldPos[i * 3 + 1] * nScale * 0.7,
            cues.worldPos[i * 3 + 2] * nScale * 0.7,
            3,
            307,
          );
        }

        const nVal = fbm3D(
          cues.worldPos[i * 3] * nScale,
          cues.worldPos[i * 3 + 1] * nScale,
          cues.worldPos[i * 3 + 2] * nScale,
          3,
          131,
        );
        const combined = metric * (1 - nMix) + nVal * nMix;
        let genRaw = clamp((combined - (thr - soft * 0.5)) / soft, 0, 1);
        if (genInvert) genRaw = 1 - genRaw;
        const genAlpha = genRaw * op * evalMaskChain(maskChain, i);
        if (genAlpha <= 0.001) continue;

        const i4 = i * 4;
        const wA = genAlpha * wOpA, wR = genAlpha * wOpR, wM = genAlpha * wOpM, wH = genAlpha * wOpH;
        if (ch.albedo && wA > 0.001) {
          const bA = chBlend("albedo");
          outA[i4] = Math.round(outA[i4] * (1 - wA) + blendChannelByte(outA[i4], gr, bA) * wA);
          outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - wA) + blendChannelByte(outA[i4 + 1], gg, bA) * wA);
          outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - wA) + blendChannelByte(outA[i4 + 2], gb, bA) * wA);
        }
        if (ch.roughness && wR > 0.001) outR[i] = Math.round(outR[i] * (1 - wR) + blendChannelByte(outR[i], gRough, chBlend("roughness")) * wR);
        if (ch.metalness && wM > 0.001) outM[i] = Math.round(outM[i] * (1 - wM) + blendChannelByte(outM[i], gMetal, chBlend("metalness")) * wM);
        if (ch.height && wH > 0.001) outH[i] = Math.round(outH[i] * (1 - wH) + blendChannelByte(outH[i], gHeight, chBlend("height")) * wH);
        for (const es of extList) {
          const we = genAlpha * es.w;
          if (we <= 0.001) continue;
          const dst = outExt[es.key];
          if (es.isRgb) {
            const p3 = i * 3;
            dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], es.er, es.b) * we);
            dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], es.eg, es.b) * we);
            dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], es.eb, es.b) * we);
          } else {
            dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], es.bVal, es.b) * we);
          }
        }
      }
    }

    // Unified Stamp Subcomponents (+ legacy decal fallback) on ANY visible layer!
    const stampList = layer.stamps?.length
      ? layer.stamps
      : (layer.kind === "decal" && layer.decal?.stampMaps
        ? [{
            visible: true,
            projection: "3d",
            anchorPos: layer.decal.anchorPos,
            anchorNormal: layer.decal.anchorNormal,
            scale: layer.decal.radius || 0.45,
            scaleX: 1,
            scaleY: 1,
            rotation: layer.decal.rotation || 0,
            opacity: 1,
            blend: layer.blend,
            heightStrength: 0.5,
            tint: "#ffffff",
            stampData: layer.decal.stampMaps,
          }]
        : []);

    for (const stmp of stampList) {
      if (!stmp?.visible) continue;
      const st = stmp.stampData || stmp.stampMaps;
      const stAlb = st?.albedo || st?.rgba;
      if (!stAlb || !st?.width) continue;
      const stH = st.heightPx || st.width;
      const stOp = op * clamp(stmp.opacity ?? 1, 0, 1);
      if (stOp <= 0.001) continue;
      const [tr, tg, tb] = hexToRgb01(stmp.tint || "#ffffff");
      const rot = ((stmp.rotation || 0) * Math.PI) / 180;
      const cr = Math.cos(rot), srRot = Math.sin(rot);
      const sx = Math.max(0.015, (stmp.scale ?? 0.28) * (stmp.scaleX ?? 1.0));
      const sy = Math.max(0.015, (stmp.scale ?? 0.28) * (stmp.scaleY ?? 1.0));
      const stCh = stmp.channels || ch;
      const stBlend = stmp.blend && stmp.blend !== "Inherit" ? stmp.blend : layer.blend;
      const hGain = stmp.heightStrength ?? 0.5;
      const wOpA = chOp("albedo"), wOpR = chOp("roughness"), wOpM = chOp("metalness"), wOpH = chOp("height");
      const extList = EXTENDED_PBR_KEYS.filter((k) => stCh[k] && chOp(k) > 0.001).map((k) => {
        if (k === "emissive") {
          const [er, eg, eb] = hexToRgb01(stmp.extValues?.emissive ?? layer.extValues?.emissive ?? "#ff7a3b").map((v) => Math.round(v * 255));
          return { key: k, isRgb: true, er, eg, eb, w: chOp(k), b: stBlend };
        }
        const v = stmp.extValues?.[k] ?? layer.extValues?.[k] ?? defaultByteForExtChannel(k) / 255;
        return { key: k, isRgb: false, bVal: encodeScalarChannelByte(k, Number(v)), w: chOp(k), b: stBlend };
      });

      if (stmp.projection === "3d") {
        const [ax, ay, az] = stmp.anchorPos || [0, 0.2, 1.0];
        const [nx, ny, nz] = stmp.anchorNormal || [0, 0, 1];
        const upRef = Math.abs(ny) < 0.9 ? [0, 1, 0] : [1, 0, 0];
        let tx = upRef[1] * nz - upRef[2] * ny;
        let ty = upRef[2] * nx - upRef[0] * nz;
        let tz = upRef[0] * ny - upRef[1] * nx;
        const tl = Math.hypot(tx, ty, tz) || 1;
        tx /= tl; ty /= tl; tz /= tl;
        const bx = ny * tz - nz * ty, by = nz * tx - nx * tz, bz = nx * ty - ny * tx;

        for (let i = 0; i < n; i++) {
          if (!cues.covered[i]) continue;
          if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
          const ndot = cues.worldNormal[i * 3] * nx + cues.worldNormal[i * 3 + 1] * ny + cues.worldNormal[i * 3 + 2] * nz;
          if (ndot < 0.15) continue;

          const vx = cues.worldPos[i * 3] - ax;
          const vy = cues.worldPos[i * 3 + 1] - ay;
          const vz = cues.worldPos[i * 3 + 2] - az;
          const pxU = vx * tx + vy * ty + vz * tz;
          const pxV = vx * bx + vy * by + vz * bz;
          const uRot = (pxU * cr - pxV * srRot) / sx;
          const vRot = (pxU * srRot + pxV * cr) / sy;
          const su = uRot * 0.5 + 0.5;
          const sv = 1 - (vRot * 0.5 + 0.5);
          if (su < 0 || su > 1 || sv < 0 || sv > 1) continue;

          const ix = Math.min(st.width - 1, Math.max(0, Math.floor(su * st.width)));
          const iy = Math.min(stH - 1, Math.max(0, Math.floor(sv * stH)));
          const si = (iy * st.width + ix) * 4;
          const eff = (stAlb[si + 3] / 255) * stOp * clamp((ndot - 0.15) * 3, 0, 1) * evalMaskChain(maskChain, i);
          if (eff <= 0.001) continue;

          const i4 = i * 4;
          const wA = eff * wOpA, wR = eff * wOpR, wM = eff * wOpM, wH = eff * wOpH;
          if (stCh.albedo && wA > 0.001) {
            const bA = chBlend("albedo") !== layer.blend ? chBlend("albedo") : stBlend;
            outA[i4] = Math.round(outA[i4] * (1 - wA) + blendChannelByte(outA[i4], Math.round(stAlb[si] * tr), bA) * wA);
            outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - wA) + blendChannelByte(outA[i4 + 1], Math.round(stAlb[si + 1] * tg), bA) * wA);
            outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - wA) + blendChannelByte(outA[i4 + 2], Math.round(stAlb[si + 2] * tb), bA) * wA);
          }
          if (stCh.roughness && wR > 0.001 && st.roughness) outR[i] = Math.round(outR[i] * (1 - wR) + blendChannelByte(outR[i], st.roughness[si], chBlend("roughness")) * wR);
          if (stCh.metalness && wM > 0.001 && st.metalness) outM[i] = Math.round(outM[i] * (1 - wM) + blendChannelByte(outM[i], st.metalness[si], chBlend("metalness")) * wM);
          if (stCh.height && wH > 0.001 && st.height) {
            const hByte = clamp(Math.round(128 + (st.height[si] - 128) * hGain), 0, 255);
            outH[i] = Math.round(outH[i] * (1 - wH) + blendChannelByte(outH[i], hByte, chBlend("height")) * wH);
          }
          for (const es of extList) {
            const we = eff * es.w;
            if (we <= 0.001) continue;
            const dst = outExt[es.key];
            if (es.isRgb) {
              const p3 = i * 3;
              dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], es.er, es.b) * we);
              dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], es.eg, es.b) * we);
              dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], es.eb, es.b) * we);
            } else {
              dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], es.bVal, es.b) * we);
            }
          }
        }
      } else {
        // 2D UV Projection (smooth dragging & scaling across UV canvas OR 3D surface UV)
        const cu = stmp.u ?? 0.5, cv = stmp.v ?? 0.5;
        const maxExtent = Math.hypot(sx, sy);
        const minX = Math.max(0, Math.floor((cu - maxExtent) * res));
        const maxX = Math.min(res - 1, Math.ceil((cu + maxExtent) * res));
        const cyUv = 1 - cv;
        const minY = Math.max(0, Math.floor((cyUv - maxExtent) * res));
        const maxY = Math.min(res - 1, Math.ceil((cyUv + maxExtent) * res));

        for (let py = minY; py <= maxY; py++) {
          const vPx = 1 - (py + 0.5) / res;
          for (let px = minX; px <= maxX; px++) {
            const i = py * res + px;
            if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
            const du = ((px + 0.5) / res) - cu;
            const dv = vPx - cv;
            const lu = (du * cr - dv * srRot) / sx;
            const lv = (du * srRot + dv * cr) / sy;
            const su = lu * 0.5 + 0.5;
            const sv = 1 - (lv * 0.5 + 0.5);
            if (su < 0 || su > 1 || sv < 0 || sv > 1) continue;

            const ix = Math.min(st.width - 1, Math.max(0, Math.floor(su * st.width)));
            const iy = Math.min(stH - 1, Math.max(0, Math.floor(sv * stH)));
            const si = (iy * st.width + ix) * 4;
            const eff = (stAlb[si + 3] / 255) * stOp * evalMaskChain(maskChain, i);
            if (eff <= 0.001) continue;

            const i4 = i * 4;
            const wA = eff * wOpA, wR = eff * wOpR, wM = eff * wOpM, wH = eff * wOpH;
            if (stCh.albedo && wA > 0.001) {
              const bA = chBlend("albedo") !== layer.blend ? chBlend("albedo") : stBlend;
              outA[i4] = Math.round(outA[i4] * (1 - wA) + blendChannelByte(outA[i4], Math.round(stAlb[si] * tr), bA) * wA);
              outA[i4 + 1] = Math.round(outA[i4 + 1] * (1 - wA) + blendChannelByte(outA[i4 + 1], Math.round(stAlb[si + 1] * tg), bA) * wA);
              outA[i4 + 2] = Math.round(outA[i4 + 2] * (1 - wA) + blendChannelByte(outA[i4 + 2], Math.round(stAlb[si + 2] * tb), bA) * wA);
            }
            if (stCh.roughness && wR > 0.001 && st.roughness) outR[i] = Math.round(outR[i] * (1 - wR) + blendChannelByte(outR[i], st.roughness[si], chBlend("roughness")) * wR);
            if (stCh.metalness && wM > 0.001 && st.metalness) outM[i] = Math.round(outM[i] * (1 - wM) + blendChannelByte(outM[i], st.metalness[si], chBlend("metalness")) * wM);
            if (stCh.height && wH > 0.001 && st.height) {
              const hByte = clamp(Math.round(128 + (st.height[si] - 128) * hGain), 0, 255);
              outH[i] = Math.round(outH[i] * (1 - wH) + blendChannelByte(outH[i], hByte, chBlend("height")) * wH);
            }
            for (const es of extList) {
              const we = eff * es.w;
              if (we <= 0.001) continue;
              const dst = outExt[es.key];
              if (es.isRgb) {
                const p3 = i * 3;
                dst[p3] = Math.round(dst[p3] * (1 - we) + blendChannelByte(dst[p3], es.er, es.b) * we);
                dst[p3 + 1] = Math.round(dst[p3 + 1] * (1 - we) + blendChannelByte(dst[p3 + 1], es.eg, es.b) * we);
                dst[p3 + 2] = Math.round(dst[p3 + 2] * (1 - we) + blendChannelByte(dst[p3 + 2], es.eb, es.b) * we);
              } else {
                dst[i] = Math.round(dst[i] * (1 - we) + blendChannelByte(dst[i], es.bVal, es.b) * we);
              }
            }
          }
        }
      }
    }
  }

  // Derive tangent-space Normal map from composited height + Normal Strength channel
  const outN = new Uint8ClampedArray(n * 4);
  const sampleH = (x, y) => outH[clamp(y, 0, res - 1) * res + clamp(x, 0, res - 1)] / 255;
  const normArr = outExt.normal;
  for (let y = 0; y < res; y++) {
    for (let x = 0; x < res; x++) {
      const i = y * res + x;
      const gain = (normArr[i] / 255) * 2.0 * 2.8;
      const dx = sampleH(x + 1, y) - sampleH(x - 1, y);
      const dy = sampleH(x, y + 1) - sampleH(x, y - 1);
      let nx = -dx * gain;
      let ny = -dy * gain;
      let nz = 1.0;
      const l = Math.hypot(nx, ny, nz);
      nx /= l; ny /= l; nz /= l;
      const i4 = i * 4;
      outN[i4] = Math.round((nx * 0.5 + 0.5) * 255);
      outN[i4 + 1] = Math.round((ny * 0.5 + 0.5) * 255);
      outN[i4 + 2] = Math.round((nz * 0.5 + 0.5) * 255);
      outN[i4 + 3] = 255;
    }
  }

  // Channel solo diagnostic visualization (supports all 19 PBR channels + active Mask)
  if (viewChannel === "mask") {
    const sel = doc.layers.find((l) => l.id === doc.selectedLayerId);
    const mp = sel?.mask?.pixels;
    for (let i = 0; i < n; i++) {
      let m = mp ? mp[i] : 255;
      if (sel?.mask?.invert) m = 255 - m;
      outA[i * 4] = m; outA[i * 4 + 1] = m; outA[i * 4 + 2] = m;
    }
  } else if (viewChannel === "roughness") {
    for (let i = 0; i < n; i++) { outA[i * 4] = outR[i]; outA[i * 4 + 1] = outR[i]; outA[i * 4 + 2] = outR[i]; }
  } else if (viewChannel === "metalness") {
    for (let i = 0; i < n; i++) { outA[i * 4] = outM[i]; outA[i * 4 + 1] = outM[i]; outA[i * 4 + 2] = outM[i]; }
  } else if (viewChannel === "height") {
    for (let i = 0; i < n; i++) { outA[i * 4] = outH[i]; outA[i * 4 + 1] = outH[i]; outA[i * 4 + 2] = outH[i]; }
  } else if (viewChannel === "normal") {
    for (let i = 0; i < n * 4; i += 4) { outA[i] = outN[i]; outA[i + 1] = outN[i + 1]; outA[i + 2] = outN[i + 2]; }
  } else if (viewChannel === "emissive") {
    const em = outExt.emissive;
    for (let i = 0; i < n; i++) { outA[i * 4] = em[i * 3]; outA[i * 4 + 1] = em[i * 3 + 1]; outA[i * 4 + 2] = em[i * 3 + 2]; }
  } else if (outExt[viewChannel]) {
    const buf = outExt[viewChannel];
    for (let i = 0; i < n; i++) { outA[i * 4] = buf[i]; outA[i * 4 + 1] = buf[i]; outA[i * 4 + 2] = buf[i]; }
  }

  return { res, albedo: outA, roughness: outR, metalness: outM, height: outH, normal: outN, ext: outExt, ...outExt };
}

export function sampleCompositedAtUV(comp, u, v) {
  const res = comp.res;
  const x = Math.min(res - 1, Math.max(0, Math.floor(u * res)));
  const y = Math.min(res - 1, Math.max(0, Math.floor((1 - v) * res)));
  const pid = y * res + x;
  const i4 = pid * 4;
  return {
    color: rgb01ToHex(comp.albedo[i4] / 255, comp.albedo[i4 + 1] / 255, comp.albedo[i4 + 2] / 255),
    roughness: comp.roughness[pid] / 255,
    metalness: comp.metalness[pid] / 255,
    height: (comp.height[pid] / 255 - 0.5) * 2,
  };
}

/**
 * Sample a smooth 3D Catmull-Rom spline along placed 3D surface control points.
 * Each point is { pos: [x,y,z] | Vector3, normal: [nx,ny,nz] | Vector3 }.
 */
export function sampleMeshSurfaceSpline3D(points, { closed = false, tension = 0.5, samplesPerSegment = 18 } = {}) {
  if (!Array.isArray(points) || points.length === 0) return [];
  const toArr = (v) => (Array.isArray(v) ? [v[0], v[1], v[2]] : [v?.x || 0, v?.y || 0, v?.z || 0]);
  const list = points.map((p) => ({ pos: toArr(p.pos || p.position), normal: toArr(p.normal || [0, 1, 0]) }));
  if (list.length === 1) return [list[0]];

  const segs = closed ? list.length : list.length - 1;
  const samples = Math.max(4, Math.min(48, Math.round(samplesPerSegment)));
  const tClamp = clamp(Number(tension ?? 0.5), 0.05, 1.0);
  const out = [];

  for (let s = 0; s < segs; s++) {
    const p0 = list[closed ? (s - 1 + list.length) % list.length : Math.max(0, s - 1)];
    const p1 = list[s];
    const p2 = list[(s + 1) % list.length];
    const p3 = list[closed ? (s + 2) % list.length : Math.min(list.length - 1, s + 2)];

    const steps = s === segs - 1 && !closed ? samples : samples;
    for (let k = 0; k <= steps; k++) {
      if (s > 0 && k === 0) continue;
      const u = k / steps;
      const u2 = u * u;
      const u3 = u2 * u;
      const cr = (v0, v1, v2, v3) => {
        const m1 = (v2 - v0) * tClamp;
        const m2 = (v3 - v1) * tClamp;
        const h00 = 2 * u3 - 3 * u2 + 1;
        const h10 = u3 - 2 * u2 + u;
        const h01 = -2 * u3 + 3 * u2;
        const h11 = u3 - u2;
        return h00 * v1 + h10 * m1 + h01 * v2 + h11 * m2;
      };
      const pos = [
        cr(p0.pos[0], p1.pos[0], p2.pos[0], p3.pos[0]),
        cr(p0.pos[1], p1.pos[1], p2.pos[1], p3.pos[1]),
        cr(p0.pos[2], p1.pos[2], p2.pos[2], p3.pos[2]),
      ];
      let nx = p1.normal[0] * (1 - u) + p2.normal[0] * u;
      let ny = p1.normal[1] * (1 - u) + p2.normal[1] * u;
      let nz = p1.normal[2] * (1 - u) + p2.normal[2] * u;
      const nl = Math.hypot(nx, ny, nz) || 1;
      out.push({ pos, normal: [nx / nl, ny / nl, nz / nl] });
    }
  }
  return out;
}

function srgbToLinear(c) {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}
function linearToSrgb(c) {
  const v = clamp(c, 0, 1);
  return v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
}

/**
 * Normalize an AlloyStudio-compatible 3D object-space multi-point Gaussian gradient.
 */
export function normalizePointGradient3D(input) {
  const rawPts = Array.isArray(input?.points) ? input.points : [];
  const points = (rawPts.length ? rawPts : [
    { id: "gp_1", position: [0.65, 0.55, 0.55], color: "#e8c27a", radius: 1.15, weight: 1.0 },
    { id: "gp_2", position: [-0.65, -0.35, -0.45], color: "#3d74b8", radius: 1.2, weight: 1.0 },
  ]).slice(0, 12).map((p, idx) => ({
    id: p.id || `gp_${idx + 1}`,
    position: [
      clamp(Number(p.position?.[0] ?? 0), -2, 2),
      clamp(Number(p.position?.[1] ?? 0), -2, 2),
      clamp(Number(p.position?.[2] ?? 0), -2, 2),
    ],
    color: p.color || "#e8c27a",
    radius: clamp(Number(p.radius ?? 1.1), 0.12, 3.5),
    weight: clamp(Number(p.weight ?? 1.0), 0.05, 1.0),
  }));
  return {
    schema: "alloy.point-gradient.v1",
    space: input?.space || "object-sphere",
    falloff: clamp(Number(input?.falloff ?? 1.0), 0.2, 3.0),
    background: input?.background || "#0b0b0b",
    points,
  };
}

export function evaluatePointGradient3DAt(grad, localPos) {
  const falloffScale = clamp(grad.falloff ?? 1.0, 0.2, 3.0);
  const bgRgb = hexToRgb01(grad.background || "#0b0b0b").map(srgbToLinear);
  let sumR = 0, sumG = 0, sumB = 0, totalW = 0, peakW = 0;

  for (let i = 0; i < grad.points.length; i++) {
    const pt = grad.points[i];
    const dx = localPos[0] - pt.position[0];
    const dy = localPos[1] - pt.position[1];
    const dz = localPos[2] - pt.position[2];
    const rad = Math.max(0.08, pt.radius * falloffScale);
    const d2 = dx * dx + dy * dy + dz * dz;
    const w = pt.weight * Math.exp(-d2 / (rad * rad));
    if (w <= 1e-4) continue;
    const [pr, pg, pb] = hexToRgb01(pt.color);
    sumR += srgbToLinear(pr) * w;
    sumG += srgbToLinear(pg) * w;
    sumB += srgbToLinear(pb) * w;
    totalW += w;
    if (w > peakW) peakW = w;
  }

  let linR, linG, linB;
  if (totalW <= 1e-4) {
    [linR, linG, linB] = bgRgb;
  } else {
    const blend = clamp(peakW, 0, 1);
    const ptR = sumR / totalW;
    const ptG = sumG / totalW;
    const ptB = sumB / totalW;
    linR = bgRgb[0] * (1 - blend) + ptR * blend;
    linG = bgRgb[1] * (1 - blend) + ptG * blend;
    linB = bgRgb[2] * (1 - blend) + ptB * blend;
  }

  return [
    Math.round(linearToSrgb(linR) * 255),
    Math.round(linearToSrgb(linG) * 255),
    Math.round(linearToSrgb(linB) * 255),
  ];
}

/**
 * Evaluate and apply a 3D multi-point interpolated gradient across mesh texels in object-sphere 3D space.
 */
export function applyPointGradient3DToLayer({ layer, cues, grad, brush, editingMask = false }) {
  if (!layer || !cues || layer.locked) return false;
  const g = normalizePointGradient3D(grad);
  const res = cues.res;
  const n = res * res;
  const cx = cues.sphereCenter?.[0] || 0;
  const cy = cues.sphereCenter?.[1] || 0;
  const cz = cues.sphereCenter?.[2] || 0;
  const invRad = 1 / Math.max(0.25, cues.sphereRadius || 1.45);
  const partFilter = layer.partFilter ?? -1;
  const op = clamp(brush?.opacity ?? 1.0, 0.05, 1.0);
  const fillCh = brush?.gradientChannels || (brush?.tool === "gradient" ? getToolChannels(brush) : brush?.fillChannels) || createDefaultChannelMap(false);
  const roughByte = Math.round(clamp(brush?.roughness ?? 0.34, 0.02, 1) * 255);
  const metalByte = Math.round(clamp(brush?.metalness ?? 0.12, 0, 1) * 255);
  const heightByte = Math.round(clamp(((brush?.height ?? 0.25) * 0.5 + 0.5), 0, 1) * 255);

  const falloffScale = clamp(g.falloff ?? 1.0, 0.2, 3.0);
  const bgLin = hexToRgb01(g.background || "#0b0b0b").map(srgbToLinear);
  const prePts = g.points.map((pt) => {
    const rgb = hexToRgb01(pt.color).map(srgbToLinear);
    const rad = Math.max(0.08, pt.radius * falloffScale);
    return {
      x: pt.position[0], y: pt.position[1], z: pt.position[2],
      r: rgb[0], g: rgb[1], b: rgb[2],
      invR2: 1 / (rad * rad),
      weight: pt.weight,
    };
  });

  if (editingMask) {
    if (!layer.mask) layer.mask = { enabled: true, opacity: 1, invert: false, pixels: new Uint8Array(n).fill(255) };
    layer.mask.enabled = true;
    const mp = layer.mask.pixels;
    for (let i = 0; i < n; i++) {
      if (!cues.covered[i]) continue;
      if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
      const lx = (cues.worldPos[i * 3] - cx) * invRad;
      const ly = (cues.worldPos[i * 3 + 1] - cy) * invRad;
      const lz = (cues.worldPos[i * 3 + 2] - cz) * invRad;
      let peakW = 0;
      for (let k = 0; k < prePts.length; k++) {
        const pt = prePts[k];
        const d2 = (lx - pt.x) ** 2 + (ly - pt.y) ** 2 + (lz - pt.z) ** 2;
        const w = pt.weight * Math.exp(-d2 * pt.invR2);
        if (w > peakW) peakW = w;
      }
      const val = Math.round(clamp(peakW, 0, 1) * 255);
      mp[i] = Math.round(mp[i] * (1 - op) + val * op);
    }
    return true;
  }

  const px = layer.pixels;
  if (!px) return false;
  if (!layer.channels) layer.channels = createDefaultChannelMap(false);
  for (const k of Object.keys(fillCh)) {
    if (fillCh[k]) layer.channels[k] = true;
  }
  const colorExtKeys = ["emissive", ...EXTRA_COLOR_KEYS].filter((k) => !!fillCh[k]);
  if (fillCh.emissive) layer.channels.emissiveIntensity = true;
  if (fillCh.sheenColor) layer.channels.sheen = true;
  if (fillCh.attenuationColor) layer.channels.transmission = true;

  const colorExtBufs = colorExtKeys.map((k) => ensureExtPixelChannel(px, k, res));
  const glowScalarBuf = fillCh.emissive || fillCh.emissiveIntensity ? ensureExtPixelChannel(px, "emissiveIntensity", res) : null;
  const glowByte = encodeScalarChannelByte("emissiveIntensity", Number(brush?.extValues?.emissiveIntensity ?? 1.0));
  const scalarExtList = EXTENDED_PBR_KEYS
    .filter((k) => k !== "emissive" && k !== "emissiveIntensity" && !!fillCh[k])
    .map((k) => {
      const defVal = PAINT_PBR_CHANNELS.find((c) => c.key === k)?.default ?? 0.5;
      const val = brush?.extValues?.[k] ?? defVal;
      return {
        key: k,
        buf: ensureExtPixelChannel(px, k, res),
        byteVal: encodeScalarChannelByte(k, Number(val)),
      };
    });
  // If sheenColor or attenuationColor was enabled without manually enabling sheen/transmission scalar, populate visible default scalar coverage
  if (fillCh.sheenColor && !fillCh.sheen) {
    scalarExtList.push({
      key: "sheen",
      buf: ensureExtPixelChannel(px, "sheen", res),
      byteVal: encodeScalarChannelByte("sheen", Number(brush?.extValues?.sheen || 0.85)),
    });
  }
  if (fillCh.attenuationColor && !fillCh.transmission) {
    scalarExtList.push({
      key: "transmission",
      buf: ensureExtPixelChannel(px, "transmission", res),
      byteVal: encodeScalarChannelByte("transmission", Number(brush?.extValues?.transmission || 0.85)),
    });
  }

  for (let i = 0; i < n; i++) {
    if (partFilter >= 0 && cues.partAt[i] >= 0 && cues.partAt[i] !== partFilter) continue;
    const i3 = i * 3;
    const lx = (cues.worldPos[i3] - cx) * invRad;
    const ly = (cues.worldPos[i3 + 1] - cy) * invRad;
    const lz = (cues.worldPos[i3 + 2] - cz) * invRad;

    let sumR = 0, sumG = 0, sumB = 0, totalW = 0, peakW = 0;
    for (let k = 0; k < prePts.length; k++) {
      const pt = prePts[k];
      const d2 = (lx - pt.x) ** 2 + (ly - pt.y) ** 2 + (lz - pt.z) ** 2;
      const w = pt.weight * Math.exp(-d2 * pt.invR2);
      if (w <= 1e-4) continue;
      sumR += pt.r * w;
      sumG += pt.g * w;
      sumB += pt.b * w;
      totalW += w;
      if (w > peakW) peakW = w;
    }

    let linR = bgLin[0], linG = bgLin[1], linB = bgLin[2];
    if (totalW > 1e-4) {
      const blend = clamp(peakW, 0, 1);
      linR = bgLin[0] * (1 - blend) + (sumR / totalW) * blend;
      linG = bgLin[1] * (1 - blend) + (sumG / totalW) * blend;
      linB = bgLin[2] * (1 - blend) + (sumB / totalW) * blend;
    }
    const cr = Math.round(linearToSrgb(linR) * 255);
    const cg = Math.round(linearToSrgb(linG) * 255);
    const cb = Math.round(linearToSrgb(linB) * 255);

    const i4 = i * 4;
    if (fillCh.albedo) {
      px.albedo[i4] = Math.round(px.albedo[i4] * (1 - op) + cr * op);
      px.albedo[i4 + 1] = Math.round(px.albedo[i4 + 1] * (1 - op) + cg * op);
      px.albedo[i4 + 2] = Math.round(px.albedo[i4 + 2] * (1 - op) + cb * op);
      px.albedo[i4 + 3] = Math.min(255, Math.round(px.albedo[i4 + 3] + (255 - px.albedo[i4 + 3]) * op));
    }
    for (let c = 0; c < colorExtBufs.length; c++) {
      const cbBuf = colorExtBufs[c];
      const oldA = cbBuf.alpha[i] / 255;
      const outA = op + oldA * (1 - op);
      if (outA > 1e-4) {
        cbBuf.values[i3] = Math.round((cr * op + cbBuf.values[i3] * oldA * (1 - op)) / outA);
        cbBuf.values[i3 + 1] = Math.round((cg * op + cbBuf.values[i3 + 1] * oldA * (1 - op)) / outA);
        cbBuf.values[i3 + 2] = Math.round((cb * op + cbBuf.values[i3 + 2] * oldA * (1 - op)) / outA);
      }
      cbBuf.alpha[i] = Math.min(255, Math.round(outA * 255));
    }
    if (glowScalarBuf) {
      const oldG = glowScalarBuf.alpha[i] / 255;
      const outG = op + oldG * (1 - op);
      if (outG > 1e-4) glowScalarBuf.values[i] = Math.round((glowByte * op + glowScalarBuf.values[i] * oldG * (1 - op)) / outG);
      glowScalarBuf.alpha[i] = Math.min(255, Math.round(outG * 255));
    }
    if (fillCh.roughness) {
      px.roughness[i] = Math.round(px.roughness[i] * (1 - op) + roughByte * op);
      if (px.roughAlpha) px.roughAlpha[i] = Math.min(255, Math.round(px.roughAlpha[i] + (255 - px.roughAlpha[i]) * op));
    }
    if (fillCh.metalness) {
      px.metalness[i] = Math.round(px.metalness[i] * (1 - op) + metalByte * op);
      if (px.metalAlpha) px.metalAlpha[i] = Math.min(255, Math.round(px.metalAlpha[i] + (255 - px.metalAlpha[i]) * op));
    }
    if (fillCh.height) {
      px.height[i] = Math.round(px.height[i] * (1 - op) + heightByte * op);
      if (px.heightAlpha) px.heightAlpha[i] = Math.min(255, Math.round(px.heightAlpha[i] + (255 - px.heightAlpha[i]) * op));
    }
    for (let s = 0; s < scalarExtList.length; s++) {
      const se = scalarExtList[s];
      const oldS = se.buf.alpha[i] / 255;
      const outS = op + oldS * (1 - op);
      if (outS > 1e-4) se.buf.values[i] = Math.round((se.byteVal * op + se.buf.values[i] * oldS * (1 - op)) / outS);
      se.buf.alpha[i] = Math.min(255, Math.round(outS * 255));
    }
  }
  return true;
}
