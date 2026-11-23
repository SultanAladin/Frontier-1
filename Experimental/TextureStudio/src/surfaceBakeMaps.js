export const SURFACE_BAKE_MAPS = {
  "base-color": {
    label: "Base color",
    type: "color",
    colorSpace: "sRGB",
    swatch: "color",
    icon: "color",
    description:
      "Unlit procedural body color. No environment, reflection or AO shadows are painted into it.",
  },
  roughness: {
    label: "Roughness",
    type: "scalar",
    colorSpace: "linear",
    swatch: "curvature",
    icon: "mask",
    description:
      "Procedural per-surface roughness, including grain/material variation. Use non-color sampling.",
  },
  metalness: {
    label: "Metalness",
    type: "scalar",
    colorSpace: "linear",
    swatch: "coverage",
    icon: "mask",
    description:
      "Conducting versus dielectric coverage. Mineral sand stays non-metallic.",
  },
  normal: {
    label: "Normal +Y",
    type: "normal",
    colorSpace: "linear",
    space: "tangent",
    swatch: "normal",
    icon: "normal",
    description:
      "OpenGL +Y tangent normal of the flat material patch, generated from the same procedural relief.",
  },
  height: {
    label: "Height",
    type: "scalar",
    colorSpace: "linear",
    swatch: "height",
    icon: "height",
    description:
      "Signed procedural surface relief encoded around 0.5; scale and 8-bit range are recorded in the recipe manifest.",
  },
  emission: {
    label: "Emission",
    type: "color",
    colorSpace: "sRGB",
    swatch: "dust",
    icon: "light",
    description:
      "Procedural self-emission, not reflected studio lighting. Intensity multiplier is recorded.",
  },
};
