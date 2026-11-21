//==========================================================================================================================================
//                                                    SHADOWPIPELINE.JS
//==========================================================================================================================================
// Non-SDF Shadow Mapping (Contact+PCSS, PCSS, SMRT, PCF, BVH), RenderSettings uniform mapping,
// tight scene-fitted Directional Sun + Area Light shadow matrices, and static unit caster meshes.

import { VEHICLE_AXLE_MOUNTS } from "./VehicleBvhAssets.js";

export function EncodeRenderSettingsUniforms(Rs = {}) {
  const ShadowMap = {
    contact_pcss: 0,
    pcss: 1,
    smrt: 2,
    pcf: 3,
    bvh: 4,
  };
  const GiMap = {
    sdf: 0,
    bvh: 1,
    off: 2,
  };
  const ReflMap = {
    bvh: 1,
    env: 2,
  };
  const EngineMap = {
    matcap: 0,
    openpbr_direct: 1,
    gtx_sdf_gi: 2,
    highend_bvh4_gi: 3,
    pt_realtime: 4,
    pt_final: 5,
  };
  const PenumbraMap = {
    point_sdf: 0,
    analytic_cone: 1,
    voxel_clipmap: 2,
    voxel_cone_gi: 3,
    hybrid_sobol_cap: 4,
    sdf_mc_ref: 5,
  };
  const engineMode = EngineMap[Rs.RenderEngine] ?? 2;
  const sdfPenumbraMode = PenumbraMap[Rs.SdfPenumbraMode] ?? 3;
  return {
    engineMode,
    sdfPenumbraMode,
    packedEnginePenumbra: engineMode * 8 + sdfPenumbraMode,
    shadowAlg: ShadowMap[Rs.ShadowAlgorithm] ?? 0,
    giAlg: GiMap[Rs.GiAlgorithm] ?? 0,
    reflAlg: ReflMap[Rs.ReflectionAlgorithm] ?? 1,
    shadowMapRes: Number(Rs.ShadowMapRes) === 1024 ? 1024 : 2048,
    pcssTaps: Math.max(8, Math.min(48, Math.round(Number(Rs.PcssTaps) || 16))),
    smrtRays: Math.max(2, Math.min(12, Math.round(Number(Rs.SmrtRays) || 4))),
    shadowRays: Math.max(1, Math.min(16, Math.round(Number(Rs.ShadowRays) || 2))),
    shadowSoftness: Math.max(0.25, Math.min(2.5, Number(Rs.ShadowSoftness) || 1.0)),
    planeFilter: Rs.PlaneFilter !== false ? 1 : 0,
    halfResShadows: Rs.HalfResShadows ? 1 : 0,
    emissiveShadowed: Rs.EmissiveShadowMode === "unshadowed" ? 0 : 1,
    giSamples: Math.max(2, Math.min(20, Math.round(Number(Rs.GiSamples) || 6))),
    giCones: Math.max(4, Math.min(14, Math.round(Number(Rs.GiCones) || 6))),
    giIntensity: Math.max(0, Math.min(2.5, Number(Rs.GiIntensity) ?? 1.25)),
    fillBoost: Math.max(0.2, Math.min(2.5, Number(Rs.FillBoost) ?? 1.15)),
    reflRays: Math.max(1, Math.min(8, Math.round(Number(Rs.ReflectionRays) || 2))),
    reflBounces: Math.max(1, Math.min(3, Math.round(Number(Rs.ReflectionBounces) || 2))),
    renderScale: [50, 75, 100, 125].includes(Number(Rs.RenderScale)) ? Number(Rs.RenderScale) : 100,
    antiAliasing: Rs.AntiAliasing !== false ? 1 : 0,
  };
}

function Normalize3(V, Fallback = [0, 0, 1]) {
  const L = Math.hypot(V[0], V[1], V[2]);
  return L > 1e-6 ? [V[0] / L, V[1] / L, V[2] / L] : Fallback;
}

function Cross3(A, B) {
  return [
    A[1] * B[2] - A[2] * B[1],
    A[2] * B[0] - A[0] * B[2],
    A[0] * B[1] - A[1] * B[0],
  ];
}

function Dot3(A, B) {
  return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

function MulMat4(A, B) {
  const Out = new Float32Array(16);
  for (let C = 0; C < 4; ++C) {
    for (let R = 0; R < 4; ++R) {
      Out[C * 4 + R] =
        A[0 * 4 + R] * B[C * 4 + 0] +
        A[1 * 4 + R] * B[C * 4 + 1] +
        A[2 * 4 + R] * B[C * 4 + 2] +
        A[3 * 4 + R] * B[C * 4 + 3];
    }
  }
  return Out;
}

function BuildLookAt(Eye, Target, UpHint) {
  const F = Normalize3([Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]], [0, 0, -1]);
  const Up = Math.abs(Dot3(F, UpHint)) > 0.99 ? [0, 1, 0] : UpHint;
  const S = Normalize3(Cross3(F, Up), [1, 0, 0]);
  const U = Cross3(S, F);
  const M = new Float32Array(16);
  M[0] = S[0]; M[4] = S[1]; M[8]  = S[2]; M[12] = -Dot3(S, Eye);
  M[1] = U[0]; M[5] = U[1]; M[9]  = U[2]; M[13] = -Dot3(U, Eye);
  M[2] = -F[0]; M[6] = -F[1]; M[10] = -F[2]; M[14] = Dot3(F, Eye);
  M[3] = 0; M[7] = 0; M[11] = 0; M[15] = 1;
  return M;
}

function BuildOrthoGl(L, R, B, T, N, F) {
  const M = new Float32Array(16);
  M[0] = 2 / (R - L);
  M[5] = 2 / (T - B);
  M[10] = -2 / (F - N);
  M[12] = -(R + L) / (R - L);
  M[13] = -(T + B) / (T - B);
  M[14] = -(F + N) / (F - N);
  M[15] = 1;
  return M;
}

function BuildPerspectiveGl(FovYRad, Aspect, Near, Far) {
  const F = 1.0 / Math.tan(FovYRad * 0.5);
  const M = new Float32Array(16);
  M[0] = F / Aspect;
  M[5] = F;
  M[10] = (Far + Near) / (Near - Far);
  M[11] = -1;
  M[14] = (2 * Far * Near) / (Near - Far);
  return M;
}

export function BuildSunShadowMatrix(ActiveScene) {
  const SunDir = Normalize3(ActiveScene?.SunDir || [0.42, -0.55, 0.72], [0.42, -0.55, 0.72]);
  // Compute scene bounding sphere from cast-shadow primitives + vehicles (excluding giant floor if > 20m)
  let MinX = -3.8, MinY = -3.2, MinZ = -0.8;
  let MaxX = 3.8, MaxY = 3.2, MaxZ = 2.6;
  const Prims = ActiveScene?.Primitives || [];
  for (const P of Prims) {
    if (!P.CastShadows) continue;
    const MaxH = Math.max(P.HalfExtents[0], P.HalfExtents[1], P.HalfExtents[2]);
    if (MaxH > 12.0) continue;
    MinX = Math.min(MinX, P.Center[0] - P.HalfExtents[0]);
    MinY = Math.min(MinY, P.Center[1] - P.HalfExtents[1]);
    MinZ = Math.min(MinZ, P.Center[2] - P.HalfExtents[2]);
    MaxX = Math.max(MaxX, P.Center[0] + P.HalfExtents[0]);
    MaxY = Math.max(MaxY, P.Center[1] + P.HalfExtents[1]);
    MaxZ = Math.max(MaxZ, P.Center[2] + P.HalfExtents[2]);
  }
  const Vehicles = ActiveScene?.Vehicles || [];
  for (const V of Vehicles) {
    const Sx = Math.max(0.1, Math.abs(V.Scale?.[0] ?? 1));
    const Sy = Math.max(0.1, Math.abs(V.Scale?.[1] ?? 1));
    const Sz = Math.max(0.1, Math.abs(V.Scale?.[2] ?? 1));
    const R = 3.6 * Math.max(Sx, Sy, Sz);
    MinX = Math.min(MinX, V.Position[0] - R);
    MinY = Math.min(MinY, V.Position[1] - R);
    MinZ = Math.min(MinZ, V.Position[2] - 0.5);
    MaxX = Math.max(MaxX, V.Position[0] + R);
    MaxY = Math.max(MaxY, V.Position[1] + R);
    MaxZ = Math.max(MaxZ, V.Position[2] + 2.0 * Sz);
  }
  const Cx = 0.5 * (MinX + MaxX);
  const Cy = 0.5 * (MinY + MaxY);
  const Cz = 0.5 * (MinZ + MaxZ);
  const HalfSpan = Math.max(4.5, Math.min(14.0, 0.55 * Math.hypot(MaxX - MinX, MaxY - MinY, MaxZ - MinZ)));
  const Dist = HalfSpan * 2.2;
  const Eye = [Cx + SunDir[0] * Dist, Cy + SunDir[1] * Dist, Cz + SunDir[2] * Dist];
  const SunNear = 0.5;
  const SunFar = Dist * 2.2;
  const View = BuildLookAt(Eye, [Cx, Cy, Cz], [0, 0, 1]);
  const Proj = BuildOrthoGl(-HalfSpan, HalfSpan, -HalfSpan, HalfSpan, SunNear, SunFar);
  return {
    vp: MulMat4(Proj, View),
    eye: Eye,
    forward: [-SunDir[0], -SunDir[1], -SunDir[2]],
    orthoHalfSpan: HalfSpan,
    near: SunNear,
    far: SunFar,
    sunDist: SunFar - SunNear,
  };
}

export function BuildAreaShadowMatrix(Light) {
  if (!Light) return null;
  const Center = Light.Center || [0.55, 0.35, 1.82];
  const Normal = Normalize3(Light.Normal || [0, 0, -1], [0, 0, -1]);
  // Place perspective shadow eye slightly in front of emitting face so overhead roof baffles don't occlude the frustum
  const Eye = [Center[0] + Normal[0] * 0.02, Center[1] + Normal[1] * 0.02, Center[2] + Normal[2] * 0.02];
  const Target = [Center[0] + Normal[0] * 2.0, Center[1] + Normal[1] * 2.0, Center[2] + Normal[2] * 2.0];
  const AreaNear = 0.20;
  const AreaFar = 14.0;
  const FovYRad = (124 * Math.PI) / 180;
  const View = BuildLookAt(Eye, Target, [0, 1, 0]);
  const Proj = BuildPerspectiveGl(FovYRad, 1.0, AreaNear, AreaFar);
  return {
    vp: MulMat4(Proj, View),
    lightPos: Eye,
    forward: Normal,
    near: AreaNear,
    far: AreaFar,
    perspTan: Math.tan(FovYRad * 0.5),
  };
}

export function BuildPrimModelMatrix(Prim) {
  const [Cx, Cy, Cz] = Prim.Center;
  const [Rx, Ry, Rz] = Prim.RotationRad || [0, 0, 0];
  const [Hx, Hy, Hz] = Prim.HalfExtents;
  const CosX = Math.cos(Rx), SinX = Math.sin(Rx);
  const CosY = Math.cos(Ry), SinY = Math.sin(Ry);
  const CosZ = Math.cos(Rz), SinZ = Math.sin(Rz);

  const C0x = CosZ * CosY, C0y = SinZ * CosY, C0z = -SinY;
  const C1x = CosZ * SinY * SinX - SinZ * CosX, C1y = SinZ * SinY * SinX + CosZ * CosX, C1z = CosY * SinX;
  const C2x = CosZ * SinY * CosX + SinZ * SinX, C2y = SinZ * SinY * CosX - CosZ * SinX, C2z = CosY * CosX;

  const M = new Float32Array(16);
  M[0] = C0x * Hx; M[1] = C0y * Hx; M[2] = C0z * Hx; M[3] = 0;
  M[4] = C1x * Hy; M[5] = C1y * Hy; M[6] = C1z * Hy; M[7] = 0;
  M[8] = C2x * Hz; M[9] = C2y * Hz; M[10] = C2z * Hz; M[11] = 0;
  M[12] = Cx; M[13] = Cy; M[14] = Cz; M[15] = 1;
  return M;
}

export function BuildVehiclePartMatrices(Vehicle) {
  const Pos = Vehicle.Position || [0, 0, 0];
  const Rot = Vehicle.Rotation || [0, 0, 0];
  const Scl = Vehicle.Scale || [1, 1, 1];
  const Rx = ((Number(Rot[0]) || 0) * Math.PI) / 180;
  const Ry = ((Number(Rot[1]) || 0) * Math.PI) / 180;
  const Rz = ((Number(Rot[2]) || 0) * Math.PI) / 180;
  const Cx = Math.cos(Rx), Sx = Math.sin(Rx);
  const Cy = Math.cos(Ry), Sy = Math.sin(Ry);
  const Cz = Math.cos(Rz), Sz = Math.sin(Rz);

  const R0 = [Cz * Cy, Sz * Cy, -Sy];
  const R1 = [Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx];
  const R2 = [Cz * Sy * Cx + Sz * Sx, Sz * Sy * Cx - Cz * Sx, Cy * Cx];

  const BuildMat = (Offset, MirrorRight) => {
    const Ox = Offset[0] * Scl[0];
    const Oy = Offset[1] * Scl[1];
    const Oz = Offset[2] * Scl[2];
    const Wx = Pos[0] + R0[0] * Ox + R1[0] * Oy + R2[0] * Oz;
    const Wy = Pos[1] + R0[1] * Ox + R1[1] * Oy + R2[1] * Oz;
    const Wz = Pos[2] + R0[2] * Ox + R1[2] * Oy + R2[2] * Oz;
    const M = new Float32Array(16);
    if (MirrorRight) {
      // 180-deg yaw around Z for right-side wheels
      M[0] = -R0[0] * Scl[0]; M[1] = -R0[1] * Scl[0]; M[2] = -R0[2] * Scl[0]; M[3] = 0;
      M[4] = -R1[0] * Scl[1]; M[5] = -R1[1] * Scl[1]; M[6] = -R1[2] * Scl[1]; M[7] = 0;
      M[8] =  R2[0] * Scl[2]; M[9] =  R2[1] * Scl[2]; M[10] = R2[2] * Scl[2]; M[11] = 0;
    } else {
      M[0] = R0[0] * Scl[0]; M[1] = R0[1] * Scl[0]; M[2] = R0[2] * Scl[0]; M[3] = 0;
      M[4] = R1[0] * Scl[1]; M[5] = R1[1] * Scl[1]; M[6] = R1[2] * Scl[1]; M[7] = 0;
      M[8] = R2[0] * Scl[2]; M[9] = R2[1] * Scl[2]; M[10] = R2[2] * Scl[2]; M[11] = 0;
    }
    M[12] = Wx; M[13] = Wy; M[14] = Wz; M[15] = 1;
    return M;
  };

  // Lift by tyre radius * scaleZ so wheels rest cleanly at z = Vehicle.Position[2]
  const LiftZ = 0.5085 * Scl[2];
  const BodyPos = [Pos[0], Pos[1], Pos[2] + LiftZ];
  const BodyM = new Float32Array(16);
  BodyM[0] = R0[0] * Scl[0]; BodyM[1] = R0[1] * Scl[0]; BodyM[2] = R0[2] * Scl[0]; BodyM[3] = 0;
  BodyM[4] = R1[0] * Scl[1]; BodyM[5] = R1[1] * Scl[1]; BodyM[6] = R1[2] * Scl[1]; BodyM[7] = 0;
  BodyM[8] = R2[0] * Scl[2]; BodyM[9] = R2[1] * Scl[2]; BodyM[10] = R2[2] * Scl[2]; BodyM[11] = 0;
  BodyM[12] = BodyPos[0]; BodyM[13] = BodyPos[1]; BodyM[14] = BodyPos[2]; BodyM[15] = 1;

  const WheelMats = VEHICLE_AXLE_MOUNTS.map((Mount) => {
    const Off = [Mount.x, Mount.y, Mount.z + 0.5085];
    return {
      mat: BuildMat(Off, Mount.mirrorRight),
      mirrorRight: Mount.mirrorRight,
    };
  });

  return { body: BodyM, wheels: WheelMats };
}

function PushTri(Arr, A, B, C) {
  Arr.push(A[0], A[1], A[2], B[0], B[1], B[2], C[0], C[1], C[2]);
}

function BuildBoxVerts() {
  const P = [
    [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
    [-1, -1,  1], [1, -1,  1], [1, 1,  1], [-1, 1,  1],
  ];
  const Tris = [
    [0, 2, 1], [0, 3, 2],
    [4, 5, 6], [4, 6, 7],
    [0, 1, 5], [0, 5, 4],
    [2, 3, 7], [2, 7, 6],
    [1, 2, 6], [1, 6, 5],
    [3, 0, 4], [3, 4, 7],
  ];
  const Out = [];
  for (const [I, J, K] of Tris) PushTri(Out, P[I], P[J], P[K]);
  return new Float32Array(Out);
}

function BuildSphereVerts(Segs = 20, Rings = 12) {
  const Out = [];
  for (let R = 0; R < Rings; ++R) {
    const T0 = (R / Rings) * Math.PI;
    const T1 = ((R + 1) / Rings) * Math.PI;
    for (let S = 0; S < Segs; ++S) {
      const P0 = (S / Segs) * Math.PI * 2;
      const P1 = ((S + 1) / Segs) * Math.PI * 2;
      const Pt = (T, P) => [Math.sin(T) * Math.cos(P), Math.sin(T) * Math.sin(P), Math.cos(T)];
      const A = Pt(T0, P0), B = Pt(T1, P0), C = Pt(T1, P1), D = Pt(T0, P1);
      if (R > 0) PushTri(Out, A, B, D);
      if (R < Rings - 1) PushTri(Out, B, C, D);
    }
  }
  return new Float32Array(Out);
}

function BuildFrustumVerts(Z0, Z1, R0, R1, Segs = 20) {
  const Out = [];
  for (let S = 0; S < Segs; ++S) {
    const A0 = (S / Segs) * Math.PI * 2;
    const A1 = ((S + 1) / Segs) * Math.PI * 2;
    const C0 = Math.cos(A0), S0 = Math.sin(A0);
    const C1 = Math.cos(A1), S1 = Math.sin(A1);
    const B0 = [C0 * R0, S0 * R0, Z0];
    const B1 = [C1 * R0, S1 * R0, Z0];
    const T0 = [C0 * R1, S0 * R1, Z1];
    const T1 = [C1 * R1, S1 * R1, Z1];
    PushTri(Out, B0, B1, T1);
    PushTri(Out, B0, T1, T0);
    PushTri(Out, [0, 0, Z0], B1, B0);
    PushTri(Out, [0, 0, Z1], T0, T1);
  }
  return new Float32Array(Out);
}

function BuildTorusVerts(MajorSegs = 22, MinorSegs = 12) {
  const Major = 0.68, Minor = 0.55;
  const Out = [];
  for (let I = 0; I < MajorSegs; ++I) {
    const U0 = (I / MajorSegs) * Math.PI * 2;
    const U1 = ((I + 1) / MajorSegs) * Math.PI * 2;
    for (let J = 0; J < MinorSegs; ++J) {
      const V0 = (J / MinorSegs) * Math.PI * 2;
      const V1 = ((J + 1) / MinorSegs) * Math.PI * 2;
      const Pt = (U, V) => {
        const R = Major + Minor * Math.cos(V);
        return [R * Math.cos(U), R * Math.sin(U), Minor * Math.sin(V)];
      };
      const A = Pt(U0, V0), B = Pt(U1, V0), C = Pt(U1, V1), D = Pt(U0, V1);
      PushTri(Out, A, B, C);
      PushTri(Out, A, C, D);
    }
  }
  return new Float32Array(Out);
}

export function BuildUnitShadowGeometries() {
  const ConeTop = BuildFrustumVerts(-0.78, 1.0, 0.72, 0.13, 18);
  const ConeBase = new Float32Array(BuildBoxVerts().map((V, Idx) => {
    if (Idx % 3 === 2) return V < 0 ? -1.0 : -0.78;
    return V;
  }));
  const ConeMerged = new Float32Array(ConeTop.length + ConeBase.length);
  ConeMerged.set(ConeTop, 0);
  ConeMerged.set(ConeBase, ConeTop.length);

  const CylConeBot = BuildFrustumVerts(-1.0, -0.84, 1.0, 0.94, 18);
  const CylConeTop = BuildFrustumVerts(-0.84, 1.0, 0.50, 0.37, 18);
  const CylConeMerged = new Float32Array(CylConeBot.length + CylConeTop.length);
  CylConeMerged.set(CylConeBot, 0);
  CylConeMerged.set(CylConeTop, CylConeBot.length);

  return {
    0: BuildBoxVerts(),
    1: BuildSphereVerts(20, 12),
    2: BuildFrustumVerts(-1.0, 1.0, 1.0, 1.0, 20),
    3: ConeMerged,
    4: BuildTorusVerts(22, 12),
    5: CylConeMerged,
  };
}
