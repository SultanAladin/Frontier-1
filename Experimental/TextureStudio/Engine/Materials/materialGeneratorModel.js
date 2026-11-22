import { materials, normalizeMaterial } from "./materials.js";
import { normalizeTextureSource } from "./textureSource.js";
export const MATERIAL_GENERATOR_ID = "MaterialStudio";
export const MATERIAL_GENERATOR_OUTPUTS = [
  "base-color",
  "roughness",
  "metalness",
  "normal",
  "height",
  "emission",
  "scratch-mask",
];
const allowed = new Set(["id", "name", "category", "description"]);
for (let type = 0; type <= 38; type++)
  for (const key of Object.keys(normalizeMaterial({ type }))) allowed.add(key);
const blocked = new Set([
  "pattern",
  "patternBaseName",
  "bakeMode",
  "bakeHeightRange",
  "hideSVG",
  "shapeSource",
  "svgSource",
]);
const bound = (v, d, min, max) =>
  v != null && Number.isFinite(Number(v))
    ? Math.min(max, Math.max(min, Number(v)))
    : d;
export function materialGeneratorSnapshot(raw) {
  if (
    !raw ||
    typeof raw !== "object" ||
    Array.isArray(raw) ||
    JSON.stringify(raw).length > 64000
  )
    throw new Error(
      "Material generators require a bounded procedural Material-studio preset.",
    );
  const data = {};
  for (const key of allowed) {
    if (blocked.has(key) || !Object.hasOwn(raw, key)) continue;
    const v = raw[key];
    if ((typeof v === "number" && Number.isFinite(v)) || typeof v === "boolean")
      data[key] = v;
    else if (typeof v === "string")
      data[key] = v.replace(/[\u0000-\u001f\u007f]/g, "").slice(0, 200);
    else if (
      ["colors", "colorStops"].includes(key) &&
      Array.isArray(v) &&
      v.length <= 12
    )
      data[key] = v.filter(
        (a) =>
          (typeof a === "number" && Number.isFinite(a)) ||
          (typeof a === "string" && /^#[\da-f]{6}$/i.test(a)),
      );
    else if (
      key === "tuning" &&
      v &&
      typeof v === "object" &&
      !Array.isArray(v)
    )
      data[key] = Object.fromEntries(
        Object.entries(v)
          .filter(
            ([k, a]) =>
              /^[a-zA-Z0-9_]{1,60}$/.test(k) &&
              typeof a === "number" &&
              Number.isFinite(a),
          )
          .slice(0, 80),
      );
    else if (key === "sandBeds" && Array.isArray(v) && v.length <= 4)
      data[key] = v.map((b) =>
        Object.fromEntries(
          Object.entries(b || {}).filter(
            ([k, a]) =>
              /^[a-zA-Z0-9_]{1,60}$/.test(k) &&
              ((typeof a === "number" && Number.isFinite(a)) ||
                typeof a === "boolean" ||
                (typeof a === "string" && a.length < 100)),
          ),
        ),
      );
  }
  const result = normalizeMaterial(data);
  return Object.fromEntries(
    Object.entries(result).filter(([k]) => allowed.has(k) && !blocked.has(k)),
  );
}
export function generatorStamp(value) {
  const key = JSON.stringify([
    value.material,
    value.output,
    value.widthMM,
    value.low,
    value.high,
    value.gamma,
    value.inverted,
  ]);
  let h = 2166136261;
  for (let i = 0; i < key.length; i++) {
    h ^= key.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return `material-generator-v1-${(h >>> 0).toString(16)}-${key.length}`;
}
export function createMaterialGenerator(
  material = materials.find((m) => m.id === "scratches"),
) {
  return normalizeMaterialGenerator({
    id: MATERIAL_GENERATOR_ID,
    material,
    output: "scratch-mask",
  });
}
export function normalizeMaterialGenerator(raw) {
  if (!raw || raw.id !== MATERIAL_GENERATOR_ID) return null;
  const value = {
    id: MATERIAL_GENERATOR_ID,
    material: materialGeneratorSnapshot(
      raw.material || materials.find((m) => m.id === "scratches"),
    ),
    output: MATERIAL_GENERATOR_OUTPUTS.includes(raw.output)
      ? raw.output
      : "height",
    widthMM: bound(raw.widthMM, 100, 1, 1000),
    low: bound(raw.low, 0, 0, 1),
    high: bound(raw.high, 1, 0, 1),
    gamma: bound(raw.gamma, 1, 0.1, 8),
    inverted: raw.inverted === true,
  };
  value.stamp = generatorStamp(value);
  value.result =
    raw.stamp === value.stamp ? normalizeTextureSource(raw.result) : null;
  return value;
}
export function generatorChannelValue(byte, options) {
  let v =
    options.output === "scratch-mask"
      ? Math.max(0, (0.5 - byte / 255) * 10)
      : byte / 255;
  const span = options.high - options.low;
  v =
    span > 1e-6
      ? Math.min(1, Math.max(0, (v - options.low) / span))
      : v >= options.high
        ? 1
        : 0;
  v = Math.pow(v, 1 / options.gamma);
  return options.inverted ? 1 - v : v;
}
