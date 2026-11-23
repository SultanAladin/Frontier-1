import { test, expect } from "@playwright/test";
import * as THREE from "three";
import {
  bakeMeshData,
  createDemoBakeAssets,
  importBakeMesh,
  mergeBakeMeshData,
} from "../src/baking/bakeMeshes.js";
import {
  rasterizeLowMesh,
  bakeMeshMaps,
  encodeBakeMap,
  dilateBakePixels,
  validateMeshBakeSettings,
} from "../src/baking/meshBakeCore.js";
import {
  BAKE_NODES,
  GRAPH_SCHEMA,
  MESH_MAPS,
  bakeNode,
  defaultBakeGraph,
  validateBakeGraph,
  connectBakeNode,
} from "../src/baking/bakeGraph.js";
import { evaluateBakeGraph } from "../src/baking/bakeGraphEvaluation.js";
import { normalizeStudioWorkspace } from "../src/StudioUI.jsx";
const emptyGraph = () => ({ schema: GRAPH_SCHEMA, nodes: [] });
function plane(z = 0) {
  const g = new THREE.PlaneGeometry(1, 1);
  g.translate(0, 0, z);
  return bakeMeshData(g);
}

test("baking built-in teapots have 32 unique charts rather than repeated patch UVs", () => {
  const assets = createDemoBakeAssets();
  const raster = rasterizeLowMesh(bakeMeshData(assets.low[0].geometry), 64);
  expect(raster.chartCount).toBe(32);
  expect(raster.covered).toBeGreaterThan(3000);
  expect(raster.covered).toBeLessThan(4096);
});
test("baking UV audits reject overlap, missing coordinates and UDIM/out-of-tile UVs", () => {
  const low = plane();
  const extra = {
    ...low,
    positions: new Float32Array(
      [...low.positions, ...low.positions].map((v, i) =>
        i >= low.positions.length && i % 3 === 2 ? v + 0.5 : v,
      ),
    ),
    normals: new Float32Array([...low.normals, ...low.normals]),
    uvs: new Float32Array([...low.uvs, ...low.uvs]),
    indices: new Uint32Array([
      ...low.indices,
      ...[...low.indices].map((i) => i + 4),
    ]),
    partIds: new Float32Array(8),
  };
  expect(() => rasterizeLowMesh(extra, 64)).toThrow(/Overlapping/);
  expect(() => rasterizeLowMesh({ ...low, uvs: null }, 64)).toThrow(/UV0/);
  const uvs = new Float32Array(low.uvs);
  uvs[0] = 1.5;
  expect(() => rasterizeLowMesh({ ...low, uvs }, 64)).toThrow(/0–1/);
});
test("baking analytic plane projection encodes height, neutral normals, flat curvature and unobstructed AO", async () => {
  const result = await bakeMeshMaps(
    plane(),
    plane(0.1),
    {
      resolution: 64,
      front: 0.3,
      back: 0.3,
      samples: 4,
      channels: ["normal", "height", "curvature", "occlusion"],
    },
    emptyGraph(),
  );
  expect(result.stats).toMatchObject({
    covered: 4096,
    hits: 4096,
    misses: 0,
    charts: 1,
  });
  expect([...result.maps.normal.data.slice(0, 4)]).toEqual([0.5, 0.5, 1, 1]);
  expect(result.maps.height.data[0]).toBeCloseTo(2 / 3, 5);
  expect(result.maps.curvature.data[0]).toBe(0.5);
  expect(result.maps.occlusion.data[0]).toBe(1);
});
test("baking high-to-low normals use low UV tangent axes, including a tilted high surface", async () => {
  const geometry = new THREE.PlaneGeometry(1, 1),
    p = geometry.attributes.position;
  for (let i = 0; i < p.count; i++) p.setZ(i, 0.1 + p.getX(i) * 0.2);
  geometry.computeVertexNormals();
  const result = await bakeMeshMaps(
    plane(),
    bakeMeshData(geometry),
    {
      resolution: 64,
      front: 0.3,
      back: 0.3,
      channels: ["normal", "world-normal"],
    },
    emptyGraph(),
  );
  expect(result.stats.misses).toBe(0);
  expect(result.maps.normal.data[0]).toBeLessThan(0.5);
  expect(result.maps.normal.data[2]).toBeGreaterThan(0.9);
  expect(result.maps["world-normal"].data[0]).toBeCloseTo(
    result.maps.normal.data[0],
    5,
  );
});
test("baking projection misses are counted and do not invent high-surface data", async () => {
  const result = await bakeMeshMaps(
    plane(),
    plane(2),
    {
      resolution: 64,
      front: 0.1,
      back: 0.1,
      channels: ["normal", "height", "thickness"],
    },
    emptyGraph(),
  );
  expect(result.stats).toMatchObject({ hits: 0, misses: 4096 });
  expect(result.maps.height.data[0]).toBe(0.5);
  expect(result.maps.thickness.data[0]).toBe(0);
});
test("baking bevel normals change edge shading on a cube without changing topology", async () => {
  const assets = createDemoBakeAssets("Cube"),
    low = bakeMeshData(assets.low[0].geometry),
    high = bakeMeshData(assets.high[0].geometry),
    graph = defaultBakeGraph();
  graph.nodes.find((n) => n.type === "Bevel").params.radius = 0.15;
  const before = Array.from(low.positions),
    result = await bakeMeshMaps(
      low,
      high,
      { resolution: 64, front: 0.3, back: 0.3, channels: ["normal"] },
      graph,
    );
  let softened = 0;
  for (let i = 0; i < 4096; i++)
    if (
      result.coverage[i] &&
      (Math.abs(result.maps.normal.data[i * 4] - 0.5) > 0.02 ||
        Math.abs(result.maps.normal.data[i * 4 + 1] - 0.5) > 0.02)
    )
      softened++;
  expect(softened).toBeGreaterThan(100);
  expect(Array.from(low.positions)).toEqual(before);
});
test("baking ID, position, thickness and coverage maps encode geometry rather than viewport lighting", async () => {
  const assets = createDemoBakeAssets("Cube"),
    result = await bakeMeshMaps(
      bakeMeshData(assets.low[0].geometry),
      bakeMeshData(assets.high[0].geometry),
      {
        resolution: 64,
        front: 0.3,
        back: 0.3,
        thicknessRange: 3,
        channels: ["id", "position", "thickness", "coverage"],
      },
      emptyGraph(),
    );
  let covered = 0,
    thick = 0;
  const ids = new Set();
  for (let i = 0; i < 4096; i++)
    if (result.coverage[i]) {
      covered++;
      ids.add(Math.round(result.maps.id.data[i * 4] * 255));
      if (result.maps.thickness.data[i * 4] > 0.4) thick++;
      expect(result.maps.coverage.data[i * 4]).toBe(1);
    }
  // One high cube object, six UV charts. Object IDs must not masquerade as
  // material IDs or UV-island IDs.
  expect(ids.size).toBe(1);
  expect(thick).toBeGreaterThan(covered * 0.95);
  expect(result.maps.position.data.some((v) => v > 0.9)).toBe(true);
});
test("baking node graphs reject cycles, invalid socket types, duplicate outputs and executable node types", () => {
  const a = bakeNode("Value", { id: "a" }),
    b = bakeNode("Math", { id: "b" }),
    o = bakeNode("Output", {
      id: "o",
      params: { map: "occlusion" },
      inputs: ["b"],
    });
  let graph = validateBakeGraph({ schema: GRAPH_SCHEMA, nodes: [a, b, o] });
  graph = connectBakeNode(graph, "b", 0, "a");
  expect(() => connectBakeNode(graph, "b", 1, "b")).toThrow(/cycle/);
  expect(() =>
    validateBakeGraph({
      schema: GRAPH_SCHEMA,
      nodes: [bakeNode("Bevel", { id: "n" }), { ...o, inputs: ["n"] }],
    }),
  ).toThrow(/needs scalar/);
  expect(() =>
    validateBakeGraph({
      ...graph,
      nodes: [...graph.nodes, { ...o, id: "another" }],
    }),
  ).toThrow(/one output/);
  expect(() =>
    validateBakeGraph({
      schema: GRAPH_SCHEMA,
      nodes: [
        { id: "script", type: "JavaScript", params: { code: "alert(1)" } },
      ],
    }),
  ).toThrow(/invalid node/);
  expect(Object.keys(BAKE_NODES)).toHaveLength(25);
});
test("baking node parameters remain bounded and recipe graphs preserve zero-valued inputs", () => {
  const graph = validateBakeGraph({
    schema: GRAPH_SCHEMA,
    nodes: [
      bakeNode("Bevel", { id: "b", params: { radius: -10, samples: 10000 } }),
      bakeNode("Value", { id: "v", params: { value: 0 } }),
    ],
  });
  expect(graph.nodes[0].params).toEqual({ radius: 0, samples: 16 });
  expect(graph.nodes[1].params.value).toBe(0);
});
test("baking procedural, color and height-to-normal graph nodes evaluate real pixels", () => {
  const coverage = new Uint8Array(16).fill(1),
    charts = new Uint32Array(16).fill(1),
    graph = {
      schema: GRAPH_SCHEMA,
      nodes: [
        bakeNode("Gradient", { id: "gradient" }),
        bakeNode("ColorRamp", { id: "ramp", inputs: ["gradient"] }),
        bakeNode("Output", {
          id: "color",
          params: { map: "base-color" },
          inputs: ["ramp"],
        }),
        bakeNode("NormalFromHeight", {
          id: "height-normal",
          inputs: ["gradient"],
        }),
        bakeNode("Output", {
          id: "normal",
          params: { map: "normal" },
          inputs: ["height-normal"],
        }),
      ],
    };
  const result = evaluateBakeGraph(
    graph,
    { width: 4, height: 4, coverage, charts, maps: {} },
    ["base-color", "normal"],
  );
  expect(result["base-color"].data[0]).toBeLessThan(
    result["base-color"].data[12],
  );
  expect(result.normal.data[0]).toBeLessThan(0.5);
  expect(result.normal.data[2]).toBeGreaterThan(0.5);
});
test("baking blur respects chart boundaries, graph noise is deterministic and unused outputs are not required", () => {
  const coverage = new Uint8Array(4).fill(1),
    charts = new Uint32Array([1, 1, 2, 2]),
    source = {
      type: "scalar",
      data: new Float32Array([0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1]),
    };
  const graph = {
    schema: GRAPH_SCHEMA,
    nodes: [
      bakeNode("MeshMap", { id: "source", params: { map: "height" } }),
      bakeNode("Blur", {
        id: "blur",
        inputs: ["source"],
        params: { radius: 3 },
      }),
      bakeNode("Output", {
        id: "out",
        params: { map: "height" },
        inputs: ["blur"],
      }),
    ],
  };
  const result = evaluateBakeGraph(
    graph,
    { width: 4, height: 1, coverage, charts, maps: { height: source } },
    ["height"],
  );
  expect(result.height.data[0]).toBe(0);
  expect(result.height.data[8]).toBe(1);
  const noise = {
      schema: GRAPH_SCHEMA,
      nodes: [
        bakeNode("Noise", { id: "n" }),
        bakeNode("Output", {
          id: "o",
          params: { map: "occlusion" },
          inputs: ["n"],
        }),
      ],
    },
    context = { width: 4, height: 1, coverage, charts, maps: {} };
  expect(
    evaluateBakeGraph(noise, context, ["occlusion"]).occlusion.data,
  ).toEqual(evaluateBakeGraph(noise, context, ["occlusion"]).occlusion.data);
});
test("baking PNG encoding flips normal Y explicitly and padding never changes coverage alpha", () => {
  const field = {
      type: "normal",
      data: new Float32Array([0.5, 0.25, 1, 1, 0.5, 0.5, 1, 0]),
    },
    coverage = new Uint8Array([1, 0]);
  const encoded = encodeBakeMap(
    field,
    2,
    1,
    { normalY: "-Y", colorSpace: "linear" },
    coverage,
  );
  expect(encoded[1]).toBe(191);
  expect(encoded[7]).toBe(0);
  const padded = dilateBakePixels(encoded, 2, 1, coverage, 1);
  expect([...padded.slice(4, 7)]).toEqual([...encoded.slice(0, 3)]);
  expect(padded[7]).toBe(0);
});
test("baking imported OBJ objects retain transforms, normals and independent names; low meshes require UVs", async () => {
  const source =
    "o Triangle\nv 10 0 0\nv 11 0 0\nv 10 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n";
  const file = {
    name: "triangle.obj",
    size: source.length,
    text: async () => source,
  };
  const parts = await importBakeMesh(file, "low");
  expect(parts[0].geometry.attributes.position.getX(0)).toBe(10);
  expect(parts[0].name).toBe("Triangle");
  expect(parts[0].geometry.attributes.normal).toBeTruthy();
  const bare = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  await expect(
    importBakeMesh(
      { name: "bare.obj", size: bare.length, text: async () => bare },
      "low",
    ),
  ).rejects.toThrow(/UV0/);
  await expect(
    importBakeMesh({ ...file, size: 31 * 1024 * 1024 }, "high"),
  ).rejects.toThrow(/30 MB/);
});
test("workspace routing recognizes baking aliases and unknown routes return a real material workspace", () => {
  for (const alias of ["bake", "Baking", "baking-studio", "BakeStudio"])
    expect(normalizeStudioWorkspace(alias)).toBe("baking");
  expect(normalizeStudioWorkspace("Pattern Studio")).toBe("pattern");
  expect(normalizeStudioWorkspace("unregistered-panel")).toBe("material");
});

test("all 25 registered baking nodes have typed, finite pixel evaluators", () => {
  const size = 16,
    coverage = new Uint8Array(size).fill(1),
    charts = new Uint32Array(size).fill(1),
    maps = {};
  for (const [key, descriptor] of Object.entries(MESH_MAPS)) {
    const data = new Float32Array(size * 4);
    for (let i = 0; i < size; i++) {
      data[i * 4] = descriptor.type === "normal" ? 0.5 : i / size;
      data[i * 4 + 1] = 0.5;
      data[i * 4 + 2] = descriptor.type === "normal" ? 1 : 0.75;
      data[i * 4 + 3] = 1;
    }
    maps[key] = { type: descriptor.type, data };
  }
  const sourceFor = {
    scalar: "height",
    color: "base-color",
    vector: "position",
    normal: "normal",
    sample: "height",
    vectorLike: "position",
  };
  for (const [type, definition] of Object.entries(BAKE_NODES)) {
    const nodes = [],
      inputs = [];
    definition.inputs.forEach((expected, i) => {
      const map = sourceFor[expected] || "normal",
        id = `source-${i}`;
      nodes.push(bakeNode("MeshMap", { id, params: { map } }));
      inputs.push(id);
    });
    const node = bakeNode(type, { id: "node", inputs });
    nodes.push(node);
    const outputType = ["MeshMap", "Output"].includes(type)
      ? MESH_MAPS[node.params.map].type
      : definition.output === "inherit"
        ? "scalar"
        : definition.output;
    const target =
      {
        scalar: "height",
        color: "base-color",
        vector: "position",
        normal: "normal",
      }[outputType] || "normal";
    if (type !== "Output")
      nodes.push(
        bakeNode("Output", {
          id: "final",
          params: { map: target },
          inputs: ["node"],
        }),
      );
    const result = evaluateBakeGraph(
      { schema: GRAPH_SCHEMA, nodes },
      {
        width: 4,
        height: 4,
        coverage,
        charts,
        maps,
        bevelFields: { node: maps.normal },
      },
      [target],
    );
    expect(result[target].data).toHaveLength(size * 4);
    expect([...result[target].data].every(Number.isFinite)).toBe(true);
  }
});

test("cage projection rejects a nearer back surface instead of baking backwards tangent normals", async () => {
  const front = plane(0),
    back = plane(0.06);
  for (let i = 2; i < back.normals.length; i += 3) back.normals[i] = -1;
  const count = front.positions.length / 3;
  const high = {
    positions: new Float32Array([...back.positions, ...front.positions]),
    normals: new Float32Array([...back.normals, ...front.normals]),
    indices: new Uint32Array([
      ...back.indices,
      ...Array.from(front.indices, (i) => i + count),
    ]),
  };
  const result = await bakeMeshMaps(
    plane(),
    high,
    { resolution: 64, front: 0.1, back: 0.1, channels: ["normal", "height"] },
    null,
  );
  expect(result.stats.misses).toBe(0);
  expect(result.stats.backfacesRejected).toBeGreaterThan(0);
  for (let i = 0; i < 64 ** 2; i++) {
    expect(result.maps.normal.data[i * 4 + 2]).toBeCloseTo(1, 6);
    expect(result.maps.height.data[i * 4]).toBeCloseTo(0.5, 6);
  }
});
test("nearest projection honors the explicit bound even with a root-leaf BVH, and thickness saturates rather than becoming a false hole", async () => {
  const miss = await bakeMeshMaps(
    plane(),
    plane(2),
    {
      resolution: 64,
      projection: "nearest",
      front: 0.1,
      back: 0.1,
      channels: ["height", "alpha"],
    },
    null,
  );
  expect(miss.stats.hits).toBe(0);
  expect(miss.maps.alpha.data[0]).toBe(0);
  const assets = createDemoBakeAssets("Cube"),
    result = await bakeMeshMaps(
      bakeMeshData(assets.low[0].geometry),
      mergeBakeMeshData(assets.high),
      { resolution: 64, thicknessRange: 0.5, channels: ["thickness"] },
      null,
    );
  for (let i = 0; i < 64 ** 2; i++)
    if (result.coverage[i])
      expect(result.maps.thickness.data[i * 4]).toBeCloseTo(1, 6);
});

test("normal-ray projection chooses the facing intersection nearest the low surface, not a farther lid near the cage", async () => {
  const own = plane(0),
    other = plane(0.09),
    count = own.positions.length / 3;
  const high = {
    positions: new Float32Array([...other.positions, ...own.positions]),
    normals: new Float32Array([...other.normals, ...own.normals]),
    indices: new Uint32Array([
      ...other.indices,
      ...Array.from(own.indices, (i) => i + count),
    ]),
  };
  const result = await bakeMeshMaps(
    plane(),
    high,
    { resolution: 64, front: 0.15, back: 0.15, channels: ["normal", "height"] },
    null,
  );
  expect(result.stats.misses).toBe(0);
  for (let i = 0; i < 64 ** 2; i++)
    expect(result.maps.height.data[i * 4]).toBeCloseTo(0.5, 6);
});
