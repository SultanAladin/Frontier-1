import BakeWorker from "./meshBakeWorker.js?worker&inline";
import { zipSync, strToU8 } from "fflate";
import { bakeMeshData, mergeBakeMeshData } from "./bakeMeshes.js";
import { MESH_MAPS, validateBakeGraph } from "./bakeGraph.js";
import { validateMeshBakeSettings } from "./meshBakeCore.js";
const aborted = () => new DOMException("Bake cancelled", "AbortError");
const canonical = (name) =>
  name.toLowerCase().replace(/(?:[_. -](?:low|high|lp|hp))$/, "");
export function startMeshBake(
  lowParts,
  highParts,
  inputSettings,
  inputGraph,
  { signal, onProgress = () => {}, onResult = () => {} } = {},
) {
  if (!lowParts.length || !highParts.length)
    return Promise.reject(
      new Error("Choose low UV targets and a high source mesh."),
    );
  const settings = validateMeshBakeSettings(inputSettings),
    graph = inputGraph ? validateBakeGraph(inputGraph) : null;
  if (
    lowParts.length * settings.resolution ** 2 * settings.channels.length * 4 >
    160 * 1024 * 1024
  )
    return Promise.reject(
      new Error(
        "This batch exceeds the 160 MB result-buffer budget. Lower resolution, reduce maps, or bake fewer objects at once.",
      ),
    );
  if (lowParts.length > 16)
    return Promise.reject(
      new Error("A batch supports up to 16 low mesh objects."),
    );
  const targets = lowParts.map((p) => ({
      name: p.name,
      data: bakeMeshData(p.geometry),
    })),
    high = mergeBakeMeshData(highParts),
    highByTarget = {};
  if (settings.matchByName)
    for (const target of targets) {
      const matches = highParts.filter(
        (p) => canonical(p.name) === canonical(target.name),
      );
      if (!matches.length)
        return Promise.reject(
          new Error(
            `No high mesh matches ${target.name}. Disable name matching or rename the corresponding high object.`,
          ),
        );
      highByTarget[target.name] = mergeBakeMeshData(
        matches,
        matches.map((p) => highParts.indexOf(p)),
      );
    }
  if (signal?.aborted) return Promise.reject(aborted());
  return new Promise((resolve, reject) => {
    let worker,
      ended = false;
    const id = globalThis.crypto?.randomUUID?.() || String(Date.now()),
      results = [];
    const finish = (error) => {
      if (ended) return;
      ended = true;
      worker?.terminate();
      signal?.removeEventListener("abort", cancel);
      error ? reject(error) : resolve(results);
    };
    const cancel = () => finish(aborted());
    try {
      worker = new BakeWorker();
      worker.onerror = (e) =>
        finish(
          new Error(
            e.message ||
              "The mesh worker could not start. Check browser worker support.",
          ),
        );
      worker.onmessage = (e) => {
        const data = e.data;
        if (data.id !== id || ended) return;
        if (data.type === "progress") onProgress(data);
        else if (data.type === "result") {
          results.push(data);
          onResult(data);
        } else if (data.type === "error") finish(new Error(data.message));
        else if (data.type === "complete") finish();
      };
      signal?.addEventListener("abort", cancel, { once: true });
      const message = { id, targets, high, highByTarget, settings, graph },
        buffers = new Set();
      const visit = (v) => {
        if (ArrayBuffer.isView(v)) {
          buffers.add(v.buffer);
          return;
        }
        if (v && typeof v === "object") Object.values(v).forEach(visit);
      };
      visit(message);
      worker.postMessage(message, [...buffers]);
    } catch (e) {
      finish(e);
    }
  });
}
export async function bakePixelsPNG(pixels, resolution) {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = resolution;
  const context = canvas.getContext("2d");
  if (!context) throw new Error("PNG encoding requires a 2D canvas.");
  context.putImageData(new ImageData(pixels, resolution, resolution), 0, 0);
  const blob = await new Promise((resolve, reject) =>
    canvas.toBlob(
      (blob) =>
        blob ? resolve(blob) : reject(new Error("PNG encoding failed.")),
      "image/png",
    ),
  );
  canvas.width = canvas.height = 1;
  return blob;
}
export async function packageMeshBake(
  results,
  graph,
  sources,
  { signal, onProgress = () => {} } = {},
) {
  const files = {},
    sets = [],
    previews = [];
  let done = 0,
    total = results.reduce((n, r) => n + Object.keys(r.maps).length, 0);
  try {
    for (const result of results) {
      const folder = `${String(result.index + 1).padStart(2, "0")}-${result.name.replace(/[^a-z0-9_-]/gi, "-").slice(0, 70) || "Mesh"}`,
        set = {
          name: result.name,
          folder,
          resolution: result.settings.resolution,
          stats: result.stats,
          bounds: result.bounds,
          chartPalette: result.chartPalette,
          channels: {},
        };
      for (const [name, pixels] of Object.entries(result.maps)) {
        if (signal?.aborted) throw aborted();
        onProgress({
          phase: "Encoding PNG maps",
          done,
          total,
          name: result.name,
        });
        const blob = await bakePixelsPNG(pixels, result.settings.resolution);
        if (signal?.aborted) throw aborted();
        files[`${folder}/${name}.png`] = new Uint8Array(
          await blob.arrayBuffer(),
        );
        previews.push({
          set: result.name,
          key: name,
          url: URL.createObjectURL(blob),
          resolution: result.settings.resolution,
        });
        set.channels[name] = {
          ...MESH_MAPS[name],
          normalConvention:
            MESH_MAPS[name].space === "tangent"
              ? result.settings.normalY === "+Y"
                ? "OpenGL +Y"
                : "DirectX −Y"
              : undefined,
        };
        done++;
      }
      sets.push(set);
    }
    const manifest = {
      schema: "alloy.mesh-bake.v1",
      engine: "Three.js / CPU BVH projection worker",
      settings: results[0]?.settings,
      ...(graph ? { graph: validateBakeGraph(graph) } : {}),
      workflow: graph ? "legacy-graph" : "mesh-maps",
      sources,
      objectPalette: sources.high?.map((part, index) => ({
        name: part.name,
        id: index + 1,
        rgb: [
          (index + 1) & 255,
          ((index + 1) >>> 8) & 255,
          ((index + 1) >>> 16) & 255,
        ],
      })),
      sets,
      limitations: [
        "Static triangle geometry. OBJ, self-contained GLB/glTF, PLY and high-only STL are supported; no FBX, USD, animation, UDIM or compressed glTF decoding.",
        "Each low mesh object is a separate texture set and must have non-overlapping 0–1 UVs. No automatic unwrap is applied to imported meshes.",
        "Tangent normals use the low triangle UV derivative frame, not a certified MikkTSpace interchange implementation. OpenGL/DirectX green-channel convention is explicit.",
        "Bevel is geometry-derived normal averaging, not a topology modifier or exact Blender Cycles Bevel implementation. Curvature is a signed local normal-variation estimate.",
        "Ray projection selects the eligible intersection nearest the original low surface, ignores high normals facing away from the low target and uses a bounded oriented nearest-point fallback when a ray misses. Backface rejections and fallback counts are reported. Remaining misses use neutral low normals/zero height. Thickness searches for the inward opposite surface without treating distances beyond thicknessRange as holes; farther hits saturate white. Open meshes can still have no inward hit.",
        "PNG channels are 8-bit. Height encodes distance = (sample − 0.5) × 2 × max(front, back), in source scene units. Thickness = sample × thicknessRange.",
        "AO uses a cosine-weighted low-discrepancy hemisphere with world-position rotation, not baked lighting. Vertex color does not transfer high-material textures. Dust, dirt, edge-wear and bevel masks are geometry-derived approximations, not physical weathering. ID values encode (part ID + 1) as 24-bit little-endian RGB.",
        "Object normals use the low target original local axes; world normals use imported scene axes. Bent normals average unoccluded directions using AO distance. Vector displacement stores world high-minus-low offset using the same signed range as height.",
        "UV island colors are deterministic and distinct per connected low chart; chartPalette records their 8-bit RGB IDs. Wireframe includes low triangle edges. Alpha / hit mask encodes projection hits, not material transparency.",
        "Padding extends RGB into uncovered texels; alpha remains zero outside real coverage. Sources are session-local and are not embedded in recipe-only JSON.",
      ],
    };
    files["manifest.json"] = strToU8(JSON.stringify(manifest, null, 2));
    files["README.txt"] = strToU8(
      "ALLOY MESH MAP BAKE\n\n" + manifest.limitations.join("\n\n"),
    );
    onProgress({ phase: "ZIP ready", done: total, total });
    return { bytes: zipSync(files, { level: 0 }), manifest, previews };
  } catch (e) {
    previews.forEach((p) => URL.revokeObjectURL(p.url));
    throw e;
  }
}
