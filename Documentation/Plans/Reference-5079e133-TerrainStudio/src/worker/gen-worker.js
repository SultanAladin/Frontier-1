import { generateRockFormation } from '../geology/generator.js';

function holdPreviewFrame(params) {
  const delayMs = Math.max(
    0,
    Math.min(1500, Number(params?.erosionPreviewDelayMs ?? params?.liveErosionPreviewDelayMs ?? 380) || 0)
  );
  if (delayMs <= 0) return;
  // The generator is intentionally synchronous/deterministic. When the user
  // opts into watching erosion, briefly hold in this dedicated worker after
  // posting a frame so the main-thread viewport can actually present it
  // before the next SDF checkpoint replaces it. This never blocks the UI.
  const until = performance.now() + delayMs;
  while (performance.now() < until) {
    // Dedicated-worker pacing for the opt-in live preview.
  }
}

self.onmessage = (event) => {
  const { id, params } = event.data;
  try {
    const livePreview = params?.liveErosionPreview === true || params?.erosionLivePreview === true;
    const result = generateRockFormation(
      params,
      (progress, stage) => {
        self.postMessage({ type: 'progress', id, progress, stage });
      },
      livePreview
        ? (preview) => {
            const mesh = preview.mesh;
            if (!mesh) return;
            const transferList = [
              mesh.positions.buffer,
              mesh.normals.buffer,
              mesh.uvs.buffer,
              mesh.colors.buffer,
              mesh.indices.buffer,
            ];
            self.postMessage(
              {
                type: 'erosion-preview',
                id,
                fraction: preview.fraction,
                stage: preview.stage,
                process: preview.process,
                processFraction: preview.processFraction,
                lithology: preview.lithology,
                dimensions: preview.dimensions,
                triangles: preview.triangles,
                mesh: {
                  positions: mesh.positions,
                  normals: mesh.normals,
                  uvs: mesh.uvs,
                  colors: mesh.colors,
                  albedos: null,
                  stains: null,
                  hasCustomAlbedo: false,
                  indices: mesh.indices,
                },
              },
              transferList
            );
            holdPreviewFrame(params);
          }
        : null
    );

    const { mesh, stats, lithology } = result;
    const transferList = [
      mesh.positions.buffer,
      mesh.normals.buffer,
      mesh.uvs.buffer,
      mesh.colors.buffer,
      mesh.indices.buffer,
    ];
    if (mesh.albedos) transferList.push(mesh.albedos.buffer);
    if (mesh.stains) transferList.push(mesh.stains.buffer);

    self.postMessage(
      {
        type: 'done',
        id,
        params: result.params,
        lithology,
        stats,
        mesh: {
          positions: mesh.positions,
          normals: mesh.normals,
          uvs: mesh.uvs,
          colors: mesh.colors,
          albedos: mesh.albedos || null,
          stains: mesh.stains || null,
          hasCustomAlbedo: !!mesh.hasCustomAlbedo,
          indices: mesh.indices,
        },
      },
      transferList
    );
  } catch (err) {
    self.postMessage({
      type: 'error',
      id,
      message: err?.message || String(err),
    });
  }
};
