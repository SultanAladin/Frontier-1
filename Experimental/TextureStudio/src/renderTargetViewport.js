// WebGLRenderer.setViewport takes LOGICAL pixels and multiplies by its DPR,
// even for an off-screen framebuffer. Render-target dimensions are already
// PHYSICAL pixels. Applying DPR twice crops/rescales the surface relative to
// ray picking, and also moves/crops strokes in packed UV captures.
export function setRenderTargetViewport(renderer, width, height) {
  const ratio = renderer.getPixelRatio?.() || 1;
  renderer.setViewport(0, 0, width / ratio, height / ratio);
}
