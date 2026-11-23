import * as THREE from "three";
import { TeapotGeometry } from "three/addons/geometries/TeapotGeometry.js";
import { materials, normalizeMaterial } from "./materials.js";
import { validatePattern } from "./patternDocument.js";
import { normalizeSandLayers } from "./materialProfiles.js";

export const TEXTURE_OBJECT_ID = "teapot";
export const AUTHORING_UV_SET = "teapot-part-udim-v1";
export const TEXTURE_PARTS = [
  { id: "body", name: "Body", index: 1, tile: 1001, u: 0, color: "#9daf98" },
  { id: "lid", name: "Lid", index: 2, tile: 1002, u: 1, color: "#d5b78c" },
  {
    id: "handle",
    name: "Handle",
    index: 3,
    tile: 1003,
    u: 2,
    color: "#92aac5",
  },
  { id: "spout", name: "Spout", index: 4, tile: 1004, u: 3, color: "#b39dc5" },
];
export const texturePart = (id) => TEXTURE_PARTS.find((part) => part.id === id);
export function normalizeSurfaceTarget(raw) {
  if (raw == null) return null;
  if (!raw || raw.objectId !== TEXTURE_OBJECT_ID || !texturePart(raw.partId))
    throw new Error(
      "Choose an existing teapot part: Body, Lid, Handle or Spout.",
    );
  return { objectId: TEXTURE_OBJECT_ID, partId: raw.partId };
}
const defaults = normalizeMaterial({});
const stringKeys = new Set(
  materials.flatMap((m) =>
    Object.entries(m)
      .filter(([, v]) => typeof v === "string")
      .map(([k]) => k),
  ),
);
const knownKeys = new Set([
  ...Object.keys(defaults),
  ...materials.flatMap((material) => Object.keys(material)),
  "pattern",
]);
export function normalizeSurfaceMaterial(raw) {
  if (raw == null) return null;
  if (typeof raw !== "object" || Array.isArray(raw))
    throw new Error("A part material must be a material snapshot.");
  if (new TextEncoder().encode(JSON.stringify(raw)).length > 200000)
    throw new Error("Part material snapshots are limited to 200 KB.");
  const clean = {};
  for (const [key, value] of Object.entries(raw)) {
    if (!knownKeys.has(key)) continue;
    if (key === "pattern") {
      if (value) clean.pattern = validatePattern(value);
    } else if (key === "tuning") {
      clean.tuning = Object.fromEntries(
        Object.entries(value || {})
          .slice(0, 128)
          .filter(([k, v]) => /^[\w-]{1,60}$/.test(k) && Number.isFinite(v))
          .map(([k, v]) => [k, Math.max(0, Math.min(1, v))]),
      );
    } else if (["colors", "colorStops", "sandLayers"].includes(key)) {
      if (!Array.isArray(value))
        throw new Error("Material palettes and layers must be arrays.");
      if (key === "colors") {
        if (
          value.some(
            (v) =>
              typeof v !== "string" ||
              !/^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(v),
          )
        )
          throw new Error("Material colours must be hexadecimal colours.");
        clean.colors = value.slice(0, 12);
      } else if (key === "colorStops") {
        if (value.some((v) => !Number.isFinite(v)))
          throw new Error("Material palette positions must be finite.");
        clean.colorStops = value
          .slice(0, 12)
          .map((v) => Math.max(0, Math.min(1, v)));
      } else clean.sandLayers = normalizeSandLayers(value);
    } else if (typeof defaults[key] === "number" && typeof value !== "number") {
      throw new Error("Part material values must be finite numbers.");
    } else if (stringKeys.has(key) && typeof value !== "string") {
      throw new Error("Part material labels and colours must be text.");
    } else if (typeof value === "number") {
      if (!Number.isFinite(value))
        throw new Error("Part material values must be finite.");
      clean[key] = Math.max(-1000000, Math.min(1000000, value));
    } else if (typeof value === "boolean") clean[key] = value;
    else if (typeof value === "string") {
      if (
        key.toLowerCase().endsWith("color") &&
        !/^#(?:[0-9a-f]{3}|[0-9a-f]{6})$/i.test(value)
      )
        throw new Error("Material colours must be hexadecimal colours.");
      clean[key] = value.replace(/[\u0000-\u001f\u007f]/g, "").slice(0, 160);
    }
  }
  if (clean.type != null)
    clean.type = Math.max(0, Math.min(38, Math.round(clean.type)));
  const result = normalizeMaterial(clean);
  // Snapshots contain declarative, validated data only, never source code,
  // external texture URLs, functions or a second material runtime.
  if (result.pattern) result.pattern = validatePattern(result.pattern);
  return result;
}
export function teapotPatchPart(patch) {
  return patch >= 20 && patch < 28
    ? TEXTURE_PARTS[1]
    : patch >= 12 && patch < 16
      ? TEXTURE_PARTS[2]
      : patch >= 16 && patch < 20
        ? TEXTURE_PARTS[3]
        : TEXTURE_PARTS[0];
}
export function createTextureTeapotGeometry(segments = 20) {
  const geometry = new TeapotGeometry(1.05, segments),
    source = geometry.attributes.uv,
    count = (segments + 1) ** 2,
    uv = new Float32Array(source.count * 2),
    part = new Float32Array(source.count),
    chart = new Float32Array(source.count);
  for (let i = 0; i < source.count; i++) {
    const patch = Math.floor(i / count),
      target = teapotPatchPart(patch),
      local =
        target.id === "body"
          ? patch < 12
            ? patch
            : patch - 16
          : target.id === "lid"
            ? patch - 20
            : target.id === "handle"
              ? patch - 12
              : patch - 16,
      columns = target.id === "body" || target.id === "lid" ? 4 : 2,
      rows = target.id === "body" ? 4 : 2,
      gutter = 0.045;
    uv[i * 2] =
      target.u +
      ((local % columns) + gutter + source.getX(i) * (1 - gutter * 2)) /
        columns;
    uv[i * 2 + 1] =
      (Math.floor(local / columns) +
        gutter +
        source.getY(i) * (1 - gutter * 2)) /
      rows;
    part[i] = target.index;
    chart[i] = patch + 1;
  }
  // Keep UV0 unmodified for existing procedural materials. The authoring set
  // is real mesh data, shared by UV picking, decal geometry and mask shaders.
  geometry.setAttribute("uvAuthoring", new THREE.BufferAttribute(uv, 2));
  geometry.setAttribute("surfacePart", new THREE.BufferAttribute(part, 1));
  geometry.setAttribute("surfaceChart", new THREE.BufferAttribute(chart, 1));
  geometry.userData.authoringUVSet = AUTHORING_UV_SET;
  geometry.userData.surfaceParts = TEXTURE_PARTS;
  return geometry;
}
export function surfacePartAtTriangle(geometry, faceIndex) {
  if (!Number.isInteger(faceIndex) || faceIndex < 0) return null;
  const i = geometry.index ? geometry.index.getX(faceIndex * 3) : faceIndex * 3,
    value = geometry.attributes.surfacePart?.getX(i);
  return TEXTURE_PARTS.find((part) => part.index === value) || null;
}
export function partGeometry(geometry, partId) {
  const part = texturePart(partId);
  if (!part || !geometry.attributes.surfacePart) return null;
  const ids = [],
    count = geometry.index?.count || geometry.attributes.position.count;
  for (let i = 0; i < count; i += 3) {
    const a = geometry.index ? geometry.index.getX(i) : i;
    if (geometry.attributes.surfacePart.getX(a) !== part.index) continue;
    for (let j = 0; j < 3; j++)
      ids.push(geometry.index ? geometry.index.getX(i + j) : i + j);
  }
  const result = new THREE.BufferGeometry();
  for (const [name, attribute] of Object.entries(geometry.attributes)) {
    const data = new Float32Array(ids.length * attribute.itemSize);
    ids.forEach((id, i) => {
      for (let c = 0; c < attribute.itemSize; c++)
        data[i * attribute.itemSize + c] = attribute.getComponent(id, c);
    });
    result.setAttribute(
      name,
      new THREE.BufferAttribute(data, attribute.itemSize),
    );
  }
  result.computeBoundingSphere();
  return result;
}
export function ensureSurfaceCoordinates(geometry) {
  if (!geometry.attributes.uvAuthoring)
    geometry.setAttribute(
      "uvAuthoring",
      geometry.attributes.uv?.clone() ||
        new THREE.Float32BufferAttribute(
          new Float32Array(geometry.attributes.position.count * 2),
          2,
        ),
    );
  if (!geometry.attributes.surfacePart)
    geometry.setAttribute(
      "surfacePart",
      new THREE.Float32BufferAttribute(
        new Float32Array(geometry.attributes.position.count),
        1,
      ),
    );
  return geometry;
}
export function installSurfaceCoordinates(material) {
  if (material.userData.surfaceCoordinates) return;
  material.userData.surfaceCoordinates = true;
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    shader.vertexShader =
      "attribute vec2 uvAuthoring;attribute float surfacePart;varying vec2 vAuthoringUv;varying float vSurfacePart;\n" +
      shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <begin_vertex>",
      "#include <begin_vertex>\nvAuthoringUv=uvAuthoring;vSurfacePart=surfacePart;",
    );
    shader.fragmentShader =
      "varying vec2 vAuthoringUv;varying float vSurfacePart;\n" +
      shader.fragmentShader;
  };
  material.customProgramCacheKey = () => cache() + "|surface-coordinates-v1";
}
export function readAssetDrop(transfer) {
  try {
    const text = transfer?.getData("application/x-alloy-asset");
    if (!text || text.length > 210000) return null;
    const asset = JSON.parse(text);
    return asset &&
      ["material", "stamp"].includes(asset.type) &&
      /^[\w-]{1,120}$/.test(asset.id)
      ? asset
      : null;
  } catch {
    return null;
  }
}
