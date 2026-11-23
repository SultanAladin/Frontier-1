import * as THREE from "three";
import {
  TEXTURE_PARTS,
  AUTHORING_UV_SET,
  texturePart,
  surfacePartAtTriangle,
} from "./textureSurfaceModel.js";

const topologyCache = new WeakMap();
const EPS = 1e-8;
export function uvTile(u, v) {
  const x = Math.floor(u),
    y = Math.floor(v);
  return x >= 0 && x < 10 && y >= 0 && y < 100
    ? { u: x, v: y, id: 1001 + x + y * 10 }
    : null;
}
export function uvTopology(geometry, source = false) {
  let bank = topologyCache.get(geometry);
  if (!bank) {
    bank = new Map();
    topologyCache.set(geometry, bank);
  }
  const key = source ? "source" : "authoring";
  if (bank.has(key)) return bank.get(key);
  const uv = source
    ? geometry.attributes.uv
    : geometry.attributes.uvAuthoring || geometry.attributes.uv;
  const triangles = [],
    tiles = new Map(),
    charts = new Map();
  if (!uv) return { triangles, tiles: [], charts: [], overlaps: false };
  const count = geometry.index?.count || geometry.attributes.position.count;
  for (let i = 0; i < count; i += 3) {
    const ids = [0, 1, 2].map((j) =>
        geometry.index ? geometry.index.getX(i + j) : i + j,
      ),
      points = ids.map((id) => [uv.getX(id), uv.getY(id)]),
      min = [0, 1].map((c) => Math.min(...points.map((p) => p[c]))),
      max = [0, 1].map((c) => Math.max(...points.map((p) => p[c]))),
      part = surfacePartAtTriangle(geometry, i / 3),
      chartId = geometry.attributes.surfaceChart?.getX(ids[0]) || 0;
    const triangle = { ids, points, min, max, part, chartId, faceIndex: i / 3 };
    triangles.push(triangle);
    if (chartId) {
      const chart = charts.get(chartId) || {
        id: chartId,
        min: [...min],
        max: [...max],
        part,
      };
      for (let c = 0; c < 2; c++) {
        chart.min[c] = Math.min(chart.min[c], min[c]);
        chart.max[c] = Math.max(chart.max[c], max[c]);
      }
      charts.set(chartId, chart);
    }
    for (let y = Math.floor(min[1] + EPS); y <= Math.floor(max[1] - EPS); y++)
      for (
        let x = Math.floor(min[0] + EPS);
        x <= Math.floor(max[0] - EPS);
        x++
      ) {
        const tile = uvTile(x, y);
        if (tile) {
          const value = tiles.get(tile.id) || { ...tile, triangles: 0 };
          value.triangles++;
          tiles.set(tile.id, value);
        }
      }
  }
  const result = {
    triangles,
    tiles: [...tiles.values()].sort((a, b) => a.id - b.id),
    charts: [...charts.values()],
    overlaps: source && geometry.userData.authoringUVSet === AUTHORING_UV_SET,
  };
  bank.set(key, result);
  return result;
}
export function barycentricUV(point, triangle, tolerance = EPS) {
  const [a, b, c] = triangle.points,
    d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
  if (Math.abs(d) < EPS) return null;
  const x =
      ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) /
      d,
    y =
      ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) /
      d,
    z = 1 - x - y;
  return Math.min(x, y, z) >= -tolerance ? [x, y, z] : null;
}
export function uvSurfaceHit(geometry, u, v, partId = null, source = false) {
  const topology = uvTopology(geometry, source);
  for (const triangle of topology.triangles) {
    if (partId && triangle.part?.id !== partId) continue;
    if (
      u < triangle.min[0] - EPS ||
      u > triangle.max[0] + EPS ||
      v < triangle.min[1] - EPS ||
      v > triangle.max[1] + EPS
    )
      continue;
    const weights = barycentricUV([u, v], triangle);
    if (!weights) continue;
    if (!geometry.boundingSphere) geometry.computeBoundingSphere();
    const position = new THREE.Vector3(),
      normal = new THREE.Vector3();
    triangle.ids.forEach((id, j) => {
      position.addScaledVector(
        new THREE.Vector3().fromBufferAttribute(
          geometry.attributes.position,
          id,
        ),
        weights[j],
      );
      normal.addScaledVector(
        new THREE.Vector3().fromBufferAttribute(geometry.attributes.normal, id),
        weights[j],
      );
    });
    position
      .sub(geometry.boundingSphere.center)
      .divideScalar(geometry.boundingSphere.radius);
    return {
      space: "uv",
      uvSet: AUTHORING_UV_SET,
      uvPosition: [u, v],
      target: triangle.part
        ? { objectId: "teapot", partId: triangle.part.id }
        : null,
      position: position.toArray(),
      quaternion: new THREE.Quaternion()
        .setFromUnitVectors(new THREE.Vector3(0, 0, 1), normal.normalize())
        .toArray(),
      faceIndex: triangle.faceIndex,
      chartId: triangle.chartId,
      tile: uvTile(u, v)?.id,
    };
  }
  return null;
}
export function uvDecalProjector(decal) {
  return new THREE.Matrix4().compose(
    new THREE.Vector3(...decal.uvPosition, 0),
    new THREE.Quaternion().setFromAxisAngle(
      new THREE.Vector3(0, 0, 1),
      (decal.roll * Math.PI) / 180,
    ),
    new THREE.Vector3(decal.size[0], decal.size[1], 1),
  );
}
const mix = (a, b, t) => a.map((n, i) => n + (b[i] - n) * t);
function clip(polygon, axis, sign) {
  const result = [];
  for (let i = 0; i < polygon.length; i++) {
    const a = polygon[i],
      b = polygon[(i + 1) % polygon.length],
      da = a.local[axis] * sign - 0.5,
      db = b.local[axis] * sign - 0.5;
    if (da <= EPS) result.push(a);
    if (da > EPS !== db > EPS) {
      const t = da / (da - db);
      result.push({
        p: mix(a.p, b.p, t),
        n: mix(a.n, b.n, t),
        uv: mix(a.uv, b.uv, t),
        sourceUV: mix(a.sourceUV, b.sourceUV, t),
        local: mix(a.local, b.local, t),
        part: a.part,
      });
    }
  }
  return result;
}
// Clip UV projectors on the actual indexed surface. This creates a real 3D
// overlay, not a whole-mesh colour change or a picture pretending to be UV paint.
// The same clipper retains authoring UVs on object-space decals for the 2D view.
export function projectArtworkGeometry(geometry, decal, localProjector = null) {
  const isUV = decal.space === "uv",
    projector = isUV ? uvDecalProjector(decal) : localProjector,
    inverse = projector.clone().invert(),
    normal = new THREE.Vector3(0, 0, 1).transformDirection(projector),
    target = texturePart(decal.target?.partId),
    positions = [],
    normals = [],
    artUV = [],
    authoringUV = [],
    parts = [],
    source = geometry.attributes.uv,
    uv = geometry.attributes.uvAuthoring || source;
  let box;
  if (isUV) {
    const corners = [
      [-0.5, -0.5],
      [-0.5, 0.5],
      [0.5, -0.5],
      [0.5, 0.5],
    ].map(([x, y]) => new THREE.Vector3(x, y, 0).applyMatrix4(projector));
    box = {
      min: [
        Math.min(...corners.map((p) => p.x)),
        Math.min(...corners.map((p) => p.y)),
      ],
      max: [
        Math.max(...corners.map((p) => p.x)),
        Math.max(...corners.map((p) => p.y)),
      ],
    };
  }
  for (const triangle of uvTopology(geometry).triangles) {
    if (target && triangle.part?.id !== target.id) continue;
    if (
      isUV &&
      (triangle.max[0] < box.min[0] ||
        triangle.min[0] > box.max[0] ||
        triangle.max[1] < box.min[1] ||
        triangle.min[1] > box.max[1])
    )
      continue;
    const faceNormal = new THREE.Vector3();
    let polygon = triangle.ids.map((id) => {
      const p = new THREE.Vector3().fromBufferAttribute(
          geometry.attributes.position,
          id,
        ),
        n = new THREE.Vector3().fromBufferAttribute(
          geometry.attributes.normal,
          id,
        );
      faceNormal.add(n);
      const texture = [uv.getX(id), uv.getY(id)],
        point = (
          isUV ? new THREE.Vector3(texture[0], texture[1], 0) : p.clone()
        ).applyMatrix4(inverse);
      return {
        p: p.toArray(),
        n: n.toArray(),
        uv: texture,
        sourceUV: source ? [source.getX(id), source.getY(id)] : texture,
        local: point.toArray(),
        part: triangle.part?.index || 0,
      };
    });
    if (!isUV && faceNormal.normalize().dot(normal) <= 0.05) continue;
    for (let axis = 0; axis < (isUV ? 2 : 3) && polygon.length; axis++)
      for (const sign of [-1, 1]) polygon = clip(polygon, axis, sign);
    for (let i = 1; i < polygon.length - 1; i++) {
      const tri = [polygon[0], polygon[i], polygon[i + 1]],
        a = new THREE.Vector3()
          .fromArray(tri[1].p)
          .sub(new THREE.Vector3().fromArray(tri[0].p)),
        b = new THREE.Vector3()
          .fromArray(tri[2].p)
          .sub(new THREE.Vector3().fromArray(tri[0].p));
      if (a.cross(b).lengthSq() < 1e-18) continue;
      for (const vertex of tri) {
        positions.push(...vertex.p);
        normals.push(
          ...new THREE.Vector3().fromArray(vertex.n).normalize().toArray(),
        );
        artUV.push(vertex.local[0] + 0.5, vertex.local[1] + 0.5);
        authoringUV.push(...vertex.uv);
        parts.push(vertex.part);
      }
    }
  }
  const result = new THREE.BufferGeometry();
  result.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(positions, 3),
  );
  result.setAttribute("normal", new THREE.Float32BufferAttribute(normals, 3));
  result.setAttribute("uv", new THREE.Float32BufferAttribute(artUV, 2));
  result.setAttribute(
    "uvAuthoring",
    new THREE.Float32BufferAttribute(authoringUV, 2),
  );
  result.setAttribute(
    "surfacePart",
    new THREE.Float32BufferAttribute(parts, 1),
  );
  result.computeBoundingSphere();
  return result;
}
export function surfaceHitDetails(specimen, hit) {
  const part = surfacePartAtTriangle(specimen.geometry, hit.faceIndex);
  if (!part) return {};
  const triangle = uvTopology(specimen.geometry).triangles[hit.faceIndex];
  const local = specimen.worldToLocal(hit.point.clone()),
    bary = new THREE.Vector3();
  new THREE.Triangle(
    ...triangle.ids.map((id) =>
      new THREE.Vector3().fromBufferAttribute(
        specimen.geometry.attributes.position,
        id,
      ),
    ),
  ).getBarycoord(local, bary);
  const weights = bary.toArray(),
    uvPosition = [0, 1].map((c) =>
      triangle.points.reduce((sum, p, i) => sum + p[c] * weights[i], 0),
    );
  return {
    target: { objectId: "teapot", partId: part.id },
    faceIndex: hit.faceIndex,
    chartId: triangle.chartId,
    uvPosition,
    uvSet: AUTHORING_UV_SET,
    tile: uvTile(...uvPosition)?.id,
  };
}

// Mesh boundary contours, including holes, rather than thousands of overlapping
// triangle subpaths. Winding is retained from the indexed UV triangles.
export function uvBoundaryLoops(geometry, source = false) {
  const topology = uvTopology(geometry, source),
    edges = new Map();
  for (const triangle of topology.triangles)
    for (let i = 0; i < 3; i++) {
      const j = (i + 1) % 3,
        a = triangle.ids[i],
        b = triangle.ids[j],
        key = a < b ? `${a}-${b}` : `${b}-${a}`;
      const old = edges.get(key);
      if (old) old.count++;
      else
        edges.set(key, {
          key,
          a,
          b,
          from: triangle.points[i],
          to: triangle.points[j],
          part: triangle.part,
          faceIndex: triangle.faceIndex,
          chartId: triangle.chartId,
          count: 1,
        });
    }
  const pending = new Map([...edges].filter(([, edge]) => edge.count === 1)),
    outgoing = new Map(),
    loops = [];
  for (const edge of pending.values()) {
    const values = outgoing.get(edge.a) || [];
    values.push(edge);
    outgoing.set(edge.a, values);
  }
  while (pending.size) {
    const first = pending.values().next().value,
      points = [first.from];
    let edge = first;
    while (edge && pending.has(edge.key)) {
      pending.delete(edge.key);
      points.push(edge.to);
      if (edge.b === first.a) break;
      edge = outgoing.get(edge.b)?.find((next) => pending.has(next.key));
    }
    if (points.length > 3)
      loops.push({ part: first.part, chartId: first.chartId, points });
  }
  return loops;
}
