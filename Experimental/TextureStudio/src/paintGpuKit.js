import * as THREE from "three";
import { normalizeDigitalBrush } from "./digitalBrush.js";
import {
  maskGeometryCache,
  MASK_ATLAS_WIDTH,
  MASK_TILE_EDGE,
} from "./maskPaintRaster.js";

// Shared WebGL2 plumbing for atlas-space painting, extracted verbatim from
// the channel session in digitalPaintGpu.js so mask strokes, screen
// projection and stroke thumbnails deposit through the exact same instanced
// dab pipeline instead of parallel reimplementations. Scratch targets hold
// premultiplied sRGB exactly like the CPU sessions' scratch canvases;
// linearisation happens once, at composite time.

export const PAINT_ATLAS_WIDTH = MASK_ATLAS_WIDTH;
export const PAINT_ATLAS_HEIGHT = MASK_TILE_EDGE;

// Instance payload for one dab batch: dab0 carries centre/radius/flow
// alpha, dab1 carries rotation and the dab colour. Exported for the Node
// packing checks in scripts/verify-gpu-packing.mjs.
export function packDabInstances(dabs) {
  const dab0 = new Float32Array(dabs.length * 4),
    dab1 = new Float32Array(dabs.length * 4);
  dabs.forEach((dab, i) => {
    dab0[i * 4] = dab.x;
    dab0[i * 4 + 1] = dab.y;
    dab0[i * 4 + 2] = dab.radius;
    dab0[i * 4 + 3] = dab.opacity;
    const [r, g, b] = hexRgb(dab.color);
    dab1[i * 4] = dab.angle ?? 0;
    dab1[i * 4 + 1] = r;
    dab1[i * 4 + 2] = g;
    dab1[i * 4 + 3] = b;
  });
  return { dab0, dab1, count: dabs.length };
}

export const hexRgb = (hex) => {
  const n = parseInt(String(hex ?? "#ffffff").slice(1), 16) || 0;
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
};

export const FULLSCREEN_VERT = /* glsl */ `
varying vec2 vUv;
void main() {
  vUv = position.xy * 0.5 + 0.5;
  gl_Position = vec4(position.xy, 0.0, 1.0);
}`;
const COPY_FRAG = /* glsl */ `
uniform sampler2D uSource;
varying vec2 vUv;
void main() {
  gl_FragColor = texture2D(uSource, vUv);
}`;
// Mirrors packAtlasRect: un-premultiply, linearize colour channels once
// (uColour), scale by stroke opacity applied exactly once. uColour = 0
// keeps premultiplied sRGB values (scalar slices, mask grey, thumbnails).
// Optional region clip and bottom-up repair rectangle mirror the CPU
// sessions' clipped composites.
const COMPOSITE_FRAG = /* glsl */ `
uniform sampler2D uSource;
uniform float uOpacity;
uniform float uColour;
uniform float uClipEnabled;
uniform sampler2D uClip;
uniform vec4 uRect;
uniform float uRectEnabled;
varying vec2 vUv;
vec3 srgbToLinear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}
void main() {
  vec2 frag = gl_FragCoord.xy;
  if (uRectEnabled > 0.5 &&
      (frag.x < uRect.x || frag.y < uRect.y || frag.x >= uRect.x + uRect.z || frag.y >= uRect.y + uRect.w))
    discard;
  if (uClipEnabled > 0.5 && texture2D(uClip, vec2(vUv.x, 1.0 - vUv.y)).r < 0.5)
    discard;
  vec4 s = texture2D(uSource, vUv);
  float a = clamp(s.a * uOpacity, 0.0, 1.0);
  vec3 rgb = vec3(0.0);
  if (a > 0.0 && s.a > 0.0) {
    vec3 straight = s.rgb / s.a;
    rgb = (uColour > 0.5 ? srgbToLinear(straight) : straight) * a;
  }
  gl_FragColor = vec4(rgb, a);
}`;
const FILL_FRAG = /* glsl */ `
uniform sampler2D uClip;
uniform vec3 uColor;
varying vec2 vUv;
void main() {
  if (texture2D(uClip, vec2(vUv.x, 1.0 - vUv.y)).r < 0.5) discard;
  gl_FragColor = vec4(uColor, 1.0);
}`;
const CLEAR_RECT_FRAG = /* glsl */ `
uniform vec4 uRect;
void main() {
  vec2 p = gl_FragCoord.xy;
  if (p.x < uRect.x || p.y < uRect.y || p.x >= uRect.x + uRect.z || p.y >= uRect.y + uRect.w)
    discard;
  gl_FragColor = vec4(0.0);
}`;
const DAB_VERT = /* glsl */ `
attribute vec4 aDab0;
attribute vec4 aDab1;
uniform vec2 uSize;
uniform float uMode;
uniform vec4 uScreen;
varying vec2 vRel;
varying vec4 vDab0;
varying vec4 vDab1;
void main() {
  vDab0 = aDab0;
  vDab1 = aDab1;
  vRel = position.xy;
  vec2 px = uMode > 0.5
    ? (aDab0.xy - uScreen.xy) * uScreen.zw + position.xy * aDab0.z * 1.42 * uScreen.zw
    : aDab0.xy + position.xy * aDab0.z * 1.42;
  gl_Position = uMode > 0.5
    ? vec4(px.x / uSize.x * 2.0 - 1.0, 1.0 - px.y / uSize.y * 2.0, 0.0, 1.0)
    : vec4(px / uSize * 2.0 - 1.0, 0.0, 1.0);
}`;
// Analytic form of digitalBrushTip + drawDigitalDabs: the tip bitmap is
// evaluated at (texel + 0.5 - 32) / 31, i.e. local/radius * 32/31, with the
// smoothstepped hardness falloff and the seeded grain of the CPU tip.
const DAB_FRAG = /* glsl */ `
uniform sampler2D uClip;
uniform float uClipEnabled;
uniform vec4 uRect;
uniform float uRectEnabled;
uniform float uHardness;
uniform float uRoundness;
uniform float uSquare;
uniform float uGrainMode;
uniform float uGrainAmount;
uniform float uGrainScale;
uniform float uPixel;
varying vec2 vRel;
varying vec4 vDab0;
varying vec4 vDab1;
float grainNoise(vec2 cell) {
  uint seed = (uint(cell.x) + 17u) * 92837111u ^ (uint(cell.y) + 5u) * 689287499u;
  uint state = seed + 0x6D2B79F5u;
  uint x = (state ^ (state >> 15u)) * (state | 1u);
  x = x ^ (x + ((x ^ (x >> 7u)) * (x | 61u)));
  return float(x ^ (x >> 14u)) / 4294967296.0;
}
void main() {
  vec2 frag = gl_FragCoord.xy;
  if (uRectEnabled > 0.5 &&
      (frag.x < uRect.x || frag.y < uRect.y || frag.x >= uRect.x + uRect.z || frag.y >= uRect.y + uRect.w))
    discard;
  if (uClipEnabled > 0.5 && texture2D(uClip, vec2(frag.x / 1024.0, 1.0 - frag.y / 256.0)).r < 0.5) discard;
  float radius = vDab0.z;
  vec2 offset = vRel * radius * 1.42;
  float c = cos(vDab1.x), s = sin(vDab1.x);
  vec2 rp = mat2(c, s, -s, c) * offset;
  vec2 local = vec2(rp.x, -rp.y / max(uRoundness, 0.001)) / radius;
  vec2 tipUv = local * 0.5 + 0.5;
  vec2 tip;
  vec2 grainCell;
  if (uPixel > 0.5) {
    vec2 texel = floor(tipUv * 64.0);
    tip = (texel + 0.5 - 32.0) / 31.0;
    grainCell = floor(texel / uGrainScale);
  } else {
    tip = local * (32.0 / 31.0);
    grainCell = floor(floor(tipUv * 64.0) / uGrainScale);
  }
  float r = uSquare > 0.5 ? max(abs(tip.x), abs(tip.y)) : length(tip);
  float fall = r >= 1.0
    ? 0.0
    : uHardness >= 1.0
      ? 1.0
      : clamp(1.0 - (r - uHardness) / (1.0 - uHardness), 0.0, 1.0);
  float a = fall * fall * (3.0 - 2.0 * fall);
  float grain = 1.0;
  if (uGrainMode > 0.5 && uGrainMode < 1.5) grain = grainNoise(max(grainCell, vec2(0.0)));
  else if (uGrainMode >= 1.5 && uGrainMode < 2.5) grain = mod(grainCell.x, 2.0) > 0.5 ? 0.25 : 1.0;
  else if (uGrainMode >= 2.5) grain = mod(grainCell.x + grainCell.y, 2.0) > 0.5 ? 0.2 : 1.0;
  a *= 1.0 - uGrainAmount + uGrainAmount * grain;
  a *= vDab0.w;
  if (a <= 0.0) discard;
  gl_FragColor = vec4(vDab1.yzw * a, a);
}`;
const LEGACY_DAB_VERT = /* glsl */ `
attribute vec4 aDab0;
uniform vec2 uSize;
varying vec2 vRel;
void main() {
  vRel = position.xy;
  vec2 px = aDab0.xy + position.xy * aDab0.z;
  gl_Position = vec4(px / uSize * 2.0 - 1.0, 0.0, 1.0);
}`;
// Canvas radial-gradient dab of rasterizeMaskPaint: full ink out to
// min(hardness, 0.999), then a linear ramp to the rim. Opacity is baked
// into every dab, exactly like the CPU gradient stops.
const LEGACY_DAB_FRAG = /* glsl */ `
uniform sampler2D uClip;
uniform float uGrey;
uniform float uOpacity;
uniform float uHardness;
varying vec2 vRel;
void main() {
  vec2 frag = gl_FragCoord.xy;
  if (texture2D(uClip, vec2(frag.x / 1024.0, 1.0 - frag.y / 256.0)).r < 0.5) discard;
  float r = length(vRel);
  float hs = min(uHardness, 0.999);
  float tip = r >= 1.0 ? 0.0 : r <= hs ? 1.0 : 1.0 - (r - hs) / (1.0 - hs);
  float a = uOpacity * tip;
  if (a <= 0.0) discard;
  gl_FragColor = vec4(vec3(uGrey * a), a);
}`;

const overBlending = () => {
  const blending = new THREE.CustomBlending();
  blending.blendEquation = THREE.AddEquation;
  blending.blendSrc = THREE.OneFactor;
  blending.blendDst = THREE.OneMinusSrcAlphaFactor;
  return blending;
};

/** renderer: WebGL2 renderer. geometry: specimen geometry (region paths). */
export function createPaintGpuKit(renderer, geometry) {
  if (!renderer?.capabilities?.isWebGL2)
    throw new Error("GPU painting needs a WebGL2 renderer.");
  const cache = maskGeometryCache(geometry);
  const quad = new THREE.PlaneGeometry(2, 2);
  const scene = new THREE.Scene();
  const camera = new THREE.Camera();
  const mesh = new THREE.Mesh(quad, null);
  mesh.frustumCulled = false;
  scene.add(mesh);
  const tracked = new Set();
  const clipTextures = new Map();

  function shaderMaterial(frag, vert, uniforms, blending = null) {
    const material = new THREE.ShaderMaterial({
      vertexShader: vert,
      fragmentShader: frag,
      uniforms,
      depthTest: false,
      depthWrite: false,
      blending: blending ?? THREE.NoBlending,
    });
    tracked.add(material);
    return material;
  }
  // Fullscreen-quad material owned by the kit (disposed with it).
  const material = (frag, uniforms, blending = null) =>
    shaderMaterial(frag, FULLSCREEN_VERT, uniforms, blending);

  const copyMat = material(COPY_FRAG, { uSource: { value: null } });
  const compositeUniforms = () => ({
    uSource: { value: null },
    uOpacity: { value: 1 },
    uColour: { value: 0 },
    uClipEnabled: { value: 0 },
    uClip: { value: null },
    uRect: { value: new THREE.Vector4() },
    uRectEnabled: { value: 0 },
  });
  const compositeMat = material(COMPOSITE_FRAG, compositeUniforms(), overBlending());
  const eraseMat = material(COMPOSITE_FRAG, compositeUniforms(), overBlending());
  eraseMat.blendSrc = THREE.ZeroFactor;
  const fillMat = material(FILL_FRAG, {
    uClip: { value: null },
    uColor: { value: new THREE.Vector3() },
  });
  const clearRectMat = material(CLEAR_RECT_FRAG, {
    uRect: { value: new THREE.Vector4() },
  });
  const dabMat = shaderMaterial(DAB_FRAG, DAB_VERT, {
    uClip: { value: null },
    uClipEnabled: { value: 1 },
    uRect: { value: new THREE.Vector4() },
    uRectEnabled: { value: 0 },
    uHardness: { value: 0 },
    uRoundness: { value: 1 },
    uSquare: { value: 0 },
    uGrainMode: { value: 0 },
    uGrainAmount: { value: 0 },
    uGrainScale: { value: 4 },
    uPixel: { value: 0 },
    uSize: { value: new THREE.Vector2(PAINT_ATLAS_WIDTH, PAINT_ATLAS_HEIGHT) },
    uMode: { value: 0 },
    uScreen: { value: new THREE.Vector4(0, 0, 1, 1) },
  }, overBlending());
  const legacyDabMat = shaderMaterial(LEGACY_DAB_FRAG, LEGACY_DAB_VERT, {
    uClip: { value: null },
    uGrey: { value: 0 },
    uOpacity: { value: 1 },
    uHardness: { value: 0 },
    uSize: { value: new THREE.Vector2(PAINT_ATLAS_WIDTH, PAINT_ATLAS_HEIGHT) },
  }, overBlending());

  function makeTarget(w = PAINT_ATLAS_WIDTH, h = PAINT_ATLAS_HEIGHT, options = {}) {
    return new THREE.WebGLRenderTarget(w, h, {
      depthBuffer: false,
      stencilBuffer: false,
      minFilter: THREE.NearestFilter,
      magFilter: THREE.NearestFilter,
      ...options,
    });
  }
  function makeArrayTarget(slices) {
    const target = new THREE.WebGLArrayRenderTarget(
      PAINT_ATLAS_WIDTH,
      PAINT_ATLAS_HEIGHT,
      slices,
      {
        depthBuffer: false,
        stencilBuffer: false,
        minFilter: THREE.LinearFilter,
        magFilter: THREE.LinearFilter,
      },
    );
    target.texture.generateMipmaps = false;
    return target;
  }
  function clipTexture(region) {
    const key = JSON.stringify(region);
    if (!clipTextures.has(key)) {
      const canvas = document.createElement("canvas");
      canvas.width = PAINT_ATLAS_WIDTH;
      canvas.height = PAINT_ATLAS_HEIGHT;
      const ctx = canvas.getContext("2d");
      ctx.setTransform(
        PAINT_ATLAS_HEIGHT,
        0,
        0,
        -PAINT_ATLAS_HEIGHT,
        0,
        PAINT_ATLAS_HEIGHT,
      );
      ctx.fillStyle = "#ffffff";
      ctx.fill(cache.path(region));
      const texture = new THREE.CanvasTexture(canvas);
      texture.flipY = false;
      texture.colorSpace = THREE.NoColorSpace;
      texture.minFilter = texture.magFilter = THREE.NearestFilter;
      texture.generateMipmaps = false;
      clipTextures.set(key, texture);
    }
    return clipTextures.get(key);
  }
  function draw(target, mat, geometryOverride = null, layer = undefined) {
    mesh.material = mat;
    mesh.geometry = geometryOverride || quad;
    if (layer === undefined) renderer.setRenderTarget(target);
    else renderer.setRenderTarget(target, layer);
    renderer.render(scene, camera);
  }
  function clearTarget(target, layer = undefined, color = null) {
    if (layer === undefined) renderer.setRenderTarget(target);
    else renderer.setRenderTarget(target, layer);
    if (color) renderer.setClearColor(color[0], color[1] ?? 1);
    else renderer.setClearColor(0x000000, 0);
    renderer.clear(true, false, false);
  }
  function copy(sourceTexture, target, layer = undefined) {
    copyMat.uniforms.uSource.value = sourceTexture;
    draw(target, copyMat, null, layer);
  }
  // rect is in GL (bottom-up) rows, matching gl_FragCoord.
  function setComposite(mat, sourceTexture, options) {
    mat.uniforms.uSource.value = sourceTexture;
    mat.uniforms.uOpacity.value = options.opacity ?? 1;
    mat.uniforms.uColour.value = options.colour ? 1 : 0;
    mat.uniforms.uClipEnabled.value = options.region !== undefined ? 1 : 0;
    if (options.region !== undefined)
      mat.uniforms.uClip.value = clipTexture(options.region);
    mat.uniforms.uRectEnabled.value = options.rect ? 1 : 0;
    if (options.rect)
      mat.uniforms.uRect.value.set(
        options.rect.x,
        options.rect.y,
        options.rect.width,
        options.rect.height,
      );
  }
  function compositeOver(target, sourceTexture, options = {}, layer = undefined) {
    setComposite(compositeMat, sourceTexture, options);
    draw(target, compositeMat, null, layer);
  }
  function eraseOver(target, sourceTexture, options = {}, layer = undefined) {
    setComposite(eraseMat, sourceTexture, options);
    draw(target, eraseMat, null, layer);
  }
  function fillRegion(target, region, rgb) {
    fillMat.uniforms.uClip.value = clipTexture(region);
    fillMat.uniforms.uColor.value.set(rgb[0], rgb[1], rgb[2]);
    draw(target, fillMat);
  }
  function clearRect(target, rect) {
    clearRectMat.uniforms.uRect.value.set(rect.x, rect.y, rect.width, rect.height);
    draw(target, clearRectMat);
  }
  // options: { region } for the atlas clip (undefined = no clip test),
  // { rect } GL-rows repair rect (atlas mode), or { screen: layout } to
  // deposit screen-space dabs into a stencil target (no clip, no rect).
  function drawDabs(target, dabs, brushSettings, options = {}) {
    if (!dabs.length) return;
    const brush = normalizeDigitalBrush(brushSettings),
      packed = packDabInstances(dabs);
    const dabGeometry = new THREE.InstancedBufferGeometry();
    dabGeometry.index = quad.index;
    dabGeometry.setAttribute("position", quad.getAttribute("position").clone());
    dabGeometry.setAttribute("aDab0", new THREE.InstancedBufferAttribute(packed.dab0, 4));
    dabGeometry.setAttribute("aDab1", new THREE.InstancedBufferAttribute(packed.dab1, 4));
    dabGeometry.instanceCount = packed.count;
    const u = dabMat.uniforms;
    u.uSize.value.set(target.width, target.height);
    if (options.screen) {
      u.uMode.value = 1;
      u.uScreen.value.set(
        options.screen.x0,
        options.screen.y0,
        options.screen.sx,
        options.screen.sy,
      );
      u.uClipEnabled.value = 0;
      u.uRectEnabled.value = 0;
    } else {
      u.uMode.value = 0;
      u.uClipEnabled.value = options.region === null ? 0 : 1;
      if (options.region !== null) u.uClip.value = clipTexture(options.region);
      u.uRectEnabled.value = options.rect ? 1 : 0;
      if (options.rect)
        u.uRect.value.set(
          options.rect.x,
          options.rect.y,
          options.rect.width,
          options.rect.height,
        );
    }
    u.uHardness.value = brush.hardness;
    u.uRoundness.value = brush.roundness;
    u.uSquare.value = brush.shape === "square" ? 1 : 0;
    u.uGrainMode.value = ["none", "noise", "lines", "checker"].indexOf(brush.grain);
    u.uGrainAmount.value = brush.grainAmount;
    u.uGrainScale.value = brush.grainScale;
    u.uPixel.value = brush.preset === "pixel" ? 1 : 0;
    try {
      draw(target, dabMat, dabGeometry);
    } finally {
      dabGeometry.dispose();
    }
  }
  // dabs: [{x, y, radius}] in atlas texel space (dab frame: x right, y up).
  function drawLegacyDabs(target, dabs, options) {
    if (!dabs.length) return;
    const packed = new Float32Array(dabs.length * 4);
    dabs.forEach((dab, i) => {
      packed[i * 4] = dab.x;
      packed[i * 4 + 1] = dab.y;
      packed[i * 4 + 2] = dab.radius;
    });
    const dabGeometry = new THREE.InstancedBufferGeometry();
    dabGeometry.index = quad.index;
    dabGeometry.setAttribute("position", quad.getAttribute("position").clone());
    dabGeometry.setAttribute("aDab0", new THREE.InstancedBufferAttribute(packed, 4));
    dabGeometry.instanceCount = dabs.length;
    legacyDabMat.uniforms.uClip.value = clipTexture(options.region);
    legacyDabMat.uniforms.uGrey.value = options.grey;
    legacyDabMat.uniforms.uOpacity.value = options.opacity;
    legacyDabMat.uniforms.uHardness.value = options.hardness;
    try {
      draw(target, legacyDabMat, dabGeometry);
    } finally {
      dabGeometry.dispose();
    }
  }
  function withState(fn) {
    const previousTarget = renderer.getRenderTarget(),
      previousClearColor = renderer.getClearColor(new THREE.Color()),
      previousClearAlpha = renderer.getClearAlpha(),
      previousAutoClear = renderer.autoClear;
    const restore = () => {
      renderer.setRenderTarget(previousTarget);
      renderer.setClearColor(previousClearColor, previousClearAlpha);
      renderer.autoClear = previousAutoClear;
    };
    try {
      renderer.autoClear = false;
      const result = fn();
      // Async callers (mask coverage awaits source decodes between draws)
      // keep the saved state until their draws are done.
      if (result && typeof result.then === "function")
        return result.finally(restore);
      restore();
      return result;
    } catch (error) {
      restore();
      throw error;
    }
  }

  return {
    renderer,
    geometry,
    material,
    withState,
    draw,
    clearTarget,
    makeTarget,
    makeArrayTarget,
    copy,
    compositeOver,
    eraseOver,
    fillRegion,
    clearRect,
    clipTexture,
    drawDabs,
    drawLegacyDabs,
    track: (resource) => tracked.add(resource),
    dispose() {
      for (const texture of clipTextures.values()) texture.dispose();
      clipTextures.clear();
      for (const resource of tracked) resource.dispose?.();
      tracked.clear();
      quad.dispose();
      scene.remove(mesh);
    },
  };
}
