import {
  bakeMeshMaps,
  encodeBakeMap,
  dilateBakePixels,
} from "./meshBakeCore.js";
import { MESH_MAPS } from "./bakeGraph.js";
self.onmessage = async (event) => {
  const { id, targets, high, highByTarget, settings, graph } = event.data;
  try {
    for (let index = 0; index < targets.length; index++) {
      const target = targets[index];
      const result = await bakeMeshMaps(
        target.data,
        highByTarget?.[target.name] || high,
        settings,
        graph,
        {
          onProgress: (p) =>
            self.postMessage({
              id,
              type: "progress",
              ...p,
              set: index + 1,
              sets: targets.length,
              name: target.name,
            }),
        },
      );
      const maps = {};
      for (const [name, field] of Object.entries(result.maps)) {
        const pixels = encodeBakeMap(
          field,
          result.settings.resolution,
          result.settings.resolution,
          { ...result.settings, colorSpace: MESH_MAPS[name].colorSpace },
          result.coverage,
        );
        maps[name] = ["coverage", "alpha"].includes(name)
          ? pixels
          : dilateBakePixels(
              pixels,
              result.settings.resolution,
              result.settings.resolution,
              result.coverage,
              result.settings.padding,
            );
      }
      self.postMessage(
        {
          id,
          type: "result",
          name: target.name,
          index,
          maps,
          stats: result.stats,
          settings: result.settings,
          bounds: result.bounds,
          chartPalette: result.chartPalette,
        },
        Object.values(maps).map((p) => p.buffer),
      );
    }
    self.postMessage({ id, type: "complete" });
  } catch (error) {
    self.postMessage({
      id,
      type: "error",
      message: error.message || "Mesh baking failed.",
    });
  }
};
