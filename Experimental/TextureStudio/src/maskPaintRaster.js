import { projectScreenOperation } from "./screenPaintProjection.js";
import {
  digitalBrushDabs,
  digitalBrushDabsFrom,
  drawDigitalDabs,
} from "./digitalBrush.js";
import * as THREE from "three";
import {
  uvTopology,
  barycentricUV,
  uvBoundaryLoops,
} from "./textureUVGeometry.js";
import {
  maskRegionContains,
  validateMaskPaintGeometry,
  maskHasPaint,
} from "./textureMaskPaint.js";
import { compilePointGradient } from "./pointGradient.js";
import { colourMaskMatch } from "./textureMasks.js";

export const MASK_TILE_EDGE = 256;
export const MASK_ATLAS_WIDTH = MASK_TILE_EDGE * 4;
const caches = new WeakMap();
const makeCanvas = () => {
  const c = document.createElement("canvas");
  c.width = MASK_ATLAS_WIDTH;
  c.height = MASK_TILE_EDGE;
  // Read back with getImageData by the coverage packer; see digitalPaintCanvas.
  c.getContext("2d", { willReadFrequently: true });
  return c;
};
export function maskGeometryCache(geometry) {
  if (caches.has(geometry)) return caches.get(geometry);
  const topology = uvTopology(geometry),
    paths = new Map(),
    loops = uvBoundaryLoops(geometry);
  function path(region) {
    const key = JSON.stringify(region);
    if (!paths.has(key)) {
      const p = new Path2D();
      if (region.kind === "face") {
        const t = topology.triangles[region.id];
        if (maskRegionContains(region, t)) {
          p.moveTo(...t.points[0]);
          p.lineTo(...t.points[1]);
          p.lineTo(...t.points[2]);
          p.closePath();
        }
      } else {
        // Exact triangle-union boundary contours, including holes. Clipping
        // thousands of overlapping triangle subpaths for every new brush
        // canvas was needlessly expensive (especially on software GPUs).
        for (const loop of loops)
          if (
            (region.kind === "surface" || loop.part?.id === region.partId) &&
            (region.kind !== "island" || loop.chartId === region.id)
          ) {
            p.moveTo(...loop.points[0]);
            for (const point of loop.points.slice(1)) p.lineTo(...point);
            p.closePath();
          }
      }
      paths.set(key, p);
    }
    return paths.get(key);
  }
  // Barycentric object coordinates for actual atlas texels, never a synthetic
  // plane or slice substituted for the teapot's gradient field.
  let lookup = null;
  function positions() {
    if (lookup) return lookup;
    geometry.computeBoundingSphere();
    const sphere = geometry.boundingSphere,
      p = geometry.attributes.position,
      xyz = new Float32Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 3),
      occupied = new Uint8Array(MASK_ATLAS_WIDTH * MASK_TILE_EDGE);
    for (const t of topology.triangles) {
      const loX = Math.max(0, Math.floor(t.min[0] * MASK_TILE_EDGE)),
        hiX = Math.min(
          MASK_ATLAS_WIDTH - 1,
          Math.ceil(t.max[0] * MASK_TILE_EDGE),
        ),
        loY = Math.max(0, Math.floor((1 - t.max[1]) * MASK_TILE_EDGE)),
        hiY = Math.min(
          MASK_TILE_EDGE - 1,
          Math.ceil((1 - t.min[1]) * MASK_TILE_EDGE),
        );
      for (let y = loY; y <= hiY; y++)
        for (let x = loX; x <= hiX; x++) {
          const w = barycentricUV(
            [(x + 0.5) / MASK_TILE_EDGE, 1 - (y + 0.5) / MASK_TILE_EDGE],
            t,
          );
          if (!w) continue;
          const k = y * MASK_ATLAS_WIDTH + x;
          occupied[k] = 1;
          for (let c = 0; c < 3; c++)
            xyz[k * 3 + c] =
              (t.ids.reduce(
                (sum, id, j) => sum + p.array[id * 3 + c] * w[j],
                0,
              ) -
                sphere.center.getComponent(c)) /
              sphere.radius;
        }
    }
    return (lookup = { xyz, occupied });
  }
  const cache = { path, positions };
  caches.set(geometry, cache);
  return cache;
}
export function rasterizeMaskPaint(mask, geometry) {
  validateMaskPaintGeometry(mask?.paint, geometry);
  const canvas = makeCanvas(),
    ctx = canvas.getContext("2d"),
    cache = maskGeometryCache(geometry);
  ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
  for (const op of mask?.paint?.operations || []) {
    ctx.save();
    // Projected strokes already enforce their real triangle/selection scope.
    // Keep their empty-gutter padding for seamless filtered mask sampling.
    if (!op.digital?.projection) ctx.clip(cache.path(op.region));
    const grey = Math.round(op.value * 255);
    if (op.kind === "region") {
      ctx.fillStyle = `rgba(${grey},${grey},${grey},${op.opacity})`;
      ctx.fill(cache.path(op.region));
    } else if (op.digital?.projection) {
      const color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`;
      const mark = projectScreenOperation(
        {
          region: op.region,
          projection: op.digital.projection,
          points: op.points.map((p, i) => [...p, op.digital.pressure[i]]),
          brush: op.digital.brush,
          seed: op.digital.seed,
          kind: op.digital.operation,
        },
        geometry,
        color,
        false,
      );
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = op.opacity;
      ctx.globalCompositeOperation =
        op.digital.operation === "erase" ? "destination-out" : "source-over";
      ctx.drawImage(mark, 0, 0);
    } else if (op.digital) {
      // Shared digital tip/pressure/grain renderer. Erasing removes the painted
      // overlay, restoring the underlying fill/gradient/source, not black ink.
      const mark = makeCanvas(),
        ink = mark.getContext("2d");
      ink.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      const color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`;
      const points = op.points.map((p, i) => [...p, op.digital.pressure[i]]);
      drawDigitalDabs(
        ink,
        digitalBrushDabs(
          points,
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          color,
          false,
        ),
        op.digital.brush,
      );
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = op.opacity;
      ctx.globalCompositeOperation =
        op.digital.operation === "erase" ? "destination-out" : "source-over";
      ctx.drawImage(mark, 0, 0);
    } else {
      // Spaced radial dabs provide a genuine soft/hard UV brush. Each stroke
      // is clipped to its semantic region, so a lid stroke cannot touch Body.
      const dab = (x, y) => {
        const g = ctx.createRadialGradient(x, y, 0, x, y, op.radius),
          ink = `rgba(${grey},${grey},${grey},${op.opacity})`;
        g.addColorStop(0, ink);
        g.addColorStop(Math.min(0.999, op.hardness), ink);
        g.addColorStop(1, `rgba(${grey},${grey},${grey},0)`);
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(x, y, op.radius, 0, Math.PI * 2);
        ctx.fill();
      };
      dab(...op.points[0]);
      for (let i = 1; i < op.points.length; i++) {
        const a = op.points[i - 1],
          b = op.points[i],
          n = Math.ceil(
            Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35),
          );
        for (let j = 1; j <= n; j++)
          dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
      }
    }
    ctx.restore();
  }
  return canvas;
}

// Shared with maskPaintGpu.js, whose session must fold on identical keys.
export const maskOpKey = (op) =>
  JSON.stringify([
    op.kind,
    op.region,
    op.value,
    op.opacity,
    op.radius ?? null,
    op.hardness ?? null,
    op.digital
      ? [
          op.digital.seed,
          op.digital.operation,
          op.digital.brush,
          op.digital.projection ?? null,
        ]
      : null,
  ]);
const maskDabExtent = (dab, brush) =>
  dab.radius * Math.sqrt(1 + brush.roundness * brush.roundness) + 2;
export function maskDabRect(dabs, brush, previous) {
  let x0 = Infinity,
    y0 = Infinity,
    x1 = -Infinity,
    y1 = -Infinity;
  const visit = (d) => {
    const e = maskDabExtent(d, brush);
    x0 = Math.min(x0, d.x - e);
    x1 = Math.max(x1, d.x + e);
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
export const sameMaskDab = (a, b) =>
  a === b ||
  (a &&
    b &&
    a.x === b.x &&
    a.y === b.y &&
    a.radius === b.radius &&
    a.opacity === b.opacity &&
    a.angle === b.angle &&
    a.color === b.color);
const MASK_FULL_RECT = Object.freeze({
  x: 0,
  y: 0,
  width: MASK_ATLAS_WIDTH,
  height: MASK_TILE_EDGE,
});
// 💡 Incremental twin of rasterizeMaskPaint, used for live mask strokes:
//    finished operations fold into a persistent base canvas once, the
//    growing operation deposits only its new dabs into a scratch canvas,
//    and the presented canvas is repaired over the changed rectangle
//    only. Any history edit that breaks the append-only prefix falls back
//    to rasterizeMaskPaint, so output stays identical.
export function createMaskPaintSession(geometry) {
  let base = makeCanvas(),
    presented = makeCanvas(),
    baseKeys = [],
    live = null,
    disposed = false;
  function composite(target, source, op, rect) {
    const ctx = target.getContext("2d"),
      cache = maskGeometryCache(geometry);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.beginPath();
    ctx.rect(rect.x, rect.y, rect.width, rect.height);
    ctx.clip();
    // Digital marks are deposited unclipped; their region clip belongs to
    // this composite, exactly as in rasterizeMaskPaint. Region fills and
    // legacy dabs were already clipped into their scratch.
    if (op.digital && !op.digital.projection) {
      ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
      ctx.clip(cache.path(op.region));
      ctx.setTransform(1, 0, 0, 1, 0, 0);
    }
    ctx.globalCompositeOperation =
      op.digital?.operation === "erase" ? "destination-out" : "source-over";
    // Region scratches store unscaled coverage and digital marks store
    // dab alpha: both take op.opacity here. Legacy scratches already hold
    // opacity-scaled gradients, exactly as deposited by the one-shot path.
    ctx.globalAlpha = op.digital || op.kind === "region" ? op.opacity : 1;
    ctx.drawImage(source, 0, 0);
    ctx.restore();
  }
  // Non-digital operations deposit directly on the presented canvas, in
  // the same draw sequence as rasterizeMaskPaint — an intermediate scratch
  // would blend twice and drift by a byte or two on soft dab edges.
  function depositDirect(target, op, fromPoint) {
    const ctx = target.getContext("2d"),
      cache = maskGeometryCache(geometry),
      grey = Math.round(op.value * 255);
    ctx.save();
    ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
    ctx.clip(cache.path(op.region));
    if (op.kind === "region") {
      if (!fromPoint) {
        ctx.fillStyle = `rgba(${grey},${grey},${grey},${op.opacity})`;
        ctx.fill(cache.path(op.region));
      }
    } else {
      const dab = (x, y) => {
        const g = ctx.createRadialGradient(x, y, 0, x, y, op.radius),
          ink = `rgba(${grey},${grey},${grey},${op.opacity})`;
        g.addColorStop(0, ink);
        g.addColorStop(Math.min(0.999, op.hardness), ink);
        g.addColorStop(1, `rgba(${grey},${grey},${grey},0)`);
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(x, y, op.radius, 0, Math.PI * 2);
        ctx.fill();
      };
      const points = op.points || [];
      if (!fromPoint && points.length) dab(...points[0]);
      for (let i = Math.max(1, fromPoint); i < points.length; i++) {
        const a = points[i - 1],
          b = points[i],
          n = Math.ceil(
            Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35),
          );
        for (let j = 1; j <= n; j++)
          dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
      }
    }
    ctx.restore();
  }
  function presentRect(op, rect) {
    const ctx = presented.getContext("2d");
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.beginPath();
    ctx.rect(rect.x, rect.y, rect.width, rect.height);
    ctx.clip();
    ctx.globalCompositeOperation = "copy";
    ctx.drawImage(base, 0, 0);
    ctx.restore();
    composite(presented, live.scratch, op, rect);
  }
  function foldInto(target, op) {
    // 📝 Non-digital operations fold by drawing directly on the target,
    //    byte-for-byte the one-shot deposition (an intermediate scratch
    //    would add a second premultiplied blend). Digital operations keep
    //    their mark-scratch + clipped composite, which is also exactly
    //    what rasterizeMaskPaint does.
    if (!op.digital) {
      const ctx = target.getContext("2d"),
        cache = maskGeometryCache(geometry),
        grey = Math.round(op.value * 255);
      ctx.save();
      ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
      ctx.clip(cache.path(op.region));
      if (op.kind === "region") {
        ctx.fillStyle = `rgba(${grey},${grey},${grey},${op.opacity})`;
        ctx.fill(cache.path(op.region));
      } else {
        const dab = (x, y) => {
          const g = ctx.createRadialGradient(x, y, 0, x, y, op.radius),
            ink = `rgba(${grey},${grey},${grey},${op.opacity})`;
          g.addColorStop(0, ink);
          g.addColorStop(Math.min(0.999, op.hardness), ink);
          g.addColorStop(1, `rgba(${grey},${grey},${grey},0)`);
          ctx.fillStyle = g;
          ctx.beginPath();
          ctx.arc(x, y, op.radius, 0, Math.PI * 2);
          ctx.fill();
        };
        const points = op.points || [];
        if (points.length) dab(...points[0]);
        for (let i = 1; i < points.length; i++) {
          const a = points[i - 1],
            b = points[i],
            n = Math.ceil(
              Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35),
            );
          for (let j = 1; j <= n; j++)
            dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
        }
      }
      ctx.restore();
      return;
    }
    const scratch = makeCanvas();
    deposit(scratch, op, 0);
    const ctx = target.getContext("2d"),
      cache = maskGeometryCache(geometry);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    if (!op.digital.projection) {
      ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
      ctx.clip(cache.path(op.region));
      ctx.setTransform(1, 0, 0, 1, 0, 0);
    }
    ctx.globalCompositeOperation =
      op.digital.operation === "erase" ? "destination-out" : "source-over";
    ctx.globalAlpha = op.opacity;
    ctx.drawImage(scratch, 0, 0);
    ctx.restore();
  }
  function deposit(scratch, op, fromDab) {
    const ctx = scratch.getContext("2d"),
      cache = maskGeometryCache(geometry),
      grey = Math.round(op.value * 255);
    if (op.kind === "region") {
      if (fromDab) return;
      ctx.save();
      ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
      ctx.clip(cache.path(op.region));
      // Opacity is applied once at composite time; the scratch stores the
      // unscaled coverage so the blend quantizes exactly like the one-shot
      // path's single fill.
      ctx.fillStyle = `rgb(${grey},${grey},${grey})`;
      ctx.fill(cache.path(op.region));
      ctx.restore();
      return;
    }
    if (op.digital?.projection) {
      const color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`;
      const mark = projectScreenOperation(
        {
          region: op.region,
          projection: op.digital.projection,
          points: op.points.map((p, i) => [...p, op.digital.pressure[i]]),
          brush: op.digital.brush,
          seed: op.digital.seed,
          kind: op.digital.operation,
        },
        geometry,
        color,
        false,
      );
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalCompositeOperation = "copy";
      ctx.drawImage(mark, 0, 0);
      ctx.restore();
      return;
    }
    if (op.digital) {
      const color = `#${grey.toString(16).padStart(2, "0").repeat(3)}`,
        points = op.points.map((p, i) => [...p, op.digital.pressure[i]]);
      ctx.save();
      ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      drawDigitalDabs(
        ctx,
        digitalBrushDabsFrom(
          points,
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          color,
          false,
          fromDab,
        ),
        op.digital.brush,
      );
      ctx.restore();
      return;
    }
    // Legacy radial-dab brush: deposit whole segments only. Segment i adds
    // its interpolated dabs; segment 0 is the initial point dab.
    ctx.save();
    ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
    ctx.clip(cache.path(op.region));
    const dab = (x, y) => {
      const g = ctx.createRadialGradient(x, y, 0, x, y, op.radius),
        ink = `rgba(${grey},${grey},${grey},${op.opacity})`;
      g.addColorStop(0, ink);
      g.addColorStop(Math.min(0.999, op.hardness), ink);
      g.addColorStop(1, `rgba(${grey},${grey},${grey},0)`);
      ctx.fillStyle = g;
      ctx.beginPath();
      ctx.arc(x, y, op.radius, 0, Math.PI * 2);
      ctx.fill();
    };
    const points = op.points || [];
    if (!fromDab && points.length) dab(...points[0]);
    for (let i = Math.max(1, fromDab); i < points.length; i++) {
      const a = points[i - 1],
        b = points[i],
        n = Math.ceil(Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35));
      for (let j = 1; j <= n; j++)
        dab(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
    }
    ctx.restore();
  }
  function rebuild(mask) {
    validateMaskPaintGeometry(mask?.paint, geometry);
    const canvas = rasterizeMaskPaint(mask, geometry);
    base = makeCanvas();
    presented = canvas;
    baseKeys = [];
    live = null;
    const ops = mask?.paint?.operations || [];
    // Rebuild the foldable base without the last operation so the live
    // path can take it over; presented already holds the full result.
    for (const op of ops.slice(0, -1)) {
      foldInto(base, op);
      baseKeys.push(maskOpKey(op));
    }
    if (ops.length) {
      const op = ops.at(-1);
      live = {
        index: ops.length - 1,
        key: maskOpKey(op),
        scratch: makeCanvas(),
        dabs: null,
        points: op.points ? op.points.map((p) => p.slice()) : null,
        depositedPoints: op.digital ? null : op.points?.length || 0,
      };
      deposit(live.scratch, op, 0);
      if (op.digital && !op.digital.projection && op.kind !== "region")
        live.dabs = digitalBrushDabs(
          op.points.map((p, i) => [...p, op.digital.pressure[i]]),
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          `#${Math.round(op.value * 255).toString(16).padStart(2, "0").repeat(3)}`,
          false,
        );
    }
    return presented;
  }
  function update(mask) {
    if (disposed) throw new Error("Mask paint session is disposed.");
    const ops = mask?.paint?.operations || [];
    if (!live && baseKeys.length === 0 && ops.length === 0) {
      // Still return the (empty) presented canvas without a full rebuild.
      if (!presented) presented = makeCanvas();
      return presented;
    }
    for (let i = 0; i < baseKeys.length; i++)
      if (i >= ops.length || baseKeys[i] !== maskOpKey(ops[i]))
        return rebuild(mask);
    if (live && live.index >= ops.length) return rebuild(mask);
    while (baseKeys.length < Math.max(0, ops.length - 1)) {
      const index = baseKeys.length,
        op = ops[index];
      if (live && live.index === index && op.digital) {
        const ctx = base.getContext("2d"),
          cache = maskGeometryCache(geometry);
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        if (!op.digital.projection) {
          ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
          ctx.clip(cache.path(op.region));
          ctx.setTransform(1, 0, 0, 1, 0, 0);
        }
        ctx.globalCompositeOperation =
          op.digital.operation === "erase" ? "destination-out" : "source-over";
        ctx.globalAlpha = op.opacity;
        ctx.drawImage(live.scratch, 0, 0);
        ctx.restore();
        live = null;
      } else foldInto(base, op);
      baseKeys.push(maskOpKey(op));
    }
    const op = ops.at(-1);
    if (!op) return presented;
    if (!live || live.index !== ops.length - 1 || live.key !== maskOpKey(op)) {
      // A non-digital operation deposits directly on the presented canvas,
      // so takeover starts from the exact one-shot raster of the history
      // (once per operation transition, never per frame).
      if (!op.digital && baseKeys.length) return rebuild(mask);
      live = {
        index: ops.length - 1,
        key: maskOpKey(op),
        scratch: makeCanvas(),
        dabs: null,
        points: null,
        depositedPoints: 0,
      };
      deposit(live.scratch, op, 0);
      if (op.digital && !op.digital.projection && op.kind !== "region")
        live.dabs = digitalBrushDabs(
          op.points.map((p, i) => [...p, op.digital.pressure[i]]),
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          `#${Math.round(op.value * 255).toString(16).padStart(2, "0").repeat(3)}`,
          false,
        );
      live.points = op.points ? op.points.map((p) => p.slice()) : null;
      live.depositedPoints = op.points?.length || 0;
      presentRect(op, MASK_FULL_RECT);
      return presented;
    }
    if (op.kind === "region") return presented;
    if (op.digital?.projection) {
      if (live.points?.length === op.points.length) return presented;
      deposit(live.scratch, op, 0);
      live.points = op.points.map((p) => p.slice());
      presentRect(op, MASK_FULL_RECT);
      return presented;
    }
    if (op.digital) {
      const points3 = op.points.map((p, i) => [...p, op.digital.pressure[i]]),
        color = `#${Math.round(op.value * 255).toString(16).padStart(2, "0").repeat(3)}`,
        dabs = digitalBrushDabs(points3, op.digital.brush, op.digital.seed, MASK_TILE_EDGE, color, false),
        previous = live.dabs || [];
      let first = 0;
      while (
        first < Math.min(previous.length, dabs.length) &&
        sameMaskDab(previous[first], dabs[first])
      )
        first++;
      if (first === previous.length && first === dabs.length) return presented;
      const rect = maskDabRect(dabs.slice(first), op.digital.brush, previous.slice(first));
      if (!rect || rect.width <= 0 || rect.height <= 0) {
        live.dabs = dabs;
        return presented;
      }
      // Repair the scratch rectangle with the full dab sequence (clipped),
      // mirroring one-shot sequential deposition.
      const ctx = live.scratch.getContext("2d");
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.beginPath();
      ctx.rect(rect.x, rect.y, rect.width, rect.height);
      ctx.clip();
      ctx.clearRect(rect.x, rect.y, rect.width, rect.height);
      ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
      drawDigitalDabs(ctx, dabs, op.digital.brush);
      ctx.restore();
      live.dabs = dabs;
      presentRect(op, rect);
      return presented;
    }
    // Legacy brush growth: only new segments deposit, directly on the
    // presented canvas (points are append-only; segment dabs never move).
    const from = live.depositedPoints || 0;
    if (op.points.length <= from) return presented;
    depositDirect(presented, op, from);
    live.depositedPoints = op.points.length;
    return presented;
  }
  return {
    update,
    dispose() {
      disposed = true;
    },
  };
}

export function maskColourSource(layer, doc) {
  const sourceLayer =
    layer?.kind === "folder"
      ? doc.layers.find((l) => {
          let node = l;
          while (node?.parentId) {
            if (node.parentId === layer.id)
              return l.channels.some(
                (id) =>
                  l.channelSettings[id]?.texture ||
                  l.channelSettings[id]?.generator?.result,
              );
            node = doc.layers.find((v) => v.id === node.parentId);
          }
          return false;
        })
      : layer;
  const settings = sourceLayer?.channels.map(
    (id) => sourceLayer.channelSettings[id],
  );
  const setting = settings?.find((s) => s.texture || s.generator?.result);
  return setting?.texture || setting?.generator?.result || null;
}
export async function readMaskSource(source) {
  if (!source) return null;
  const image = new Image();
  image.src = source.dataUrl;
  await image.decode();
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = MASK_TILE_EDGE;
  const ctx = canvas.getContext("2d", { willReadFrequently: true });
  ctx.drawImage(image, 0, 0, MASK_TILE_EDGE, MASK_TILE_EDGE);
  return ctx.getImageData(0, 0, MASK_TILE_EDGE, MASK_TILE_EDGE).data;
}
export function rasterizeMaskField(
  mask,
  geometry,
  source = null,
  strokesCanvas = null,
) {
  const strokes = (strokesCanvas || rasterizeMaskPaint(mask, geometry))
      .getContext("2d")
      .getImageData(0, 0, MASK_ATLAS_WIDTH, MASK_TILE_EDGE).data,
    output = makeCanvas(),
    ctx = output.getContext("2d"),
    image = ctx.createImageData(MASK_ATLAS_WIDTH, MASK_TILE_EDGE),
    sample =
      mask.kind === "gradient" ? compilePointGradient(mask.gradient) : null,
    lookup = sample ? maskGeometryCache(geometry).positions() : null;
  for (let k = 0; k < MASK_ATLAS_WIDTH * MASK_TILE_EDGE; k++) {
    let raw = mask.fill;
    if (sample) {
      const c = sample(Array.from(lookup.xyz.subarray(k * 3, k * 3 + 3)));
      raw = c[0] * 0.2126 + c[1] * 0.7152 + c[2] * 0.0722;
    }
    if (mask.kind === "color") {
      const n =
        (Math.floor(k / MASK_ATLAS_WIDTH) * MASK_TILE_EDGE +
          (k % MASK_TILE_EDGE)) *
        4;
      raw = source
        ? (colourMaskMatch(
            [source[n] / 255, source[n + 1] / 255, source[n + 2] / 255],
            { ...mask, inverted: false, paint: null },
          ) *
            source[n + 3]) /
          255
        : 0;
    }
    const a = strokes[k * 4 + 3] / 255;
    raw = raw * (1 - a) + (strokes[k * 4] / 255) * a;
    if (mask.inverted) raw = 1 - raw;
    const grey = Math.round(
      255 * (mask.enabled ? 1 + ((raw - 1) * mask.strength) / 100 : 1),
    );
    image.data[k * 4] = image.data[k * 4 + 1] = image.data[k * 4 + 2] = grey;
    image.data[k * 4 + 3] = 255;
  }
  ctx.putImageData(image, 0, 0);
  return output;
}
export function createMaskCoverageTexture(geometry) {
  const canvas = makeCanvas(),
    texture = new THREE.CanvasTexture(canvas);
  texture.colorSpace = THREE.NoColorSpace;
  texture.generateMipmaps = false;
  texture.minFilter = texture.magFilter = THREE.LinearFilter;
  const uniforms = {
    uPaintedMaskAtlas: { value: texture },
    uPaintedMaskEnabled: { value: 0 },
  };
  let ticket = 0,
    disposed = false;
  const sessions = new Map();
  return {
    uniforms,
    async update(owners, doc) {
      const job = ++ticket,
        masks = owners.filter((l) => maskHasPaint(l.mask));
      if (!masks.length) {
        uniforms.uPaintedMaskEnabled.value = 0;
        return;
      }
      const fields = await Promise.all(
        masks.map(async (l) => {
          let session = sessions.get(l.id);
          if (!session) {
            session = createMaskPaintSession(geometry);
            sessions.set(l.id, session);
          }
          return rasterizeMaskField(
            l.mask,
            geometry,
            l.mask.kind === "color"
              ? await readMaskSource(maskColourSource(l, doc))
              : null,
            session.update(l.mask),
          );
        }),
      );
      for (const id of sessions.keys())
        if (!masks.some((l) => l.id === id)) {
          sessions.get(id).dispose();
          sessions.delete(id);
        }
      if (disposed || job !== ticket) return;
      const ctx = canvas.getContext("2d");
      ctx.globalCompositeOperation = "source-over";
      ctx.fillStyle = "white";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.globalCompositeOperation = "multiply";
      for (const f of fields) ctx.drawImage(f, 0, 0);
      ctx.globalCompositeOperation = "source-over";
      uniforms.uPaintedMaskEnabled.value = 1;
      texture.needsUpdate = true;
    },
    dispose() {
      disposed = true;
      ticket++;
      texture.dispose();
    },
  };
}
export function installMaskPaintCoverage(material, uniforms, mode = "alpha") {
  const original = material.onBeforeCompile,
    cache = material.customProgramCacheKey.bind(material);
  material.onBeforeCompile = (shader) => {
    original.call(material, shader);
    Object.assign(shader.uniforms, uniforms);
    shader.fragmentShader =
      "uniform sampler2D uPaintedMaskAtlas; uniform int uPaintedMaskEnabled;\n" +
      shader.fragmentShader;
    const value =
      "(uPaintedMaskEnabled==1?texture2D(uPaintedMaskAtlas,vec2(vAuthoringUv.x/4.,vAuthoringUv.y)).r:1.)";
    shader.fragmentShader =
      mode === "gradient"
        ? shader.fragmentShader.replace(
            "float coverage=uPointGradientOpacity",
            `float coverage=${value}*uPointGradientOpacity`,
          )
        : shader.fragmentShader.replace(
            "#include <alphatest_fragment>",
            `diffuseColor.a*=${value};\n#include <alphatest_fragment>`,
          );
  };
  material.customProgramCacheKey = () =>
    cache() + `|painted-uv-mask-v1-${mode}`;
}
