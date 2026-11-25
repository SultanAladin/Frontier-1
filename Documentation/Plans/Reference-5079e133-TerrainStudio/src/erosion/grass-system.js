/**
 * Procedural 3D Multi-Blade Grass Tuft Geometry for WebGL2 GPU Instanced Vegetation.
 *
 * Each instance renders a natural 5-blade 3D tuft of curved, tapered grass blades
 * (not flat billboards). GPU vertex instancing places up to 44,000 tufts (= 220,000
 * real 3D blades) directly onto the 512x512 heightfield with wind-gust sway, slope/water/snow
 * masking, and lush Green Grass <-> Golden Dry Grass shading.
 */

export const MAX_GRASS_TUFTS = 44000;

export function buildGrassTuftMesh() {
  const blades = [
    { az: 0.00, lean: 0.28, h: 1.00, w: 0.085, offX:  0.00, offZ:  0.00 },
    { az: 1.26, lean: 0.44, h: 0.88, w: 0.078, offX:  0.14, offZ:  0.06 },
    { az: 2.51, lean: 0.38, h: 0.94, w: 0.080, offX: -0.11, offZ:  0.13 },
    { az: 3.77, lean: 0.48, h: 0.80, w: 0.074, offX: -0.13, offZ: -0.10 },
    { az: 5.03, lean: 0.35, h: 0.91, w: 0.078, offX:  0.10, offZ: -0.14 },
  ];

  const segs = 3; // 3 segments = 7 vertices (3 pairs + 1 sharp apex tip) & 5 triangles per blade
  const vertsPerBlade = segs * 2 + 1;
  const trisPerBlade = (segs - 1) * 2 + 1;

  const totalVerts = blades.length * vertsPerBlade;
  const totalIndices = blades.length * trisPerBlade * 3;

  // Interleaved: aPos (vec3) + aNormal (vec3) + aBladeMeta (vec2: tAlong [0..1], bladeHash [0..1])
  const positions = new Float32Array(totalVerts * 3);
  const normals = new Float32Array(totalVerts * 3);
  const bladeMeta = new Float32Array(totalVerts * 2);
  const indices = new Uint16Array(totalIndices);

  let vCursor = 0;
  let iCursor = 0;

  blades.forEach((b, bIdx) => {
    const baseVertex = vCursor;
    const ca = Math.cos(b.az);
    const sa = Math.sin(b.az);
    // Forward lean direction (ca, 0, sa), side width direction (-sa, 0, ca)
    const sideX = -sa;
    const sideZ = ca;
    const bladeSeed = (bIdx + 0.5) / blades.length;

    for (let s = 0; s <= segs; s++) {
      const t = s / segs;
      const y = b.h * (t - 0.10 * t * t);
      const curveFwd = b.h * b.lean * t * t;
      const cx = b.offX + ca * curveFwd;
      const cz = b.offZ + sa * curveFwd;

      // Normal tilted upward so curved blades catch rich zenith & grazing sunlight from all angles
      const tangFwd = 2.0 * b.lean * t;
      const tangUp = Math.max(0.2, 1.0 - 0.20 * t);
      // Outward normal perpendicular to tangent (-ca * tangUp, tangFwd, -sa * tangUp) blended with +Y
      let nx = -ca * 0.45;
      let ny = 0.82 + 0.18 * tangFwd;
      let nz = -sa * 0.45;
      const nl = Math.hypot(nx, ny, nz) || 1;
      nx /= nl; ny /= nl; nz /= nl;

      if (s < segs) {
        const halfW = b.w * (1.0 - 0.68 * t);
        // Left edge vertex (slight V-crease normal splay for 3D volumetric blade roundness!)
        let nlx = nx - sideX * 0.32, nly = ny, nlz = nz - sideZ * 0.32;
        const nll = Math.hypot(nlx, nly, nlz) || 1;
        positions[vCursor * 3 + 0] = cx - sideX * halfW;
        positions[vCursor * 3 + 1] = y;
        positions[vCursor * 3 + 2] = cz - sideZ * halfW;
        normals[vCursor * 3 + 0] = nlx / nll;
        normals[vCursor * 3 + 1] = nly / nll;
        normals[vCursor * 3 + 2] = nlz / nll;
        bladeMeta[vCursor * 2 + 0] = t;
        bladeMeta[vCursor * 2 + 1] = bladeSeed;
        vCursor++;

        // Right edge vertex
        let nrx = nx + sideX * 0.32, nry = ny, nrz = nz + sideZ * 0.32;
        const nrl = Math.hypot(nrx, nry, nrz) || 1;
        positions[vCursor * 3 + 0] = cx + sideX * halfW;
        positions[vCursor * 3 + 1] = y;
        positions[vCursor * 3 + 2] = cz + sideZ * halfW;
        normals[vCursor * 3 + 0] = nrx / nrl;
        normals[vCursor * 3 + 1] = nry / nrl;
        normals[vCursor * 3 + 2] = nrz / nrl;
        bladeMeta[vCursor * 2 + 0] = t;
        bladeMeta[vCursor * 2 + 1] = bladeSeed;
        vCursor++;
      } else {
        // Sharp blade tip apex
        positions[vCursor * 3 + 0] = cx;
        positions[vCursor * 3 + 1] = y;
        positions[vCursor * 3 + 2] = cz;
        normals[vCursor * 3 + 0] = nx;
        normals[vCursor * 3 + 1] = ny;
        normals[vCursor * 3 + 2] = nz;
        bladeMeta[vCursor * 2 + 0] = 1.0;
        bladeMeta[vCursor * 2 + 1] = bladeSeed;
        vCursor++;
      }
    }

    for (let s = 0; s < segs - 1; s++) {
      const i0 = baseVertex + s * 2;
      const i1 = i0 + 1;
      const i2 = i0 + 2;
      const i3 = i0 + 3;
      indices[iCursor++] = i0; indices[iCursor++] = i1; indices[iCursor++] = i2;
      indices[iCursor++] = i2; indices[iCursor++] = i1; indices[iCursor++] = i3;
    }
    const lastL = baseVertex + (segs - 1) * 2;
    const lastR = lastL + 1;
    const tip = lastL + 2;
    indices[iCursor++] = lastL;
    indices[iCursor++] = lastR;
    indices[iCursor++] = tip;
  });

  return {
    positions,
    normals,
    bladeMeta,
    indices,
    vertexCount: totalVerts,
    indexCount: totalIndices,
  };
}
