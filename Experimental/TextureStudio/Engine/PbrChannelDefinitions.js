/*==========================================================================================================================================
                                                        PBRCHANNELDEFINITIONS.JS
==========================================================================================================================================*/
/* Single-source-of-truth Full PBR channel registry (19 scalar/core/emissive + 3 extra tint channels = 22 channels total) shared across
   Texture Paint, Pattern Studio, and Stamp Studio without circular dependencies. */

function clamp(v, lo = 0, hi = 1) {
  return Math.max(lo, Math.min(hi, v));
}

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
  { key: "specularColor", label: "Spec Colour", short: "SpecCol", sub: "Specular Tint", kind: "spec", dot: "#9ea2aa", type: "color", default: "#ffffff", defaultVal: "#ffffff", extended: true, isColor: true },
  { key: "sheenColor", label: "Sheen Colour", short: "SheenCol", sub: "Fabric Velour Tint", kind: "sheen", dot: "#9ea2aa", type: "color", default: "#ffffff", defaultVal: "#ffffff", extended: true, isColor: true },
  { key: "attenuationColor", label: "Subsurf Tint", short: "SubCol", sub: "Volume Attenuation", kind: "trans", dot: "#9ea2aa", type: "color", default: "#ffffff", defaultVal: "#ffffff", extended: true, isColor: true },
];
export const EXTRA_COLOR_KEYS = EXTRA_COLOR_CHANNELS.map((c) => c.key);
export const EXTRA_COLOR_CHANNEL_ROWS = EXTRA_COLOR_CHANNELS.map((c) => [c.key, c.label, c.sub, c.kind, c.dot]);

export const ALL_PAINT_CHANNELS = [...PAINT_PBR_CHANNELS, ...EXTRA_COLOR_CHANNELS];
export const ALL_PAINT_CHANNEL_ROWS = [...PAINT_PBR_CHANNEL_ROWS, ...EXTRA_COLOR_CHANNEL_ROWS];
export const ALL_PAINT_CHANNEL_MAP = Object.fromEntries(ALL_PAINT_CHANNELS.map((c) => [c.key, c]));
export const ALL_EXTENDED_PBR_KEYS = [...EXTENDED_PBR_KEYS, ...EXTRA_COLOR_KEYS];

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
  for (const c of ALL_PAINT_CHANNELS) out[c.key] = 1;
  return { ...out, ...overrides };
}

export function createDefaultChannelBlends(overrides = {}) {
  const out = {};
  for (const c of ALL_PAINT_CHANNELS) out[c.key] = "Inherit";
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
    specularColor: "#ffffff",
    sheenColor: "#ffffff",
    attenuationColor: "#ffffff",
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
