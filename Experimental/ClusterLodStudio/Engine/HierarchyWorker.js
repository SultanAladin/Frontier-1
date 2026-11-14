// Builds source geometry + cluster hierarchy off the main thread.
import { constructSource, parseObj, computeNormals } from './SourceGeometry.js';
import { buildClusterHierarchy, hierarchyTransferables } from './ClusterHierarchy.js';

self.onmessage = (event) => {
  const { ticket, request } = event.data;
  try {
    const report = (fraction, label) => self.postMessage({ ticket, type: 'progress', fraction, label });
    report(0, 'Constructing source geometry');
    const source = request.objText ? parseObj(request.objText) : constructSource(request.preset, request.triangleBudget);
    const normals = computeNormals(source.positions, source.indices);
    const hierarchy = buildClusterHierarchy(source.positions, source.indices, request.build, report);
    let lowest = Infinity;
    for (let i = 1; i < source.positions.length; i += 3) lowest = Math.min(lowest, source.positions[i]);
    const result = { positions: source.positions, normals, hierarchy, closed: source.closed, lowest, vertexCount: source.positions.length / 3 };
    self.postMessage({ ticket, type: 'done', result }, [source.positions.buffer, normals.buffer, ...hierarchyTransferables(hierarchy)]);
  } catch (error) {
    self.postMessage({ ticket, type: 'error', message: String(error && error.message || error) });
  }
};
