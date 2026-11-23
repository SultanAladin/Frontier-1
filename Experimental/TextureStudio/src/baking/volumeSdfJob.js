import Worker from "./volumeSdfWorker.js?worker&inline";
import { zipSync, strToU8 } from "fflate";
import { mergeBakeMeshData } from "./bakeMeshes.js";
import { validateVolumeSdfSettings } from "./volumeSdf.js";
import { bakePixelsPNG } from "./bakeMeshJob.js";
export function startVolumeSdf(
  parts,
  input,
  { signal, onProgress = () => {} } = {},
) {
  if (!parts.length)
    return Promise.reject(
      new Error("Choose a high source mesh for the volume."),
    );
  const settings = validateVolumeSdfSettings(input),
    high = mergeBakeMeshData(parts),
    sources = parts.map((p) => ({
      name: p.name,
      triangles:
        (p.geometry.index?.count || p.geometry.attributes.position.count) / 3,
    }));
  if (signal?.aborted)
    return Promise.reject(new DOMException("SDF bake cancelled", "AbortError"));
  return new Promise((resolve, reject) => {
    const worker = new Worker(),
      id = crypto.randomUUID();
    let ended = false;
    const finish = (error, result) => {
      if (ended) return;
      ended = true;
      worker.terminate();
      signal?.removeEventListener("abort", cancel);
      error ? reject(error) : resolve(result);
    };
    const cancel = () =>
      finish(new DOMException("SDF bake cancelled", "AbortError"));
    signal?.addEventListener("abort", cancel, { once: true });
    worker.onerror = (e) =>
      finish(new Error(e.message || "SDF worker could not start."));
    worker.onmessage = ({ data }) => {
      if (data.id !== id || ended) return;
      if (data.type === "progress") onProgress(data);
      else if (data.type === "error") finish(new Error(data.message));
      else if (data.type === "result") finish(null, data);
    };
    worker.postMessage({ id, high, settings, sources }, [
      high.positions.buffer,
      high.normals.buffer,
      high.indices.buffer,
      high.colors.buffer,
      high.partIds.buffer,
    ]);
  });
}
export async function packageVolumeSdf(result, { signal } = {}) {
  const floats = new Uint8Array(result.distances.length * 4),
    view = new DataView(floats.buffer);
  for (let i = 0; i < result.distances.length; i++)
    view.setFloat32(i * 4, result.distances[i], true);
  const files = {
      "distance.f32": floats,
      "sign-confidence.u8": result.confidence,
    },
    previews = [];
  try {
    for (const [key, pixels] of Object.entries(result.slices.pixels)) {
      if (signal?.aborted)
        throw new DOMException("SDF bake cancelled", "AbortError");
      const blob = await bakePixelsPNG(pixels, result.slices.resolution);
      files[key + ".png"] = new Uint8Array(await blob.arrayBuffer());
      previews.push({
        set: "Volume SDF",
        key,
        url: URL.createObjectURL(blob),
        resolution: result.slices.resolution,
      });
    }
    const manifest = {
      ...result.manifest,
      slicePreviews: {
        range: result.slices.range,
        encoding: result.slices.encoding,
      },
    };
    files["manifest.json"] = strToU8(JSON.stringify(manifest, null, 2));
    files["README.txt"] = strToU8(
      "ALLOY 3D DISTANCE VOLUME\n\n" +
        manifest.algorithm +
        "\n\n" +
        manifest.limitations.join("\n\n") +
        "\n\nRead manifest.json before interpreting distance.f32. Texture-map resolution does not set volume dimensions.",
    );
    return { bytes: zipSync(files, { level: 0 }), manifest, previews };
  } catch (e) {
    previews.forEach((p) => URL.revokeObjectURL(p.url));
    throw e;
  }
}
