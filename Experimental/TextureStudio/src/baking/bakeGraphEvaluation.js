import {
  BAKE_NODES,
  MESH_MAPS,
  graphNodeType,
  reachableBakeNodes,
  validateBakeGraph,
} from "./bakeGraph.js";
const clamp = (v, lo = 0, hi = 1) =>
  Math.max(lo, Math.min(hi, Number.isFinite(v) ? v : 0));
const srgb = (c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const rgb = (hex) =>
  [1, 3, 5].map((i) => srgb(parseInt(hex.slice(i, i + 2), 16) / 255));
const hash = (x, y, z, seed) => {
  let n =
    Math.imul(x | 0, 374761393) ^
    Math.imul(y | 0, 668265263) ^
    Math.imul(z | 0, 2147483647) ^
    Math.imul(seed | 0, 1274126177);
  n = Math.imul(n ^ (n >>> 13), 1274126177);
  return ((n ^ (n >>> 16)) >>> 0) / 4294967295;
};
function noise(x, y, z, seed) {
  const X = Math.floor(x),
    Y = Math.floor(y),
    Z = Math.floor(z),
    f = [x - X, y - Y, z - Z].map((t) => t * t * (3 - 2 * t));
  let total = 0;
  for (let a = 0; a < 2; a++)
    for (let b = 0; b < 2; b++)
      for (let c = 0; c < 2; c++)
        total +=
          hash(X + a, Y + b, Z + c, seed) *
          (a ? f[0] : 1 - f[0]) *
          (b ? f[1] : 1 - f[1]) *
          (c ? f[2] : 1 - f[2]);
  return total;
}
function voronoi(x, y, z, seed) {
  const X = Math.floor(x),
    Y = Math.floor(y),
    Z = Math.floor(z);
  let best = 3;
  for (let a = -1; a <= 1; a++)
    for (let b = -1; b <= 1; b++)
      for (let c = -1; c <= 1; c++) {
        const A = X + a,
          B = Y + b,
          C = Z + c;
        best = Math.min(
          best,
          Math.hypot(
            x - A - hash(A, B, C, seed),
            y - B - hash(A, B, C, seed + 7),
            z - C - hash(A, B, C, seed + 13),
          ),
        );
      }
  return clamp(best / 1.3);
}
function normalized(x, y, z) {
  const length = Math.hypot(x, y, z);
  return length > 1e-9 ? [x / length, y / length, z / length] : [0, 0, 1];
}

export function evaluateBakeGraph(input, context, channels) {
  const graph = validateBakeGraph(input),
    nodes = new Map(graph.nodes.map((n) => [n.id, n]));
  const { width, height, coverage, charts, maps, bevelFields = {} } = context;
  const size = width * height,
    cache = new Map();
  const field = (type) => ({ type, data: new Float32Array(size * 4) });
  function evaluate(id) {
    if (cache.has(id)) return cache.get(id);
    const n = nodes.get(id),
      p = n.params,
      type = graphNodeType(graph, id);
    if (n.type === "MeshMap") {
      const result = maps[p.map];
      if (!result)
        throw new Error(`${MESH_MAPS[p.map].label} was not evaluated.`);
      cache.set(id, result);
      return result;
    }
    if (n.type === "Bevel") {
      const result = bevelFields[id] || maps.normal;
      if (!result) throw new Error("The bevel-normal field was not evaluated.");
      cache.set(id, result);
      return result;
    }
    if (n.type === "Output") {
      if (!n.inputs[0])
        throw new Error(
          `Connect the ${MESH_MAPS[p.map].label} output before baking.`,
        );
      const result = evaluate(n.inputs[0]);
      cache.set(id, result);
      return result;
    }
    const inputs = n.inputs.map((id) => (id ? evaluate(id) : null)),
      result = field(type),
      out = result.data;
    const value = (slot, i, c, fallback = 0.5) =>
      inputs[slot]?.data[i * 4 + c] ?? fallback;
    const colors =
      n.type === "Color"
        ? [rgb(p.color)]
        : n.type === "ColorRamp"
          ? [rgb(p.low), rgb(p.high)]
          : null;
    if (n.type === "Blur") {
      const source = inputs[0];
      if (!source) throw new Error("Connect an input to Coverage-aware blur.");
      const radius = Math.ceil(p.radius),
        temp = new Float32Array(out.length);
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          const i = y * width + x;
          if (!coverage[i]) continue;
          let count = 0;
          for (let t = -radius; t <= radius; t++) {
            const X = x + t,
              j = y * width + X;
            if (X < 0 || X >= width || !coverage[j] || charts[j] !== charts[i])
              continue;
            count++;
            for (let c = 0; c < 3; c++)
              temp[i * 4 + c] += source.data[j * 4 + c];
          }
          for (let c = 0; c < 3; c++) temp[i * 4 + c] /= count || 1;
        }
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          const i = y * width + x;
          if (!coverage[i]) continue;
          let count = 0;
          for (let t = -radius; t <= radius; t++) {
            const Y = y + t,
              j = Y * width + x;
            if (Y < 0 || Y >= height || !coverage[j] || charts[j] !== charts[i])
              continue;
            count++;
            for (let c = 0; c < 3; c++) out[i * 4 + c] += temp[j * 4 + c];
          }
          for (let c = 0; c < 3; c++) out[i * 4 + c] /= count || 1;
          out[i * 4 + 3] = 1;
        }
    } else
      for (let i = 0; i < size; i++) {
        if (!coverage[i]) continue;
        const x = i % width,
          y = Math.floor(i / width),
          u = (x + 0.5) / width,
          v = 1 - (y + 0.5) / height;
        let values = [0.5, 0.5, 0.5];
        switch (n.type) {
          case "Value":
            values = [p.value, p.value, p.value];
            break;
          case "Color":
            values = colors[0];
            break;
          case "Noise": {
            const q = inputs[0]
              ? [value(0, i, 0), value(0, i, 1), value(0, i, 2)]
              : [u, v, 0];
            let f = p.scale,
              sum = 0,
              amplitude = 1,
              total = 0;
            for (let k = 0; k < p.detail; k++) {
              sum +=
                noise(q[0] * f, q[1] * f, q[2] * f, p.seed + k) * amplitude;
              total += amplitude;
              amplitude *= 0.5;
              f *= 2;
            }
            values.fill(sum / total);
            break;
          }
          case "Voronoi": {
            const q = inputs[0]
              ? [value(0, i, 0), value(0, i, 1), value(0, i, 2)]
              : [u, v, 0];
            values.fill(
              voronoi(q[0] * p.scale, q[1] * p.scale, q[2] * p.scale, p.seed),
            );
            break;
          }
          case "Checker": {
            const q = inputs[0]
              ? [value(0, i, 0), value(0, i, 1), value(0, i, 2)]
              : [u, v, 0];
            values.fill(
              (Math.floor(q[0] * p.scale) +
                Math.floor(q[1] * p.scale) +
                Math.floor(q[2] * p.scale)) &
                1,
            );
            break;
          }
          case "Gradient": {
            const q = inputs[0]
              ? [value(0, i, 0), value(0, i, 1), value(0, i, 2)]
              : [u, v, 0];
            values.fill(
              p.axis === "Radial"
                ? clamp(Math.hypot(q[0] - 0.5, q[1] - 0.5) * Math.SQRT2)
                : q[p.axis === "X" ? 0 : 1],
            );
            break;
          }
          case "Math": {
            const a = value(0, i, 0),
              b = value(1, i, 0, p.value);
            const operations = {
              Add: () => a + b,
              Subtract: () => a - b,
              Multiply: () => a * b,
              Divide: () => (Math.abs(b) > 1e-9 ? a / b : 0),
              Min: () => Math.min(a, b),
              Max: () => Math.max(a, b),
              Power: () => Math.pow(Math.max(a, 0), b),
              Absolute: () => Math.abs(a),
              Sine: () => Math.sin(a),
            };
            values.fill(operations[p.operation]());
            break;
          }
          case "Invert":
            values = values.map((_, c) => 1 - value(0, i, c));
            break;
          case "Clamp":
            values = values.map((_, c) =>
              clamp(
                value(0, i, c),
                Math.min(p.min, p.max),
                Math.max(p.min, p.max),
              ),
            );
            break;
          case "MapRange": {
            const t =
              (value(0, i, 0) - p.fromMin) /
              (Math.abs(p.fromMax - p.fromMin) > 1e-9
                ? p.fromMax - p.fromMin
                : 1);
            values.fill(p.toMin + t * (p.toMax - p.toMin));
            break;
          }
          case "Gamma":
            values = values.map(
              (_, c) => Math.max(0, value(0, i, c)) ** (1 / p.gamma),
            );
            break;
          case "Contrast":
            values = values.map(
              (_, c) =>
                (value(0, i, c) - 0.5) * (1 + p.contrast) + 0.5 + p.brightness,
            );
            break;
          case "Threshold":
            values.fill(value(0, i, 0) >= p.threshold ? 1 : 0);
            break;
          case "ColorRamp": {
            const t = clamp(value(0, i, 0));
            values = colors[0].map((v, c) => v * (1 - t) + colors[1][c] * t);
            break;
          }
          case "Mix": {
            const t = clamp(value(2, i, 0, p.factor));
            values = values.map(
              (_, c) => value(0, i, c, 0) * (1 - t) + value(1, i, c, 1) * t,
            );
            break;
          }
          case "Luminance":
            values.fill(
              value(0, i, 0) * 0.2126 +
                value(0, i, 1) * 0.7152 +
                value(0, i, 2) * 0.0722,
            );
            break;
          case "Separate":
            values.fill(value(0, i, ["X", "Y", "Z"].indexOf(p.component)));
            break;
          case "Combine":
            values = [value(0, i, 0), value(1, i, 0), value(2, i, 0)];
            break;
          case "NormalFromHeight": {
            const neighbor = (X, Y) => {
              if (X < 0 || X >= width || Y < 0 || Y >= height)
                return value(0, i, 0);
              const j = Y * width + X;
              return coverage[j] && charts[j] === charts[i]
                ? value(0, j, 0)
                : value(0, i, 0);
            };
            const dx = (neighbor(x + 1, y) - neighbor(x - 1, y)) * p.strength,
              dy = (neighbor(x, y - 1) - neighbor(x, y + 1)) * p.strength;
            values = normalized(-dx, -dy, 1).map((v) => v * 0.5 + 0.5);
            break;
          }
          case "NormalStrength":
            values = normalized(
              (value(0, i, 0, 0.5) * 2 - 1) * p.strength,
              (value(0, i, 1, 0.5) * 2 - 1) * p.strength,
              value(0, i, 2, 1) * 2 - 1,
            ).map((v) => v * 0.5 + 0.5);
            break;
          case "NormalMix": {
            const a = [0, 1, 2].map(
                (c) => value(0, i, c, c === 2 ? 1 : 0.5) * 2 - 1,
              ),
              b = [0, 1, 2].map(
                (c) => value(1, i, c, c === 2 ? 1 : 0.5) * 2 - 1,
              );
            values = normalized(
              ...a.map((v, c) => v * (1 - p.factor) + b[c] * p.factor),
            ).map((v) => v * 0.5 + 0.5);
            break;
          }
          default:
            throw new Error(
              `The ${BAKE_NODES[n.type].label} evaluator is unavailable.`,
            );
        }
        for (let c = 0; c < 3; c++)
          out[i * 4 + c] = Number.isFinite(values[c]) ? values[c] : 0;
        out[i * 4 + 3] = 1;
      }
    cache.set(id, result);
    return result;
  }
  const results = {};
  for (const key of channels) {
    const output = graph.nodes.find(
      (n) => n.type === "Output" && n.params.map === key,
    );
    results[key] = output ? evaluate(output.id) : maps[key];
    if (!results[key])
      throw new Error(`The ${MESH_MAPS[key].label} map is unavailable.`);
  }
  return results;
}
export function requiredBakeMaps(graph, channels) {
  const active = reachableBakeNodes(graph, channels),
    maps = new Set(channels);
  active.forEach((n) => {
    if (n.type === "MeshMap") maps.add(n.params.map);
    if (n.type === "Bevel") maps.add("normal");
  });
  return { maps: [...maps], bevels: active.filter((n) => n.type === "Bevel") };
}
