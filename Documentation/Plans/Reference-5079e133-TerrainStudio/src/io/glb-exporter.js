/**
 * Standards-Compliant Binary glTF 2.0 (.glb) Exporter for AAA Game Engines (UE5 / Unity / Godot).
 *
 * Exports:
 * - POSITION (VEC3 FLOAT, with required exact min/max bounds)
 * - NORMAL   (VEC3 FLOAT)
 * - TEXCOORD_0 (VEC2 FLOAT, meter-scaled box UVs)
 * - COLOR_0  (VEC4 FLOAT: R = Volumetric AO, G = Cavity, B = Convex Edge Mask, A = UpSlope)
 * - INDICES  (SCALAR UNSIGNED_INT)
 * - Flat PBR Material + embedded Frontier geological recipe in `asset.extras`
 */

import { LITHOLOGY_PALETTES } from '../geology/presets.js';

function hexToLinearRgb(hex) {
  const clean = hex.replace('#', '');
  const sR = parseInt(clean.slice(0, 2), 16) / 255;
  const sG = parseInt(clean.slice(2, 4), 16) / 255;
  const sB = parseInt(clean.slice(4, 6), 16) / 255;
  return [
    Number(Math.pow(sR, 2.2).toFixed(4)),
    Number(Math.pow(sG, 2.2).toFixed(4)),
    Number(Math.pow(sB, 2.2).toFixed(4)),
    1.0,
  ];
}

export function exportFormationToGLB(formationResult) {
  const { mesh, params, lithology, stats } = formationResult;
  const { positions, normals, uvs, colors, indices } = mesh;

  const vertexCount = positions.length / 3;
  const indexCount = indices.length;

  // Compute required exact min/max for POSITION accessor
  const minPos = [Infinity, Infinity, Infinity];
  const maxPos = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < positions.length; i += 3) {
    const x = positions[i], y = positions[i + 1], z = positions[i + 2];
    if (x < minPos[0]) minPos[0] = x; if (x > maxPos[0]) maxPos[0] = x;
    if (y < minPos[1]) minPos[1] = y; if (y > maxPos[1]) maxPos[1] = y;
    if (z < minPos[2]) minPos[2] = z; if (z > maxPos[2]) maxPos[2] = z;
  }

  // Pack binary buffer views (all 4-byte aligned since Float32 & Uint32 are 4-byte multiples)
  const posBytes = positions.byteLength;
  const normBytes = normals.byteLength;
  const uvBytes = uvs.byteLength;
  const colBytes = colors.byteLength;
  const idxBytes = indices.byteLength;

  const offPos = 0;
  const offNorm = offPos + posBytes;
  const offUV = offNorm + normBytes;
  const offCol = offUV + uvBytes;
  const offIdx = offCol + colBytes;
  const totalBinBytes = offIdx + idxBytes;
  const paddedBinBytes = Math.ceil(totalBinBytes / 4) * 4;

  const palette = LITHOLOGY_PALETTES[lithology] || LITHOLOGY_PALETTES.neutralStone;

  const gltfJson = {
    asset: {
      version: '2.0',
      generator: 'Frontier AAA Structural Rock & Cliff Generator',
      extras: {
        frontierRecipe: params,
        stats,
        vertexColorChannels: {
          R: 'Volumetric Ambient Occlusion',
          G: 'Concave Fracture Cavity',
          B: 'Convex Ridge / Edge Wear',
          A: 'Upward Slope Mask',
        },
      },
    },
    scene: 0,
    scenes: [{ name: 'Scene', nodes: [0] }],
    nodes: [{ name: `Frontier_${params.archetype}_${params.seed}`, mesh: 0 }],
    materials: [
      {
        name: `M_Rock_${lithology || 'Flat'}`,
        pbrMetallicRoughness: {
          baseColorFactor: hexToLinearRgb(palette.color),
          metallicFactor: 0.0,
          roughnessFactor: palette.roughness,
        },
        doubleSided: false,
      },
    ],
    meshes: [
      {
        name: `SM_Rock_${params.archetype}_${params.seed}`,
        primitives: [
          {
            attributes: {
              POSITION: 0,
              NORMAL: 1,
              TEXCOORD_0: 2,
              COLOR_0: 3,
            },
            indices: 4,
            material: 0,
            mode: 4, // TRIANGLES
          },
        ],
      },
    ],
    buffers: [{ byteLength: paddedBinBytes }],
    bufferViews: [
      { buffer: 0, byteOffset: offPos,  byteLength: posBytes,  target: 34962 },
      { buffer: 0, byteOffset: offNorm, byteLength: normBytes, target: 34962 },
      { buffer: 0, byteOffset: offUV,   byteLength: uvBytes,   target: 34962 },
      { buffer: 0, byteOffset: offCol,  byteLength: colBytes,  target: 34962 },
      { buffer: 0, byteOffset: offIdx,  byteLength: idxBytes,  target: 34963 },
    ],
    accessors: [
      { bufferView: 0, componentType: 5126, count: vertexCount, type: 'VEC3', min: minPos, max: maxPos },
      { bufferView: 1, componentType: 5126, count: vertexCount, type: 'VEC3' },
      { bufferView: 2, componentType: 5126, count: vertexCount, type: 'VEC2' },
      { bufferView: 3, componentType: 5126, count: vertexCount, type: 'VEC4' },
      { bufferView: 4, componentType: 5125, count: indexCount,  type: 'SCALAR' },
    ],
  };

  const encoder = new TextEncoder();
  const jsonRaw = encoder.encode(JSON.stringify(gltfJson));
  const paddedJsonBytes = Math.ceil(jsonRaw.byteLength / 4) * 4;

  const totalGlbBytes = 12 + 8 + paddedJsonBytes + 8 + paddedBinBytes;
  const glbBuffer = new ArrayBuffer(totalGlbBytes);
  const view = new DataView(glbBuffer);
  const bytes = new Uint8Array(glbBuffer);

  // 12-byte GLB Header
  view.setUint32(0, 0x46546c67, true); // 'glTF'
  view.setUint32(4, 2, true);          // version 2
  view.setUint32(8, totalGlbBytes, true);

  // Chunk 0: JSON
  view.setUint32(12, paddedJsonBytes, true);
  view.setUint32(16, 0x4e4f534a, true); // 'JSON'
  bytes.set(jsonRaw, 20);
  for (let i = jsonRaw.byteLength; i < paddedJsonBytes; i++) {
    bytes[20 + i] = 0x20; // pad JSON with ASCII space
  }

  // Chunk 1: BIN
  const binChunkStart = 20 + paddedJsonBytes;
  view.setUint32(binChunkStart, paddedBinBytes, true);
  view.setUint32(binChunkStart + 4, 0x004e4942, true); // 'BIN\0'

  const binDataOffset = binChunkStart + 8;
  bytes.set(new Uint8Array(positions.buffer, positions.byteOffset, posBytes), binDataOffset + offPos);
  bytes.set(new Uint8Array(normals.buffer, normals.byteOffset, normBytes), binDataOffset + offNorm);
  bytes.set(new Uint8Array(uvs.buffer, uvs.byteOffset, uvBytes), binDataOffset + offUV);
  bytes.set(new Uint8Array(colors.buffer, colors.byteOffset, colBytes), binDataOffset + offCol);
  bytes.set(new Uint8Array(indices.buffer, indices.byteOffset, idxBytes), binDataOffset + offIdx);

  return new Uint8Array(glbBuffer);
}
