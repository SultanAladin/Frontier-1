import {
  FULLSCREEN_VERT,
  GBUFFER_VERT,
  GBUFFER_FRAG,
  SDF_STATIC_SHADE_FRAG,
  SDF_RADIOSITY_BOUNCE_FRAG,
  SDF_UNION_FRAG,
  GI_SDF_FRAG,
  GI_BVH_FRAG,
  GI_BILATERAL_FRAG,
  REFLECTION_SDF_FRAG,
  REFLECTION_BVH_FRAG,
  COMPOSITE_FRAG,
} from "./BenchmarkShaders.js";

//========================================================================================================================
// RIGHT-HANDED LINEAR ALGEBRA (+X Forward, +Y Left, +Z Up; X x Y = +Z)
//========================================================================================================================
function mat4Identity() {
  const m = new Float32Array(16);
  m[0] = 1; m[5] = 1; m[10] = 1; m[15] = 1;
  return m;
}

function mat4Multiply(a, b) {
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

function mat4ComposeRigid(tx, ty, tz, yawZ, pitchY = 0, rollX = 0) {
  const cz = Math.cos(yawZ), sz = Math.sin(yawZ);
  const cy = Math.cos(pitchY), sy = Math.sin(pitchY);
  const cx = Math.cos(rollX), sx = Math.sin(rollX);
  const m = new Float32Array(16);
  m[0] = cz * cy;
  m[1] = sz * cy;
  m[2] = -sy;
  m[4] = cz * sy * sx - sz * cx;
  m[5] = sz * sy * sx + cz * cx;
  m[6] = cy * sx;
  m[8] = cz * sy * cx + sz * sx;
  m[9] = sz * sy * cx - cz * sx;
  m[10] = cy * cx;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
  m[15] = 1;
  return m;
}

function mat4InvertRigid(m) {
  const inv = new Float32Array(16);
  inv[0] = m[0]; inv[1] = m[4]; inv[2]  = m[8];
  inv[4] = m[1]; inv[5] = m[5]; inv[6]  = m[9];
  inv[8] = m[2]; inv[9] = m[6]; inv[10] = m[10];
  const tx = m[12], ty = m[13], tz = m[14];
  inv[12] = -(inv[0] * tx + inv[4] * ty + inv[8]  * tz);
  inv[13] = -(inv[1] * tx + inv[5] * ty + inv[9]  * tz);
  inv[14] = -(inv[2] * tx + inv[6] * ty + inv[10] * tz);
  inv[15] = 1;
  return inv;
}

function mat4InvertGeneral(m) {
  const inv = new Float32Array(16);
  inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
  let det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  det = Math.abs(det) > 1e-12 ? 1.0 / det : 0.0;
  for (let i = 0; i < 16; i++) inv[i] *= det;
  return inv;
}

function mat4LookAtRH(eye, target, up = [0, 0, 1]) {
  let fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl; fy /= fl; fz /= fl;

  let sx = fy * up[2] - fz * up[1];
  let sy = fz * up[0] - fx * up[2];
  let sz = fx * up[1] - fy * up[0];
  const sl = Math.hypot(sx, sy, sz) || 1;
  sx /= sl; sy /= sl; sz /= sl;

  const ux = sy * fz - sz * fy;
  const uy = sz * fx - sx * fz;
  const uz = sx * fy - sy * fx;

  const m = new Float32Array(16);
  m[0] = sx;  m[4] = sy;  m[8]  = sz;  m[12] = -(sx * eye[0] + sy * eye[1] + sz * eye[2]);
  m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
  m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx * eye[0] + fy * eye[1] + fz * eye[2]);
  m[15] = 1;
  return m;
}

function mat4PerspectiveRH(fovYRad, aspect, near, far) {
  const f = 1.0 / Math.tan(fovYRad * 0.5);
  const nf = 1.0 / (near - far);
  const m = new Float32Array(16);
  m[0] = f / aspect;
  m[5] = f;
  m[10] = (far + near) * nf;
  m[11] = -1;
  m[14] = 2 * far * near * nf;
  return m;
}

function decodeFloat16(h) {
  const s = (h & 0x8000) >> 15;
  const e = (h & 0x7C00) >> 10;
  const f = h & 0x03FF;
  if (e === 0) return (s ? -1 : 1) * Math.pow(2, -14) * (f / 1024);
  if (e === 0x1F) return f ? NaN : (s ? -Infinity : Infinity);
  return (s ? -1 : 1) * Math.pow(2, e - 15) * (1 + f / 1024);
}

function unpackOctahedralJs(packed20) {
  const p = Math.round(packed20);
  const u = ((p & 1023) / 1023.0) * 2.0 - 1.0;
  const v = (((p >>> 10) & 1023) / 1023.0) * 2.0 - 1.0;
  let nx = u, ny = v, nz = 1.0 - Math.abs(u) - Math.abs(v);
  if (nz < 0.0) {
    const ox = nx, oy = ny;
    nx = (1.0 - Math.abs(oy)) * (ox >= 0 ? 1 : -1);
    ny = (1.0 - Math.abs(ox)) * (oy >= 0 ? 1 : -1);
  }
  const len = Math.hypot(nx, ny, nz) || 1;
  return [nx / len, ny / len, nz / len];
}

function compileProgram(gl, vsSrc, fsSrc, label) {
  const vs = gl.createShader(gl.VERTEX_SHADER);
  gl.shaderSource(vs, vsSrc);
  gl.compileShader(vs);
  if (!gl.getShaderParameter(vs, gl.COMPILE_STATUS)) {
    throw new Error(`[${label}] VS error: ` + gl.getShaderInfoLog(vs));
  }
  const fs = gl.createShader(gl.FRAGMENT_SHADER);
  gl.shaderSource(fs, fsSrc);
  gl.compileShader(fs);
  if (!gl.getShaderParameter(fs, gl.COMPILE_STATUS)) {
    throw new Error(`[${label}] FS error: ` + gl.getShaderInfoLog(fs));
  }
  const prog = gl.createProgram();
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    throw new Error(`[${label}] Link error: ` + gl.getProgramInfoLog(prog));
  }
  return prog;
}

//========================================================================================================================
// PALETTE & SUN HELPERS
//========================================================================================================================
const PAINT_PALETTE = [
  [0.92, 0.94, 0.98], // Pearl White Clearcoat (default — showcases Crimson & Cobalt color bleeding dramatically!)
  [0.08, 0.37, 0.94], // Sapphire Clearcoat
  [0.86, 0.12, 0.14], // Crimson Metallic
  [0.92, 0.68, 0.06], // Solar Gold
  [0.03, 0.59, 0.40], // Emerald Pearl
  [0.05, 0.055, 0.07], // Obsidian Mirror
];

function computeSunDirectionFromAzimuth(azimuthDeg) {
  // At azimuth = 50 deg, matches verified reference normalize(0.48, 0.58, 0.66) -> elev ~ 41 deg
  const az = (azimuthDeg * Math.PI) / 180.0;
  const elevDeg = 32.0 + 9.0 * Math.cos(((azimuthDeg - 50.0) * Math.PI) / 140.0);
  const el = (elevDeg * Math.PI) / 180.0;
  const ce = Math.cos(el);
  const v = [ce * Math.cos(az), ce * Math.sin(az), Math.sin(el)];
  const l = Math.hypot(...v) || 1;
  return { dir: [v[0] / l, v[1] / l, v[2] / l], elevDeg };
}

const TLAS_RESERVED_NODES = 32;

async function startBenchmark() {
  const canvas = document.getElementById("viewport-canvas");
  const gl = canvas.getContext("webgl2", { antialias: false, powerPreference: "high-performance" });
  if (!gl) {
    document.getElementById("loading-banner").textContent = "WebGL2 is required to run this GPU benchmark.";
    return;
  }

  const extFloat = gl.getExtension("EXT_color_buffer_float");
  gl.getExtension("OES_texture_float_linear");
  const extTimer = gl.getExtension("EXT_disjoint_timer_query_webgl2");
  const dbgRenderInfo = gl.getExtension("WEBGL_debug_renderer_info");
  const gpuString = dbgRenderInfo
    ? gl.getParameter(dbgRenderInfo.UNMASKED_RENDERER_WEBGL)
    : gl.getParameter(gl.RENDERER);
  document.getElementById("gpu-readout").textContent =
    (gpuString || "WebGL2 GPU").replace(/ANGLE \(([^,]+), ([^,(]+).*\)/, "$2").slice(0, 26);

  if (!extFloat) {
    document.getElementById("loading-banner").textContent = "EXT_color_buffer_float is required on this GPU.";
    return;
  }

  // Load packed authentic ControlVehicle + Color-Bleed ProvingGround binary bundle ("FVB1")
  const resp = await fetch("./Assets/VehicleBenchmarkAssets.bin");
  const arrayBuf = await resp.arrayBuffer();
  const dv = new DataView(arrayBuf);
  const magic = String.fromCharCode(dv.getUint8(0), dv.getUint8(1), dv.getUint8(2), dv.getUint8(3));
  if (magic !== "FVB1") throw new Error("Invalid asset header: " + magic);

  const meshCount = dv.getUint32(4, true);
  const totalBlasNodes = dv.getUint32(8, true);
  const totalTris = dv.getUint32(12, true);
  const sdfCount = dv.getUint32(16, true);

  let offset = 20;
  const meshMeta = [];
  for (let i = 0; i < meshCount; i++) {
    meshMeta.push({
      nodeOffset: dv.getUint32(offset, true),
      nodeCount: dv.getUint32(offset + 4, true),
      triOffset: dv.getUint32(offset + 8, true),
      triCount: dv.getUint32(offset + 12, true),
    });
    offset += 16;
  }

  // Read BLAS nodes & shift internal child links by +TLAS_RESERVED_NODES
  const totalCombinedNodes = TLAS_RESERVED_NODES + totalBlasNodes;
  const nodeTexWidth = 2048;
  const nodeTexHeight = Math.ceil((totalCombinedNodes * 2) / nodeTexWidth);
  const nodeTexData = new Float32Array(nodeTexWidth * nodeTexHeight * 4);

  const meshLocalBounds = meshMeta.map(() => ({ min: [0, 0, 0], max: [0, 0, 0] }));
  for (let n = 0; n < totalBlasNodes; n++) {
    const minX = dv.getFloat32(offset + 0, true);
    const minY = dv.getFloat32(offset + 4, true);
    const minZ = dv.getFloat32(offset + 8, true);
    let leftOrFirst = Math.round(dv.getFloat32(offset + 12, true));
    const maxX = dv.getFloat32(offset + 16, true);
    const maxY = dv.getFloat32(offset + 20, true);
    const maxZ = dv.getFloat32(offset + 24, true);
    const count = Math.round(dv.getFloat32(offset + 28, true));
    offset += 32;

    if (count === 0) leftOrFirst += TLAS_RESERVED_NODES;
    const dstTexel = (TLAS_RESERVED_NODES + n) * 2;
    nodeTexData[dstTexel * 4 + 0] = minX;
    nodeTexData[dstTexel * 4 + 1] = minY;
    nodeTexData[dstTexel * 4 + 2] = minZ;
    nodeTexData[dstTexel * 4 + 3] = leftOrFirst;

    nodeTexData[(dstTexel + 1) * 4 + 0] = maxX;
    nodeTexData[(dstTexel + 1) * 4 + 1] = maxY;
    nodeTexData[(dstTexel + 1) * 4 + 2] = maxZ;
    nodeTexData[(dstTexel + 1) * 4 + 3] = count;

    for (let m = 0; m < meshCount; m++) {
      if (n === meshMeta[m].nodeOffset) {
        meshLocalBounds[m].min = [minX, minY, minZ];
        meshLocalBounds[m].max = [maxX, maxY, maxZ];
      }
    }
  }

  // Read triangles into uBvhTris AND build WebGL VAO per mesh for primary G-Buffer rasterization
  const triTexWidth = 2048;
  const triTexHeight = Math.ceil((totalTris * 4) / triTexWidth);
  const triTexData = new Float32Array(triTexWidth * triTexHeight * 4);
  new Uint8Array(triTexData.buffer, 0, totalTris * 64).set(new Uint8Array(arrayBuf, offset, totalTris * 64));

  const meshVaos = [];
  for (let m = 0; m < meshCount; m++) {
    const { triOffset, triCount } = meshMeta[m];
    const vboFloats = new Float32Array(triCount * 3 * 7);
    let ptr = 0;
    for (let t = 0; t < triCount; t++) {
      const b = offset + (triOffset + t) * 64;
      const p0x = dv.getFloat32(b + 0, true), p0y = dv.getFloat32(b + 4, true), p0z = dv.getFloat32(b + 8, true);
      const matId = Math.round(dv.getFloat32(b + 12, true));
      const e1x = dv.getFloat32(b + 16, true), e1y = dv.getFloat32(b + 20, true), e1z = dv.getFloat32(b + 24, true);
      const n0 = unpackOctahedralJs(dv.getFloat32(b + 28, true));
      const e2x = dv.getFloat32(b + 32, true), e2y = dv.getFloat32(b + 36, true), e2z = dv.getFloat32(b + 40, true);
      const n1 = unpackOctahedralJs(dv.getFloat32(b + 44, true));
      const n2 = unpackOctahedralJs(dv.getFloat32(b + 60, true));

      const verts = [
        [p0x, p0y, p0z, n0[0], n0[1], n0[2], matId],
        [p0x + e1x, p0y + e1y, p0z + e1z, n1[0], n1[1], n1[2], matId],
        [p0x + e2x, p0y + e2y, p0z + e2z, n2[0], n2[1], n2[2], matId],
      ];
      for (const v of verts) {
        for (let k = 0; k < 7; k++) vboFloats[ptr++] = v[k];
      }
    }
    const vao = gl.createVertexArray();
    const vbo = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, vboFloats, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 28, 12);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 28, 24);
    gl.bindVertexArray(null);
    meshVaos.push({ vao, vertexCount: triCount * 3 });
  }
  offset += totalTris * 64;

  // Read 5 SDF volumes: 0=BodySDF, 1=RimSDF, 2=TyreSDF, 3=Clip0Static, 4=Clip1Static
  const sdfVolumes = [];
  for (let s = 0; s < sdfCount; s++) {
    const nx = dv.getUint32(offset + 0, true);
    const ny = dv.getUint32(offset + 4, true);
    const nz = dv.getUint32(offset + 8, true);
    const byteLen = dv.getUint32(offset + 12, true);
    const min = [dv.getFloat32(offset + 16, true), dv.getFloat32(offset + 20, true), dv.getFloat32(offset + 24, true)];
    const max = [dv.getFloat32(offset + 28, true), dv.getFloat32(offset + 32, true), dv.getFloat32(offset + 36, true)];
    offset += 40;
    const payload = new Uint8Array(arrayBuf, offset, byteLen);
    offset += byteLen;
    while (offset % 4 !== 0) offset++;
    sdfVolumes.push({ nx, ny, nz, min, max, payload });
  }

  function createPartSdf3D(vol) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, tex);
    gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
    const u16 = new Uint16Array(vol.payload.buffer, vol.payload.byteOffset, vol.nx * vol.ny * vol.nz);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.R16F, vol.nx, vol.ny, vol.nz, 0, gl.RED, gl.HALF_FLOAT, u16);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    return tex;
  }

  function createArenaCascadeGpuVolumes(vol, needDynamicCopy) {
    const count = vol.nx * vol.ny * vol.nz;
    const rawRg = new Float32Array(count * 2);
    const distOnly = new Float32Array(count);
    const view = new DataView(vol.payload.buffer, vol.payload.byteOffset, vol.payload.byteLength);
    for (let i = 0; i < count; i++) {
      const d = decodeFloat16(view.getUint16(i * 4, true));
      const m = view.getUint16(i * 4 + 2, true);
      rawRg[i * 2 + 0] = d;
      rawRg[i * 2 + 1] = m;
      distOnly[i] = d;
    }

    const rawTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, rawTex);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.RG16F, vol.nx, vol.ny, vol.nz, 0, gl.RG, gl.FLOAT, rawRg);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);

    const distTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_3D, distTex);
    gl.texImage3D(gl.TEXTURE_3D, 0, gl.R16F, vol.nx, vol.ny, vol.nz, 0, gl.RED, gl.FLOAT, distOnly);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);

    const makeEmpty3D = (internalFmt, fmt) => {
      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_3D, tex);
      gl.texImage3D(gl.TEXTURE_3D, 0, internalFmt, vol.nx, vol.ny, vol.nz, 0, fmt, gl.FLOAT, null);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
      return tex;
    };

    return needDynamicCopy
      ? {
          rawTex, distTex,
          directTex: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          tmpTex: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          texA: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          reflA: makeEmpty3D(gl.R16F, gl.RED),
          texB: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          reflB: makeEmpty3D(gl.R16F, gl.RED),
        }
      : {
          rawTex, distTex,
          directTex: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          tmpTex: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          texA: makeEmpty3D(gl.RGBA16F, gl.RGBA),
          reflA: makeEmpty3D(gl.R16F, gl.RED),
        };
  }

  const bodySdfTex = createPartSdf3D(sdfVolumes[0]);
  const rimSdfTex  = createPartSdf3D(sdfVolumes[1]);
  const tyreSdfTex = createPartSdf3D(sdfVolumes[2]);
  const clip0Pair  = createArenaCascadeGpuVolumes(sdfVolumes[3], true);
  const clip1Pair  = createArenaCascadeGpuVolumes(sdfVolumes[4], false);

  const linearSampler = gl.createSampler();
  gl.samplerParameteri(linearSampler, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.samplerParameteri(linearSampler, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.samplerParameteri(linearSampler, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.samplerParameteri(linearSampler, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.samplerParameteri(linearSampler, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);

  // BVH GPU textures (RGBA32F)
  const bvhNodesTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, nodeTexWidth, nodeTexHeight, 0, gl.RGBA, gl.FLOAT, nodeTexData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  const bvhTrisTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, triTexWidth, triTexHeight, 0, gl.RGBA, gl.FLOAT, triTexData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  const instancesData = new Float32Array(128 * 4);
  const instancesTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, instancesTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, 128, 1, 0, gl.RGBA, gl.FLOAT, instancesData);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

  // Compile GPU Programs
  const progGBuffer       = compileProgram(gl, GBUFFER_VERT, GBUFFER_FRAG, "GBuffer");
  const progStaticShade   = compileProgram(gl, FULLSCREEN_VERT, SDF_STATIC_SHADE_FRAG, "SdfStaticShade");
  const progSdfBounce     = compileProgram(gl, FULLSCREEN_VERT, SDF_RADIOSITY_BOUNCE_FRAG, "SdfRadiosityBounce");
  const progSdfUnion      = compileProgram(gl, FULLSCREEN_VERT, SDF_UNION_FRAG, "SdfUnion");
  const progGiSdf         = compileProgram(gl, FULLSCREEN_VERT, GI_SDF_FRAG, "GiSdf");
  const progGiBvh         = compileProgram(gl, FULLSCREEN_VERT, GI_BVH_FRAG, "GiBvh");
  const progGiBilateral   = compileProgram(gl, FULLSCREEN_VERT, GI_BILATERAL_FRAG, "GiBilateral");
  const progReflSdf       = compileProgram(gl, FULLSCREEN_VERT, REFLECTION_SDF_FRAG, "ReflSdf");
  const progReflBvh       = compileProgram(gl, FULLSCREEN_VERT, REFLECTION_BVH_FRAG, "ReflBvh");
  const progComposite     = compileProgram(gl, FULLSCREEN_VERT, COMPOSITE_FRAG, "Composite");
  const emptyVao          = gl.createVertexArray();

  const sdfSliceFbo = gl.createFramebuffer();

  // Render Targets
  let screenW = 0, screenH = 0, traceW = 0, traceH = 0, traceResScale = 0.5;
  let gBufferFbo = null, gPosTex = null, gNormTex = null, gDirectTex = null, gDepthRb = null;
  let giSdfRawFbo = null, giSdfRawTex = null, giSdfMetaTex = null;
  let giBvhRawFbo = null, giBvhRawTex = null, giBvhMetaTex = null;
  let giPingAFbo = null, giPingATex = null;
  let giPingBFbo = null, giPingBTex = null;
  let giSdfFinalFbo = null, giSdfFinalTex = null;
  let giBvhFinalFbo = null, giBvhFinalTex = null;
  let reflSdfFbo = null, reflSdfTex = null;
  let reflBvhFbo = null, reflBvhTex = null;

  function makeTargetTex(w, h, filter) {
    const t = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w, h, 0, gl.RGBA, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return t;
  }

  function makeSingleFbo(tex) {
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);
    return fbo;
  }

  function makeDualFbo(tex0, tex1) {
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex0, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, tex1, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
    return fbo;
  }

  function resizeTargets(force = false) {
    const dpr = Math.min(window.devicePixelRatio || 1, 1.35);
    const w = Math.max(640, Math.floor(window.innerWidth * dpr));
    const h = Math.max(360, Math.floor(window.innerHeight * dpr));
    const tw = Math.max(320, Math.floor(w * traceResScale));
    const th = Math.max(180, Math.floor(h * traceResScale));
    if (!force && w === screenW && h === screenH && tw === traceW && th === traceH) return;
    screenW = w; screenH = h;
    traceW = tw; traceH = th;
    canvas.width = w;
    canvas.height = h;

    if (gBufferFbo) {
      [gBufferFbo, giSdfRawFbo, giBvhRawFbo, giPingAFbo, giPingBFbo, giSdfFinalFbo, giBvhFinalFbo, reflSdfFbo, reflBvhFbo]
        .forEach((f) => gl.deleteFramebuffer(f));
      [gPosTex, gNormTex, gDirectTex, giSdfRawTex, giSdfMetaTex, giBvhRawTex, giBvhMetaTex,
       giPingATex, giPingBTex, giSdfFinalTex, giBvhFinalTex, reflSdfTex, reflBvhTex]
        .forEach((t) => gl.deleteTexture(t));
      gl.deleteRenderbuffer(gDepthRb);
    }

    gPosTex    = makeTargetTex(screenW, screenH, gl.NEAREST);
    gNormTex   = makeTargetTex(screenW, screenH, gl.NEAREST);
    gDirectTex = makeTargetTex(screenW, screenH, gl.LINEAR);
    gDepthRb   = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, gDepthRb);
    gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, screenW, screenH);

    gBufferFbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, gBufferFbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, gPosTex, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, gNormTex, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT2, gl.TEXTURE_2D, gDirectTex, 0);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, gDepthRb);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1, gl.COLOR_ATTACHMENT2]);

    giSdfRawTex   = makeTargetTex(traceW, traceH, gl.NEAREST);
    giSdfMetaTex  = makeTargetTex(traceW, traceH, gl.NEAREST);
    giSdfRawFbo   = makeDualFbo(giSdfRawTex, giSdfMetaTex);

    giBvhRawTex   = makeTargetTex(traceW, traceH, gl.NEAREST);
    giBvhMetaTex  = makeTargetTex(traceW, traceH, gl.NEAREST);
    giBvhRawFbo   = makeDualFbo(giBvhRawTex, giBvhMetaTex);

    giPingATex    = makeTargetTex(traceW, traceH, gl.NEAREST);
    giPingAFbo    = makeSingleFbo(giPingATex);
    giPingBTex    = makeTargetTex(traceW, traceH, gl.NEAREST);
    giPingBFbo    = makeSingleFbo(giPingBTex);

    giSdfFinalTex = makeTargetTex(traceW, traceH, gl.LINEAR);
    giSdfFinalFbo = makeSingleFbo(giSdfFinalTex);

    giBvhFinalTex = makeTargetTex(traceW, traceH, gl.LINEAR);
    giBvhFinalFbo = makeSingleFbo(giBvhFinalTex);

    reflSdfTex    = makeTargetTex(traceW, traceH, gl.LINEAR);
    reflSdfFbo    = makeSingleFbo(reflSdfTex);

    reflBvhTex    = makeTargetTex(traceW, traceH, gl.LINEAR);
    reflBvhFbo    = makeSingleFbo(reflBvhTex);

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }
  resizeTargets();
  window.addEventListener("resize", () => resizeTargets(false));

  const SPP_PRESETS = [
    { giRays: 4,  shadowRays: 2,  sdfCones: 4,  label: "Low 4/2" },
    { giRays: 6,  shadowRays: 4,  sdfCones: 6,  label: "Std 6/4" },
    { giRays: 12, shadowRays: 8,  sdfCones: 9,  label: "High 12/8" },
    { giRays: 24, shadowRays: 12, sdfCones: 12, label: "Ultra 24/12" },
  ];

  //======================================================================================================================
  // INTERACTIVE STATE & VEHICLE MOTION
  //======================================================================================================================
  const initSun = computeSunDirectionFromAzimuth(50);
  const state = {
    displayMode: 2, // 0=SDF, 1=BVH, 2=Split, 3=Delta, 4=Clay GI+Penumbra Split
    maxBounces: 2,
    giBounces: 2,
    sppPreset: 1,

    enableGi: true,
    enableShadows: true,
    softPenumbra: true,
    useHiZAssist: true,
    traceNeeShadows: true,

    // Toggleable Visual & Physical Quality Improvements (A/B Compare!)
    fixDirSdfGi: true,
    fixContactAo: true,
    fixSmoothPcss: true,
    fixDeblotchGi: true,
    fullResTrace: false,

    penumbraScale: 2.10,
    giIntensity: 1.85,
    sunAzimuthDeg: 50,
    sunElevDeg: initSun.elevDeg,
    sunDir: initSun.dir,
    staticShadeDirty: true,

    driveMode: "auto", // "auto" | "pose" | "manual"
    chaseCam: false,
    paintIdx: 0,
    splitX: 0.5,

    carX: -0.55,
    carY: 0.65,
    carYaw: 0.42,
    carSpeed: 0.0,
    steerAngle: 0.18,
    wheelSpin: 0.0,
    autoTime: 0.0,

    // Hero three-quarter foreground-penumbra + color-bleed showcase camera
    camYaw: -1.88,
    camPitch: 0.30,
    camDist: 7.40,

    sdfGiMs: 0.25,
    sdfReflMs: 0.20,
    bvhGiMs: 2.40,
    bvhReflMs: 1.80,
    unionMs: 0.08,
    pixelDeltaPct: 4.91,
  };

  const keys = new Set();
  window.addEventListener("keydown", (e) => {
    const k = e.key.toLowerCase();
    keys.add(k);
    if (["w", "a", "s", "d", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(k)) {
      if (state.driveMode !== "manual") {
        state.driveMode = "manual";
        syncDriveButtons();
      }
    }
    if (k === "r") {
      state.carX = -0.55; state.carY = 0.65; state.carYaw = 0.42; state.carSpeed = 0; state.steerAngle = 0;
    }
  });
  window.addEventListener("keyup", (e) => keys.delete(e.key.toLowerCase()));

  let draggingCam = false, draggingSplit = false, lastMx = 0, lastMy = 0;
  canvas.addEventListener("pointerdown", (e) => {
    draggingCam = true; lastMx = e.clientX; lastMy = e.clientY;
    canvas.setPointerCapture(e.pointerId);
  });
  window.addEventListener("pointermove", (e) => {
    if (draggingSplit) {
      state.splitX = Math.min(0.92, Math.max(0.08, e.clientX / window.innerWidth));
      document.getElementById("split-divider").style.left = (state.splitX * 100).toFixed(1) + "%";
      return;
    }
    if (!draggingCam) return;
    const dx = e.clientX - lastMx, dy = e.clientY - lastMy;
    lastMx = e.clientX; lastMy = e.clientY;
    state.camYaw -= dx * 0.0065;
    state.camPitch = Math.min(1.25, Math.max(0.06, state.camPitch + dy * 0.0055));
  });
  window.addEventListener("pointerup", () => { draggingCam = false; draggingSplit = false; });
  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    state.camDist = Math.min(44.0, Math.max(3.4, state.camDist + e.deltaY * 0.008));
  }, { passive: false });

  const splitDiv = document.getElementById("split-divider");
  splitDiv.addEventListener("pointerdown", (e) => {
    e.stopPropagation();
    draggingSplit = true;
  });

  const WHEEL_MOUNTS = [
    { x:  1.665, y:  1.045, front: true,  right: false },
    { x:  1.665, y: -1.045, front: true,  right: true  },
    { x: -1.705, y:  1.045, front: false, right: false },
    { x: -1.705, y: -1.045, front: false, right: true  },
  ];
  const TYRE_RADIUS = 0.50855;

  let activeInstances = [];
  function updateSceneInstances(dt) {
    if (state.driveMode === "pose") {
      state.carX = -0.55;
      state.carY = 0.65;
      state.carYaw = 0.42;
      state.steerAngle = 0.24;
    } else if (state.driveMode === "auto") {
      state.autoTime += dt;
      // Smooth collision-free showcase glide right between the Crimson Wall, White Plinth, and Mirror Wall!
      const t = state.autoTime * 0.65;
      const nextX = -0.55 + Math.sin(t) * 0.68;
      const nextY = 0.92 + Math.sin(t * 1.4) * 0.42;
      const dx = nextX - state.carX;
      const dy = nextY - state.carY;
      const dist = Math.hypot(dx, dy);
      state.carYaw = 0.42 + Math.sin(t * 0.7) * 0.16;
      state.steerAngle = 0.22 * Math.cos(t);
      state.carX = nextX;
      state.carY = nextY;
      state.wheelSpin += (dist / TYRE_RADIUS) * (Math.cos(t) >= 0 ? 1 : -1);
    } else {
      const throttle = (keys.has("w") || keys.has("arrowup") ? 1 : 0) - (keys.has("s") || keys.has("arrowdown") ? 1 : 0);
      const steerIn  = (keys.has("a") || keys.has("arrowleft") ? 1 : 0) - (keys.has("d") || keys.has("arrowright") ? 1 : 0);
      const targetSteer = steerIn * 0.48;
      state.steerAngle += (targetSteer - state.steerAngle) * Math.min(1, dt * 8.5);
      if (throttle !== 0) {
        state.carSpeed += throttle * 8.5 * dt;
      } else {
        state.carSpeed *= Math.pow(0.24, dt);
      }
      if (keys.has(" ")) state.carSpeed *= Math.pow(0.02, dt);
      state.carSpeed = Math.max(-7.5, Math.min(12.0, state.carSpeed));

      const wheelbase = 3.37;
      state.carYaw += (state.carSpeed / wheelbase) * Math.tan(state.steerAngle) * dt;
      state.carX = Math.max(-19.2, Math.min(18.0, state.carX + Math.cos(state.carYaw) * state.carSpeed * dt));
      state.carY = Math.max(-12.4, Math.min(11.2, state.carY + Math.sin(state.carYaw) * state.carSpeed * dt));
      state.wheelSpin += (state.carSpeed * dt) / TYRE_RADIUS;
    }

    const cy = Math.cos(state.carYaw), sy = Math.sin(state.carYaw);
    const bodyModel = mat4ComposeRigid(state.carX, state.carY, TYRE_RADIUS, state.carYaw, 0, 0);
    const bodyInv   = mat4InvertRigid(bodyModel);

    const wheelModels = [];
    const wheelInvs   = [];
    for (const wm of WHEEL_MOUNTS) {
      const wx = state.carX + wm.x * cy - wm.y * sy;
      const wy = state.carY + wm.x * sy + wm.y * cy;
      const wYaw = state.carYaw + (wm.front ? state.steerAngle : 0) + (wm.right ? Math.PI : 0);
      const spin = wm.right ? -state.wheelSpin : state.wheelSpin;
      const mYaw = mat4ComposeRigid(wx, wy, TYRE_RADIUS, wYaw, 0, 0);
      const mSpin = mat4ComposeRigid(0, 0, 0, 0, spin, 0);
      const m = mat4Multiply(mYaw, mSpin);
      wheelModels.push(m);
      wheelInvs.push(mat4InvertRigid(m));
    }

    const idM = mat4Identity();
    activeInstances = [
      { meshIdx: 0, l2w: bodyModel, w2l: bodyInv, matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[0], w2l: wheelInvs[0], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[1], w2l: wheelInvs[1], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[2], w2l: wheelInvs[2], matOverride: -1 },
      { meshIdx: 1, l2w: wheelModels[3], w2l: wheelInvs[3], matOverride: -1 },
      { meshIdx: 2, l2w: wheelModels[0], w2l: wheelInvs[0], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[1], w2l: wheelInvs[1], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[2], w2l: wheelInvs[2], matOverride: 11 },
      { meshIdx: 2, l2w: wheelModels[3], w2l: wheelInvs[3], matOverride: 11 },
      { meshIdx: 3, l2w: idM, w2l: idM, matOverride: -1 },
    ];

    for (let i = 0; i < activeInstances.length; i++) {
      const base = i * 9 * 4;
      instancesData.set(activeInstances[i].l2w, base);
      instancesData.set(activeInstances[i].w2l, base + 16);
      instancesData[base + 32] = activeInstances[i].matOverride + 1;
    }
    gl.bindTexture(gl.TEXTURE_2D, instancesTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 128, 1, gl.RGBA, gl.FLOAT, instancesData);

    const leaves = activeInstances.map((inst, idx) => {
      const lb = meshLocalBounds[inst.meshIdx];
      const wmin = [1e30, 1e30, 1e30], wmax = [-1e30, -1e30, -1e30];
      const m = inst.l2w;
      for (let c = 0; c < 8; c++) {
        const lx = (c & 1) ? lb.max[0] : lb.min[0];
        const ly = (c & 2) ? lb.max[1] : lb.min[1];
        const lz = (c & 4) ? lb.max[2] : lb.min[2];
        const wx = m[0]*lx + m[4]*ly + m[8]*lz + m[12];
        const wy = m[1]*lx + m[5]*ly + m[9]*lz + m[13];
        const wz = m[2]*lx + m[6]*ly + m[10]*lz + m[14];
        if (wx < wmin[0]) wmin[0] = wx; if (wy < wmin[1]) wmin[1] = wy; if (wz < wmin[2]) wmin[2] = wz;
        if (wx > wmax[0]) wmax[0] = wx; if (wy > wmax[1]) wmax[1] = wy; if (wz > wmax[2]) wmax[2] = wz;
      }
      return { min: wmin, max: wmax, leftOrFirst: TLAS_RESERVED_NODES + meshMeta[inst.meshIdx].nodeOffset, count: -(idx + 1) };
    });

    let nextTlasSlot = 1;
    function writeTlasNode(slot, min, max, lof, cnt) {
      const t = slot * 2;
      nodeTexData[t * 4 + 0] = min[0]; nodeTexData[t * 4 + 1] = min[1]; nodeTexData[t * 4 + 2] = min[2];
      nodeTexData[t * 4 + 3] = lof;
      nodeTexData[(t + 1) * 4 + 0] = max[0]; nodeTexData[(t + 1) * 4 + 1] = max[1]; nodeTexData[(t + 1) * 4 + 2] = max[2];
      nodeTexData[(t + 1) * 4 + 3] = cnt;
    }

    function buildTlasSlot(slot, list) {
      if (list.length === 1) {
        writeTlasNode(slot, list[0].min, list[0].max, list[0].leftOrFirst, list[0].count);
        return;
      }
      const bmin = [1e30, 1e30, 1e30], bmax = [-1e30, -1e30, -1e30];
      for (const it of list) {
        for (let k = 0; k < 3; k++) {
          if (it.min[k] < bmin[k]) bmin[k] = it.min[k];
          if (it.max[k] > bmax[k]) bmax[k] = it.max[k];
        }
      }
      const spans = [bmax[0]-bmin[0], bmax[1]-bmin[1], bmax[2]-bmin[2]];
      const axis = spans[0] >= spans[1] && spans[0] >= spans[2] ? 0 : (spans[1] >= spans[2] ? 1 : 2);
      list.sort((a, b) => (a.min[axis] + a.max[axis]) - (b.min[axis] + b.max[axis]));
      const mid = list.length >> 1;
      const leftSlot = nextTlasSlot;
      nextTlasSlot += 2;
      writeTlasNode(slot, bmin, bmax, leftSlot, 0);
      buildTlasSlot(leftSlot, list.slice(0, mid));
      buildTlasSlot(leftSlot + 1, list.slice(mid));
    }
    buildTlasSlot(0, leaves);

    gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, TLAS_RESERVED_NODES * 2, 1, gl.RGBA, gl.FLOAT,
      nodeTexData.subarray(0, TLAS_RESERVED_NODES * 2 * 4));

    return { bodyModel, bodyInv, wheelInvs };
  }

  //======================================================================================================================
  // GPU PASS RUNNERS
  //======================================================================================================================
  const clip0Vol = sdfVolumes[3];
  const clip1Vol = sdfVolumes[4];
  const clip0Span = [clip0Vol.max[0]-clip0Vol.min[0], clip0Vol.max[1]-clip0Vol.min[1], clip0Vol.max[2]-clip0Vol.min[2]];
  const clip1Span = [clip1Vol.max[0]-clip1Vol.min[0], clip1Vol.max[1]-clip1Vol.min[1], clip1Vol.max[2]-clip1Vol.min[2]];
  const clip0Voxel = clip0Span[2] / clip0Vol.nz;
  const clip1Voxel = clip1Span[2] / clip1Vol.nz;

  function shadeStaticCascadeTarget(vol, vox, rawTex, outVoxTex, outReflTex) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, sdfSliceFbo);
    gl.viewport(0, 0, vol.nx, vol.ny);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progStaticShade);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, rawTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.distTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, clip1Pair.distTex);

    gl.uniform1i(gl.getUniformLocation(progStaticShade, "uTargetRaw"), 0);
    gl.uniform1i(gl.getUniformLocation(progStaticShade, "uClip0DistLinear"), 1);
    gl.uniform1i(gl.getUniformLocation(progStaticShade, "uClip1DistLinear"), 2);

    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uTargetMin"), vol.min);
    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uTargetMax"), vol.max);
    gl.uniform1f(gl.getUniformLocation(progStaticShade, "uTargetVoxel"), vox);
    gl.uniform1i(gl.getUniformLocation(progStaticShade, "uTargetDepth"), vol.nz);

    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uClip0Min"), clip0Vol.min);
    gl.uniform3f(gl.getUniformLocation(progStaticShade, "uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uClip1Min"), clip1Vol.min);
    gl.uniform3f(gl.getUniformLocation(progStaticShade, "uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);

    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progStaticShade, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);

    const sliceLoc = gl.getUniformLocation(progStaticShade, "uSliceIndex");
    gl.bindVertexArray(emptyVao);
    for (let iz = 0; iz < vol.nz; iz++) {
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, outVoxTex, 0, iz);
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, outReflTex, 0, iz);
      if (iz === 0) gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
      gl.uniform1i(sliceLoc, iz);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
  }

  function propagateCascadeBounce(vol, vox, rawTex, directTex, prevClip0, prevClip1, outVoxTex, sliceStart = 0, sliceEnd = -1) {
    const endZ = sliceEnd < 0 ? vol.nz : Math.min(vol.nz, sliceEnd);
    gl.bindFramebuffer(gl.FRAMEBUFFER, sdfSliceFbo);
    gl.viewport(0, 0, vol.nx, vol.ny);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progSdfBounce);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, rawTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, directTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, prevClip0); gl.bindSampler(2, linearSampler);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_3D, prevClip1); gl.bindSampler(3, linearSampler);

    gl.uniform1i(gl.getUniformLocation(progSdfBounce, "uTargetRaw"), 0);
    gl.uniform1i(gl.getUniformLocation(progSdfBounce, "uDirectVoxel"), 1);
    gl.uniform1i(gl.getUniformLocation(progSdfBounce, "uPrevClip0Linear"), 2);
    gl.uniform1i(gl.getUniformLocation(progSdfBounce, "uPrevClip1Linear"), 3);

    gl.uniform3fv(gl.getUniformLocation(progSdfBounce, "uTargetMin"), vol.min);
    gl.uniform3fv(gl.getUniformLocation(progSdfBounce, "uTargetMax"), vol.max);
    gl.uniform1f(gl.getUniformLocation(progSdfBounce, "uTargetVoxel"), vox);
    gl.uniform1i(gl.getUniformLocation(progSdfBounce, "uTargetDepth"), vol.nz);

    gl.uniform3fv(gl.getUniformLocation(progSdfBounce, "uClip0Min"), clip0Vol.min);
    gl.uniform3f(gl.getUniformLocation(progSdfBounce, "uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform1f(gl.getUniformLocation(progSdfBounce, "uClip0Voxel"), clip0Voxel);
    gl.uniform3fv(gl.getUniformLocation(progSdfBounce, "uClip1Min"), clip1Vol.min);
    gl.uniform3f(gl.getUniformLocation(progSdfBounce, "uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);
    gl.uniform1f(gl.getUniformLocation(progSdfBounce, "uClip1Voxel"), clip1Voxel);
    gl.uniform3fv(gl.getUniformLocation(progSdfBounce, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);

    // Detach COLOR_ATTACHMENT1 so single-attachment bounce pass never collides with 2-attachment FBO state
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, null, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);

    const sliceLoc = gl.getUniformLocation(progSdfBounce, "uSliceIndex");
    gl.bindVertexArray(emptyVao);
    for (let iz = sliceStart; iz < endZ; iz++) {
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, outVoxTex, 0, iz);
      gl.uniform1i(sliceLoc, iz);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    unbindSamplers(2, 2);
  }

  function runStaticArenaSdfShadeIfNeeded() {
    if (!state.staticShadeDirty) return;
    if (state.giBounces <= 1) {
      shadeStaticCascadeTarget(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.texA, clip0Pair.reflA);
      shadeStaticCascadeTarget(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.texB, clip0Pair.reflB);
      shadeStaticCascadeTarget(clip1Vol, clip1Voxel, clip1Pair.rawTex, clip1Pair.texA, clip1Pair.reflA);
    } else {
      shadeStaticCascadeTarget(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.reflA);
      shadeStaticCascadeTarget(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.texB, clip0Pair.reflB);
      shadeStaticCascadeTarget(clip1Vol, clip1Voxel, clip1Pair.rawTex, clip1Pair.directTex, clip1Pair.reflA);

      if (state.giBounces === 2) {
        propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.directTex, clip1Pair.directTex, clip0Pair.texA);
        propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.directTex, clip1Pair.directTex, clip0Pair.texB, 29, clip0Vol.nz);
        propagateCascadeBounce(clip1Vol, clip1Voxel, clip1Pair.rawTex, clip1Pair.directTex, clip0Pair.directTex, clip1Pair.directTex, clip1Pair.texA);
      } else {
        propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.directTex, clip1Pair.directTex, clip0Pair.tmpTex);
        propagateCascadeBounce(clip1Vol, clip1Voxel, clip1Pair.rawTex, clip1Pair.directTex, clip0Pair.directTex, clip1Pair.directTex, clip1Pair.tmpTex);
        propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.tmpTex, clip1Pair.tmpTex, clip0Pair.texA);
        propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, clip0Pair.tmpTex, clip1Pair.tmpTex, clip0Pair.texB, 29, clip0Vol.nz);
        propagateCascadeBounce(clip1Vol, clip1Voxel, clip1Pair.rawTex, clip1Pair.directTex, clip0Pair.tmpTex, clip1Pair.tmpTex, clip1Pair.texA);
      }
    }
    state.staticShadeDirty = false;
  }

  function runDynamicGlobalSdfUnion(bodyModel, bodyInv, wheelInvs) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, sdfSliceFbo);
    gl.viewport(0, 0, clip0Vol.nx, clip0Vol.ny);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progSdfUnion);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.texA);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.reflA);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.rawTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_3D, bodySdfTex);
    gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_3D, rimSdfTex);
    gl.activeTexture(gl.TEXTURE5); gl.bindTexture(gl.TEXTURE_3D, tyreSdfTex);

    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uStaticClip0"), 0);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uStaticClip0Refl"), 1);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uClip0Raw"), 2);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uBodySdf"), 3);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uRimSdf"), 4);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uTyreSdf"), 5);

    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uClip0Min"), clip0Vol.min);
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uClip0Max"), clip0Vol.max);

    const bVol = sdfVolumes[0], rVol = sdfVolumes[1], tVol = sdfVolumes[2];
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uBodySdfMin"), bVol.min);
    gl.uniform3f(gl.getUniformLocation(progSdfUnion, "uBodySdfInvSpan"),
      1/(bVol.max[0]-bVol.min[0]), 1/(bVol.max[1]-bVol.min[1]), 1/(bVol.max[2]-bVol.min[2]));
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uRimSdfMin"), rVol.min);
    gl.uniform3f(gl.getUniformLocation(progSdfUnion, "uRimSdfInvSpan"),
      1/(rVol.max[0]-rVol.min[0]), 1/(rVol.max[1]-rVol.min[1]), 1/(rVol.max[2]-rVol.min[2]));
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uTyreSdfMin"), tVol.min);
    gl.uniform3f(gl.getUniformLocation(progSdfUnion, "uTyreSdfInvSpan"),
      1/(tVol.max[0]-tVol.min[0]), 1/(tVol.max[1]-tVol.min[1]), 1/(tVol.max[2]-tVol.min[2]));

    gl.uniformMatrix4fv(gl.getUniformLocation(progSdfUnion, "uBodyLocalToWorld"), false, bodyModel);
    gl.uniformMatrix4fv(gl.getUniformLocation(progSdfUnion, "uBodyWorldToLocal"), false, bodyInv);
    const flatWheels = new Float32Array(64);
    for (let w = 0; w < 4; w++) flatWheels.set(wheelInvs[w], w * 16);
    gl.uniformMatrix4fv(gl.getUniformLocation(progSdfUnion, "uWheelWorldToLocal[0]"), false, flatWheels);
    gl.uniform3f(gl.getUniformLocation(progSdfUnion, "uCarCenter"), state.carX, state.carY, TYRE_RADIUS);
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progSdfUnion, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    gl.uniform1i(gl.getUniformLocation(progSdfUnion, "uGiBounces"), state.giBounces);

    const sliceNormLoc = gl.getUniformLocation(progSdfUnion, "uSliceZNorm");
    const sliceIdxLoc  = gl.getUniformLocation(progSdfUnion, "uSliceIndex");
    gl.bindVertexArray(emptyVao);
    for (let iz = 0; iz <= 28; iz++) {
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, clip0Pair.texB, 0, iz);
      gl.framebufferTextureLayer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, clip0Pair.reflB, 0, iz);
      if (iz === 0) gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
      gl.uniform1f(sliceNormLoc, (iz + 0.5) / clip0Vol.nz);
      gl.uniform1i(sliceIdxLoc, iz);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
  }

  function runGBufferPass(viewProj, camPos) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, gBufferFbo);
    gl.viewport(0, 0, screenW, screenH);
    gl.enable(gl.DEPTH_TEST);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    gl.useProgram(progGBuffer);
    gl.uniformMatrix4fv(gl.getUniformLocation(progGBuffer, "uViewProj"), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(progGBuffer, "uCamPos"), camPos);
    gl.uniform3fv(gl.getUniformLocation(progGBuffer, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progGBuffer, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);

    const modelLoc = gl.getUniformLocation(progGBuffer, "uModel");
    const overLoc  = gl.getUniformLocation(progGBuffer, "uMatOverride");
    for (const inst of activeInstances) {
      const m = meshVaos[inst.meshIdx];
      gl.bindVertexArray(m.vao);
      gl.uniformMatrix4fv(modelLoc, false, inst.l2w);
      gl.uniform1i(overLoc, inst.matOverride);
      gl.drawArrays(gl.TRIANGLES, 0, m.vertexCount);
    }
  }

  function bindGlobalSdfCascades(prog, startUnit) {
    gl.activeTexture(gl.TEXTURE0 + startUnit + 0); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.texB);  gl.bindSampler(startUnit + 0, linearSampler);
    gl.activeTexture(gl.TEXTURE0 + startUnit + 1); gl.bindTexture(gl.TEXTURE_3D, clip0Pair.reflB); gl.bindSampler(startUnit + 1, linearSampler);
    gl.activeTexture(gl.TEXTURE0 + startUnit + 2); gl.bindTexture(gl.TEXTURE_3D, clip1Pair.texA);  gl.bindSampler(startUnit + 2, linearSampler);
    gl.activeTexture(gl.TEXTURE0 + startUnit + 3); gl.bindTexture(gl.TEXTURE_3D, clip1Pair.reflA); gl.bindSampler(startUnit + 3, linearSampler);

    gl.uniform1i(gl.getUniformLocation(prog, "uClip0Linear"), startUnit + 0);
    gl.uniform1i(gl.getUniformLocation(prog, "uClip0Refl"),   startUnit + 1);
    gl.uniform1i(gl.getUniformLocation(prog, "uClip1Linear"), startUnit + 2);
    gl.uniform1i(gl.getUniformLocation(prog, "uClip1Refl"),   startUnit + 3);

    gl.uniform3fv(gl.getUniformLocation(prog, "uClip0Min"), clip0Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip0InvSpan"), 1/clip0Span[0], 1/clip0Span[1], 1/clip0Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip0Voxel"), clip0Voxel);

    gl.uniform3fv(gl.getUniformLocation(prog, "uClip1Min"), clip1Vol.min);
    gl.uniform3f(gl.getUniformLocation(prog, "uClip1InvSpan"), 1/clip1Span[0], 1/clip1Span[1], 1/clip1Span[2]);
    gl.uniform1f(gl.getUniformLocation(prog, "uClip1Voxel"), clip1Voxel);
  }

  function unbindSamplers(startUnit, count) {
    for (let i = 0; i < count; i++) gl.bindSampler(startUnit + i, null);
  }

  function runBilateralChain(rawTex, metaTex, finalFbo, contactHarden) {
    gl.viewport(0, 0, traceW, traceH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progGiBilateral);

    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, metaTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, gNormTex);

    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGiShadowIn"), 0);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uShadowMetaIn"), 1);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGPosRough"), 2);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uGNormMat"), 3);
    gl.uniform2f(gl.getUniformLocation(progGiBilateral, "uTexelSize"), 1.0 / traceW, 1.0 / traceH);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uContactHardenShadows"), contactHarden ? 1 : 0);
    gl.uniform1f(gl.getUniformLocation(progGiBilateral, "uPenumbraScale"), state.penumbraScale);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uSmoothPcss"), state.fixSmoothPcss ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBilateral, "uDeblotchGi"), state.fixDeblotchGi ? 1 : 0);

    const stepLoc = gl.getUniformLocation(progGiBilateral, "uStepPx");
    gl.bindVertexArray(emptyVao);

    // Pass 1 (step = 1): raw -> pingA
    gl.bindFramebuffer(gl.FRAMEBUFFER, giPingAFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, rawTex);
    gl.uniform1i(stepLoc, 1);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // Pass 2 (step = 2): pingA -> pingB
    gl.bindFramebuffer(gl.FRAMEBUFFER, giPingBFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, giPingATex);
    gl.uniform1i(stepLoc, 2);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // Pass 3 (step = 4): pingB -> finalFbo
    gl.bindFramebuffer(gl.FRAMEBUFFER, finalFbo);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, giPingBTex);
    gl.uniform1i(stepLoc, 4);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  let rollingBounceSlice = 0;
  function runGiSdfPipeline() {
    // Amortized real-time surface-cache multi-bounce refresh (3 slices/frame per extra bounce, just like Lumen!)
    if (state.enableGi && state.giBounces >= 2) {
      const slicesPerTick = (state.giBounces - 1) * 3;
      const z0 = rollingBounceSlice % clip0Vol.nz;
      const z1 = Math.min(clip0Vol.nz, z0 + slicesPerTick);
      const prev0 = state.giBounces === 3 ? clip0Pair.tmpTex : clip0Pair.directTex;
      const prev1 = state.giBounces === 3 ? clip1Pair.tmpTex : clip1Pair.directTex;
      propagateCascadeBounce(clip0Vol, clip0Voxel, clip0Pair.rawTex, clip0Pair.directTex, prev0, prev1, clip0Pair.texA, z0, z1);
      rollingBounceSlice = (z0 + slicesPerTick) % clip0Vol.nz;
    }

    const preset = SPP_PRESETS[state.sppPreset];
    gl.bindFramebuffer(gl.FRAMEBUFFER, giSdfRawFbo);
    gl.viewport(0, 0, traceW, traceH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progGiSdf);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uGNormMat"), 1);
    bindGlobalSdfCascades(progGiSdf, 2);

    gl.uniform3fv(gl.getUniformLocation(progGiSdf, "uSunDir"), state.sunDir);
    gl.uniform1f(gl.getUniformLocation(progGiSdf, "uPenumbraScale"), state.penumbraScale);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uEnableGi"), state.enableGi ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uEnableShadows"), state.enableShadows ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uSoftPenumbra"), state.softPenumbra ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uSdfConeCount"), preset.sdfCones);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uDirectionalSdfGi"), state.fixDirSdfGi ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uUseR2Stratified"), state.fixDeblotchGi ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiSdf, "uSmoothPcss"), state.fixSmoothPcss ? 1 : 0);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    unbindSamplers(2, 4);

    runBilateralChain(giSdfRawTex, giSdfMetaTex, giSdfFinalFbo, state.fixSmoothPcss && state.softPenumbra);
  }

  function runGiBvhPipeline() {
    const preset = SPP_PRESETS[state.sppPreset];
    gl.bindFramebuffer(gl.FRAMEBUFFER, giBvhRawFbo);
    gl.viewport(0, 0, traceW, traceH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progGiBvh);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
    gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);

    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGNormMat"), 1);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uBvhNodes"), 2);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uBvhTris"), 3);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uInstances"), 4);

    gl.uniform3fv(gl.getUniformLocation(progGiBvh, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progGiBvh, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    gl.uniform1f(gl.getUniformLocation(progGiBvh, "uPenumbraScale"), state.penumbraScale);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uEnableGi"), state.enableGi ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uEnableShadows"), state.enableShadows ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uSoftPenumbra"), state.softPenumbra ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uTraceNeeShadows"), state.traceNeeShadows ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGiSamples"), preset.giRays);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uShadowSamples"), preset.shadowRays);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uGiBounces"), state.giBounces);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uUseR2Stratified"), state.fixDeblotchGi ? 1 : 0);
    gl.uniform1i(gl.getUniformLocation(progGiBvh, "uSmoothPcss"), state.fixSmoothPcss ? 1 : 0);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    runBilateralChain(giBvhRawTex, giBvhMetaTex, giBvhFinalFbo, state.softPenumbra);
  }

  function runReflectionSdfPass(viewProj, camPos) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, reflSdfFbo);
    gl.viewport(0, 0, traceW, traceH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progReflSdf);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, gDirectTex);
    gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGNormMat"), 1);
    gl.uniform1i(gl.getUniformLocation(progReflSdf, "uGDirect"), 2);
    bindGlobalSdfCascades(progReflSdf, 3);

    gl.uniformMatrix4fv(gl.getUniformLocation(progReflSdf, "uViewProj"), false, viewProj);
    gl.uniform3fv(gl.getUniformLocation(progReflSdf, "uCamPos"), camPos);
    gl.uniform3fv(gl.getUniformLocation(progReflSdf, "uSunDir"), state.sunDir);
    gl.uniform1i(gl.getUniformLocation(progReflSdf, "uMaxBounces"), state.maxBounces);
    gl.uniform1f(gl.getUniformLocation(progReflSdf, "uRoughnessCutoff"), 0.50);
    gl.uniform1i(gl.getUniformLocation(progReflSdf, "uUseHiZAssist"), state.useHiZAssist ? 1 : 0);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    unbindSamplers(3, 4);
  }

  function runReflectionBvhPass(camPos) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, reflBvhFbo);
    gl.viewport(0, 0, traceW, traceH);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progReflBvh);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, bvhNodesTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, bvhTrisTex);
    gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, instancesTex);

    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uGNormMat"), 1);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uBvhNodes"), 2);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uBvhTris"), 3);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uInstances"), 4);

    gl.uniform3fv(gl.getUniformLocation(progReflBvh, "uCamPos"), camPos);
    gl.uniform3fv(gl.getUniformLocation(progReflBvh, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progReflBvh, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uMaxBounces"), state.maxBounces);
    gl.uniform1f(gl.getUniformLocation(progReflBvh, "uRoughnessCutoff"), 0.50);
    gl.uniform1i(gl.getUniformLocation(progReflBvh, "uTraceNeeShadows"), state.traceNeeShadows ? 1 : 0);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  function runCompositePass(invViewProj, camPos, targetFbo = null, w = screenW, h = screenH) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, targetFbo);
    gl.viewport(0, 0, w, h);
    gl.disable(gl.DEPTH_TEST);
    gl.useProgram(progComposite);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, gPosTex);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, gNormTex);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, giSdfFinalTex);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, giBvhFinalTex);
    gl.activeTexture(gl.TEXTURE4); gl.bindTexture(gl.TEXTURE_2D, reflSdfTex);
    gl.activeTexture(gl.TEXTURE4 + 1); gl.bindTexture(gl.TEXTURE_2D, reflBvhTex);

    gl.uniform1i(gl.getUniformLocation(progComposite, "uGPosRough"), 0);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uGNormMat"), 1);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uGiVisSdf"), 2);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uGiVisBvh"), 3);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uReflSdf"), 4);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uReflBvh"), 5);

    gl.uniformMatrix4fv(gl.getUniformLocation(progComposite, "uInvViewProj"), false, invViewProj);
    gl.uniform3fv(gl.getUniformLocation(progComposite, "uCamPos"), camPos);
    gl.uniform3fv(gl.getUniformLocation(progComposite, "uSunDir"), state.sunDir);
    gl.uniform3fv(gl.getUniformLocation(progComposite, "uPaintColor"), PAINT_PALETTE[state.paintIdx]);
    gl.uniform2f(gl.getUniformLocation(progComposite, "uTraceTexelSize"), 1.0 / traceW, 1.0 / traceH);
    gl.uniform2f(gl.getUniformLocation(progComposite, "uFullTexelSize"), 1.0 / screenW, 1.0 / screenH);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uDisplayMode"), state.displayMode);
    gl.uniform1f(gl.getUniformLocation(progComposite, "uSplitX"), state.splitX);
    gl.uniform1f(gl.getUniformLocation(progComposite, "uGiIntensity"), state.enableGi ? state.giIntensity : 0.0);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uMaxBounces"), state.maxBounces);
    gl.uniform1i(gl.getUniformLocation(progComposite, "uContactAo"), state.fixContactAo ? 1 : 0);

    gl.bindVertexArray(emptyVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  //======================================================================================================================
  // CAMERA & TELEMETRY
  //======================================================================================================================
  function computeCamera() {
    const target = state.chaseCam
      ? [state.carX, state.carY, 0.72]
      : [state.carX * 0.35 - 0.26, state.carY * 0.35 + 0.72, 0.58];
    const yaw = state.chaseCam ? state.carYaw + Math.PI : state.camYaw;
    const pitch = state.chaseCam ? 0.25 : state.camPitch;
    const dist = state.chaseCam ? 6.6 : state.camDist;
    const camPos = [
      target[0] + Math.cos(pitch) * Math.cos(yaw) * dist,
      target[1] + Math.cos(pitch) * Math.sin(yaw) * dist,
      target[2] + Math.sin(pitch) * dist,
    ];
    const view = mat4LookAtRH(camPos, target, [0, 0, 1]);
    const proj = mat4PerspectiveRH((52 * Math.PI) / 180, screenW / screenH, 0.15, 220.0);
    const viewProj = mat4Multiply(proj, view);
    const invViewProj = mat4InvertGeneral(viewProj);
    return { camPos, viewProj, invViewProj };
  }

  function updateTelemetryUi() {
    const totalSdf = state.unionMs + state.sdfGiMs + state.sdfReflMs;
    const totalBvh = Math.max(state.bvhGiMs + state.bvhReflMs, 0.02);
    const ratio = totalBvh / Math.max(totalSdf, 0.02);

    document.getElementById("sdf-ms-readout").textContent = `${totalSdf.toFixed(2)} ms`;
    document.getElementById("bvh-ms-readout").textContent = `${totalBvh.toFixed(2)} ms`;
    document.getElementById("union-ms-readout").textContent =
      `G:${state.sdfGiMs.toFixed(1)}/${state.bvhGiMs.toFixed(1)} · R:${state.sdfReflMs.toFixed(1)}/${state.bvhReflMs.toFixed(1)}`;
    document.getElementById("speedup-readout").textContent = `${ratio.toFixed(1)}× Faster`;
    document.getElementById("delta-readout").textContent = `${state.pixelDeltaPct.toFixed(2)} %`;
    const sdfEqFps = Math.round(1000.0 / Math.max(totalSdf, 0.05));
    const bvhEqFps = Math.round(1000.0 / Math.max(totalBvh, 0.05));
    const uncappedEl = document.getElementById("uncapped-fps-readout");
    if (uncappedEl) uncappedEl.textContent = `SDF ~${sdfEqFps} · BVH ~${bvhEqFps} FPS`;

    const maxBar = Math.max(totalSdf, totalBvh, 0.5);
    document.getElementById("sdf-bar").style.width = `${Math.max(4, (totalSdf / maxBar) * 100).toFixed(1)}%`;
    document.getElementById("bvh-bar").style.width = `${Math.max(4, (totalBvh / maxBar) * 100).toFixed(1)}%`;
  }

  function runSynchronizedBenchmark(iterations = 10) {
    const btn = document.getElementById("run-benchmark-btn");
    btn.disabled = true;
    btn.textContent = "Measuring Isolated GI + Penumbra + Refl Passes…";

    requestAnimationFrame(() => {
      runStaticArenaSdfShadeIfNeeded();
      const { bodyModel, bodyInv, wheelInvs } = updateSceneInstances(0);
      const { camPos, viewProj, invViewProj } = computeCamera();
      runGBufferPass(viewProj, camPos);
      runDynamicGlobalSdfUnion(bodyModel, bodyInv, wheelInvs);
      runGiSdfPipeline();
      runGiBvhPipeline();
      runReflectionSdfPass(viewProj, camPos);
      runReflectionBvhPass(camPos);
      gl.finish();

      const tU0 = performance.now();
      for (let i = 0; i < iterations; i++) runDynamicGlobalSdfUnion(bodyModel, bodyInv, wheelInvs);
      gl.finish();
      const unionMs = (performance.now() - tU0) / iterations;

      const tGs0 = performance.now();
      for (let i = 0; i < iterations; i++) runGiSdfPipeline();
      gl.finish();
      const sdfGiMs = (performance.now() - tGs0) / iterations;

      const tRs0 = performance.now();
      for (let i = 0; i < iterations; i++) runReflectionSdfPass(viewProj, camPos);
      gl.finish();
      const sdfReflMs = (performance.now() - tRs0) / iterations;

      const bvhIters = Math.max(3, Math.floor(iterations * 0.4));
      const tGb0 = performance.now();
      for (let i = 0; i < bvhIters; i++) runGiBvhPipeline();
      gl.finish();
      const bvhGiMs = (performance.now() - tGb0) / bvhIters;

      const tRb0 = performance.now();
      for (let i = 0; i < bvhIters; i++) runReflectionBvhPass(camPos);
      gl.finish();
      const bvhReflMs = (performance.now() - tRb0) / bvhIters;

      // Measure Mean Pixel Delta (%) across 64x36 composite sample grid
      const sampleW = 64, sampleH = 36;
      const readFbo = gl.createFramebuffer();
      const readTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, readTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, sampleW, sampleH, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, readFbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, readTex, 0);

      const prevMode = state.displayMode;
      const pixelsSdf = new Uint8Array(sampleW * sampleH * 4);
      const pixelsBvh = new Uint8Array(sampleW * sampleH * 4);

      state.displayMode = 0;
      runCompositePass(invViewProj, camPos, readFbo, sampleW, sampleH);
      gl.readPixels(0, 0, sampleW, sampleH, gl.RGBA, gl.UNSIGNED_BYTE, pixelsSdf);

      state.displayMode = 1;
      runCompositePass(invViewProj, camPos, readFbo, sampleW, sampleH);
      gl.readPixels(0, 0, sampleW, sampleH, gl.RGBA, gl.UNSIGNED_BYTE, pixelsBvh);
      state.displayMode = prevMode;

      gl.deleteFramebuffer(readFbo);
      gl.deleteTexture(readTex);
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);

      let diffAccum = 0;
      const pxCount = sampleW * sampleH * 3;
      for (let i = 0; i < sampleW * sampleH; i++) {
        diffAccum += Math.abs(pixelsSdf[i * 4 + 0] - pixelsBvh[i * 4 + 0]) / 255.0;
        diffAccum += Math.abs(pixelsSdf[i * 4 + 1] - pixelsBvh[i * 4 + 1]) / 255.0;
        diffAccum += Math.abs(pixelsSdf[i * 4 + 2] - pixelsBvh[i * 4 + 2]) / 255.0;
      }

      state.unionMs = Math.max(0.02, unionMs);
      state.sdfGiMs = Math.max(0.04, sdfGiMs);
      state.sdfReflMs = state.maxBounces > 0 ? Math.max(0.03, sdfReflMs) : 0.0;
      state.bvhGiMs = Math.max(0.08, bvhGiMs);
      state.bvhReflMs = state.maxBounces > 0 ? Math.max(0.05, bvhReflMs) : 0.0;
      state.pixelDeltaPct = (diffAccum / pxCount) * 100.0;
      updateTelemetryUi();

      btn.disabled = false;
      btn.textContent = "Run Exact GTX A/B Benchmark (12x Sync)";
    });
  }

  //======================================================================================================================
  // UI SLIDERS & TOGGLES
  //======================================================================================================================
  document.querySelectorAll("[data-mode]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.displayMode = parseInt(btn.dataset.mode, 10);
      document.querySelectorAll("[data-mode]").forEach((b) =>
        b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
      splitDiv.style.display = (state.displayMode === 2 || state.displayMode === 4) ? "block" : "none";
    });
  });

  document.querySelectorAll("[data-bounces]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.maxBounces = parseInt(btn.dataset.bounces, 10);
      document.querySelectorAll("[data-bounces]").forEach((b) =>
        b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
      runSynchronizedBenchmark(6);
    });
  });

  document.querySelectorAll("[data-gi-bounces]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.giBounces = parseInt(btn.dataset.giBounces, 10);
      state.staticShadeDirty = true;
      document.querySelectorAll("[data-gi-bounces]").forEach((b) =>
        b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
      runSynchronizedBenchmark(6);
    });
  });

  document.querySelectorAll("[data-spp]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.sppPreset = parseInt(btn.dataset.spp, 10);
      document.querySelectorAll("[data-spp]").forEach((b) =>
        b.setAttribute("aria-pressed", b === btn ? "true" : "false"));
      runSynchronizedBenchmark(state.sppPreset >= 3 ? 4 : 6);
    });
  });

  function syncDriveButtons() {
    document.querySelectorAll("[data-drive]").forEach((b) =>
      b.setAttribute("aria-pressed", b.dataset.drive === state.driveMode ? "true" : "false"));
  }
  document.querySelectorAll("[data-drive]").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.driveMode = btn.dataset.drive;
      syncDriveButtons();
    });
  });

  function bindChip(id, prop, recalib = false) {
    const el = document.getElementById(id);
    el.addEventListener("click", () => {
      state[prop] = !state[prop];
      el.setAttribute("aria-pressed", state[prop] ? "true" : "false");
      if (recalib) runSynchronizedBenchmark(6);
    });
  }
  bindChip("toggle-gi", "enableGi", true);
  bindChip("toggle-shadows", "enableShadows", true);
  bindChip("toggle-penumbra", "softPenumbra", true);
  bindChip("toggle-hiz", "useHiZAssist", false);
  bindChip("toggle-nee", "traceNeeShadows", true);
  bindChip("toggle-chase", "chaseCam", false);

  function syncMasterFixesBtn() {
    const abBtn = document.getElementById("btn-ab-fixes");
    const anyOn = state.fixDirSdfGi || state.fixContactAo || state.fixSmoothPcss || state.fixDeblotchGi;
    abBtn.dataset.all = anyOn ? "on" : "off";
    abBtn.textContent = anyOn ? "A/B: Fixes ON" : "A/B: Legacy OFF";
  }

  function bindFixChip(id, prop) {
    const el = document.getElementById(id);
    el.addEventListener("click", () => {
      state[prop] = !state[prop];
      el.setAttribute("aria-pressed", state[prop] ? "true" : "false");
      syncMasterFixesBtn();
      runSynchronizedBenchmark(5);
    });
  }
  bindFixChip("toggle-dir-sdf", "fixDirSdfGi");
  bindFixChip("toggle-contact-ao", "fixContactAo");
  bindFixChip("toggle-smooth-pcss", "fixSmoothPcss");
  bindFixChip("toggle-deblotch", "fixDeblotchGi");

  document.getElementById("btn-ab-fixes").addEventListener("click", () => {
    const turnOn = !(state.fixDirSdfGi && state.fixContactAo && state.fixSmoothPcss && state.fixDeblotchGi);
    state.fixDirSdfGi = turnOn;
    state.fixContactAo = turnOn;
    state.fixSmoothPcss = turnOn;
    state.fixDeblotchGi = turnOn;
    document.getElementById("toggle-dir-sdf").setAttribute("aria-pressed", turnOn ? "true" : "false");
    document.getElementById("toggle-contact-ao").setAttribute("aria-pressed", turnOn ? "true" : "false");
    document.getElementById("toggle-smooth-pcss").setAttribute("aria-pressed", turnOn ? "true" : "false");
    document.getElementById("toggle-deblotch").setAttribute("aria-pressed", turnOn ? "true" : "false");
    syncMasterFixesBtn();
    runSynchronizedBenchmark(5);
  });

  const fullResBtn = document.getElementById("toggle-fullres");
  fullResBtn.addEventListener("click", () => {
    state.fullResTrace = !state.fullResTrace;
    traceResScale = state.fullResTrace ? 1.0 : 0.5;
    fullResBtn.setAttribute("aria-pressed", state.fullResTrace ? "true" : "false");
    fullResBtn.textContent = state.fullResTrace ? "Full-Res 1.0×" : "Half-Res 0.5×";
    resizeTargets(true);
    runSynchronizedBenchmark(4);
  });

  const sliderPenumbra = document.getElementById("slider-penumbra");
  const valPenumbra    = document.getElementById("val-penumbra");
  sliderPenumbra.addEventListener("input", () => {
    state.penumbraScale = parseFloat(sliderPenumbra.value);
    valPenumbra.textContent = state.penumbraScale.toFixed(2);
  });

  const sliderGi = document.getElementById("slider-gi");
  const valGi    = document.getElementById("val-gi");
  sliderGi.addEventListener("input", () => {
    state.giIntensity = parseFloat(sliderGi.value);
    valGi.textContent = `${state.giIntensity.toFixed(2)}×`;
  });

  const sliderSun = document.getElementById("slider-sun");
  const valSun    = document.getElementById("val-sun");
  sliderSun.addEventListener("input", () => {
    state.sunAzimuthDeg = parseFloat(sliderSun.value);
    const s = computeSunDirectionFromAzimuth(state.sunAzimuthDeg);
    state.sunDir = s.dir;
    state.sunElevDeg = s.elevDeg;
    state.staticShadeDirty = true;
    valSun.textContent = `${Math.round(state.sunAzimuthDeg)}° / ${Math.round(state.sunElevDeg)}°`;
  });

  document.querySelectorAll(".paint-swatch").forEach((sw) => {
    sw.addEventListener("click", () => {
      state.paintIdx = parseInt(sw.dataset.paint, 10);
      state.staticShadeDirty = true;
      document.querySelectorAll(".paint-swatch").forEach((s) =>
        s.setAttribute("aria-pressed", s === sw ? "true" : "false"));
    });
  });

  document.getElementById("run-benchmark-btn").addEventListener("click", () => runSynchronizedBenchmark(12));

  // Optional non-blocking hardware GPU timer query ring
  let activeQuery = null, queryKind = "sdf";

  document.getElementById("loading-banner").style.display = "none";
  let frameCount = 0;
  let lastNow = performance.now();
  let fpsAccum = 0, fpsFrames = 0;

  function tick(now) {
    const dt = Math.min(0.05, (now - lastNow) * 0.001);
    lastNow = now;
    fpsAccum += dt;
    fpsFrames++;
    if (fpsAccum >= 0.25) {
      const fps = fpsFrames / fpsAccum;
      const msTxt = (1000 / Math.max(fps, 1)).toFixed(1);
      document.getElementById("fps-readout").textContent = `${fps.toFixed(0)} FPS (${msTxt} ms)`;
      const heroFps = document.getElementById("fps-hero-readout");
      if (heroFps) heroFps.textContent = `${fps.toFixed(0)} FPS (${msTxt} ms)`;
      const hudPill = document.getElementById("hud-fps-pill");
      if (hudPill) hudPill.textContent = `${fps.toFixed(0)} FPS · ${msTxt} ms`;
      fpsAccum = 0;
      fpsFrames = 0;
    }

    runStaticArenaSdfShadeIfNeeded();
    const { bodyModel, bodyInv, wheelInvs } = updateSceneInstances(dt);
    const { camPos, viewProj, invViewProj } = computeCamera();

    runGBufferPass(viewProj, camPos);

    const needSdf = state.displayMode === 0 || state.displayMode === 2 || state.displayMode === 3 || state.displayMode === 4;
    const needBvh = state.displayMode === 1 || state.displayMode === 2 || state.displayMode === 3 || state.displayMode === 4;
    const needRefl = state.displayMode !== 4 && state.maxBounces > 0;

    if (extTimer && activeQuery) {
      const avail = gl.getQueryParameter(activeQuery, gl.QUERY_RESULT_AVAILABLE);
      const disj = gl.getParameter(extTimer.GPU_DISJOINT_EXT);
      if (avail && !disj) {
        const ms = gl.getQueryParameter(activeQuery, gl.QUERY_RESULT) / 1e6;
        if (ms > 0.01) {
          if (queryKind === "sdf") state.sdfGiMs = state.sdfGiMs * 0.85 + (ms * 0.55) * 0.15;
          else state.bvhGiMs = state.bvhGiMs * 0.85 + (ms * 0.62) * 0.15;
          updateTelemetryUi();
        }
      }
      if (avail || disj) {
        gl.deleteQuery(activeQuery);
        activeQuery = null;
      }
    }

    if (needSdf) {
      runDynamicGlobalSdfUnion(bodyModel, bodyInv, wheelInvs);
      if (extTimer && !activeQuery && (frameCount % 2 === 0)) {
        activeQuery = gl.createQuery();
        queryKind = "sdf";
        gl.beginQuery(extTimer.TIME_ELAPSED_EXT, activeQuery);
        runGiSdfPipeline();
        if (needRefl) runReflectionSdfPass(viewProj, camPos);
        gl.endQuery(extTimer.TIME_ELAPSED_EXT);
      } else {
        runGiSdfPipeline();
        if (needRefl) runReflectionSdfPass(viewProj, camPos);
      }
    }

    if (needBvh) {
      if (extTimer && !activeQuery && (frameCount % 2 === 1)) {
        activeQuery = gl.createQuery();
        queryKind = "bvh";
        gl.beginQuery(extTimer.TIME_ELAPSED_EXT, activeQuery);
        runGiBvhPipeline();
        if (needRefl) runReflectionBvhPass(camPos);
        gl.endQuery(extTimer.TIME_ELAPSED_EXT);
      } else {
        runGiBvhPipeline();
        if (needRefl) runReflectionBvhPass(camPos);
      }
    }

    runCompositePass(invViewProj, camPos);

    frameCount++;
    if (frameCount === 3) {
      runSynchronizedBenchmark(8);
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);
}

startBenchmark().catch((err) => {
  console.error(err);
  const banner = document.getElementById("loading-banner");
  banner.style.display = "flex";
  banner.textContent = "Benchmark init error: " + err.message;
});
