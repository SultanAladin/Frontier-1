// Procedural 3D triangle surface constructors for Photographic Studio Lobby
// components + Frontier .GeometrySpace binary vehicle decoder.
// Packed vertex layout per triangle vertex:
// [px, py, pz, nx, ny, nz, matCode, u] (8 float32 = 32 bytes per vertex)

export const SurfaceMaterialCode = {
  BodyPaint: 0,
  CarGlass: 1,
  CarOptics: 2,
  CarTrim: 3,
  CarTire: 4,
  CarRim: 5,
  Cyclorama: 6,
  TiledFloor: 7,
  DarkMetal: 8,
  ChromeMetal: 9,
  RubberMat: 10,
  EmissiveDiffuser: 11,
  UmbrellaSilver: 12,
  TetherScreen: 13,
  PelicanCase: 14,
  CeilingRib: 15,
  ForestMoss: 16,
  MossyRock: 17,
  FallenWood: 18,
  RiverPebble: 19,
  ForestWater: 20,
  ForestBackdrop: 21,
  BlackFlag: 22,
  FanHousing: 23,
  FernFrond: 24,
  GrassBlade: 25,
  FanBlade: 26,
  CherryRedTool: 27,
  SafetyYellow: 28,
  ConcreteGarage: 29,
};

function CreateBuilder() {
  const Verts = [];
  const Bounds = {
    Min: [Infinity, Infinity, Infinity],
    Max: [-Infinity, -Infinity, -Infinity],
  };

  function PushTri(A, B, C, Na, Nb, Nc, Mat, Ua = 0, Ub = 0, Uc = 0) {
    Verts.push(
      A[0], A[1], A[2], Na[0], Na[1], Na[2], Mat, Ua,
      B[0], B[1], B[2], Nb[0], Nb[1], Nb[2], Mat, Ub,
      C[0], C[1], C[2], Nc[0], Nc[1], Nc[2], Mat, Uc,
    );
    for (const P of [A, B, C]) {
      if (P[0] < Bounds.Min[0]) Bounds.Min[0] = P[0];
      if (P[1] < Bounds.Min[1]) Bounds.Min[1] = P[1];
      if (P[2] < Bounds.Min[2]) Bounds.Min[2] = P[2];
      if (P[0] > Bounds.Max[0]) Bounds.Max[0] = P[0];
      if (P[1] > Bounds.Max[1]) Bounds.Max[1] = P[1];
      if (P[2] > Bounds.Max[2]) Bounds.Max[2] = P[2];
    }
  }

  function FlatNormal(A, B, C) {
    const Ux = B[0] - A[0], Uy = B[1] - A[1], Uz = B[2] - A[2];
    const Vx = C[0] - A[0], Vy = C[1] - A[1], Vz = C[2] - A[2];
    let Nx = Uy * Vz - Uz * Vy;
    let Ny = Uz * Vx - Ux * Vz;
    let Nz = Ux * Vy - Uy * Vx;
    const Len = Math.hypot(Nx, Ny, Nz);
    if (Len < 1e-7) return [0, 1, 0];
    return [Nx / Len, Ny / Len, Nz / Len];
  }

  function AddQuadFlat(A, B, C, D, Mat, U = 0) {
    const N = FlatNormal(A, B, C);
    PushTri(A, B, C, N, N, N, Mat, U, U, U);
    PushTri(A, C, D, N, N, N, Mat, U, U, U);
  }

  function AddBox(Center, HalfSize, Mat, PitchRad = 0, YawRad = 0, RollRad = 0) {
    const [Cx, Cy, Cz] = Center;
    const [Hx, Hy, Hz] = HalfSize;
    const Cp = Math.cos(PitchRad), Sp = Math.sin(PitchRad);
    const Cyw = Math.cos(YawRad), Syw = Math.sin(YawRad);
    const Cr = Math.cos(RollRad), Sr = Math.sin(RollRad);

    function Xf(Lx, Ly, Lz) {
      // Roll Z -> Pitch X -> Yaw Y
      const X1 = Lx * Cr - Ly * Sr;
      const Y1 = Lx * Sr + Ly * Cr;
      const Z1 = Lz;
      const X2 = X1;
      const Y2 = Y1 * Cp - Z1 * Sp;
      const Z2 = Y1 * Sp + Z1 * Cp;
      return [
        Cx + X2 * Cyw + Z2 * Syw,
        Cy + Y2,
        Cz - X2 * Syw + Z2 * Cyw,
      ];
    }

    const P = [
      Xf(-Hx, -Hy, -Hz), Xf(Hx, -Hy, -Hz), Xf(Hx, Hy, -Hz), Xf(-Hx, Hy, -Hz),
      Xf(-Hx, -Hy, Hz), Xf(Hx, -Hy, Hz), Xf(Hx, Hy, Hz), Xf(-Hx, Hy, Hz),
    ];
    AddQuadFlat(P[4], P[5], P[6], P[7], Mat); // +Z
    AddQuadFlat(P[1], P[0], P[3], P[2], Mat); // -Z
    AddQuadFlat(P[5], P[1], P[2], P[6], Mat); // +X
    AddQuadFlat(P[0], P[4], P[7], P[3], Mat); // -X
    AddQuadFlat(P[7], P[6], P[2], P[3], Mat); // +Y
    AddQuadFlat(P[0], P[1], P[5], P[4], Mat); // -Y
  }

  function AddTube(Start, End, RadiusA, RadiusB, Segments, Mat, CapEnds = true) {
    const Dx = End[0] - Start[0];
    const Dy = End[1] - Start[1];
    const Dz = End[2] - Start[2];
    const Len = Math.hypot(Dx, Dy, Dz);
    if (Len < 1e-5) return;
    const Axis = [Dx / Len, Dy / Len, Dz / Len];
    const Ref = Math.abs(Axis[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0];
    // Right = normalize(cross(Ref, Axis))
    let Rx = Ref[1] * Axis[2] - Ref[2] * Axis[1];
    let Ry = Ref[2] * Axis[0] - Ref[0] * Axis[2];
    let Rz = Ref[0] * Axis[1] - Ref[1] * Axis[0];
    const RL = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= RL; Ry /= RL; Rz /= RL;
    // Fwd = cross(Axis, Right)
    const Fx = Axis[1] * Rz - Axis[2] * Ry;
    const Fy = Axis[2] * Rx - Axis[0] * Rz;
    const Fz = Axis[0] * Ry - Axis[1] * Rx;

    const RingA = [];
    const RingB = [];
    const Norm = [];
    for (let I = 0; I <= Segments; I++) {
      const Angle = (I / Segments) * Math.PI * 2;
      const C = Math.cos(Angle), S = Math.sin(Angle);
      const Nx = Rx * C + Fx * S;
      const Ny = Ry * C + Fy * S;
      const Nz = Rz * C + Fz * S;
      Norm.push([Nx, Ny, Nz]);
      RingA.push([Start[0] + Nx * RadiusA, Start[1] + Ny * RadiusA, Start[2] + Nz * RadiusA]);
      RingB.push([End[0] + Nx * RadiusB, End[1] + Ny * RadiusB, End[2] + Nz * RadiusB]);
    }

    for (let I = 0; I < Segments; I++) {
      PushTri(RingA[I], RingA[I + 1], RingB[I + 1], Norm[I], Norm[I + 1], Norm[I + 1], Mat);
      PushTri(RingA[I], RingB[I + 1], RingB[I], Norm[I], Norm[I + 1], Norm[I], Mat);
      if (CapEnds) {
        const NegAxis = [-Axis[0], -Axis[1], -Axis[2]];
        PushTri(Start, RingA[I + 1], RingA[I], NegAxis, NegAxis, NegAxis, Mat);
        PushTri(End, RingB[I], RingB[I + 1], Axis, Axis, Axis, Mat);
      }
    }
  }

  function AddSphere(Center, Radius, Rings, Slices, Mat) {
    for (let R = 0; R < Rings; R++) {
      const Phi0 = (R / Rings) * Math.PI;
      const Phi1 = ((R + 1) / Rings) * Math.PI;
      for (let S = 0; S < Slices; S++) {
        const Th0 = (S / Slices) * Math.PI * 2;
        const Th1 = ((S + 1) / Slices) * Math.PI * 2;
        const Pt = (Phi, Th) => {
          const Nx = Math.sin(Phi) * Math.cos(Th);
          const Ny = Math.cos(Phi);
          const Nz = Math.sin(Phi) * Math.sin(Th);
          return [
            [Center[0] + Nx * Radius, Center[1] + Ny * Radius, Center[2] + Nz * Radius],
            [Nx, Ny, Nz],
          ];
        };
        const [P00, N00] = Pt(Phi0, Th0);
        const [P10, N10] = Pt(Phi1, Th0);
        const [P11, N11] = Pt(Phi1, Th1);
        const [P01, N01] = Pt(Phi0, Th1);
        if (R > 0) PushTri(P00, P11, P01, N00, N11, N01, Mat);
        if (R < Rings - 1) PushTri(P00, P10, P11, N00, N10, N11, Mat);
      }
    }
  }

  function Finish() {
    if (!Number.isFinite(Bounds.Min[0])) {
      Bounds.Min = [-0.5, 0, -0.5];
      Bounds.Max = [0.5, 1, 0.5];
    }
    return {
      Buffer: new Float32Array(Verts),
      VertexCount: Verts.length / 8,
      TriangleCount: Verts.length / 24,
      Bounds,
    };
  }

  return { PushTri, FlatNormal, AddQuadFlat, AddBox, AddTube, AddSphere, Finish };
}

// Decode Frontier .GeometrySpace binary vehicle (ControlledIntegrator.GeometrySpace)
// into packed WebGL2 vertex buffer with smooth per-part-group normals.
export function DecodeGeometrySpace(ArrayBuffer) {
  const Bytes = new Uint8Array(ArrayBuffer);
  const Marker = "# === FRONTIER_BINARY_BOUNDARY ===\n";
  const HeaderText = new TextDecoder().decode(Bytes.subarray(0, Math.min(2048, Bytes.length)));
  const MarkerIdx = HeaderText.indexOf(Marker);
  if (MarkerIdx < 0) throw new Error("Missing FRONTIER_BINARY_BOUNDARY marker");

  const RawOffset = MarkerIdx + Marker.length;
  const View = new DataView(ArrayBuffer);
  const AlignedOffset = (RawOffset + 63) & ~63;
  const Offset =
    View.getUint32(RawOffset, true) > 0 && View.getUint32(RawOffset, true) < 500000
      ? RawOffset
      : AlignedOffset;

  const VertexCount = View.getUint32(Offset, true);
  const FaceCount = View.getUint32(Offset + 4, true);
  if (VertexCount === 0 || FaceCount === 0 || VertexCount > 500000) {
    throw new Error("Invalid GeometrySpace header counts");
  }

  const PosBase = Offset + 8;
  const Positions = new Float32Array(VertexCount * 3);
  for (let I = 0; I < VertexCount * 3; I++) {
    Positions[I] = View.getFloat32(PosBase + I * 4, true);
  }

  const FaceBase = PosBase + VertexCount * 12;
  const FaceIdx = new Uint32Array(FaceCount * 3);
  const FacePart = new Uint16Array(FaceCount);

  // Accumulate area-weighted smooth normals per (vertex, partGroup) to keep material seams crisp
  const PartNormals = Array.from({ length: 6 }, () => new Float32Array(VertexCount * 3));
  let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
  let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;

  for (let V = 0; V < VertexCount; V++) {
    const X = Positions[V * 3];
    const Y = Positions[V * 3 + 1];
    const Z = Positions[V * 3 + 2];
    if (X < MinX) MinX = X; if (X > MaxX) MaxX = X;
    if (Y < MinY) MinY = Y; if (Y > MaxY) MaxY = Y;
    if (Z < MinZ) MinZ = Z; if (Z > MaxZ) MaxZ = Z;
  }

  for (let F = 0; F < FaceCount; F++) {
    const Base = FaceBase + F * 16;
    const I0 = View.getUint32(Base, true);
    const I1 = View.getUint32(Base + 4, true);
    const I2 = View.getUint32(Base + 8, true);
    const Part = Math.min(5, View.getUint16(Base + 14, true));
    FaceIdx[F * 3] = I0;
    FaceIdx[F * 3 + 1] = I1;
    FaceIdx[F * 3 + 2] = I2;
    FacePart[F] = Part;

    const Ax = Positions[I0 * 3], Ay = Positions[I0 * 3 + 1], Az = Positions[I0 * 3 + 2];
    const Bx = Positions[I1 * 3], By = Positions[I1 * 3 + 1], Bz = Positions[I1 * 3 + 2];
    const Cx = Positions[I2 * 3], Cy = Positions[I2 * 3 + 1], Cz = Positions[I2 * 3 + 2];
    const Ux = Bx - Ax, Uy = By - Ay, Uz = Bz - Az;
    const Vx = Cx - Ax, Vy = Cy - Ay, Vz = Cz - Az;
    const Nx = Uy * Vz - Uz * Vy;
    const Ny = Uz * Vx - Ux * Vz;
    const Nz = Ux * Vy - Uy * Vx;

    const Acc = PartNormals[Part];
    Acc[I0 * 3] += Nx; Acc[I0 * 3 + 1] += Ny; Acc[I0 * 3 + 2] += Nz;
    Acc[I1 * 3] += Nx; Acc[I1 * 3 + 1] += Ny; Acc[I1 * 3 + 2] += Nz;
    Acc[I2 * 3] += Nx; Acc[I2 * 3 + 1] += Ny; Acc[I2 * 3 + 2] += Nz;
  }

  for (let P = 0; P < 6; P++) {
    const Acc = PartNormals[P];
    for (let V = 0; V < VertexCount; V++) {
      const O = V * 3;
      const Len = Math.hypot(Acc[O], Acc[O + 1], Acc[O + 2]) || 1;
      Acc[O] /= Len; Acc[O + 1] /= Len; Acc[O + 2] /= Len;
    }
  }

  const Packed = new Float32Array(FaceCount * 3 * 8);
  let Cursor = 0;
  for (let F = 0; F < FaceCount; F++) {
    const Part = FacePart[F];
    const Acc = PartNormals[Part];
    for (let K = 0; K < 3; K++) {
      const Vi = FaceIdx[F * 3 + K];
      const Po = Vi * 3;
      Packed[Cursor++] = Positions[Po];
      Packed[Cursor++] = Positions[Po + 1] - MinY; // Guarantee flush Y=0 grounding
      Packed[Cursor++] = Positions[Po + 2];
      Packed[Cursor++] = Acc[Po];
      Packed[Cursor++] = Acc[Po + 1];
      Packed[Cursor++] = Acc[Po + 2];
      Packed[Cursor++] = Part;
      Packed[Cursor++] = 0;
    }
  }

  return {
    Buffer: Packed,
    VertexCount: FaceCount * 3,
    TriangleCount: FaceCount,
    Bounds: {
      Min: [MinX, 0, MinZ],
      Max: [MaxX, MaxY - MinY, MaxZ],
    },
    Source: "ControlledIntegrator.GeometrySpace",
  };
}

// High-detail sculpted fallback coupe if running offline without HTTP server
export function ConstructFallbackVehicleSurface() {
  const B = CreateBuilder();
  // Lower chassis skirt
  B.AddBox([0, 0.34, 0], [2.45, 0.14, 1.02], SurfaceMaterialCode.CarTrim);
  // Sculpted main body fuselage
  B.AddBox([0.06, 0.62, 0], [2.35, 0.28, 1.04], SurfaceMaterialCode.BodyPaint);
  // Sloped front hood
  B.AddBox([1.48, 0.72, 0], [0.85, 0.14, 0.96], SurfaceMaterialCode.BodyPaint, 0, 0, -0.14);
  // Fastback cabin greenhouse
  B.AddBox([-0.24, 1.06, 0], [0.98, 0.24, 0.82], SurfaceMaterialCode.CarGlass);
  // Roof panel
  B.AddBox([-0.22, 1.28, 0], [0.68, 0.05, 0.74], SurfaceMaterialCode.BodyPaint);
  // Rear GT aero wing & uprights
  B.AddBox([-2.22, 1.18, 0], [0.18, 0.025, 0.96], SurfaceMaterialCode.CarTrim, 0, 0, 0.09);
  B.AddBox([-2.16, 0.96, -0.42], [0.06, 0.22, 0.02], SurfaceMaterialCode.DarkMetal);
  B.AddBox([-2.16, 0.96, 0.42], [0.06, 0.22, 0.02], SurfaceMaterialCode.DarkMetal);
  // Headlights & taillights
  B.AddBox([2.38, 0.68, -0.72], [0.08, 0.07, 0.22], SurfaceMaterialCode.CarOptics);
  B.AddBox([2.38, 0.68, 0.72], [0.08, 0.07, 0.22], SurfaceMaterialCode.CarOptics);
  B.AddBox([-2.38, 0.76, 0], [0.05, 0.05, 0.84], SurfaceMaterialCode.CarOptics);
  // Four tires & alloy rims
  for (const Wx of [-1.42, 1.42]) {
    for (const Wz of [-0.94, 0.94]) {
      const Sign = Wz < 0 ? -1 : 1;
      B.AddTube([Wx, 0.38, Wz - Sign * 0.16], [Wx, 0.38, Wz + Sign * 0.16], 0.38, 0.38, 24, SurfaceMaterialCode.CarTire);
      B.AddTube([Wx, 0.38, Wz - Sign * 0.17], [Wx, 0.38, Wz + Sign * 0.17], 0.25, 0.25, 18, SurfaceMaterialCode.CarRim);
    }
  }
  const Result = B.Finish();
  Result.Source = "Procedural Fallback Coupe";
  return Result;
}

// 1. Photographic Cyclorama Infinity Cove (studio.cyclorama)
function ConstructCyclorama(Params) {
  const B = CreateBuilder();
  const W = Number(Params.CoveWidth ?? 11.4);
  const D = Number(Params.FloorDepth ?? 8.2);
  const H = Number(Params.WallHeight ?? 5.6);
  const R = Math.min(Number(Params.CoveRadius ?? 1.85), Math.min(D * 0.48, H * 0.48));
  const HalfW = W * 0.5;
  const FrontZ = D * 0.42;
  const BackFlatZ = -D * 0.58 + R;
  const PlinthY = 0.026; // Subtle crisp studio cyclorama floor lip resting on outer tiled floor

  // Build smooth 2D profile (z, y, nz, ny) from front stage lip -> flat floor -> curved cove -> vertical wall
  const Profile = [];
  Profile.push([FrontZ, PlinthY, 0, 1]);
  // Subdivide flat floor slightly for clean lighting interpolation
  const FloorSteps = 6;
  for (let I = 1; I <= FloorSteps; I++) {
    const T = I / FloorSteps;
    Profile.push([FrontZ + (BackFlatZ - FrontZ) * T, PlinthY, 0, 1]);
  }
  // Curved quarter-circle cove
  const CoveSegs = 26;
  for (let I = 1; I <= CoveSegs; I++) {
    const Angle = (I / CoveSegs) * (Math.PI * 0.5);
    const Z = BackFlatZ - Math.sin(Angle) * R;
    const Y = PlinthY + R - Math.cos(Angle) * R;
    const Nz = Math.sin(Angle);
    const Ny = Math.cos(Angle);
    Profile.push([Z, Y, Nz, Ny]);
  }
  // Vertical back wall
  const BackZ = BackFlatZ - R;
  Profile.push([BackZ, H, 1, 0]);

  const XSegs = 12;
  for (let X = 0; X < XSegs; X++) {
    const X0 = -HalfW + (X / XSegs) * W;
    const X1 = -HalfW + ((X + 1) / XSegs) * W;
    for (let P = 0; P < Profile.length - 1; P++) {
      const [Z0, Y0, Nz0, Ny0] = Profile[P];
      const [Z1, Y1, Nz1, Ny1] = Profile[P + 1];
      const A = [X0, Y0, Z0], Bpt = [X1, Y0, Z0], C = [X1, Y1, Z1], Dpt = [X0, Y1, Z1];
      const Na = [0, Ny0, Nz0], Nb = [0, Ny1, Nz1];
      B.PushTri(A, Bpt, C, Na, Na, Nb, SurfaceMaterialCode.Cyclorama, X / XSegs, (X + 1) / XSegs, (X + 1) / XSegs);
      B.PushTri(A, C, Dpt, Na, Nb, Nb, SurfaceMaterialCode.Cyclorama, X / XSegs, (X + 1) / XSegs, X / XSegs);
    }
  }

  // Crisp beveled front edge & side plinth edges resting on outer tiled floor
  B.AddQuadFlat([-HalfW, 0, FrontZ], [HalfW, 0, FrontZ], [HalfW, PlinthY, FrontZ], [-HalfW, PlinthY, FrontZ], SurfaceMaterialCode.DarkMetal);
  B.AddQuadFlat([-HalfW, 0, BackZ], [-HalfW, 0, FrontZ], [-HalfW, PlinthY, FrontZ], [-HalfW, PlinthY, BackZ], SurfaceMaterialCode.DarkMetal);
  B.AddQuadFlat([HalfW, 0, FrontZ], [HalfW, 0, BackZ], [HalfW, PlinthY, BackZ], [HalfW, PlinthY, FrontZ], SurfaceMaterialCode.DarkMetal);

  // Optional side studio cove wings (framing the cyclorama like Ref 1 & Ref 2!)
  if (Params.SideWings !== false) {
    const WingDepth = D * 0.72;
    const WingHeight = H * 0.94;
    for (const Side of [-1, 1]) {
      const XWall = Side * HalfW;
      const Nx = -Side;
      const InnerX = XWall - Side * 0.95;
      // Swept vertical wing wall
      const ZBack = BackZ;
      const ZFront = BackZ + WingDepth;
      if (Side < 0) {
        B.PushTri([XWall, PlinthY, ZBack], [XWall, PlinthY, ZFront], [XWall, WingHeight, ZFront], [Nx, 0, 0], [Nx, 0, 0], [Nx, 0, 0], SurfaceMaterialCode.Cyclorama);
        B.PushTri([XWall, PlinthY, ZBack], [XWall, WingHeight, ZFront], [XWall, WingHeight, ZBack], [Nx, 0, 0], [Nx, 0, 0], [Nx, 0, 0], SurfaceMaterialCode.Cyclorama);
      } else {
        B.PushTri([XWall, PlinthY, ZFront], [XWall, PlinthY, ZBack], [XWall, WingHeight, ZBack], [Nx, 0, 0], [Nx, 0, 0], [Nx, 0, 0], SurfaceMaterialCode.Cyclorama);
        B.PushTri([XWall, PlinthY, ZFront], [XWall, WingHeight, ZBack], [XWall, WingHeight, ZFront], [Nx, 0, 0], [Nx, 0, 0], [Nx, 0, 0], SurfaceMaterialCode.Cyclorama);
      }
      // Subtle angled corner cove fillet along side wall base
      const Fillet = 0.85;
      const Steps = 10;
      for (let S = 0; S < Steps; S++) {
        const A0 = (S / Steps) * (Math.PI * 0.5);
        const A1 = ((S + 1) / Steps) * (Math.PI * 0.5);
        const Fx0 = XWall - Side * (Fillet - Math.sin(A0) * Fillet);
        const Fy0 = PlinthY + Fillet - Math.cos(A0) * Fillet;
        const Fx1 = XWall - Side * (Fillet - Math.sin(A1) * Fillet);
        const Fy1 = PlinthY + Fillet - Math.cos(A1) * Fillet;
        const N0 = [-Side * Math.sin(A0), Math.cos(A0), 0];
        const N1 = [-Side * Math.sin(A1), Math.cos(A1), 0];
        if (Side < 0) {
          B.PushTri([Fx0, Fy0, ZBack + R * 0.7], [Fx0, Fy0, ZFront], [Fx1, Fy1, ZFront], N0, N0, N1, SurfaceMaterialCode.Cyclorama);
          B.PushTri([Fx0, Fy0, ZBack + R * 0.7], [Fx1, Fy1, ZFront], [Fx1, Fy1, ZBack + R * 0.7], N0, N1, N1, SurfaceMaterialCode.Cyclorama);
        } else {
          B.PushTri([Fx0, Fy0, ZFront], [Fx0, Fy0, ZBack + R * 0.7], [Fx1, Fy1, ZBack + R * 0.7], N0, N0, N1, SurfaceMaterialCode.Cyclorama);
          B.PushTri([Fx0, Fy0, ZFront], [Fx1, Fy1, ZBack + R * 0.7], [Fx1, Fy1, ZFront], N0, N1, N1, SurfaceMaterialCode.Cyclorama);
        }
      }
      void InnerX;
    }
  }

  return B.Finish();
}

// 2. Dark Glossy Tiled Studio Outer Floor (studio.tiled_floor)
function ConstructTiledFloor(Params) {
  const B = CreateBuilder();
  const W = Number(Params.FloorWidth ?? 34);
  const D = Number(Params.FloorDepth ?? 28);
  const Hw = W * 0.5, Hd = D * 0.5;
  const N = [0, 1, 0];
  B.PushTri([-Hw, 0, Hd], [Hw, 0, Hd], [Hw, 0, -Hd], N, N, N, SurfaceMaterialCode.TiledFloor);
  B.PushTri([-Hw, 0, Hd], [Hw, 0, -Hd], [-Hw, 0, -Hd], N, N, N, SurfaceMaterialCode.TiledFloor);
  return B.Finish();
}

// 3. Dark Ribbed Industrial Ceiling & Structural Truss (studio.ceiling_truss)
function ConstructCeilingTruss(Params) {
  const B = CreateBuilder();
  const W = Number(Params.SpanWidth ?? 26);
  const D = Number(Params.SpanDepth ?? 20);
  const H = Number(Params.CeilingHeight ?? 5.95);
  const Ribs = Math.max(4, Math.min(24, Math.round(Number(Params.RibCount ?? 12))));

  // Ceiling roof deck
  B.AddBox([0, H + 0.14, 0], [W * 0.5, 0.08, D * 0.5], SurfaceMaterialCode.CeilingRib);

  // Corrugated dark ceiling ribs
  for (let I = 0; I < Ribs; I++) {
    const Z = -D * 0.45 + (I / (Ribs - 1)) * (D * 0.9);
    B.AddBox([0, H, Z], [W * 0.48, 0.09, 0.07], SurfaceMaterialCode.CeilingRib);
  }

  // Steel lighting truss beams & power conduit busbars overhead
  if (Params.TrussGrid !== false) {
    for (const Tx of [-5.6, -2.8, 0, 2.8, 5.6]) {
      B.AddBox([Tx, H - 0.18, 0], [0.06, 0.06, D * 0.45], SurfaceMaterialCode.DarkMetal);
      B.AddBox([Tx, H - 0.44, 0], [0.05, 0.05, D * 0.45], SurfaceMaterialCode.DarkMetal);
      const Bays = 12;
      for (let J = 0; J < Bays; J++) {
        const Z0 = -D * 0.42 + (J / Bays) * D * 0.84;
        const Z1 = -D * 0.42 + ((J + 1) / Bays) * D * 0.84;
        B.AddTube([Tx, H - 0.18, Z0], [Tx, H - 0.44, Z1], 0.02, 0.02, 6, SurfaceMaterialCode.DarkMetal, false);
        B.AddTube([Tx, H - 0.44, Z0], [Tx, H - 0.18, Z1], 0.02, 0.02, 6, SurfaceMaterialCode.DarkMetal, false);
      }
    }
    for (const Tz of [-4.6, -1.5, 1.5, 4.6]) {
      B.AddBox([0, H - 0.3, Tz], [W * 0.44, 0.055, 0.055], SurfaceMaterialCode.DarkMetal);
    }
  }
  return B.Finish();
}

// 4. Shared Reusable Procedural Studio Tripod / Light Stand (studio.tripod)
function ConstructTripod(Params) {
  const B = CreateBuilder();
  const StandH = Number(Params.StandHeight ?? 1.55);
  const Lift = Number(Params.ColumnLift ?? 0.28);
  const SpreadRad = ((Number(Params.LegSpreadDeg ?? 26)) * Math.PI) / 180;
  const TubeR = Number(Params.TubeRadius ?? 0.018);
  const LegSegments = Math.max(2, Math.min(4, Math.round(Number(Params.LegSegments ?? 3))));
  const HeadTilt = ((Number(Params.HeadTiltDeg ?? 0)) * Math.PI) / 180;
  const HasSpreader = Params.Spreader !== false;
  const HasCasters = Boolean(Params.Casters);
  const HasMat = Boolean(Params.FloorMat);

  const BaseY = (HasMat ? 0.032 : 0) + (HasCasters ? 0.08 : 0);
  const ApexY = BaseY + StandH * 0.72;
  const LegReach = Math.tan(SpreadRad) * (ApexY - BaseY + 0.14);

  // Optional thick matte rubber studio floor mat (matching Ref 1 foreground tripods!)
  if (HasMat) {
    B.AddBox([0, 0.016, 0], [0.58, 0.016, 0.44], SurfaceMaterialCode.RubberMat);
  }

  // Three telescoping legs with articulated stage sleeves + optional casters
  for (let L = 0; L < 3; L++) {
    const Ang = (L / 3) * Math.PI * 2 + Math.PI / 6;
    const FootX = Math.cos(Ang) * LegReach;
    const FootZ = Math.sin(Ang) * LegReach;
    const TopX = Math.cos(Ang) * 0.05;
    const TopZ = Math.sin(Ang) * 0.05;

    for (let S = 0; S < LegSegments; S++) {
      const T0 = S / LegSegments;
      const T1 = (S + 1) / LegSegments;
      const P0 = [TopX + (FootX - TopX) * T0, ApexY + (BaseY - ApexY) * T0, TopZ + (FootZ - TopZ) * T0];
      const P1 = [TopX + (FootX - TopX) * T1, ApexY + (BaseY - ApexY) * T1, TopZ + (FootZ - TopZ) * T1];
      const Rad = TubeR * (1 - S * 0.18);
      const LegMat = S === LegSegments - 1 && !HasMat ? SurfaceMaterialCode.ChromeMetal : SurfaceMaterialCode.DarkMetal;
      B.AddTube(P0, P1, Rad, Rad * 0.94, 10, LegMat);
      // Locking collar sleeve at each joint
      if (S > 0) {
        B.AddTube(
          [P0[0], P0[1] + 0.025, P0[2]],
          [P0[0], P0[1] - 0.025, P0[2]],
          Rad * 1.45,
          Rad * 1.45,
          10,
          SurfaceMaterialCode.RubberMat,
        );
      }
    }

    // Foot pad or studio rolling caster wheel
    if (HasCasters) {
      B.AddTube([FootX, BaseY, FootZ], [FootX, BaseY - 0.04, FootZ], 0.02, 0.02, 8, SurfaceMaterialCode.ChromeMetal);
      B.AddTube(
        [FootX - Math.sin(Ang) * 0.022, 0.036, FootZ + Math.cos(Ang) * 0.022],
        [FootX + Math.sin(Ang) * 0.022, 0.036, FootZ - Math.cos(Ang) * 0.022],
        0.036,
        0.036,
        12,
        SurfaceMaterialCode.RubberMat,
      );
    } else {
      B.AddTube([FootX, BaseY + 0.03, FootZ], [FootX, BaseY, FootZ], TubeR * 1.35, TubeR * 1.65, 10, SurfaceMaterialCode.RubberMat);
    }

    // Mid-level spreader strut to center column
    if (HasSpreader) {
      const MidT = 0.56;
      const Mx = TopX + (FootX - TopX) * MidT;
      const My = ApexY + (BaseY - ApexY) * MidT;
      const Mz = TopZ + (FootZ - TopZ) * MidT;
      B.AddTube([0, My - 0.03, 0], [Mx, My, Mz], TubeR * 0.58, TubeR * 0.58, 8, SurfaceMaterialCode.DarkMetal);
    }
  }

  // Center spider collar & telescoping main vertical mast
  const ColTopY = BaseY + StandH + Lift;
  B.AddTube([0, ApexY - 0.08, 0], [0, ApexY + 0.07, 0], 0.056, 0.05, 14, SurfaceMaterialCode.DarkMetal);
  B.AddTube([0, BaseY + StandH * 0.25, 0], [0, BaseY + StandH, 0], TubeR * 1.4, TubeR * 1.32, 12, SurfaceMaterialCode.DarkMetal);
  B.AddTube([0, BaseY + StandH - 0.05, 0], [0, ColTopY, 0], TubeR * 1.05, TubeR * 1.02, 12, SurfaceMaterialCode.ChromeMetal);

  // Fluid pan-tilt mounting head & pan handle
  B.AddBox([0, ColTopY + 0.035, 0], [0.068, 0.038, 0.068], SurfaceMaterialCode.DarkMetal, HeadTilt, 0, 0);
  B.AddTube([0.05, ColTopY + 0.03, -0.02], [0.07, ColTopY - 0.09, -0.28], 0.008, 0.011, 8, SurfaceMaterialCode.DarkMetal);

  // Studio power / tether cable dropping cleanly along the rear of the mast and snaking across floor
  if (Params.PowerCable !== false) {
    const CablePts = [
      [0.035, ColTopY - 0.02, -0.04],
      [0.08, ColTopY * 0.72, -0.14],
      [0.05, ColTopY * 0.36, -0.12],
      [0.09, 0.06, -0.28],
      [0.24, 0.014, -0.58],
      [0.12, 0.014, -0.95],
      [-0.22, 0.014, -1.28],
    ];
    for (let I = 0; I < CablePts.length - 1; I++) {
      B.AddTube(CablePts[I], CablePts[I + 1], 0.01, 0.01, 6, SurfaceMaterialCode.RubberMat, false);
    }
    // Small inline ballast power brick resting on the floor behind wheeled stands
    if (HasCasters) {
      B.AddBox([0.24, 0.055, -0.58], [0.11, 0.055, 0.075], SurfaceMaterialCode.PelicanCase, 0, 0.3, 0);
    }
  }

  return B.Finish();
}

// 5. Procedural Cinema / Tether Camera Body (studio.cinema_camera)
function ConstructCinemaCamera(Params) {
  const B = CreateBuilder();
  const Style = Params.CameraStyle || "Cinema Matte Box";
  const LensLen = Number(Params.LensLength ?? 0.32);
  const ReelR = Number(Params.ReelRadius ?? 0.13);
  const HasMatte = Params.MatteBox !== false;
  const HasHandle = Params.TopHandle !== false;
  const Tilt = ((Number(Params.PitchDeg ?? 0)) * Math.PI) / 180;
  const CosT = Math.cos(Tilt);
  const SinT = Math.sin(Tilt);
  const PivotY = 0.12;

  // Pitch local point around tripod head pivot (positive Tilt aims lens +Z downward)
  const AimPt = (X, Y, Z) => {
    const Dy = Y - PivotY;
    return [
      X,
      PivotY + Dy * CosT - Z * SinT,
      Dy * SinT + Z * CosT,
    ];
  };

  // Base dovetail riser sitting directly on tripod mount plate
  B.AddBox(AimPt(0, 0.03, 0), [0.08, 0.024, 0.12], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);

  if (Style === "Cinema Matte Box") {
    // Classic twin-magazine motion-picture camera silhouette (Ref Image 1!)
    B.AddBox(AimPt(0, 0.17, -0.02), [0.11, 0.115, 0.18], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    // Twin film magazine reels mounted angled above body
    for (const [Rz, Ry] of [[-0.12, 0.36], [0.1, 0.35]]) {
      B.AddTube(AimPt(-0.055, Ry, Rz), AimPt(0.055, Ry, Rz), ReelR, ReelR, 20, SurfaceMaterialCode.DarkMetal);
      B.AddTube(AimPt(-0.062, Ry, Rz), AimPt(0.062, Ry, Rz), ReelR * 0.34, ReelR * 0.34, 14, SurfaceMaterialCode.ChromeMetal);
    }
    // Side follow-focus wheel + viewfinder
    B.AddTube(AimPt(-0.115, 0.16, 0.04), AimPt(-0.155, 0.16, 0.04), 0.045, 0.045, 14, SurfaceMaterialCode.RubberMat);
    B.AddTube(AimPt(-0.07, 0.21, -0.18), AimPt(-0.09, 0.22, -0.32), 0.026, 0.036, 12, SurfaceMaterialCode.RubberMat);
  } else if (Style === "Medium Format Tether") {
    B.AddBox(AimPt(0, 0.15, -0.01), [0.095, 0.095, 0.11], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    B.AddBox(AimPt(0, 0.26, -0.04), [0.065, 0.035, 0.075], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    B.AddBox(AimPt(0, 0.15, -0.125), [0.065, 0.045, 0.008], SurfaceMaterialCode.TetherScreen, Tilt, 0, 0);
  } else {
    B.AddBox(AimPt(0, 0.12, 0), [0.075, 0.065, 0.055], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
  }

  // Cinema prime lens barrel + focus gear rings + front glass optic
  const LensStartZ = 0.14;
  const LensEndZ = LensStartZ + LensLen;
  B.AddTube(AimPt(0, 0.16, LensStartZ), AimPt(0, 0.16, LensEndZ), 0.058, 0.064, 18, SurfaceMaterialCode.DarkMetal);
  B.AddTube(AimPt(0, 0.16, LensStartZ + LensLen * 0.35), AimPt(0, 0.16, LensStartZ + LensLen * 0.52), 0.069, 0.069, 18, SurfaceMaterialCode.RubberMat);
  B.AddTube(AimPt(0, 0.16, LensEndZ - 0.012), AimPt(0, 0.16, LensEndZ + 0.008), 0.052, 0.052, 16, SurfaceMaterialCode.CarGlass);

  // Flared cinema matte box & French flag
  if (HasMatte) {
    const Mz = LensEndZ + 0.045;
    B.AddBox(AimPt(0, 0.16, Mz), [0.13, 0.095, 0.045], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    B.AddBox(AimPt(0, 0.265, Mz + 0.055), [0.135, 0.006, 0.065], SurfaceMaterialCode.DarkMetal, Tilt - 0.35, 0, 0);
  }

  if (HasHandle) {
    B.AddTube(AimPt(0, 0.29, -0.09), AimPt(0, 0.29, 0.1), 0.014, 0.014, 10, SurfaceMaterialCode.DarkMetal);
  }

  return B.Finish();
}

// 6. Procedural Studio Light Fixture / Modifier (studio.light_fixture)
function ConstructLightFixture(Params) {
  const B = CreateBuilder();
  const Style = Params.FixtureStyle || "Overhead Softbox";
  const Aw = Number(Params.ApertureWidth ?? 6.2);
  const Ah = Number(Params.ApertureHeight ?? 3.6);
  const Depth = Number(Params.HousingDepth ?? 0.28);
  const Tilt = ((Number(Params.YokeTiltDeg ?? 0)) * Math.PI) / 180;

  if (Style === "Overhead Softbox") {
    const Hw = Aw * 0.5, Hh = Ah * 0.5, Hd = Depth * 0.5;
    // Outer sleek matte-black aluminum light bank frame (4 perimeter rails + top cover)
    const Rim = 0.13;
    B.AddBox([0, Hd, 0], [Hw + Rim, 0.03, Hh + Rim], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0, Hh + Rim * 0.5], [Hw + Rim, Hd, Rim * 0.5], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0, -(Hh + Rim * 0.5)], [Hw + Rim, Hd, Rim * 0.5], SurfaceMaterialCode.DarkMetal);
    B.AddBox([Hw + Rim * 0.5, 0, 0], [Rim * 0.5, Hd, Hh], SurfaceMaterialCode.DarkMetal);
    B.AddBox([-(Hw + Rim * 0.5), 0, 0], [Rim * 0.5, Hd, Hh], SurfaceMaterialCode.DarkMetal);
    // Upper ballast power distribution box & DMX junction manifold
    B.AddBox([0, Hd + 0.075, 0], [Hw * 0.65, 0.055, Hh * 0.22], SurfaceMaterialCode.DarkMetal);
    B.AddBox([Hw * 0.34, Hd + 0.11, -Hh * 0.12], [0.22, 0.08, 0.16], SurfaceMaterialCode.PelicanCase);
    B.AddBox([-Hw * 0.34, Hd + 0.11, -Hh * 0.12], [0.18, 0.07, 0.14], SurfaceMaterialCode.PelicanCase);

    // Pure white glowing silk diffuser plane on the underside (-Y normal)
    const DiffY = -Hd + 0.018;
    const Dn = [0, -1, 0];
    B.PushTri([-Hw, DiffY, -Hh], [Hw, DiffY, -Hh], [Hw, DiffY, Hh], Dn, Dn, Dn, SurfaceMaterialCode.EmissiveDiffuser, 0, 1, 1);
    B.PushTri([-Hw, DiffY, -Hh], [Hw, DiffY, Hh], [-Hw, DiffY, Hh], Dn, Dn, Dn, SurfaceMaterialCode.EmissiveDiffuser, 0, 1, 0);

    // Strictly perpendicular straight vertical steel suspension hoist cables (ZERO angle!)
    // plus machined eye-bolts, ceiling truss shackles & vertical power/DMX umbilical cables.
    const Drop = Number(Params.SuspensionDrop ?? 2.88);
    if (Drop > 0.1) {
      const AnchorXs = [-Hw * 0.84, 0, Hw * 0.84];
      const AnchorZs = [-Hh * 0.78, Hh * 0.78];
      for (const Bx of AnchorXs) {
        for (const Bz of AnchorZs) {
          // Bottom shackle collar bolted onto softbox roof
          B.AddTube([Bx, Hd, Bz], [Bx, Hd + 0.09, Bz], 0.026, 0.02, 8, SurfaceMaterialCode.ChromeMetal);
          // Plumb straight vertical steel aircraft wire (strictly perpendicular!)
          B.AddTube([Bx, Hd + 0.08, Bz], [Bx, Hd + Drop, Bz], 0.012, 0.012, 8, SurfaceMaterialCode.DarkMetal);
          // Top turnbuckle / truss clamp bracket at ceiling
          B.AddBox([Bx, Hd + Drop - 0.05, Bz], [0.055, 0.055, 0.055], SurfaceMaterialCode.DarkMetal);
        }
      }

      // Twin insulated black power & DMX feeder wires dropping straight down with a natural coiled slack loop
      const UmbilicalX = Hw * 0.34;
      const UmbilicalZ = -Hh * 0.12;
      const CoilSteps = 22;
      let PrevPt = [UmbilicalX, Hd + Drop, UmbilicalZ];
      for (let S = 1; S <= CoilSteps; S++) {
        const T = S / CoilSteps;
        const Y = Hd + Drop * (1 - T) + 0.14 * T;
        const SpiralR = 0.045 * Math.sin(T * Math.PI);
        const Ang = T * Math.PI * 10;
        const NextPt = [
          UmbilicalX + Math.cos(Ang) * SpiralR,
          Y,
          UmbilicalZ + Math.sin(Ang) * SpiralR,
        ];
        B.AddTube(PrevPt, NextPt, 0.013, 0.013, 6, SurfaceMaterialCode.RubberMat, false);
        PrevPt = NextPt;
      }

      // Secondary straight vertical DMX signal cable on left ballast bank
      const DmxX = -Hw * 0.34;
      const DmxZ = -Hh * 0.12;
      B.AddTube([DmxX, Hd + 0.14, DmxZ], [DmxX, Hd + Drop, DmxZ], 0.011, 0.011, 6, SurfaceMaterialCode.RubberMat);

      // Rooftop wiring harness linking central ballast boxes to outer lamp modules
      for (const Sx of [-1, 1]) {
        B.AddTube(
          [Sx * Hw * 0.34, Hd + 0.09, -Hh * 0.12],
          [Sx * Hw * 0.78, Hd + 0.05, -Hh * 0.12],
          0.012,
          0.012,
          6,
          SurfaceMaterialCode.RubberMat,
        );
        B.AddTube(
          [Sx * Hw * 0.34, Hd + 0.09, -Hh * 0.12],
          [Sx * Hw * 0.34, Hd + 0.05, Hh * 0.65],
          0.012,
          0.012,
          6,
          SurfaceMaterialCode.RubberMat,
        );
      }
    }
    return B.Finish();
  }

  if (Style === "Barn-Door Fresnel") {
    const Radius = Math.max(0.14, Aw * 0.45);
    const Flare = ((Number(Params.BarnDoorFlare ?? 34)) * Math.PI) / 180;
    // U-yoke mount from tripod spigot
    B.AddTube([0, 0, 0], [0, 0.1, 0], 0.018, 0.018, 8, SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0.09, 0], [Radius + 0.06, 0.014, 0.02], SurfaceMaterialCode.DarkMetal);
    B.AddBox([-(Radius + 0.05), 0.24, 0], [0.014, 0.16, 0.022], SurfaceMaterialCode.DarkMetal);
    B.AddBox([Radius + 0.05, 0.24, 0], [0.014, 0.16, 0.022], SurfaceMaterialCode.DarkMetal);

    // Lamp barrel tilted by YokeTiltDeg
    const Cy = 0.28;
    const CosT = Math.cos(Tilt), SinT = Math.sin(Tilt);
    const Fwd = [0, -SinT, CosT];
    const Up = [0, CosT, SinT];
    const BackPt = [0, Cy - Fwd[1] * Depth * 0.55, -Fwd[2] * Depth * 0.55];
    const FrontPt = [0, Cy + Fwd[1] * Depth * 0.45, Fwd[2] * Depth * 0.45];
    B.AddTube(BackPt, FrontPt, Radius * 0.88, Radius, 18, SurfaceMaterialCode.DarkMetal);
    // Luminous Fresnel lens disc
    const LensPt = [0, FrontPt[1] + Fwd[1] * 0.018, FrontPt[2] + Fwd[2] * 0.018];
    B.AddTube(FrontPt, LensPt, Radius * 0.82, Radius * 0.82, 18, SurfaceMaterialCode.EmissiveDiffuser);

    // 4 articulated matte-black barn-door shutters
    const LeafLen = Radius * 1.15;
    // Top & bottom flaps
    for (const Sy of [-1, 1]) {
      const Center = [
        0,
        LensPt[1] + Up[1] * Sy * (Radius + LeafLen * 0.42 * Math.sin(Flare)) + Fwd[1] * LeafLen * 0.48 * Math.cos(Flare),
        LensPt[2] + Up[2] * Sy * (Radius + LeafLen * 0.42 * Math.sin(Flare)) + Fwd[2] * LeafLen * 0.48 * Math.cos(Flare),
      ];
      B.AddBox(Center, [Radius * 0.96, 0.008, LeafLen * 0.5], SurfaceMaterialCode.DarkMetal, Tilt - Sy * Flare, 0, 0);
    }
    // Left & right flaps
    for (const Sx of [-1, 1]) {
      const Center = [
        Sx * (Radius + LeafLen * 0.42 * Math.sin(Flare)),
        LensPt[1] + Fwd[1] * LeafLen * 0.48 * Math.cos(Flare),
        LensPt[2] + Fwd[2] * LeafLen * 0.48 * Math.cos(Flare),
      ];
      B.AddBox(Center, [0.008, Radius * 0.96, LeafLen * 0.5], SurfaceMaterialCode.DarkMetal, Tilt, Sx * Flare, 0);
    }
    return B.Finish();
  }

  if (Style === "Parabolic Umbrella") {
    const Ribs = Math.max(8, Math.min(20, Math.round(Number(Params.UmbrellaRibs ?? 16))));
    const R = Math.max(0.45, Aw * 0.5);
    const BowlDepth = Math.max(0.28, Depth);
    const Cy = 0.24;
    const CosT = Math.cos(Tilt), SinT = Math.sin(Tilt);
    const Fwd = [0, -SinT, CosT]; // Direction light shines (open mouth)
    const Up = [0, CosT, SinT];

    // Strobe head + chrome shaft pointing into the parabolic bowl
    B.AddBox([0, 0.09, 0], [0.055, 0.09, 0.08], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    const Tip = [0, Cy - Fwd[1] * (BowlDepth + 0.12), -Fwd[2] * (BowlDepth + 0.12)];
    const Mouth = [0, Cy + Fwd[1] * 0.14, Fwd[2] * 0.14];
    B.AddTube(Tip, Mouth, 0.009, 0.009, 8, SurfaceMaterialCode.ChromeMetal);
    // Glowing strobe flash tube facing back into silver dome
    B.AddSphere([0, Cy + Fwd[1] * 0.04, Fwd[2] * 0.04], 0.065, 10, 12, SurfaceMaterialCode.EmissiveDiffuser);

    // Build parabolic faceted umbrella canopy (black back, reflective silver inner + chrome ribs!)
    const Rings = 5;
    const RingPts = [];
    for (let Ri = 0; Ri <= Rings; Ri++) {
      const T = Ri / Rings;
      const Rad = Math.max(0.018, R * Math.sin(T * Math.PI * 0.5));
      const ZOff = -(1 - T * T) * BowlDepth;
      const Pts = [];
      for (let S = 0; S < Ribs; S++) {
        const A = (S / Ribs) * Math.PI * 2;
        const Cx = Math.cos(A) * Rad;
        const CyLocal = Math.sin(A) * Rad;
        Pts.push([
          Cx,
          Cy + Up[1] * CyLocal + Fwd[1] * ZOff,
          Up[2] * CyLocal + Fwd[2] * ZOff,
        ]);
      }
      RingPts.push(Pts);
    }

    for (let Ri = 0; Ri < Rings; Ri++) {
      for (let S = 0; S < Ribs; S++) {
        const S1 = (S + 1) % Ribs;
        const A = RingPts[Ri][S], Bpt = RingPts[Ri][S1];
        const C = RingPts[Ri + 1][S1], Dpt = RingPts[Ri + 1][S];
        // Outer black shell
        B.AddQuadFlat(A, Bpt, C, Dpt, SurfaceMaterialCode.DarkMetal);
        // Inner reflective parabolic silver lining
        B.AddQuadFlat(Dpt, C, Bpt, A, SurfaceMaterialCode.UmbrellaSilver);
      }
    }
    // Crisp chrome radial ribs visible on inside (iconic detail from Ref Image 1!)
    for (let S = 0; S < Ribs; S += 2) {
      B.AddTube(Tip, RingPts[Rings][S], 0.005, 0.004, 6, SurfaceMaterialCode.ChromeMetal, false);
    }
    return B.Finish();
  }

  if (Style === "Studio LED Panel") {
    const Hw = Math.max(0.25, Aw * 0.5);
    const Hh = Math.max(0.18, Ah * 0.5);
    const Cy = Hh + 0.14;
    // U-yoke mount
    B.AddTube([0, 0, 0], [0, 0.09, 0], 0.018, 0.018, 8, SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0.09, 0], [Hw + 0.06, 0.014, 0.02], SurfaceMaterialCode.DarkMetal);
    B.AddBox([-(Hw + 0.05), Cy * 0.58, 0], [0.015, Cy * 0.45, 0.02], SurfaceMaterialCode.DarkMetal);
    B.AddBox([Hw + 0.05, Cy * 0.58, 0], [0.015, Cy * 0.45, 0.02], SurfaceMaterialCode.DarkMetal);
    // Panel body & luminous front face
    B.AddBox([0, Cy, 0], [Hw, Hh, Depth * 0.45], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);
    const CosT = Math.cos(Tilt), SinT = Math.sin(Tilt);
    const FaceOff = Depth * 0.46;
    B.AddBox([0, Cy - SinT * FaceOff, CosT * FaceOff], [Hw * 0.92, Hh * 0.9, 0.008], SurfaceMaterialCode.EmissiveDiffuser, Tilt, 0, 0);
    return B.Finish();
  }

  // Octa Softbox
  const R = Math.max(0.32, Aw * 0.5);
  const Cy = 0.22;
  const CosT = Math.cos(Tilt), SinT = Math.sin(Tilt);
  const Fwd = [0, -SinT, CosT];
  const Up = [0, CosT, SinT];
  const Back = [0, Cy - Fwd[1] * Depth * 0.65, -Fwd[2] * Depth * 0.65];
  const Front = [0, Cy + Fwd[1] * Depth * 0.35, Fwd[2] * Depth * 0.35];
  B.AddBox([0, 0.08, 0], [0.06, 0.08, 0.09], SurfaceMaterialCode.DarkMetal, Tilt, 0, 0);

  const Sides = 8;
  const BackRing = [], FrontRing = [];
  for (let S = 0; S < Sides; S++) {
    const A = (S / Sides) * Math.PI * 2 + Math.PI / 8;
    const Ca = Math.cos(A), Sa = Math.sin(A);
    BackRing.push([Ca * 0.1, Back[1] + Up[1] * Sa * 0.1, Back[2] + Up[2] * Sa * 0.1]);
    FrontRing.push([Ca * R, Front[1] + Up[1] * Sa * R, Front[2] + Up[2] * Sa * R]);
  }
  for (let S = 0; S < Sides; S++) {
    const S1 = (S + 1) % Sides;
    B.AddQuadFlat(BackRing[S], BackRing[S1], FrontRing[S1], FrontRing[S], SurfaceMaterialCode.DarkMetal);
    B.PushTri(Front, FrontRing[S], FrontRing[S1], Fwd, Fwd, Fwd, SurfaceMaterialCode.EmissiveDiffuser);
  }
  return B.Finish();
}

// 7. Procedural Tether Workstation, Hairpin Desk, Laptop, Flight Cases & Coiled Cables (studio.workstation)
function ConstructWorkstation(Params) {
  const B = CreateBuilder();
  const Dw = Number(Params.DeskWidth ?? 1.48);
  const Dd = Number(Params.DeskDepth ?? 0.72);
  const Dh = Number(Params.DeskHeight ?? 0.78);
  const HasDesk = Params.HasDesk !== false;
  const HasCases = Params.FlightCases !== false;
  const HasCables = Params.CoiledCables !== false;

  if (HasDesk) {
    // Sleek matte-black tabletop (matching Ref Image 2 right foreground desk!)
    B.AddBox([0, Dh, 0], [Dw * 0.5, 0.024, Dd * 0.5], SurfaceMaterialCode.DarkMetal);

    // 4 twin-rod angled steel hairpin legs
    for (const Sx of [-1, 1]) {
      for (const Sz of [-1, 1]) {
        const Tx = Sx * (Dw * 0.44);
        const Tz = Sz * (Dd * 0.4);
        const Fx = Sx * (Dw * 0.49);
        const Fz = Sz * (Dd * 0.46);
        B.AddTube([Tx, Dh - 0.02, Tz], [Fx, 0.01, Fz], 0.0085, 0.0085, 8, SurfaceMaterialCode.DarkMetal);
        B.AddTube([Tx - Sx * 0.04, Dh - 0.02, Tz], [Fx, 0.01, Fz], 0.008, 0.008, 8, SurfaceMaterialCode.DarkMetal);
      }
    }

    // Open tether laptop / studio calibration workstation display
    B.AddBox([-0.12, Dh + 0.032, 0.02], [0.22, 0.009, 0.15], SurfaceMaterialCode.DarkMetal, 0, -0.18, 0);
    B.AddBox([-0.14, Dh + 0.15, -0.13], [0.22, 0.135, 0.008], SurfaceMaterialCode.DarkMetal, -0.22, -0.18, 0);
    B.AddBox([-0.135, Dh + 0.15, -0.12], [0.2, 0.118, 0.004], SurfaceMaterialCode.TetherScreen, -0.22, -0.18, 0);
    // Color checker / audio interface box
    B.AddBox([0.36, Dh + 0.045, 0.06], [0.11, 0.025, 0.08], SurfaceMaterialCode.PelicanCase, 0, 0.22, 0);
  }

  // Stacked Pelican rugged equipment flight cases
  if (HasCases) {
    const Cx = HasDesk ? Dw * 0.52 + 0.28 : 0.0;
    const Cz = HasDesk ? -0.12 : 0.0;
    B.AddBox([Cx, 0.18, Cz], [0.34, 0.18, 0.24], SurfaceMaterialCode.PelicanCase, 0, 0.15, 0);
    // Ribbed lid ridges
    for (let R = -2; R <= 2; R++) {
      B.AddBox([Cx + R * 0.11, 0.368, Cz], [0.018, 0.01, 0.22], SurfaceMaterialCode.DarkMetal, 0, 0.15, 0);
    }
    // Second standing equipment case / strobe power pack
    B.AddBox([Cx - 0.44, 0.26, Cz - 0.22], [0.16, 0.26, 0.2], SurfaceMaterialCode.PelicanCase, 0, -0.24, 0);
    B.AddTube([Cx - 0.44, 0.53, Cz - 0.28], [Cx - 0.44, 0.53, Cz - 0.16], 0.012, 0.012, 8, SurfaceMaterialCode.ChromeMetal);
  }

  // Coiled black studio power cables resting on floor (Ref Image 2!)
  if (HasCables) {
    const Loops = 22;
    const Cx = HasDesk ? -Dw * 0.48 : 0.42;
    const Cz = HasDesk ? 0.38 : 0.28;
    for (let I = 0; I < Loops; I++) {
      const A0 = (I / Loops) * Math.PI * 2;
      const A1 = ((I + 1) / Loops) * Math.PI * 2;
      const R0 = 0.26 + 0.03 * Math.sin(A0 * 2);
      const R1 = 0.26 + 0.03 * Math.sin(A1 * 2);
      B.AddTube(
        [Cx + Math.cos(A0) * R0, 0.014, Cz + Math.sin(A0) * R0],
        [Cx + Math.cos(A1) * R1, 0.014, Cz + Math.sin(A1) * R1],
        0.012,
        0.012,
        6,
        SurfaceMaterialCode.RubberMat,
        false,
      );
    }
    // Trailing floor cable tail
    B.AddTube([Cx - 0.26, 0.014, Cz], [Cx - 1.15, 0.014, Cz - 0.65], 0.011, 0.011, 6, SurfaceMaterialCode.RubberMat);
  }

  return B.Finish();
}

// Deterministic seeded PRNG for repeatable studio forest placement
function MakeSeededRng(Seed) {
  let State = (Seed >>> 0) || 1337;
  return () => {
    State = (Math.imul(State, 1664525) + 1013904223) >>> 0;
    return State / 4294967296;
  };
}

function OrganicNoise2(X, Z) {
  return (
    Math.sin(X * 0.85 + Z * 0.42) * 0.42 +
    Math.cos(X * 1.63 - Z * 1.17) * 0.31 +
    Math.sin(X * 3.1 + Z * 2.75) * 0.17 +
    Math.cos(X * 5.4 - Z * 4.9) * 0.1
  );
}

// Helper: faceted organic 3D granite boulder with deterministic displacement
function AddOrganicBoulder(B, Center, Radii, YawRad, Seed, Mat) {
  const [Cx, Cy, Cz] = Center;
  const [Rx, Ry, Rz] = Radii;
  const Rings = 8;
  const Segs = 12;
  const Cyw = Math.cos(YawRad), Syw = Math.sin(YawRad);

  const Pt = (I, J) => {
    const Phi = (I / Rings) * Math.PI;
    const Th = (J / Segs) * Math.PI * 2;
    const Sp = Math.sin(Phi), Cp = Math.cos(Phi);
    const Nx = Sp * Math.cos(Th);
    const Ny = Cp;
    const Nz = Sp * Math.sin(Th);
    const Facet =
      1.0 +
      0.19 * Math.sin(Nx * 3.7 + Seed) * Math.cos(Nz * 3.3 - Seed * 0.7) +
      0.11 * Math.cos(Ny * 5.2 + Nx * 4.1 + Seed * 1.3) +
      0.06 * Math.sin(Nz * 7.1 - Ny * 6.3 + Seed);
    // Flatten underside slightly so boulders seat firmly into moss/earth
    const FlatY = Ny < -0.25 ? Ny * 0.62 : Ny;
    const Lx = Nx * Rx * Facet;
    const Ly = FlatY * Ry * Facet;
    const Lz = Nz * Rz * Facet;
    return [
      Cx + Lx * Cyw + Lz * Syw,
      Math.max(0.0, Cy + Ly),
      Cz - Lx * Syw + Lz * Cyw,
    ];
  };

  for (let I = 0; I < Rings; I++) {
    for (let J = 0; J < Segs; J++) {
      const P00 = Pt(I, J);
      const P10 = Pt(I + 1, J);
      const P11 = Pt(I + 1, J + 1);
      const P01 = Pt(I, J + 1);
      if (I > 0) {
        const N1 = B.FlatNormal(P00, P10, P11);
        B.PushTri(P00, P10, P11, N1, N1, N1, Mat);
      }
      if (I < Rings - 1) {
        const N2 = B.FlatNormal(P00, P11, P01);
        B.PushTri(P00, P11, P01, N2, N2, N2, Mat);
      }
    }
  }
}

// Evaluate exact rock incline crest plane so Hero Vehicle tires sit flush on the outcrop!
export function EvaluateForestRockPlane(LocalX, LocalZ, Params = {}) {
  const CrestY = Number(Params.InclineHeight ?? 0.56);
  const InclineRad = (Number(Params.InclineDeg ?? 8.5) * Math.PI) / 180;
  const BankRad = (Number(Params.BankDeg ?? 3.8) * Math.PI) / 180;
  const YawRad = (Number(Params.VehicleYawDeg ?? -22) * Math.PI) / 180;
  const Along = LocalX * Math.cos(YawRad) - LocalZ * Math.sin(YawRad);
  const Across = LocalX * Math.sin(YawRad) + LocalZ * Math.cos(YawRad);
  return {
    Along,
    Across,
    PlaneY: CrestY + Along * Math.tan(InclineRad) + Across * Math.tan(BankRad),
  };
}

// 8. Procedural Mossy Granite Rock Incline Mound, Woodland Moss Blanket, River Cobbles & Stream (studio.rock_outcrop)
function ConstructRockOutcrop(Params) {
  const B = CreateBuilder();
  const IslandRadiusX = Number(Params.IslandWidth ?? 11.4) * 0.5;
  const IslandRadiusZ = Number(Params.IslandDepth ?? 9.2) * 0.5;
  const RockScale = Number(Params.RockScale ?? 1.0);
  const HasStream = Params.HasStream !== false;
  const GridX = 40;
  const GridZ = 34;

  // Height & material classifier for the sculpted moss/granite island
  const SampleTerrain = (Ix, Iz) => {
    const Fx = (Ix / GridX) * 2 - 1;
    const Fz = (Iz / GridZ) * 2 - 1;
    const Lx = Fx * IslandRadiusX;
    const Lz = Fz * IslandRadiusZ;
    const NormR = Math.hypot(Lx / IslandRadiusX, Lz / IslandRadiusZ);
    const EdgeNoise = 0.14 * OrganicNoise2(Lx * 0.55, Lz * 0.55);
    const Mask = Math.max(0, Math.min(1, 1 - Math.max(0, (NormR + EdgeNoise - 0.56) / 0.46)));
    const SmoothMask = Mask * Mask * (3 - 2 * Mask);

    const { Along, Across, PlaneY } = EvaluateForestRockPlane(Lx, Lz, Params);
    // Elliptical rock outcrop core directly beneath the Hero Vehicle
    const CoreDist = Math.hypot(Along / 2.65, Across / 1.68);
    const CoreWeight = Math.max(0, Math.min(1, 1 - Math.max(0, (CoreDist - 0.72) / 0.55)));
    const SmoothCore = CoreWeight * CoreWeight * (3 - 2 * CoreWeight);

    // Natural mossy woodland undulation surrounding the rock crest
    const MossHummock =
      (0.18 + 0.14 * Math.max(0, OrganicNoise2(Lx * 0.85, Lz * 0.85)) + 0.08 * Math.sin(Lx * 1.4) * Math.cos(Lz * 1.2)) *
      SmoothMask;

    // Shallow forest stream channel winding through front-left hollow (Ref Image 1 & 2)
    const StreamCenterX = -1.65 + Math.sin(Lz * 0.7) * 0.65;
    const StreamDist = Math.abs(Lx - StreamCenterX);
    const InStreamZone = Lz > 1.25 && Lz < IslandRadiusZ * 0.82 && StreamDist < 0.92;
    const StreamCarve = InStreamZone ? Math.cos((StreamDist / 0.92) * Math.PI * 0.5) * 0.16 : 0;

    let Y = MossHummock * (1 - SmoothCore) + (PlaneY - 0.015 + 0.025 * OrganicNoise2(Lx * 2.1, Lz * 2.1) * (1 - SmoothCore * 0.85)) * SmoothCore;
    Y = Math.max(0.0, Y * SmoothMask - StreamCarve);
    if (NormR > 0.96) Y = 0.0;

    // Granite outcrop rock near center crest & steep ledges; velvety moss elsewhere
    const IsRock = SmoothCore > 0.28 || (Y > 0.34 && OrganicNoise2(Lx * 1.6, Lz * 1.6) > -0.15);
    return { P: [Lx, Y, Lz], IsRock };
  };

  const Grid = [];
  for (let Iz = 0; Iz <= GridZ; Iz++) {
    const Row = [];
    for (let Ix = 0; Ix <= GridX; Ix++) Row.push(SampleTerrain(Ix, Iz));
    Grid.push(Row);
  }
  for (let Iz = 0; Iz < GridZ; Iz++) {
    for (let Ix = 0; Ix < GridX; Ix++) {
      const A = Grid[Iz][Ix];
      const Bv = Grid[Iz][Ix + 1];
      const C = Grid[Iz + 1][Ix + 1];
      const D = Grid[Iz + 1][Ix];
      if (A.P[1] <= 0.002 && Bv.P[1] <= 0.002 && C.P[1] <= 0.002 && D.P[1] <= 0.002) continue;
      const Mat = A.IsRock || C.IsRock ? SurfaceMaterialCode.MossyRock : SurfaceMaterialCode.ForestMoss;
      B.AddQuadFlat(A.P, D.P, C.P, Bv.P, Mat);
    }
  }

  // Sculpted rugged granite boulders buttressing the rock incline mound (Ref Image 2!)
  const Boulders = [
    // Prominent front-center & front-left rock face directly beneath the vehicle crest
    [0.25, 0.34, 1.55, 1.18, 0.42, 0.88, 0.3, 11],
    [-1.45, 0.3, 1.38, 0.95, 0.38, 0.82, -0.45, 23],
    [1.82, 0.32, 1.15, 0.96, 0.4, 0.78, 0.62, 37],
    // Rear & side granite outcrop ledges supporting the inclined ramp
    [-2.25, 0.36, -0.2, 1.12, 0.46, 0.96, 0.2, 41],
    [2.35, 0.42, -0.35, 1.18, 0.52, 0.92, -0.3, 53],
    [-1.35, 0.36, -1.65, 1.25, 0.48, 0.88, 0.55, 67],
    [1.15, 0.44, -1.72, 1.32, 0.54, 0.94, -0.22, 79],
    // Secondary mossy accent stones around the outer moss bank
    [-3.4, 0.18, 1.7, 0.62, 0.26, 0.52, 0.9, 91],
    [3.3, 0.2, 1.9, 0.68, 0.28, 0.56, -0.8, 107],
    [-2.95, 0.22, -1.85, 0.72, 0.32, 0.58, 0.4, 119],
    [3.1, 0.24, -1.6, 0.78, 0.34, 0.62, -0.5, 131],
  ];
  for (const [X, Y, Z, Rx, Ry, Rz, Yaw, Seed] of Boulders) {
    AddOrganicBoulder(B, [X, Y * RockScale, Z], [Rx * RockScale, Ry * RockScale, Rz * RockScale], Yaw, Seed, SurfaceMaterialCode.MossyRock);
  }

  // Lush mossy hummock cushions along the front & sides (soft ragged transition to concrete floor)
  const Rng = MakeSeededRng(4207);
  for (let M = 0; M < 32; M++) {
    const Ang = (M / 32) * Math.PI * 2 + (Rng() - 0.5) * 0.22;
    const DistX = (3.4 + Rng() * 2.15) * (IslandRadiusX / 5.7);
    const DistZ = (2.75 + Rng() * 1.75) * (IslandRadiusZ / 4.6);
    const Mx = Math.cos(Ang) * DistX;
    const Mz = Math.sin(Ang) * DistZ;
    AddOrganicBoulder(
      B,
      [Mx, 0.06 + Rng() * 0.07, Mz],
      [0.45 + Rng() * 0.52, 0.14 + Rng() * 0.14, 0.42 + Rng() * 0.48],
      Rng() * Math.PI,
      300 + M * 17,
      SurfaceMaterialCode.ForestMoss,
    );
  }

  // Smooth wet river pebbles & cobbles clustered along front moss bank & stream edge (Ref Image 2!)
  for (let P = 0; P < 58; P++) {
    const Side = P % 3 === 0 ? -1 : 1;
    const Px = -2.6 + Rng() * 5.6 + (P < 18 ? -1.1 : 0);
    const Pz = 2.35 + Rng() * 1.85;
    const Pr = 0.065 + Rng() * 0.11;
    AddOrganicBoulder(
      B,
      [Px, Pr * 0.45, Pz],
      [Pr * (1.1 + Rng() * 0.45), Pr * (0.48 + Rng() * 0.24), Pr * (0.95 + Rng() * 0.4) * Math.abs(Side)],
      Rng() * Math.PI,
      700 + P * 13,
      SurfaceMaterialCode.RiverPebble,
    );
  }

  // Shallow shimmering forest stream / woodland puddle surface (Ref Image 1 & 2)
  if (HasStream) {
    const StreamSegs = 12;
    const WaterY = 0.065;
    for (let S = 0; S < StreamSegs; S++) {
      const Z0 = 1.35 + (S / StreamSegs) * 2.55;
      const Z1 = 1.35 + ((S + 1) / StreamSegs) * 2.55;
      const Cx0 = -1.65 + Math.sin(Z0 * 0.7) * 0.65;
      const Cx1 = -1.65 + Math.sin(Z1 * 0.7) * 0.65;
      const W0 = 0.48 + 0.18 * Math.sin(S * 0.8);
      const W1 = 0.48 + 0.18 * Math.sin((S + 1) * 0.8);
      B.AddQuadFlat(
        [Cx0 - W0, WaterY, Z0],
        [Cx1 - W1, WaterY, Z1],
        [Cx1 + W1, WaterY, Z1],
        [Cx0 + W0, WaterY, Z0],
        SurfaceMaterialCode.ForestWater,
      );
    }
    // Small circular woodland basin pond at front center-right (Ref Image 2!)
    const PondSegs = 16;
    const PondC = [1.15, 0.055, 3.25];
    for (let S = 0; S < PondSegs; S++) {
      const A0 = (S / PondSegs) * Math.PI * 2;
      const A1 = ((S + 1) / PondSegs) * Math.PI * 2;
      const P0 = [PondC[0] + Math.cos(A0) * 0.62, PondC[1], PondC[2] + Math.sin(A0) * 0.46];
      const P1 = [PondC[0] + Math.cos(A1) * 0.62, PondC[1], PondC[2] + Math.sin(A1) * 0.46];
      B.PushTri(PondC, P1, P0, [0, 1, 0], [0, 1, 0], [0, 1, 0], SurfaceMaterialCode.ForestWater);
    }
  }

  return B.Finish();
}

// Helper: gnarled fluted organic tree trunk / branch segment chain
function AddGnarledBranch(B, Points, Radii, Sides, TwistSeed, Mat) {
  const Rings = [];
  for (let I = 0; I < Points.length; I++) {
    const P = Points[I];
    const Prev = Points[Math.max(0, I - 1)];
    const Next = Points[Math.min(Points.length - 1, I + 1)];
    let Tx = Next[0] - Prev[0], Ty = Next[1] - Prev[1], Tz = Next[2] - Prev[2];
    const Tl = Math.hypot(Tx, Ty, Tz) || 1;
    Tx /= Tl; Ty /= Tl; Tz /= Tl;
    const Ref = Math.abs(Ty) < 0.85 ? [0, 1, 0] : [1, 0, 0];
    let Rx = Ref[1] * Tz - Ref[2] * Ty;
    let Ry = Ref[2] * Tx - Ref[0] * Tz;
    let Rz = Ref[0] * Ty - Ref[1] * Tx;
    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= Rl; Ry /= Rl; Rz /= Rl;
    const Ux = Ty * Rz - Tz * Ry;
    const Uy = Tz * Rx - Tx * Rz;
    const Uz = Tx * Ry - Ty * Rx;

    const Ring = [];
    for (let S = 0; S < Sides; S++) {
      const A = (S / Sides) * Math.PI * 2 + I * 0.24;
      const Fluting = 1.0 + 0.16 * Math.sin(A * 3 + I * 0.9 + TwistSeed) + 0.08 * Math.cos(A * 5 - I * 1.4);
      const Rad = Radii[I] * Fluting;
      const Ca = Math.cos(A), Sa = Math.sin(A);
      Ring.push([
        P[0] + (Rx * Ca + Ux * Sa) * Rad,
        P[1] + (Ry * Ca + Uy * Sa) * Rad,
        P[2] + (Rz * Ca + Uz * Sa) * Rad,
      ]);
    }
    Rings.push(Ring);
  }
  for (let I = 0; I < Rings.length - 1; I++) {
    for (let S = 0; S < Sides; S++) {
      const S1 = (S + 1) % Sides;
      B.AddQuadFlat(Rings[I][S], Rings[I][S1], Rings[I + 1][S1], Rings[I + 1][S], Mat, I / (Rings.length - 1));
    }
  }
  // Cap both ends with jagged wood fracture face
  const StartC = Points[0], EndC = Points[Points.length - 1];
  for (let S = 0; S < Sides; S++) {
    const S1 = (S + 1) % Sides;
    const N0 = B.FlatNormal(StartC, Rings[0][S1], Rings[0][S]);
    B.PushTri(StartC, Rings[0][S1], Rings[0][S], N0, N0, N0, Mat);
    const N1 = B.FlatNormal(EndC, Rings[Rings.length - 1][S], Rings[Rings.length - 1][S1]);
    B.PushTri(EndC, Rings[Rings.length - 1][S], Rings[Rings.length - 1][S1], N1, N1, N1, Mat);
  }
}

// 9. Procedural Weathered Fallen Tree Trunk, Twisted Roots & Split Branches (studio.fallen_wood)
function ConstructFallenWood(Params) {
  const B = CreateBuilder();
  const Scale = Number(Params.LogScale ?? 1.0);
  const S = (Pts) => Pts.map(([X, Y, Z]) => [X * Scale, Y * Scale, Z * Scale]);
  const R = (Arr) => Arr.map((V) => V * Scale);

  // Main dramatic gnarled fallen log rising toward the root crown (Ref Image 1 foreground right!)
  AddGnarledBranch(
    B,
    S([
      [-2.15, 0.24, 0.85],
      [-1.35, 0.34, 0.56],
      [-0.45, 0.48, 0.22],
      [0.45, 0.64, -0.06],
      [1.22, 0.88, -0.28],
      [1.76, 1.18, -0.44],
    ]),
    R([0.16, 0.22, 0.28, 0.34, 0.42, 0.36]),
    10,
    1.2,
    SurfaceMaterialCode.FallenWood,
  );

  // Split twisted upper root-flare limbs arching off the right crown
  AddGnarledBranch(
    B,
    S([
      [1.28, 0.86, -0.28],
      [1.92, 0.96, 0.08],
      [2.52, 0.72, 0.34],
      [2.95, 0.22, 0.48],
    ]),
    R([0.2, 0.15, 0.11, 0.055]),
    8,
    3.4,
    SurfaceMaterialCode.FallenWood,
  );
  AddGnarledBranch(
    B,
    S([
      [1.48, 1.04, -0.34],
      [2.05, 1.38, -0.58],
      [2.65, 1.56, -0.78],
    ]),
    R([0.18, 0.12, 0.045]),
    8,
    5.1,
    SurfaceMaterialCode.FallenWood,
  );
  AddGnarledBranch(
    B,
    S([
      [0.72, 0.66, -0.14],
      [1.15, 0.44, 0.42],
      [1.62, 0.16, 0.78],
    ]),
    R([0.15, 0.11, 0.05]),
    8,
    7.8,
    SurfaceMaterialCode.FallenWood,
  );

  // Broken side snag protruding from mid-trunk
  AddGnarledBranch(
    B,
    S([
      [-0.42, 0.5, 0.22],
      [-0.68, 0.78, -0.18],
      [-0.86, 0.96, -0.42],
    ]),
    R([0.13, 0.085, 0.035]),
    7,
    2.6,
    SurfaceMaterialCode.FallenWood,
  );

  // Moss pads clinging along top ridges of the weathered trunk
  AddOrganicBoulder(B, [0.1 * Scale, 0.68 * Scale, 0.05 * Scale], [0.55 * Scale, 0.14 * Scale, 0.26 * Scale], -0.32, 91, SurfaceMaterialCode.ForestMoss);
  AddOrganicBoulder(B, [-1.05 * Scale, 0.45 * Scale, 0.42 * Scale], [0.48 * Scale, 0.12 * Scale, 0.22 * Scale], -0.32, 97, SurfaceMaterialCode.ForestMoss);

  return B.Finish();
}

// Helper: build one REAL 3D sculpted pinnate fern rosette (arching stems + V-folded leaflets, aUv = wind flex!)
function AddRealFernRosette(B, Center, FrondCount, FrondLength, Seed) {
  const [Cx, Cy, Cz] = Center;
  const Rng = MakeSeededRng(Seed);

  for (let F = 0; F < FrondCount; F++) {
    const GoldenAng = F * 2.39996 + Rng() * 0.28;
    const RingLayer = F < FrondCount * 0.35 ? 0 : 1; // inner upright fronds vs outer arching fronds
    const Len = FrondLength * (RingLayer === 0 ? 0.68 + Rng() * 0.22 : 0.88 + Rng() * 0.3);
    const LiftAngle = RingLayer === 0 ? 0.92 + Rng() * 0.25 : 0.46 + Rng() * 0.28;
    const Droop = RingLayer === 0 ? 0.32 : 0.58;
    const CosA = Math.cos(GoldenAng);
    const SinA = Math.sin(GoldenAng);
    const SideX = -SinA;
    const SideZ = CosA;

    const SpineSteps = 10;
    const Spine = [];
    for (let S = 0; S <= SpineSteps; S++) {
      const T = S / SpineSteps;
      const Horiz = Math.cos(LiftAngle) * Len * T + Math.sin(T * Math.PI * 0.5) * Len * 0.14;
      const Vert = Math.sin(LiftAngle) * Len * T - Droop * Len * T * T;
      Spine.push([
        Cx + CosA * Horiz,
        Math.max(0.02, Cy + Vert),
        Cz + SinA * Horiz,
      ]);
    }

    // Central arching rachis stem rib
    for (let S = 0; S < SpineSteps; S++) {
      const T0 = S / SpineSteps;
      const T1 = (S + 1) / SpineSteps;
      const W0 = 0.011 * (1 - T0 * 0.72);
      const W1 = 0.011 * (1 - T1 * 0.72);
      const P0 = Spine[S], P1 = Spine[S + 1];
      const L0 = [P0[0] - SideX * W0, P0[1], P0[2] - SideZ * W0];
      const R0 = [P0[0] + SideX * W0, P0[1], P0[2] + SideZ * W0];
      const L1 = [P1[0] - SideX * W1, P1[1], P1[2] - SideZ * W1];
      const R1 = [P1[0] + SideX * W1, P1[1], P1[2] + SideZ * W1];
      const N = B.FlatNormal(L0, R0, R1);
      B.PushTri(L0, R0, R1, N, N, N, SurfaceMaterialCode.FernFrond, T0, T0, T1);
      B.PushTri(L0, R1, L1, N, N, N, SurfaceMaterialCode.FernFrond, T0, T1, T1);
    }

    // Pairs of serrated 3D angled pinnate leaflets (pinnae) radiating along the frond
    for (let S = 1; S < SpineSteps; S++) {
      const T = S / SpineSteps;
      const Envelope = Math.sin(Math.pow(T, 0.72) * Math.PI);
      const PinnaLen = Len * 0.24 * Envelope;
      const PinnaHalfW = Math.max(0.018, (Len / SpineSteps) * 0.44);
      const Base = Spine[S];
      const Next = Spine[Math.min(SpineSteps, S + 1)];
      let FwdX = Next[0] - Base[0], FwdY = Next[1] - Base[1], FwdZ = Next[2] - Base[2];
      const Fl = Math.hypot(FwdX, FwdY, FwdZ) || 1;
      FwdX /= Fl; FwdY /= Fl; FwdZ /= Fl;

      for (const Sign of [-1, 1]) {
        // Angle each leaflet slightly forward & upward into a V-trough for realistic 3D light catch
        const Mid = [
          Base[0] + SideX * Sign * PinnaLen * 0.55 + FwdX * PinnaLen * 0.22,
          Base[1] + PinnaLen * 0.14 + FwdY * PinnaLen * 0.18,
          Base[2] + SideZ * Sign * PinnaLen * 0.55 + FwdZ * PinnaLen * 0.22,
        ];
        const Tip = [
          Base[0] + SideX * Sign * PinnaLen + FwdX * PinnaLen * 0.45,
          Base[1] + PinnaLen * 0.04 + FwdY * PinnaLen * 0.3,
          Base[2] + SideZ * Sign * PinnaLen + FwdZ * PinnaLen * 0.45,
        ];
        const BackBase = [
          Base[0] - FwdX * PinnaHalfW * 0.5,
          Base[1] - FwdY * PinnaHalfW * 0.5,
          Base[2] - FwdZ * PinnaHalfW * 0.5,
        ];
        const FrontBase = [
          Base[0] + FwdX * PinnaHalfW * 0.85,
          Base[1] + FwdY * PinnaHalfW * 0.85,
          Base[2] + FwdZ * PinnaHalfW * 0.85,
        ];
        const EdgeMid = [
          Mid[0] + FwdX * PinnaHalfW * 0.52,
          Mid[1] + FwdY * PinnaHalfW * 0.52 + 0.012,
          Mid[2] + FwdZ * PinnaHalfW * 0.52,
        ];
        const FlexBase = Math.max(0.08, T * 0.82);
        const FlexTip = Math.min(1.0, T * 0.92 + 0.14);
        const N1 = B.FlatNormal(BackBase, FrontBase, EdgeMid);
        B.PushTri(BackBase, FrontBase, EdgeMid, N1, N1, N1, SurfaceMaterialCode.FernFrond, FlexBase, FlexBase, (FlexBase + FlexTip) * 0.5);
        const N2 = B.FlatNormal(BackBase, EdgeMid, Tip);
        B.PushTri(BackBase, EdgeMid, Tip, N2, N2, N2, SurfaceMaterialCode.FernFrond, FlexBase, (FlexBase + FlexTip) * 0.5, FlexTip);
      }
    }
  }
}

// 10. Procedural Real 3D Ferns Collection arranged around rock outcrop & moss bank (studio.fern_cluster)
function ConstructFernCluster(Params) {
  const B = CreateBuilder();
  const FernScale = Number(Params.FernScale ?? 1.0);
  const Density = Math.max(4, Math.min(18, Math.round(Number(Params.FernCount ?? 13))));

  // Curated hero placements matching Reference Image 2 (lush foreground & flanking ferns)
  const Spots = [
    [-1.15, 0.16, 2.38, 11, 1.15, 101],
    [-2.52, 0.14, 1.95, 10, 1.02, 113],
    [2.28, 0.18, 2.12, 11, 1.1, 127],
    [-3.38, 0.12, 0.85, 10, 1.08, 139],
    [3.55, 0.15, 0.72, 10, 1.06, 151],
    [-2.85, 0.24, -1.45, 9, 1.12, 163],
    [2.95, 0.22, -1.35, 9, 1.08, 179],
    [-0.15, 0.22, 2.55, 9, 0.92, 191],
    [1.15, 0.24, 2.25, 9, 0.96, 211],
    [-3.75, 0.1, -0.45, 9, 0.98, 223],
    [3.92, 0.1, -0.35, 9, 1.04, 239],
    [-1.72, 0.26, -2.28, 8, 1.02, 251],
    [1.82, 0.24, -2.25, 8, 1.05, 263],
    [-4.1, 0.08, 1.75, 8, 0.86, 277],
    [4.18, 0.08, 1.68, 8, 0.88, 283],
    [0.25, 0.22, -2.55, 8, 0.95, 293],
    [-2.05, 0.18, 2.95, 8, 0.84, 307],
    [2.75, 0.14, 2.85, 8, 0.86, 313],
  ];

  for (let I = 0; I < Math.min(Density, Spots.length); I++) {
    const [X, Y, Z, Fronds, Len, Seed] = Spots[I];
    AddRealFernRosette(B, [X, Y, Z], Fronds, Len * FernScale, Seed);
  }
  return B.Finish();
}

// 11. Procedural Real 3D Grass Blades, Sedges, Woodland Leafy Shrubs & Bamboo Shoots (studio.grass_plants)
function ConstructGrassPlants(Params) {
  const B = CreateBuilder();
  const GrassScale = Number(Params.GrassScale ?? 1.0);
  const TuftCount = Math.max(18, Math.min(72, Math.round(Number(Params.TuftCount ?? 52))));
  const Rng = MakeSeededRng(8821);

  // Build individual curved tapered 3D grass tufts scattered naturally around the moss island & boulders
  for (let T = 0; T < TuftCount; T++) {
    // Ring distribution avoiding exact car center footprint while framing rocks & moss bank
    const Ang = (T / TuftCount) * Math.PI * 2 + (Rng() - 0.5) * 0.35;
    const RingR = 1.95 + Rng() * 2.95;
    const Gx = Math.cos(Ang) * RingR * 1.12;
    const Gz = Math.sin(Ang) * RingR * 0.84;
    const Gy = Math.max(0.06, 0.22 * Math.max(0, 1 - RingR / 5.4));

    const Blades = 6 + Math.floor(Rng() * 4);
    for (let K = 0; K < Blades; K++) {
      const Ba = Rng() * Math.PI * 2;
      const Bx = Gx + Math.cos(Ba) * (0.04 + Rng() * 0.16);
      const Bz = Gz + Math.sin(Ba) * (0.04 + Rng() * 0.16);
      const Bh = (0.34 + Rng() * 0.44) * GrassScale;
      const Lean = (0.14 + Rng() * 0.26) * GrassScale;
      const BaseW = 0.024 * GrassScale;
      const DirX = Math.cos(Ba), DirZ = Math.sin(Ba);
      const SideX = -DirZ, SideZ = DirX;

      // 3-segment curved blade with aUv = 0.0 at root -> 1.0 at tip for realistic wind sway!
      const P0 = [Bx, Gy, Bz];
      const P1 = [Bx + DirX * Lean * 0.35, Gy + Bh * 0.48, Bz + DirZ * Lean * 0.35];
      const P2 = [Bx + DirX * Lean * 0.85, Gy + Bh * 0.84, Bz + DirZ * Lean * 0.85];
      const P3 = [Bx + DirX * Lean * 1.32, Gy + Bh, Bz + DirZ * Lean * 1.32];

      const L0 = [P0[0] - SideX * BaseW, P0[1], P0[2] - SideZ * BaseW];
      const R0 = [P0[0] + SideX * BaseW, P0[1], P0[2] + SideZ * BaseW];
      const L1 = [P1[0] - SideX * BaseW * 0.72, P1[1], P1[2] - SideZ * BaseW * 0.72];
      const R1 = [P1[0] + SideX * BaseW * 0.72, P1[1], P1[2] + SideZ * BaseW * 0.72];
      const L2 = [P2[0] - SideX * BaseW * 0.38, P2[1], P2[2] - SideZ * BaseW * 0.38];
      const R2 = [P2[0] + SideX * BaseW * 0.38, P2[1], P2[2] + SideZ * BaseW * 0.38];

      const N0 = B.FlatNormal(L0, R0, R1);
      B.PushTri(L0, R0, R1, N0, N0, N0, SurfaceMaterialCode.GrassBlade, 0.0, 0.0, 0.42);
      B.PushTri(L0, R1, L1, N0, N0, N0, SurfaceMaterialCode.GrassBlade, 0.0, 0.42, 0.42);
      const N1 = B.FlatNormal(L1, R1, R2);
      B.PushTri(L1, R1, R2, N1, N1, N1, SurfaceMaterialCode.GrassBlade, 0.42, 0.42, 0.78);
      B.PushTri(L1, R2, L2, N1, N1, N1, SurfaceMaterialCode.GrassBlade, 0.42, 0.78, 0.78);
      const N2 = B.FlatNormal(L2, R2, P3);
      B.PushTri(L2, R2, P3, N2, N2, N2, SurfaceMaterialCode.GrassBlade, 0.78, 0.78, 1.0);
    }
  }

  // Branching woodland shrubs (left & right flanking shrubs from Reference Image 2!)
  const Shrubs = [
    [-3.05, 0.16, 1.52, 1.05, 501],
    [2.62, 0.18, 1.88, 1.12, 509],
    [-3.45, 0.14, -1.15, 0.95, 521],
    [3.55, 0.16, -1.05, 1.0, 541],
    [-1.95, 0.15, 2.55, 0.85, 557],
  ];
  for (const [Sx, Sy, Sz, Sc, Seed] of Shrubs) {
    const Sr = MakeSeededRng(Seed);
    const Stems = 7;
    for (let I = 0; I < Stems; I++) {
      const A = (I / Stems) * Math.PI * 2 + Sr() * 0.4;
      const StemH = (0.72 + Sr() * 0.48) * Sc * GrassScale;
      const Spread = (0.36 + Sr() * 0.34) * Sc;
      const Top = [Sx + Math.cos(A) * Spread, Sy + StemH, Sz + Math.sin(A) * Spread];
      const Mid = [Sx + Math.cos(A) * Spread * 0.45, Sy + StemH * 0.54, Sz + Math.sin(A) * Spread * 0.45];
      B.AddTube([Sx, Sy, Sz], Mid, 0.014, 0.01, 5, SurfaceMaterialCode.FallenWood, false);
      B.AddTube(Mid, Top, 0.01, 0.005, 5, SurfaceMaterialCode.FallenWood, false);

      // Cluster of 3D angled oval woodland leaves near stem middle & tip (wind-animated!)
      for (let L = 0; L < 5; L++) {
        const Tpos = 0.45 + (L / 5) * 0.55;
        const Base = [
          Sx + (Top[0] - Sx) * Tpos,
          Sy + (Top[1] - Sy) * Tpos,
          Sz + (Top[2] - Sz) * Tpos,
        ];
        const La = A + (L - 2) * 0.95 + Sr() * 0.4;
        const Llen = (0.14 + Sr() * 0.09) * Sc * GrassScale;
        const Lw = Llen * 0.42;
        const Dx = Math.cos(La), Dz = Math.sin(La);
        const Px = -Dz, Pz = Dx;
        const MidL = [Base[0] + Dx * Llen * 0.5 - Px * Lw, Base[1] + 0.035, Base[2] + Dz * Llen * 0.5 - Pz * Lw];
        const MidR = [Base[0] + Dx * Llen * 0.5 + Px * Lw, Base[1] + 0.035, Base[2] + Dz * Llen * 0.5 + Pz * Lw];
        const Tip = [Base[0] + Dx * Llen, Base[1] + 0.015, Base[2] + Dz * Llen];
        const Flex = Math.min(1.0, Tpos * 0.95);
        const N1 = B.FlatNormal(Base, MidR, Tip);
        B.PushTri(Base, MidR, Tip, N1, N1, N1, SurfaceMaterialCode.GrassBlade, Flex * 0.85, Flex, Math.min(1, Flex + 0.1));
        const N2 = B.FlatNormal(Base, Tip, MidL);
        B.PushTri(Base, Tip, MidL, N2, N2, N2, SurfaceMaterialCode.GrassBlade, Flex * 0.85, Math.min(1, Flex + 0.1), Flex);
      }
    }
  }

  // Pair of sculpted wild forest bamboo shoots emerging in foreground (straight from Reference Image 2!)
  const Shoots = [
    [0.52, 0.1, 2.52, 0.64, 0.068],
    [0.72, 0.1, 2.66, 0.48, 0.056],
  ];
  for (const [Bx, By, Bz, Bh, Br] of Shoots) {
    B.AddTube([Bx, By, Bz], [Bx + 0.02, By + Bh, Bz - 0.01], Br, 0.008, 8, SurfaceMaterialCode.FallenWood);
  }

  return B.Finish();
}

// 12. Procedural Backlit Forest LED Volume Backdrop Wall, Proscenium Frame & Angled Studio Flags (studio.forest_backdrop)
function ConstructForestBackdrop(Params) {
  const B = CreateBuilder();
  const ScreenW = Number(Params.ScreenWidth ?? 12.4);
  const ScreenH = Number(Params.ScreenHeight ?? 6.2);
  const FrameW = Number(Params.ProsceniumWidth ?? 20.5);
  const FrameH = Number(Params.WallHeight ?? 7.25);
  const HasFlags = Params.StudioFlags !== false;

  const HalfSw = ScreenW * 0.5;
  const HalfFw = FrameW * 0.5;
  const BottomY = 0.08;
  const TopScreenY = BottomY + ScreenH;

  // Backlit Forest LED Volume Screen (subdivided front plane with clean local coordinates for procedural forest shader)
  const SegX = 12, SegY = 8;
  for (let Iy = 0; Iy < SegY; Iy++) {
    const Y0 = BottomY + (Iy / SegY) * ScreenH;
    const Y1 = BottomY + ((Iy + 1) / SegY) * ScreenH;
    for (let Ix = 0; Ix < SegX; Ix++) {
      const X0 = -HalfSw + (Ix / SegX) * ScreenW;
      const X1 = -HalfSw + ((Ix + 1) / SegX) * ScreenW;
      B.PushTri(
        [X0, Y0, 0.02], [X1, Y0, 0.02], [X1, Y1, 0.02],
        [0, 0, 1], [0, 0, 1], [0, 0, 1],
        SurfaceMaterialCode.ForestBackdrop,
        Ix / SegX, (Ix + 1) / SegX, (Ix + 1) / SegX,
      );
      B.PushTri(
        [X0, Y0, 0.02], [X1, Y1, 0.02], [X0, Y1, 0.02],
        [0, 0, 1], [0, 0, 1], [0, 0, 1],
        SurfaceMaterialCode.ForestBackdrop,
        Ix / SegX, (Ix + 1) / SegX, Ix / SegX,
      );
    }
  }

  // Deep matte-black studio proscenium walls surrounding the glowing LED forest window (Ref Image 1 & 2!)
  const SideW = Math.max(1.2, (FrameW - ScreenW) * 0.5);
  B.AddBox([-(HalfSw + SideW * 0.5), FrameH * 0.5, -0.08], [SideW * 0.5, FrameH * 0.5, 0.16], SurfaceMaterialCode.BlackFlag);
  B.AddBox([HalfSw + SideW * 0.5, FrameH * 0.5, -0.08], [SideW * 0.5, FrameH * 0.5, 0.16], SurfaceMaterialCode.BlackFlag);
  const TopBandH = Math.max(0.4, FrameH - TopScreenY);
  B.AddBox([0, TopScreenY + TopBandH * 0.5, -0.08], [HalfSw, TopBandH * 0.5, 0.16], SurfaceMaterialCode.BlackFlag);
  // Rear light-tight backing cabinet & steel truss uprights behind LED wall
  B.AddBox([0, FrameH * 0.5, -0.26], [HalfFw, FrameH * 0.5, 0.06], SurfaceMaterialCode.DarkMetal);
  for (let Ix = -3; Ix <= 3; Ix++) {
    B.AddTube([Ix * 2.6, 0, -0.34], [Ix * 2.6, FrameH, -0.34], 0.04, 0.04, 8, SurfaceMaterialCode.DarkMetal);
  }

  // Side matte-black studio return walls framing the soundstage
  const WingDepth = 7.4;
  B.AddBox([-HalfFw + 0.12, FrameH * 0.5, WingDepth * 0.5], [0.14, FrameH * 0.5, WingDepth * 0.5], SurfaceMaterialCode.BlackFlag);
  B.AddBox([HalfFw - 0.12, FrameH * 0.5, WingDepth * 0.5], [0.14, FrameH * 0.5, WingDepth * 0.5], SurfaceMaterialCode.BlackFlag);

  // Two angled studio negative-fill bounce flags / V-flats flanking left & right (straight from Ref Image 2!)
  if (HasFlags) {
    for (const Side of [-1, 1]) {
      const Fx = Side * (HalfSw + 1.45);
      const Fz = 5.4;
      const Fy = 3.15;
      const FlagYaw = Side * -0.46;
      // Matte duvetyne black flag panel with aluminum perimeter tube frame
      B.AddBox([Fx, Fy, Fz], [0.03, 1.85, 1.55], SurfaceMaterialCode.BlackFlag, 0, FlagYaw, 0);
      B.AddBox([Fx, Fy + 1.86, Fz], [0.035, 0.028, 1.58], SurfaceMaterialCode.ChromeMetal, 0, FlagYaw, 0);
      B.AddBox([Fx, Fy - 1.86, Fz], [0.035, 0.028, 1.58], SurfaceMaterialCode.ChromeMetal, 0, FlagYaw, 0);
      // Support C-stand column & base legs
      B.AddTube([Fx, 0.04, Fz], [Fx, Fy - 1.82, Fz], 0.026, 0.022, 8, SurfaceMaterialCode.ChromeMetal);
      for (let L = 0; L < 3; L++) {
        const La = (L / 3) * Math.PI * 2 + 0.4;
        B.AddTube([Fx, 0.34, Fz], [Fx + Math.cos(La) * 0.52, 0.02, Fz + Math.sin(La) * 0.52], 0.015, 0.013, 6, SurfaceMaterialCode.DarkMetal);
      }
    }
  }

  return B.Finish();
}

// 13. Procedural Wheeled Studio Wind Fan (Spinning 5-Blade Propeller + Safety Cage + Live Wind Source!) (studio.studio_fan)
function ConstructStudioFan(Params) {
  const B = CreateBuilder();
  const StandH = Number(Params.StandHeight ?? 1.28);
  const CageR = Number(Params.FanRadius ?? 0.52);
  const Tilt = (Number(Params.TiltDeg ?? -4) * Math.PI) / 180;
  const HubY = StandH;
  const CosT = Math.cos(Tilt), SinT = Math.sin(Tilt);

  // Heavy wheeled steel rolling base + 4 caster legs
  B.AddBox([0, 0.12, 0], [0.11, 0.045, 0.11], SurfaceMaterialCode.DarkMetal);
  for (let L = 0; L < 4; L++) {
    const A = (L / 4) * Math.PI * 2 + Math.PI / 4;
    const Ex = Math.cos(A) * 0.44;
    const Ez = Math.sin(A) * 0.44;
    B.AddTube([0, 0.12, 0], [Ex, 0.075, Ez], 0.022, 0.018, 8, SurfaceMaterialCode.DarkMetal);
    B.AddTube([Ex, 0.075, Ez], [Ex, 0.025, Ez], 0.024, 0.024, 8, SurfaceMaterialCode.RubberMat);
  }
  // Vertical chrome telescoping riser + locking collar + U-yoke cradle
  B.AddTube([0, 0.14, 0], [0, HubY - CageR - 0.1, 0], 0.028, 0.025, 10, SurfaceMaterialCode.ChromeMetal);
  B.AddTube([0, HubY - CageR - 0.14, 0], [0, HubY - CageR - 0.05, 0], 0.036, 0.036, 10, SurfaceMaterialCode.DarkMetal);
  const YokeW = CageR + 0.09;
  B.AddBox([0, HubY - CageR - 0.08, 0], [YokeW, 0.02, 0.028], SurfaceMaterialCode.FanHousing);
  B.AddBox([-YokeW, HubY - (CageR + 0.08) * 0.5, 0], [0.022, (CageR + 0.08) * 0.52, 0.028], SurfaceMaterialCode.FanHousing);
  B.AddBox([YokeW, HubY - (CageR + 0.08) * 0.5, 0], [0.022, (CageR + 0.08) * 0.52, 0.028], SurfaceMaterialCode.FanHousing);
  // Side tilt locking handwheels
  B.AddTube([-YokeW - 0.05, HubY, 0], [-YokeW + 0.02, HubY, 0], 0.042, 0.042, 10, SurfaceMaterialCode.DarkMetal);
  B.AddTube([YokeW - 0.02, HubY, 0], [YokeW + 0.05, HubY, 0], 0.042, 0.042, 10, SurfaceMaterialCode.DarkMetal);

  // Transform helper relative to tilted fan drum center [0, HubY, 0] (+Z is blow direction!)
  const DrumPt = (Lx, Ly, Lz) => [
    Lx,
    HubY + Ly * CosT - Lz * SinT,
    Ly * SinT + Lz * CosT,
  ];

  // Cylindrical sheet-steel wind tunnel shroud drum
  const Segs = 24;
  const ShroudHalfD = 0.18;
  for (let S = 0; S < Segs; S++) {
    const A0 = (S / Segs) * Math.PI * 2;
    const A1 = ((S + 1) / Segs) * Math.PI * 2;
    const C0 = Math.cos(A0), S0 = Math.sin(A0);
    const C1 = Math.cos(A1), S1 = Math.sin(A1);
    const P0b = DrumPt(C0 * CageR, S0 * CageR, -ShroudHalfD);
    const P1b = DrumPt(C1 * CageR, S1 * CageR, -ShroudHalfD);
    const P0f = DrumPt(C0 * CageR, S0 * CageR, ShroudHalfD);
    const P1f = DrumPt(C1 * CageR, S1 * CageR, ShroudHalfD);
    B.AddQuadFlat(P0b, P1b, P1f, P0f, SurfaceMaterialCode.FanHousing);
    // Concentric chrome wire guard rings on front (+Z) & rear (-Z)
    for (const Zface of [-ShroudHalfD - 0.015, ShroudHalfD + 0.025]) {
      for (const RingScale of [0.38, 0.68, 0.96]) {
        B.AddTube(
          DrumPt(C0 * CageR * RingScale, S0 * CageR * RingScale, Zface),
          DrumPt(C1 * CageR * RingScale, S1 * CageR * RingScale, Zface),
          0.005, 0.005, 5, SurfaceMaterialCode.ChromeMetal, false,
        );
      }
    }
  }
  // Radial chrome grille spokes
  for (let R = 0; R < 8; R++) {
    const A = (R / 8) * Math.PI * 2;
    const Ca = Math.cos(A), Sa = Math.sin(A);
    B.AddTube(DrumPt(0, 0, ShroudHalfD + 0.028), DrumPt(Ca * CageR, Sa * CageR, ShroudHalfD + 0.022), 0.0055, 0.0055, 5, SurfaceMaterialCode.ChromeMetal, false);
    B.AddTube(DrumPt(0, 0, -ShroudHalfD - 0.018), DrumPt(Ca * CageR, Sa * CageR, -ShroudHalfD - 0.015), 0.0055, 0.0055, 5, SurfaceMaterialCode.ChromeMetal, false);
  }

  // Rear industrial electric drive motor & variable speed control box
  B.AddTube(DrumPt(0, 0, -ShroudHalfD - 0.24), DrumPt(0, 0, 0.04), 0.11, 0.1, 14, SurfaceMaterialCode.DarkMetal);
  B.AddBox(DrumPt(0, -0.15, -ShroudHalfD - 0.14), [0.075, 0.06, 0.065], SurfaceMaterialCode.PelicanCase, Tilt, 0, 0);

  // 5-blade pitched aluminum propeller (Mat = FanBlade -> spins live in GPU vertex shader around HubY!)
  // Store HubY in vUv so the vertex shader rotates around [0, HubY, 0] smoothly!
  const BladeCount = 5;
  for (let K = 0; K < BladeCount; K++) {
    const BaseAng = (K / BladeCount) * Math.PI * 2;
    const RInner = 0.07;
    const ROuter = CageR * 0.89;
    const Sweep = 0.44;
    const PitchZ = 0.055;
    const A0 = BaseAng - Sweep * 0.45;
    const A1 = BaseAng + Sweep * 0.55;
    const P00 = DrumPt(Math.cos(A0) * RInner, Math.sin(A0) * RInner, -PitchZ * 0.4);
    const P10 = DrumPt(Math.cos(A1) * RInner, Math.sin(A1) * RInner, PitchZ * 0.4);
    const P11 = DrumPt(Math.cos(A1 + 0.14) * ROuter, Math.sin(A1 + 0.14) * ROuter, PitchZ);
    const P01 = DrumPt(Math.cos(A0 + 0.08) * ROuter, Math.sin(A0 + 0.08) * ROuter, -PitchZ);
    const N = B.FlatNormal(P00, P10, P11);
    B.PushTri(P00, P10, P11, N, N, N, SurfaceMaterialCode.FanBlade, HubY, HubY, HubY);
    B.PushTri(P00, P11, P01, N, N, N, SurfaceMaterialCode.FanBlade, HubY, HubY, HubY);
  }
  // Center spinner nose cone
  B.AddTube(DrumPt(0, 0, -0.02), DrumPt(0, 0, 0.09), 0.068, 0.02, 12, SurfaceMaterialCode.ChromeMetal);

  // Trailing studio AC power cable running off the motor box down to the floor
  B.AddTube(DrumPt(0, -0.18, -ShroudHalfD - 0.14), [0.14, 0.55, -0.35], 0.011, 0.011, 6, SurfaceMaterialCode.RubberMat);
  B.AddTube([0.14, 0.55, -0.35], [0.22, 0.014, -0.68], 0.011, 0.011, 6, SurfaceMaterialCode.RubberMat);
  B.AddTube([0.22, 0.014, -0.68], [-0.42, 0.014, -1.25], 0.011, 0.011, 6, SurfaceMaterialCode.RubberMat);

  return B.Finish();
}

// 14. Lightweight emitter beacon for Atmospheric Dust & Sunbeam Motes (studio.dust_field)
function ConstructDustFieldMarker(Params) {
  const B = CreateBuilder();
  const R = Number(Params.EmitterRadius ?? 0.18);
  B.AddSphere([0, R, 0], R * 0.55, 10, 14, SurfaceMaterialCode.EmissiveDiffuser);
  return B.Finish();
}

// 15. Procedural Racing Stadium Pit Garage Shell, Steel Bar Shutters, Mezzanine Catwalks & Linear LEDs (studio.garage_shell)
function ConstructGarageShell(Params) {
  const B = CreateBuilder();
  const W = Number(Params.GarageWidth ?? 22.0);
  const D = Number(Params.GarageDepth ?? 18.0);
  const H = Number(Params.GarageHeight ?? 7.15);
  const ShowMezzanine = Params.ShowMezzanine !== false;
  const ShowLinearLeds = Params.ShowLinearLeds !== false;
  const HalfW = W * 0.5;
  const BackZ = -6.2;

  // --- A) Sunlit Racing Stadium Grandstand & Pit-Lane Vista Backdrop outside the open pit archway ---
  const VistaW = Number(Params.ScreenWidth ?? 18.4);
  const VistaH = Number(Params.ScreenHeight ?? 8.2);
  const VistaZ = BackZ - 3.6;
  const VistaCx = -3.8;
  const SegX = 10, SegY = 6;
  for (let Iy = 0; Iy < SegY; Iy++) {
    const Y0 = 0.08 + (Iy / SegY) * VistaH;
    const Y1 = 0.08 + ((Iy + 1) / SegY) * VistaH;
    for (let Ix = 0; Ix < SegX; Ix++) {
      const X0 = VistaCx - VistaW * 0.5 + (Ix / SegX) * VistaW;
      const X1 = VistaCx - VistaW * 0.5 + ((Ix + 1) / SegX) * VistaW;
      // Store local coords offset relative to VistaCx in U? Shader reads vLocalPos.xy directly,
      // so center the vista UV cleanly around VistaCx in shader via uBackdropCenter!
      B.PushTri(
        [X0, Y0, VistaZ], [X1, Y0, VistaZ], [X1, Y1, VistaZ],
        [0, 0, 1], [0, 0, 1], [0, 0, 1],
        SurfaceMaterialCode.ForestBackdrop,
        Ix / SegX, (Ix + 1) / SegX, (Ix + 1) / SegX,
      );
      B.PushTri(
        [X0, Y0, VistaZ], [X1, Y1, VistaZ], [X0, Y1, VistaZ],
        [0, 0, 1], [0, 0, 1], [0, 0, 1],
        SurfaceMaterialCode.ForestBackdrop,
        Ix / SegX, (Ix + 1) / SegX, Ix / SegX,
      );
    }
  }
  // Sunlit outdoor pit-lane concrete apron extending from garage lip out toward stadium vista
  B.AddBox([VistaCx, -0.02, BackZ - 1.85], [VistaW * 0.52, 0.02, 1.9], SurfaceMaterialCode.ConcreteGarage);

  // --- B) Back Pit Wall: Open Stadium Bay Archway on Left + Steel Bar Security Cages on Right (Ref Image 1!) ---
  // Left corner structural pier & mid divider concrete column framing the wide open stadium pit portal
  B.AddBox([-HalfW + 0.55, H * 0.5, BackZ], [0.55, H * 0.5, 0.55], SurfaceMaterialCode.ConcreteGarage);
  B.AddBox([1.15, H * 0.5, BackZ], [0.46, H * 0.5, 0.55], SurfaceMaterialCode.ConcreteGarage);
  B.AddBox([6.2, H * 0.5, BackZ], [0.42, H * 0.5, 0.55], SurfaceMaterialCode.ConcreteGarage);
  B.AddBox([HalfW - 0.55, H * 0.5, BackZ], [0.55, H * 0.5, 0.55], SurfaceMaterialCode.ConcreteGarage);

  // Overhead concrete lintel header & rolled-up corrugated steel shutter above the open pit-bay archway
  const ArchTop = 4.95;
  B.AddBox([-4.9, ArchTop + (H - ArchTop) * 0.5, BackZ], [6.2, (H - ArchTop) * 0.5, 0.42], SurfaceMaterialCode.ConcreteGarage);
  B.AddTube([-10.2, ArchTop - 0.22, BackZ + 0.28], [0.7, ArchTop - 0.22, BackZ + 0.28], 0.22, 0.22, 12, SurfaceMaterialCode.FanHousing);
  // Subtle bay-number red accent placard ("07" look from Ref 1!) above pit archway
  B.AddBox([-4.8, ArchTop + 0.36, BackZ + 0.44], [0.42, 0.22, 0.03], SurfaceMaterialCode.CherryRedTool);

  // Right-side two-tier Galvanized Steel Vertical-Bar Security Shutters (signature look of Ref Image 1!)
  const AddSecurityCageBay = (XMin, XMax, ZPos) => {
    const Cx = (XMin + XMax) * 0.5;
    const Span = XMax - XMin;
    // Mid horizontal concrete/steel divider beam
    B.AddBox([Cx, 3.35, ZPos], [Span * 0.5, 0.26, 0.34], SurfaceMaterialCode.ConcreteGarage);
    B.AddBox([Cx, H - 0.32, ZPos], [Span * 0.5, 0.32, 0.34], SurfaceMaterialCode.ConcreteGarage);
    // Dark recessed neighbor pit bay behind the bars
    B.AddBox([Cx, H * 0.5, ZPos - 1.35], [Span * 0.52, H * 0.5, 0.12], SurfaceMaterialCode.BlackFlag);
    // Crisp vertical galvanized steel security cage bars (lower & upper bays)
    const BarCount = Math.max(10, Math.round(Span * 3.4));
    for (let I = 0; I <= BarCount; I++) {
      const Bx = XMin + 0.12 + (I / BarCount) * (Span - 0.24);
      B.AddTube([Bx, 0.08, ZPos], [Bx, 3.08, ZPos], 0.015, 0.015, 6, SurfaceMaterialCode.ChromeMetal, false);
      B.AddTube([Bx, 3.62, ZPos], [Bx, H - 0.65, ZPos], 0.015, 0.015, 6, SurfaceMaterialCode.ChromeMetal, false);
    }
    // Horizontal locking bars across cages
    for (const Ry of [1.05, 2.08, 4.6, 5.65]) {
      B.AddBox([Cx, Ry, ZPos], [Span * 0.49, 0.035, 0.04], SurfaceMaterialCode.DarkMetal);
    }
  };
  AddSecurityCageBay(1.62, 5.76, BackZ);
  AddSecurityCageBay(6.62, HalfW - 1.1, BackZ);

  // --- C) Left & Right Concrete/Steel Side Garage Walls & Pillars ---
  for (const Side of [-1, 1]) {
    const Wx = Side * HalfW;
    B.AddBox([Wx, H * 0.5, 1.5], [0.22, H * 0.5, D * 0.5], SurfaceMaterialCode.ConcreteGarage);
    // Structural steel H-columns lining both side walls
    for (let Z = BackZ + 2.6; Z <= BackZ + D - 2.2; Z += 4.2) {
      B.AddBox([Wx - Side * 0.36, H * 0.5, Z], [0.24, H * 0.5, 0.24], SurfaceMaterialCode.FanHousing);
    }
    // Subtle safety-yellow painted dado bumper strip along lower side walls
    B.AddBox([Wx - Side * 0.24, 1.12, 1.5], [0.03, 0.08, D * 0.48], SurfaceMaterialCode.SafetyYellow);
  }

  // --- D) Two-Story Industrial Mezzanine Catwalks & Yellow-Railed Staircases (Ref Image 3!) ---
  if (ShowMezzanine) {
    for (const Side of [-1, 1]) {
      const CatX = Side * (HalfW - 1.45);
      const DeckY = 3.35;
      // Steel catwalk deck & structural edge I-beam
      B.AddBox([CatX, DeckY, 2.4], [1.25, 0.1, 6.2], SurfaceMaterialCode.FanHousing);
      B.AddBox([CatX - Side * 1.24, DeckY + 0.06, 2.4], [0.05, 0.14, 6.2], SurfaceMaterialCode.SafetyYellow);
      // Support columns beneath catwalk
      for (const Zp of [-3.2, 2.4, 8.0]) {
        B.AddBox([CatX - Side * 1.15, DeckY * 0.5, Zp], [0.1, DeckY * 0.5, 0.1], SurfaceMaterialCode.DarkMetal);
      }
      // Yellow safety top & mid railings along inner edge of mezzanine
      const RailX = CatX - Side * 1.2;
      B.AddTube([RailX, DeckY + 1.02, -3.7], [RailX, DeckY + 1.02, 8.5], 0.026, 0.026, 6, SurfaceMaterialCode.SafetyYellow);
      B.AddTube([RailX, DeckY + 0.54, -3.7], [RailX, DeckY + 0.54, 8.5], 0.018, 0.018, 6, SurfaceMaterialCode.SafetyYellow);
      for (let Zp = -3.6; Zp <= 8.4; Zp += 1.5) {
        B.AddTube([RailX, DeckY + 0.05, Zp], [RailX, DeckY + 1.04, Zp], 0.02, 0.02, 6, SurfaceMaterialCode.DarkMetal);
      }
      // Angled steel staircase descending from mezzanine toward front bay
      const Steps = 11;
      for (let S = 0; S < Steps; S++) {
        const T = S / (Steps - 1);
        const Sy = 0.25 + T * (DeckY - 0.25);
        const Sz = 9.8 - T * 3.1;
        B.AddBox([CatX - Side * 0.45, Sy, Sz], [0.58, 0.035, 0.14], SurfaceMaterialCode.DarkMetal);
      }
      B.AddTube([CatX - Side * 1.02, 0.95, 9.9], [CatX - Side * 1.02, DeckY + 1.02, 6.7], 0.024, 0.024, 6, SurfaceMaterialCode.SafetyYellow);
      B.AddTube([CatX + Side * 0.12, 0.95, 9.9], [CatX + Side * 0.12, DeckY + 1.02, 6.7], 0.024, 0.024, 6, SurfaceMaterialCode.SafetyYellow);
    }
  }

  // --- E) Industrial Steel Roof Deck, Transverse I-Beam Trusses, Chain Hoist & Overhead Linear LEDs (Ref 2 & 3!) ---
  B.AddBox([0, H, 1.5], [HalfW + 0.3, 0.12, D * 0.5 + 0.4], SurfaceMaterialCode.CeilingRib);
  for (let Z = BackZ + 0.4; Z <= BackZ + D - 0.5; Z += 3.1) {
    // Deep box roof truss chord + web braces
    B.AddBox([0, H - 0.34, Z], [HalfW - 0.1, 0.09, 0.12], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, H - 0.78, Z], [HalfW - 0.1, 0.07, 0.1], SurfaceMaterialCode.DarkMetal);
    for (let X = -HalfW + 1.6; X <= HalfW - 1.6; X += 2.2) {
      B.AddTube([X, H - 0.34, Z], [X + 1.1, H - 0.78, Z], 0.022, 0.022, 5, SurfaceMaterialCode.FanHousing);
    }
  }

  // Overhead central hoist trolley rail + hanging chain loops (Ref Image 2!)
  B.AddBox([-0.6, H - 0.92, 0.8], [0.16, 0.16, 13.5], SurfaceMaterialCode.SafetyYellow);
  B.AddBox([-0.6, H - 1.24, -0.8], [0.34, 0.24, 0.48], SurfaceMaterialCode.FanHousing);
  B.AddTube([-0.6, H - 1.48, -0.8], [-0.6, H - 2.35, -0.8], 0.018, 0.018, 6, SurfaceMaterialCode.ChromeMetal);
  // Hanging overhead festoon power cable arcs along roof hoist
  for (let Loop = 0; Loop < 4; Loop++) {
    const Z0 = -3.8 + Loop * 1.45;
    const Z1 = Z0 + 1.45;
    const Zm = (Z0 + Z1) * 0.5;
    B.AddTube([-0.38, H - 1.05, Z0], [-0.38, H - 1.85, Zm], 0.016, 0.016, 6, SurfaceMaterialCode.RubberMat);
    B.AddTube([-0.38, H - 1.85, Zm], [-0.38, H - 1.05, Z1], 0.016, 0.016, 6, SurfaceMaterialCode.RubberMat);
  }

  // Crisp rows of suspended Linear Industrial LED Tube Bars (Ref Image 3 signature lighting!)
  if (ShowLinearLeds) {
    const LedY = H - 1.02;
    const RowsZ = [-3.6, 0.4, 4.4];
    const ColsX = [-5.8, 0.0, 5.8];
    for (const Lz of RowsZ) {
      for (const Lx of ColsX) {
        const BarHalfLen = Lx === 0 ? 2.15 : 1.55;
        // Straight vertical suspension rods to roof truss
        B.AddTube([Lx - BarHalfLen * 0.75, LedY + 0.06, Lz], [Lx - BarHalfLen * 0.75, H - 0.1, Lz], 0.008, 0.008, 5, SurfaceMaterialCode.ChromeMetal);
        B.AddTube([Lx + BarHalfLen * 0.75, LedY + 0.06, Lz], [Lx + BarHalfLen * 0.75, H - 0.1, Lz], 0.008, 0.008, 5, SurfaceMaterialCode.ChromeMetal);
        // Aluminum housing channel + bright glowing LED diffuser tube
        B.AddBox([Lx, LedY + 0.04, Lz], [BarHalfLen, 0.04, 0.11], SurfaceMaterialCode.DarkMetal);
        B.AddBox([Lx, LedY - 0.012, Lz], [BarHalfLen * 0.96, 0.022, 0.085], SurfaceMaterialCode.EmissiveDiffuser);
      }
    }
  }

  return B.Finish();
}

// 16. Procedural Heavy 4-Post Hydraulic Mechanic Car Lift (supports real elevated car via LiftHeight!) (studio.car_lift)
function ConstructCarLift(Params) {
  const B = CreateBuilder();
  const LiftH = Math.max(0.15, Math.min(2.6, Number(Params.LiftHeight ?? 2.12)));
  const PostH = Number(Params.PostHeight ?? 3.85);
  const DeckLen = 4.75;
  const DeckW = 2.48;
  const PostX = DeckLen * 0.44;
  const PostZ = DeckW * 0.54;

  // Yellow-black hazard floor pad & 4 bolted heavy steel H-columns
  B.AddBox([0, 0.012, 0], [DeckLen * 0.56, 0.012, DeckW * 0.68], SurfaceMaterialCode.DarkMetal);
  B.AddBox([0, 0.016, PostZ + 0.18], [DeckLen * 0.56, 0.01, 0.08], SurfaceMaterialCode.SafetyYellow);
  B.AddBox([0, 0.016, -(PostZ + 0.18)], [DeckLen * 0.56, 0.01, 0.08], SurfaceMaterialCode.SafetyYellow);

  for (const Sx of [-1, 1]) {
    for (const Sz of [-1, 1]) {
      const Px = Sx * PostX;
      const Pz = Sz * PostZ;
      // Heavy anchor base plate & steel vertical H-column
      B.AddBox([Px, 0.05, Pz], [0.22, 0.05, 0.22], SurfaceMaterialCode.SafetyYellow);
      B.AddBox([Px, PostH * 0.5, Pz], [0.11, PostH * 0.5, 0.12], SurfaceMaterialCode.FanHousing);
      // Chrome hydraulic lift piston rod running beside column
      B.AddTube([Px - Sx * 0.11, 0.12, Pz], [Px - Sx * 0.11, LiftH + 0.12, Pz], 0.036, 0.036, 10, SurfaceMaterialCode.ChromeMetal);
      // Heavy lift carriage slider hugging column at LiftHeight
      B.AddBox([Px, LiftH - 0.06, Pz], [0.16, 0.22, 0.17], SurfaceMaterialCode.SafetyYellow);
    }
    // Overhead top gantry cross-beam connecting front/back columns
    B.AddBox([Sx * PostX, PostH - 0.1, 0], [0.1, 0.12, PostZ + 0.12], SurfaceMaterialCode.FanHousing);
    // Cross lifting bridge beam under the twin decks at LiftHeight
    B.AddBox([Sx * PostX, LiftH - 0.12, 0], [0.12, 0.11, PostZ + 0.08], SurfaceMaterialCode.SafetyYellow);
  }

  // Hydraulic power pump unit mounted on front-right column
  B.AddBox([PostX + 0.22, 1.35, PostZ], [0.14, 0.28, 0.14], SurfaceMaterialCode.CherryRedTool);
  B.AddTube([PostX + 0.22, 1.65, PostZ], [PostX + 0.22, 2.02, PostZ], 0.08, 0.08, 10, SurfaceMaterialCode.DarkMetal);

  // Twin steel drive-on car runway tracks sitting flush at LiftH (car tires rest cleanly on Y = LiftH!)
  const TrackZ = 0.78;
  for (const Sz of [-1, 1]) {
    B.AddBox([0, LiftH - 0.05, Sz * TrackZ], [DeckLen * 0.5, 0.05, 0.32], SurfaceMaterialCode.FanHousing);
    // Safety-yellow raised outer wheel-guide rails
    B.AddBox([0, LiftH + 0.025, Sz * (TrackZ + 0.31)], [DeckLen * 0.49, 0.032, 0.025], SurfaceMaterialCode.SafetyYellow);
    // Angled hinged drive-on approach ramps at rear end (-X)
    B.AddBox([-DeckLen * 0.5 - 0.34, LiftH - 0.18, Sz * TrackZ], [0.38, 0.035, 0.29], SurfaceMaterialCode.SafetyYellow, 0, 0, 0.38);
    // Front wheel stop chocks (+X)
    B.AddBox([DeckLen * 0.47, LiftH + 0.06, Sz * TrackZ], [0.06, 0.06, 0.26], SurfaceMaterialCode.CherryRedTool);
  }
  // Center rolling jack bridge tray between runways
  B.AddBox([0.35, LiftH - 0.08, 0], [0.42, 0.04, TrackZ - 0.28], SurfaceMaterialCode.CherryRedTool);

  return B.Finish();
}

// 17. Procedural Mechanic's Rolling Tool Cart, Cherry-Red Toolboxes, Tire Rack & Floor Jack (studio.garage_props)
function ConstructGarageProps(Params) {
  const B = CreateBuilder();
  const Variant = Params.PropVariant || "tool-cart";

  if (Variant === "tool-cart") {
    // Rolling stainless mechanic's service trolley with stacked cherry-red Snap-on toolboxes (Ref Image 2 right!)
    const W = 1.55, D = 0.72, H = 0.94;
    // Lower & upper brushed steel cart shelves
    B.AddBox([0, 0.2, 0], [W * 0.5, 0.025, D * 0.5], SurfaceMaterialCode.ChromeMetal);
    B.AddBox([0, H, 0], [W * 0.5, 0.028, D * 0.5], SurfaceMaterialCode.ChromeMetal);
    // 4 vertical chrome corner posts + 4 caster wheels + side push handle
    for (const Sx of [-1, 1]) {
      for (const Sz of [-1, 1]) {
        const Cx = Sx * (W * 0.47), Cz = Sz * (D * 0.45);
        B.AddTube([Cx, 0.12, Cz], [Cx, H, Cz], 0.022, 0.022, 8, SurfaceMaterialCode.ChromeMetal);
        B.AddTube([Cx, 0.12, Cz], [Cx, 0.03, Cz], 0.048, 0.048, 10, SurfaceMaterialCode.RubberMat);
      }
    }
    B.AddTube([W * 0.54, H + 0.08, -D * 0.38], [W * 0.54, H + 0.08, D * 0.38], 0.018, 0.018, 8, SurfaceMaterialCode.ChromeMetal);

    // Hero stacked cherry-red multi-drawer mechanic's toolboxes resting atop trolley!
    B.AddBox([0.22, H + 0.28, 0], [0.48, 0.26, 0.31], SurfaceMaterialCode.CherryRedTool);
    for (let Dr = 0; Dr < 4; Dr++) {
      const Dy = H + 0.1 + Dr * 0.115;
      B.AddBox([0.22, Dy, 0.322], [0.42, 0.014, 0.014], SurfaceMaterialCode.ChromeMetal);
    }
    B.AddBox([-0.48, H + 0.16, 0.02], [0.24, 0.14, 0.26], SurfaceMaterialCode.CherryRedTool);
    B.AddBox([-0.48, H + 0.16, 0.288], [0.19, 0.014, 0.014], SurfaceMaterialCode.ChromeMetal);

    // Stacked spare matte rubber slick tires resting on lower trolley shelf (straight from Ref Image 2!)
    for (let Tr = 0; Tr < 2; Tr++) {
      B.AddTube([-0.24, 0.23 + Tr * 0.22, 0], [-0.24, 0.43 + Tr * 0.22, 0], 0.31, 0.31, 16, SurfaceMaterialCode.CarTire);
      B.AddTube([-0.24, 0.26 + Tr * 0.22, 0], [-0.24, 0.41 + Tr * 0.22, 0], 0.17, 0.17, 14, SurfaceMaterialCode.CarRim);
    }
    // Motor oil jerrycan sitting on floor beside tool cart
    B.AddBox([W * 0.5 + 0.28, 0.22, 0.08], [0.13, 0.22, 0.18], SurfaceMaterialCode.UmbrellaSilver, 0, -0.25, 0);
    B.AddBox([W * 0.5 + 0.28, 0.46, 0.02], [0.04, 0.04, 0.04], SurfaceMaterialCode.CherryRedTool);
    return B.Finish();
  }

  if (Variant === "tire-rack") {
    // Motorsport pit-garage twin-tier steel racing slick tire rack + hydraulic trolley jack & jack stands
    const Rw = 2.4, Rh = 1.95;
    for (const Sx of [-1, 1]) {
      for (const Sz of [-1, 1]) {
        B.AddTube([Sx * Rw * 0.5, 0, Sz * 0.34], [Sx * Rw * 0.5, Rh, Sz * 0.34], 0.026, 0.026, 8, SurfaceMaterialCode.FanHousing);
      }
    }
    for (const Ry of [0.55, 1.38]) {
      B.AddBox([0, Ry, -0.28], [Rw * 0.5, 0.035, 0.035], SurfaceMaterialCode.SafetyYellow);
      B.AddBox([0, Ry, 0.28], [Rw * 0.5, 0.035, 0.035], SurfaceMaterialCode.SafetyYellow);
      // 5 wide racing slick tires standing upright on each tier
      for (let T = -2; T <= 2; T++) {
        const Tx = T * 0.44;
        B.AddTube([Tx - 0.16, Ry + 0.34, 0], [Tx + 0.16, Ry + 0.34, 0], 0.34, 0.34, 16, SurfaceMaterialCode.CarTire);
        B.AddTube([Tx - 0.17, Ry + 0.34, 0], [Tx + 0.17, Ry + 0.34, 0], 0.18, 0.18, 12, SurfaceMaterialCode.CarRim);
      }
    }
    // Low-profile cherry-red hydraulic racing floor jack resting in front of rack
    B.AddBox([-0.45, 0.08, 0.92], [0.36, 0.06, 0.18], SurfaceMaterialCode.CherryRedTool, 0, 0.32, 0);
    B.AddTube([-0.75, 0.12, 1.02], [-1.35, 0.95, 1.25], 0.018, 0.016, 8, SurfaceMaterialCode.ChromeMetal);
    // Pair of safety-yellow jack stands
    for (const Jx of [0.42, 0.86]) {
      B.AddTube([Jx, 0.02, 0.88], [Jx, 0.36, 0.88], 0.14, 0.045, 4, SurfaceMaterialCode.SafetyYellow);
      B.AddBox([Jx, 0.38, 0.88], [0.06, 0.025, 0.04], SurfaceMaterialCode.DarkMetal);
    }
    return B.Finish();
  }

  // Tall cherry-red & brushed-steel rolling diagnostic cabinet & rear workshop shelving (Ref Image 2 mid-left!)
  B.AddBox([0, 0.88, 0], [0.58, 0.82, 0.42], SurfaceMaterialCode.CherryRedTool);
  B.AddBox([0, 1.32, 0.425], [0.48, 0.28, 0.02], SurfaceMaterialCode.TetherScreen);
  for (let Dr = 0; Dr < 4; Dr++) {
    B.AddBox([0, 0.24 + Dr * 0.19, 0.43], [0.48, 0.016, 0.016], SurfaceMaterialCode.ChromeMetal);
  }
  for (const Sx of [-0.46, 0.46]) {
    for (const Sz of [-0.32, 0.32]) {
      B.AddTube([Sx, 0.09, Sz], [Sx, 0.02, Sz], 0.045, 0.045, 8, SurfaceMaterialCode.RubberMat);
    }
  }
  return B.Finish();
}

// 18. Procedural Snaking Black & Orange Pneumatic Garage Air Hoses / Power Cables & Cable Ramp (studio.garage_cables)
function ConstructGarageCables(Params) {
  const B = CreateBuilder();
  const Span = Number(Params.CableSpan ?? 11.5);

  const AddSnakingCable = (Pts, Rad, Mat) => {
    for (let I = 0; I < Pts.length - 1; I++) {
      B.AddTube(Pts[I], Pts[I + 1], Rad, Rad, 6, Mat, false);
    }
  };

  // Bundle of snaking black rubber air/power cables trailing from left lift across center garage floor (Ref 2 & 3!)
  const Scale = Span / 11.5;
  AddSnakingCable(
    [
      [-5.6 * Scale, 0.022, -1.2],
      [-4.2 * Scale, 0.022, 0.6],
      [-2.8 * Scale, 0.022, 1.85],
      [-1.1 * Scale, 0.022, 2.55],
      [0.6 * Scale, 0.022, 3.25],
      [2.1 * Scale, 0.022, 4.15],
      [3.6 * Scale, 0.022, 4.95],
      [5.1 * Scale, 0.022, 5.45],
    ],
    0.024,
    SurfaceMaterialCode.RubberMat,
  );
  AddSnakingCable(
    [
      [-5.3 * Scale, 0.02, -0.8],
      [-3.8 * Scale, 0.02, 0.9],
      [-2.3 * Scale, 0.02, 2.1],
      [-0.7 * Scale, 0.02, 2.75],
      [0.85 * Scale, 0.02, 3.4],
      [2.5 * Scale, 0.02, 4.05],
      [4.2 * Scale, 0.02, 4.65],
    ],
    0.019,
    SurfaceMaterialCode.RubberMat,
  );
  // High-visibility safety-orange/yellow pneumatic compressor hose looping toward right tool cart (Ref Image 3!)
  AddSnakingCable(
    [
      [1.2 * Scale, 0.018, -3.4],
      [2.4 * Scale, 0.018, -1.6],
      [3.3 * Scale, 0.018, 0.45],
      [4.6 * Scale, 0.018, 2.15],
      [5.8 * Scale, 0.018, 3.45],
    ],
    0.018,
    SurfaceMaterialCode.SafetyYellow,
  );

  // Heavy-duty yellow & black ribbed rubber floor cable protector ramp across the foreground cable bundle (Ref Image 2!)
  B.AddBox([2.35 * Scale, 0.032, 4.25], [0.95, 0.032, 0.26], SurfaceMaterialCode.RubberMat, 0, -0.45, 0);
  B.AddBox([2.35 * Scale, 0.066, 4.25], [0.88, 0.01, 0.14], SurfaceMaterialCode.SafetyYellow, 0, -0.45, 0);

  return B.Finish();
}

// Classic Showcase level primitives so level switching renders full 3D PBR immediately!
export function ConstructShowcasePrimitive(Kind) {
  const B = CreateBuilder();
  if (Kind === "cube") {
    B.AddBox([0, 0.65, 0], [0.65, 0.65, 0.65], SurfaceMaterialCode.BodyPaint);
  } else if (Kind === "sphere") {
    B.AddSphere([0, 0.72, 0], 0.72, 18, 28, SurfaceMaterialCode.CarRim);
  } else if (Kind === "cylinder") {
    B.AddTube([0, 0, 0], [0, 1.45, 0], 0.55, 0.55, 28, SurfaceMaterialCode.BodyPaint);
  } else if (Kind === "cone") {
    B.AddTube([0, 0, 0], [0, 1.5, 0], 0.68, 0.01, 28, SurfaceMaterialCode.UmbrellaSilver);
  } else if (Kind === "torus") {
    const Major = 0.62, Minor = 0.22, SegU = 28, SegV = 16;
    for (let U = 0; U < SegU; U++) {
      const Th0 = (U / SegU) * Math.PI * 2;
      const Th1 = ((U + 1) / SegU) * Math.PI * 2;
      for (let V = 0; V < SegV; V++) {
        const Ph0 = (V / SegV) * Math.PI * 2;
        const Ph1 = ((V + 1) / SegV) * Math.PI * 2;
        const TorusPt = (Th, Ph) => {
          const R = Major + Minor * Math.cos(Ph);
          const P = [R * Math.cos(Th), 0.76 + Minor * Math.sin(Ph), R * Math.sin(Th)];
          const N = [Math.cos(Ph) * Math.cos(Th), Math.sin(Ph), Math.cos(Ph) * Math.sin(Th)];
          return [P, N];
        };
        const [P00, N00] = TorusPt(Th0, Ph0);
        const [P10, N10] = TorusPt(Th1, Ph0);
        const [P11, N11] = TorusPt(Th1, Ph1);
        const [P01, N01] = TorusPt(Th0, Ph1);
        B.PushTri(P00, P10, P11, N00, N10, N11, SurfaceMaterialCode.BodyPaint);
        B.PushTri(P00, P11, P01, N00, N11, N01, SurfaceMaterialCode.BodyPaint);
      }
    }
  }
  return B.Finish();
}

export function ConstructProceduralSurface(Kind, Params = {}) {
  switch (Kind) {
    case "studio.cyclorama":
      return ConstructCyclorama(Params);
    case "studio.tiled_floor":
      return ConstructTiledFloor(Params);
    case "studio.ceiling_truss":
      return ConstructCeilingTruss(Params);
    case "studio.tripod":
      return ConstructTripod(Params);
    case "studio.cinema_camera":
      return ConstructCinemaCamera(Params);
    case "studio.light_fixture":
      return ConstructLightFixture(Params);
    case "studio.workstation":
      return ConstructWorkstation(Params);
    case "studio.rock_outcrop":
      return ConstructRockOutcrop(Params);
    case "studio.fallen_wood":
      return ConstructFallenWood(Params);
    case "studio.fern_cluster":
      return ConstructFernCluster(Params);
    case "studio.grass_plants":
      return ConstructGrassPlants(Params);
    case "studio.forest_backdrop":
      return ConstructForestBackdrop(Params);
    case "studio.studio_fan":
      return ConstructStudioFan(Params);
    case "studio.dust_field":
      return ConstructDustFieldMarker(Params);
    case "studio.garage_shell":
      return ConstructGarageShell(Params);
    case "studio.car_lift":
      return ConstructCarLift(Params);
    case "studio.garage_props":
      return ConstructGarageProps(Params);
    case "studio.garage_cables":
      return ConstructGarageCables(Params);
    default:
      return ConstructShowcasePrimitive("cube");
  }
}

export function ProceduralSignature(Kind, Params = {}) {
  const Relevant = {};
  for (const [Key, Val] of Object.entries(Params)) {
    if (Key === "Position" || Key === "YawDeg" || Key === "SurfaceTint") continue;
    Relevant[Key] = Val;
  }
  return Kind + ":" + JSON.stringify(Relevant);
}
