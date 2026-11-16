// Off-main-thread Web Worker for Sparse 8x8x8 SDF Volume baking.
import { compileSparseSdfVolume, evictMip0ForBudget, sdfVolumeTransferables } from './SparseSdfBaker.js';

self.onmessage = (event) => {
  const { ticket, request } = event.data;
  try {
    const report = (fraction, label) => self.postMessage({ ticket, type: 'progress', fraction, label });
    const volume = compileSparseSdfVolume(
      { positions: request.positions, indices: request.indices, closed: request.closed },
      {
        gridDim: request.gridDim,
        paddingFraction: request.paddingFraction,
        narrowBandMult: request.narrowBandMult,
      },
      report,
    );
    const eviction = request.vramBudgetBytes
      ? evictMip0ForBudget(volume, request.vramBudgetBytes)
      : { evicted: false, freedBytes: 0 };
    volume.lodLevel = request.lodLevel ?? 0;
    self.postMessage(
      { ticket, type: 'done', volume, eviction },
      sdfVolumeTransferables(volume),
    );
  } catch (error) {
    self.postMessage({ ticket, type: 'error', message: String(error && error.message || error) });
  }
};
