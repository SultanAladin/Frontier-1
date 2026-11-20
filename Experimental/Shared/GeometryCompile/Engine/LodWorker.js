// Builds the Nanite-style cluster hierarchy (ClusterHierarchy.js, from Frontier-1's Cluster LOD Studio) off the main
// thread for the Geometry Compile "Cluster LOD" step.
import { buildClusterHierarchy, hierarchyTransferables } from './ClusterHierarchy.js';

self.onmessage = (event) => {
  const { ticket, request } = event.data;
  try {
    const report = (fraction, label) => self.postMessage({ ticket, type: 'progress', fraction, label });
    const hierarchy = buildClusterHierarchy(request.positions, request.indices, { ...request.build, families: request.families || null }, report);
    self.postMessage({ ticket, type: 'done', result: hierarchy }, hierarchyTransferables(hierarchy));
  } catch (error) {
    self.postMessage({ ticket, type: 'error', message: String((error && error.message) || error) });
  }
};
