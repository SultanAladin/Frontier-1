// Geometry-only bake outputs. Every enabled tile has a real worker evaluator.
const map = (label, type, group, description, extra = {}) => ({
  label,
  type,
  group,
  description,
  colorSpace: "linear",
  ...extra,
});
export const MESH_MAPS = {
  normal: map(
    "Tangent normal",
    "normal",
    "Normals",
    "Projected high-surface normal in the low mesh UV tangent frame.",
    { space: "tangent", swatch: "normal" },
  ),
  "world-normal": map(
    "World normal",
    "vector",
    "Normals",
    "Projected high-surface normal in scene/world axes; XYZ is encoded from −1…1 to 0…1.",
    { space: "world", swatch: "normal-world" },
  ),
  "object-normal": map(
    "Object normal",
    "vector",
    "Normals",
    "Projected normal in the low target's original local axes, undoing its imported world transform.",
    { space: "object", swatch: "normal-object" },
  ),
  "bent-normal": map(
    "Bent tangent normal",
    "normal",
    "Normals",
    "Average unoccluded hemisphere direction in the low UV tangent frame; uses AO distance and samples.",
    { space: "tangent", swatch: "normal" },
  ),
  "bent-world-normal": map(
    "Bent world normal",
    "vector",
    "Normals",
    "Average unoccluded hemisphere direction in world axes.",
    { space: "world", swatch: "normal-world" },
  ),
  "bent-object-normal": map(
    "Bent object normal",
    "vector",
    "Normals",
    "Average unoccluded hemisphere direction in the low target's original local axes.",
    { space: "object", swatch: "normal-object" },
  ),
  "bevel-normal": map(
    "Bevel normal",
    "normal",
    "Normals",
    "Geometry-derived rounded-edge normal approximation in tangent space. Changes shading, not topology.",
    { space: "tangent", swatch: "normal", derived: true },
  ),
  occlusion: map(
    "Ambient occlusion",
    "scalar",
    "Surface",
    "Hemisphere visibility against the high mesh: white exposed, black occluded.",
    { swatch: "ao" },
  ),
  "occlusion-secondary": map(
    "Secondary AO",
    "scalar",
    "Surface",
    "A second hemisphere-visibility bake with its own distance, useful for broader or finer occlusion.",
    { swatch: "ao" },
  ),
  curvature: map(
    "Curvature",
    "scalar",
    "Surface",
    "Signed local normal variation estimate: 0.5 flat, brighter convex, darker concave.",
    { swatch: "curvature" },
  ),
  convexity: map(
    "Convexity",
    "scalar",
    "Surface",
    "Positive signed curvature isolated as a bright-edge mask; controlled by curvature radius and mask strength.",
    { swatch: "curvature" },
  ),
  cavity: map(
    "Cavity / concavity",
    "scalar",
    "Surface",
    "Negative signed curvature isolated as a bright-recess mask, not an alias of ambient occlusion.",
    { swatch: "ao" },
  ),
  thickness: map(
    "Thickness",
    "scalar",
    "Surface",
    "Inward high-mesh ray distance divided by thickness range: white thicker, black thinner or no inward hit.",
    { swatch: "height" },
  ),
  height: map(
    "Projection height",
    "scalar",
    "Surface",
    "Signed high-to-low distance along the low normal; 0.5 is zero, encoded using front/back range.",
    { swatch: "height" },
  ),
  "vector-displacement": map(
    "Vector displacement",
    "vector",
    "Surface",
    "Full projected high-minus-low XYZ offset in world axes; 0.5 per channel is zero. Not a multires sculpt solver.",
    { space: "world", swatch: "normal-object" },
  ),
  position: map(
    "Position",
    "vector",
    "Coordinates & IDs",
    "Projected high-surface XYZ normalized to the source world bounding box.",
    { swatch: "position" },
  ),
  id: map(
    "Object ID",
    "color",
    "Coordinates & IDs",
    "24-bit source-object identity, encoded little-endian RGB; not a UV chart or material ID.",
    { swatch: "id" },
  ),
  "uv-island": map(
    "UV island ID",
    "color",
    "Coordinates & IDs",
    "A stable, distinct display color for each connected low UV chart. The manifest records the chart palette.",
    { swatch: "id" },
  ),
  uv: map(
    "UV coordinates",
    "vector",
    "Coordinates & IDs",
    "Low target UV0 coordinates in R/G, blue = 1. Coordinates, not a UV-island ID.",
    { swatch: "position" },
  ),
  wireframe: map(
    "UV wireframe",
    "scalar",
    "Coordinates & IDs",
    "Low triangle edges in texture space, using a pixel-width control. Includes internal triangle edges.",
    { swatch: "wireframe" },
  ),
  coverage: map(
    "UV coverage",
    "scalar",
    "Masks",
    "White on covered low UV texels; transparent/black outside. Independent of high projection hits.",
    { swatch: "coverage" },
  ),
  alpha: map(
    "Alpha / hit mask",
    "scalar",
    "Masks",
    "White where a high surface was projected, black for holes/misses. Does not transfer material transparency.",
    { swatch: "coverage" },
  ),
  "bevel-mask": map(
    "Bevel mask",
    "scalar",
    "Masks",
    "Angular change caused by rounded-edge normal averaging, isolated as a grayscale mask.",
    { swatch: "curvature", derived: true },
  ),
  dust: map(
    "Dust mask",
    "scalar",
    "Masks",
    "Derived upward-facing deposition mask, using world normal, chosen up axis, slope falloff and occlusion. Not a particle simulation.",
    { swatch: "dust", derived: true },
  ),
  dirt: map(
    "Dirt mask",
    "scalar",
    "Masks",
    "Derived occlusion plus concavity mask, useful for recesses and grime. No painted dirt texture is invented.",
    { swatch: "ao", derived: true },
  ),
  "edge-wear": map(
    "Edge wear mask",
    "scalar",
    "Masks",
    "Derived convexity weighted by surface exposure, for exposed-edge masking; not simulated abrasion.",
    { swatch: "curvature", derived: true },
  ),
  "base-color": map(
    "Vertex color",
    "color",
    "Color",
    "Interpolated high vertex color, converted from linear to sRGB for PNG. White if no vertex color exists; no material texture transfer.",
    { colorSpace: "sRGB", swatch: "color" },
  ),
};
export const DEFAULT_MESH_CHANNELS = [
  "normal",
  "world-normal",
  "occlusion",
  "curvature",
  "thickness",
  "position",
  "id",
  "height",
  "coverage",
];
export const MESH_MAP_PRESETS = {
  Essential: [
    "normal",
    "world-normal",
    "occlusion",
    "curvature",
    "position",
    "thickness",
  ],
  Masks: [
    "occlusion",
    "curvature",
    "convexity",
    "cavity",
    "uv-island",
    "dust",
    "dirt",
    "edge-wear",
    "bevel-mask",
    "alpha",
  ],
  Normals: [
    "normal",
    "world-normal",
    "object-normal",
    "bent-normal",
    "bent-world-normal",
    "bent-object-normal",
    "bevel-normal",
  ],
};
// Research inventory, not pretend selectable outputs: these need data the
// geometry-only importer/worker deliberately does not load or evaluate.
export const OTHER_BAKE_FAMILIES = [
  {
    label: "Material channels",
    types:
      "Albedo, roughness / gloss, metalness, specular, emission, material opacity, transferred textures",
    requirement:
      "Require high-material / texture evaluation. Material patch mode exports the current procedural material separately.",
  },
  {
    label: "Material / group IDs",
    types: "Material ID, collection / group ID",
    requirement:
      "Require preserved material slots or grouping metadata. Object and UV-island IDs are available above; they are not substituted for these IDs.",
  },
  {
    label: "Lighting passes",
    types:
      "Combined / complete, diffuse, specular / glossy, indirect, transmission, shadow, environment",
    requirement:
      "Require a lighting/material integrator; AO is visibility, not baked scene lighting.",
  },
];
export function uvIslandColor(id) {
  // Bijective odd multiplication mod 2^24: chart colors are deterministic and
  // distinct for all supported chart counts. Zero remains reserved for outside.
  const code = (Math.imul(id, 0x9e3779) >>> 0) & 0xffffff;
  return [
    (code & 255) / 255,
    ((code >>> 8) & 255) / 255,
    ((code >>> 16) & 255) / 255,
  ];
}
