// Run context for the Geometry Compile steps, shared by both workspaces: the SDF and cluster-LOD workers plus the
// offscreen icon baker. The Component Viewer uses it headlessly (the ControlVehicle sample compiles at boot); the
// Geometry Compiler uses it for every step it runs.
import { workerChannel } from './CompileSteps.js';
import { IconRenderer, meshObject, assetSlots } from '../Studio/Render.js';

export function createCompileContext() {
  const iconRenderer = new IconRenderer();
  const ctx = {
    sdfWorker: workerChannel(new URL('./Engine/SdfWorker.js', import.meta.url)),
    lodWorker: workerChannel(new URL('./Engine/LodWorker.js', import.meta.url)),
    async renderIcon(asset) {
      const s = asset.settings.icon;
      const channel = s.shading === 'normals' ? 'normals' : 'lit';
      const overrides = s.shading === 'clay' ? Object.fromEntries(assetSlots(asset).map((x) => [x.family, '#c3c7cf'])) : {};
      return iconRenderer.render(meshObject(asset, { channel, overrides, castShadow: false }), s);
    },
  };
  return { ctx, iconRenderer };
}
