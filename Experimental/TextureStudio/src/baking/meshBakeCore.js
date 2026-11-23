import * as THREE from "three";
import { MeshBVH } from "three-mesh-bvh";
import { uvIslandColor } from "./meshMaps.js";
import {
  MESH_MAPS,
  validateBakeGraph,
  reachableBakeNodes,
} from "./bakeGraph.js";
import { requiredBakeMaps, evaluateBakeGraph } from "./bakeGraphEvaluation.js";
const clamp = (v, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, v));
const tick = () => new Promise((r) => setTimeout(r, 0));
const vector = (a, i) =>
  new THREE.Vector3(a[i * 3], a[i * 3 + 1], a[i * 3 + 2]);
const uv = (a, i) => new THREE.Vector2(a[i * 2], a[i * 2 + 1]);
const field = (type, size) => ({ type, data: new Float32Array(size * 4) });
function write(map, i, values, alpha = 1) {
  if (!map) return;
  const v = Array.isArray(values) ? values : [values, values, values];
  for (let c = 0; c < 3; c++)
    map.data[i * 4 + c] = Number.isFinite(v[c]) ? v[c] : 0;
  map.data[i * 4 + 3] = alpha;
}

export function validateMeshBakeSettings(input = {}) {
  if (!input || typeof input !== "object" || Array.isArray(input))
    throw new Error("Choose valid mesh bake settings.");
  if (input.channels != null && !Array.isArray(input.channels))
    throw new Error("Mesh map selection must be a list.");
  const channels = [
    ...new Set(
      (input.channels || ["normal", "occlusion", "curvature", "height"]).filter(
        (k) => Object.hasOwn(MESH_MAPS, k),
      ),
    ),
  ];
  if (!channels.length) throw new Error("Select at least one mesh map.");
  const finite = (v, d, min, max) =>
    Number.isFinite(Number(v)) ? clamp(Number(v), min, max) : d;
  const resolution = [64, 128, 256, 512, 1024].includes(
    Number(input.resolution),
  )
    ? Number(input.resolution)
    : 256;
  return {
    resolution,
    channels,
    front: finite(input.front, 0.15, 0.0001, 100),
    back: finite(input.back, 0.15, 0.0001, 100),
    padding: Math.round(finite(input.padding, 4, 0, 32)),
    samples: Math.round(finite(input.samples, 16, 4, 64)),
    aoDistance: finite(input.aoDistance, 0.35, 0.0001, 100),
    thicknessRange: finite(input.thicknessRange, 2.5, 0.0001, 1000),
    curvatureRadius: finite(input.curvatureRadius, 0.03, 0.00001, 10),
    secondaryAoDistance: finite(input.secondaryAoDistance, 1, 0.0001, 100),
    bevelRadius: finite(input.bevelRadius, 0.025, 0, 10),
    bevelSamples: Math.round(finite(input.bevelSamples, 8, 4, 16)),
    bevelStrength: finite(input.bevelStrength, 1, 0, 1),
    maskStrength: finite(input.maskStrength, 3, 0, 16),
    dustAxis: ["+X", "-X", "+Y", "-Y", "+Z", "-Z"].includes(input.dustAxis)
      ? input.dustAxis
      : "+Y",
    dustFalloff: finite(input.dustFalloff, 2, 0.1, 16),
    wireWidth: finite(input.wireWidth, 1, 0.25, 8),
    normalY: input.normalY === "-Y" ? "-Y" : "+Y",
    projection: input.projection === "nearest" ? "nearest" : "ray",
    matchByName: input.matchByName === true,
    nearestFallback: input.nearestFallback !== false,
  };
}
function uvCharts(mesh) {
  const triangles = mesh.indices.length / 3,
    parent = Int32Array.from({ length: triangles }, (_, i) => i),
    edges = new Map();
  const find = (i) => {
    while (parent[i] !== i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  const key = (i) =>
    `${Math.round(mesh.positions[i * 3] * 1e6)},${Math.round(mesh.positions[i * 3 + 1] * 1e6)},${Math.round(mesh.positions[i * 3 + 2] * 1e6)}:${Math.round(mesh.uvs[i * 2] * 1e6)},${Math.round(mesh.uvs[i * 2 + 1] * 1e6)}`;
  for (let t = 0; t < triangles; t++) {
    const ids = [
      mesh.indices[t * 3],
      mesh.indices[t * 3 + 1],
      mesh.indices[t * 3 + 2],
    ];
    if (mesh.chartIds) {
      parent[t] = -Math.round(mesh.chartIds[ids[0]] || 1);
      continue;
    }
    const keys = ids.map(key);
    for (let e = 0; e < 3; e++) {
      const a = keys[e],
        b = keys[(e + 1) % 3],
        k = a < b ? `${a}|${b}` : `${b}|${a}`;
      if (edges.has(k)) parent[find(t)] = find(edges.get(k));
      else edges.set(k, t);
    }
  }
  const groups = new Map(),
    out = new Uint32Array(triangles);
  for (let t = 0; t < triangles; t++) {
    const root = mesh.chartIds ? parent[t] : find(t);
    if (!groups.has(root)) groups.set(root, groups.size + 1);
    out[t] = groups.get(root);
  }
  return { ids: out, count: groups.size };
}
export function rasterizeLowMesh(mesh, resolution) {
  if (!mesh.uvs || mesh.uvs.length !== (mesh.positions.length / 3) * 2)
    throw new Error("The low mesh requires valid UV0 coordinates.");
  const triangles = mesh.indices.length / 3;
  if (triangles > 150000)
    throw new Error(
      "Low UV targets are limited to 150,000 triangles per object. Decimate the low mesh before baking.",
    );
  for (const v of mesh.uvs)
    if (!Number.isFinite(v) || v < -0.00001 || v > 1.00001)
      throw new Error(
        "Low UVs must lie in one non-overlapping 0–1 tile. UDIM/out-of-tile UVs are not supported.",
      );
  const size = resolution * resolution,
    coverage = new Uint8Array(size),
    charts = new Uint32Array(size),
    triangleAt = new Int32Array(size).fill(-1),
    barycentric = new Float32Array(size * 3),
    world = new Float32Array(size * 3);
  const chartInfo = uvCharts(mesh);
  let covered = 0,
    overlap = 0,
    degenerate = 0;
  const lowBounds = new THREE.Box3();
  for (let i = 0; i < mesh.positions.length / 3; i++)
    lowBounds.expandByPoint(vector(mesh.positions, i));
  const overlapTolerance =
    Math.max(lowBounds.getSize(new THREE.Vector3()).length() * 1e-6, 1e-6) ** 2;
  for (let t = 0; t < triangles; t++) {
    const ids = [
        mesh.indices[t * 3],
        mesh.indices[t * 3 + 1],
        mesh.indices[t * 3 + 2],
      ],
      U = ids.map((i) => uv(mesh.uvs, i)),
      P = ids.map((i) => vector(mesh.positions, i));
    const a = U[0],
      b = U[1],
      c = U[2],
      den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (
      Math.abs(den) < 1e-12 ||
      new THREE.Vector3()
        .subVectors(P[1], P[0])
        .cross(new THREE.Vector3().subVectors(P[2], P[0]))
        .lengthSq() < 1e-18
    ) {
      degenerate++;
      continue;
    }
    const minX = clamp(
        Math.ceil(Math.min(a.x, b.x, c.x) * resolution - 0.5),
        0,
        resolution - 1,
      ),
      maxX = clamp(
        Math.floor(Math.max(a.x, b.x, c.x) * resolution - 0.5),
        0,
        resolution - 1,
      );
    const minY = clamp(
        Math.ceil((1 - Math.max(a.y, b.y, c.y)) * resolution - 0.5),
        0,
        resolution - 1,
      ),
      maxY = clamp(
        Math.floor((1 - Math.min(a.y, b.y, c.y)) * resolution - 0.5),
        0,
        resolution - 1,
      );
    for (let y = minY; y <= maxY; y++)
      for (let x = minX; x <= maxX; x++) {
        const u = (x + 0.5) / resolution,
          v = 1 - (y + 0.5) / resolution,
          A = ((b.y - c.y) * (u - c.x) + (c.x - b.x) * (v - c.y)) / den,
          B = ((c.y - a.y) * (u - c.x) + (a.x - c.x) * (v - c.y)) / den,
          C = 1 - A - B;
        if (Math.min(A, B, C) < -1e-7) continue;
        const i = y * resolution + x,
          w = [A, B, C],
          point = P[0]
            .clone()
            .multiplyScalar(A)
            .addScaledVector(P[1], B)
            .addScaledVector(P[2], C);
        if (coverage[i]) {
          const previous = vector(world, i);
          if (previous.distanceToSquared(point) > overlapTolerance) overlap++;
          continue;
        }
        coverage[i] = 1;
        charts[i] = chartInfo.ids[t];
        triangleAt[i] = t;
        for (let k = 0; k < 3; k++) {
          barycentric[i * 3 + k] = w[k];
          world[i * 3 + k] = point.getComponent(k);
        }
        covered++;
      }
  }
  if (!covered)
    throw new Error(
      "The low UV layout covers no texels at this resolution. Check its UVs/triangle areas.",
    );
  if (overlap)
    throw new Error(
      `Overlapping low UVs detected (${overlap.toLocaleString()} conflicting texel samples). Unwrap the object, or bake separate objects as separate texture sets.`,
    );
  return {
    coverage,
    charts,
    triangleAt,
    barycentric,
    world,
    covered,
    chartCount: chartInfo.count,
    degenerate,
  };
}
function highGeometry(data) {
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.BufferAttribute(data.positions, 3),
  );
  geometry.setAttribute("normal", new THREE.BufferAttribute(data.normals, 3));
  geometry.setIndex(new THREE.BufferAttribute(data.indices, 1));
  if (data.colors)
    geometry.setAttribute("color", new THREE.BufferAttribute(data.colors, 3));
  if (data.partIds)
    geometry.setAttribute(
      "bakePartId",
      new THREE.BufferAttribute(data.partIds, 1),
    );
  geometry.computeBoundingBox();
  const bvh = new MeshBVH(geometry, { targetLeafSize: 8, verbose: false });
  return { geometry, bvh };
}
function highSample(geometry, faceIndex, point) {
  const index = geometry.index.array,
    ids = [
      index[faceIndex * 3],
      index[faceIndex * 3 + 1],
      index[faceIndex * 3 + 2],
    ],
    pos = geometry.attributes.position,
    normals = geometry.attributes.normal;
  const P = ids.map((i) => new THREE.Vector3().fromBufferAttribute(pos, i)),
    weights =
      THREE.Triangle.getBarycoord(point, ...P, new THREE.Vector3()) ||
      new THREE.Vector3(1, 0, 0);
  const normal = new THREE.Vector3();
  ids.forEach((id, i) =>
    normal.addScaledVector(
      new THREE.Vector3().fromBufferAttribute(normals, id),
      weights.getComponent(i),
    ),
  );
  if (normal.lengthSq() < 1e-12) new THREE.Triangle(...P).getNormal(normal);
  normal.normalize();
  const color = geometry.attributes.color,
    colors = color
      ? [0, 1, 2].map((c) =>
          ids.reduce(
            (sum, id, i) =>
              sum + color.getComponent(id, c) * weights.getComponent(i),
            0,
          ),
        )
      : [1, 1, 1];
  return {
    normal,
    color: colors,
    partId: Math.round(geometry.attributes.bakePartId?.getX(ids[0]) || 0),
  };
}
// A normal bake must not project onto the back of another surface merely
// because that face is closest to the cage origin. Attribute normals are used
// rather than triangle winding, so mirrored imported transforms remain valid.
function facingProjection(bvh, geometry, ray, normal, maximum, cageOffset) {
  const hits = bvh.raycast(ray, THREE.DoubleSide, 0, maximum);
  let rejected = 0,
    selected = null,
    closest = Infinity;
  for (const hit of hits) {
    if (
      highSample(geometry, hit.faceIndex, hit.point).normal.dot(normal) < -1e-6
    ) {
      rejected++;
      continue;
    }
    // Choose the eligible intersection nearest the original low surface, not
    // the cage origin. A long cage otherwise captures a neighbouring lid,
    // handle or overlapping shell before the surface that actually matches.
    const distance = Math.abs(hit.distance - cageOffset);
    if (distance < closest) {
      closest = distance;
      selected = hit;
    }
  }
  return { hit: selected, rejected };
}
function closestFacingPoint(bvh, geometry, point, normal, radius) {
  const closest = bvh.closestPointToPoint(point, {}, 0, radius);
  if (
    closest &&
    closest.distance <= radius &&
    highSample(geometry, closest.faceIndex, closest.point).normal.dot(normal) >=
      -1e-6
  )
    return closest;
  let best = null,
    distance = radius;
  const target = new THREE.Vector3();
  bvh.shapecast({
    boundsTraverseOrder: (box) => box.distanceToPoint(point),
    intersectsBounds: (box) => box.distanceToPoint(point) <= distance,
    intersectsTriangle: (triangle, faceIndex) => {
      triangle.closestPointToPoint(point, target);
      const d = target.distanceTo(point);
      if (
        d <= distance &&
        highSample(geometry, faceIndex, target).normal.dot(normal) >= -1e-6
      ) {
        distance = d;
        best = { point: target.clone(), faceIndex, distance: d };
      }
      return false;
    },
  });
  return best;
}
function tangentFrame(low, tri, weights) {
  const ids = [
      low.indices[tri * 3],
      low.indices[tri * 3 + 1],
      low.indices[tri * 3 + 2],
    ],
    P = ids.map((i) => vector(low.positions, i)),
    U = ids.map((i) => uv(low.uvs, i));
  const normal = new THREE.Vector3();
  ids.forEach((id, i) =>
    normal.addScaledVector(vector(low.normals, id), weights[i]),
  );
  if (normal.lengthSq() < 1e-12) new THREE.Triangle(...P).getNormal(normal);
  normal.normalize();
  const e1 = P[1].clone().sub(P[0]),
    e2 = P[2].clone().sub(P[0]),
    d1 = U[1].clone().sub(U[0]),
    d2 = U[2].clone().sub(U[0]),
    det = d1.x * d2.y - d1.y * d2.x;
  const T = e1
      .clone()
      .multiplyScalar(d2.y)
      .addScaledVector(e2, -d1.y)
      .divideScalar(det),
    B = e2
      .clone()
      .multiplyScalar(d1.x)
      .addScaledVector(e1, -d2.x)
      .divideScalar(det);
  T.addScaledVector(normal, -T.dot(normal)).normalize();
  const bitangent = new THREE.Vector3()
    .crossVectors(normal, T)
    .multiplyScalar(
      B.dot(new THREE.Vector3().crossVectors(normal, T)) < 0 ? -1 : 1,
    )
    .normalize();
  return { normal, tangent: T, bitangent };
}
const tangentNormal = (normal, frame) => [
  normal.dot(frame.tangent) * 0.5 + 0.5,
  normal.dot(frame.bitangent) * 0.5 + 0.5,
  normal.dot(frame.normal) * 0.5 + 0.5,
];
function hemisphere(normal) {
  const tangent = new THREE.Vector3()
    .crossVectors(
      Math.abs(normal.z) < 0.9
        ? new THREE.Vector3(0, 0, 1)
        : new THREE.Vector3(0, 1, 0),
      normal,
    )
    .normalize();
  return {
    tangent,
    bitangent: new THREE.Vector3().crossVectors(normal, tangent),
  };
}
function averageNormal(
  bvh,
  geometry,
  point,
  normal,
  radius,
  samples,
  epsilon,
  facing = normal,
) {
  if (radius <= epsilon) return normal.clone();
  const basis = hemisphere(normal),
    sum = normal.clone().multiplyScalar(2);
  let weight = 2;
  for (let k = 0; k < samples; k++) {
    const a = (k / samples) * Math.PI * 2,
      query = point
        .clone()
        .addScaledVector(basis.tangent, Math.cos(a) * radius)
        .addScaledVector(basis.bitangent, Math.sin(a) * radius)
        .addScaledVector(normal, -radius * 0.3);
    const near = bvh.closestPointToPoint(query, {}, 0, radius * 1.8);
    if (!near || near.distance > radius * 1.8) continue;
    const n = highSample(geometry, near.faceIndex, near.point).normal;
    if (n.dot(normal) < -0.1 || n.dot(facing) < -1e-6) continue;
    const w = 1 - clamp(near.distance / (radius * 1.8));
    sum.addScaledVector(n, w);
    weight += w;
  }
  return sum.divideScalar(weight).normalize();
}
export async function bakeMeshMaps(
  low,
  high,
  inputSettings,
  inputGraph,
  { onProgress = () => {}, signal } = {},
) {
  const settings = validateMeshBakeSettings(inputSettings),
    graph = inputGraph ? validateBakeGraph(inputGraph) : null,
    required = graph
      ? requiredBakeMaps(graph, settings.channels)
      : { maps: settings.channels, bevels: [] },
    resolution = settings.resolution,
    size = resolution ** 2;
  if (required.bevels.length > 3)
    throw new Error(
      "Up to three active bevel-normal fields are supported per bake.",
    );
  const estimatedBytes =
    size *
      4 *
      4 *
      (required.maps.length +
        required.bevels.length +
        (graph
          ? reachableBakeNodes(graph, settings.channels).filter(
              (n) => !["MeshMap", "Bevel", "Output"].includes(n.type),
            ).length
          : 0)) +
    size * 40;
  if (estimatedBytes > 320 * 1024 * 1024)
    throw new Error(
      "This map selection/resolution exceeds the 320 MB working-field budget. Lower resolution or select fewer maps.",
    );
  const check = () => {
    if (signal?.aborted) throw new DOMException("Bake cancelled", "AbortError");
  };
  check();
  onProgress({ phase: "UV audit", done: 0, total: 1 });
  const raster = rasterizeLowMesh(low, resolution);
  check();
  onProgress({ phase: "Building high-mesh acceleration", done: 0, total: 1 });
  await tick();
  const { geometry, bvh } = highGeometry(high),
    bounds = geometry.boundingBox,
    extent = bounds.getSize(new THREE.Vector3()),
    diagonal = extent.length(),
    epsilon = Math.max(diagonal * 1e-5, 1e-7);
  // If N_world = inverse(transpose(M)) * N_local, then N_local = transpose(M) * N_world.
  const objectNormalMatrix = new THREE.Matrix3()
    .setFromMatrix4(
      new THREE.Matrix4().fromArray(
        low.objectToWorld || new THREE.Matrix4().toArray(),
      ),
    )
    .transpose();
  const encodedNormal = (n) => n.toArray().map((v) => v * 0.5 + 0.5);
  const localNormal = (n) =>
    n.clone().applyMatrix3(objectNormalMatrix).normalize();
  const dustUp = new THREE.Vector3();
  dustUp.setComponent(
    "XYZ".indexOf(settings.dustAxis[1]),
    settings.dustAxis[0] === "-" ? -1 : 1,
  );
  const maps = Object.fromEntries(
      required.maps.map((key) => [key, field(MESH_MAPS[key].type, size)]),
    ),
    bevelFields = Object.fromEntries(
      required.bevels.map((node) => [node.id, field("normal", size)]),
    );
  let hits = 0,
    misses = 0,
    nearestFallbacks = 0,
    backfacesRejected = 0,
    done = 0;
  try {
    for (let i = 0; i < size; i++) {
      if (!raster.coverage[i]) continue;
      const tri = raster.triangleAt[i],
        weights = [0, 1, 2].map((k) => raster.barycentric[i * 3 + k]),
        frame = tangentFrame(low, tri, weights),
        P = vector(raster.world, i),
        N = frame.normal;
      let hit;
      if (settings.projection === "nearest") {
        const limit = Math.max(settings.front, settings.back),
          nearest = bvh.closestPointToPoint(P, {}, 0, limit);
        hit = nearest && nearest.distance <= limit ? nearest : null;
      } else {
        const projected = facingProjection(
          bvh,
          geometry,
          new THREE.Ray(
            P.clone().addScaledVector(N, settings.front),
            N.clone().negate(),
          ),
          N,
          settings.front + settings.back,
          settings.front,
        );
        hit = projected.hit;
        backfacesRejected += projected.rejected;
        if (!hit && settings.nearestFallback) {
          hit = closestFacingPoint(
            bvh,
            geometry,
            P,
            N,
            Math.max(settings.front, settings.back),
          );
          if (hit) nearestFallbacks++;
        }
      }
      const Q = hit?.point || P,
        sample = hit
          ? highSample(geometry, hit.faceIndex, Q)
          : { normal: N, color: [1, 1, 1], partId: 0 },
        NH = sample.normal;
      if (hit) hits++;
      else misses++;
      write(maps.normal, i, tangentNormal(NH, frame));
      write(
        maps["world-normal"],
        i,
        NH.toArray().map((v) => v * 0.5 + 0.5),
      );
      write(maps["object-normal"], i, encodedNormal(localNormal(NH)));
      write(
        maps["vector-displacement"],
        i,
        Q.clone()
          .sub(P)
          .toArray()
          .map((v) => 0.5 + v / (2 * Math.max(settings.front, settings.back))),
      );
      write(maps.uv, i, [
        ((i % resolution) + 0.5) / resolution,
        1 - (Math.floor(i / resolution) + 0.5) / resolution,
        1,
      ]);
      write(maps["uv-island"], i, uvIslandColor(raster.charts[i]));
      write(maps.alpha, i, hit ? 1 : 0);
      if (maps.wireframe) {
        const ids = [0, 1, 2].map((k) => low.indices[tri * 3 + k]),
          U = ids.map((id) => uv(low.uvs, id).multiplyScalar(resolution));
        const area = Math.abs(
          (U[1].x - U[0].x) * (U[2].y - U[0].y) -
            (U[1].y - U[0].y) * (U[2].x - U[0].x),
        );
        const distance = Math.min(
          ...weights.map(
            (w, k) =>
              (Math.abs(w) * area) /
              Math.max(U[(k + 1) % 3].distanceTo(U[(k + 2) % 3]), 1e-9),
          ),
        );
        write(maps.wireframe, i, clamp(settings.wireWidth + 0.5 - distance));
      }
      write(
        maps.height,
        i,
        0.5 +
          (hit ? Q.clone().sub(P).dot(N) : 0) /
            (2 * Math.max(settings.front, settings.back)),
      );
      write(
        maps.position,
        i,
        [0, 1, 2].map((c) =>
          extent.getComponent(c) > 1e-9
            ? (Q.getComponent(c) - bounds.min.getComponent(c)) /
              extent.getComponent(c)
            : 0.5,
        ),
      );
      const id = hit ? sample.partId + 1 : 0;
      write(maps.id, i, [
        (id & 255) / 255,
        ((id >>> 8) & 255) / 255,
        ((id >>> 16) & 255) / 255,
      ]);
      write(maps.coverage, i, 1);
      write(maps["base-color"], i, sample.color);
      const basis = hemisphere(NH),
        origin = Q.clone().addScaledVector(NH, epsilon * 3);
      let visibility = 1,
        secondaryVisibility = 1;
      const bent = new THREE.Vector3();
      if (
        maps.occlusion ||
        maps["occlusion-secondary"] ||
        maps["bent-normal"] ||
        maps["bent-world-normal"] ||
        maps["bent-object-normal"] ||
        maps.dust ||
        maps.dirt ||
        maps["edge-wear"]
      ) {
        let blocked = 0,
          secondaryBlocked = 0;
        const needSecondary = !!maps["occlusion-secondary"];
        const distance = needSecondary
          ? Math.max(settings.aoDistance, settings.secondaryAoDistance)
          : settings.aoDistance;
        if (hit)
          for (let s = 0; s < settings.samples; s++) {
            // Cosine-weighted low-discrepancy hemisphere sampling: diffuse AO
            // estimates the Lambertian visibility integral without an extra
            // cosine weight. Rotation is world-position based, not UV-index
            // based, so adjacent UV charts do not acquire unrelated noise.
            const u = (s + 0.5) / settings.samples,
              z = Math.sqrt(1 - u),
              r = Math.sqrt(u),
              a =
                s * 2.399963229728653 +
                ((Q.x * 0.754877666 + Q.y * 0.569840296 + Q.z * 0.438579021) %
                  1) *
                  Math.PI *
                  2,
              direction = NH.clone()
                .multiplyScalar(z)
                .addScaledVector(basis.tangent, Math.cos(a) * r)
                .addScaledVector(basis.bitangent, Math.sin(a) * r);
            const obstruction = bvh.raycastFirst(
              new THREE.Ray(origin, direction),
              THREE.DoubleSide,
              epsilon,
              distance,
            );
            const occluded =
              obstruction && obstruction.distance <= settings.aoDistance;
            if (occluded) blocked++;
            else bent.add(direction);
            if (
              obstruction &&
              obstruction.distance <= settings.secondaryAoDistance
            )
              secondaryBlocked++;
          }
        visibility = 1 - blocked / settings.samples;
        secondaryVisibility = 1 - secondaryBlocked / settings.samples;
        write(maps.occlusion, i, visibility);
        write(maps["occlusion-secondary"], i, secondaryVisibility);
      }
      if (bent.lengthSq() < 1e-12) bent.copy(NH);
      else bent.normalize();
      write(maps["bent-normal"], i, tangentNormal(bent, frame));
      write(maps["bent-world-normal"], i, encodedNormal(bent));
      write(maps["bent-object-normal"], i, encodedNormal(localNormal(bent)));
      if (maps.thickness) {
        const inward = bvh.raycastFirst(
          new THREE.Ray(
            Q.clone().addScaledVector(NH, -epsilon * 3),
            NH.clone().negate(),
          ),
          THREE.DoubleSide,
          epsilon,
          Infinity,
        );
        write(
          maps.thickness,
          i,
          hit && inward
            ? clamp((inward.distance + epsilon * 3) / settings.thicknessRange)
            : 0,
        );
      }
      let signedCurvature = 0;
      if (
        maps.curvature ||
        maps.convexity ||
        maps.cavity ||
        maps.dirt ||
        maps["edge-wear"]
      ) {
        let sum = 0,
          count = 0;
        if (hit)
          for (let s = 0; s < 4; s++) {
            const a = s * Math.PI * 0.5,
              tangent = basis.tangent
                .clone()
                .multiplyScalar(Math.cos(a))
                .addScaledVector(basis.bitangent, Math.sin(a)),
              query = Q.clone().addScaledVector(
                tangent,
                settings.curvatureRadius,
              ),
              near = bvh.closestPointToPoint(
                query,
                {},
                0,
                settings.curvatureRadius * 2,
              );
            if (near && near.distance <= settings.curvatureRadius * 2) {
              const neighbor = highSample(
                geometry,
                near.faceIndex,
                near.point,
              ).normal;
              sum += neighbor.clone().sub(NH).dot(tangent);
              count++;
            }
          }
        signedCurvature = count ? sum / count : 0;
        write(maps.curvature, i, 0.5 + signedCurvature * 0.5);
      }
      const convexity = clamp(signedCurvature * settings.maskStrength),
        cavity = clamp(-signedCurvature * settings.maskStrength);
      write(maps.convexity, i, convexity);
      write(maps.cavity, i, cavity);
      write(
        maps.dust,
        i,
        hit
          ? Math.pow(Math.max(0, NH.dot(dustUp)), settings.dustFalloff) *
              (0.65 + 0.35 * (1 - visibility))
          : 0,
      );
      write(
        maps.dirt,
        i,
        hit
          ? clamp((1 - visibility) * settings.maskStrength + cavity * 0.5)
          : 0,
      );
      write(maps["edge-wear"], i, hit ? convexity * visibility : 0);
      if (maps["bevel-normal"] || maps["bevel-mask"]) {
        const averaged = hit
          ? averageNormal(
              bvh,
              geometry,
              Q,
              NH,
              settings.bevelRadius,
              settings.bevelSamples,
              epsilon,
              N,
            )
          : NH.clone();
        averaged.lerp(NH, 1 - settings.bevelStrength).normalize();
        write(maps["bevel-normal"], i, tangentNormal(averaged, frame));
        write(
          maps["bevel-mask"],
          i,
          hit ? clamp((1 - NH.dot(averaged)) * 8) : 0,
        );
      }
      for (const node of required.bevels) {
        const averaged = hit
          ? averageNormal(
              bvh,
              geometry,
              Q,
              NH,
              node.params.radius,
              node.params.samples,
              epsilon,
              N,
            )
          : NH;
        write(bevelFields[node.id], i, tangentNormal(averaged, frame));
      }
      done++;
      if (done % 1024 === 0) {
        check();
        onProgress({
          phase: "Projecting mesh maps",
          done,
          total: raster.covered,
          hits,
          misses,
        });
        await tick();
      }
    }
    check();
    onProgress({
      phase: graph ? "Evaluating legacy recipe" : "Finalizing selected maps",
      done: raster.covered,
      total: raster.covered,
      hits,
      misses,
    });
    await tick();
    const results = graph
      ? evaluateBakeGraph(
          graph,
          {
            width: resolution,
            height: resolution,
            coverage: raster.coverage,
            charts: raster.charts,
            maps,
            bevelFields,
          },
          settings.channels,
        )
      : maps;
    onProgress({
      phase: "Maps complete",
      done: raster.covered,
      total: raster.covered,
      hits,
      misses,
    });
    return {
      maps: results,
      coverage: raster.coverage,
      charts: raster.charts,
      stats: {
        covered: raster.covered,
        hits,
        misses,
        nearestFallbacks,
        backfacesRejected,
        aoSampling:
          "cosine-weighted low-discrepancy hemisphere; world-position rotation",
        projectionAlgorithm:
          settings.projection === "ray"
            ? "nearest normal-attribute-facing cage intersection; bounded oriented nearest fallback"
            : "bounded nearest point",
        charts: raster.chartCount,
        degenerate: raster.degenerate,
        estimatedBytes,
      },
      settings,
      bounds: { min: bounds.min.toArray(), max: bounds.max.toArray() },
      graph,
      ...(maps["uv-island"]
        ? {
            chartPalette: Array.from(
              { length: raster.chartCount },
              (_, index) => ({
                id: index + 1,
                rgb: uvIslandColor(index + 1).map((v) => Math.round(v * 255)),
              }),
            ),
          }
        : {}),
    };
  } finally {
    geometry.dispose();
  }
}
export function encodeBakeMap(field, width, height, settings, coverage) {
  const out = new Uint8ClampedArray(width * height * 4),
    srgb = (v) => (v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055);
  for (let i = 0; i < width * height; i++) {
    for (let c = 0; c < 3; c++) {
      let v = field.data[i * 4 + c];
      if (field.type === "normal" && settings.normalY === "-Y" && c === 1)
        v = 1 - v;
      if (settings.colorSpace === "sRGB") v = srgb(clamp(v));
      out[i * 4 + c] = Math.round(clamp(v) * 255);
    }
    out[i * 4 + 3] = coverage[i] ? 255 : 0;
  }
  return out;
}
export function dilateBakePixels(input, width, height, coverage, steps) {
  const pixels = new Uint8ClampedArray(input),
    owned = new Int32Array(width * height).fill(-1);
  for (let i = 0; i < owned.length; i++) if (coverage[i]) owned[i] = i;
  let frontier = Array.from({ length: owned.length }, (_, i) => i).filter(
    (i) => owned[i] >= 0,
  );
  for (let s = 0; s < steps; s++) {
    const next = [];
    for (const i of frontier) {
      const x = i % width,
        y = Math.floor(i / width);
      for (const j of [
        x > 0 ? i - 1 : -1,
        x < width - 1 ? i + 1 : -1,
        y > 0 ? i - width : -1,
        y < height - 1 ? i + width : -1,
      ])
        if (j >= 0 && owned[j] < 0) {
          owned[j] = owned[i];
          for (let c = 0; c < 3; c++)
            pixels[j * 4 + c] = pixels[owned[i] * 4 + c];
          next.push(j);
        }
    }
    frontier = next;
    if (!frontier.length) break;
  }
  return pixels;
}
