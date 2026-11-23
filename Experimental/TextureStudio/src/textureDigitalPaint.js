import {
  normalizeScreenProjection,
  normalizeScreenPoints,
} from "./screenPaintProjection.js";
import { normalizeDigitalBrush, DIGITAL_DAB_LIMIT } from "./digitalBrush.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import {
  normalizeMaskRegion,
  validateMaskPaintGeometry,
} from "./textureMaskPaint.js";
import { AUTHORING_UV_SET, texturePart } from "./textureSurfaceModel.js";
import { uvTopology, barycentricUV } from "./textureUVGeometry.js";
import { normalizeTextureSource } from "./textureSource.js";

export const DIGITAL_PAINT_SCHEMA = "alloy.digital-paint.v1";
export const PAINT_OPERATION_LIMIT = 128;
export const PAINT_POINT_LIMIT = 512;
export const PAINT_TOTAL_POINT_LIMIT = 8192;
export const PAINT_CHANNEL_SLICE_LIMIT = 64;
export const PAINT_DAB_WORK_LIMIT = 262144;
export const DIGITAL_PAINT_CHANNELS = PAINT_CHANNELS.filter(
  (c) => c.edit !== "derived",
);
const finite = (v) => typeof v === "number" && Number.isFinite(v);
const id = (v) => typeof v === "string" && /^[a-zA-Z0-9_-]{1,80}$/.test(v);
export function normalizeDigitalPoints(raw, region) {
  if (!Array.isArray(raw) || !raw.length || raw.length > PAINT_POINT_LIMIT)
    throw new Error(`Digital strokes need 1–${PAINT_POINT_LIMIT} samples.`);
  const part = texturePart(region.partId);
  return raw.map((p) => {
    if (
      !Array.isArray(p) ||
      p.length !== 3 ||
      !p.every(finite) ||
      p[0] < part.u ||
      p[0] > part.u + 1 ||
      p[1] < 0 ||
      p[1] > 1 ||
      p[2] < 0 ||
      p[2] > 1
    )
      throw new Error(
        "Digital stroke samples must be finite UV/pressure triples inside their hit component.",
      );
    return p.map((n) => Math.round(n * 1e5) / 1e5);
  });
}
export function normalizeDigitalPaint(raw) {
  if (!raw) return null;
  if (
    raw.schema !== DIGITAL_PAINT_SCHEMA ||
    raw.uvSet !== AUTHORING_UV_SET ||
    !Array.isArray(raw.operations)
  )
    throw new Error(
      "Digital paint requires the packed teapot authoring UV set.",
    );
  if (raw.operations.length > PAINT_OPERATION_LIMIT)
    throw new Error(
      `A layer supports ${PAINT_OPERATION_LIMIT} digital stroke segments or fills. Start another paint layer.`,
    );
  let total = 0,
    work = 0;
  const identities = new Set();
  const operations = raw.operations.map((op) => {
    if (
      !op ||
      !id(op.id) ||
      !["paint", "erase", "fill"].includes(op.kind) ||
      !Number.isInteger(op.seed) ||
      op.seed < 0 ||
      op.seed > 4294967295
    )
      throw new Error("Invalid digital paint operation.");
    if (identities.has(op.id))
      throw new Error(
        "Digital paint operation IDs must be unique within a layer.",
      );
    identities.add(op.id);
    if (
      !op.projection &&
      op.kind !== "fill" &&
      !["face", "island"].includes(op.region?.kind)
    )
      throw new Error(
        "Digital stroke segments must be clipped to one face or UV chart.",
      );
    if (op.projection && op.kind === "fill")
      throw new Error("Region Fill cannot contain a screen stroke projection.");
    const projection = op.projection
      ? normalizeScreenProjection(op.projection)
      : null;
    const region = normalizeMaskRegion(op.region, !!projection),
      brush = normalizeDigitalBrush(op.brush),
      channels = {};
    if (
      !op.channels ||
      typeof op.channels !== "object" ||
      Array.isArray(op.channels)
    )
      throw new Error("Digital paint needs explicit channel values.");
    for (const c of DIGITAL_PAINT_CHANNELS) {
      if (!Object.hasOwn(op.channels, c.id)) continue;
      const value = op.channels[c.id];
      if (c.edit === "color") {
        if (!/^#[\da-f]{6}$/i.test(value || ""))
          throw new Error("Invalid digital channel colour.");
        channels[c.id] = value.toLowerCase();
      } else {
        if (!finite(value) || value < c.min || value > c.max)
          throw new Error(
            "Digital channel values must stay inside their channel range.",
          );
        channels[c.id] = value;
      }
    }
    if (!Object.keys(channels).length)
      throw new Error("Choose at least one painting channel.");
    const points =
      op.kind === "fill"
        ? []
        : projection
          ? normalizeScreenPoints(op.points, projection)
          : normalizeDigitalPoints(op.points, region);
    total += points.length;
    let length = 0;
    for (let i = 1; i < points.length; i++)
      length += Math.hypot(
        points[i][0] - points[i - 1][0],
        points[i][1] - points[i - 1][1],
      );
    const dabs =
      brush.size > 0 && brush.opacity > 0 && brush.flow > 0
        ? Math.ceil(
            (length * 256) / Math.max(0.4, brush.size * brush.spacing),
          ) + 1
        : 0;
    if (dabs > DIGITAL_DAB_LIMIT)
      throw new Error(
        "This stroke exceeds the digital dab budget. Increase brush size or spacing.",
      );
    work += (op.kind === "fill" ? 1 : dabs) * Object.keys(channels).length;
    return {
      id: op.id,
      kind: op.kind,
      region,
      seed: op.seed,
      brush,
      channels,
      points,
      ...(projection ? { projection } : {}),
    };
  });
  if (total > PAINT_TOTAL_POINT_LIMIT)
    throw new Error(
      "The layer's digital sampling budget is full. Start another layer.",
    );
  if (work > PAINT_DAB_WORK_LIMIT)
    throw new Error(
      "The layer's digital raster-work budget is full. Increase brush spacing, clear its overlay or start another layer.",
    );
  return { schema: DIGITAL_PAINT_SCHEMA, uvSet: AUTHORING_UV_SET, operations };
}
export function validateDigitalPaintGeometry(paint, geometry) {
  if (!paint) return;
  validateMaskPaintGeometry(
    { uvSet: paint.uvSet, operations: paint.operations },
    geometry,
  );
  const topology = uvTopology(geometry);
  for (const op of paint.operations) {
    if (op.kind === "fill" || op.projection) continue;
    if (op.region.kind === "face") {
      const triangle = topology.triangles[op.region.id];
      if (op.points.some((p) => !barycentricUV(p, triangle, 0.002)))
        throw new Error("Digital samples must stay in their selected face.");
    } else {
      const chart = topology.charts.find((c) => c.id === op.region.id);
      if (
        !chart ||
        op.points.some(
          (p) =>
            p[0] < chart.min[0] - 1e-5 ||
            p[0] > chart.max[0] + 1e-5 ||
            p[1] < chart.min[1] - 1e-5 ||
            p[1] > chart.max[1] + 1e-5,
        )
      )
        throw new Error(
          "Digital samples must stay in their selected UV chart.",
        );
    }
  }
}
export function paintedChannelIds(layer) {
  const ids = new Set(
    (layer?.paint?.operations || []).flatMap((op) => Object.keys(op.channels)),
  );
  return DIGITAL_PAINT_CHANNELS.filter(
    (c) => ids.has(c.id) && layer.channels.includes(c.id),
  ).map((c) => c.id);
}
export function hasDigitalPaint(layer) {
  return (
    paintedChannelIds(layer).length > 0 &&
    layer.paint.operations.some(
      (op) =>
        op.kind !== "erase" &&
        op.brush.opacity > 0 &&
        (op.kind === "fill" || (op.brush.size > 0 && op.brush.flow > 0)),
    )
  );
}
export function digitalChannelValues(layer, brush) {
  return Object.fromEntries(
    DIGITAL_PAINT_CHANNELS.filter((c) => layer.channels.includes(c.id)).map(
      (c) => [
        c.id,
        c.id === "baseColor" ? brush.color : layer.channelSettings[c.id].value,
      ],
    ),
  );
}
export function normalizeLastDigitalStroke(raw) {
  if (!raw) return null;
  if (
    !id(raw.id) ||
    !id(raw.layerId) ||
    !["paint", "erase", "fill"].includes(raw.operation) ||
    !Array.isArray(raw.channels)
  )
    throw new Error("Invalid last digital stroke record.");
  const image = normalizeTextureSource(raw.image);
  if (
    !image ||
    image.width > 512 ||
    image.height > 256 ||
    image.dataUrl.length > 48000
  )
    throw new Error("The digital stroke preview exceeds its portable budget.");
  return {
    id: raw.id,
    layerId: raw.layerId,
    layerName: String(raw.layerName || "Layer")
      .replace(/[\u0000-\u001f\u007f]/g, "")
      .slice(0, 80),
    mask: raw.mask === true,
    operation: raw.operation,
    channels: raw.channels
      .filter((v) => PAINT_CHANNELS.some((c) => c.id === v))
      .slice(0, 14),
    brush: normalizeDigitalBrush(raw.brush),
    image,
  };
}
