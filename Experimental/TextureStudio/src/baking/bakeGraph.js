// A typed, serializable image/mesh-map graph. No source code or eval is accepted.
import { MESH_MAPS } from "./meshMaps.js";
export { MESH_MAPS } from "./meshMaps.js";
export const BAKE_NODES = {
  MeshMap: {
    label: "Mesh map",
    group: "Sources",
    inputs: [],
    output: "dynamic",
    defaults: { map: "normal" },
  },
  Bevel: {
    label: "Bevel normals",
    group: "Normals",
    inputs: [],
    output: "normal",
    defaults: { radius: 0.025, samples: 8 },
    note: "Geometry-derived normal averaging within a scene-unit radius. A shading approximation, not geometry beveling or exact Cycles parity.",
  },
  Value: {
    label: "Value",
    group: "Sources",
    inputs: [],
    output: "scalar",
    defaults: { value: 0.5 },
  },
  Color: {
    label: "Color",
    group: "Sources",
    inputs: [],
    output: "color",
    defaults: { color: "#a9ada3" },
  },
  Noise: {
    label: "Fractal noise",
    group: "Textures",
    inputs: ["vector"],
    output: "scalar",
    defaults: { scale: 8, detail: 3, seed: 1 },
  },
  Voronoi: {
    label: "Voronoi distance",
    group: "Textures",
    inputs: ["vector"],
    output: "scalar",
    defaults: { scale: 8, seed: 1 },
  },
  Checker: {
    label: "Checker",
    group: "Textures",
    inputs: ["vector"],
    output: "scalar",
    defaults: { scale: 8 },
  },
  Gradient: {
    label: "Gradient",
    group: "Textures",
    inputs: ["vector"],
    output: "scalar",
    defaults: { axis: "X" },
  },
  Math: {
    label: "Math",
    group: "Math",
    inputs: ["scalar", "scalar"],
    output: "scalar",
    defaults: { operation: "Multiply", value: 0.5 },
  },
  Invert: {
    label: "Invert",
    group: "Color / scalar",
    inputs: ["sample"],
    output: "inherit",
    defaults: {},
  },
  Clamp: {
    label: "Clamp",
    group: "Color / scalar",
    inputs: ["sample"],
    output: "inherit",
    defaults: { min: 0, max: 1 },
  },
  MapRange: {
    label: "Map range",
    group: "Math",
    inputs: ["scalar"],
    output: "scalar",
    defaults: { fromMin: 0, fromMax: 1, toMin: 0, toMax: 1 },
  },
  Gamma: {
    label: "Gamma",
    group: "Color / scalar",
    inputs: ["sample"],
    output: "inherit",
    defaults: { gamma: 1 },
  },
  Contrast: {
    label: "Brightness / contrast",
    group: "Color / scalar",
    inputs: ["sample"],
    output: "inherit",
    defaults: { brightness: 0, contrast: 0 },
  },
  Threshold: {
    label: "Threshold",
    group: "Math",
    inputs: ["scalar"],
    output: "scalar",
    defaults: { threshold: 0.5 },
  },
  ColorRamp: {
    label: "Color ramp",
    group: "Color / scalar",
    inputs: ["scalar"],
    output: "color",
    defaults: { low: "#161b21", high: "#e7d7b4" },
  },
  Mix: {
    label: "Mix color",
    group: "Color / scalar",
    inputs: ["color", "color", "scalar"],
    output: "color",
    defaults: { factor: 0.5 },
  },
  Luminance: {
    label: "Luminance",
    group: "Color / scalar",
    inputs: ["color"],
    output: "scalar",
    defaults: {},
  },
  Separate: {
    label: "Separate XYZ / RGB",
    group: "Vectors",
    inputs: ["vectorLike"],
    output: "scalar",
    defaults: { component: "X" },
  },
  Combine: {
    label: "Combine RGB",
    group: "Vectors",
    inputs: ["scalar", "scalar", "scalar"],
    output: "color",
    defaults: {},
  },
  Blur: {
    label: "Coverage-aware blur",
    group: "Filters",
    inputs: ["sample"],
    output: "inherit",
    defaults: { radius: 1 },
  },
  NormalFromHeight: {
    label: "Normal from height",
    group: "Normals",
    inputs: ["scalar"],
    output: "normal",
    defaults: { strength: 2 },
  },
  NormalStrength: {
    label: "Normal strength",
    group: "Normals",
    inputs: ["normal"],
    output: "normal",
    defaults: { strength: 1 },
  },
  NormalMix: {
    label: "Mix normals",
    group: "Normals",
    inputs: ["normal", "normal"],
    output: "normal",
    defaults: { factor: 0.5 },
  },
  Output: {
    label: "Map output",
    group: "Outputs",
    inputs: ["dynamic"],
    output: "dynamic",
    defaults: { map: "normal" },
  },
};
export const GRAPH_SCHEMA = "alloy.bake-graph.v1";
let serial = 0;
export function bakeNode(type, options = {}) {
  if (!Object.hasOwn(BAKE_NODES, type)) throw new Error("Unknown baking node.");
  return {
    id:
      globalThis.crypto?.randomUUID?.() ||
      `node-${Date.now().toString(36)}-${++serial}`,
    type,
    x: 50,
    y: 70,
    params: { ...BAKE_NODES[type].defaults },
    inputs: [],
    ...options,
  };
}
export function defaultBakeGraph() {
  return {
    schema: GRAPH_SCHEMA,
    nodes: [
      bakeNode("MeshMap", { id: "mesh-normal", x: 50, y: 75 }),
      bakeNode("Bevel", { id: "bevel-normal", x: 50, y: 220 }),
      bakeNode("Output", {
        id: "normal-output",
        x: 340,
        y: 200,
        inputs: ["bevel-normal"],
      }),
    ],
  };
}
const finite = (v, d, lo, hi) =>
  Number.isFinite(Number(v)) ? Math.max(lo, Math.min(hi, Number(v))) : d;
const color = (v, d) => (/^#[0-9a-f]{6}$/i.test(v || "") ? v.toLowerCase() : d);
export function normalizeBakeParams(type, input = {}) {
  const p = { ...BAKE_NODES[type].defaults };
  for (const key of Object.keys(p)) {
    const v = input[key];
    if (["color", "low", "high"].includes(key)) p[key] = color(v, p[key]);
    else if (key === "map") p[key] = Object.hasOwn(MESH_MAPS, v) ? v : p[key];
    else if (key === "operation")
      p[key] = [
        "Add",
        "Subtract",
        "Multiply",
        "Divide",
        "Min",
        "Max",
        "Power",
        "Absolute",
        "Sine",
      ].includes(v)
        ? v
        : p[key];
    else if (key === "axis")
      p[key] = ["X", "Y", "Radial"].includes(v) ? v : p[key];
    else if (key === "component")
      p[key] = ["X", "Y", "Z"].includes(v) ? v : p[key];
    else if (key === "samples") p[key] = Math.round(finite(v, p[key], 4, 16));
    else if (key === "detail") p[key] = Math.round(finite(v, p[key], 1, 6));
    else if (key === "seed") p[key] = Math.round(finite(v, p[key], 0, 100000));
    else if (key === "radius")
      p[key] = finite(v, p[key], 0, type === "Bevel" ? 1 : 8);
    else if (key === "scale") p[key] = finite(v, p[key], 0.01, 128);
    else if (key === "gamma") p[key] = finite(v, p[key], 0.05, 8);
    else if (key === "strength") p[key] = finite(v, p[key], 0, 16);
    else if (["factor", "threshold"].includes(key))
      p[key] = finite(v, p[key], 0, 1);
    else p[key] = finite(v, p[key], -16, 16);
  }
  return p;
}
export function validateBakeGraph(input) {
  if (
    !input ||
    input.schema !== GRAPH_SCHEMA ||
    !Array.isArray(input.nodes) ||
    input.nodes.length > 48
  )
    throw new Error("Choose a valid baking graph with up to 48 nodes.");
  const nodes = input.nodes.map((n) => {
    if (
      !n ||
      typeof n.type !== "string" ||
      !Object.hasOwn(BAKE_NODES, n.type) ||
      typeof n.id !== "string" ||
      !/^[\w-]{1,90}$/.test(n.id)
    )
      throw new Error("The graph contains an invalid node.");
    return {
      id: n.id,
      type: n.type,
      x: finite(n.x, 50, 0, 4000),
      y: finite(n.y, 70, 0, 4000),
      params: normalizeBakeParams(n.type, n.params),
      inputs: Array.from(
        { length: BAKE_NODES[n.type].inputs.length },
        (_, i) => (typeof n.inputs?.[i] === "string" ? n.inputs[i] : null),
      ),
    };
  });
  if (new Set(nodes.map((n) => n.id)).size !== nodes.length)
    throw new Error("Node identities must be unique.");
  const graph = { schema: GRAPH_SCHEMA, nodes },
    index = new Map(nodes.map((n) => [n.id, n]));
  const visiting = new Set(),
    visited = new Set();
  function visit(id) {
    if (visiting.has(id))
      throw new Error("Node graphs cannot contain a cycle.");
    if (visited.has(id)) return;
    const node = index.get(id);
    if (!node) throw new Error("A connection references a missing node.");
    visiting.add(id);
    node.inputs.filter(Boolean).forEach(visit);
    visiting.delete(id);
    visited.add(id);
  }
  nodes.forEach((n) => visit(n.id));
  const outputs = new Set();
  for (const n of nodes) {
    if (n.type === "Output") {
      if (outputs.has(n.params.map))
        throw new Error("Only one output node may write each map.");
      outputs.add(n.params.map);
    }
    n.inputs.forEach((id, i) => {
      if (!id) return;
      const source = graphNodeType(graph, id),
        expected = inputNodeType(graph, n, i);
      if (!acceptsGraphType(expected, source))
        throw new Error(
          `${BAKE_NODES[n.type].label} input ${i + 1} needs ${expected}, not ${source}.`,
        );
    });
  }
  return graph;
}
export function graphNodeType(graph, id, seen = new Set()) {
  const n = graph.nodes.find((n) => n.id === id);
  if (!n || seen.has(id)) return "scalar";
  seen.add(id);
  if (["MeshMap", "Output"].includes(n.type))
    return MESH_MAPS[n.params.map]?.type || "scalar";
  if (BAKE_NODES[n.type].output === "inherit")
    return n.inputs[0] ? graphNodeType(graph, n.inputs[0], seen) : "scalar";
  return BAKE_NODES[n.type].output;
}
export function inputNodeType(graph, node, slot) {
  return node.type === "Output"
    ? MESH_MAPS[node.params.map].type
    : BAKE_NODES[node.type].inputs[slot];
}
export function acceptsGraphType(expected, actual) {
  return (
    expected === actual ||
    (expected === "sample" && ["scalar", "color"].includes(actual)) ||
    (expected === "vectorLike" &&
      ["vector", "color", "normal"].includes(actual))
  );
}
export function connectBakeNode(graph, targetId, slot, sourceId) {
  const candidate = {
    ...graph,
    nodes: graph.nodes.map((n) =>
      n.id === targetId
        ? { ...n, inputs: n.inputs.map((v, i) => (i === slot ? sourceId : v)) }
        : n,
    ),
  };
  return validateBakeGraph(candidate);
}
export function reachableBakeNodes(graph, channels) {
  const ids = new Set(),
    index = new Map(graph.nodes.map((n) => [n.id, n]));
  function visit(n) {
    if (!n || ids.has(n.id)) return;
    ids.add(n.id);
    n.inputs.filter(Boolean).forEach((id) => visit(index.get(id)));
  }
  graph.nodes
    .filter((n) => n.type === "Output" && channels.includes(n.params.map))
    .forEach(visit);
  return graph.nodes.filter((n) => ids.has(n.id));
}
