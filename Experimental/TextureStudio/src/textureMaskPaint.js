import {
  normalizeScreenProjection,
  normalizeScreenPoints,
} from "./screenPaintProjection.js";
import { normalizeDigitalBrush } from "./digitalBrush.js";
import {
  AUTHORING_UV_SET,
  texturePart,
  teapotPatchPart,
} from "./textureSurfaceModel.js";
import { uvTopology } from "./textureUVGeometry.js";

export const MASK_OPERATION_LIMIT = 128;
export const MASK_POINT_LIMIT = 512;
export const MASK_TOTAL_POINT_LIMIT = 8192;
export const MASK_PATH_LENGTH_LIMIT = 32;
export const MASK_SELECTION_MODES = ["face", "tile", "island", "object"];
const finite = (v) => typeof v === "number" && Number.isFinite(v);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export function normalizeMaskRegion(raw, allowSurface = false) {
  if (allowSurface && raw?.kind === "surface") return { kind: "surface" };
  if (
    !raw ||
    !MASK_SELECTION_MODES.includes(raw.kind) ||
    !texturePart(raw.partId)
  )
    throw new Error("Mask regions must target one native teapot component.");
  const part = texturePart(raw.partId);
  if (raw.kind === "object") return { kind: "object", partId: part.id };
  if (!Number.isInteger(raw.id))
    throw new Error("Invalid mask region identity.");
  if (raw.kind === "tile" && raw.id !== part.tile)
    throw new Error("That UV tile does not belong to the selected component.");
  if (
    raw.kind === "island" &&
    (raw.id < 1 || raw.id > 32 || teapotPatchPart(raw.id - 1).id !== part.id)
  )
    throw new Error(
      "That UV island does not belong to the selected component.",
    );
  if (raw.kind === "face" && (raw.id < 0 || raw.id >= 25440))
    throw new Error("That teapot triangle is outside the authoring surface.");
  return { kind: raw.kind, partId: part.id, id: raw.id };
}
export function normalizeMaskPaint(raw) {
  if (!raw) return null;
  if (raw.uvSet !== AUTHORING_UV_SET || !Array.isArray(raw.operations))
    throw new Error(
      "Painted masks require the packed teapot authoring UV set.",
    );
  if (raw.operations.length > MASK_OPERATION_LIMIT)
    throw new Error(
      `A mask supports up to ${MASK_OPERATION_LIMIT} brush strokes or region fills.`,
    );
  let totalPoints = 0,
    totalLength = 0;
  return {
    uvSet: AUTHORING_UV_SET,
    operations: raw.operations.map((op) => {
      if (
        !op ||
        !["brush", "region"].includes(op.kind) ||
        !finite(op.value) ||
        !finite(op.opacity)
      )
        throw new Error("Invalid mask paint operation.");
      const projection = op.digital?.projection
        ? normalizeScreenProjection(op.digital.projection)
        : null;
      const region = normalizeMaskRegion(op.region, !!projection),
        value = clamp(op.value, 0, 1),
        opacity = clamp(op.opacity, 0, 1);
      if (op.kind === "region")
        return { kind: "region", region, value, opacity };
      if (
        !finite(op.radius) ||
        !finite(op.hardness) ||
        !Array.isArray(op.points) ||
        !op.points.length ||
        op.points.length > MASK_POINT_LIMIT
      )
        throw new Error(
          `Mask strokes need 1–${MASK_POINT_LIMIT} finite UV samples.`,
        );
      const points = projection
        ? normalizeScreenPoints(
            op.points.map((p, i) => [...p, op.digital.pressure?.[i]]),
            projection,
          ).map((p) => p.slice(0, 2))
        : op.points.map((p) => {
            if (!Array.isArray(p) || p.length !== 2 || !p.every(finite))
              throw new Error(
                "Mask stroke coordinates must be finite UV pairs.",
              );
            const part = texturePart(region.partId);
            if (p[0] < part.u || p[0] > part.u + 1 || p[1] < 0 || p[1] > 1)
              throw new Error(
                "Mask samples must stay inside their component's UV tile.",
              );
            return p.map((v) => Math.round(v * 1e6) / 1e6);
          });
      totalPoints += points.length;
      for (let i = 1; i < points.length; i++)
        totalLength += Math.hypot(
          points[i][0] - points[i - 1][0],
          points[i][1] - points[i - 1][1],
        );
      if (
        totalPoints > MASK_TOTAL_POINT_LIMIT ||
        totalLength > MASK_PATH_LENGTH_LIMIT
      )
        throw new Error(
          "The mask paint sampling budget is full. Clear the painted overlay or use another mask.",
        );
      let digital = null;
      if (op.digital) {
        const d = op.digital;
        if (
          !Number.isInteger(d.seed) ||
          d.seed < 0 ||
          d.seed > 4294967295 ||
          !["paint", "erase"].includes(d.operation) ||
          !Array.isArray(d.pressure) ||
          d.pressure.length !== points.length ||
          d.pressure.some((p) => !finite(p) || p < 0 || p > 1)
        )
          throw new Error("Invalid digital mask brush/pressure data.");
        digital = {
          brush: normalizeDigitalBrush(d.brush),
          seed: d.seed,
          operation: d.operation,
          pressure: d.pressure.map((p) => Math.round(p * 1e5) / 1e5),
          ...(projection ? { projection } : {}),
        };
      }
      return {
        kind: "brush",
        region,
        value,
        opacity,
        radius: clamp(op.radius, 0.002, 0.25),
        hardness: clamp(op.hardness, 0, 1),
        points,
        ...(digital ? { digital } : {}),
      };
    }),
  };
}
export function maskRegionFromHit(hit, mode) {
  if (
    hit?.target?.objectId !== "teapot" ||
    !hit.uvPosition ||
    !MASK_SELECTION_MODES.includes(mode)
  )
    return null;
  const partId = hit.target.partId;
  return normalizeMaskRegion({
    kind: mode,
    partId,
    id:
      mode === "face"
        ? hit.faceIndex
        : mode === "island"
          ? hit.chartId
          : texturePart(partId)?.tile,
  });
}
export function maskRegionContains(region, triangle) {
  if (!region || !triangle) return false;
  return region.kind === "surface"
    ? !!triangle.part
    : triangle.part?.id === region?.partId &&
        (region.kind === "object" ||
          (region.kind === "tile" &&
            texturePart(region.partId)?.tile === region.id) ||
          (region.kind === "island" && triangle.chartId === region.id) ||
          (region.kind === "face" && triangle.faceIndex === region.id));
}
export function validateMaskPaintGeometry(paint, geometry) {
  if (!paint) return;
  const topology = uvTopology(geometry);
  for (const op of paint.operations) {
    if (
      op.region.kind === "face" &&
      !maskRegionContains(op.region, topology.triangles[op.region.id])
    )
      throw new Error("The selected face belongs to another component.");
  }
}
export function appendMaskOperation(mask, operation) {
  const paint = normalizeMaskPaint({
    uvSet: AUTHORING_UV_SET,
    operations: [...(mask.paint?.operations || []), operation],
  });
  return { ...mask, paint };
}
export const maskHasPaint = (mask) =>
  mask?.kind === "paint" || !!mask?.paint?.operations.length;
export function maskRegionLabel(region) {
  if (!region) return "No surface selection";
  if (region.kind === "surface") return "All visible surface components";
  const name = texturePart(region.partId)?.name;
  return `${name} · ${region.kind === "object" ? "Object" : region.kind === "tile" ? `UV Tile ${region.id}` : region.kind === "island" ? `UV Island ${region.id}` : `Face ${region.id + 1}`}`;
}
