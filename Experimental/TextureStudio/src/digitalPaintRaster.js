import { projectScreenOperation } from "./screenPaintProjection.js";
import * as THREE from "three";
import {
  digitalBrushDabCount,
  digitalBrushDabs,
  digitalBrushDabsFrom,
  drawDigitalDabs,
} from "./digitalBrush.js";
import {
  DIGITAL_PAINT_CHANNELS,
  paintedChannelIds,
} from "./textureDigitalPaint.js";
import {
  maskGeometryCache,
  MASK_ATLAS_WIDTH,
  MASK_TILE_EDGE,
} from "./maskPaintRaster.js";

export function digitalPaintCanvas() {
  const c = document.createElement("canvas");
  c.width = MASK_ATLAS_WIDTH;
  c.height = MASK_TILE_EDGE;
  // These canvases are read back with getImageData on every pack; declaring
  // the readback intent once (first getContext wins) keeps Chrome from
  // warning — and from paying a GPU->CPU download per read.
  c.getContext("2d", { willReadFrequently: true });
  return c;
}
const scalarColour = (value, c) => {
  const v = Math.round(((value - c.min) / (c.max - c.min)) * 255)
    .toString(16)
    .padStart(2, "0");
  return `#${v}${v}${v}`;
};
// One scratch image for an operation. Stroke opacity is applied ONCE when
// compositing it; flow and pressure are per dab and cannot accidentally turn a
// 50% opacity stroke into an opaque mark when the pointer emits more events.
export function rasterDigitalOperation(
  op,
  geometry,
  color = op.brush.color,
  dynamics = true,
) {
  if (op.projection)
    return projectScreenOperation(op, geometry, color, dynamics);
  const canvas = digitalPaintCanvas(),
    ctx = canvas.getContext("2d");
  const cache = maskGeometryCache(geometry);
  ctx.save();
  ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
  ctx.clip(cache.path(op.region));
  if (op.kind === "fill") {
    ctx.fillStyle = color;
    ctx.fill(cache.path(op.region));
  } else {
    ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
    drawDigitalDabs(
      ctx,
      digitalBrushDabs(
        op.points,
        op.brush,
        op.seed,
        MASK_TILE_EDGE,
        color,
        dynamics,
      ),
      op.brush,
    );
  }
  ctx.restore();
  return canvas;
}
export function rasterizeDigitalChannels(layer, geometry) {
  const ids = paintedChannelIds(layer),
    canvases = new Map(ids.map((id) => [id, digitalPaintCanvas()]));
  for (const op of layer.paint?.operations || []) {
    for (const id of ids) {
      if (!Object.hasOwn(op.channels, id)) continue;
      const c = DIGITAL_PAINT_CHANNELS.find((c) => c.id === id),
        target = canvases.get(id).getContext("2d");
      const color =
        c.edit === "color" ? op.channels[id] : scalarColour(op.channels[id], c);
      const stroke = rasterDigitalOperation(
        op,
        geometry,
        op.kind === "erase" ? "#ffffff" : color,
        id === "baseColor" && op.kind !== "erase",
      );
      target.globalCompositeOperation =
        op.kind === "erase" ? "destination-out" : "source-over";
      target.globalAlpha = op.brush.opacity;
      target.drawImage(stroke, 0, 0);
      target.globalAlpha = 1;
      target.globalCompositeOperation = "source-over";
    }
  }
  return canvases;
}
const SLICE_BYTES = MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 4;
const FULL_RECT = Object.freeze({
  x: 0,
  y: 0,
  width: MASK_ATLAS_WIDTH,
  height: MASK_TILE_EDGE,
});
const channelSpec = (id) => DIGITAL_PAINT_CHANNELS.find((c) => c.id === id);
// Convert one canvas rectangle into a premultiplied atlas slice. Colour
// slices are linearized exactly as the one-shot builder always has; scalar
// slices stay linear. Canvas row y lands in byte row (edge - 1 - y).
function packAtlasRect(bytes, slice, canvas, rect, isColour, alphas) {
  const pixels = canvas
    .getContext("2d")
    .getImageData(rect.x, rect.y, rect.width, rect.height).data;
  for (let row = 0; row < rect.height; row++) {
    const canvasY = rect.y + row,
      destRow =
        slice * SLICE_BYTES + (MASK_TILE_EDGE - canvasY - 1) * MASK_ATLAS_WIDTH * 4;
    for (let col = 0; col < rect.width; col++) {
      const from = (row * rect.width + col) * 4,
        to = destRow + (rect.x + col) * 4,
        alpha = pixels[from + 3] / 255;
      for (let c = 0; c < 3; c++) {
        let value = pixels[from + c] / 255;
        if (isColour)
          value =
            value <= 0.04045
              ? value / 12.92
              : Math.pow((value + 0.055) / 1.055, 2.4);
        bytes[to + c] = Math.round(value * alpha * 255);
      }
      bytes[to + 3] = pixels[from + 3];
      if (alphas) {
        const k = row * rect.width + col;
        if (pixels[from + 3] > alphas[k]) alphas[k] = pixels[from + 3];
      }
    }
  }
}
function packUnionRect(bytes, unionSlice, rect, alphas) {
  const offset = unionSlice * SLICE_BYTES;
  for (let row = 0; row < rect.height; row++) {
    const canvasY = rect.y + row,
      destRow =
        offset + (MASK_TILE_EDGE - canvasY - 1) * MASK_ATLAS_WIDTH * 4;
    for (let col = 0; col < rect.width; col++) {
      const to = destRow + (rect.x + col) * 4,
        a = alphas[row * rect.width + col];
      bytes[to] = bytes[to + 1] = bytes[to + 2] = bytes[to + 3] = a;
    }
  }
}
function makeAtlasTexture(bytes, depth) {
  const texture = new THREE.DataArrayTexture(
    bytes,
    MASK_ATLAS_WIDTH,
    MASK_TILE_EDGE,
    depth,
  );
  texture.colorSpace = THREE.NoColorSpace;
  texture.minFilter = texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  return texture;
}
export function createDigitalPaintAtlas(layer, geometry) {
  const canvases = rasterizeDigitalChannels(layer, geometry),
    ids = [...canvases.keys()];
  const bytes = new Uint8Array(SLICE_BYTES * (ids.length + 1)),
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1),
    alphas = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE);
  ids.forEach((id, slice) => {
    slots[DIGITAL_PAINT_CHANNELS.findIndex((c) => c.id === id)] = slice;
    packAtlasRect(
      bytes,
      slice,
      canvases.get(id),
      FULL_RECT,
      channelSpec(id).edit === "color",
      alphas,
    );
  });
  packUnionRect(bytes, ids.length, FULL_RECT, alphas);
  const texture = makeAtlasTexture(bytes, ids.length + 1);
  return { texture, ids, slots, unionSlot: ids.length, canvases };
}
export const digitalPaintOpKey = (op) =>
  JSON.stringify([
    op.id,
    op.kind,
    op.seed,
    op.region,
    op.projection ?? null,
    op.brush,
    op.channels,
  ]);
const depositKey = digitalPaintOpKey;
const dabExtent = (dab, brush) =>
  dab.radius * Math.sqrt(1 + brush.roundness * brush.roundness) + 2;
function dabRect(dabs, brush, previous) {
  let x0 = Infinity,
    y0 = Infinity,
    x1 = -Infinity,
    y1 = -Infinity;
  const visit = (d) => {
    const e = dabExtent(d, brush);
    x0 = Math.min(x0, d.x - e);
    x1 = Math.max(x1, d.x + e);
    // 📝 Dabs are deposited under a Y-flipped transform, so canvas rows run
    //    opposite to dab pixel space.
    y0 = Math.min(y0, MASK_TILE_EDGE - (d.y + e));
    y1 = Math.max(y1, MASK_TILE_EDGE - (d.y - e));
  };
  dabs.forEach(visit);
  previous?.forEach(visit);
  if (!Number.isFinite(x0)) return null;
  const x = Math.max(0, Math.floor(x0)),
    y = Math.max(0, Math.floor(y0));
  return {
    x,
    y,
    width: Math.min(MASK_ATLAS_WIDTH, Math.ceil(x1)) - x,
    height: Math.min(MASK_TILE_EDGE, Math.ceil(y1)) - y,
  };
}
const sameDab = (a, b) =>
  a === b ||
  (a &&
    b &&
    a.x === b.x &&
    a.y === b.y &&
    a.radius === b.radius &&
    a.opacity === b.opacity &&
    a.angle === b.angle &&
    a.color === b.color);
export { dabRect as strokeDabRect, sameDab as sameStrokeDab };
// 💡 A live stroke is append-only at the document level: finished operations
//    never change, and the final operation only gains samples (interior
//    samples can still be smoothed, and tapers rescale every dab). The
//    session therefore folds finished operations into persistent base
//    canvases once, keeps the growing operation on its own scratch canvas
//    (stroke opacity still applied exactly once, at composite time), and
//    repairs only the rectangle whose dabs actually changed. The atlas
//    texture stays allocated on the GPU; per frame only dirty channel
//    slices are re-uploaded via addLayerUpdate. Any history edit that
//    breaks the append-only prefix (undo, clear, channel change) falls
//    back to the one-shot builder above, so bytes stay identical.
export function createDigitalPaintAtlasSession(geometry) {
  let ids = [],
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1),
    bytes = null,
    texture = null,
    base = new Map(),
    presented = new Map(),
    baseKeys = [],
    live = null,
    disposed = false;
  const result = () => ({
    texture,
    ids,
    slots,
    unionSlot: ids.length,
    canvases: presented,
  });
  function compositeInto(target, source, op) {
    const ctx = target.getContext("2d");
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.globalCompositeOperation =
      op.kind === "erase" ? "destination-out" : "source-over";
    ctx.globalAlpha = op.brush.opacity;
    ctx.drawImage(source, 0, 0);
    ctx.restore();
  }
  // Deposit an operation (or its not-yet-deposited dab tail) into a scratch
  // canvas. Identical transforms, clip and dab stream to
  // rasterDigitalOperation; stroke opacity is NOT applied here.
  function depositScratch(scratches, op, channelId, fromDab) {
    const spec = channelSpec(channelId),
      color =
        op.kind === "erase"
          ? "#ffffff"
          : spec.edit === "color"
            ? op.channels[channelId]
            : scalarColour(op.channels[channelId], spec),
      dynamics = channelId === "baseColor" && op.kind !== "erase";
    if (op.projection) {
      scratches.set(
        channelId,
        projectScreenOperation(op, geometry, color, dynamics),
      );
      return;
    }
    let scratch = scratches.get(channelId);
    if (!scratch) {
      scratch = digitalPaintCanvas();
      scratches.set(channelId, scratch);
    }
    const ctx = scratch.getContext("2d"),
      cache = maskGeometryCache(geometry);
    ctx.save();
    ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
    ctx.clip(cache.path(op.region));
    if (op.kind === "fill") {
      if (!fromDab) {
        ctx.fillStyle = color;
        ctx.fill(cache.path(op.region));
      }
    } else {
      ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      drawDigitalDabs(
        ctx,
        digitalBrushDabsFrom(
          op.points,
          op.brush,
          op.seed,
          MASK_TILE_EDGE,
          color,
          dynamics,
          fromDab,
        ),
        op.brush,
      );
    }
    ctx.restore();
  }
  function channelDabs(op, channelId) {
    const spec = channelSpec(channelId),
      color =
        op.kind === "erase"
          ? "#ffffff"
          : spec.edit === "color"
            ? op.channels[channelId]
            : scalarColour(op.channels[channelId], spec);
    return digitalBrushDabs(
      op.points,
      op.brush,
      op.seed,
      MASK_TILE_EDGE,
      color,
      channelId === "baseColor" && op.kind !== "erase",
    );
  }
  function presentRect(target, baseCanvas, scratch, op, rect) {
    const ctx = target.getContext("2d");
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.beginPath();
    ctx.rect(rect.x, rect.y, rect.width, rect.height);
    ctx.clip();
    ctx.globalCompositeOperation = "copy";
    ctx.drawImage(baseCanvas, 0, 0);
    ctx.restore();
    ctx.save();
    ctx.beginPath();
    ctx.rect(rect.x, rect.y, rect.width, rect.height);
    ctx.clip();
    ctx.globalCompositeOperation =
      op.kind === "erase" ? "destination-out" : "source-over";
    ctx.globalAlpha = op.brush.opacity;
    ctx.drawImage(scratch, 0, 0);
    ctx.restore();
  }
  function packDirty(rectsByChannel) {
    if (!rectsByChannel.size) return;
    const slices = new Set();
    for (const [id, rect] of rectsByChannel) {
      packAtlasRect(
        bytes,
        ids.indexOf(id),
        presented.get(id),
        rect,
        channelSpec(id).edit === "color",
        null,
      );
      slices.add(ids.indexOf(id));
    }
    // Union coverage is the max alpha over ALL channels in the rectangle.
    const unionRects = new Map();
    for (const rect of rectsByChannel.values()) {
      const key = `${rect.x},${rect.y},${rect.width},${rect.height}`;
      unionRects.set(key, rect);
    }
    for (const rect of unionRects.values()) {
      const alphas = new Uint8Array(rect.width * rect.height);
      for (const id of ids) {
        const pixels = presented
          .get(id)
          .getContext("2d")
          .getImageData(rect.x, rect.y, rect.width, rect.height).data;
        for (let k = 0; k < alphas.length; k++)
          if (pixels[k * 4 + 3] > alphas[k]) alphas[k] = pixels[k * 4 + 3];
      }
      packUnionRect(bytes, ids.length, rect, alphas);
    }
    for (const slice of slices) texture.addLayerUpdate(slice);
    texture.addLayerUpdate(ids.length);
    texture.needsUpdate = true;
  }
  function rebuild(layer) {
    const ops = layer.paint?.operations || [];
    ids = paintedChannelIds(layer);
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1);
    ids.forEach((id, slice) => {
      slots[DIGITAL_PAINT_CHANNELS.findIndex((c) => c.id === id)] = slice;
    });
    bytes = new Uint8Array(SLICE_BYTES * (ids.length + 1));
    texture?.dispose();
    texture = makeAtlasTexture(bytes, ids.length + 1);
    base = new Map(ids.map((id) => [id, digitalPaintCanvas()]));
    presented = new Map(ids.map((id) => [id, digitalPaintCanvas()]));
    baseKeys = [];
    live = null;
    // Base holds every operation except the last; the last stays live so a
    // following stroke frame can grow it incrementally.
    const prefix = ops.slice(0, -1);
    for (const op of prefix) {
      const scratches = new Map();
      for (const id of ids) {
        if (!Object.hasOwn(op.channels, id)) continue;
        depositScratch(scratches, op, id, 0);
        compositeInto(base.get(id), scratches.get(id), op);
      }
      baseKeys.push(depositKey(op));
    }
    for (const id of ids) {
      const ctx = presented.get(id).getContext("2d");
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalCompositeOperation = "copy";
      ctx.drawImage(base.get(id), 0, 0);
      ctx.restore();
    }
    const rectsByChannel = new Map();
    const op = ops.at(-1);
    if (op) {
      live = {
        index: ops.length - 1,
        key: depositKey(op),
        scratches: new Map(),
        dabs: new Map(),
        points: null,
      };
      for (const id of ids) {
        if (!Object.hasOwn(op.channels, id)) continue;
        depositScratch(live.scratches, op, id, 0);
        presentRect(
          presented.get(id),
          base.get(id),
          live.scratches.get(id),
          op,
          FULL_RECT,
        );
        if (!op.projection && op.kind !== "fill")
          live.dabs.set(id, channelDabs(op, id));
        rectsByChannel.set(id, FULL_RECT);
      }
      live.points = op.points ? op.points.map((p) => p.slice()) : null;
    }
    if (rectsByChannel.size) {
      const alphas = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE);
      for (const id of ids)
        packAtlasRect(
          bytes,
          ids.indexOf(id),
          presented.get(id),
          FULL_RECT,
          channelSpec(id).edit === "color",
          alphas,
        );
      packUnionRect(bytes, ids.length, FULL_RECT, alphas);
    }
    return result();
  }
  function update(layer) {
    if (disposed) throw new Error("Digital paint atlas session is disposed.");
    const ops = layer.paint?.operations || [],
      nextIds = paintedChannelIds(layer);
    if (!texture || JSON.stringify(nextIds) !== JSON.stringify(ids))
      return rebuild(layer);
    // History must still start with exactly the operations folded into the
    // base. Undo, redo to a different branch, clear and stroke edits all
    // break that prefix and take the one-shot path.
    for (let i = 0; i < baseKeys.length; i++)
      if (i >= ops.length || baseKeys[i] !== depositKey(ops[i]))
        return rebuild(layer);
    if (live && live.index >= ops.length) return rebuild(layer);
    // Fold operations that stopped being last since the previous update.
    while (baseKeys.length < Math.max(0, ops.length - 1)) {
      const index = baseKeys.length,
        op = ops[index];
      for (const id of ids) {
        if (!Object.hasOwn(op.channels, id)) continue;
        if (live && live.index === index && live.scratches.has(id)) {
          compositeInto(base.get(id), live.scratches.get(id), op);
        } else {
          const scratches = new Map();
          depositScratch(scratches, op, id, 0);
          compositeInto(base.get(id), scratches.get(id), op);
        }
      }
      baseKeys.push(depositKey(op));
      if (live && live.index === index) live = null;
    }
    const op = ops.at(-1);
    if (!op) return result();
    if (!live || live.index !== ops.length - 1 || live.key !== depositKey(op)) {
      live = {
        index: ops.length - 1,
        key: depositKey(op),
        scratches: new Map(),
        dabs: new Map(),
        points: null,
      };
    }
    const rectsByChannel = new Map();
    for (const id of ids) {
      if (!Object.hasOwn(op.channels, id)) continue;
      if (op.projection) {
        const depositedPoints = live.points?.length ?? -1;
        if (live.scratches.has(id) && depositedPoints === op.points.length)
          continue;
        depositScratch(live.scratches, op, id, 0);
        presentRect(
          presented.get(id),
          base.get(id),
          live.scratches.get(id),
          op,
          FULL_RECT,
        );
        rectsByChannel.set(id, FULL_RECT);
        continue;
      }
      if (op.kind === "fill") {
        if (live.scratches.has(id)) continue;
        depositScratch(live.scratches, op, id, 0);
        presentRect(
          presented.get(id),
          base.get(id),
          live.scratches.get(id),
          op,
          FULL_RECT,
        );
        rectsByChannel.set(id, FULL_RECT);
        continue;
      }
      const previousPoints = live.points,
        previousDabs = live.dabs.get(id) || [];
      // Strict append: every previously deposited sample is still present,
      // verbatim, at the head of the stroke. (An endpoint replacement keeps
      // the same length, so it can never satisfy this.)
      let appended = !!previousPoints && op.points.length > previousPoints.length;
      if (appended)
        for (let i = 0; i < previousPoints.length && appended; i++)
          appended =
            previousPoints[i][0] === op.points[i][0] &&
            previousPoints[i][1] === op.points[i][1] &&
            previousPoints[i][2] === op.points[i][2];
      // ⚠️ Tapers rescale every dab from total arc length, so even a strict
      //    append changes earlier dabs when a taper is configured.
      const tapered = !!(op.brush.taperStart || op.brush.taperEnd);
      if (appended && !tapered && previousDabs.length) {
        // Fast path: dabs before the last are bit-identical, because spacing
        // is arc length from the stroke origin. ⚠️ The final dab is clamped
        // to the stroke endpoint, so appending samples MOVES it — the tail
        // from that dab on must be repaired, never just appended over.
        const from = previousDabs.length - 1,
          tail = digitalBrushDabsFrom(
            op.points,
            op.brush,
            op.seed,
            MASK_TILE_EDGE,
            op.kind === "erase"
              ? "#ffffff"
              : channelSpec(id).edit === "color"
                ? op.channels[id]
                : scalarColour(op.channels[id], channelSpec(id)),
            id === "baseColor" && op.kind !== "erase",
            from,
          ),
          dabs = previousDabs.slice(0, from).concat(tail);
        if (tail.length === 1 && sameDab(tail[0], previousDabs[from])) {
          live.dabs.set(id, dabs);
          continue;
        }
        repairChannel(id, op, dabs, previousDabs, from, rectsByChannel);
        continue;
      }
      if (previousPoints && previousDabs.length) {
        // Endpoint move, interior smoothing or taper rescale: regenerate
        // and repair only the rectangle whose dabs actually changed.
        const dabs = channelDabs(op, id);
        let first = 0;
        while (
          first < Math.min(previousDabs.length, dabs.length) &&
          sameDab(previousDabs[first], dabs[first])
        )
          first++;
        if (first === previousDabs.length && first === dabs.length) continue;
        repairChannel(id, op, dabs, previousDabs, first, rectsByChannel);
        continue;
      }
      // First sight of this channel's stroke, or an interior sample changed
      // (stabilization): rebuild the live scratch for this channel.
      live.scratches.delete(id);
      depositScratch(live.scratches, op, id, 0);
      const dabs = channelDabs(op, id);
      live.dabs.set(id, dabs);
      const rect =
        dabRect(dabs, op.brush, previousDabs) || FULL_RECT;
      presentRect(
        presented.get(id),
        base.get(id),
        live.scratches.get(id),
        op,
        rect,
      );
      rectsByChannel.set(id, rect);
    }
    live.points = op.points ? op.points.map((p) => p.slice()) : null;
    packDirty(rectsByChannel);
    return result();
  }
  function repairChannel(id, op, dabs, previousDabs, first, rectsByChannel) {
    const rect = dabRect(dabs.slice(first), op.brush, previousDabs.slice(first));
    if (!rect || rect.width <= 0 || rect.height <= 0) {
      live.dabs.set(id, dabs);
      return;
    }
    if (!live.scratches.has(id)) {
      depositScratch(live.scratches, op, id, 0);
    } else {
      const scratch = live.scratches.get(id),
        ctx = scratch.getContext("2d"),
        cache = maskGeometryCache(geometry);
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.beginPath();
      ctx.rect(rect.x, rect.y, rect.width, rect.height);
      ctx.clip();
      ctx.clearRect(rect.x, rect.y, rect.width, rect.height);
      ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
      ctx.clip(cache.path(op.region));
      ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      // 📝 Redraw the operation's FULL dab sequence, clipped to the repair
      //    rectangle — never a filtered subset. Dab deposition is sequential
      //    source-over, so the repaired pixels must see the same draw order
      //    as a one-shot deposit; a subset can composite differently where
      //    earlier tips overlap the rectangle.
      drawDigitalDabs(ctx, dabs, op.brush);
      ctx.restore();
    }
    live.dabs.set(id, dabs);
    presentRect(presented.get(id), base.get(id), live.scratches.get(id), op, rect);
    rectsByChannel.set(id, rect);
  }
  return {
    update,
    dispose() {
      disposed = true;
      texture?.dispose();
      texture = null;
    },
  };
}
export function digitalStrokeThumbnail(operations, geometry, mask = false) {
  const atlas = digitalPaintCanvas(),
    ctx = atlas.getContext("2d");
  for (const op of operations) {
    const mark = rasterDigitalOperation(
      op,
      geometry,
      op.kind === "erase"
        ? "#d5d1c3"
        : mask
          ? op.brush.color
          : op.channels.baseColor || op.brush.color,
      !mask,
    );
    ctx.globalAlpha = op.brush.opacity;
    ctx.drawImage(mark, 0, 0);
  }
  ctx.globalAlpha = 1;
  const pixels = ctx.getImageData(0, 0, atlas.width, atlas.height).data;
  let x0 = atlas.width,
    x1 = 0,
    y0 = atlas.height,
    y1 = 0;
  for (let y = 0; y < atlas.height; y++)
    for (let x = 0; x < atlas.width; x++)
      if (pixels[(y * atlas.width + x) * 4 + 3]) {
        x0 = Math.min(x0, x);
        x1 = Math.max(x1, x);
        y0 = Math.min(y0, y);
        y1 = Math.max(y1, y);
      }
  const out = document.createElement("canvas");
  out.width = 256;
  out.height = 128;
  if (x0 <= x1) {
    const w = x1 - x0 + 1,
      h = y1 - y0 + 1,
      scale = Math.min(240 / w, 112 / h);
    out
      .getContext("2d")
      .drawImage(
        atlas,
        x0,
        y0,
        w,
        h,
        (256 - w * scale) / 2,
        (128 - h * scale) / 2,
        w * scale,
        h * scale,
      );
  }
  return {
    name: "Digital stroke footprint",
    origin: "paint",
    width: 256,
    height: 128,
    dataUrl: out.toDataURL("image/png"),
  };
}
