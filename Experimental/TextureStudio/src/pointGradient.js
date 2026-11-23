export const GRADIENT_POINT_LIMIT = 12;
export const GRADIENT_SCHEMA = "alloy.point-gradient.v1";
const colour = (v, fallback) =>
  typeof v === "string" && /^#[\da-f]{6}$/i.test(v)
    ? v.toLowerCase()
    : fallback;
const bounded = (v, d, min, max) =>
  v != null && Number.isFinite(Number(v))
    ? Math.min(max, Math.max(min, Number(v)))
    : d;
export function defaultPointGradient(mask = false) {
  return {
    schema: GRADIENT_SCHEMA,
    space: "object-sphere",
    background: mask ? "#000000" : "#b87333",
    points: [
      {
        id: "gradient-a",
        position: [-0.4, 0.2, 0.55],
        color: mask ? "#ffffff" : "#e5ac6b",
        radius: 0.8,
        weight: 1,
      },
      {
        id: "gradient-b",
        position: [0.45, -0.2, 0.45],
        color: mask ? "#000000" : "#6489cc",
        radius: 0.8,
        weight: 1,
      },
    ],
  };
}
export function normalizePointGradient(raw, mask = false) {
  if (raw == null) return null;
  if (
    typeof raw !== "object" ||
    Array.isArray(raw) ||
    raw.schema !== GRADIENT_SCHEMA ||
    raw.space !== "object-sphere" ||
    !Array.isArray(raw.points) ||
    raw.points.length > GRADIENT_POINT_LIMIT
  )
    throw new Error(
      "Choose a valid surface-point gradient with at most 12 points.",
    );
  const ids = new Set();
  const points = raw.points.map((point) => {
    if (
      !point ||
      typeof point.id !== "string" ||
      !/^[a-zA-Z0-9_-]{1,80}$/.test(point.id) ||
      ids.has(point.id)
    )
      throw new Error("Gradient points must have unique valid identities.");
    ids.add(point.id);
    if (
      !Array.isArray(point.position) ||
      point.position.length !== 3 ||
      point.position.some((v) => typeof v !== "number" || !Number.isFinite(v))
    )
      throw new Error(
        "Gradient point positions must contain three finite coordinates.",
      );
    return {
      id: point.id,
      position: point.position.map((v) => bounded(v, 0, -2, 2)),
      color: colour(point.color, mask ? "#ffffff" : "#b87333"),
      radius: bounded(point.radius, 0.8, 0, 3),
      weight: bounded(point.weight, 1, 0, 1),
    };
  });
  return {
    schema: GRADIENT_SCHEMA,
    space: "object-sphere",
    background: colour(raw.background, mask ? "#000000" : "#b87333"),
    points,
  };
}
export const srgbToLinear = (v) =>
  v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
export const linearToSRGB = (v) =>
  v <= 0.0031308 ? v * 12.92 : 1.055 * Math.max(0, v) ** (1 / 2.4) - 0.055;
export const linearColour = (hex) =>
  [1, 3, 5].map((i) => srgbToLinear(parseInt(hex.slice(i, i + 2), 16) / 255));
// Gaussian distance blending in a normalized object-space bounding sphere.
// Radius zero contributes nothing; weight zero mutes a point. Colour is blended
// in linear RGB. This is a surface field, NOT camera/screen or teapot UV paint.
export function compilePointGradient(raw) {
  const gradient = normalizePointGradient(raw),
    points = gradient.points.map((p) => ({
      ...p,
      linear: linearColour(p.color),
    })),
    background = linearColour(gradient.background);
  return (position) => {
    const sum = [0, 0, 0];
    let total = 0;
    for (const p of points) {
      if (p.weight === 0 || p.radius === 0) continue;
      const d2 = p.position.reduce((a, v, i) => a + (v - position[i]) ** 2, 0),
        w = p.weight * Math.exp(-d2 / (p.radius * p.radius));
      total += w;
      for (let i = 0; i < 3; i++) sum[i] += p.linear[i] * w;
    }
    return total > 1e-8 ? sum.map((v) => v / total) : background;
  };
}
export function gradientMaskCoverage(linear, mask) {
  let v = linear[0] * 0.2126 + linear[1] * 0.7152 + linear[2] * 0.0722;
  if (mask.inverted) v = 1 - v;
  return mask.enabled === false
    ? 1
    : 1 + ((v - 1) * bounded(mask.strength, 100, 0, 100)) / 100;
}
export function rasterizePointGradient(
  raw,
  { edge = 128, mask = null, z = 0 } = {},
) {
  const sample = compilePointGradient(raw),
    pixels = new Uint8ClampedArray(edge * edge * 4);
  for (let y = 0; y < edge; y++)
    for (let x = 0; x < edge; x++) {
      const rgb = sample([
          (2 * (x + 0.5)) / edge - 1,
          1 - (2 * (y + 0.5)) / edge,
          z,
        ]),
        index = (y * edge + x) * 4;
      if (mask) {
        const v = Math.round(
          255 * Math.min(1, Math.max(0, gradientMaskCoverage(rgb, mask))),
        );
        pixels[index] = pixels[index + 1] = pixels[index + 2] = v;
      } else
        for (let i = 0; i < 3; i++)
          pixels[index + i] = Math.round(
            255 * Math.min(1, Math.max(0, linearToSRGB(rgb[i]))),
          );
      pixels[index + 3] = 255;
    }
  return pixels;
}
