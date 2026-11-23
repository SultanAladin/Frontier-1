import * as THREE from "three";
import { MeshBVH } from "three-mesh-bvh";

export const VOLUME_SDF_SCHEMA = "alloy.volume-sdf.v1";
export const VOLUME_SDF_TRIANGLE_LIMIT = 250000;
const tick = () => new Promise((resolve) => setTimeout(resolve, 0));
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export function validateVolumeSdfSettings(input = {}) {
  const resolution = Number(input.resolution ?? 64);
  if (![16, 32, 64, 96, 128].includes(resolution))
    throw new Error(
      "Volume SDF resolution must be 16, 32, 64, 96 or 128 voxels per axis. A 512³ float volume is outside the browser budget; 512×512 mesh maps are separate.",
    );
  const padding = Number(input.padding ?? 0.1);
  if (!Number.isFinite(padding) || padding < 0.01 || padding > 0.5)
    throw new Error(
      "SDF bounds padding must be between 0.01 and 0.5 of the source diagonal.",
    );
  return {
    resolution,
    padding,
    signMode: ["strict", "approximate", "unsigned"].includes(input.signMode)
      ? input.signMode
      : "strict",
  };
}
function validateGeometry(data) {
  if (
    !data?.positions ||
    !data?.indices ||
    data.positions.length % 3 ||
    data.indices.length % 3
  )
    throw new Error(
      "Volume baking requires valid triangle positions and indices.",
    );
  if (data.indices.length / 3 > VOLUME_SDF_TRIANGLE_LIMIT)
    throw new Error(
      "Volume SDF supports up to 250,000 source triangles. Decimate the source before a volume bake.",
    );
  if (!data.indices.length || data.positions.length < 9)
    throw new Error("The SDF source has no triangles.");
  for (const value of data.positions)
    if (!Number.isFinite(value))
      throw new Error("SDF source contains non-finite positions.");
  for (const index of data.indices)
    if (index >= data.positions.length / 3)
      throw new Error("SDF index references a missing vertex.");
}

// Welding is for edge-topology diagnostics only. The closest-point query always
// uses the original triangle geometry: no hidden cap, remesh or geometry repair.
export function auditVolumeTopology(data) {
  validateGeometry(data);
  const geometry = new THREE.BufferGeometry().setAttribute(
    "position",
    new THREE.BufferAttribute(data.positions, 3),
  );
  geometry.computeBoundingBox();
  const bounds = geometry.boundingBox.clone(),
    diagonal = bounds.getSize(new THREE.Vector3()).length();
  geometry.dispose();
  if (!Number.isFinite(diagonal) || diagonal < 1e-10)
    throw new Error("SDF source has zero or invalid extent.");
  const epsilon = Math.max(diagonal * 1e-6, 1e-9),
    epsilon2 = epsilon * epsilon;
  const buckets = new Map(),
    points = [],
    welded = new Uint32Array(data.positions.length / 3);
  for (let i = 0; i < welded.length; i++) {
    const p = [
      data.positions[i * 3],
      data.positions[i * 3 + 1],
      data.positions[i * 3 + 2],
    ];
    const q = p.map((v, a) =>
      Math.floor((v - bounds.min.getComponent(a)) / epsilon),
    );
    let found = -1;
    search: for (let z = -1; z <= 1; z++)
      for (let y = -1; y <= 1; y++)
        for (let x = -1; x <= 1; x++) {
          for (const j of buckets.get(`${q[0] + x},${q[1] + y},${q[2] + z}`) ||
            []) {
            const point = points[j];
            if (
              (p[0] - point[0]) ** 2 +
                (p[1] - point[1]) ** 2 +
                (p[2] - point[2]) ** 2 <=
              epsilon2
            ) {
              found = j;
              break search;
            }
          }
        }
    if (found < 0) {
      found = points.length;
      points.push(p);
      const key = q.join(",");
      if (!buckets.has(key)) buckets.set(key, []);
      buckets.get(key).push(found);
    }
    welded[i] = found;
  }
  const edges = new Map();
  let degenerate = 0,
    triangles = 0;
  const A = new THREE.Vector3(),
    B = new THREE.Vector3(),
    C = new THREE.Vector3();
  for (let i = 0; i < data.indices.length; i += 3) {
    const ids = [data.indices[i], data.indices[i + 1], data.indices[i + 2]],
      w = ids.map((j) => welded[j]);
    A.fromArray(data.positions, ids[0] * 3);
    B.fromArray(data.positions, ids[1] * 3);
    C.fromArray(data.positions, ids[2] * 3);
    if (
      new Set(w).size < 3 ||
      B.sub(A).cross(C.sub(A)).lengthSq() < diagonal ** 4 * 1e-20
    ) {
      degenerate++;
      continue;
    }
    triangles++;
    for (let e = 0; e < 3; e++) {
      const a = w[e],
        b = w[(e + 1) % 3],
        key = Math.min(a, b) * points.length + Math.max(a, b);
      const edge = edges.get(key) || { count: 0, balance: 0 };
      edge.count++;
      edge.balance += a < b ? 1 : -1;
      edges.set(key, edge);
    }
  }
  let boundaryEdges = 0,
    nonManifoldEdges = 0,
    orientationConflicts = 0;
  for (const edge of edges.values()) {
    if (edge.count === 1) boundaryEdges++;
    if (edge.count > 2) nonManifoldEdges++;
    if (edge.count === 2 && edge.balance !== 0) orientationConflicts++;
  }
  return {
    triangles,
    degenerate,
    vertices: welded.length,
    weldedVertices: points.length,
    weldTolerance: epsilon,
    boundaryEdges,
    nonManifoldEdges,
    orientationConflicts,
    closedEdgeManifold:
      triangles > 0 &&
      boundaryEdges === 0 &&
      nonManifoldEdges === 0 &&
      orientationConflicts === 0,
    bounds: { min: bounds.min.toArray(), max: bounds.max.toArray() },
    diagonal,
  };
}
const SIGN_RAYS = [
  [1, 0.173, 0.371],
  [-0.257, 1, 0.419],
  [0.319, -0.223, 1],
].map((v) => new THREE.Vector3(...v).normalize());

export function createVolumeDistanceSampler(data, signMode = "strict") {
  const topology = auditVolumeTopology(data);
  if (signMode === "strict" && !topology.closedEdgeManifold)
    throw new Error(
      `Signed volume requires a closed, consistently oriented edge-manifold source. Found ${topology.boundaryEdges} boundary edges, ${topology.nonManifoldEdges} non-manifold edges and ${topology.orientationConflicts} orientation conflicts. Choose explicitly Approximate signed or Unsigned; the source is not silently sealed.`,
    );
  const geometry = new THREE.BufferGeometry().setAttribute(
    "position",
    new THREE.BufferAttribute(data.positions, 3),
  );
  geometry.setIndex(
    new THREE.BufferAttribute(new Uint32Array(data.indices), 1),
  );
  geometry.computeBoundingBox();
  const bvh = new MeshBVH(geometry, { targetLeafSize: 8 });
  const epsilon = Math.max(topology.diagonal * 1e-7, 1e-9),
    closest = {},
    ray = new THREE.Ray();
  return {
    topology,
    sample(point) {
      const near = bvh.closestPointToPoint(point, closest, 0, Infinity);
      if (!near || !Number.isFinite(near.distance))
        throw new Error(
          "The BVH could not compute a finite closest-point distance.",
        );
      if (signMode === "unsigned")
        return {
          distance: near.distance < epsilon * 0.1 ? 0 : near.distance,
          confidence: 0,
          agreement: 0,
        };
      if (near.distance < epsilon * 0.1)
        return { distance: 0, confidence: 255, agreement: 3 };
      let insideVotes = 0;
      for (const direction of SIGN_RAYS) {
        ray.origin.copy(point);
        ray.direction.copy(direction);
        const hits = bvh
          .raycast(ray, THREE.DoubleSide, epsilon, Infinity)
          .sort((a, b) => a.distance - b.distance);
        let crossings = 0,
          previous = -Infinity;
        // A ray hitting a shared triangle edge must count that physical crossing
        // only once. This is parity of unique hit distances, not triangle count.
        for (const hit of hits)
          if (hit.distance - previous > epsilon) {
            crossings++;
            previous = hit.distance;
          }
        insideVotes += crossings % 2;
      }
      const agreement = Math.max(insideVotes, 3 - insideVotes);
      return {
        distance: near.distance * (insideVotes >= 2 ? -1 : 1),
        confidence: agreement === 3 ? 255 : 170,
        agreement,
      };
    },
    dispose() {
      geometry.dispose();
    },
  };
}

export async function bakeVolumeSdf(
  high,
  inputSettings = {},
  { onProgress = () => {}, signal } = {},
) {
  const settings = validateVolumeSdfSettings(inputSettings),
    n = settings.resolution;
  const check = () => {
    if (signal?.aborted)
      throw new DOMException("SDF bake cancelled", "AbortError");
  };
  check();
  onProgress({ phase: "Auditing volume topology", done: 0, total: 1 });
  await tick();
  check();
  const sampler = createVolumeDistanceSampler(high, settings.signMode),
    topology = sampler.topology;
  const padding = topology.diagonal * settings.padding;
  const min = topology.bounds.min.map((v) => v - padding),
    max = topology.bounds.max.map((v) => v + padding);
  const spacing = min.map((v, a) => (max[a] - v) / n),
    origin = min.map((v, a) => v + spacing[a] / 2);
  const total = n ** 3,
    distances = new Float32Array(total),
    confidence = new Uint8Array(total),
    point = new THREE.Vector3();
  let negative = 0,
    ambiguous = 0,
    minimum = Infinity,
    maximum = -Infinity;
  try {
    for (let z = 0; z < n; z++)
      for (let y = 0; y < n; y++)
        for (let x = 0; x < n; x++) {
          const i = x + n * (y + n * z);
          point.set(
            origin[0] + x * spacing[0],
            origin[1] + y * spacing[1],
            origin[2] + z * spacing[2],
          );
          const value = sampler.sample(point);
          distances[i] = value.distance;
          confidence[i] = value.confidence;
          if (value.distance < 0) negative++;
          if (value.agreement === 2) ambiguous++;
          minimum = Math.min(minimum, value.distance);
          maximum = Math.max(maximum, value.distance);
          if ((i + 1) % 4096 === 0) {
            check();
            onProgress({
              phase: "Sampling 3D distance / parity",
              done: i + 1,
              total,
            });
            await tick();
          }
        }
    check();
    onProgress({ phase: "Volume SDF complete", done: total, total });
    return {
      distances,
      confidence,
      settings,
      topology,
      min,
      max,
      spacing,
      origin,
      stats: {
        voxels: total,
        negative,
        ambiguous,
        ambiguousFraction: ambiguous / total,
        min: minimum,
        max: maximum,
        bytes: total * 5,
      },
      signed: settings.signMode !== "unsigned",
      approximate:
        settings.signMode === "approximate" || !topology.closedEdgeManifold,
    };
  } finally {
    sampler.dispose();
  }
}
export function volumeSdfManifest(result, sources = []) {
  return {
    schema: VOLUME_SDF_SCHEMA,
    dimensions: [
      result.settings.resolution,
      result.settings.resolution,
      result.settings.resolution,
    ],
    settings: result.settings,
    signed: result.signed,
    approximate: result.approximate,
    signConvention: result.signed
      ? "negative inside, zero surface, positive outside"
      : "unsigned distance only; no inside/outside classification",
    storage: {
      file: "distance.f32",
      type: "IEEE754 Float32 little-endian",
      layout: "x-fastest; index = x + nx * (y + ny * z)",
      units: "source scene units",
      clamped: false,
    },
    confidence: {
      file: "sign-confidence.u8",
      type: "Uint8",
      values: {
        255: "all three parity rays agree (or on surface)",
        170: "only two of three rays agree; ambiguous",
        0: "unsigned mode; no sign",
      },
      note: "Agreement is not a proof of correctness on an open or self-intersecting source.",
    },
    bounds: { min: result.min, max: result.max },
    voxelSpacing: result.spacing,
    voxelCenterOrigin: result.origin,
    topology: result.topology,
    stats: result.stats,
    sources,
    algorithm:
      "Exact Euclidean point-to-triangle distance via MeshBVH; sign by majority of three oblique ray parities, deduplicating coincident triangle hits. Topology audited by tolerance-welded edges; query geometry is not modified.",
    limitations: [
      "A mesh volume, not a 2D surface texture or projection-height alias. Signing uses an odd-even fill rule, not a constructive union of overlapping solids.",
      "Strict mode requires a closed consistently oriented edge-manifold source; topology checks cannot certify absence of geometric self-intersections.",
      "Open/intersecting sources do not define an unambiguous solid. Approximate signs are explicitly flagged; use unsigned distances or supply a repaired closed mesh when reliable signs are required.",
      "Samples are voxel centres, not voxel corners. Trilinear reconstruction introduces grid-resolution error; stored distances are unclamped Float32 values in scene units.",
      "A 512×512 texture-map bake is independent of volume resolution. Volume resolution is bounded to 16–128 per axis and 250,000 source triangles.",
    ],
  };
}
export function volumeSlicePixels(result) {
  const n = result.settings.resolution,
    mid = Math.floor(n / 2),
    range = result.topology.diagonal * 0.15;
  const output = {};
  for (const [axis, a] of ["X", "Y", "Z"].map((name, a) => [name, a])) {
    const distance = new Uint8ClampedArray(n * n * 4),
      confidence = new Uint8ClampedArray(n * n * 4);
    for (let v = 0; v < n; v++)
      for (let u = 0; u < n; u++) {
        const xyz =
            a === 0
              ? [mid, u, n - 1 - v]
              : a === 1
                ? [u, mid, n - 1 - v]
                : [u, n - 1 - v, mid],
          index = xyz[0] + n * (xyz[1] + n * xyz[2]),
          i = (u + n * v) * 4;
        const gray = Math.round(
          clamp(0.5 + result.distances[index] / (2 * range), 0, 1) * 255,
        );
        distance.set([gray, gray, gray, 255], i);
        const c = result.confidence[index];
        confidence.set(
          c === 255
            ? [80, 180, 130, 255]
            : c === 170
              ? [244, 165, 66, 255]
              : [128, 128, 128, 255],
          i,
        );
      }
    output[`slice-${axis.toLowerCase()}`] = distance;
    output[`confidence-${axis.toLowerCase()}`] = confidence;
  }
  return {
    pixels: output,
    resolution: n,
    range,
    encoding:
      "preview gray = clamp(0.5 + signed distance / (2 * range),0,1); full float volume is not clamped",
  };
}
