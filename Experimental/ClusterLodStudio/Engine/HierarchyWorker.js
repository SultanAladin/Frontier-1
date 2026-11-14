// Builds source geometry + cluster hierarchy off the main thread.
import { constructSource, parseObj, computeNormals } from './SourceGeometry.js';
import { buildClusterHierarchy, hierarchyTransferables } from './ClusterHierarchy.js';
import { transformGeometry, isIdentityTransform } from './GeometryTransform.js';

self.onmessage = (event) => {
  const { ticket, request } = event.data;
  try {
    const report = (fraction, label) => self.postMessage({ ticket, type: 'progress', fraction, label });
    report(0, 'Constructing source geometry');
    const source = request.objText ? parseObj(request.objText) : constructSource(request.preset, request.triangleBudget);
    // The untransformed source stays with the hero so the Geometry Editor can preview new transforms without a rebuild.
    const basePositions = new Float32Array(source.positions), baseIndices = new Uint32Array(source.indices);
    if (request.transform && !isIdentityTransform(request.transform)) {
      const moved = transformGeometry(source.positions, source.indices, request.transform);
      source.positions = moved.positions; source.indices = moved.indices;
    }
    const normals = computeNormals(source.positions, source.indices);
    const hierarchy = buildClusterHierarchy(source.positions, source.indices, request.build, report);
    let lowest = Infinity;
    for (let i = 1; i < source.positions.length; i += 3) lowest = Math.min(lowest, source.positions[i]);
    // Source triangles are copied for the UV layout, which the main thread builds from the welded positions.
    const indices = new Uint32Array(source.indices);
    const base = { positions: basePositions, indices: baseIndices };
    const result = { positions: source.positions, normals, indices, hierarchy, closed: source.closed, lowest, vertexCount: source.positions.length / 3, base };
    self.postMessage({ ticket, type: 'done', result }, [source.positions.buffer, normals.buffer, indices.buffer, basePositions.buffer, baseIndices.buffer, ...hierarchyTransferables(hierarchy)]);
  } catch (error) {
    self.postMessage({ ticket, type: 'error', message: String(error && error.message || error) });
  }
};
