// Bakes a signed distance field off the main thread.
import { bakeSdf } from './SdfBake.js';

self.onmessage = (event) => {
  const { ticket, request } = event.data;
  try {
    const report = (fraction, label) => self.postMessage({ ticket, type: 'progress', fraction, label });
    const baked = bakeSdf(request.positions, request.indices, { resolution: request.resolution, band: request.band, report });
    self.postMessage({ ticket, type: 'done', result: baked }, [baked.values.buffer]);
  } catch (error) {
    self.postMessage({ ticket, type: 'error', message: String(error && error.message || error) });
  }
};
