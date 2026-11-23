import {
  bakeVolumeSdf,
  volumeSlicePixels,
  volumeSdfManifest,
} from "./volumeSdf.js";
self.onmessage = async ({ data }) => {
  const { id, high, settings, sources } = data;
  try {
    const result = await bakeVolumeSdf(high, settings, {
      onProgress: (p) => self.postMessage({ id, type: "progress", ...p }),
    });
    const slices = volumeSlicePixels(result),
      manifest = volumeSdfManifest(result, sources);
    self.postMessage(
      {
        id,
        type: "result",
        distances: result.distances,
        confidence: result.confidence,
        manifest,
        slices,
      },
      [
        result.distances.buffer,
        result.confidence.buffer,
        ...Object.values(slices.pixels).map((p) => p.buffer),
      ],
    );
  } catch (e) {
    self.postMessage({ id, type: "error", message: e.message });
  }
};
