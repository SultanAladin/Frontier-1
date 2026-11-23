//==========================================================================================================================================
//                                                    VEHICLEBVHASSETS.JS
//==========================================================================================================================================
// Loads pre-baked ControlVehicle meshes + SAH BVH from VehicleBenchmarkAssets.bin (FVB1 format),
// packs GPU node/triangle textures & static shadow-map vertex buffers, and provides CPU/GPU TLAS updates + ray picking.

export const TLAS_RESERVED_NODES = 96;
export const MAX_BVH_INSTANCES = 36; // Up to 4 ControlVehicles × 9 parts (1 Body + 4 Rims + 4 Tyres)

// ControlVehicle (+X Forward, +Y Left, +Z Up) wheel mount offsets matching ControlVehicleBody/Rim/Tyre.facet (VehicleAssembly.js)
export const VEHICLE_AXLE_MOUNTS = [
  { x: 1.7274, y: 1.0475, z: 0.0914, front: true, right: false },
  { x: 1.7274, y: -1.0475, z: 0.0914, front: true, right: true },
  { x: -1.6686, y: 1.0475, z: 0.0914, front: false, right: false },
  { x: -1.6686, y: -1.0475, z: 0.0914, front: false, right: true },
];
export const VEHICLE_TYRE_RADIUS = 0.50855;
export const VEHICLE_BODY_LIFT = VEHICLE_TYRE_RADIUS - 0.0914; // 0.41715 m so tyres sit flush at local Z = 0
export const VEHICLE_RIM_TO_HUB_SCALE = 0.3446 / 0.344818;

let CachedPromise = null;
let CachedAssets = null;

function Mat4Identity() {
  const m = new Float32Array(16);
  m[0] = 1;
  m[5] = 1;
  m[10] = 1;
  m[15] = 1;
  return m;
}

function Mat4Multiply(a, b) {
  const out = new Float32Array(16);
  for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 4; row++) {
      out[col * 4 + row] =
        a[0 * 4 + row] * b[col * 4 + 0] +
        a[1 * 4 + row] * b[col * 4 + 1] +
        a[2 * 4 + row] * b[col * 4 + 2] +
        a[3 * 4 + row] * b[col * 4 + 3];
    }
  }
  return out;
}

function Mat4ComposeTRS(pos, rotDeg, scale) {
  const tx = Number(pos?.[0]) || 0;
  const ty = Number(pos?.[1]) || 0;
  const tz = Number(pos?.[2]) || 0;
  const rx = ((Number(rotDeg?.[0]) || 0) * Math.PI) / 180;
  const ry = ((Number(rotDeg?.[1]) || 0) * Math.PI) / 180;
  const rz = ((Number(rotDeg?.[2]) || 0) * Math.PI) / 180;
  const sx = Math.max(0.02, Number(scale?.[0]) || 1);
  const sy = Math.max(0.02, Number(scale?.[1]) || 1);
  const sz = Math.max(0.02, Number(scale?.[2]) || 1);

  const cx = Math.cos(rx), sxr = Math.sin(rx);
  const cy = Math.cos(ry), syr = Math.sin(ry);
  const cz = Math.cos(rz), szr = Math.sin(rz);

  // Intrinsic XYZ -> Rz * Ry * Rx
  const r00 = cz * cy;
  const r10 = szr * cy;
  const r20 = -syr;
  const r01 = cz * syr * sxr - szr * cx;
  const r11 = szr * syr * sxr + cz * cx;
  const r21 = cy * sxr;
  const r02 = cz * syr * cx + szr * sxr;
  const r12 = szr * syr * cx - cz * sxr;
  const r22 = cy * cx;

  const m = new Float32Array(16);
  m[0] = r00 * sx;
  m[1] = r10 * sx;
  m[2] = r20 * sx;
  m[4] = r01 * sy;
  m[5] = r11 * sy;
  m[6] = r21 * sy;
  m[8] = r02 * sz;
  m[9] = r12 * sz;
  m[10] = r22 * sz;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
  m[15] = 1;
  return m;
}

function Mat4InvertAffine(m) {
  const a00 = m[0], a01 = m[4], a02 = m[8];
  const a10 = m[1], a11 = m[5], a12 = m[9];
  const a20 = m[2], a21 = m[6], a22 = m[10];
  const b01 = a22 * a11 - a12 * a21;
  const b11 = -a22 * a10 + a12 * a20;
  const b21 = a21 * a10 - a11 * a20;
  const det = a00 * b01 + a01 * b11 + a02 * b21;
  const invDet = Math.abs(det) > 1e-12 ? 1.0 / det : 0.0;
  const inv = new Float32Array(16);
  inv[0] = b01 * invDet;
  inv[4] = (-a22 * a01 + a02 * a21) * invDet;
  inv[8] = (a12 * a01 - a02 * a11) * invDet;
  inv[1] = b11 * invDet;
  inv[5] = (a22 * a00 - a02 * a20) * invDet;
  inv[9] = (-a12 * a00 + a02 * a10) * invDet;
  inv[2] = b21 * invDet;
  inv[6] = (-a21 * a00 + a01 * a20) * invDet;
  inv[10] = (a11 * a00 - a01 * a10) * invDet;
  const tx = m[12], ty = m[13], tz = m[14];
  inv[12] = -(inv[0] * tx + inv[4] * ty + inv[8] * tz);
  inv[13] = -(inv[1] * tx + inv[5] * ty + inv[9] * tz);
  inv[14] = -(inv[2] * tx + inv[6] * ty + inv[10] * tz);
  inv[15] = 1;
  return inv;
}

function UnpackOctahedralJs(packedFloat) {
  const packed20 = Math.round(packedFloat);
  const u = ((packed20 & 1023) / 1023.0) * 2.0 - 1.0;
  const v = (((packed20 >> 10) & 1023) / 1023.0) * 2.0 - 1.0;
  let x = u, y = v, z = 1.0 - Math.abs(u) - Math.abs(v);
  if (z < 0.0) {
    const ox = x, oy = y;
    x = (1.0 - Math.abs(oy)) * (ox >= 0.0 ? 1.0 : -1.0);
    y = (1.0 - Math.abs(ox)) * (oy >= 0.0 ? 1.0 : -1.0);
  }
  const len = Math.hypot(x, y, z) || 1.0;
  return [x / len, y / len, z / len];
}

export function ParseVehicleFvb1Buffer(arrayBuf) {
  const dv = new DataView(arrayBuf);
  const magic = String.fromCharCode(
    dv.getUint8(0),
    dv.getUint8(1),
    dv.getUint8(2),
    dv.getUint8(3),
  );
  if (magic !== "FVB1") {
    throw new Error(`Invalid FVB1 vehicle asset header: ${magic}`);
  }

  const fileMeshCount = dv.getUint32(4, true);
  const fileBlasNodes = dv.getUint32(8, true);
  const fileTris = dv.getUint32(12, true);

  let offset = 20;
  const meshMeta = [];
  for (let i = 0; i < fileMeshCount; i++) {
    meshMeta.push({
      nodeOffset: dv.getUint32(offset, true),
      nodeCount: dv.getUint32(offset + 4, true),
      triOffset: dv.getUint32(offset + 8, true),
      triCount: dv.getUint32(offset + 12, true),
    });
    offset += 16;
  }

  const blasByteStart = offset;
  offset += fileBlasNodes * 32;
  const triByteStart = offset;

  // Keep only the 3 vehicle meshes (0=Body, 1=Rim, 2=Tyre) for compact GPU buffers
  const vehicleMeshes = meshMeta.slice(0, 3);
  const vehicleNodeCount = vehicleMeshes.reduce((acc, m) => Math.max(acc, m.nodeOffset + m.nodeCount), 0);
  const vehicleTriCount = vehicleMeshes.reduce((acc, m) => Math.max(acc, m.triOffset + m.triCount), 0);

  const totalCombinedNodes = TLAS_RESERVED_NODES + vehicleNodeCount;
  const nodeTexWidth = 2048;
  const nodeTexHeight = Math.ceil((totalCombinedNodes * 2) / nodeTexWidth);
  const nodeTexData = new Float32Array(nodeTexWidth * nodeTexHeight * 4);
  const meshLocalBounds = vehicleMeshes.map(() => ({ min: [0, 0, 0], max: [0, 0, 0] }));

  let nOff = blasByteStart;
  for (let n = 0; n < vehicleNodeCount; n++) {
    const minX = dv.getFloat32(nOff + 0, true);
    const minY = dv.getFloat32(nOff + 4, true);
    const minZ = dv.getFloat32(nOff + 8, true);
    let leftOrFirst = Math.round(dv.getFloat32(nOff + 12, true));
    const maxX = dv.getFloat32(nOff + 16, true);
    const maxY = dv.getFloat32(nOff + 20, true);
    const maxZ = dv.getFloat32(nOff + 24, true);
    const count = Math.round(dv.getFloat32(nOff + 28, true));
    nOff += 32;
    if (count === 0) leftOrFirst += TLAS_RESERVED_NODES;
    const dst = (TLAS_RESERVED_NODES + n) * 2;
    nodeTexData[dst * 4 + 0] = minX;
    nodeTexData[dst * 4 + 1] = minY;
    nodeTexData[dst * 4 + 2] = minZ;
    nodeTexData[dst * 4 + 3] = leftOrFirst;
    nodeTexData[(dst + 1) * 4 + 0] = maxX;
    nodeTexData[(dst + 1) * 4 + 1] = maxY;
    nodeTexData[(dst + 1) * 4 + 2] = maxZ;
    nodeTexData[(dst + 1) * 4 + 3] = count;

    for (let m = 0; m < vehicleMeshes.length; m++) {
      if (n === vehicleMeshes[m].nodeOffset) {
        meshLocalBounds[m].min = [minX, minY, minZ];
        meshLocalBounds[m].max = [maxX, maxY, maxZ];
      }
    }
  }

  const triTexWidth = 2048;
  const triTexHeight = Math.ceil((vehicleTriCount * 4) / triTexWidth);
  const triTexData = new Float32Array(triTexWidth * triTexHeight * 4);
  const shadowVertPositions = new Float32Array(vehicleTriCount * 9);

  let tOff = triByteStart;
  for (let t = 0; t < vehicleTriCount; t++) {
    const base = t * 16;
    const p0x = dv.getFloat32(tOff + 0, true);
    const p0y = dv.getFloat32(tOff + 4, true);
    const p0z = dv.getFloat32(tOff + 8, true);
    const matId = Math.round(dv.getFloat32(tOff + 12, true));
    const e1x = dv.getFloat32(tOff + 16, true);
    const e1y = dv.getFloat32(tOff + 20, true);
    const e1z = dv.getFloat32(tOff + 24, true);
    const oct0 = dv.getFloat32(tOff + 28, true);
    const e2x = dv.getFloat32(tOff + 32, true);
    const e2y = dv.getFloat32(tOff + 36, true);
    const e2z = dv.getFloat32(tOff + 40, true);
    const oct1 = dv.getFloat32(tOff + 44, true);
    const oct2 = dv.getFloat32(tOff + 60, true);
    tOff += 64;

    const p1x = p0x + e1x, p1y = p0y + e1y, p1z = p0z + e1z;
    const p2x = p0x + e2x, p2y = p0y + e2y, p2z = p0z + e2z;
    const n0 = UnpackOctahedralJs(oct0);
    const n1 = UnpackOctahedralJs(oct1);
    const n2 = UnpackOctahedralJs(oct2);
    let nx = n0[0] + n1[0] + n2[0];
    let ny = n0[1] + n1[1] + n2[1];
    let nz = n0[2] + n1[2] + n2[2];
    const nl = Math.hypot(nx, ny, nz);
    if (nl > 1e-6) {
      nx /= nl; ny /= nl; nz /= nl;
    } else {
      const fx = e1y * e2z - e1z * e2y;
      const fy = e1z * e2x - e1x * e2z;
      const fz = e1x * e2y - e1y * e2x;
      const fl = Math.hypot(fx, fy, fz) || 1.0;
      nx = fx / fl; ny = fy / fl; nz = fz / fl;
    }

    triTexData[base + 0] = p0x;
    triTexData[base + 1] = p0y;
    triTexData[base + 2] = p0z;
    triTexData[base + 3] = matId;
    triTexData[base + 4] = p1x;
    triTexData[base + 5] = p1y;
    triTexData[base + 6] = p1z;
    triTexData[base + 7] = oct0;
    triTexData[base + 8] = p2x;
    triTexData[base + 9] = p2y;
    triTexData[base + 10] = p2z;
    triTexData[base + 11] = oct1;
    triTexData[base + 12] = nx;
    triTexData[base + 13] = ny;
    triTexData[base + 14] = nz;
    triTexData[base + 15] = oct2;

    const sv = t * 9;
    shadowVertPositions[sv + 0] = p0x;
    shadowVertPositions[sv + 1] = p0y;
    shadowVertPositions[sv + 2] = p0z;
    shadowVertPositions[sv + 3] = p1x;
    shadowVertPositions[sv + 4] = p1y;
    shadowVertPositions[sv + 5] = p1z;
    shadowVertPositions[sv + 6] = p2x;
    shadowVertPositions[sv + 7] = p2y;
    shadowVertPositions[sv + 8] = p2z;
  }

  return {
    meshMeta: vehicleMeshes,
    meshLocalBounds,
    nodeTexWidth,
    nodeTexHeight,
    nodeTexData,
    triTexWidth,
    triTexHeight,
    triTexData,
    shadowVertPositions,
    totalTriangles: vehicleTriCount,
  };
}

export function GetCachedVehicleBvhAssets() {
  return CachedAssets;
}

export async function LoadVehicleBvhAssets() {
  if (CachedAssets) return CachedAssets;
  if (CachedPromise) return CachedPromise;

  CachedPromise = (async () => {
    const urls = [
      "/Experimental/ShadowAlgorithmLab/Assets/VehicleBenchmarkAssets.bin",
      "../ShadowAlgorithmLab/Assets/VehicleBenchmarkAssets.bin",
      "./Assets/VehicleBenchmarkAssets.bin",
    ];
    let arrayBuf = null;
    for (const url of urls) {
      try {
        const resp = await fetch(url);
        if (resp.ok) {
          arrayBuf = await resp.arrayBuffer();
          break;
        }
      } catch {}
    }
    if (!arrayBuf) {
      throw new Error("Could not load ControlVehicle VehicleBenchmarkAssets.bin");
    }
    CachedAssets = ParseVehicleFvb1Buffer(arrayBuf);
    return CachedAssets;
  })();

  return CachedPromise;
}

export function ExpandVehicleInstances(vehicles = [], assets) {
  if (!assets || !vehicles || vehicles.length === 0) return [];
  const instances = [];
  for (const veh of vehicles) {
    if (!veh) continue;
    const vehMatrix = Mat4ComposeTRS(veh.Position, veh.Rotation, veh.Scale);
    const bodyLocal = Mat4ComposeTRS([0, 0, VEHICLE_BODY_LIFT], [0, 0, 0], [1, 1, 1]);
    const bodyL2w = Mat4Multiply(vehMatrix, bodyLocal);
    const bodyW2l = Mat4InvertAffine(bodyL2w);
    const paint = veh.BaseColor || [0.92, 0.94, 0.98];
    const rough = Number.isFinite(veh.Roughness) ? veh.Roughness : 0.14;
    const metal = Number.isFinite(veh.Metallic) ? veh.Metallic : 0.25;
    const coat = Number.isFinite(veh.Coat) ? veh.Coat : 1.0;

    instances.push({
      meshIdx: 0,
      l2w: bodyL2w,
      w2l: bodyW2l,
      matOverride: -1,
      pickCode: veh.PickCode || 501,
      castShadows: veh.CastShadows !== false,
      paint,
      rough,
      metal,
      coat,
    });

    for (const wm of VEHICLE_AXLE_MOUNTS) {
      const wheelLocal = Mat4ComposeTRS(
        [wm.x, wm.y, VEHICLE_BODY_LIFT + wm.z],
        [0, 0, wm.right ? 180 : 0],
        [1, 1, 1],
      );
      const wheelL2w = Mat4Multiply(vehMatrix, wheelLocal);
      const wheelW2l = Mat4InvertAffine(wheelL2w);
      // Tyre (mesh 2, matOverride 11 = TyreRubber)
      instances.push({
        meshIdx: 2,
        l2w: wheelL2w,
        w2l: wheelW2l,
        matOverride: 11,
        pickCode: veh.PickCode || 501,
        castShadows: veh.CastShadows !== false,
        paint,
        rough,
        metal,
        coat,
      });
      // Rim (mesh 1, matOverride 10 = RimAlloy)
      const rimLocal = Mat4ComposeTRS(
        [wm.x, wm.y, VEHICLE_BODY_LIFT + wm.z],
        [0, 0, wm.right ? 180 : 0],
        [VEHICLE_RIM_TO_HUB_SCALE, VEHICLE_RIM_TO_HUB_SCALE, VEHICLE_RIM_TO_HUB_SCALE],
      );
      const rimL2w = Mat4Multiply(vehMatrix, rimLocal);
      const rimW2l = Mat4InvertAffine(rimL2w);
      instances.push({
        meshIdx: 1,
        l2w: rimL2w,
        w2l: rimW2l,
        matOverride: 10,
        pickCode: veh.PickCode || 501,
        castShadows: veh.CastShadows !== false,
        paint,
        rough,
        metal,
        coat,
      });
    }
    if (instances.length >= MAX_BVH_INSTANCES) break;
  }
  return instances.slice(0, MAX_BVH_INSTANCES);
}

// Writes TLAS into nodeTexData slots [0..95] and packs instance matrix + paint buffer (128x10 RGBA32F -> 10 texels per instance)
export function BuildVehicleTlasAndInstances(vehicles, assets, instanceFloatBuf = null) {
  const buf = instanceFloatBuf || new Float32Array(128 * 10 * 4);
  buf.fill(0);
  if (!assets) return { instanceCount: 0, instances: [], instanceBuffer: buf, instData: buf.subarray(0, 0) };
  const instances = ExpandVehicleInstances(vehicles, assets);

  if (instances.length === 0) {
    return { instanceCount: 0, instances, instanceBuffer: buf, instData: buf.subarray(0, 0) };
  }

  for (let i = 0; i < instances.length; i++) {
    const inst = instances[i];
    const base = i * 10 * 4;
    buf.set(inst.l2w, base);
    buf.set(inst.w2l, base + 16);
    buf[base + 32] = inst.matOverride + 1;
    buf[base + 33] = inst.pickCode;
    buf[base + 34] = inst.castShadows ? 1 : 0;
    buf[base + 35] = inst.coat;
    buf[base + 36] = inst.paint[0];
    buf[base + 37] = inst.paint[1];
    buf[base + 38] = inst.paint[2];
    buf[base + 39] = inst.rough;
  }

  const leaves = instances.map((inst, idx) => {
    const lb = assets.meshLocalBounds[inst.meshIdx];
    const wmin = [1e30, 1e30, 1e30];
    const wmax = [-1e30, -1e30, -1e30];
    const m = inst.l2w;
    for (let c = 0; c < 8; c++) {
      const lx = c & 1 ? lb.max[0] : lb.min[0];
      const ly = c & 2 ? lb.max[1] : lb.min[1];
      const lz = c & 4 ? lb.max[2] : lb.min[2];
      const wx = m[0] * lx + m[4] * ly + m[8] * lz + m[12];
      const wy = m[1] * lx + m[5] * ly + m[9] * lz + m[13];
      const wz = m[2] * lx + m[6] * ly + m[10] * lz + m[14];
      if (wx < wmin[0]) wmin[0] = wx;
      if (wy < wmin[1]) wmin[1] = wy;
      if (wz < wmin[2]) wmin[2] = wz;
      if (wx > wmax[0]) wmax[0] = wx;
      if (wy > wmax[1]) wmax[1] = wy;
      if (wz > wmax[2]) wmax[2] = wz;
    }
    return {
      min: wmin,
      max: wmax,
      leftOrFirst: TLAS_RESERVED_NODES + assets.meshMeta[inst.meshIdx].nodeOffset,
      count: -(idx + 1),
    };
  });

  // Clear TLAS slots
  for (let s = 0; s < TLAS_RESERVED_NODES * 2 * 4; s++) {
    assets.nodeTexData[s] = 0;
  }

  let nextSlot = 1;
  function writeTlasNode(slot, min, max, lof, cnt) {
    const t = slot * 2;
    assets.nodeTexData[t * 4 + 0] = min[0];
    assets.nodeTexData[t * 4 + 1] = min[1];
    assets.nodeTexData[t * 4 + 2] = min[2];
    assets.nodeTexData[t * 4 + 3] = lof;
    assets.nodeTexData[(t + 1) * 4 + 0] = max[0];
    assets.nodeTexData[(t + 1) * 4 + 1] = max[1];
    assets.nodeTexData[(t + 1) * 4 + 2] = max[2];
    assets.nodeTexData[(t + 1) * 4 + 3] = cnt;
  }

  function buildTlas(slot, start, end) {
    const bmin = [1e30, 1e30, 1e30];
    const bmax = [-1e30, -1e30, -1e30];
    for (let i = start; i < end; i++) {
      for (let a = 0; a < 3; a++) {
        if (leaves[i].min[a] < bmin[a]) bmin[a] = leaves[i].min[a];
        if (leaves[i].max[a] > bmax[a]) bmax[a] = leaves[i].max[a];
      }
    }
    if (end - start === 1 || nextSlot + 2 >= TLAS_RESERVED_NODES) {
      if (end - start === 1) {
        writeTlasNode(slot, bmin, bmax, leaves[start].leftOrFirst, leaves[start].count);
        return;
      }
    }
    let axis = 0;
    const ext = [bmax[0] - bmin[0], bmax[1] - bmin[1], bmax[2] - bmin[2]];
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;
    const sub = leaves.slice(start, end).sort((a, b) => (a.min[axis] + a.max[axis]) - (b.min[axis] + b.max[axis]));
    for (let i = start; i < end; i++) leaves[i] = sub[i - start];
    const mid = (start + end) >> 1;
    const leftSlot = nextSlot++;
    const rightSlot = nextSlot++;
    writeTlasNode(slot, bmin, bmax, leftSlot, 0);
    buildTlas(leftSlot, start, mid);
    buildTlas(rightSlot, mid, end);
  }

  buildTlas(0, 0, leaves.length);
  return {
    instanceCount: instances.length,
    instances,
    instanceBuffer: buf,
    instData: buf.subarray(0, instances.length * 40),
  };
}

function IntersectAabbRay(ro, invDir, bmin, bmax, maxT) {
  let tmin = 0.0;
  let tmax = maxT;
  for (let a = 0; a < 3; a++) {
    const t0 = (bmin[a] - ro[a]) * invDir[a];
    const t1 = (bmax[a] - ro[a]) * invDir[a];
    const ta = t0 < t1 ? t0 : t1;
    const tb = t0 < t1 ? t1 : t0;
    if (ta > tmin) tmin = ta;
    if (tb < tmax) tmax = tb;
    if (tmax < tmin) return 1e30;
  }
  return tmin;
}

function IntersectTriMoller(ro, rd, v0x, v0y, v0z, v1x, v1y, v1z, v2x, v2y, v2z) {
  const e1x = v1x - v0x, e1y = v1y - v0y, e1z = v1z - v0z;
  const e2x = v2x - v0x, e2y = v2y - v0y, e2z = v2z - v0z;
  const px = rd[1] * e2z - rd[2] * e2y;
  const py = rd[2] * e2x - rd[0] * e2z;
  const pz = rd[0] * e2y - rd[1] * e2x;
  const det = e1x * px + e1y * py + e1z * pz;
  if (Math.abs(det) < 1e-8) return -1;
  const invDet = 1.0 / det;
  const sx = ro[0] - v0x, sy = ro[1] - v0y, sz = ro[2] - v0z;
  const u = (sx * px + sy * py + sz * pz) * invDet;
  if (u < 0 || u > 1) return -1;
  const qx = sy * e1z - sz * e1y;
  const qy = sz * e1x - sx * e1z;
  const qz = sx * e1y - sy * e1x;
  const v = (rd[0] * qx + rd[1] * qy + rd[2] * qz) * invDet;
  if (v < 0 || u + v > 1) return -1;
  const t = (e2x * qx + e2y * qy + e2z * qz) * invDet;
  return t > 1e-4 ? t : -1;
}

// Fast CPU ray-BVH trace used by viewport picking & CpuGtxFallbackRenderer
export function TraceVehicleBvhCpu(ro, rd, maxT, vehicles, assets) {
  if (!assets || !vehicles || vehicles.length === 0) return null;
  const { instances } = BuildVehicleTlasAndInstances(vehicles, assets);
  if (instances.length === 0) return null;

  const nodes = assets.nodeTexData;
  const tris = assets.triTexData;
  let bestT = maxT;
  let bestHit = null;

  for (let i = 0; i < instances.length; i++) {
    const inst = instances[i];
    const lb = assets.meshLocalBounds[inst.meshIdx];
    const m = inst.w2l;
    const lro = [
      m[0] * ro[0] + m[4] * ro[1] + m[8] * ro[2] + m[12],
      m[1] * ro[0] + m[5] * ro[1] + m[9] * ro[2] + m[13],
      m[2] * ro[0] + m[6] * ro[1] + m[10] * ro[2] + m[14],
    ];
    const lrd = [
      m[0] * rd[0] + m[4] * rd[1] + m[8] * rd[2],
      m[1] * rd[0] + m[5] * rd[1] + m[9] * rd[2],
      m[2] * rd[0] + m[6] * rd[1] + m[10] * rd[2],
    ];
    const invD = [
      Math.abs(lrd[0]) > 1e-8 ? 1 / lrd[0] : 1e8,
      Math.abs(lrd[1]) > 1e-8 ? 1 / lrd[1] : 1e8,
      Math.abs(lrd[2]) > 1e-8 ? 1 / lrd[2] : 1e8,
    ];
    if (IntersectAabbRay(lro, invD, lb.min, lb.max, bestT) >= bestT) continue;

    const stack = [TLAS_RESERVED_NODES + assets.meshMeta[inst.meshIdx].nodeOffset];
    while (stack.length > 0) {
      const nodeIdx = stack.pop();
      const t = nodeIdx * 8;
      const lof = Math.round(nodes[t + 3]);
      const cnt = Math.round(nodes[t + 7]);
      if (cnt > 0) {
        for (let k = 0; k < cnt; k++) {
          const tb = (lof + k) * 16;
          const th = IntersectTriMoller(
            lro, lrd,
            tris[tb + 0], tris[tb + 1], tris[tb + 2],
            tris[tb + 4], tris[tb + 5], tris[tb + 6],
            tris[tb + 8], tris[tb + 9], tris[tb + 10],
          );
          if (th > 1e-4 && th < bestT) {
            bestT = th;
            const l2w = inst.l2w;
            const nx = tris[tb + 12], ny = tris[tb + 13], nz = tris[tb + 14];
            const wnx = l2w[0] * nx + l2w[4] * ny + l2w[8] * nz;
            const wny = l2w[1] * nx + l2w[5] * ny + l2w[9] * nz;
            const wnz = l2w[2] * nx + l2w[6] * ny + l2w[10] * nz;
            const nl = Math.hypot(wnx, wny, wnz) || 1;
            const rawMat = Math.round(tris[tb + 3]);
            const matId = inst.matOverride >= 0 ? inst.matOverride : rawMat;
            bestHit = {
              t: th,
              pickCode: inst.pickCode,
              matId,
              inst,
              normal: [wnx / nl, wny / nl, wnz / nl],
              pos: [ro[0] + rd[0] * th, ro[1] + rd[1] * th, ro[2] + rd[2] * th],
            };
          }
        }
      } else {
        const leftIdx = lof;
        const rightIdx = lof + 1;
        const lt = leftIdx * 8;
        const rt = rightIdx * 8;
        const dL = IntersectAabbRay(lro, invD, [nodes[lt], nodes[lt + 1], nodes[lt + 2]], [nodes[lt + 4], nodes[lt + 5], nodes[lt + 6]], bestT);
        const dR = IntersectAabbRay(lro, invD, [nodes[rt], nodes[rt + 1], nodes[rt + 2]], [nodes[rt + 4], nodes[rt + 5], nodes[rt + 6]], bestT);
        if (dL < dR) {
          if (dR < bestT) stack.push(rightIdx);
          if (dL < bestT) stack.push(leftIdx);
        } else {
          if (dL < bestT) stack.push(leftIdx);
          if (dR < bestT) stack.push(rightIdx);
        }
      }
    }
  }
  return bestHit;
}
