/**
 * Wavefront OBJ Exporter (positions, meter-scaled box UVs, crease-split normals).
 */

export function exportFormationToOBJ(formationResult) {
  const { mesh, params, stats } = formationResult;
  const { positions, normals, uvs, indices } = mesh;
  const numVerts = positions.length / 3;
  const numTris = indices.length / 3;

  const lines = [
    '# Frontier AAA Procedural Rock & Cliff Generator',
    `# Archetype: ${params.archetype} | Seed: ${params.seed}`,
    `# Vertices: ${numVerts} | Triangles: ${numTris} | Dimensions: ${stats.dimensions.join('m x ')}m`,
    `o Frontier_${params.archetype}_${params.seed}`,
  ];

  for (let v = 0; v < numVerts; v++) {
    const i = v * 3;
    lines.push(`v ${positions[i].toFixed(5)} ${positions[i + 1].toFixed(5)} ${positions[i + 2].toFixed(5)}`);
  }

  for (let v = 0; v < numVerts; v++) {
    const i = v * 2;
    lines.push(`vt ${uvs[i].toFixed(5)} ${uvs[i + 1].toFixed(5)}`);
  }

  for (let v = 0; v < numVerts; v++) {
    const i = v * 3;
    lines.push(`vn ${normals[i].toFixed(4)} ${normals[i + 1].toFixed(4)} ${normals[i + 2].toFixed(4)}`);
  }

  lines.push('s 1');
  for (let t = 0; t < numTris; t++) {
    const a = indices[t * 3] + 1;
    const b = indices[t * 3 + 1] + 1;
    const c = indices[t * 3 + 2] + 1;
    lines.push(`f ${a}/${a}/${a} ${b}/${b}/${b} ${c}/${c}/${c}`);
  }

  return lines.join('\n') + '\n';
}
