import * as THREE from "three";
import {
  digitalBrushDabs,
  digitalBrushDabsFrom,
} from "./digitalBrush.js";
import {
  maskGeometryCache,
  maskOpKey,
  maskDabRect,
  sameMaskDab,
  maskColourSource,
  readMaskSource,
  createMaskCoverageTexture,
  MASK_ATLAS_WIDTH,
  MASK_TILE_EDGE,
} from "./maskPaintRaster.js";
import {
  maskHasPaint,
  validateMaskPaintGeometry,
} from "./textureMaskPaint.js";
import {
  normalizePointGradient,
  linearColour,
} from "./pointGradient.js";
import { createPaintGpuKit, hexRgb } from "./paintGpuKit.js";
import { createScreenPaintGpu } from "./screenPaintGpu.js";

// GPU counterpart of the mask chain in maskPaintRaster.js: the incremental
// mask session (createMaskPaintSession), the field bake (rasterizeMaskField)
// and the owner-field multiply (createMaskCoverageTexture) all run in
// render targets. Op folding, append-only prefix validation and live-repair
// rules mirror the CPU session one-for-one; the CPU chain remains the
// fallback (see createMaskCoverageTexture) and the pixel reference.

const WIDTH = MASK_ATLAS_WIDTH,
  HEIGHT = MASK_TILE_EDGE;

// Interpolated radial-dab centres of a legacy mask stroke, in atlas texel
// space (dab frame: x right, y up). Segment i contributes its subdivided
// dabs; segment 0 is the initial point dab. Pure — it is the exact loop
// rasterizeMaskPaint / deposit perform on canvas, exported for the Node
// checks in scripts/verify-mask-gpu.mjs.
export function legacyMaskDabs(op, fromPoint = 0) {
  const dabs = [],
    points = op.points || [],
    radius = op.radius * MASK_TILE_EDGE,
    push = (x, y) =>
      dabs.push({ x: x * MASK_TILE_EDGE, y: y * MASK_TILE_EDGE, radius });
  if (!fromPoint && points.length) push(points[0][0], points[0][1]);
  for (let i = Math.max(1, fromPoint); i < points.length; i++) {
    const a = points[i - 1],
      b = points[i],
      n = Math.ceil(
        Math.hypot(b[0] - a[0], b[1] - a[1]) / (op.radius * 0.35),
      );
    for (let j = 1; j <= n; j++)
      push(a[0] + ((b[0] - a[0]) * j) / n, a[1] + ((b[1] - a[1]) * j) / n);
  }
  return dabs;
}

// Uniform payload for the field shader's Gaussian point gradient — the
// CPU's compilePointGradient evaluated per texel in GLSL. Pure and
// exported for the Node checks in scripts/verify-mask-gpu.mjs.
export function packGradientUniforms(gradient) {
  const normalized = normalizePointGradient(gradient),
    pointA = new Float32Array(48),
    pointB = new Float32Array(48);
  normalized.points.forEach((p, i) => {
    pointA.set([...p.position, p.radius], i * 4);
    pointB.set([...linearColour(p.color), p.weight], i * 4);
  });
  return {
    count: normalized.points.length,
    pointA,
    pointB,
    background: linearColour(normalized.background),
  };
}

const FIELD_FRAG = /* glsl */ `
uniform sampler2D uStrokes;
uniform sampler2D uPositions;
uniform sampler2D uSource;
uniform float uKind;
uniform float uFill;
uniform float uInverted;
uniform float uStrength;
uniform float uEnabled;
uniform vec3 uTarget;
uniform float uTolerance;
uniform float uSoftness;
uniform int uPointCount;
uniform vec4 uPointA[12];
uniform vec4 uPointB[12];
uniform vec3 uBackground;
varying vec2 vUv;
void main() {
  vec2 canvasUv = vec2(vUv.x, 1.0 - vUv.y);
  float raw = uFill;
  if (uKind > 0.5 && uKind < 1.5) {
    vec3 p = texture2D(uPositions, canvasUv).rgb;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < 12; i++) {
      if (i >= uPointCount) break;
      vec4 pa = uPointA[i];
      vec4 pb = uPointB[i];
      float r2 = pa.w * pa.w;
      if (pb.w <= 0.0 || r2 <= 0.0) continue;
      vec3 d = pa.xyz - p;
      float w = pb.w * exp(-dot(d, d) / r2);
      total += w;
      sum += pb.rgb * w;
    }
    vec3 c = total > 1e-8 ? sum / total : uBackground;
    raw = c.r * 0.2126 + c.g * 0.7152 + c.b * 0.0722;
  } else if (uKind > 1.5) {
    vec4 src = texture2D(uSource, vec2(fract(canvasUv.x * 4.0), canvasUv.y));
    float distance = length(uTarget - src.rgb) / 1.7320508;
    float matchValue;
    if (uSoftness <= 0.0)
      matchValue = distance <= uTolerance + 1e-9 ? 1.0 : 0.0;
    else {
      float t = clamp((distance - uTolerance) / uSoftness, 0.0, 1.0);
      matchValue = 1.0 - t * t * (3.0 - 2.0 * t);
    }
    raw = matchValue * src.a;
  }
  vec4 s = texture2D(uStrokes, vUv);
  raw = raw * (1.0 - s.a) + s.r;
  if (uInverted > 0.5) raw = 1.0 - raw;
  float g = uEnabled > 0.5 ? 1.0 + (raw - 1.0) * uStrength / 100.0 : 1.0;
  gl_FragColor = vec4(vec3(clamp(g, 0.0, 1.0)), 1.0);
}`;

const MULTIPLY_FRAG = /* glsl */ `
uniform sampler2D uField;
varying vec2 vUv;
void main() {
  float g = texture2D(uField, vUv).r;
  gl_FragColor = vec4(g, g, g, 1.0);
}`;

const greyHex = (op) =>
  `#${Math.round(op.value * 255).toString(16).padStart(2, "0").repeat(3)}`;
const grey01 = (op) => Math.round(op.value * 255) / 255;
const greyRgb = (op) => {
  const g = grey01(op);
  return [g, g, g];
};
const points3 = (op) =>
  op.points.map((p, i) => [...p, op.digital.pressure[i]]);
// maskDabRect speaks canvas rows (top-down); GL targets are bottom-up.
const glRect = (rect) => ({
  x: rect.x,
  y: HEIGHT - rect.y - rect.height,
  width: rect.width,
  height: rect.height,
});

// Incremental GPU twin of createMaskPaintSession: update(mask) returns the
// presented render target (strokes in premultiplied grey + coverage).
export function createGpuMaskPaintSession(kit, screen, geometry) {
  let base = kit.makeTarget(),
    presented = kit.makeTarget(),
    scratch = kit.makeTarget(),
    baseKeys = [],
    live = null,
    disposed = false;
  kit.clearTarget(base);
  kit.clearTarget(presented);
  kit.clearTarget(scratch);

  function screenOp(op) {
    return {
      region: op.region,
      projection: op.digital.projection,
      points: points3(op),
      brush: op.digital.brush,
      seed: op.digital.seed,
      kind: op.digital.operation,
    };
  }
  // Deposit an operation into a target the caller has cleared; mirrors
  // deposit() in maskPaintRaster.js branch for branch.
  function depositInto(target, op, fromDab) {
    if (op.kind === "region") {
      if (!fromDab) kit.fillRegion(target, op.region, greyRgb(op));
      return;
    }
    if (op.digital?.projection) {
      const mark = screen.projectMark(screenOp(op), greyHex(op), false);
      kit.clearTarget(target);
      if (mark) kit.copy(mark.texture, target);
      return;
    }
    if (op.digital) {
      kit.drawDabs(
        target,
        digitalBrushDabsFrom(
          points3(op),
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          greyHex(op),
          false,
          fromDab,
        ),
        op.digital.brush,
        { region: null },
      );
      return;
    }
    kit.drawLegacyDabs(target, legacyMaskDabs(op, fromDab), {
      grey: grey01(op),
      opacity: op.opacity,
      hardness: op.hardness,
      region: op.region,
    });
  }
  // Mirrors composite(): region clip belongs to digital marks, opacity is
  // op.opacity for digital/region ops (legacy scratches baked it in).
  function composite(target, sourceTexture, op) {
    const options = {
      opacity: op.digital || op.kind === "region" ? op.opacity : 1,
    };
    if (op.digital && !op.digital.projection) options.region = op.region;
    if (op.digital?.operation === "erase")
      kit.eraseOver(target, sourceTexture, options);
    else kit.compositeOver(target, sourceTexture, options);
  }
  function depositDirect(target, op, fromPoint) {
    if (op.kind === "region") {
      if (!fromPoint) {
        kit.clearTarget(scratch);
        kit.fillRegion(scratch, op.region, greyRgb(op));
        kit.compositeOver(target, scratch.texture, { opacity: op.opacity });
      }
      return;
    }
    kit.drawLegacyDabs(target, legacyMaskDabs(op, fromPoint), {
      grey: grey01(op),
      opacity: op.opacity,
      hardness: op.hardness,
      region: op.region,
    });
  }
  function foldInto(target, op) {
    if (!op.digital) {
      depositDirect(target, op, 0);
      return;
    }
    kit.clearTarget(scratch);
    depositInto(scratch, op, 0);
    composite(target, scratch.texture, op);
  }
  function presentFull(op) {
    kit.copy(base.texture, presented);
    composite(presented, live.scratch.texture, op);
  }
  function makeLive(op, index) {
    return {
      index,
      key: maskOpKey(op),
      scratch: kit.makeTarget(),
      dabs: null,
      points: null,
      depositedPoints: 0,
    };
  }
  function takeLive(op) {
    kit.clearTarget(live.scratch);
    depositInto(live.scratch, op, 0);
    if (op.digital && !op.digital.projection && op.kind !== "region")
      live.dabs = digitalBrushDabs(
        points3(op),
        op.digital.brush,
        op.digital.seed,
        MASK_TILE_EDGE,
        greyHex(op),
        false,
      );
    live.points = op.points ? op.points.map((p) => p.slice()) : null;
    live.depositedPoints = op.points?.length || 0;
  }
  function rebuild(mask) {
    validateMaskPaintGeometry(mask?.paint, geometry);
    const ops = mask?.paint?.operations || [];
    kit.clearTarget(base);
    kit.clearTarget(presented);
    baseKeys = [];
    if (live) {
      live.scratch.dispose();
      live = null;
    }
    for (const op of ops.slice(0, -1)) {
      foldInto(base, op);
      baseKeys.push(maskOpKey(op));
    }
    kit.copy(base.texture, presented);
    if (ops.length) {
      const op = ops.at(-1);
      live = makeLive(op, ops.length - 1);
      takeLive(op);
      // Region/legacy last ops already sit in the live scratch or deposit
      // directly; digital ops present through their scratch composite.
      if (op.digital || op.kind === "region") presentFull(op);
      else depositDirect(presented, op, 0);
      if (!op.digital) live.depositedPoints = op.points?.length || 0;
    }
    return presented;
  }
  function update(mask) {
    if (disposed) throw new Error("Mask paint GPU session is disposed.");
    const ops = mask?.paint?.operations || [];
    if (!live && baseKeys.length === 0 && ops.length === 0) return presented;
    for (let i = 0; i < baseKeys.length; i++)
      if (i >= ops.length || baseKeys[i] !== maskOpKey(ops[i]))
        return rebuild(mask);
    if (live && live.index >= ops.length) return rebuild(mask);
    while (baseKeys.length < Math.max(0, ops.length - 1)) {
      const index = baseKeys.length,
        op = ops[index];
      if (live && live.index === index && op.digital) {
        composite(base, live.scratch.texture, op);
        live.scratch.dispose();
        live = null;
      } else foldInto(base, op);
      baseKeys.push(maskOpKey(op));
    }
    const op = ops.at(-1);
    if (!op) return presented;
    if (!live || live.index !== ops.length - 1 || live.key !== maskOpKey(op)) {
      // A non-digital operation deposits directly on the presented target,
      // so takeover starts from the folded base (once per operation
      // transition, never per frame) — the CPU session rebuilds through
      // rasterizeMaskPaint for the same reason.
      if (!op.digital && baseKeys.length) return rebuild(mask);
      if (live) live.scratch.dispose();
      live = makeLive(op, ops.length - 1);
      takeLive(op);
      presentFull(op);
      return presented;
    }
    if (op.kind === "region") return presented;
    if (op.digital?.projection) {
      if (live.points?.length === op.points.length) return presented;
      depositInto(live.scratch, op, 0);
      live.points = op.points.map((p) => p.slice());
      presentFull(op);
      return presented;
    }
    if (op.digital) {
      const color = greyHex(op),
        dabs = digitalBrushDabs(
          points3(op),
          op.digital.brush,
          op.digital.seed,
          MASK_TILE_EDGE,
          color,
          false,
        ),
        previous = live.dabs || [];
      let first = 0;
      while (
        first < Math.min(previous.length, dabs.length) &&
        sameMaskDab(previous[first], dabs[first])
      )
        first++;
      if (first === previous.length && first === dabs.length) return presented;
      const rect = maskDabRect(
        dabs.slice(first),
        op.digital.brush,
        previous.slice(first),
      );
      if (!rect || rect.width <= 0 || rect.height <= 0) {
        live.dabs = dabs;
        return presented;
      }
      // Repair the scratch rectangle with the full dab sequence (clipped),
      // mirroring one-shot sequential deposition.
      kit.clearRect(live.scratch, glRect(rect));
      kit.drawDabs(live.scratch, dabs, op.digital.brush, {
        region: null,
        rect: glRect(rect),
      });
      live.dabs = dabs;
      presentFull(op);
      return presented;
    }
    // Legacy brush growth: only new segments deposit, directly on the
    // presented target (points are append-only; segment dabs never move).
    const from = live.depositedPoints || 0;
    if (op.points.length <= from) return presented;
    depositDirect(presented, op, from);
    live.depositedPoints = op.points.length;
    return presented;
  }
  const session = {
    gpu: true,
    update,
    dispose() {
      disposed = true;
      base.dispose();
      presented.dispose();
      scratch.dispose();
      live?.scratch.dispose();
      live = null;
    },
  };
  return session;
}

// GPU twin of createMaskCoverageTexture with the identical uniforms
// contract. A GL failure at any point delegates the rest of the
// controller's life to the CPU chain (uniform values are re-pointed at the
// CPU texture, so installed materials never notice).
export function createMaskCoverageGpu(renderer, geometry) {
  const kit = createPaintGpuKit(renderer, geometry),
    screen = createScreenPaintGpu(renderer, geometry, kit);
  const coverage = kit.makeTarget(WIDTH, HEIGHT, {
      minFilter: THREE.LinearFilter,
      magFilter: THREE.LinearFilter,
    }),
    fieldTarget = kit.makeTarget();
  const uniforms = {
    uPaintedMaskAtlas: { value: coverage.texture },
    uPaintedMaskEnabled: { value: 0 },
  };
  const fieldMat = kit.material(FIELD_FRAG, {
    uStrokes: { value: null },
    uPositions: { value: null },
    uSource: { value: null },
    uKind: { value: 0 },
    uFill: { value: 1 },
    uInverted: { value: 0 },
    uStrength: { value: 100 },
    uEnabled: { value: 1 },
    uTarget: { value: new THREE.Vector3() },
    uTolerance: { value: 0.1 },
    uSoftness: { value: 0.1 },
    uPointCount: { value: 0 },
    uPointA: { value: new Float32Array(48) },
    uPointB: { value: new Float32Array(48) },
    uBackground: { value: new THREE.Vector3() },
  });
  const multiplyBlending = new THREE.CustomBlending();
  multiplyBlending.blendEquation = THREE.AddEquation;
  multiplyBlending.blendSrc = THREE.DstColorFactor;
  multiplyBlending.blendDst = THREE.ZeroFactor;
  const multiplyMat = kit.material(
    MULTIPLY_FRAG,
    { uField: { value: null } },
    multiplyBlending,
  );
  const dummyTexture = new THREE.DataTexture(
    new Float32Array(4),
    1,
    1,
    THREE.RGBAFormat,
    THREE.FloatType,
  );
  dummyTexture.needsUpdate = true;
  kit.track(dummyTexture);
  let positionsTexture = null;
  const sessions = new Map(),
    sourceTextures = new Map();
  let ticket = 0,
    disposed = false,
    cpuFallback = null;

  function ensurePositions() {
    if (positionsTexture) return positionsTexture;
    const { xyz, occupied } = maskGeometryCache(geometry).positions(),
      data = new Float32Array(WIDTH * HEIGHT * 4);
    for (let k = 0; k < WIDTH * HEIGHT; k++) {
      data[k * 4] = xyz[k * 3];
      data[k * 4 + 1] = xyz[k * 3 + 1];
      data[k * 4 + 2] = xyz[k * 3 + 2];
      data[k * 4 + 3] = occupied[k];
    }
    positionsTexture = new THREE.DataTexture(
      data,
      WIDTH,
      HEIGHT,
      THREE.RGBAFormat,
      THREE.FloatType,
    );
    positionsTexture.minFilter = positionsTexture.magFilter =
      THREE.NearestFilter;
    positionsTexture.generateMipmaps = false;
    positionsTexture.needsUpdate = true;
    return positionsTexture;
  }
  async function sourceTexture(layer, doc) {
    const source = maskColourSource(layer, doc);
    if (!source?.dataUrl) return null;
    if (sourceTextures.has(source.dataUrl))
      return sourceTextures.get(source.dataUrl);
    const data = await readMaskSource(source);
    if (!data) return null;
    const texture = new THREE.DataTexture(
      data,
      MASK_TILE_EDGE,
      MASK_TILE_EDGE,
      THREE.RGBAFormat,
    );
    texture.minFilter = texture.magFilter = THREE.NearestFilter;
    texture.generateMipmaps = false;
    texture.needsUpdate = true;
    sourceTextures.set(source.dataUrl, texture);
    return texture;
  }
  function paintField(mask, strokesTexture, source) {
    const u = fieldMat.uniforms;
    u.uStrokes.value = strokesTexture;
    u.uPositions.value =
      mask.kind === "gradient" ? ensurePositions() : dummyTexture;
    u.uSource.value = source ?? dummyTexture;
    u.uKind.value =
      mask.kind === "gradient" ? 1 : mask.kind === "color" ? 2 : 0;
    u.uFill.value = mask.fill;
    u.uInverted.value = mask.inverted ? 1 : 0;
    u.uStrength.value = mask.strength;
    u.uEnabled.value = mask.enabled ? 1 : 0;
    if (mask.kind === "gradient") {
      const packed = packGradientUniforms(mask.gradient);
      u.uPointCount.value = packed.count;
      // Fresh arrays per field: three uploads uniform arrays by reference.
      u.uPointA.value = packed.pointA;
      u.uPointB.value = packed.pointB;
      u.uBackground.value.set(...packed.background);
    }
    if (mask.kind === "color") {
      const [r, g, b] = hexRgb(mask.color);
      u.uTarget.value.set(r, g, b);
      u.uTolerance.value = mask.tolerance;
      u.uSoftness.value = mask.softness;
    }
    kit.draw(fieldTarget, fieldMat);
    multiplyMat.uniforms.uField.value = fieldTarget.texture;
    kit.draw(coverage, multiplyMat);
  }
  async function update(owners, doc) {
    if (cpuFallback) {
      await cpuFallback.update(owners, doc);
      uniforms.uPaintedMaskEnabled.value =
        cpuFallback.uniforms.uPaintedMaskEnabled.value;
      return;
    }
    const job = ++ticket,
      masks = owners.filter((l) => maskHasPaint(l.mask));
    if (!masks.length) {
      uniforms.uPaintedMaskEnabled.value = 0;
      return;
    }
    try {
      await kit.withState(async () => {
        kit.clearTarget(coverage, undefined, [0xffffff, 1]);
        for (const l of masks) {
          let session = sessions.get(l.id);
          if (!session) {
            session = createGpuMaskPaintSession(kit, screen, geometry);
            sessions.set(l.id, session);
          }
          const strokesTexture = session.update(l.mask).texture,
            source =
              l.mask.kind === "color"
                ? await sourceTexture(l, doc)
                : null;
          if (disposed || job !== ticket) return;
          paintField(l.mask, strokesTexture, source);
        }
        for (const id of sessions.keys())
          if (!masks.some((l) => l.id === id)) {
            sessions.get(id).dispose();
            sessions.delete(id);
          }
        if (disposed || job !== ticket) return;
        uniforms.uPaintedMaskEnabled.value = 1;
      });
    } catch (error) {
      if (disposed) return;
      console.warn(
        "Mask GPU update failed; falling back to CPU mask raster.",
        error,
      );
      cpuFallback = createMaskCoverageTexture(geometry);
      uniforms.uPaintedMaskAtlas.value =
        cpuFallback.uniforms.uPaintedMaskAtlas.value;
      await cpuFallback.update(owners, doc);
      uniforms.uPaintedMaskEnabled.value =
        cpuFallback.uniforms.uPaintedMaskEnabled.value;
    }
  }
  return {
    uniforms,
    gpu: true,
    update,
    dispose() {
      disposed = true;
      ticket++;
      cpuFallback?.dispose();
      cpuFallback = null;
      for (const session of sessions.values()) session.dispose();
      sessions.clear();
      for (const texture of sourceTextures.values()) texture.dispose();
      sourceTextures.clear();
      positionsTexture?.dispose();
      coverage.dispose();
      fieldTarget.dispose();
      screen.dispose();
      kit.dispose();
    },
  };
}
