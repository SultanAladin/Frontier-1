// The footprint is the same procedural dab as deposition. Screen mode never
// deforms or clips its outline to the mesh/UV charts.
import { digitalBrushLastDab, drawDigitalDabs } from "./digitalBrush.js";
import { maskGeometryCache } from "./maskPaintRaster.js";
import { maskRegionContains } from "./textureMaskPaint.js";
import { texturePart } from "./textureSurfaceModel.js";
import { uvTopology } from "./textureUVGeometry.js";

export function digitalPointerPressure(event, hover = false) {
  if (!event?.pointerType || event.pointerType === "mouse") return 1;
  const p = Number(event.pressure);
  if (!Number.isFinite(p) || (hover && !event.buttons && p === 0)) return 1;
  return Math.max(0.02, Math.min(1, p));
}

export function digitalBrushCursor({
  hit,
  geometry,
  target,
  brush,
  channels,
  seed,
  points,
  mask = false,
  ink = 1,
  pressure = 1,
  projection,
  screenPosition,
}) {
  const triangle = hit && uvTopology(geometry).triangles[hit.faceIndex];
  if (
    ((!projection || brush.tool === "fill") &&
      (!hit?.uvPosition || !maskRegionContains(target, triangle))) ||
    brush.opacity <= 0 ||
    (brush.tool !== "fill" && (brush.size <= 0 || brush.flow <= 0))
  )
    return null;
  const region =
    projection || brush.tool === "fill" || target.kind === "face"
      ? target
      : { kind: "island", partId: target.partId, id: hit.chartId };
  const color =
    brush.tool === "erase"
      ? "#d5dce2"
      : mask
        ? ink
          ? "#ffffff"
          : "#000000"
        : channels.baseColor || channels.emission || "#aebdc9";
  let dab = null;
  if (brush.tool !== "fill") {
    try {
      dab = digitalBrushLastDab(
        points?.length
          ? points
          : [
              [
                ...(projection
                  ? screenPosition.map((n) => n / 256)
                  : hit.uvPosition),
                pressure,
              ],
            ],
        brush,
        seed,
        256,
        color,
        !mask && brush.tool !== "erase" && !!channels.baseColor,
      );
    } catch (error) {
      // Draft validation owns the atomic budget error. Hover must not throw
      // from the render loop while that validation is queued for this frame.
      if (error.message.includes("digital dab budget")) return null;
      throw error;
    }
  }
  if (brush.tool !== "fill" && !dab) return null;
  return {
    kind: brush.tool,
    brush,
    region,
    dab,
    uvPosition: hit?.uvPosition,
    ...(projection && brush.tool !== "fill"
      ? { space: "screen", screenPosition }
      : {}),
    mask,
    color,
    channels: Object.keys(channels),
  };
}

export function digitalCursorLabel(cursor) {
  if (!cursor) return "";
  const tool =
    cursor.kind === "erase"
      ? "Erase"
      : cursor.kind === "fill"
        ? "Fill"
        : "Paint";
  if (!cursor.dab)
    return `${tool} · ${texturePart(cursor.region.partId)?.name || "Surface"} ${cursor.region.kind}`;
  const width = cursor.dab.radius * 2,
    height = width * cursor.brush.roundness;
  const dimensions =
    Math.abs(width - height) < 0.01
      ? width.toFixed(1)
      : `${width.toFixed(1)} × ${height.toFixed(1)}`;
  return `${tool}${cursor.mask ? " mask" : ""} · ${dimensions} ${cursor.space === "screen" ? "px" : "UV px"}${cursor.brush.grain !== "none" && cursor.brush.grainAmount ? ` · ${cursor.brush.grain} grain` : ""}`;
}

// Canvas drawing uses logical pixels; a DOM rect can be fractional, CSS-scaled,
// or displayed on a different DPR. Never mix those coordinate systems.
export function uvCanvasPoint(clientX, clientY, rect, size) {
  return [
    ((clientX - rect.left) * size.width) / rect.width,
    ((clientY - rect.top) * size.height) / rect.height,
  ];
}
export function uvAtCanvasPoint(point, camera) {
  return [
    (point[0] - camera.x) / camera.scale,
    (camera.y - point[1]) / camera.scale,
  ];
}
export function digitalCursorOutline(cursor) {
  const p = new Path2D(),
    d = cursor.dab;
  if (!d) return p;
  const radius = (d.radius * 31) / (32 * 256),
    tip = new Path2D();
  if (cursor.brush.shape === "square")
    tip.rect(-radius, -radius, radius * 2, radius * 2);
  else tip.arc(0, 0, radius, 0, Math.PI * 2);
  p.addPath(
    tip,
    new DOMMatrix()
      .translate(d.x / 256, d.y / 256)
      .rotate((d.angle * 180) / Math.PI)
      .scale(1, cursor.brush.roundness),
  );
  return p;
}
export function drawUVBrushCursor(canvas, size, camera, cursor, geometry) {
  if (cursor?.space === "screen")
    return drawScreenBrushCursor(
      canvas,
      {
        width: size.screenWidth || size.width,
        height: size.screenHeight || size.height,
      },
      cursor,
    );
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.round(size.width * dpr),
    height = Math.round(size.height * dpr);
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, width, height);
  canvas.dataset.brushCursor = cursor ? JSON.stringify(cursor) : "";
  canvas.dataset.brushCursorLabel = digitalCursorLabel(cursor);
  if (!cursor) return;
  const { scale, x, y } = camera;
  ctx.setTransform(dpr * scale, 0, 0, -dpr * scale, dpr * x, dpr * y);
  ctx.save();
  const path = maskGeometryCache(geometry).path(cursor.region);
  ctx.clip(path);
  if (!cursor.dab) {
    ctx.globalAlpha = 0.28 * cursor.brush.opacity;
    ctx.fillStyle = cursor.color;
    ctx.fill(path);
    ctx.globalAlpha = 1;
  } else {
    ctx.save();
    ctx.scale(1 / 256, 1 / 256);
    drawDigitalDabs(
      ctx,
      [
        {
          ...cursor.dab,
          opacity: cursor.dab.opacity * cursor.brush.opacity * 0.42,
        },
      ],
      cursor.brush,
    );
    ctx.restore();
  }
  const outline = cursor.dab ? digitalCursorOutline(cursor) : path;
  ctx.strokeStyle = "#111820";
  ctx.lineWidth = 3 / scale;
  ctx.stroke(outline);
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1.25 / scale;
  ctx.stroke(outline);
  ctx.restore();
  // A small, constant-screen-size cross marks the actual ray/input anchor;
  // intentional scatter may move the textured dab away from this anchor.
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const cx = x + cursor.uvPosition[0] * scale,
    cy = y - cursor.uvPosition[1] * scale;
  ctx.beginPath();
  ctx.moveTo(cx - 3, cy);
  ctx.lineTo(cx + 3, cy);
  ctx.moveTo(cx, cy - 3);
  ctx.lineTo(cx, cy + 3);
  ctx.strokeStyle = "#111820";
  ctx.lineWidth = 3;
  ctx.stroke();
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1;
  ctx.stroke();
}

// Transparent CSS-pixel overlay, independent of mesh UVs, camera foreshortening,
// chart gaps and DPR. Only the deposited atlas is clipped/projected to geometry.
export function drawScreenBrushCursor(canvas, size, cursor) {
  const dpr = Math.min(
      window.devicePixelRatio || 1,
      2,
      Math.sqrt(4194304 / (size.width * size.height)),
    ),
    w = Math.max(1, Math.floor(size.width * dpr)),
    h = Math.max(1, Math.floor(size.height * dpr));
  if (canvas.width !== w) canvas.width = w;
  if (canvas.height !== h) canvas.height = h;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.clearRect(0, 0, w, h);
  canvas.dataset.brushCursor = cursor ? JSON.stringify(cursor) : "";
  canvas.dataset.brushCursorLabel = digitalCursorLabel(cursor);
  if (!cursor?.dab) return;
  ctx.setTransform(w / size.width, 0, 0, h / size.height, 0, 0);
  const d = cursor.dab;
  drawDigitalDabs(
    ctx,
    [{ ...d, opacity: d.opacity * cursor.brush.opacity * 0.42 }],
    cursor.brush,
  );
  ctx.save();
  ctx.translate(d.x, d.y);
  ctx.rotate(d.angle);
  ctx.scale(1, cursor.brush.roundness);
  ctx.beginPath();
  const radius = (d.radius * 31) / 32;
  if (cursor.brush.shape === "square")
    ctx.rect(-radius, -radius, radius * 2, radius * 2);
  else ctx.arc(0, 0, radius, 0, Math.PI * 2);
  ctx.strokeStyle = "#111820";
  ctx.lineWidth = 3;
  ctx.stroke();
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1.25;
  ctx.stroke();
  ctx.restore();
  const [x, y] = cursor.screenPosition;
  ctx.beginPath();
  ctx.moveTo(x - 3, y);
  ctx.lineTo(x + 3, y);
  ctx.moveTo(x, y - 3);
  ctx.lineTo(x, y + 3);
  ctx.strokeStyle = "#111820";
  ctx.lineWidth = 3;
  ctx.stroke();
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 1;
  ctx.stroke();
}
