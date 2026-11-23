import * as THREE from "three";
import { digitalBrushDabs } from "./digitalBrush.js";
import { uvTopology } from "./textureUVGeometry.js";
import { TEXTURE_PARTS } from "./textureSurfaceModel.js";
import {
  screenPaintTexelTable,
  SCREEN_PROJECTION_VERSION,
} from "./screenPaintProjection.js";
import { MASK_ATLAS_WIDTH, MASK_TILE_EDGE } from "./maskPaintRaster.js";

// GPU counterpart of projectScreenOperation (screenPaintProjection.js):
// the per-texel layout is uploaded once as data textures, a frozen camera
// renders depth + front-face targets once per projection, each stroke's
// dabs land in a screen-space stencil target through the shared dab
// pipeline, and a transfer pass performs scope test, projection, depth
// acceptance, bilinear stencil sampling and one-texel gutter dilation —
// per stroke, no CPU loops over texels or pixels anywhere.
//
// Deliberate divergences from the CPU reference (which stays available as
// the fallback): the layout keeps ONE mapping per atlas texel (the last in
// table order) where the CPU retains every overlapping mapping and lets
// the highest stencil alpha win; and depth acceptance tests the texel's
// own projected depth against hardware-rasterized depths at neighbouring
// pixel centres instead of analytic per-face depths at the exact XY. Both
// matter only for genuinely overlapping charts and near-parallel touching
// faces; typical screen strokes produce identical coverage.

const WIDTH = MASK_ATLAS_WIDTH,
  HEIGHT = MASK_TILE_EDGE,
  EDGE = MASK_TILE_EDGE,
  PIXELS = 1048576,
  DEPTH_EPSILON = 2e-4;

// Stencil sizing, mirroring stencil() in screenPaintProjection.js exactly:
// dab extents bound the bitmap, which is capped at 2048px / 1M pixels.
// Pure and exported for the Node checks in scripts/verify-mask-gpu.mjs.
export function screenStencilLayout(dabs, brush, size) {
  const [w, h] = size;
  if (!dabs.length) return null;
  const ext = (d) => Math.hypot(d.radius, d.radius * brush.roundness),
    x0 = Math.max(0, Math.floor(Math.min(...dabs.map((d) => d.x - ext(d))) - 1)),
    y0 = Math.max(0, Math.floor(Math.min(...dabs.map((d) => d.y - ext(d))) - 1)),
    x1 = Math.min(w, Math.ceil(Math.max(...dabs.map((d) => d.x + ext(d))) + 1)),
    y1 = Math.min(h, Math.ceil(Math.max(...dabs.map((d) => d.y + ext(d))) + 1));
  if (x1 <= x0 || y1 <= y0) return null;
  const factor = Math.min(
      1,
      2048 / Math.max(x1 - x0, y1 - y0),
      Math.sqrt(PIXELS / ((x1 - x0) * (y1 - y0))),
    ),
    width = Math.max(1, Math.floor((x1 - x0) * factor)),
    height = Math.max(1, Math.floor((y1 - y0) * factor));
  return {
    x0,
    y0,
    x1,
    y1,
    width,
    height,
    sx: width / (x1 - x0),
    sy: height / (y1 - y0),
  };
}

const PACK_DEPTH_GLSL = /* glsl */ `
vec4 packDepth01(float d) {
  vec4 enc = fract(vec4(1.0, 255.0, 65025.0, 16581375.0) * d);
  enc -= enc.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
  return enc;
}
float unpackDepth01(vec4 enc) {
  return dot(enc, vec4(1.0, 1.0 / 255.0, 1.0 / 65025.0, 1.0 / 16581375.0));
}`;

const TRANSFER_FRAG = /* glsl */ `
uniform sampler2D uTexelA;
uniform sampler2D uTexelB;
uniform sampler2D uStencil;
uniform sampler2D uDepth;
uniform sampler2D uFace;
uniform float uProjKind;
uniform mat4 uProjMat;
uniform mat3 uUvMat;
uniform vec2 uProjSize;
uniform vec2 uDepthSize;
uniform vec4 uStencilRect;
uniform float uScopeKind;
uniform float uScopePart;
uniform float uScopeId;
varying vec2 vUv;
${PACK_DEPTH_GLSL}
float decodeFace(vec4 c) {
  return floor(c.r * 255.0 + 0.5) * 256.0 + floor(c.g * 255.0 + 0.5);
}
void main() {
  if (uScopeKind < -0.5) discard;
  vec2 tableUv = vec2(vUv.x, 1.0 - vUv.y);
  vec4 a = texture2D(uTexelA, tableUv);
  if (a.w < -0.5) discard;
  vec4 b = texture2D(uTexelB, tableUv);
  if (b.x < -0.5) discard;
  if (uScopeKind > 0.5) {
    if (b.x != uScopePart) discard;
    if (uScopeKind > 1.5 && uScopeKind < 2.5 && 1001.0 + b.x != uScopeId) discard;
    if (uScopeKind > 2.5 && uScopeKind < 3.5 && b.y != uScopeId) discard;
    if (uScopeKind > 3.5 && a.w != uScopeId) discard;
  }
  vec2 screen;
  float pz = 0.0;
  if (uProjKind < 0.5) {
    screen = (uUvMat * vec3(b.z, b.w, 1.0)).xy;
  } else {
    vec4 clip = uProjMat * vec4(a.xyz, 1.0);
    if (clip.w <= 1e-8) discard;
    vec3 ndc = clip.xyz / clip.w;
    if (ndc.z < -1.0 || ndc.z > 1.0) discard;
    screen = vec2(
      (ndc.x * 0.5 + 0.5) * uProjSize.x,
      (0.5 - ndc.y * 0.5) * uProjSize.y
    );
    pz = ndc.z;
  }
  if (screen.x < 0.0 || screen.x >= uProjSize.x ||
      screen.y < 0.0 || screen.y >= uProjSize.y)
    discard;
  if (uProjKind > 0.5) {
    vec2 centre = screen / uProjSize;
    vec2 step = 1.0 / uDepthSize;
    float nearest = 2.0;
    for (int j = -1; j <= 1; j++)
      for (int i = -1; i <= 1; i++) {
        float d = unpackDepth01(
          texture2D(uDepth, centre + vec2(float(i), float(j)) * step)
        ) * 2.0 - 1.0;
        nearest = min(nearest, d);
      }
    float frontFace = decodeFace(texture2D(uFace, centre));
    if (frontFace != a.w && pz > nearest + ${DEPTH_EPSILON}) discard;
  }
  vec2 sUv = (screen - uStencilRect.xy) / uStencilRect.zw;
  if (sUv.x < 0.0 || sUv.x > 1.0 || sUv.y < 0.0 || sUv.y > 1.0) discard;
  sUv.y = 1.0 - sUv.y;
  vec4 st = texture2D(uStencil, sUv);
  if (st.a <= 0.0) discard;
  gl_FragColor = vec4(st.rgb, st.a);
}`;

const DILATE_FRAG = /* glsl */ `
uniform sampler2D uSource;
uniform sampler2D uTexelA;
varying vec2 vUv;
void main() {
  vec4 own = texture2D(uTexelA, vec2(vUv.x, 1.0 - vUv.y));
  vec4 centre = texture2D(uSource, vUv);
  if (own.w > -0.5) {
    gl_FragColor = centre;
    return;
  }
  vec4 best = vec4(0.0);
  vec2 step = vec2(1.0 / ${WIDTH}.0, 1.0 / ${HEIGHT}.0);
  for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++) {
      if (i == 0 && j == 0) continue;
      vec2 at = vUv + vec2(float(i), float(j)) * step;
      vec4 n = texture2D(uTexelA, vec2(at.x, 1.0 - at.y));
      if (n.w < -0.5) continue;
      vec4 value = texture2D(uSource, at);
      if (value.a > best.a) best = value;
    }
  gl_FragColor = best;
}`;

const DEPTH_VERT = /* glsl */ `
in vec3 position;
in float aFace;
uniform mat4 uProjMat;
out float vFace;
void main() {
  vFace = aFace;
  gl_Position = uProjMat * vec4(position, 1.0);
}`;
const DEPTH_FRAG = /* glsl */ `
precision highp float;
precision highp int;
layout(location = 0) out vec4 oDepth;
layout(location = 1) out vec4 oFace;
in float vFace;
vec4 packDepth01(float d) {
  vec4 enc = fract(vec4(1.0, 255.0, 65025.0, 16581375.0) * d);
  enc -= enc.yzww * vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
  return enc;
}
void main() {
  oDepth = packDepth01(gl_FragCoord.z);
  float hi = floor(vFace / 256.0);
  oFace = vec4(hi / 255.0, (vFace - hi * 256.0) / 255.0, 0.0, 1.0);
}`;

const SCOPE_KINDS = { surface: 0, object: 1, tile: 2, island: 3, face: 4 };

export function createScreenPaintGpu(renderer, geometry, kit) {
  let texelA = null,
    texelB = null,
    depthScene = null,
    depthMesh = null,
    disposed = false;
  const depthCache = new Map(),
    markCache = new Map(),
    coverageCache = new Map();
  const transferMat = kit.material(TRANSFER_FRAG, {
    uTexelA: { value: null },
    uTexelB: { value: null },
    uStencil: { value: null },
    uDepth: { value: null },
    uFace: { value: null },
    uProjKind: { value: 0 },
    uProjMat: { value: new THREE.Matrix4() },
    uUvMat: { value: new THREE.Matrix3() },
    uProjSize: { value: new THREE.Vector2(1, 1) },
    uDepthSize: { value: new THREE.Vector2(1, 1) },
    uStencilRect: { value: new THREE.Vector4() },
    uScopeKind: { value: 0 },
    uScopePart: { value: -1 },
    uScopeId: { value: 0 },
  });
  const dilateMat = kit.material(DILATE_FRAG, {
    uSource: { value: null },
    uTexelA: { value: null },
  });
  const readback = new Uint8Array(WIDTH * HEIGHT * 4);

  function ensureTexelTextures() {
    if (texelA) return;
    const table = screenPaintTexelTable(geometry),
      topology = uvTopology(geometry),
      dataA = new Float32Array(WIDTH * HEIGHT * 4).fill(-1),
      dataB = new Float32Array(WIDTH * HEIGHT * 4).fill(-1);
    for (let k = 0; k < table.indices.length; k++) {
      const at = table.indices[k],
        face = table.faces[k],
        t = topology.triangles[face];
      dataA[at * 4] = table.xyz[k * 3];
      dataA[at * 4 + 1] = table.xyz[k * 3 + 1];
      dataA[at * 4 + 2] = table.xyz[k * 3 + 2];
      dataA[at * 4 + 3] = face;
      dataB[at * 4] = t?.part
        ? TEXTURE_PARTS.findIndex((p) => p.id === t.part.id)
        : -1;
      dataB[at * 4 + 1] = t?.chartId ?? 0;
      dataB[at * 4 + 2] = table.uv[k * 2];
      dataB[at * 4 + 3] = table.uv[k * 2 + 1];
    }
    const make = (data) => {
      const texture = new THREE.DataTexture(
        data,
        WIDTH,
        HEIGHT,
        THREE.RGBAFormat,
        THREE.FloatType,
      );
      texture.minFilter = texture.magFilter = THREE.NearestFilter;
      texture.generateMipmaps = false;
      texture.needsUpdate = true;
      return texture;
    };
    texelA = make(dataA);
    texelB = make(dataB);
  }

  function ensureDepth(projection) {
    const key = JSON.stringify(projection);
    if (depthCache.has(key)) return depthCache.get(key);
    const [width, height] = projection.size,
      factor = Math.min(
        1,
        2048 / Math.max(width, height),
        Math.sqrt(PIXELS / (width * height)),
      ),
      dw = Math.max(1, Math.floor(width * factor)),
      dh = Math.max(1, Math.floor(height * factor));
    if (!depthScene) {
      const triangles = uvTopology(geometry).triangles;
      if (triangles.length > 65534)
        throw new Error("Screen paint surface exceeds the GPU face budget.");
      const position = geometry.attributes.position,
        positions = new Float32Array(triangles.length * 9),
        faces = new Float32Array(triangles.length * 3);
      triangles.forEach((t, i) => {
        t.ids.forEach((id, j) => {
          positions.set(
            [position.getX(id), position.getY(id), position.getZ(id)],
            (i * 3 + j) * 3,
          );
          faces[i * 3 + j] = t.faceIndex;
        });
      });
      const depthGeometry = new THREE.BufferGeometry();
      depthGeometry.setAttribute(
        "position",
        new THREE.BufferAttribute(positions, 3),
      );
      depthGeometry.setAttribute("aFace", new THREE.BufferAttribute(faces, 1));
      const depthMaterial = new THREE.RawShaderMaterial({
        vertexShader: DEPTH_VERT,
        fragmentShader: DEPTH_FRAG,
        glslVersion: THREE.GLSL3,
        uniforms: { uProjMat: { value: new THREE.Matrix4() } },
        side: THREE.DoubleSide,
      });
      depthMesh = new THREE.Mesh(depthGeometry, depthMaterial);
      depthMesh.frustumCulled = false;
      depthScene = new THREE.Scene();
      depthScene.add(depthMesh);
      kit.track(depthGeometry);
      kit.track(depthMaterial);
    }
    const mrt = new THREE.WebGLRenderTarget(dw, dh, {
      count: 2,
      depthBuffer: true,
      stencilBuffer: false,
      minFilter: THREE.NearestFilter,
      magFilter: THREE.NearestFilter,
    });
    mrt.textures.forEach((t) => (t.generateMipmaps = false));
    depthMesh.material.uniforms.uProjMat.value.fromArray(projection.matrix);
    const previousTarget = renderer.getRenderTarget(),
      previousClearColor = renderer.getClearColor(new THREE.Color()),
      previousClearAlpha = renderer.getClearAlpha(),
      previousAutoClear = renderer.autoClear;
    try {
      renderer.autoClear = false;
      renderer.setRenderTarget(mrt);
      // White reads back as depth > 1 (farthest) and face 65535 (none).
      renderer.setClearColor(0xffffff, 1);
      renderer.clear(true, true, false);
      renderer.render(depthScene, new THREE.Camera());
    } finally {
      renderer.setRenderTarget(previousTarget);
      renderer.setClearColor(previousClearColor, previousClearAlpha);
      renderer.autoClear = previousAutoClear;
    }
    const entry = { mrt, dw, dh };
    depthCache.set(key, entry);
    if (depthCache.size > 3) {
      const [oldKey, old] = depthCache.entries().next().value;
      depthCache.delete(oldKey);
      old.mrt.dispose();
    }
    return entry;
  }

  function rememberMark(key, value) {
    markCache.set(key, value);
    if (markCache.size > 8) {
      const [oldKey, old] = markCache.entries().next().value;
      markCache.delete(oldKey);
      old?.dispose();
    }
    return value;
  }

  // Premultiplied sRGB mark in atlas orientation, or null when the stroke
  // deposits nothing. The returned target is cache-owned: copy it, never
  // draw into it or dispose it.
  function projectMark(op, color, dynamics) {
    if (disposed) throw new Error("Screen paint GPU is disposed.");
    if (op.projection?.version !== SCREEN_PROJECTION_VERSION)
      throw new Error("Unsupported screen projection for GPU transfer.");
    const key = JSON.stringify([op, color, dynamics]);
    if (markCache.has(key)) return markCache.get(key);
    const dabs = digitalBrushDabs(
      op.points,
      op.brush,
      op.seed,
      EDGE,
      color,
      dynamics,
    );
    const layout = screenStencilLayout(dabs, op.brush, op.projection.size);
    if (!layout) return rememberMark(key, null);
    ensureTexelTextures();
    const stencil = kit.makeTarget(layout.width, layout.height, {
      minFilter: THREE.LinearFilter,
      magFilter: THREE.LinearFilter,
    });
    const mark = kit.makeTarget();
    const dilated = kit.makeTarget();
    try {
      kit.clearTarget(stencil);
      kit.drawDabs(stencil, dabs, op.brush, { screen: layout });
      const u = transferMat.uniforms,
        region = op.region ?? null;
      u.uTexelA.value = texelA;
      u.uTexelB.value = texelB;
      u.uStencil.value = stencil.texture;
      u.uProjKind.value = op.projection.kind === "lit" ? 1 : 0;
      u.uProjSize.value.set(op.projection.size[0], op.projection.size[1]);
      u.uStencilRect.value.set(
        layout.x0,
        layout.y0,
        layout.x1 - layout.x0,
        layout.y1 - layout.y0,
      );
      u.uScopeKind.value = region
        ? (SCOPE_KINDS[region.kind] ?? -1)
        : -1;
      u.uScopePart.value = region
        ? TEXTURE_PARTS.findIndex((p) => p.id === region.partId)
        : -1;
      u.uScopeId.value = region?.id ?? 0;
      if (op.projection.kind === "lit") {
        const depth = ensureDepth(op.projection);
        u.uDepth.value = depth.mrt.textures[0];
        u.uFace.value = depth.mrt.textures[1];
        u.uDepthSize.value.set(depth.dw, depth.dh);
        u.uProjMat.value.fromArray(op.projection.matrix);
      } else {
        const m = op.projection.matrix;
        u.uUvMat.value.set(m[0], m[2], m[4], m[1], m[3], m[5], 0, 0, 1);
      }
      kit.clearTarget(mark);
      kit.draw(mark, transferMat);
      dilateMat.uniforms.uSource.value = mark.texture;
      dilateMat.uniforms.uTexelA.value = texelA;
      kit.clearTarget(dilated);
      kit.draw(dilated, dilateMat);
    } finally {
      stencil.dispose();
      mark.dispose();
    }
    return rememberMark(key, dilated);
  }

  function hasCoverage(op) {
    const key = JSON.stringify(op);
    if (coverageCache.has(key)) return coverageCache.get(key);
    const mark = projectMark(op, "#ffffff", false);
    let covered = false;
    if (mark) {
      renderer.readRenderTargetPixels(mark, 0, 0, WIDTH, HEIGHT, readback);
      for (let i = 3; i < readback.length; i += 4)
        if (readback[i]) {
          covered = true;
          break;
        }
    }
    coverageCache.set(key, covered);
    if (coverageCache.size > 16) coverageCache.delete(coverageCache.keys().next().value);
    return covered;
  }

  return {
    projectMark,
    hasCoverage,
    dispose() {
      disposed = true;
      for (const target of markCache.values()) target?.dispose();
      markCache.clear();
      for (const entry of depthCache.values()) entry.mrt.dispose();
      depthCache.clear();
      coverageCache.clear();
      texelA?.dispose();
      texelB?.dispose();
      texelA = texelB = null;
    },
  };
}
