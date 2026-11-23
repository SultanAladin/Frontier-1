import {
  normalizePointGradient,
  defaultPointGradient,
} from "./pointGradient.js";
import {
  normalizeMaterialGenerator,
  MATERIAL_GENERATOR_ID,
} from "./materialGeneratorModel.js";
import { normalizeTextureSource } from "./textureSource.js";
// Channel/source configuration ported from ChannelPropertyPanel.html.
// These are authoring targets; no texture-atlas allocation is implied.
export const PAINT_CHANNELS = [
  {
    id: "baseColor",
    label: "Base color",
    referenceLabel: "Base Colour",
    group: "Surface",
    color: "#b87333",
    short: "C",
    edit: "color",
    value: "#b87333",
  },
  {
    id: "metalness",
    label: "Metalness",
    referenceLabel: "Metallic",
    group: "Surface",
    color: "#8b5cf6",
    short: "M",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0,
  },
  {
    id: "roughness",
    label: "Roughness",
    group: "Surface",
    color: "#3b82f6",
    short: "R",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0.5,
  },
  {
    id: "height",
    label: "Height",
    group: "Surface",
    color: "#8a8a8a",
    short: "H",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0.5,
  },
  {
    id: "normal",
    label: "Normal",
    group: "Surface",
    color: "#10b981",
    short: "N",
    edit: "derived",
    source: "height",
  },
  {
    id: "opacity",
    label: "Opacity",
    group: "Surface",
    color: "#94a3b8",
    short: "A",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 1,
  },
  {
    id: "emission",
    label: "Emission",
    referenceLabel: "Emissive",
    group: "Radiance",
    color: "#f59e0b",
    short: "E",
    edit: "color",
    value: "#000000",
  },
  {
    id: "occlusion",
    label: "Ambient occlusion",
    group: "Radiance",
    color: "#6b7280",
    short: "AO",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 1,
  },
  {
    id: "anisotropy",
    label: "Anisotropy",
    group: "Reflectance",
    color: "#22d3ee",
    short: "AN",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0,
  },
  {
    id: "anisotropyAngle",
    label: "Anisotropy angle",
    group: "Reflectance",
    color: "#0ea5e9",
    short: "°",
    edit: "scalar",
    min: 0,
    max: 360,
    value: 0,
    unit: "°",
  },
  {
    id: "clearcoat",
    label: "Clearcoat",
    group: "Reflectance",
    color: "#e2e8f0",
    short: "CC",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0,
  },
  {
    id: "transmission",
    label: "Transmission",
    group: "Reflectance",
    color: "#7dd3fc",
    short: "TR",
    edit: "scalar",
    min: 0,
    max: 1,
    value: 0,
  },
  {
    id: "refractionIndex",
    label: "Refraction index",
    group: "Reflectance",
    color: "#a78bfa",
    short: "IOR",
    edit: "scalar",
    min: 1,
    max: 3,
    value: 1.5,
  },
  {
    id: "sheen",
    label: "Sheen",
    group: "Scattering",
    color: "#f472b6",
    short: "SH",
    edit: "color",
    value: "#3a3a3a",
  },
  {
    id: "subsurface",
    label: "Subsurface",
    group: "Scattering",
    color: "#fb7185",
    short: "SS",
    edit: "color",
    value: "#d98a72",
  },
];
export const CHANNEL_GROUPS = [
  "Surface",
  "Radiance",
  "Reflectance",
  "Scattering",
];
export const CHANNEL_GENERATORS = [
  {
    id: "CurvatureEdges",
    label: "Curvature",
    group: "Mask",
    note: "Needs curvature mesh map",
    parameters: [
      { key: "Balance", label: "Balance", value: 0.5 },
      { key: "Contrast", label: "Contrast", value: 0.7 },
      { key: "Radius", label: "Radius", value: 0.25 },
    ],
  },
  {
    id: "AmbientOcclusion",
    label: "Ambient Occlusion",
    group: "Mask",
    note: "Needs occlusion mesh map",
    parameters: [
      { key: "Spread", label: "Spread", value: 0.4 },
      { key: "Contrast", label: "Contrast", value: 0.6 },
    ],
  },
  {
    id: "Thickness",
    label: "Thickness",
    group: "Mask",
    note: "Needs thickness mesh map",
    parameters: [
      { key: "Depth", label: "Depth", value: 0.5 },
      { key: "Contrast", label: "Contrast", value: 0.5 },
    ],
  },
  {
    id: "PositionGradient",
    label: "Position Gradient",
    group: "Mask",
    note: "World-axis ramp configuration",
    parameters: [
      { key: "Origin", label: "Origin", value: 0.5 },
      { key: "Falloff", label: "Falloff", value: 0.35 },
    ],
  },
  {
    id: "MetalEdgeWear",
    label: "Metal Edge Wear",
    group: "Wear",
    note: "Needs curvature / grunge inputs",
    parameters: [
      { key: "Intensity", label: "Intensity", value: 0.6 },
      { key: "Softness", label: "Softness", value: 0.3 },
      { key: "Roughness", label: "Grain", value: 0.45 },
    ],
  },
  {
    id: "DirtAccumulation",
    label: "Dirt",
    group: "Wear",
    note: "Needs occlusion input",
    parameters: [
      { key: "Amount", label: "Amount", value: 0.5 },
      { key: "Scale", label: "Scale", value: 0.3 },
    ],
  },
  {
    id: "WaterRunoff",
    label: "Water Runoff",
    group: "Wear",
    note: "Gravity-streak configuration",
    parameters: [
      { key: "Length", label: "Length", value: 0.55 },
      { key: "Density", label: "Density", value: 0.4 },
      { key: "Gravity", label: "Gravity", value: 0.8 },
    ],
  },
  {
    id: "PerlinNoise",
    label: "Perlin Noise",
    group: "Procedural",
    note: "Fractal-noise configuration",
    parameters: [
      { key: "Scale", label: "Scale", value: 0.4 },
      { key: "Octaves", label: "Octaves", value: 0.5 },
      { key: "Contrast", label: "Contrast", value: 0.5 },
    ],
  },
  {
    id: "VoronoiCells",
    label: "Voronoi",
    group: "Procedural",
    note: "Cellular configuration",
    parameters: [
      { key: "Density", label: "Density", value: 0.35 },
      { key: "Jitter", label: "Jitter", value: 0.7 },
    ],
  },
  {
    id: "BrushedAnisotropy",
    label: "Brushed Metal",
    group: "Procedural",
    note: "Directional-grain configuration",
    parameters: [
      { key: "Direction", label: "Angle", value: 0 },
      { key: "Grain", label: "Grain", value: 0.6 },
    ],
  },
];
export function normalizeGenerator(raw = {}) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  if (raw.id === MATERIAL_GENERATOR_ID) return normalizeMaterialGenerator(raw);
  const definition = CHANNEL_GENERATORS.find((g) => g.id === raw.id);
  if (!definition) return null;
  const parameters = Object.fromEntries(
    definition.parameters.map((p) => [
      p.key,
      Number.isFinite(Number(raw.parameters?.[p.key]))
        ? Math.min(1, Math.max(0, Number(raw.parameters[p.key])))
        : p.value,
    ]),
  );
  return { id: definition.id, parameters };
}
export function normalizeChannelSettings(raw = {}) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) raw = {};
  return Object.fromEntries(
    PAINT_CHANNELS.map((c) => {
      const r = raw[c.id] && typeof raw[c.id] === "object" ? raw[c.id] : {};
      const mode =
        c.edit === "derived"
          ? "derived"
          : ["value", "texture", "paint", "generator", "gradient"].includes(
                r.mode,
              )
            ? r.mode === "paint"
              ? "texture"
              : r.mode
            : "value";
      const value =
        c.edit === "color"
          ? /^#[a-f0-9]{6}$/i.test(r.value || "")
            ? r.value.toLowerCase()
            : c.value
          : c.edit === "scalar"
            ? Number.isFinite(Number(r.value))
              ? Math.min(c.max, Math.max(c.min, Number(r.value)))
              : c.value
            : null;
      return [
        c.id,
        {
          mode,
          value,
          generator: normalizeGenerator(r.generator || {}),
          gradient: r.gradient
            ? normalizePointGradient(r.gradient)
            : mode === "gradient"
              ? defaultPointGradient()
              : null,
          texture: normalizeTextureSource(r.texture),
        },
      ];
    }),
  );
}
export function togglePaintChannel(enabled, id, on = !enabled.includes(id)) {
  const set = new Set(enabled);
  if (on) {
    set.add(id);
    if (id === "normal") set.add("height");
  } else {
    set.delete(id);
    if (id === "height") set.delete("normal");
  }
  return PAINT_CHANNELS.filter((c) => set.has(c.id)).map((c) => c.id);
}
