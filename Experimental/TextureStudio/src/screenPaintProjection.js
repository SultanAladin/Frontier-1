// A stroke lives in CSS screen pixels. Its frozen projection maps coverage to
// EVERY touched authoring-UV triangle; chart IDs never split or scale the tip.
// Persist the small declarative camera/UV transform, not a screenshot or code.
import * as THREE from "three";
import { uvTopology, barycentricUV } from "./textureUVGeometry.js";
import { digitalBrushDabs, drawDigitalDabs } from "./digitalBrush.js";

export const SCREEN_PROJECTION_VERSION = 1;
const EDGE = 256,
  WIDTH = 1024,
  PIXELS = 1048576;
const banks = new WeakMap();
const finite = (n) => typeof n === "number" && Number.isFinite(n);
export function normalizeScreenProjection(raw) {
  if (
    !raw ||
    raw.version !== SCREEN_PROJECTION_VERSION ||
    !["lit", "uv"].includes(raw.kind) ||
    !Array.isArray(raw.size) ||
    raw.size.length !== 2 ||
    raw.size.some((n) => !finite(n) || n < 1 || n > 16384) ||
    !Array.isArray(raw.matrix) ||
    raw.matrix.length !== (raw.kind === "lit" ? 16 : 6) ||
    raw.matrix.some((n) => !finite(n) || Math.abs(n) > 1e8)
  )
    throw new Error("Invalid bounded screen paint projection.");
  if (
    raw.kind === "uv" &&
    Math.abs(raw.matrix[0] * raw.matrix[3] - raw.matrix[1] * raw.matrix[2]) <
      1e-8
  )
    throw new Error("Screen paint needs an invertible UV view.");
  if (
    raw.kind === "lit" &&
    Math.abs(new THREE.Matrix4().fromArray(raw.matrix).determinant()) < 1e-15
  )
    throw new Error("Screen paint needs an invertible camera projection.");
  return {
    version: SCREEN_PROJECTION_VERSION,
    kind: raw.kind,
    size: raw.size.slice(),
    matrix: raw.matrix.slice(),
  };
}
export function normalizeScreenPoints(raw, projection) {
  if (!Array.isArray(raw) || !raw.length || raw.length > 512)
    throw new Error("Screen strokes need 1–512 samples.");
  return raw.map((p) => {
    if (
      !Array.isArray(p) ||
      p.length !== 3 ||
      !p.every(finite) ||
      p[2] < 0 ||
      p[2] > 1 ||
      p[0] * EDGE < -2048 ||
      p[0] * EDGE > projection.size[0] + 2048 ||
      p[1] * EDGE < -2048 ||
      p[1] * EDGE > projection.size[1] + 2048
    )
      throw new Error("Invalid bounded screen/pressure samples.");
    return p.map((n) => Math.round(n * 1e5) / 1e5);
  });
}
export function litScreenProjection(
  engine,
  rect = engine.renderer.domElement.getBoundingClientRect(),
) {
  engine.camera.updateMatrixWorld();
  engine.specimen.updateWorldMatrix(true, false);
  const matrix = new THREE.Matrix4()
    .multiplyMatrices(
      engine.camera.projectionMatrix,
      engine.camera.matrixWorldInverse,
    )
    .multiply(engine.specimen.matrixWorld);
  return {
    version: SCREEN_PROJECTION_VERSION,
    kind: "lit",
    size: [rect.width, rect.height],
    matrix: matrix.toArray(),
  };
}
export function uvScreenProjection(camera, logicalSize, rect) {
  const sx = rect.width / logicalSize.width,
    sy = rect.height / logicalSize.height;
  return {
    version: SCREEN_PROJECTION_VERSION,
    kind: "uv",
    size: [rect.width, rect.height],
    matrix: [
      camera.scale * sx,
      0,
      0,
      -camera.scale * sy,
      camera.x * sx,
      camera.y * sy,
    ],
  };
}
export function screenPointer(event, projection, rect) {
  return {
    clientX: event.clientX,
    clientY: event.clientY,
    pointerId: event.pointerId,
    pointerType: event.pointerType,
    pressure: event.pressure,
    buttons: event.buttons,
    isPrimary: event.isPrimary,
    altKey: event.altKey,
    ctrlKey: event.ctrlKey,
    metaKey: event.metaKey,
    projection,
    screenPosition: [event.clientX - rect.left, event.clientY - rect.top],
  };
}
export function screenScopeContains(region, t) {
  if (!region || !t?.part) return false;
  if (region.kind === "surface") return true;
  return (
    t.part.id === region.partId &&
    (region.kind === "object" ||
      (region.kind === "tile" && t.part.tile === region.id) ||
      (region.kind === "island" && t.chartId === region.id) ||
      (region.kind === "face" && t.faceIndex === region.id))
  );
}
function bankOf(geometry) {
  let b = banks.get(geometry);
  if (!b) {
    b = { maps: new Map(), rasters: new Map(), texels: null };
    banks.set(geometry, b);
  }
  return b;
}
function remember(map, key, value, max) {
  map.set(key, value);
  if (map.size > max) map.delete(map.keys().next().value);
  return value;
}
function project(p, projection, uv = null) {
  const m = projection.matrix;
  if (projection.kind === "uv")
    return [
      m[0] * uv[0] + m[2] * uv[1] + m[4],
      m[1] * uv[0] + m[3] * uv[1] + m[5],
      0,
    ];
  const [x, y, z] = p,
    w = m[3] * x + m[7] * y + m[11] * z + m[15];
  if (w <= 1e-8) return null;
  const nz = (m[2] * x + m[6] * y + m[10] * z + m[14]) / w;
  if (nz < -1 || nz > 1) return null;
  return [
    (((m[0] * x + m[4] * y + m[8] * z + m[12]) / w) * 0.5 + 0.5) *
      projection.size[0],
    (0.5 - ((m[1] * x + m[5] * y + m[9] * z + m[13]) / w) * 0.5) *
      projection.size[1],
    nz,
  ];
}
// Clip in homogeneous space, just like the GPU. A triangle crossing the near
// plane is not discarded; its still-visible polygon participates in depth.
function clipTriangle(positions, projection) {
  const m = projection.matrix;
  let poly = positions.map(([x, y, z]) => [
    m[0] * x + m[4] * y + m[8] * z + m[12],
    m[1] * x + m[5] * y + m[9] * z + m[13],
    m[2] * x + m[6] * y + m[10] * z + m[14],
    m[3] * x + m[7] * y + m[11] * z + m[15],
  ]);
  for (const distance of [
    (p) => p[3] - 1e-8,
    (p) => p[0] + p[3],
    (p) => p[3] - p[0],
    (p) => p[1] + p[3],
    (p) => p[3] - p[1],
    (p) => p[2] + p[3],
    (p) => p[3] - p[2],
  ]) {
    const next = [];
    for (let j = 0; j < poly.length; j++) {
      const a = poly[j],
        b = poly[(j + 1) % poly.length],
        da = distance(a),
        db = distance(b);
      if (da >= 0) next.push(a);
      if (da >= 0 !== db >= 0) {
        const t = da / (da - db);
        next.push(a.map((v, c) => v + (b[c] - v) * t));
      }
    }
    poly = next;
    if (poly.length < 3) return [];
  }
  const screen = poly.map(([x, y, z, w]) => [
    ((x / w) * 0.5 + 0.5) * projection.size[0],
    (0.5 - (y / w) * 0.5) * projection.size[1],
    z / w,
  ]);
  return screen
    .slice(1, -1)
    .map((_, i) => [screen[0], screen[i + 1], screen[i + 2]]);
}
function triangleDepth(v, x, y) {
  const [a, c, d] = v,
    det = (c[1] - d[1]) * (a[0] - d[0]) + (d[0] - c[0]) * (a[1] - d[1]);
  if (Math.abs(det) < 1e-10) return Infinity;
  const wa = ((c[1] - d[1]) * (x - d[0]) + (d[0] - c[0]) * (y - d[1])) / det,
    wc = ((d[1] - a[1]) * (x - d[0]) + (a[0] - d[0]) * (y - d[1])) / det,
    wd = 1 - wa - wc;
  return Math.min(wa, wc, wd) < -1e-7
    ? Infinity
    : wa * a[2] + wc * c[2] + wd * d[2];
}
// Retain all mappings, including genuinely overlapping UVs. One atlas address
// necessarily affects all surfaces sharing it, but a hidden mapping cannot
// overwrite a visible mapping's coverage.
function texels(geometry, b) {
  if (b.texels) return b.texels;
  const position = geometry.attributes.position,
    indices = [],
    faces = [],
    xyz = [],
    uv = [],
    occupied = new Uint8Array(WIDTH * EDGE);
  for (const t of uvTopology(geometry).triangles) {
    const vertices = t.ids.map((i) => [
      position.getX(i),
      position.getY(i),
      position.getZ(i),
    ]);
    const x0 = Math.max(0, Math.floor(t.min[0] * EDGE)),
      x1 = Math.min(WIDTH - 1, Math.ceil(t.max[0] * EDGE)),
      y0 = Math.max(0, Math.floor((1 - t.max[1]) * EDGE)),
      y1 = Math.min(EDGE - 1, Math.ceil((1 - t.min[1]) * EDGE));
    for (let y = y0; y <= y1; y++)
      for (let x = x0; x <= x1; x++) {
        const u = (x + 0.5) / EDGE,
          v = 1 - (y + 0.5) / EDGE,
          w = barycentricUV([u, v], t);
        if (!w) continue;
        if (indices.length >= PIXELS)
          throw new Error(
            "The screen paint surface exceeds its texel-mapping budget.",
          );
        indices.push(y * WIDTH + x);
        occupied[y * WIDTH + x] = 1;
        faces.push(t.faceIndex);
        uv.push(u, v);
        for (let c = 0; c < 3; c++)
          xyz.push(vertices.reduce((sum, p, j) => sum + p[c] * w[j], 0));
      }
  }
  return (b.texels = {
    indices: Uint32Array.from(indices),
    faces: Uint32Array.from(faces),
    xyz: Float32Array.from(xyz),
    uv: Float32Array.from(uv),
    occupied,
  });
}
function projectedMap(geometry, projection, region) {
  const b = bankOf(geometry),
    key = JSON.stringify([projection, region]);
  if (b.maps.has(key)) return b.maps.get(key);
  const topology = uvTopology(geometry),
    position = geometry.attributes.position,
    [width, height] = projection.size;
  const factor = Math.min(
      1,
      2048 / Math.max(width, height),
      Math.sqrt(PIXELS / (width * height)),
    ),
    dw = Math.max(1, Math.floor(width * factor)),
    dh = Math.max(1, Math.floor(height * factor)),
    sx = dw / width,
    sy = dh / height;
  let depth = null,
    front = null,
    projectedFaces = null;
  if (projection.kind === "lit") {
    depth = new Float32Array(dw * dh).fill(Infinity);
    front = new Int32Array(dw * dh).fill(-1);
    projectedFaces = [];
    // Include all parts in visibility, even when only a selected face may write.
    for (const t of topology.triangles) {
      const triangles = clipTriangle(
        t.ids.map((id) => [
          position.getX(id),
          position.getY(id),
          position.getZ(id),
        ]),
        projection,
      ).map((v) => v.map((p) => [p[0] * sx, p[1] * sy, p[2]]));
      projectedFaces[t.faceIndex] = triangles;
      for (const v of triangles) {
        const x0 = Math.max(0, Math.floor(Math.min(...v.map((p) => p[0])))),
          x1 = Math.min(dw - 1, Math.ceil(Math.max(...v.map((p) => p[0])))),
          y0 = Math.max(0, Math.floor(Math.min(...v.map((p) => p[1])))),
          y1 = Math.min(dh - 1, Math.ceil(Math.max(...v.map((p) => p[1]))));
        for (let y = y0; y <= y1; y++)
          for (let x = x0; x <= x1; x++) {
            const z = triangleDepth(v, x + 0.5, y + 0.5),
              k = y * dw + x;
            if (z < depth[k]) {
              depth[k] = z;
              front[k] = t.faceIndex;
            }
          }
      }
    }
  }
  const lookup = texels(geometry, b),
    indices = [],
    xy = [];
  for (let k = 0; k < lookup.indices.length; k++) {
    const t = topology.triangles[lookup.faces[k]];
    if (!screenScopeContains(region, t)) continue;
    const p = project(
      lookup.xyz.subarray(k * 3, k * 3 + 3),
      projection,
      lookup.uv.subarray(k * 2, k * 2 + 2),
    );
    if (!p || p[0] < 0 || p[0] >= width || p[1] < 0 || p[1] >= height) continue;
    if (depth) {
      const px = p[0] * sx,
        py = p[1] * sy,
        cx = Math.floor(px),
        cy = Math.floor(py);
      // Test the front pixel candidates at this exact texel's projected XY,
      // not a slope-tolerant nearby depth. Parallel, closely separated faces
      // must not paint through each other. Neighbours cover triangle edges.
      let surfaceDepth = Infinity;
      for (const v of projectedFaces[t.faceIndex])
        surfaceDepth = Math.min(surfaceDepth, triangleDepth(v, px, py));
      if (!Number.isFinite(surfaceDepth)) surfaceDepth = p[2];
      let nearest = surfaceDepth;
      const seen = new Set();
      for (let y = Math.max(0, cy - 1); y <= Math.min(dh - 1, cy + 1); y++)
        for (let x = Math.max(0, cx - 1); x <= Math.min(dw - 1, cx + 1); x++) {
          const face = front[y * dw + x];
          if (face < 0 || seen.has(face)) continue;
          seen.add(face);
          for (const v of projectedFaces[face])
            nearest = Math.min(nearest, triangleDepth(v, px, py));
        }
      if (surfaceDepth > nearest + 1e-9) continue;
    }
    indices.push(lookup.indices[k]);
    xy.push(p[0], p[1]);
  }
  return remember(
    b.maps,
    key,
    { indices: Uint32Array.from(indices), xy: Float32Array.from(xy) },
    3,
  );
}
function canvas(w, h) {
  const c = document.createElement("canvas");
  c.width = w;
  c.height = h;
  // Projection transfer canvases are putImageData/getImageData bound.
  c.getContext("2d", { willReadFrequently: true });
  return c;
}
function stencil(op, color, dynamics) {
  const dabs = digitalBrushDabs(
      op.points,
      op.brush,
      op.seed,
      EDGE,
      color,
      dynamics,
    ),
    [w, h] = op.projection.size;
  if (!dabs.length) return null;
  const ext = (d) => Math.hypot(d.radius, d.radius * op.brush.roundness),
    x0 = Math.max(
      0,
      Math.floor(Math.min(...dabs.map((d) => d.x - ext(d))) - 1),
    ),
    y0 = Math.max(
      0,
      Math.floor(Math.min(...dabs.map((d) => d.y - ext(d))) - 1),
    ),
    x1 = Math.min(w, Math.ceil(Math.max(...dabs.map((d) => d.x + ext(d))) + 1)),
    y1 = Math.min(h, Math.ceil(Math.max(...dabs.map((d) => d.y + ext(d))) + 1));
  if (x1 <= x0 || y1 <= y0) return null;
  const factor = Math.min(
      1,
      2048 / Math.max(x1 - x0, y1 - y0),
      Math.sqrt(PIXELS / ((x1 - x0) * (y1 - y0))),
    ),
    image = canvas(
      Math.max(1, Math.floor((x1 - x0) * factor)),
      Math.max(1, Math.floor((y1 - y0) * factor)),
    ),
    ctx = image.getContext("2d"),
    sx = image.width / (x1 - x0),
    sy = image.height / (y1 - y0);
  ctx.setTransform(sx, 0, 0, sy, -x0 * sx, -y0 * sy);
  drawDigitalDabs(ctx, dabs, op.brush);
  return {
    bytes: ctx.getImageData(0, 0, image.width, image.height).data,
    width: image.width,
    height: image.height,
    x0,
    y0,
    sx,
    sy,
  };
}
export function projectScreenOperation(
  op,
  geometry,
  color = op.brush.color,
  dynamics = true,
) {
  const b = bankOf(geometry),
    key = JSON.stringify([op, color, dynamics]);
  if (b.rasters.has(key)) return b.rasters.get(key);
  const out = canvas(WIDTH, EDGE),
    ctx = out.getContext("2d"),
    img = ctx.createImageData(WIDTH, EDGE),
    s = stencil(op, color, dynamics);
  if (s) {
    const map = projectedMap(geometry, op.projection, op.region);
    for (let k = 0; k < map.indices.length; k++) {
      const x = (map.xy[k * 2] - s.x0) * s.sx - 0.5,
        y = (map.xy[k * 2 + 1] - s.y0) * s.sy - 0.5,
        ix = Math.floor(x),
        iy = Math.floor(y),
        fx = x - ix,
        fy = y - iy;
      if (ix < -1 || iy < -1 || ix >= s.width || iy >= s.height) continue;
      const rgba = [0, 0, 0, 0];
      for (let dy = 0; dy < 2; dy++)
        for (let dx = 0; dx < 2; dx++) {
          const px = ix + dx,
            py = iy + dy;
          if (px < 0 || py < 0 || px >= s.width || py >= s.height) continue;
          const at = (py * s.width + px) * 4,
            weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy),
            a = s.bytes[at + 3] * weight;
          rgba[3] += a;
          for (let c = 0; c < 3; c++) rgba[c] += s.bytes[at + c] * a;
        }
      const dest = map.indices[k] * 4;
      if (rgba[3] <= img.data[dest + 3] || !rgba[3]) continue;
      for (let c = 0; c < 3; c++) img.data[dest + c] = rgba[c] / rgba[3];
      img.data[dest + 3] = rgba[3];
    }
  }
  // Linear UV sampling reaches half a texel outside each chart. Extrude one
  // covered boundary texel into EMPTY gutters, never into another face/chart
  // or excluded part. Otherwise a fully painted screen circle gets a white
  // hairline at every packed chart seam. No dilation inside surface domains.
  const occupied = b.texels?.occupied;
  if (occupied)
    for (let at = 0; at < occupied.length; at++) {
      const src = at * 4,
        alpha = img.data[src + 3];
      if (!occupied[at] || !alpha) continue;
      const x = at % WIDTH,
        y = Math.floor(at / WIDTH);
      for (let dy = -1; dy <= 1; dy++)
        for (let dx = -1; dx <= 1; dx++) {
          const nx = x + dx,
            ny = y + dy;
          if (nx < 0 || nx >= WIDTH || ny < 0 || ny >= EDGE) continue;
          const k = ny * WIDTH + nx,
            dest = k * 4;
          if (occupied[k] || img.data[dest + 3] >= alpha) continue;
          img.data.set(img.data.subarray(src, src + 4), dest);
        }
    }
  ctx.putImageData(img, 0, 0);
  return remember(b.rasters, key, out, 8);
}
// The cached per-texel mapping table (atlas index, face, barycentric object
// position, authoring uv, occupied flags), shared with the GPU transfer in
// screenPaintGpu.js so both paths project the identical layout.
export function screenPaintTexelTable(geometry) {
  return texels(geometry, bankOf(geometry));
}
export function screenOperationHasCoverage(op, geometry) {
  const c = projectScreenOperation(op, geometry, "#ffffff", false),
    data = c.getContext("2d").getImageData(0, 0, WIDTH, EDGE).data;
  for (let i = 3; i < data.length; i += 4) if (data[i]) return true;
  return false;
}
