import { createMaskCoverageTexture } from "./maskPaintRaster.js";
import { createMaskCoverageGpu } from "./maskPaintGpu.js";

// Mask coverage with a WebGL2 renderer runs the whole chain (stroke
// deposition, field baking, owner-field multiply) in render targets; see
// maskPaintGpu.js. Without one — or when GPU construction fails — the CPU
// chain in maskPaintRaster.js serves the same uniforms contract. Kept in
// its own module so maskPaintRaster.js never imports the GPU chain (the
// GPU modules import its geometry cache and session keys).
export function createMaskCoverage(geometry, renderer = null) {
  if (renderer) {
    try {
      return createMaskCoverageGpu(renderer, geometry);
    } catch (error) {
      console.warn(
        "Mask GPU pipeline unavailable; using CPU mask raster.",
        error,
      );
    }
  }
  return createMaskCoverageTexture(geometry);
}
