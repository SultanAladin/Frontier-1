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

  // Steel lighting truss beams overhead
  if (Params.TrussGrid !== false) {
    for (const Tx of [-4.2, 0, 4.2]) {
      B.AddBox([Tx, H - 0.18, 0], [0.06, 0.06, D * 0.45], SurfaceMaterialCode.DarkMetal);
      B.AddBox([Tx, H - 0.44, 0], [0.05, 0.05, D * 0.45], SurfaceMaterialCode.DarkMetal);
      const Bays = 10;
      for (let J = 0; J < Bays; J++) {
        const Z0 = -D * 0.42 + (J / Bays) * D * 0.84;
        const Z1 = -D * 0.42 + ((J + 1) / Bays) * D * 0.84;
        B.AddTube([Tx, H - 0.18, Z0], [Tx, H - 0.44, Z1], 0.022, 0.022, 6, SurfaceMaterialCode.DarkMetal, false);
        B.AddTube([Tx, H - 0.44, Z0], [Tx, H - 0.18, Z1], 0.022, 0.022, 6, SurfaceMaterialCode.DarkMetal, false);
      }
    }
    for (const Tz of [-3.4, 0, 3.4]) {
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

  // Base dovetail riser sitting directly on tripod mount plate
  B.AddBox([0, 0.03, 0], [0.08, 0.024, 0.12], SurfaceMaterialCode.DarkMetal);

  if (Style === "Cinema Matte Box") {
    // Classic twin-magazine motion-picture camera silhouette (Ref Image 1!)
    B.AddBox([0, 0.17, -0.02], [0.11, 0.115, 0.18], SurfaceMaterialCode.DarkMetal);
    // Twin film magazine reels mounted angled above body
    for (const [Rz, Ry] of [[-0.12, 0.36], [0.1, 0.35]]) {
      B.AddTube([-0.055, Ry, Rz], [0.055, Ry, Rz], ReelR, ReelR, 20, SurfaceMaterialCode.DarkMetal);
      B.AddTube([-0.062, Ry, Rz], [0.062, Ry, Rz], ReelR * 0.34, ReelR * 0.34, 14, SurfaceMaterialCode.ChromeMetal);
    }
    // Side follow-focus wheel + viewfinder
    B.AddTube([-0.115, 0.16, 0.04], [-0.155, 0.16, 0.04], 0.045, 0.045, 14, SurfaceMaterialCode.RubberMat);
    B.AddTube([-0.07, 0.21, -0.18], [-0.09, 0.22, -0.32], 0.026, 0.036, 12, SurfaceMaterialCode.RubberMat);
  } else if (Style === "Medium Format Tether") {
    B.AddBox([0, 0.15, -0.01], [0.095, 0.095, 0.11], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0.26, -0.04], [0.065, 0.035, 0.075], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0.15, -0.125], [0.065, 0.045, 0.008], SurfaceMaterialCode.TetherScreen);
  } else {
    B.AddBox([0, 0.12, 0], [0.075, 0.065, 0.055], SurfaceMaterialCode.DarkMetal);
  }

  // Cinema prime lens barrel + focus gear rings + front glass optic
  const LensStartZ = 0.14;
  const LensEndZ = LensStartZ + LensLen;
  B.AddTube([0, 0.16, LensStartZ], [0, 0.16, LensEndZ], 0.058, 0.064, 18, SurfaceMaterialCode.DarkMetal);
  B.AddTube([0, 0.16, LensStartZ + LensLen * 0.35], [0, 0.16, LensStartZ + LensLen * 0.52], 0.069, 0.069, 18, SurfaceMaterialCode.RubberMat);
  B.AddTube([0, 0.16, LensEndZ - 0.012], [0, 0.16, LensEndZ + 0.008], 0.052, 0.052, 16, SurfaceMaterialCode.CarGlass);

  // Flared cinema matte box & French flag
  if (HasMatte) {
    const Mz = LensEndZ + 0.045;
    B.AddBox([0, 0.16, Mz], [0.13, 0.095, 0.045], SurfaceMaterialCode.DarkMetal);
    B.AddBox([0, 0.265, Mz + 0.055], [0.135, 0.006, 0.065], SurfaceMaterialCode.DarkMetal, -0.35, 0, 0);
  }

  if (HasHandle) {
    B.AddTube([0, 0.29, -0.09], [0, 0.29, 0.1], 0.014, 0.014, 10, SurfaceMaterialCode.DarkMetal);
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
    // Upper ballast housing bar
    B.AddBox([0, Hd + 0.075, 0], [Hw * 0.65, 0.055, Hh * 0.22], SurfaceMaterialCode.DarkMetal);

    // Pure white glowing silk diffuser plane on the underside (-Y normal)
    const DiffY = -Hd + 0.018;
    const Dn = [0, -1, 0];
    B.PushTri([-Hw, DiffY, -Hh], [Hw, DiffY, -Hh], [Hw, DiffY, Hh], Dn, Dn, Dn, SurfaceMaterialCode.EmissiveDiffuser, 0, 1, 1);
    B.PushTri([-Hw, DiffY, -Hh], [Hw, DiffY, Hh], [-Hw, DiffY, Hh], Dn, Dn, Dn, SurfaceMaterialCode.EmissiveDiffuser, 0, 1, 0);

    // Angled twin steel suspension rigging cables up to the ceiling truss (Ref Image 1!)
    const Drop = Number(Params.SuspensionDrop ?? 2.3);
    if (Drop > 0.1) {
      for (const Sx of [-1, 1]) {
        for (const Sz of [-1, 1]) {
          const Bx = Sx * (Hw * 0.78);
          const Bz = Sz * (Hh * 0.74);
          B.AddTube([Bx, Hd, Bz], [Bx + Sx * 0.35, Hd + Drop, Bz + Sz * 0.18], 0.016, 0.016, 8, SurfaceMaterialCode.DarkMetal);
          B.AddTube([Bx * 0.76, Hd, Bz], [Bx * 0.76 + Sx * 0.2, Hd + Drop, Bz], 0.012, 0.012, 8, SurfaceMaterialCode.DarkMetal);
        }
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
