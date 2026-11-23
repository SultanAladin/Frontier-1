// Frontier Geometry Compiler — Hardware WebGPU Sparse 8x8x8 SDF 3D Raymarcher + Software Fallback + 2D Slice Renderer.
// Y-up camera basis matching ViewportRasterization so every hero stands upright and orbits identically.

import { extractSdfSliceRgba } from './SparseSdfBaker.js';

export const SDF_RAYMARCH_WGSL = /* wgsl */ `
struct SdfRayUniforms {
  camPosIso    : vec4<f32>, // xyz = eye, w = isoOffsetMeters
  camRightFov  : vec4<f32>, // xyz = right, w = tanHalfFov * aspect
  camUpTan     : vec4<f32>, // xyz = up, w = tanHalfFov
  camFwdTime   : vec4<f32>, // xyz = forward, w = timeSec
  boundsMinDim : vec4<f32>, // xyz = boundsMin, w = dim
  boundsMaxBpa : vec4<f32>, // xyz = boundsMax, w = bricksPerAxis
  voxelParams  : vec4<f32>, // x = voxelStep, y = narrowBandMeters, z = mip1Dim, w = forceMip1
  vizParams    : vec4<f32>, // x = vizMode (0=IsoAo, 1=Cutaway, 2=StepHeat, 3=SparseBricks), y = sliceAxis, z = sliceDepth, w = cutFlip
  tintOptical  : vec4<f32>, // rgb = baseTint, w = opticalFlag
};

@group(0) @binding(0) var<uniform> uSdf : SdfRayUniforms;
@group(0) @binding(1) var<storage, read> mip1Data : array<f32>;
@group(0) @binding(2) var<storage, read> brickIndir : array<i32>;
@group(0) @binding(3) var<storage, read> brickPool : array<u32>;

struct SdfRayOut {
  @builtin(position) clipPos : vec4<f32>,
  @location(0) ndc : vec2<f32>,
};

@vertex
fn vsSdfRay(@builtin(vertex_index) vid : u32) -> SdfRayOut {
  var verts = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>( 3.0, -1.0),
    vec2<f32>(-1.0,  3.0),
  );
  let pt = verts[vid];
  var vOut : SdfRayOut;
  vOut.clipPos = vec4<f32>(pt, 0.0, 1.0);
  vOut.ndc = pt;
  return vOut;
}

fn hitBox(ro : vec3<f32>, invRd : vec3<f32>, bMin : vec3<f32>, bMax : vec3<f32>) -> vec2<f32> {
  let t0 = (bMin - ro) * invRd;
  let t1 = (bMax - ro) * invRd;
  let tSm = min(t0, t1);
  let tLg = max(t0, t1);
  let tNear = max(max(tSm.x, tSm.y), tSm.z);
  let tFar = min(min(tLg.x, tLg.y), tLg.z);
  return vec2<f32>(tNear, tFar);
}

fn mip1Voxel(x : i32, y : i32, z : i32, mDim : i32) -> f32 {
  return mip1Data[u32((z * mDim + y) * mDim + x)];
}

fn sampleMip1(p : vec3<f32>) -> f32 {
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let cp = clamp(p, bMin, bMax);
  let ext = length(p - cp);
  let mDim = i32(uSdf.voxelParams.z + 0.5);
  let mMax = f32(mDim - 1);
  let step2 = uSdf.voxelParams.x * 2.0;
  let fc = clamp((cp - bMin) / step2 - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(mMax));
  let i0 = vec3<i32>(fc);
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(mDim - 1));
  let fr = fc - vec3<f32>(i0);

  let c00 = mix(mip1Voxel(i0.x, i0.y, i0.z, mDim), mip1Voxel(i1.x, i0.y, i0.z, mDim), fr.x);
  let c10 = mix(mip1Voxel(i0.x, i1.y, i0.z, mDim), mip1Voxel(i1.x, i1.y, i0.z, mDim), fr.x);
  let c01 = mix(mip1Voxel(i0.x, i0.y, i1.z, mDim), mip1Voxel(i1.x, i0.y, i1.z, mDim), fr.x);
  let c11 = mix(mip1Voxel(i0.x, i1.y, i1.z, mDim), mip1Voxel(i1.x, i1.y, i1.z, mDim), fr.x);
  let val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  return select(val, max(0.0, val) + ext, ext > 0.0);
}

fn fetchBrickWord(slot : i32, lx : i32, ly : i32, lz : i32) -> f32 {
  let li = u32((lz * 8 + ly) * 8 + lx);
  let wIdx = u32(slot) * 256u + (li >> 1u);
  let packed = brickPool[wIdx];
  let shift = (li & 1u) * 16u;
  let raw16 = i32(packed << (16u - shift)) >> 16u;
  return (f32(raw16) / 32767.0) * uSdf.voxelParams.y;
}

fn fetchMip0Corner(vx : i32, vy : i32, vz : i32, bpa : i32, mDim : i32) -> f32 {
  let bx = vx >> 3u;
  let by = vy >> 3u;
  let bz = vz >> 3u;
  let slot = brickIndir[u32((bz * bpa + by) * bpa + bx)];
  if (slot >= 0) {
    return fetchBrickWord(slot, vx & 7, vy & 7, vz & 7);
  }
  return mip1Voxel(min(vx >> 1u, mDim - 1), min(vy >> 1u, mDim - 1), min(vz >> 1u, mDim - 1), mDim);
}

fn sampleMip0(p : vec3<f32>) -> f32 {
  if (uSdf.voxelParams.w > 0.5) {
    return sampleMip1(p);
  }
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let cp = clamp(p, bMin, bMax);
  let ext = length(p - cp);
  let vDim = i32(uSdf.boundsMinDim.w + 0.5);
  let bpa = i32(uSdf.boundsMaxBpa.w + 0.5);
  let mDim = i32(uSdf.voxelParams.z + 0.5);
  let fc = clamp((cp - bMin) / uSdf.voxelParams.x - vec3<f32>(0.5), vec3<f32>(0.0), vec3<f32>(f32(vDim - 1)));
  let i0 = vec3<i32>(fc);
  let i1 = min(i0 + vec3<i32>(1), vec3<i32>(vDim - 1));
  let fr = fc - vec3<f32>(i0);

  let b0 = i0 >> vec3<u32>(3u);
  let b1 = i1 >> vec3<u32>(3u);
  var val = 0.0;
  if (all(b0 == b1)) {
    let slot = brickIndir[u32((b0.z * bpa + b0.y) * bpa + b0.x)];
    if (slot < 0) {
      return sampleMip1(p);
    }
    let l0 = i0 & vec3<i32>(7);
    let l1 = i1 & vec3<i32>(7);
    let c00 = mix(fetchBrickWord(slot, l0.x, l0.y, l0.z), fetchBrickWord(slot, l1.x, l0.y, l0.z), fr.x);
    let c10 = mix(fetchBrickWord(slot, l0.x, l1.y, l0.z), fetchBrickWord(slot, l1.x, l1.y, l0.z), fr.x);
    let c01 = mix(fetchBrickWord(slot, l0.x, l0.y, l1.z), fetchBrickWord(slot, l1.x, l0.y, l1.z), fr.x);
    let c11 = mix(fetchBrickWord(slot, l0.x, l1.y, l1.z), fetchBrickWord(slot, l1.x, l1.y, l1.z), fr.x);
    val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  } else {
    let c00 = mix(fetchMip0Corner(i0.x, i0.y, i0.z, bpa, mDim), fetchMip0Corner(i1.x, i0.y, i0.z, bpa, mDim), fr.x);
    let c10 = mix(fetchMip0Corner(i0.x, i1.y, i0.z, bpa, mDim), fetchMip0Corner(i1.x, i1.y, i0.z, bpa, mDim), fr.x);
    let c01 = mix(fetchMip0Corner(i0.x, i0.y, i1.z, bpa, mDim), fetchMip0Corner(i1.x, i0.y, i1.z, bpa, mDim), fr.x);
    let c11 = mix(fetchMip0Corner(i0.x, i1.y, i1.z, bpa, mDim), fetchMip0Corner(i1.x, i1.y, i1.z, bpa, mDim), fr.x);
    val = mix(mix(c00, c10, fr.y), mix(c01, c11, fr.y), fr.z);
  }
  return select(val, max(0.0, val) + ext, ext > 0.0);
}

fn sampleField(p : vec3<f32>) -> f32 {
  let isoOff = uSdf.camPosIso.w;
  let dCoarse = sampleMip1(p) - isoOff;
  if (uSdf.voxelParams.w < 0.5 && abs(dCoarse) < uSdf.voxelParams.y * 0.65) {
    return sampleMip0(p) - isoOff;
  }
  return dCoarse;
}

fn calcNormal(p : vec3<f32>) -> vec3<f32> {
  let h = uSdf.voxelParams.x * 0.75;
  let k0 = vec3<f32>( 1.0, -1.0, -1.0);
  let k1 = vec3<f32>(-1.0, -1.0,  1.0);
  let k2 = vec3<f32>(-1.0,  1.0, -1.0);
  let k3 = vec3<f32>( 1.0,  1.0,  1.0);
  let g = k0 * sampleField(p + k0 * h)
        + k1 * sampleField(p + k1 * h)
        + k2 * sampleField(p + k2 * h)
        + k3 * sampleField(p + k3 * h);
  let gl = length(g);
  return select(vec3<f32>(0.0, 1.0, 0.0), g / gl, gl > 1e-5);
}

fn calcAo(p : vec3<f32>, n : vec3<f32>) -> f32 {
  let vStep = uSdf.voxelParams.x;
  var occ = 0.0;
  var w = 1.0;
  for (var i = 1; i <= 4; i = i + 1) {
    let dist = vStep * (f32(i) * f32(i) * 0.75 + 0.5);
    let sd = sampleMip1(p + n * dist) - uSdf.camPosIso.w;
    occ = occ + w * max(0.0, dist - sd) / max(dist, 1e-4);
    w = w * 0.55;
  }
  return clamp(1.0 - occ * 0.42, 0.22, 1.0);
}

fn calcSoftShadow(ro : vec3<f32>, rd : vec3<f32>) -> f32 {
  let vStep = uSdf.voxelParams.x;
  let span = length(uSdf.boundsMaxBpa.xyz - uSdf.boundsMinDim.xyz);
  var t = vStep * 2.2;
  var sh = 1.0;
  for (var i = 0; i < 10; i = i + 1) {
    if (t > span * 0.75) { break; }
    let d = sampleMip1(ro + rd * t) - uSdf.camPosIso.w;
    if (d < vStep * 0.15) { return 0.25; }
    sh = min(sh, 6.0 * d / t);
    t = t + max(d, vStep * 0.8);
  }
  return clamp(sh, 0.25, 1.0);
}

fn isolinesColor(sd : f32, band : f32) -> vec3<f32> {
  let normD = clamp(sd / max(band, 1e-4), -1.0, 1.0);
  let rings = 0.5 + 0.5 * cos(sd * 56.0);
  let lineEdge = smoothstep(0.86, 0.98, rings);
  let zeroContour = 1.0 - smoothstep(0.0, band * 0.085, abs(sd));
  var col = vec3<f32>(0.0);
  if (sd <= 0.0) {
    col = mix(vec3<f32>(0.96, 0.52, 0.22), vec3<f32>(0.36, 0.09, 0.07), clamp(-normD, 0.0, 1.0));
    col = mix(col, vec3<f32>(1.0, 0.86, 0.58), lineEdge * 0.42);
  } else {
    col = mix(vec3<f32>(0.22, 0.78, 0.76), vec3<f32>(0.05, 0.12, 0.18), clamp(normD, 0.0, 1.0));
    col = mix(col, vec3<f32>(0.62, 0.96, 0.94), lineEdge * 0.38);
  }
  return mix(col, vec3<f32>(1.0, 0.98, 0.92), zeroContour * 0.92);
}

fn paletteHeat(x : f32) -> vec3<f32> {
  let t = clamp(x, 0.0, 1.0);
  let c0 = vec3<f32>(0.05, 0.07, 0.18);
  let c1 = vec3<f32>(0.10, 0.58, 0.64);
  let c2 = vec3<f32>(0.96, 0.68, 0.22);
  let c3 = vec3<f32>(0.95, 0.18, 0.34);
  if (t < 0.33) { return mix(c0, c1, t / 0.33); }
  if (t < 0.66) { return mix(c1, c2, (t - 0.33) / 0.33); }
  return mix(c2, c3, (t - 0.66) / 0.34);
}

fn shadeIsoSurface(p : vec3<f32>, n : vec3<f32>, viewDir : vec3<f32>) -> vec3<f32> {
  let keyDir = normalize(vec3<f32>(0.55, 0.78, 0.64));
  let fillDir = normalize(vec3<f32>(-0.72, 0.25, -0.35));
  let rimDir = normalize(vec3<f32>(-0.18, 0.55, -0.78));
  let halfKey = normalize(keyDir + viewDir);

  let ao = calcAo(p, n);
  let sh = calcSoftShadow(p + n * uSdf.voxelParams.x * 1.4, keyDir);
  let ndl = max(dot(n, keyDir), 0.0);
  let ndf = max(dot(n, fillDir), 0.0);
  let ndr = max(dot(n, rimDir), 0.0);
  let spec = pow(max(dot(n, halfKey), 0.0), 48.0);
  let ndv = clamp(dot(n, viewDir), 0.0, 1.0);
  let fresnel = pow(1.0 - ndv, 3.4);

  let baseCol = mix(uSdf.tintOptical.rgb, vec3<f32>(0.42, 0.84, 0.76), 0.24);
  return baseCol * (0.20 * ao + 0.72 * ndl * sh * ao + 0.22 * ndf * ao)
       + vec3<f32>(0.85, 0.94, 1.0) * (spec * 0.42 * sh)
       + vec3<f32>(0.34, 0.76, 0.78) * (ndr * 0.20 + fresnel * 0.28);
}

fn traceBricksOverlay(ro : vec3<f32>, rd : vec3<f32>, tStart : f32, tMax : f32) -> vec4<f32> {
  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let bpa = i32(uSdf.boundsMaxBpa.w + 0.5);
  let cellSize = (bMax - bMin) / f32(bpa);
  let safeRd = vec3<f32>(
    select(rd.x, 1e-6, abs(rd.x) < 1e-6),
    select(rd.y, 1e-6, abs(rd.y) < 1e-6),
    select(rd.z, 1e-6, abs(rd.z) < 1e-6),
  );
  let p0 = ro + rd * (tStart + 1e-4);
  let gPos = (p0 - bMin) / cellSize;
  var cell = clamp(vec3<i32>(floor(gPos)), vec3<i32>(0), vec3<i32>(bpa - 1));
  let stepDir = vec3<i32>(
    select(-1, 1, safeRd.x >= 0.0),
    select(-1, 1, safeRd.y >= 0.0),
    select(-1, 1, safeRd.z >= 0.0),
  );
  let tDelta = abs(cellSize / safeRd);
  let nextEdge = bMin + vec3<f32>(cell + max(stepDir, vec3<i32>(0))) * cellSize;
  var tNext = (nextEdge - ro) / safeRd;

  var accum = vec3<f32>(0.0);
  var alpha = 0.0;
  var tCur = tStart;
  for (var i = 0; i < 48; i = i + 1) {
    if (cell.x < 0 || cell.x >= bpa || cell.y < 0 || cell.y >= bpa || cell.z < 0 || cell.z >= bpa) { break; }
    if (tCur > tMax || alpha > 0.82) { break; }
    let tExitCell = min(min(tNext.x, tNext.y), tNext.z);
    let slot = brickIndir[u32((cell.z * bpa + cell.y) * bpa + cell.x)];
    if (slot >= 0) {
      let hitP = ro + rd * tCur;
      let loc = fract((hitP - bMin) / cellSize);
      let dx = min(loc.x, 1.0 - loc.x);
      let dy = min(loc.y, 1.0 - loc.y);
      let dz = min(loc.z, 1.0 - loc.z);
      let e2 = min(max(dx, dy), min(max(dy, dz), max(dx, dz)));
      let wire = 1.0 - smoothstep(0.025, 0.065, e2);
      let brickCol = mix(vec3<f32>(0.16, 0.72, 0.70), vec3<f32>(1.0, 0.74, 0.32), wire);
      let contribA = 0.022 + wire * 0.30;
      accum = accum + (1.0 - alpha) * contribA * brickCol;
      alpha = alpha + (1.0 - alpha) * contribA;
    }
    tCur = tExitCell;
    if (tNext.x <= tNext.y && tNext.x <= tNext.z) {
      tNext.x = tNext.x + tDelta.x;
      cell.x = cell.x + stepDir.x;
    } else if (tNext.y <= tNext.z) {
      tNext.y = tNext.y + tDelta.y;
      cell.y = cell.y + stepDir.y;
    } else {
      tNext.z = tNext.z + tDelta.z;
      cell.z = cell.z + stepDir.z;
    }
  }
  return vec4<f32>(accum, alpha);
}

@fragment
fn fsSdfRay(vIn : SdfRayOut) -> @location(0) vec4<f32> {
  let ro = uSdf.camPosIso.xyz;
  let rd = normalize(
    uSdf.camFwdTime.xyz
    + uSdf.camRightFov.xyz * (vIn.ndc.x * uSdf.camRightFov.w)
    + uSdf.camUpTan.xyz * (vIn.ndc.y * uSdf.camUpTan.w)
  );
  let invRd = vec3<f32>(
    1.0 / select(rd.x, 1e-6, abs(rd.x) < 1e-6),
    1.0 / select(rd.y, 1e-6, abs(rd.y) < 1e-6),
    1.0 / select(rd.z, 1e-6, abs(rd.z) < 1e-6),
  );

  let bgGrad = clamp(1.0 - length(vIn.ndc) * 0.55, 0.0, 1.0);
  var bg = mix(vec3<f32>(0.038, 0.042, 0.050), vec3<f32>(0.068, 0.076, 0.092), bgGrad);

  let bMin = uSdf.boundsMinDim.xyz;
  let bMax = uSdf.boundsMaxBpa.xyz;
  let boxT = hitBox(ro, invRd, bMin, bMax);
  if (boxT.y < max(boxT.x, 0.0)) {
    return vec4<f32>(bg, 1.0);
  }

  let tBoxIn = max(boxT.x, 0.0);
  let tBoxOut = boxT.y;
  let vizMode = i32(uSdf.vizParams.x + 0.5);
  let vStep = uSdf.voxelParams.x;
  let band = uSdf.voxelParams.y;

  let entryP = ro + rd * tBoxIn;
  let uvwIn = (entryP - bMin) / max(bMax - bMin, vec3<f32>(1e-4));
  let bEdgeX = min(uvwIn.x, 1.0 - uvwIn.x);
  let bEdgeY = min(uvwIn.y, 1.0 - uvwIn.y);
  let bEdgeZ = min(uvwIn.z, 1.0 - uvwIn.z);
  let cageMask = 1.0 - smoothstep(0.004, 0.011, max(min(bEdgeX, bEdgeY), min(max(bEdgeX, bEdgeY), bEdgeZ)));
  bg = mix(bg, vec3<f32>(0.34, 0.44, 0.52), cageMask * 0.35);

  var tStart = tBoxIn;
  var tEnd = tBoxOut;
  var planeHitAlpha = 0.0;
  var planeHitCol = vec3<f32>(0.0);
  var cutCapHit = false;
  var cutCapCol = vec3<f32>(0.0);

  if (vizMode == 1) {
    let ax = i32(uSdf.vizParams.y + 0.5); // 0=Z, 1=Y, 2=X
    let depth01 = clamp(uSdf.vizParams.z, 0.02, 0.98);
    let signDir = select(1.0, -1.0, uSdf.vizParams.w > 0.5);
    var pNorm = vec3<f32>(0.0, 0.0, signDir);
    var cutCoord = mix(bMin.z, bMax.z, depth01);
    var roAx = ro.z;
    var rdAx = rd.z;
    if (ax == 1) {
      pNorm = vec3<f32>(0.0, signDir, 0.0);
      cutCoord = mix(bMin.y, bMax.y, depth01);
      roAx = ro.y; rdAx = rd.y;
    } else if (ax == 2) {
      pNorm = vec3<f32>(signDir, 0.0, 0.0);
      cutCoord = mix(bMin.x, bMax.x, depth01);
      roAx = ro.x; rdAx = rd.x;
    }
    let roSigned = (roAx - cutCoord) * signDir;
    let rdSigned = rdAx * signDir;
    if (abs(rdSigned) > 1e-5) {
      let tPlane = -roSigned / rdSigned;
      if (roSigned > 0.0) {
        if (rdSigned >= 0.0 || tPlane > tBoxOut) {
          return vec4<f32>(bg, 1.0);
        }
        if (tPlane >= tBoxIn) {
          let pPl = ro + rd * tPlane;
          let dPl = sampleField(pPl);
          let isoCol = isolinesColor(dPl, band);
          if (dPl <= 0.0) {
            cutCapHit = true;
            let lit = 0.72 + 0.28 * abs(dot(pNorm, normalize(vec3<f32>(0.55, 0.78, 0.64))));
            cutCapCol = isoCol * lit;
          } else {
            let fade = clamp(1.0 - dPl / (band * 1.45), 0.0, 1.0);
            planeHitAlpha = 0.14 + 0.44 * fade * fade;
            planeHitCol = isoCol;
          }
          tStart = tPlane + vStep * 0.15;
        }
      } else if (rdSigned > 0.0 && tPlane > tBoxIn && tPlane < tBoxOut) {
        tEnd = tPlane;
        let pPl = ro + rd * tPlane;
        let dPl = sampleField(pPl);
        if (dPl > 0.0) {
          let fade = clamp(1.0 - dPl / (band * 1.45), 0.0, 1.0);
          planeHitAlpha = 0.12 + 0.36 * fade * fade;
          planeHitCol = isolinesColor(dPl, band);
        }
      }
    }
  }

  if (cutCapHit) {
    return vec4<f32>(clamp(cutCapCol, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
  }

  var t = tStart;
  var steps = 0;
  var hit = false;
  var prevD = 0.0;
  var stepLen = 0.0;
  let isoOff = uSdf.camPosIso.w;

  for (var i = 0; i < 128; i = i + 1) {
    if (t > tEnd) { break; }
    steps = i + 1;
    let p = ro + rd * t;
    let dCoarse = sampleMip1(p) - isoOff;
    var d = dCoarse;
    var inFine = false;
    if (uSdf.voxelParams.w < 0.5 && dCoarse < band * 0.85) {
      d = sampleMip0(p) - isoOff;
      inFine = true;
    }
    let hitEps = max(vStep * 0.12, t * 0.0014);
    if (d < hitEps) {
      if (d < 0.0 && prevD > 0.0) {
        let frac = prevD / max(prevD - d, 1e-5);
        t = (t - stepLen) + stepLen * clamp(frac, 0.0, 1.0);
      }
      hit = true;
      break;
    }
    let omega = select(0.96, 0.78, inFine);
    stepLen = max(d * omega, vStep * 0.14);
    prevD = d;
    t = t + stepLen;
  }

  var color = bg;
  if (vizMode == 2) {
    let heat = paletteHeat(f32(steps) / 56.0);
    if (hit) {
      let hitP = ro + rd * t;
      let n = calcNormal(hitP);
      let rim = 0.55 + 0.45 * max(dot(n, normalize(vec3<f32>(0.55, 0.78, 0.64))), 0.0);
      color = heat * rim;
    } else {
      color = mix(bg, heat, 0.55);
    }
    return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
  }

  if (hit) {
    let hitP = ro + rd * t;
    let n = calcNormal(hitP);
    color = shadeIsoSurface(hitP, n, -rd);
  }

  if (vizMode == 1 && planeHitAlpha > 0.0) {
    color = mix(color, planeHitCol, planeHitAlpha);
  }

  if (vizMode == 3) {
    let ddaMax = select(tEnd, t, hit);
    let brickVis = traceBricksOverlay(ro, rd, tStart, ddaMax);
    color = mix(color, brickVis.rgb / max(brickVis.a, 1e-4), brickVis.a * 0.85);
  }

  return vec4<f32>(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
`;

// Y-up camera basis matching ClusterLodStudio's ViewportRasterization
function computeCameraEyeYUp(camera) {
  const cp = Math.cos(camera.pitch);
  return [
    camera.target[0] + Math.sin(camera.yaw) * cp * camera.distance,
    camera.target[1] + Math.sin(camera.pitch) * camera.distance,
    camera.target[2] + Math.cos(camera.yaw) * cp * camera.distance,
  ];
}

function computeCameraBasisYUp(eye, target) {
  let fx = target[0] - eye[0], fy = target[1] - eye[1], fz = target[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl; fy /= fl; fz /= fl;

  // right = normalize(cross(fwd, up=[0,1,0])) = (-fz, 0, fx)
  let rx = -fz, ry = 0, rz = fx;
  const rl = Math.hypot(rx, ry, rz) || 1;
  rx /= rl; rz /= rl;

  // up = cross(right, fwd)
  const ux = ry * fz - rz * fy;
  const uy = rz * fx - rx * fz;
  const uz = rx * fy - ry * fx;
  return { fwd: [fx, fy, fz], right: [rx, ry, rz], up: [ux, uy, uz] };
}

// Reusable 2D SDF cross-section painter (shared by Inspector preview & SDF Workspace)
export function renderSdfSliceToCanvas(canvas, volume, { axis = 0, depth = 0.5, useMip1 = false, isoOffsetMeters = 0, showBrickGrid = true, resolution = 192 } = {}) {
  if (!canvas) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const side = Math.max(96, Math.round((canvas.clientWidth || 200) * dpr));
  if (canvas.width !== side || canvas.height !== side) {
    canvas.width = side;
    canvas.height = side;
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  ctx.fillStyle = '#0d0d0f';
  ctx.fillRect(0, 0, side, side);
  if (!volume) return;

  const rgba = extractSdfSliceRgba(volume, axis, depth, resolution, useMip1 || volume.mip0Evicted, isoOffsetMeters);
  if (!renderSdfSliceToCanvas.off || renderSdfSliceToCanvas.off.width !== resolution) {
    renderSdfSliceToCanvas.off = document.createElement('canvas');
    renderSdfSliceToCanvas.off.width = resolution;
    renderSdfSliceToCanvas.off.height = resolution;
  }
  const octx = renderSdfSliceToCanvas.off.getContext('2d');
  const img = octx.createImageData(resolution, resolution);
  img.data.set(rgba);
  octx.putImageData(img, 0, 0);

  ctx.imageSmoothingEnabled = true;
  ctx.drawImage(renderSdfSliceToCanvas.off, 0, 0, side, side);

  if (showBrickGrid) {
    const bpa = volume.bricksPerAxis;
    ctx.strokeStyle = 'rgba(255,255,255,0.11)';
    ctx.lineWidth = Math.max(1, dpr * 0.7);
    for (let i = 1; i < bpa; ++i) {
      const p = (i / bpa) * side;
      ctx.beginPath(); ctx.moveTo(p, 0); ctx.lineTo(p, side); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(0, p); ctx.lineTo(side, p); ctx.stroke();
    }
  }
  ctx.strokeStyle = 'rgba(255,178,56,0.35)';
  ctx.lineWidth = Math.max(1, dpr);
  ctx.strokeRect(0.5, 0.5, side - 1, side - 1);
}

export class SdfRaymarchViewport {
  constructor(gpuCanvas, overlayCanvas, onStatusChange = null) {
    this.gpuCanvas = gpuCanvas;
    this.overlayCanvas = overlayCanvas;
    this.overlayCtx = overlayCanvas.getContext('2d');
    this.onStatusChange = onStatusChange;

    this.webGpuAvailable = false;
    this.engineLabel = 'Software Sparse 8×8×8 Raymarch';
    this.device = null;
    this.gpuCtx = null;
    this.format = null;
    this.pipeline = null;
    this.bindLayout = null;
    this.uniformBuf = null;
    this.gpuVol = { mip1Buf: null, indirBuf: null, poolBuf: null, bindGroup: null };
    this._fallbackCanvas = null;
    this._sliceCanvas = null;
    this._initialized = false;
    this._gpuBusy = false;
    this._pendingFrame = false;
  }

  get isBusy() {
    return this._gpuBusy;
  }

  async initialize() {
    if (this._initialized) return;
    this._initialized = true;
    if (typeof navigator === 'undefined' || !navigator.gpu) return;
    try {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) return;
      const device = await adapter.requestDevice();
      const ctx = this.gpuCanvas.getContext('webgpu');
      if (!ctx) return;
      const format = navigator.gpu.getPreferredCanvasFormat();
      ctx.configure({ device, format, alphaMode: 'opaque' });

      let deviceFailed = false;
      const markFallback = () => {
        deviceFailed = true;
        this.webGpuAvailable = false;
        this._gpuBusy = false;
        this.engineLabel = 'Software Sparse 8×8×8 Raymarch';
        if (this.onStatusChange) this.onStatusChange();
      };
      device.addEventListener('uncapturederror', markFallback);
      device.lost.then(markFallback);

      device.pushErrorScope('validation');
      const module = device.createShaderModule({ code: SDF_RAYMARCH_WGSL });
      if (typeof module.getCompilationInfo === 'function') {
        const info = await module.getCompilationInfo();
        if (info.messages.some((m) => m.type === 'error')) {
          await device.popErrorScope();
          return;
        }
      }
      const bindLayout = device.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } },
          { binding: 1, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
          { binding: 2, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
          { binding: 3, visibility: GPUShaderStage.FRAGMENT, buffer: { type: 'read-only-storage' } },
        ],
      });
      const pipeline = await device.createRenderPipelineAsync({
        layout: device.createPipelineLayout({ bindGroupLayouts: [bindLayout] }),
        vertex: { module, entryPoint: 'vsSdfRay' },
        fragment: { module, entryPoint: 'fsSdfRay', targets: [{ format }] },
        primitive: { topology: 'triangle-list' },
      });
      const err = await device.popErrorScope();
      if (err || deviceFailed) return;

      this.device = device;
      this.gpuCtx = ctx;
      this.format = format;
      this.bindLayout = bindLayout;
      this.pipeline = pipeline;
      this.uniformBuf = device.createBuffer({ size: 160, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      this.webGpuAvailable = true;
      this.engineLabel = 'WebGPU Sparse 8×8×8 Raymarch';
    } catch {
      this.webGpuAvailable = false;
      this.engineLabel = 'Software Sparse 8×8×8 Raymarch';
    }
  }

  uploadVolume(volume) {
    if (!this.webGpuAvailable || !this.device || !this.bindLayout || !volume) return;
    if (this.gpuVol.mip1Buf) this.gpuVol.mip1Buf.destroy();
    if (this.gpuVol.indirBuf) this.gpuVol.indirBuf.destroy();
    if (this.gpuVol.poolBuf) this.gpuVol.poolBuf.destroy();

    const mip1Buf = this.device.createBuffer({
      size: Math.max(4, volume.conservativeMip1.byteLength),
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(mip1Buf, 0, volume.conservativeMip1);

    const indirBuf = this.device.createBuffer({
      size: Math.max(4, volume.brickIndirection.byteLength),
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(indirBuf, 0, volume.brickIndirection);

    // Pack pairs of Int16 brick voxels into u32 words (2x less GPU bandwidth)
    const pool = volume.brickPool || new Int16Array(0);
    const wordCount = Math.max(1, (pool.length + 1) >> 1);
    const packedPool = new Uint32Array(wordCount);
    for (let i = 0; i < (pool.length >> 1); ++i) {
      packedPool[i] = ((pool[i * 2 + 1] & 0xffff) << 16) | (pool[i * 2] & 0xffff);
    }
    const poolBuf = this.device.createBuffer({
      size: packedPool.byteLength,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
    });
    this.device.queue.writeBuffer(poolBuf, 0, packedPool);

    const bindGroup = this.device.createBindGroup({
      layout: this.bindLayout,
      entries: [
        { binding: 0, resource: { buffer: this.uniformBuf } },
        { binding: 1, resource: { buffer: mip1Buf } },
        { binding: 2, resource: { buffer: indirBuf } },
        { binding: 3, resource: { buffer: poolBuf } },
      ],
    });
    this.gpuVol = { mip1Buf, indirBuf, poolBuf, bindGroup };
  }

  render(state, volume) {
    const host = this.gpuCanvas.parentElement;
    if (!host) return false;
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(200, Math.round(host.clientWidth * dpr));
    const h = Math.max(180, Math.round(host.clientHeight * dpr));
    if (this.gpuCanvas.width !== w || this.gpuCanvas.height !== h) {
      this.gpuCanvas.width = w;
      this.gpuCanvas.height = h;
      this.overlayCanvas.width = w;
      this.overlayCanvas.height = h;
      if (this.webGpuAvailable && this.gpuCtx && this.device && this.format) {
        this.gpuCtx.configure({ device: this.device, format: this.format, alphaMode: 'opaque' });
      }
    }

    const ctx = this.overlayCtx;
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, w, h);

    if (!volume) {
      this.gpuCanvas.style.opacity = '0';
      ctx.fillStyle = '#0b0b0d';
      ctx.fillRect(0, 0, w, h);
      return true;
    }

    if (state.previewMode === 'slice2d') {
      this.gpuCanvas.style.opacity = '0';
      this._renderSliceFull(ctx, w, h, state, volume, dpr);
      return true;
    }

    const eye = computeCameraEyeYUp(state.camera);
    if (this.webGpuAvailable && this.device && this.pipeline && this.gpuVol.bindGroup) {
      if (this._gpuBusy) return false;
      this.gpuCanvas.style.opacity = '1';
      this._renderWebGpu(w, h, state, volume, eye);
      return true;
    }
    this.gpuCanvas.style.opacity = '0';
    this._renderSoftware(ctx, w, h, state, volume, eye);
    return true;
  }

  _renderSliceFull(ctx, w, h, state, volume, dpr) {
    ctx.fillStyle = '#090a0d';
    ctx.fillRect(0, 0, w, h);
    const res = 256;
    const rgba = extractSdfSliceRgba(
      volume,
      state.sliceAxis,
      state.sliceDepth,
      res,
      state.sliceLevel === 1 || volume.mip0Evicted,
      state.isoOffsetMm * 0.001,
    );
    if (!this._sliceCanvas || this._sliceCanvas.width !== res) {
      this._sliceCanvas = document.createElement('canvas');
      this._sliceCanvas.width = res;
      this._sliceCanvas.height = res;
    }
    const octx = this._sliceCanvas.getContext('2d');
    const img = octx.createImageData(res, res);
    img.data.set(rgba);
    octx.putImageData(img, 0, 0);

    const side = Math.floor(Math.min(w, h) * 0.78);
    const ox = Math.floor((w - side) * 0.5);
    const oy = Math.floor((h - side) * 0.48);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(this._sliceCanvas, ox, oy, side, side);

    const bpa = volume.bricksPerAxis;
    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.lineWidth = Math.max(1, dpr * 0.75);
    for (let i = 0; i <= bpa; ++i) {
      const px = ox + (i / bpa) * side;
      ctx.beginPath(); ctx.moveTo(px, oy); ctx.lineTo(px, oy + side); ctx.stroke();
      const py = oy + (i / bpa) * side;
      ctx.beginPath(); ctx.moveTo(ox, py); ctx.lineTo(ox + side, py); ctx.stroke();
    }
    ctx.strokeStyle = 'rgba(255,178,56,0.6)';
    ctx.lineWidth = Math.max(1.5, dpr);
    ctx.strokeRect(ox, oy, side, side);
  }

  _renderWebGpu(w, h, state, volume, eye) {
    const aspect = w / Math.max(1, h);
    const tanHalfFov = Math.tan(0.68 * 0.5);
    const basis = computeCameraBasisYUp(eye, state.camera.target);
    const forceMip1 = volume.mip0Evicted || state.sliceLevel === 1 ? 1.0 : 0.0;

    const u = new Float32Array(36);
    u[0] = eye[0]; u[1] = eye[1]; u[2] = eye[2]; u[3] = state.isoOffsetMm * 0.001;
    u[4] = basis.right[0]; u[5] = basis.right[1]; u[6] = basis.right[2]; u[7] = tanHalfFov * aspect;
    u[8] = basis.up[0]; u[9] = basis.up[1]; u[10] = basis.up[2]; u[11] = tanHalfFov;
    u[12] = basis.fwd[0]; u[13] = basis.fwd[1]; u[14] = basis.fwd[2]; u[15] = performance.now() * 0.001;
    u[16] = volume.boundsMin[0]; u[17] = volume.boundsMin[1]; u[18] = volume.boundsMin[2]; u[19] = volume.dim;
    u[20] = volume.boundsMax[0]; u[21] = volume.boundsMax[1]; u[22] = volume.boundsMax[2]; u[23] = volume.bricksPerAxis;
    u[24] = volume.voxelStep; u[25] = volume.narrowBandMeters; u[26] = volume.mip1Dim; u[27] = forceMip1;
    u[28] = state.vizMode; u[29] = state.sliceAxis; u[30] = state.sliceDepth; u[31] = state.cutFlip ? 1.0 : 0.0;
    u[32] = 0.72; u[33] = 0.78; u[34] = 0.86; u[35] = 0.0;

    this.device.queue.writeBuffer(this.uniformBuf, 0, u);
    const tex = this.gpuCtx.getCurrentTexture();
    const enc = this.device.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: [{
        view: tex.createView(),
        clearValue: { r: 0.04, g: 0.044, b: 0.052, a: 1 },
        loadOp: 'clear',
        storeOp: 'store',
      }],
    });
    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, this.gpuVol.bindGroup);
    pass.draw(3);
    pass.end();
    this._gpuBusy = true;
    this.device.queue.submit([enc.finish()]);
    this.device.queue.onSubmittedWorkDone().then(
      () => { this._gpuBusy = false; },
      () => { this._gpuBusy = false; },
    );
  }

  _renderSoftware(ctx, w, h, state, volume, eye) {
    const aspect = w / Math.max(1, h);
    const rw = Math.min(320, Math.max(128, Math.round(w * 0.42)));
    const rh = Math.max(96, Math.round(rw / aspect));
    if (!this._fallbackCanvas || this._fallbackCanvas.width !== rw || this._fallbackCanvas.height !== rh) {
      this._fallbackCanvas = document.createElement('canvas');
      this._fallbackCanvas.width = rw;
      this._fallbackCanvas.height = rh;
    }
    const octx = this._fallbackCanvas.getContext('2d');
    const img = octx.createImageData(rw, rh);
    const out = img.data;

    const { dim, mip1Dim, bricksPerAxis, voxelStep, narrowBandMeters, boundsMin, boundsMax, conservativeMip1, brickIndirection, brickPool } = volume;
    const bMinX = boundsMin[0], bMinY = boundsMin[1], bMinZ = boundsMin[2];
    const bMaxX = boundsMax[0], bMaxY = boundsMax[1], bMaxZ = boundsMax[2];
    const invStep = 1 / voxelStep;
    const invStep2 = 0.5 * invStep;
    const bandScale = narrowBandMeters / 32767;
    const forceMip1 = volume.mip0Evicted || state.sliceLevel === 1 || brickPool.length === 0;
    const isoOff = state.isoOffsetMm * 0.001;
    const vizMode = state.vizMode;
    const sliceAxis = state.sliceAxis;
    const sliceDepth = Math.min(0.98, Math.max(0.02, state.sliceDepth));
    const cutSign = state.cutFlip ? -1 : 1;
    const cutCoord = sliceAxis === 0
      ? bMinZ + (bMaxZ - bMinZ) * sliceDepth
      : sliceAxis === 1
        ? bMinY + (bMaxY - bMinY) * sliceDepth
        : bMinX + (bMaxX - bMinX) * sliceDepth;

    const sampleMip1 = (px, py, pz) => {
      const cx = px < bMinX ? bMinX : px > bMaxX ? bMaxX : px;
      const cy = py < bMinY ? bMinY : py > bMaxY ? bMaxY : py;
      const cz = pz < bMinZ ? bMinZ : pz > bMaxZ ? bMaxZ : pz;
      const fx = Math.max(0, Math.min(mip1Dim - 1, (cx - bMinX) * invStep2 - 0.5));
      const fy = Math.max(0, Math.min(mip1Dim - 1, (cy - bMinY) * invStep2 - 0.5));
      const fz = Math.max(0, Math.min(mip1Dim - 1, (cz - bMinZ) * invStep2 - 0.5));
      const x0 = fx | 0, y0 = fy | 0, z0 = fz | 0;
      const x1 = x0 + 1 < mip1Dim ? x0 + 1 : mip1Dim - 1;
      const y1 = y0 + 1 < mip1Dim ? y0 + 1 : mip1Dim - 1;
      const z1 = z0 + 1 < mip1Dim ? z0 + 1 : mip1Dim - 1;
      const tx = fx - x0, ty = fy - y0, tz = fz - z0;
      const row0 = z0 * mip1Dim, row1 = z1 * mip1Dim;
      const c00 = conservativeMip1[(row0 + y0) * mip1Dim + x0] * (1 - tx) + conservativeMip1[(row0 + y0) * mip1Dim + x1] * tx;
      const c10 = conservativeMip1[(row0 + y1) * mip1Dim + x0] * (1 - tx) + conservativeMip1[(row0 + y1) * mip1Dim + x1] * tx;
      const c01 = conservativeMip1[(row1 + y0) * mip1Dim + x0] * (1 - tx) + conservativeMip1[(row1 + y0) * mip1Dim + x1] * tx;
      const c11 = conservativeMip1[(row1 + y1) * mip1Dim + x0] * (1 - tx) + conservativeMip1[(row1 + y1) * mip1Dim + x1] * tx;
      const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
      const dx = px - cx, dy = py - cy, dz = pz - cz;
      return (dx || dy || dz) ? Math.max(0, val) + Math.hypot(dx, dy, dz) : val;
    };

    const atMip0 = (vx, vy, vz) => {
      const slot = brickIndirection[((vz >> 3) * bricksPerAxis + (vy >> 3)) * bricksPerAxis + (vx >> 3)];
      if (slot >= 0) return brickPool[slot * 512 + ((vz & 7) * 8 + (vy & 7)) * 8 + (vx & 7)] * bandScale;
      const mx = Math.min(vx >> 1, mip1Dim - 1), my = Math.min(vy >> 1, mip1Dim - 1), mz = Math.min(vz >> 1, mip1Dim - 1);
      return conservativeMip1[(mz * mip1Dim + my) * mip1Dim + mx];
    };

    const sampleMip0 = (px, py, pz) => {
      const cx = px < bMinX ? bMinX : px > bMaxX ? bMaxX : px;
      const cy = py < bMinY ? bMinY : py > bMaxY ? bMaxY : py;
      const cz = pz < bMinZ ? bMinZ : pz > bMaxZ ? bMaxZ : pz;
      const fx = Math.max(0, Math.min(dim - 1, (cx - bMinX) * invStep - 0.5));
      const fy = Math.max(0, Math.min(dim - 1, (cy - bMinY) * invStep - 0.5));
      const fz = Math.max(0, Math.min(dim - 1, (cz - bMinZ) * invStep - 0.5));
      const x0 = fx | 0, y0 = fy | 0, z0 = fz | 0;
      const x1 = x0 + 1 < dim ? x0 + 1 : dim - 1;
      const y1 = y0 + 1 < dim ? y0 + 1 : dim - 1;
      const z1 = z0 + 1 < dim ? z0 + 1 : dim - 1;
      const tx = fx - x0, ty = fy - y0, tz = fz - z0;
      const c00 = atMip0(x0, y0, z0) * (1 - tx) + atMip0(x1, y0, z0) * tx;
      const c10 = atMip0(x0, y1, z0) * (1 - tx) + atMip0(x1, y1, z0) * tx;
      const c01 = atMip0(x0, y0, z1) * (1 - tx) + atMip0(x1, y0, z1) * tx;
      const c11 = atMip0(x0, y1, z1) * (1 - tx) + atMip0(x1, y1, z1) * tx;
      const val = (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
      const dx = px - cx, dy = py - cy, dz = pz - cz;
      return (dx || dy || dz) ? Math.max(0, val) + Math.hypot(dx, dy, dz) : val;
    };

    const sampleField = (px, py, pz) => {
      const dc = sampleMip1(px, py, pz) - isoOff;
      if (!forceMip1 && Math.abs(dc) < narrowBandMeters * 0.65) return sampleMip0(px, py, pz) - isoOff;
      return dc;
    };

    const isoColor = (sd) => {
      const nd = Math.max(-1, Math.min(1, sd / Math.max(narrowBandMeters, 1e-4)));
      const ring = 0.5 + 0.5 * Math.cos(sd * 56.0);
      const line = ring > 0.9 ? (ring - 0.9) * 10 : 0;
      const zero = Math.max(0, 1 - Math.abs(sd) / (narrowBandMeters * 0.09));
      let r, g, b;
      if (sd <= 0) {
        const k = Math.min(1, -nd);
        r = 0.96 * (1 - k) + 0.36 * k + line * 0.12;
        g = 0.52 * (1 - k) + 0.09 * k + line * 0.18;
        b = 0.22 * (1 - k) + 0.07 * k + line * 0.16;
      } else {
        const k = Math.min(1, nd);
        r = 0.22 * (1 - k) + 0.05 * k + line * 0.18;
        g = 0.78 * (1 - k) + 0.12 * k + line * 0.22;
        b = 0.76 * (1 - k) + 0.18 * k + line * 0.20;
      }
      return [
        r * (1 - zero * 0.9) + zero * 0.9,
        g * (1 - zero * 0.9) + zero * 0.88,
        b * (1 - zero * 0.9) + zero * 0.82,
      ];
    };

    const heatCol = (x) => {
      const t = Math.max(0, Math.min(1, x));
      if (t < 0.33) { const u = t / 0.33; return [0.05 + 0.05 * u, 0.07 + 0.51 * u, 0.18 + 0.46 * u]; }
      if (t < 0.66) { const u = (t - 0.33) / 0.33; return [0.10 + 0.86 * u, 0.58 + 0.10 * u, 0.64 - 0.42 * u]; }
      const u = (t - 0.66) / 0.34;
      return [0.96 - 0.01 * u, 0.68 - 0.50 * u, 0.22 + 0.12 * u];
    };

    const tanHalfFov = Math.tan(0.68 * 0.5);
    const basis = computeCameraBasisYUp(eye, state.camera.target);
    const roX = eye[0], roY = eye[1], roZ = eye[2];
    const kl = Math.hypot(0.55, 0.78, 0.64);
    const keyX = 0.55 / kl, keyY = 0.78 / kl, keyZ = 0.64 / kl;
    const fl = Math.hypot(-0.72, 0.25, -0.35);
    const fillX = -0.72 / fl, fillY = 0.25 / fl, fillZ = -0.35 / fl;
    const cellX = (bMaxX - bMinX) / bricksPerAxis;
    const cellY = (bMaxY - bMinY) / bricksPerAxis;
    const cellZ = (bMaxZ - bMinZ) / bricksPerAxis;

    for (let py = 0; py < rh; ++py) {
      const ndcY = 1 - 2 * ((py + 0.5) / rh);
      for (let px = 0; px < rw; ++px) {
        const ndcX = 2 * ((px + 0.5) / rw) - 1;
        let rdX = basis.fwd[0] + basis.right[0] * (ndcX * tanHalfFov * aspect) + basis.up[0] * (ndcY * tanHalfFov);
        let rdY = basis.fwd[1] + basis.right[1] * (ndcX * tanHalfFov * aspect) + basis.up[1] * (ndcY * tanHalfFov);
        let rdZ = basis.fwd[2] + basis.right[2] * (ndcX * tanHalfFov * aspect) + basis.up[2] * (ndcY * tanHalfFov);
        const rLen = Math.hypot(rdX, rdY, rdZ) || 1;
        rdX /= rLen; rdY /= rLen; rdZ /= rLen;

        const bgG = Math.max(0, 1 - Math.hypot(ndcX, ndcY) * 0.55);
        let rCol = 0.038 + 0.030 * bgG;
        let gCol = 0.042 + 0.034 * bgG;
        let bCol = 0.050 + 0.042 * bgG;

        const invX = 1 / (Math.abs(rdX) < 1e-6 ? 1e-6 : rdX);
        const invY = 1 / (Math.abs(rdY) < 1e-6 ? 1e-6 : rdY);
        const invZ = 1 / (Math.abs(rdZ) < 1e-6 ? 1e-6 : rdZ);
        const tx0 = (bMinX - roX) * invX, tx1 = (bMaxX - roX) * invX;
        const ty0 = (bMinY - roY) * invY, ty1 = (bMaxY - roY) * invY;
        const tz0 = (bMinZ - roZ) * invZ, tz1 = (bMaxZ - roZ) * invZ;
        const tNear = Math.max(Math.min(tx0, tx1), Math.min(ty0, ty1), Math.min(tz0, tz1));
        const tFar = Math.min(Math.max(tx0, tx1), Math.max(ty0, ty1), Math.max(tz0, tz1));

        const idx = (py * rw + px) * 4;
        if (tFar < Math.max(tNear, 0)) {
          out[idx] = rCol * 255; out[idx + 1] = gCol * 255; out[idx + 2] = bCol * 255; out[idx + 3] = 255;
          continue;
        }

        let tStart = Math.max(tNear, 0), tEnd = tFar;
        let planeA = 0, pCol = null, cutCap = false;

        if (vizMode === 1) {
          const roAx = sliceAxis === 0 ? roZ : sliceAxis === 1 ? roY : roX;
          const rdAx = sliceAxis === 0 ? rdZ : sliceAxis === 1 ? rdY : rdX;
          const roS = (roAx - cutCoord) * cutSign;
          const rdS = rdAx * cutSign;
          if (Math.abs(rdS) > 1e-5) {
            const tPl = -roS / rdS;
            if (roS > 0) {
              if (rdS >= 0 || tPl > tFar) {
                out[idx] = rCol * 255; out[idx + 1] = gCol * 255; out[idx + 2] = bCol * 255; out[idx + 3] = 255;
                continue;
              }
              if (tPl >= tStart) {
                const dPl = sampleField(roX + rdX * tPl, roY + rdY * tPl, roZ + rdZ * tPl);
                const ic = isoColor(dPl);
                if (dPl <= 0) {
                  cutCap = true;
                  rCol = ic[0] * 0.92; gCol = ic[1] * 0.92; bCol = ic[2] * 0.92;
                } else {
                  const fade = Math.max(0, Math.min(1, 1 - dPl / (narrowBandMeters * 1.45)));
                  planeA = 0.14 + 0.44 * fade * fade;
                  pCol = ic;
                }
                tStart = tPl + voxelStep * 0.15;
              }
            } else if (rdS > 0 && tPl > tStart && tPl < tEnd) {
              tEnd = tPl;
              const dPl = sampleField(roX + rdX * tPl, roY + rdY * tPl, roZ + rdZ * tPl);
              if (dPl > 0) {
                const fade = Math.max(0, Math.min(1, 1 - dPl / (narrowBandMeters * 1.45)));
                planeA = 0.12 + 0.36 * fade * fade;
                pCol = isoColor(dPl);
              }
            }
          }
        }

        if (!cutCap) {
          let t = tStart, steps = 0, hit = false, prevD = 0, stepLen = 0;
          for (let i = 0; i < 112 && t <= tEnd; ++i) {
            steps = i + 1;
            const pxW = roX + rdX * t, pyW = roY + rdY * t, pzW = roZ + rdZ * t;
            const dCoarse = sampleMip1(pxW, pyW, pzW) - isoOff;
            let d = dCoarse;
            let inFine = false;
            if (!forceMip1 && dCoarse < narrowBandMeters * 0.85) {
              d = sampleMip0(pxW, pyW, pzW) - isoOff;
              inFine = true;
            }
            const eps = Math.max(voxelStep * 0.14, t * 0.0018);
            if (d < eps) {
              if (d < 0 && prevD > 0) {
                const f = Math.max(0, Math.min(1, prevD / Math.max(prevD - d, 1e-5)));
                t = (t - stepLen) + stepLen * f;
              }
              hit = true;
              break;
            }
            stepLen = Math.max(d * (inFine ? 0.78 : 0.96), voxelStep * 0.14);
            prevD = d;
            t += stepLen;
          }

          if (vizMode === 2) {
            const hc = heatCol(steps / 52);
            const mixK = hit ? 0.95 : 0.55;
            rCol = rCol * (1 - mixK) + hc[0] * mixK;
            gCol = gCol * (1 - mixK) + hc[1] * mixK;
            bCol = bCol * (1 - mixK) + hc[2] * mixK;
          } else {
            if (hit) {
              const hx = roX + rdX * t, hy = roY + rdY * t, hz = roZ + rdZ * t;
              const hEps = voxelStep * 0.78;
              const d0 = sampleField(hx + hEps, hy - hEps, hz - hEps);
              const d1 = sampleField(hx - hEps, hy - hEps, hz + hEps);
              const d2 = sampleField(hx - hEps, hy + hEps, hz - hEps);
              const d3 = sampleField(hx + hEps, hy + hEps, hz + hEps);
              let nx = d0 - d1 - d2 + d3;
              let ny = -d0 - d1 + d2 + d3;
              let nz = -d0 + d1 - d2 + d3;
              const nl = Math.hypot(nx, ny, nz) || 1;
              nx /= nl; ny /= nl; nz /= nl;

              let occ = 0;
              for (let a = 1; a <= 3; ++a) {
                const dist = voxelStep * (a * a * 0.85 + 0.6);
                const sd = sampleMip1(hx + nx * dist, hy + ny * dist, hz + nz * dist) - isoOff;
                occ += Math.max(0, dist - sd) / Math.max(dist, 1e-4) * (a === 1 ? 1 : a === 2 ? 0.55 : 0.3);
              }
              const ao = Math.max(0.25, Math.min(1, 1 - occ * 0.42));
              const ndl = Math.max(0, nx * keyX + ny * keyY + nz * keyZ);
              const ndf = Math.max(0, nx * fillX + ny * fillY + nz * fillZ);
              const ndv = Math.max(0, Math.min(1, -(nx * rdX + ny * rdY + nz * rdZ)));
              const fres = Math.pow(1 - ndv, 3.2);
              const lit = (0.20 + 0.72 * ndl + 0.22 * ndf) * ao;
              rCol = 0.65 * lit + 0.34 * fres * 0.35;
              gCol = 0.80 * lit + 0.76 * fres * 0.35;
              bCol = 0.84 * lit + 0.78 * fres * 0.35;
            }
            if (vizMode === 1 && planeA > 0 && pCol) {
              rCol = rCol * (1 - planeA) + pCol[0] * planeA;
              gCol = gCol * (1 - planeA) + pCol[1] * planeA;
              bCol = bCol * (1 - planeA) + pCol[2] * planeA;
            }
            if (vizMode === 3) {
              const ddaMax = hit ? t : tEnd;
              const p0x = roX + rdX * (tStart + 1e-4);
              const p0y = roY + rdY * (tStart + 1e-4);
              const p0z = roZ + rdZ * (tStart + 1e-4);
              let cx = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0x - bMinX) / cellX)));
              let cy = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0y - bMinY) / cellY)));
              let cz = Math.max(0, Math.min(bricksPerAxis - 1, Math.floor((p0z - bMinZ) / cellZ)));
              const sx = rdX >= 0 ? 1 : -1, sy = rdY >= 0 ? 1 : -1, sz = rdZ >= 0 ? 1 : -1;
              const tdx = Math.abs(cellX * invX), tdy = Math.abs(cellY * invY), tdz = Math.abs(cellZ * invZ);
              let tnx = ((bMinX + (cx + (sx > 0 ? 1 : 0)) * cellX) - roX) * invX;
              let tny = ((bMinY + (cy + (sy > 0 ? 1 : 0)) * cellY) - roY) * invY;
              let tnz = ((bMinZ + (cz + (sz > 0 ? 1 : 0)) * cellZ) - roZ) * invZ;
              let tCur = tStart, accR = 0, accG = 0, accB = 0, accA = 0;
              for (let k = 0; k < 44 && tCur <= ddaMax && accA < 0.8; ++k) {
                if (cx < 0 || cx >= bricksPerAxis || cy < 0 || cy >= bricksPerAxis || cz < 0 || cz >= bricksPerAxis) break;
                const slot = brickIndirection[(cz * bricksPerAxis + cy) * bricksPerAxis + cx];
                const tNextCell = Math.min(tnx, tny, tnz);
                if (slot >= 0) {
                  const hpx = (roX + rdX * tCur - bMinX) / cellX;
                  const hpy = (roY + rdY * tCur - bMinY) / cellY;
                  const hpz = (roZ + rdZ * tCur - bMinZ) / cellZ;
                  const fx = hpx - Math.floor(hpx), fy = hpy - Math.floor(hpy), fz = hpz - Math.floor(hpz);
                  const dx = Math.min(fx, 1 - fx), dy = Math.min(fy, 1 - fy), dz = Math.min(fz, 1 - fz);
                  const e2 = Math.min(Math.max(dx, dy), Math.max(dy, dz), Math.max(dx, dz));
                  const wire = e2 < 0.065 ? 1 : 0;
                  const ca = 0.022 + wire * 0.30;
                  const brR = wire ? 1.0 : 0.16, brG = wire ? 0.74 : 0.72, brB = wire ? 0.32 : 0.70;
                  accR += (1 - accA) * ca * brR;
                  accG += (1 - accA) * ca * brG;
                  accB += (1 - accA) * ca * brB;
                  accA += (1 - accA) * ca;
                }
                tCur = tNextCell;
                if (tnx <= tny && tnx <= tnz) { tnx += tdx; cx += sx; }
                else if (tny <= tnz) { tny += tdy; cy += sy; }
                else { tnz += tdz; cz += sz; }
              }
              if (accA > 0) {
                const k = accA * 0.85;
                rCol = rCol * (1 - k) + (accR / accA) * k;
                gCol = gCol * (1 - k) + (accG / accA) * k;
                bCol = bCol * (1 - k) + (accB / accA) * k;
              }
            }
          }
        }

        out[idx] = Math.max(0, Math.min(255, Math.round(rCol * 255)));
        out[idx + 1] = Math.max(0, Math.min(255, Math.round(gCol * 255)));
        out[idx + 2] = Math.max(0, Math.min(255, Math.round(bCol * 255)));
        out[idx + 3] = 255;
      }
    }
    octx.putImageData(img, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(this._fallbackCanvas, 0, 0, w, h);
  }
}
