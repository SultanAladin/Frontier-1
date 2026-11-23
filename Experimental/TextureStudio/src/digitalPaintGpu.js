import * as THREE from "three";
import {
  digitalBrushDabs,
  digitalBrushDabsFrom,
} from "./digitalBrush.js";
import {
  createDigitalPaintAtlasSession,
  digitalPaintOpKey,
  sameStrokeDab,
  strokeDabRect,
} from "./digitalPaintRaster.js";
import {
  DIGITAL_PAINT_CHANNELS,
  paintedChannelIds,
} from "./textureDigitalPaint.js";
import { MASK_ATLAS_WIDTH, MASK_TILE_EDGE } from "./maskPaintRaster.js";
import {
  createPaintGpuKit,
  packDabInstances,
  hexRgb,
} from "./paintGpuKit.js";
import { createScreenPaintGpu } from "./screenPaintGpu.js";

export { packDabInstances };

// GPU-resident counterpart of createDigitalPaintAtlasSession (see
// digitalPaintRaster.js for the document contract both sessions honour:
// finished operations fold into a persistent base once, the growing final
// operation deposits only its new dabs, stroke opacity is applied exactly
// once at composite time, and any history edit that breaks the append-only
// prefix rebuilds from the operations list). Every stage lives in render
// targets: dabs are one instanced draw per stroke step, composites are
// fullscreen passes, and the published atlas is a WebGLArrayRenderTarget
// whose DataArrayTexture binds directly as the material shader's
// sampler2DArray — no canvas readback, no per-frame CPU packing, no slice
// re-uploads. Screen-projection operations transfer through
// screenPaintGpu.js; any GL failure delegates the layer wholesale to the
// CPU session. Dab placement, determinism and channel colour rules mirror
// the CPU session; tip falloff and grain are evaluated analytically in
// GLSL (the CPU reference rasterizes a 64px tip bitmap), so pixels are
// behaviourally equivalent but not guaranteed byte-identical.

const WIDTH = MASK_ATLAS_WIDTH;
const HEIGHT = MASK_TILE_EDGE;
const channelSpec = (id) => DIGITAL_PAINT_CHANNELS.find((c) => c.id === id);
const scalarColour = (value, spec) => {
  const v = Math.round(((value - spec.min) / (spec.max - spec.min)) * 255)
    .toString(16)
    .padStart(2, "0");
  return `#${v}${v}${v}`;
};

export function digitalPaintGpuSupportError(renderer) {
  if (!renderer) return new Error("Digital paint GPU session needs a renderer.");
  if (!renderer.capabilities?.isWebGL2)
    return new Error("Digital paint GPU session needs WebGL2.");
  return null;
}

const UNION_FRAG = /* glsl */ `
uniform sampler2D uSource;
varying vec2 vUv;
void main() {
  float a = texture2D(uSource, vUv).a;
  gl_FragColor = vec4(a, a, a, a);
}`;

export function createDigitalPaintGpuAtlasSession(renderer, geometry) {
  const supportError = digitalPaintGpuSupportError(renderer);
  if (supportError) throw supportError;
  const kit = createPaintGpuKit(renderer, geometry);
  let screen = null;
  const screenGpu = () =>
    (screen ??= createScreenPaintGpu(renderer, geometry, kit));
  const unionBlending = new THREE.CustomBlending();
  unionBlending.blendEquation = THREE.MaxEquation;
  unionBlending.blendSrc = THREE.OneFactor;
  unionBlending.blendDst = THREE.OneMinusSrcAlphaFactor;
  const unionMat = kit.material(UNION_FRAG, { uSource: { value: null } }, unionBlending);

  let ids = [],
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1),
    atlas = null,
    base = new Map(),
    presented = new Map(),
    foldScratches = new Map(),
    baseKeys = [],
    live = null,
    disposed = false,
    fallback = null;
  const result = () => ({
    texture: atlas.texture,
    ids,
    slots,
    unionSlot: ids.length,
    canvases: new Map(),
  });

  function channelColour(op, channelId) {
    const spec = channelSpec(channelId);
    return op.kind === "erase"
      ? "#ffffff"
      : spec.edit === "color"
        ? op.channels[channelId]
        : scalarColour(op.channels[channelId], spec);
  }
  function channelDabs(op, channelId) {
    return digitalBrushDabs(
      op.points,
      op.brush,
      op.seed,
      HEIGHT,
      channelColour(op, channelId),
      channelId === "baseColor" && op.kind !== "erase",
    );
  }
  // Deposit an operation into a scratch target. Stroke opacity is NOT
  // applied here; it lands exactly once, at composite time. Projection
  // marks are cache-owned by screenPaintGpu, so they are copied into the
  // session's own scratch (one fullscreen pass) and never drawn into.
  function depositScratch(scratches, op, channelId) {
    let scratch = scratches.get(channelId);
    if (!scratch) {
      scratch = kit.makeTarget();
      scratches.set(channelId, scratch);
    }
    if (op.projection) {
      const mark = screenGpu().projectMark(
        op,
        channelColour(op, channelId),
        channelId === "baseColor" && op.kind !== "erase",
      );
      kit.clearTarget(scratch);
      if (mark) kit.copy(mark.texture, scratch);
      return scratch;
    }
    kit.clearTarget(scratch);
    if (op.kind === "fill") {
      kit.fillRegion(scratch, op.region, hexRgb(channelColour(op, channelId)));
    } else {
      kit.drawDabs(scratch, channelDabs(op, channelId), op.brush, {
        region: op.region,
      });
    }
    return scratch;
  }
  // Composite a scratch over a target (optionally refreshing the target
  // from its base first). Erase uses destination-out factor blending, so
  // only the scratch coverage reaches the framebuffer.
  function compositeOver(target, baseTarget, scratch, op, channelId) {
    if (baseTarget) kit.copy(baseTarget.texture, target);
    const options = {
      opacity: op.brush.opacity,
      colour: channelSpec(channelId).edit === "color",
    };
    if (op.kind === "erase") kit.eraseOver(target, scratch.texture, options);
    else kit.compositeOver(target, scratch.texture, options);
  }
  function presentChannel(channelId, op) {
    compositeOver(
      presented.get(channelId),
      base.get(channelId),
      live.scratches.get(channelId),
      op,
      channelId,
    );
  }
  function publishSlices(dirty) {
    if (!dirty.size) return;
    for (const id of dirty)
      kit.copy(presented.get(id).texture, atlas, ids.indexOf(id));
    kit.clearTarget(atlas, ids.length);
    for (const id of ids) {
      unionMat.uniforms.uSource.value = presented.get(id).texture;
      kit.draw(atlas, unionMat, null, ids.length);
    }
  }
  // strokeDabRect speaks canvas rows (top-down); GL targets are bottom-up.
  const gpuRect = (rect) =>
    rect && {
      x: rect.x,
      y: HEIGHT - rect.y - rect.height,
      width: rect.width,
      height: rect.height,
    };
  function repairChannel(channelId, op, dabs, previousDabs, first, dirty) {
    const rect = gpuRect(strokeDabRect(dabs.slice(first), op.brush, previousDabs.slice(first)));
    live.dabs.set(channelId, dabs);
    if (!rect || rect.width <= 0 || rect.height <= 0) return;
    if (!live.scratches.has(channelId)) {
      depositScratch(live.scratches, op, channelId);
    } else {
      const scratch = live.scratches.get(channelId);
      kit.clearRect(scratch, rect);
      // Redraw the FULL dab sequence clipped to the repair rectangle —
      // never a filtered subset — so sequential source-over inside the
      // rectangle matches a one-shot deposit (same rule as the CPU session).
      kit.drawDabs(scratch, dabs, op.brush, { region: op.region, rect });
    }
    presentChannel(channelId, op);
    dirty.add(channelId);
  }
  function depositLiveChannels(op, dirty) {
    for (const id of ids) {
      if (!Object.hasOwn(op.channels, id)) continue;
      if (op.projection) {
        // Same rule as the CPU session: a projection stroke whose sample
        // count did not change deposits nothing new.
        if (live.scratches.has(id) && live.points?.length === op.points.length)
          continue;
        depositScratch(live.scratches, op, id);
        presentChannel(id, op);
        dirty.add(id);
        continue;
      }
      if (op.kind === "fill") {
        if (live.scratches.has(id)) continue;
        depositScratch(live.scratches, op, id);
        presentChannel(id, op);
        dirty.add(id);
        continue;
      }
      const previousPoints = live.points,
        previousDabs = live.dabs.get(id) || [];
      let appended = !!previousPoints && op.points.length > previousPoints.length;
      if (appended)
        for (let i = 0; i < previousPoints.length && appended; i++)
          appended =
            previousPoints[i][0] === op.points[i][0] &&
            previousPoints[i][1] === op.points[i][1] &&
            previousPoints[i][2] === op.points[i][2];
      const tapered = !!(op.brush.taperStart || op.brush.taperEnd);
      if (appended && !tapered && previousDabs.length) {
        // Dabs before the last are bit-identical on append; the final dab
        // is endpoint-clamped and MOVES, so the tail is repaired, never
        // just appended over (same rule as the CPU session).
        const from = previousDabs.length - 1,
          tail = digitalBrushDabsFrom(
            op.points,
            op.brush,
            op.seed,
            HEIGHT,
            channelColour(op, id),
            id === "baseColor" && op.kind !== "erase",
            from,
          ),
          dabs = previousDabs.slice(0, from).concat(tail);
        if (tail.length === 1 && sameStrokeDab(tail[0], previousDabs[from])) {
          live.dabs.set(id, dabs);
          continue;
        }
        repairChannel(id, op, dabs, previousDabs, from, dirty);
        continue;
      }
      if (previousPoints && previousDabs.length) {
        const dabs = channelDabs(op, id);
        let first = 0;
        while (
          first < Math.min(previousDabs.length, dabs.length) &&
          sameStrokeDab(previousDabs[first], dabs[first])
        )
          first++;
        if (first === previousDabs.length && first === dabs.length) continue;
        repairChannel(id, op, dabs, previousDabs, first, dirty);
        continue;
      }
      live.scratches.get(id)?.dispose();
      live.scratches.delete(id);
      depositScratch(live.scratches, op, id);
      live.dabs.set(id, channelDabs(op, id));
      presentChannel(id, op);
      dirty.add(id);
    }
    live.points = op.points ? op.points.map((p) => p.slice()) : null;
  }
  function disposeTargets() {
    atlas?.dispose();
    atlas = null;
    for (const map of [base, presented, foldScratches])
      for (const target of map.values()) target.dispose();
    base = new Map();
    presented = new Map();
    foldScratches = new Map();
    if (live) for (const target of live.scratches.values()) target.dispose();
    live = null;
  }
  function foldOperation(op) {
    for (const id of ids) {
      if (!Object.hasOwn(op.channels, id)) continue;
      const fromLive = live && live.scratches.has(id);
      const scratch = fromLive ? live.scratches.get(id) : depositScratch(foldScratches, op, id);
      compositeOver(base.get(id), null, scratch, op, id);
      if (fromLive) {
        scratch.dispose();
        live.scratches.delete(id);
      }
    }
  }
  function rebuild(layer) {
    const ops = layer.paint?.operations || [];
    ids = paintedChannelIds(layer);
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1);
    ids.forEach((id, slice) => {
      slots[DIGITAL_PAINT_CHANNELS.findIndex((c) => c.id === id)] = slice;
    });
    disposeTargets();
    atlas = kit.makeArrayTarget(ids.length + 1);
    base = new Map(ids.map((id) => [id, kit.makeTarget()]));
    presented = new Map(ids.map((id) => [id, kit.makeTarget()]));
    baseKeys = [];
    for (const id of ids) {
      kit.clearTarget(base.get(id));
      kit.clearTarget(presented.get(id));
    }
    for (const op of ops.slice(0, -1)) {
      foldOperation(op);
      baseKeys.push(digitalPaintOpKey(op));
    }
    for (const id of ids) kit.copy(base.get(id).texture, presented.get(id));
    const dirty = new Set(ids);
    const op = ops.at(-1);
    if (op) {
      live = {
        index: ops.length - 1,
        key: digitalPaintOpKey(op),
        scratches: new Map(),
        dabs: new Map(),
        points: null,
      };
      depositLiveChannels(op, dirty);
    }
    publishSlices(dirty);
    return result();
  }
  function updateGpu(layer) {
    const ops = layer.paint?.operations || [],
      nextIds = paintedChannelIds(layer);
    if (!atlas || JSON.stringify(nextIds) !== JSON.stringify(ids)) return rebuild(layer);
    for (let i = 0; i < baseKeys.length; i++)
      if (i >= ops.length || baseKeys[i] !== digitalPaintOpKey(ops[i])) return rebuild(layer);
    if (live && live.index >= ops.length) return rebuild(layer);
    const baseDirty = new Set();
    while (baseKeys.length < Math.max(0, ops.length - 1)) {
      const index = baseKeys.length;
      foldOperation(ops[index]);
      for (const id of ids)
        if (Object.hasOwn(ops[index].channels, id)) baseDirty.add(id);
      baseKeys.push(digitalPaintOpKey(ops[index]));
      if (live && live.index === index) live = null;
    }
    for (const id of baseDirty) kit.copy(base.get(id).texture, presented.get(id));
    const dirty = new Set(baseDirty);
    const op = ops.at(-1);
    if (!op) {
      publishSlices(dirty);
      return result();
    }
    if (!live || live.index !== ops.length - 1 || live.key !== digitalPaintOpKey(op)) {
      if (live) for (const target of live.scratches.values()) target.dispose();
      live = {
        index: ops.length - 1,
        key: digitalPaintOpKey(op),
        scratches: new Map(),
        dabs: new Map(),
        points: null,
      };
    }
    depositLiveChannels(op, dirty);
    publishSlices(dirty);
    return result();
  }
  function update(layer) {
    if (disposed) throw new Error("Digital paint atlas session is disposed.");
    if (fallback) return fallback.update(layer);
    try {
      return kit.withState(() => updateGpu(layer));
    } catch (error) {
      // A GL failure mid-stroke must never lose the painting: rebuild the
      // layer once through the CPU session and stay there.
      console.warn("Digital paint GPU session failed; falling back to CPU deposition.", error);
      fallback = createDigitalPaintAtlasSession(geometry);
      session.gpu = false;
      disposeTargets();
      return fallback.update(layer);
    }
  }
  const session = {
    gpu: true,
    update,
    dispose() {
      disposed = true;
      fallback?.dispose();
      fallback = null;
      screen?.dispose();
      screen = null;
      disposeTargets();
      kit.dispose();
    },
  };
  return session;
}

// Session factory used by the material surface controller: GPU deposition
// when the renderer supports it, the CPU session otherwise.
export function createDigitalPaintSession(renderer, geometry) {
  if (digitalPaintGpuSupportError(renderer)) return createDigitalPaintAtlasSession(geometry);
  try {
    return createDigitalPaintGpuAtlasSession(renderer, geometry);
  } catch {
    return createDigitalPaintAtlasSession(geometry);
  }
}

// ---------------------------------------------------------------------------
// Stroke thumbnails on the GPU. History records need a data URL, so one
// small readback per finished stroke remains; everything before it (dab
// deposition, fills, projection transfer, opacity compositing) runs in
// render targets on a lazily created shared renderer. Returns null when
// WebGL2 is unavailable so the caller keeps the CPU thumbnail.

let thumbnailGpu = null;

function ensureThumbnailGpu(geometry) {
  if (thumbnailGpu && thumbnailGpu.geometry === geometry) return thumbnailGpu;
  const stale = thumbnailGpu;
  thumbnailGpu = null;
  stale?.kit.dispose();
  stale?.screen.dispose();
  stale?.renderer.dispose();
  const renderer = new THREE.WebGLRenderer({
    antialias: false,
    alpha: false,
    powerPreference: "high-performance",
  });
  if (!renderer.capabilities.isWebGL2) {
    renderer.dispose();
    return null;
  }
  const kit = createPaintGpuKit(renderer, geometry);
  thumbnailGpu = {
    geometry,
    renderer,
    kit,
    screen: createScreenPaintGpu(renderer, geometry, kit),
    target: kit.makeTarget(),
    scratch: kit.makeTarget(),
  };
  return thumbnailGpu;
}

// GPU twin of screenOperationHasCoverage for stroke commit: shares the
// thumbnail stack (and its projection-mark cache), so the probe costs one
// cached transfer plus a single readback. Returns null when no GPU stack
// is available so the caller keeps the CPU probe.
export function digitalScreenCoverageGpu(op, geometry) {
  try {
    const stack = ensureThumbnailGpu(geometry);
    if (!stack) return null;
    return stack.screen.hasCoverage(op);
  } catch {
    return null;
  }
}

export function digitalStrokeThumbnailGpu(operations, geometry, mask = false) {
  try {
    const stack = ensureThumbnailGpu(geometry);
    if (!stack) return null;
    const { renderer, kit, screen, target, scratch } = stack;
    kit.withState(() => {
      kit.clearTarget(target);
      for (const op of operations) {
        const colour =
          op.kind === "erase"
            ? "#d5d1c3"
            : mask
              ? op.brush.color
              : op.channels.baseColor || op.brush.color;
        kit.clearTarget(scratch);
        if (op.projection) {
          const mark = screen.projectMark(op, colour, !mask);
          if (mark) kit.copy(mark.texture, scratch);
        } else if (op.kind === "fill") {
          kit.fillRegion(scratch, op.region, hexRgb(colour));
        } else {
          kit.drawDabs(
            scratch,
            digitalBrushDabs(op.points, op.brush, op.seed, HEIGHT, colour, !mask),
            op.brush,
            { region: op.region },
          );
        }
        kit.compositeOver(target, scratch.texture, {
          opacity: op.brush.opacity,
          colour: false,
        });
      }
    });
    const premult = new Uint8Array(WIDTH * HEIGHT * 4);
    renderer.readRenderTargetPixels(target, 0, 0, WIDTH, HEIGHT, premult);
    // Un-premultiply into a straight-alpha canvas, flipping GL rows into
    // canvas rows, exactly what rasterDigitalOperation canvases hold.
    const atlas = document.createElement("canvas");
    atlas.width = WIDTH;
    atlas.height = HEIGHT;
    const actx = atlas.getContext("2d", { willReadFrequently: true }),
      image = actx.createImageData(WIDTH, HEIGHT);
    for (let row = 0; row < HEIGHT; row++)
      for (let col = 0; col < WIDTH; col++) {
        const from = ((HEIGHT - 1 - row) * WIDTH + col) * 4,
          to = (row * WIDTH + col) * 4,
          a = premult[from + 3];
        image.data[to + 3] = a;
        if (a)
          for (let c = 0; c < 3; c++)
            image.data[to + c] = Math.round((premult[from + c] * 255) / a);
      }
    actx.putImageData(image, 0, 0);
    let x0 = WIDTH,
      x1 = 0,
      y0 = HEIGHT,
      y1 = 0;
    for (let y = 0; y < HEIGHT; y++)
      for (let x = 0; x < WIDTH; x++)
        if (image.data[(y * WIDTH + x) * 4 + 3]) {
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
  } catch (error) {
    console.warn("Digital stroke GPU thumbnail failed; falling back to CPU.", error);
    return null;
  }
}
