//==========================================================================================================================================
//                                                    TRANSFORMGIZMO3D.JS
//==========================================================================================================================================
// Exact 1:1 WebGL2 port of Engine/Editor/Viewports/TransformGizmo.{h,cpp} and
// Engine/GeometricRaster/Shaders/GizmoRasterization.slang (Slate 13-handle GizmoPRO manipulator).

export const GizmoHandle = Object.freeze({
  None: 0,
  TranslateX: 1,
  TranslateY: 2,
  TranslateZ: 3,
  ScaleX: 4,
  ScaleY: 5,
  ScaleZ: 6,
  PlaneYZ: 7,
  PlaneXZ: 8,
  PlaneXY: 9,
  RotateX: 10,
  RotateY: 11,
  RotateZ: 12,
  CenterRing: 13,
});

export const GIZMO_HANDLE_LABELS = Object.freeze({
  [GizmoHandle.None]: "",
  [GizmoHandle.TranslateX]: "X Move",
  [GizmoHandle.TranslateY]: "Y Move",
  [GizmoHandle.TranslateZ]: "Z Move",
  [GizmoHandle.ScaleX]: "X Scale",
  [GizmoHandle.ScaleY]: "Y Scale",
  [GizmoHandle.ScaleZ]: "Z Scale",
  [GizmoHandle.PlaneYZ]: "YZ Plane Move",
  [GizmoHandle.PlaneXZ]: "XZ Plane Move",
  [GizmoHandle.PlaneXY]: "XY Plane Move",
  [GizmoHandle.RotateX]: "X Rotate",
  [GizmoHandle.RotateY]: "Y Rotate",
  [GizmoHandle.RotateZ]: "Z Rotate",
  [GizmoHandle.CenterRing]: "Screen Free Move",
});

// Exact C++ Slate GizmoPRO palette from TransformGizmo.cpp
const PALETTE = Object.freeze({
  AxisX: [0.878, 0.078, 0.078, 1.0],      // #E01414
  AxisY: [0.071, 0.831, 0.039, 1.0],      // #12D40A
  AxisZ: [0.082, 0.376, 0.878, 1.0],      // #1560E0
  PlaneYZ: [0.122, 0.780, 0.780, 0.28],   // #1FC7C7 @ 0.28 alpha
  EdgeYZ: [0.122, 0.780, 0.780, 1.0],
  PlaneXZ: [0.784, 0.118, 0.784, 0.28],   // #C81EC8 @ 0.28 alpha
  EdgeXZ: [0.784, 0.118, 0.784, 1.0],
  PlaneXY: [0.878, 0.804, 0.071, 0.28],   // #E0CD12 @ 0.28 alpha
  EdgeXY: [0.878, 0.804, 0.071, 1.0],
  CenterRing: [1.0, 1.0, 1.0, 1.0],       // #FFFFFF
});

const TWO_PI = Math.PI * 2.0;
const DEG_TO_RAD = Math.PI / 180.0;
const RAD_TO_DEG = 180.0 / Math.PI;

const Add3 = (A, B) => [A[0] + B[0], A[1] + B[1], A[2] + B[2]];
const Sub3 = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
const Mul3 = (A, S) => [A[0] * S, A[1] * S, A[2] * S];
const Dot3 = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Cross3 = (A, B) => [
  A[1] * B[2] - A[2] * B[1],
  A[2] * B[0] - A[0] * B[2],
  A[0] * B[1] - A[1] * B[0],
];
const Len3 = (V) => Math.hypot(V[0], V[1], V[2]);
const Norm3 = (V) => {
  const L = Len3(V);
  return L > 1e-8 ? [V[0] / L, V[1] / L, V[2] / L] : [0, 0, 1];
};

function BuildPerpendicularBasis(Axis) {
  const Helper = Math.abs(Axis[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
  const U = Norm3(Cross3(Helper, Axis));
  const V = Norm3(Cross3(Axis, U));
  return [U, V];
}

export function ComputeGizmoWorldRadius(Pivot, CameraState, Aspect = 1.6) {
  if (CameraState.Projection !== "Perspective") {
    return Math.max(0.18, (CameraState.OrthoHalfHeight || 2.15) * 0.34);
  }
  const CamDist = Math.max(0.25, Len3(Sub3(Pivot, CameraState.Position)));
  const HalfV = Math.tan(((CameraState.FovDegrees || 52) * DEG_TO_RAD) * 0.5);
  return Math.max(0.12, CamDist * HalfV * 0.36);
}

// Vertex layout: 12 floats per vertex
//   [px, py, pz, nx, ny, nz, r, g, b, a, handleId, shadingWeight]
const FLOATS_PER_VERTEX = 12;

export function BuildSlateGizmoGeometry(Pivot, CameraForward, WorldRadius) {
  const OpaqueVerts = [];
  const TranslucentVerts = [];
  const PickTriangles = []; // { A, B, C, Handle }

  const PushTri = (Target, A, B, C, Na, Nb, Nc, Col, Handle, ShadingWeight, Pickable = true) => {
    const Emit = (P, N) => {
      Target.push(
        P[0], P[1], P[2],
        N[0], N[1], N[2],
        Col[0], Col[1], Col[2], Col[3],
        Handle,
        ShadingWeight,
      );
    };
    Emit(A, Na);
    Emit(B, Nb);
    Emit(C, Nc);
    if (Pickable) {
      PickTriangles.push({ A, B, C, Handle });
    }
  };

  const AppendAxisStem = (Axis, R0, R1, Radius, Col, Handle) => {
    const [U, V] = BuildPerpendicularBasis(Axis);
    const P0 = Add3(Pivot, Mul3(Axis, R0));
    const P1 = Add3(Pivot, Mul3(Axis, R1));
    const Segs = 10;
    for (let I = 0; I < Segs; ++I) {
      const A0 = (I / Segs) * TWO_PI;
      const A1 = ((I + 1) / Segs) * TWO_PI;
      const RDir0 = Add3(Mul3(U, Math.cos(A0)), Mul3(V, Math.sin(A0)));
      const RDir1 = Add3(Mul3(U, Math.cos(A1)), Mul3(V, Math.sin(A1)));
      const B0 = Add3(P0, Mul3(RDir0, Radius));
      const B1 = Add3(P0, Mul3(RDir1, Radius));
      const T0 = Add3(P1, Mul3(RDir0, Radius));
      const T1 = Add3(P1, Mul3(RDir1, Radius));
      PushTri(OpaqueVerts, B0, B1, T1, RDir0, RDir1, RDir1, Col, Handle, 0.65, true);
      PushTri(OpaqueVerts, B0, T1, T0, RDir0, RDir1, RDir0, Col, Handle, 0.65, true);
    }
  };

  const AppendCone = (BaseCenter, Axis, Radius, Height, Col, Handle) => {
    const [U, V] = BuildPerpendicularBasis(Axis);
    const Apex = Add3(BaseCenter, Mul3(Axis, Height));
    const SlopeScale = Radius / Math.max(Height, 1e-5);
    const Segs = 24;
    for (let I = 0; I < Segs; ++I) {
      const A0 = (I / Segs) * TWO_PI;
      const A1 = ((I + 1) / Segs) * TWO_PI;
      const R0 = Add3(Mul3(U, Math.cos(A0)), Mul3(V, Math.sin(A0)));
      const R1 = Add3(Mul3(U, Math.cos(A1)), Mul3(V, Math.sin(A1)));
      const P0 = Add3(BaseCenter, Mul3(R0, Radius));
      const P1 = Add3(BaseCenter, Mul3(R1, Radius));
      const N0 = Norm3(Add3(R0, Mul3(Axis, SlopeScale)));
      const N1 = Norm3(Add3(R1, Mul3(Axis, SlopeScale)));
      const NApex = Norm3(Add3(N0, N1));
      PushTri(OpaqueVerts, P0, P1, Apex, N0, N1, NApex, Col, Handle, 1.0, true);
      const NegAxis = Mul3(Axis, -1);
      PushTri(OpaqueVerts, BaseCenter, P1, P0, NegAxis, NegAxis, NegAxis, Col, Handle, 1.0, true);
    }
  };

  const AppendCylinder = (CenterPos, Axis, Radius, Height, Col, Handle) => {
    const [U, V] = BuildPerpendicularBasis(Axis);
    const BasePos = Sub3(CenterPos, Mul3(Axis, Height * 0.5));
    const TopPos = Add3(CenterPos, Mul3(Axis, Height * 0.5));
    const NegAxis = Mul3(Axis, -1);
    const Segs = 24;
    for (let I = 0; I < Segs; ++I) {
      const A0 = (I / Segs) * TWO_PI;
      const A1 = ((I + 1) / Segs) * TWO_PI;
      const R0 = Add3(Mul3(U, Math.cos(A0)), Mul3(V, Math.sin(A0)));
      const R1 = Add3(Mul3(U, Math.cos(A1)), Mul3(V, Math.sin(A1)));
      const B0 = Add3(BasePos, Mul3(R0, Radius));
      const B1 = Add3(BasePos, Mul3(R1, Radius));
      const T0 = Add3(TopPos, Mul3(R0, Radius));
      const T1 = Add3(TopPos, Mul3(R1, Radius));
      PushTri(OpaqueVerts, B0, B1, T1, R0, R1, R1, Col, Handle, 1.0, true);
      PushTri(OpaqueVerts, B0, T1, T0, R0, R1, R0, Col, Handle, 1.0, true);
      PushTri(OpaqueVerts, TopPos, T0, T1, Axis, Axis, Axis, Col, Handle, 1.0, true);
      PushTri(OpaqueVerts, BasePos, B1, B0, NegAxis, NegAxis, NegAxis, Col, Handle, 1.0, true);
    }
  };

  const AppendDoubleSidedQuad = (Target, A, B, C, D, Col, Handle, Pickable = true) => {
    const N = Norm3(Cross3(Sub3(B, A), Sub3(C, A)));
    const NegN = Mul3(N, -1);
    PushTri(Target, A, B, C, N, N, N, Col, Handle, 0.0, Pickable);
    PushTri(Target, A, C, D, N, N, N, Col, Handle, 0.0, Pickable);
    PushTri(Target, A, C, B, NegN, NegN, NegN, Col, Handle, 0.0, false);
    PushTri(Target, A, D, C, NegN, NegN, NegN, Col, Handle, 0.0, false);
  };

  const AppendPlaneQuad = (AxisU, AxisV, Offset, HalfSize, BorderW, FillCol, EdgeCol, Handle) => {
    const QuadCenter = Add3(Pivot, Mul3(Add3(AxisU, AxisV), Offset));
    const P00 = Add3(Add3(QuadCenter, Mul3(AxisU, -HalfSize)), Mul3(AxisV, -HalfSize));
    const P10 = Add3(Add3(QuadCenter, Mul3(AxisU, HalfSize)), Mul3(AxisV, -HalfSize));
    const P11 = Add3(Add3(QuadCenter, Mul3(AxisU, HalfSize)), Mul3(AxisV, HalfSize));
    const P01 = Add3(Add3(QuadCenter, Mul3(AxisU, -HalfSize)), Mul3(AxisV, HalfSize));

    AppendDoubleSidedQuad(TranslucentVerts, P00, P10, P11, P01, FillCol, Handle, true);

    // Two opaque outer border strips away from Pivot (+U edge and +V edge)
    const TopInner0 = Sub3(P01, Mul3(AxisV, BorderW));
    const TopInner1 = Sub3(P11, Mul3(AxisV, BorderW));
    AppendDoubleSidedQuad(OpaqueVerts, TopInner0, TopInner1, P11, P01, EdgeCol, Handle, true);

    const RightInner0 = Sub3(P10, Mul3(AxisU, BorderW));
    const RightInner1 = Sub3(P11, Mul3(AxisU, BorderW));
    AppendDoubleSidedQuad(OpaqueVerts, RightInner0, P10, P11, RightInner1, EdgeCol, Handle, true);
  };

  const AppendArcStrip = (AxisU, AxisV, InnerR, OuterR, StartRad, EndRad, Col, Handle) => {
    const PlaneN = Norm3(Cross3(AxisU, AxisV));
    const NegN = Mul3(PlaneN, -1);
    const Segs = 24;
    for (let I = 0; I < Segs; ++I) {
      const A0 = StartRad + (I / Segs) * (EndRad - StartRad);
      const A1 = StartRad + ((I + 1) / Segs) * (EndRad - StartRad);
      const D0 = Add3(Mul3(AxisU, Math.cos(A0)), Mul3(AxisV, Math.sin(A0)));
      const D1 = Add3(Mul3(AxisU, Math.cos(A1)), Mul3(AxisV, Math.sin(A1)));
      const I0 = Add3(Pivot, Mul3(D0, InnerR));
      const O0 = Add3(Pivot, Mul3(D0, OuterR));
      const I1 = Add3(Pivot, Mul3(D1, InnerR));
      const O1 = Add3(Pivot, Mul3(D1, OuterR));
      PushTri(OpaqueVerts, I0, O0, O1, PlaneN, PlaneN, PlaneN, Col, Handle, 0.0, true);
      PushTri(OpaqueVerts, I0, O1, I1, PlaneN, PlaneN, PlaneN, Col, Handle, 0.0, true);
      PushTri(OpaqueVerts, I0, O1, O0, NegN, NegN, NegN, Col, Handle, 0.0, false);
      PushTri(OpaqueVerts, I0, I1, O1, NegN, NegN, NegN, Col, Handle, 0.0, false);
    }
  };

  const AppendBillboardTorus = (ViewNormal, MajorR, TubeR, Col, Handle) => {
    const [RingU, RingV] = BuildPerpendicularBasis(ViewNormal);
    const MajorSegs = 40;
    const MinorSegs = 8;
    for (let I = 0; I < MajorSegs; ++I) {
      const Phi0 = (I / MajorSegs) * TWO_PI;
      const Phi1 = ((I + 1) / MajorSegs) * TWO_PI;
      const Rad0 = Add3(Mul3(RingU, Math.cos(Phi0)), Mul3(RingV, Math.sin(Phi0)));
      const Rad1 = Add3(Mul3(RingU, Math.cos(Phi1)), Mul3(RingV, Math.sin(Phi1)));
      for (let J = 0; J < MinorSegs; ++J) {
        const Th0 = (J / MinorSegs) * TWO_PI;
        const Th1 = ((J + 1) / MinorSegs) * TWO_PI;
        const EvalPt = (Rad, Th) => {
          const Off = Add3(
            Mul3(Rad, MajorR + TubeR * Math.cos(Th)),
            Mul3(ViewNormal, TubeR * Math.sin(Th)),
          );
          const N = Norm3(Add3(Mul3(Rad, Math.cos(Th)), Mul3(ViewNormal, Math.sin(Th))));
          return [Add3(Pivot, Off), N];
        };
        const [P00, N00] = EvalPt(Rad0, Th0);
        const [P10, N10] = EvalPt(Rad1, Th0);
        const [P11, N11] = EvalPt(Rad1, Th1);
        const [P01, N01] = EvalPt(Rad0, Th1);
        PushTri(OpaqueVerts, P00, P10, P11, N00, N10, N11, Col, Handle, 0.0, false);
        PushTri(OpaqueVerts, P00, P11, P01, N00, N11, N01, Col, Handle, 0.0, false);
      }
    }
  };

  const L = Math.max(WorldRadius, 1e-3);
  const TipDist = 0.95 * L;
  const CylDist = 0.67 * L;
  const ConeR = 0.06 * L;
  const ConeH = 0.18 * L;
  const CylR = 0.06 * L;
  const CylH = 0.14 * L;
  const StemR = 0.0085 * L;
  const PlaneHalf = 0.08 * L;
  const PlaneOff = TipDist - PlaneHalf;
  const PlaneBorder = 0.011 * L;
  const ArcRadius = TipDist * 0.62;
  const ArcHalfBand = 0.038 * L;
  const ArcStart = (45.0 - 15.5) * DEG_TO_RAD;
  const ArcEnd = (45.0 + 15.5) * DEG_TO_RAD;
  const CenterMajorR = 0.16 * L;
  const CenterTubeR = 0.010 * L;

  const AxisX = [1, 0, 0];
  const AxisY = [0, 1, 0];
  const AxisZ = [0, 0, 1];

  // Subtle connecting stems from center ring to arrow cone
  AppendAxisStem(AxisX, CenterMajorR, TipDist, StemR, PALETTE.AxisX, GizmoHandle.TranslateX);
  AppendAxisStem(AxisY, CenterMajorR, TipDist, StemR, PALETTE.AxisY, GizmoHandle.TranslateY);
  AppendAxisStem(AxisZ, CenterMajorR, TipDist, StemR, PALETTE.AxisZ, GizmoHandle.TranslateZ);

  // 1..3: Translate Cones
  AppendCone(Add3(Pivot, Mul3(AxisX, TipDist)), AxisX, ConeR, ConeH, PALETTE.AxisX, GizmoHandle.TranslateX);
  AppendCone(Add3(Pivot, Mul3(AxisY, TipDist)), AxisY, ConeR, ConeH, PALETTE.AxisY, GizmoHandle.TranslateY);
  AppendCone(Add3(Pivot, Mul3(AxisZ, TipDist)), AxisZ, ConeR, ConeH, PALETTE.AxisZ, GizmoHandle.TranslateZ);

  // 4..6: Scale Cylinders
  AppendCylinder(Add3(Pivot, Mul3(AxisX, CylDist)), AxisX, CylR, CylH, PALETTE.AxisX, GizmoHandle.ScaleX);
  AppendCylinder(Add3(Pivot, Mul3(AxisY, CylDist)), AxisY, CylR, CylH, PALETTE.AxisY, GizmoHandle.ScaleY);
  AppendCylinder(Add3(Pivot, Mul3(AxisZ, CylDist)), AxisZ, CylR, CylH, PALETTE.AxisZ, GizmoHandle.ScaleZ);

  // 7..9: Plane Quads + 2 Outer Edges
  AppendPlaneQuad(AxisY, AxisZ, PlaneOff, PlaneHalf, PlaneBorder, PALETTE.PlaneYZ, PALETTE.EdgeYZ, GizmoHandle.PlaneYZ);
  AppendPlaneQuad(AxisX, AxisZ, PlaneOff, PlaneHalf, PlaneBorder, PALETTE.PlaneXZ, PALETTE.EdgeXZ, GizmoHandle.PlaneXZ);
  AppendPlaneQuad(AxisX, AxisY, PlaneOff, PlaneHalf, PlaneBorder, PALETTE.PlaneXY, PALETTE.EdgeXY, GizmoHandle.PlaneXY);

  // 10..12: 31-degree Rotation Arcs at 45 degrees
  AppendArcStrip(AxisY, AxisZ, ArcRadius - ArcHalfBand, ArcRadius + ArcHalfBand, ArcStart, ArcEnd, PALETTE.AxisX, GizmoHandle.RotateX);
  AppendArcStrip(AxisZ, AxisX, ArcRadius - ArcHalfBand, ArcRadius + ArcHalfBand, ArcStart, ArcEnd, PALETTE.AxisY, GizmoHandle.RotateY);
  AppendArcStrip(AxisX, AxisY, ArcRadius - ArcHalfBand, ArcRadius + ArcHalfBand, ArcStart, ArcEnd, PALETTE.AxisZ, GizmoHandle.RotateZ);

  // 13: Camera-Billboarded Center Ring
  const ViewNormal = Norm3(Mul3(CameraForward, -1));
  AppendBillboardTorus(ViewNormal, CenterMajorR, CenterTubeR, PALETTE.CenterRing, GizmoHandle.CenterRing);

  return {
    Pivot,
    WorldRadius: L,
    ViewNormal,
    CenterMajorR,
    OpaqueBuffer: new Float32Array(OpaqueVerts),
    OpaqueVertexCount: OpaqueVerts.length / FLOATS_PER_VERTEX,
    TranslucentBuffer: new Float32Array(TranslucentVerts),
    TranslucentVertexCount: TranslucentVerts.length / FLOATS_PER_VERTEX,
    PickTriangles,
  };
}

function IntersectRayTriangle(RayOrigin, RayDir, A, B, C) {
  const E1 = Sub3(B, A);
  const E2 = Sub3(C, A);
  const PVec = Cross3(RayDir, E2);
  const Det = Dot3(E1, PVec);
  if (Math.abs(Det) < 1e-8) return Infinity;
  const InvDet = 1.0 / Det;
  const TVec = Sub3(RayOrigin, A);
  const U = Dot3(TVec, PVec) * InvDet;
  if (U < 0.0 || U > 1.0) return Infinity;
  const QVec = Cross3(TVec, E1);
  const V = Dot3(RayDir, QVec) * InvDet;
  if (V < 0.0 || U + V > 1.0) return Infinity;
  const T = Dot3(E2, QVec) * InvDet;
  return T > 1e-4 ? T : Infinity;
}

export function EvaluateAxisRayParameter(AxisOrigin, AxisDir, RayOrigin, RayDir) {
  const Diff = Sub3(AxisOrigin, RayOrigin);
  const B = Dot3(AxisDir, RayDir);
  const Denom = 1.0 - B * B;
  if (Math.abs(Denom) < 1e-5) {
    return -Dot3(AxisDir, Diff);
  }
  const D = Dot3(AxisDir, Diff);
  const E = Dot3(RayDir, Diff);
  return (B * E - D) / Denom;
}

export function EvaluatePlaneRayIntersection(PlaneOrigin, PlaneNormal, RayOrigin, RayDir) {
  const Denom = Dot3(PlaneNormal, RayDir);
  if (Math.abs(Denom) < 1e-5) return null;
  const T = Dot3(PlaneNormal, Sub3(PlaneOrigin, RayOrigin)) / Denom;
  if (T < 0.0) return null;
  return Add3(RayOrigin, Mul3(RayDir, T));
}

function EvaluateRaySegmentClosest(RayOrigin, RayDir, SegA, SegB) {
  const Seg = Sub3(SegB, SegA);
  const SegLen = Len3(Seg);
  if (SegLen < 1e-6) {
    const T = Math.max(0.0, Dot3(Sub3(SegA, RayOrigin), RayDir));
    const Pt = Add3(RayOrigin, Mul3(RayDir, T));
    return { Dist: Len3(Sub3(Pt, SegA)), RayT: T, AxisParam: 0.0 };
  }
  const Axis = Mul3(Seg, 1.0 / SegLen);
  const RawS = EvaluateAxisRayParameter(SegA, Axis, RayOrigin, RayDir);
  const ClampedS = Math.max(0.0, Math.min(SegLen, RawS));
  const AxisPt = Add3(SegA, Mul3(Axis, ClampedS));
  const RayT = Math.max(0.0, Dot3(Sub3(AxisPt, RayOrigin), RayDir));
  const RayPt = Add3(RayOrigin, Mul3(RayDir, RayT));
  return { Dist: Len3(Sub3(RayPt, AxisPt)), RayT, AxisParam: ClampedS };
}

export function PickGizmoHandle(RayOrigin, RayDir, BuiltGizmo) {
  if (!BuiltGizmo) return GizmoHandle.None;
  const Pivot = BuiltGizmo.Pivot;
  const L = BuiltGizmo.WorldRadius;
  let BestT = Infinity;
  let BestHandle = GizmoHandle.None;

  const Consider = (CandidateT, Handle) => {
    if (CandidateT > 1e-4 && CandidateT < BestT) {
      BestT = CandidateT;
      BestHandle = Handle;
    }
  };

  // 1. CenterRing billboarded annulus with front-tube priority & generous Slate pick tolerance
  const RingHit = EvaluatePlaneRayIntersection(
    Pivot,
    BuiltGizmo.ViewNormal,
    RayOrigin,
    RayDir,
  );
  if (RingHit) {
    const Radial = Len3(Sub3(RingHit, Pivot));
    const BandTol = L * 0.046;
    if (Math.abs(Radial - BuiltGizmo.CenterMajorR) <= BandTol) {
      Consider(Len3(Sub3(RingHit, RayOrigin)) - 0.028 * L, GizmoHandle.CenterRing);
    }
  }

  // 2. Plane Quads (YZ, XZ, XY) + 31° Rotation Arcs (RotateX, RotateY, RotateZ)
  const TipDist = 0.95 * L;
  const PlaneHalf = 0.08 * L;
  const PlaneOff = TipDist - PlaneHalf;
  const PlanePickHalf = PlaneHalf * 1.24;
  const ArcRadius = TipDist * 0.62;
  const ArcPickTol = 0.062 * L;
  const ArcMinRad = (45.0 - 19.0) * DEG_TO_RAD;
  const ArcMaxRad = (45.0 + 19.0) * DEG_TO_RAD;

  const PlaneDefs = [
    { N: [1, 0, 0], U: [0, 1, 0], V: [0, 0, 1], PlaneH: GizmoHandle.PlaneYZ, RotH: GizmoHandle.RotateX },
    { N: [0, 1, 0], U: [0, 0, 1], V: [1, 0, 0], PlaneH: GizmoHandle.PlaneXZ, RotH: GizmoHandle.RotateY },
    { N: [0, 0, 1], U: [1, 0, 0], V: [0, 1, 0], PlaneH: GizmoHandle.PlaneXY, RotH: GizmoHandle.RotateZ },
  ];
  for (const Def of PlaneDefs) {
    const Hit = EvaluatePlaneRayIntersection(Pivot, Def.N, RayOrigin, RayDir);
    if (!Hit) continue;
    const Rel = Sub3(Hit, Pivot);
    const Cu = Dot3(Rel, Def.U);
    const Cv = Dot3(Rel, Def.V);
    const HitT = Len3(Sub3(Hit, RayOrigin));

    if (
      Math.abs(Cu - PlaneOff) <= PlanePickHalf &&
      Math.abs(Cv - PlaneOff) <= PlanePickHalf
    ) {
      Consider(HitT - 0.015 * L, Def.PlaneH);
    }

    if (Cu > 0.0 && Cv > 0.0) {
      const Rad = Math.hypot(Cu, Cv);
      const Ang = Math.atan2(Cv, Cu);
      if (Math.abs(Rad - ArcRadius) <= ArcPickTol && Ang >= ArcMinRad && Ang <= ArcMaxRad) {
        Consider(HitT - 0.012 * L, Def.RotH);
      }
    }
  }

  // 3. Scale Cylinders + Full Axis Stems & Translate Cones (wide ray-capsule tolerance so stems NEVER miss!)
  const CylDist = 0.67 * L;
  const CylHalfH = 0.095 * L;
  const CylPickR = 0.082 * L;
  const StemStart = BuiltGizmo.CenterMajorR + 0.038 * L;
  const ConeEnd = TipDist + 0.195 * L;
  const StemPickR = 0.048 * L;
  const ConePickR = 0.082 * L;

  const AxisDefs = [
    { Axis: [1, 0, 0], TransH: GizmoHandle.TranslateX, ScaleH: GizmoHandle.ScaleX },
    { Axis: [0, 1, 0], TransH: GizmoHandle.TranslateY, ScaleH: GizmoHandle.ScaleY },
    { Axis: [0, 0, 1], TransH: GizmoHandle.TranslateZ, ScaleH: GizmoHandle.ScaleZ },
  ];
  for (const Def of AxisDefs) {
    const CylA = Add3(Pivot, Mul3(Def.Axis, CylDist - CylHalfH));
    const CylB = Add3(Pivot, Mul3(Def.Axis, CylDist + CylHalfH));
    const CylSeg = EvaluateRaySegmentClosest(RayOrigin, RayDir, CylA, CylB);
    if (CylSeg.Dist <= CylPickR) {
      Consider(CylSeg.RayT - 0.02 * L, Def.ScaleH);
    }

    const ConeA = Add3(Pivot, Mul3(Def.Axis, TipDist - 0.02 * L));
    const ConeB = Add3(Pivot, Mul3(Def.Axis, ConeEnd));
    const ConeSeg = EvaluateRaySegmentClosest(RayOrigin, RayDir, ConeA, ConeB);
    if (ConeSeg.Dist <= ConePickR) {
      Consider(ConeSeg.RayT - 0.018 * L, Def.TransH);
    }

    const StemA = Add3(Pivot, Mul3(Def.Axis, StemStart));
    const StemB = Add3(Pivot, Mul3(Def.Axis, TipDist));
    const StemSeg = EvaluateRaySegmentClosest(RayOrigin, RayDir, StemA, StemB);
    if (StemSeg.Dist <= StemPickR) {
      Consider(StemSeg.RayT, Def.TransH);
    }
  }

  // 4. Exact triangle refinement on visual mesh
  for (const Tri of BuiltGizmo.PickTriangles) {
    const T = IntersectRayTriangle(RayOrigin, RayDir, Tri.A, Tri.B, Tri.C);
    if (T < BestT) {
      BestT = T;
      BestHandle = Tri.Handle;
    }
  }

  return BestHandle;
}

function HandlePlaneNormalAndBasis(Handle, ViewNormal) {
  switch (Handle) {
    case GizmoHandle.PlaneYZ:
    case GizmoHandle.RotateX:
      return { Normal: [1, 0, 0], U: [0, 1, 0], V: [0, 0, 1], AxisIdx: 0, Tag: "X" };
    case GizmoHandle.PlaneXZ:
    case GizmoHandle.RotateY:
      return { Normal: [0, 1, 0], U: [0, 0, 1], V: [1, 0, 0], AxisIdx: 1, Tag: "Y" };
    case GizmoHandle.PlaneXY:
    case GizmoHandle.RotateZ:
      return { Normal: [0, 0, 1], U: [1, 0, 0], V: [0, 1, 0], AxisIdx: 2, Tag: "Z" };
    case GizmoHandle.CenterRing: {
      const [U, V] = BuildPerpendicularBasis(ViewNormal);
      return { Normal: ViewNormal, U, V, AxisIdx: -1, Tag: "Free" };
    }
    default:
      return null;
  }
}

function HandleAxisInfo(Handle) {
  switch (Handle) {
    case GizmoHandle.TranslateX:
    case GizmoHandle.ScaleX:
      return { Axis: [1, 0, 0], AxisIdx: 0, Tag: "X" };
    case GizmoHandle.TranslateY:
    case GizmoHandle.ScaleY:
      return { Axis: [0, 1, 0], AxisIdx: 1, Tag: "Y" };
    case GizmoHandle.TranslateZ:
    case GizmoHandle.ScaleZ:
      return { Axis: [0, 0, 1], AxisIdx: 2, Tag: "Z" };
    default:
      return null;
  }
}

export function BeginGizmoDrag(Handle, RayOrigin, RayDir, BuiltGizmo, InitialTransform) {
  const Pivot = [...InitialTransform.Position];
  const Base = {
    Handle,
    Pivot,
    ActiveWorldRadius: BuiltGizmo.WorldRadius,
    ViewNormal: [...BuiltGizmo.ViewNormal],
    StartPosition: [...InitialTransform.Position],
    StartRotation: [...InitialTransform.Rotation],
    StartScale: [...InitialTransform.Scale],
    StartAxisParam: 0,
    StartPlanePoint: [...Pivot],
    StartAngleRad: 0,
  };

  const Ax = HandleAxisInfo(Handle);
  if (Ax) {
    Base.StartAxisParam = EvaluateAxisRayParameter(Pivot, Ax.Axis, RayOrigin, RayDir);
    return Base;
  }

  const Pl = HandlePlaneNormalAndBasis(Handle, BuiltGizmo.ViewNormal);
  if (Pl) {
    const Hit = EvaluatePlaneRayIntersection(Pivot, Pl.Normal, RayOrigin, RayDir) || Pivot;
    Base.StartPlanePoint = Hit;
    const Rel = Sub3(Hit, Pivot);
    Base.StartAngleRad = Math.atan2(Dot3(Rel, Pl.V), Dot3(Rel, Pl.U));
  }

  return Base;
}

const SnapStep = (Val, Step) => Math.round(Val / Step) * Step;
const SignedFmt = (V, Digits = 2) => `${V >= 0 ? "+" : ""}${V.toFixed(Digits)}`;

export function SolveGizmoDrag(DragState, RayOrigin, RayDir, SnapEnabled = false) {
  const NextPos = [...DragState.StartPosition];
  const NextRot = [...DragState.StartRotation];
  const NextScale = [...DragState.StartScale];
  const H = DragState.Handle;
  let Readout = "";

  // 1..3: Axis Translate
  if (H >= GizmoHandle.TranslateX && H <= GizmoHandle.TranslateZ) {
    const Ax = HandleAxisInfo(H);
    let Delta =
      EvaluateAxisRayParameter(DragState.Pivot, Ax.Axis, RayOrigin, RayDir) -
      DragState.StartAxisParam;
    if (SnapEnabled) Delta = SnapStep(Delta, 0.25);
    NextPos[Ax.AxisIdx] = +(DragState.StartPosition[Ax.AxisIdx] + Delta).toFixed(3);
    Readout = `${Ax.Tag} move ${SignedFmt(Delta)} m`;
    return { Position: NextPos, Rotation: NextRot, Scale: NextScale, Readout };
  }

  // 4..6: Axis Scale (world radius frozen at Start!)
  if (H >= GizmoHandle.ScaleX && H <= GizmoHandle.ScaleZ) {
    const Ax = HandleAxisInfo(H);
    const RawDelta =
      EvaluateAxisRayParameter(DragState.Pivot, Ax.Axis, RayOrigin, RayDir) -
      DragState.StartAxisParam;
    const RefArm = Math.max(0.08, 0.67 * DragState.ActiveWorldRadius);
    let Factor = Math.max(0.05, Math.min(20.0, 1.0 + RawDelta / RefArm));
    if (SnapEnabled) Factor = Math.max(0.1, SnapStep(Factor, 0.1));
    NextScale[Ax.AxisIdx] = +Math.max(0.02, DragState.StartScale[Ax.AxisIdx] * Factor).toFixed(3);
    Readout = `${Ax.Tag} scale ×${Factor.toFixed(2)}`;
    return { Position: NextPos, Rotation: NextRot, Scale: NextScale, Readout };
  }

  // 7..9 & 13: Planar / CenterRing Translate
  if (
    (H >= GizmoHandle.PlaneYZ && H <= GizmoHandle.PlaneXY) ||
    H === GizmoHandle.CenterRing
  ) {
    const Pl = HandlePlaneNormalAndBasis(H, DragState.ViewNormal);
    const Hit = EvaluatePlaneRayIntersection(
      DragState.Pivot,
      Pl.Normal,
      RayOrigin,
      RayDir,
    );
    if (Hit) {
      let Delta = Sub3(Hit, DragState.StartPlanePoint);
      if (SnapEnabled) {
        Delta = Delta.map((C) => SnapStep(C, 0.25));
      }
      for (let K = 0; K < 3; ++K) {
        NextPos[K] = +(DragState.StartPosition[K] + Delta[K]).toFixed(3);
      }
      const Dist = Len3(Delta);
      const Label =
        H === GizmoHandle.PlaneYZ
          ? "YZ move"
          : H === GizmoHandle.PlaneXZ
            ? "XZ move"
            : H === GizmoHandle.PlaneXY
              ? "XY move"
              : "Free move";
      Readout = `${Label} ${Dist.toFixed(2)} m`;
    }
    return { Position: NextPos, Rotation: NextRot, Scale: NextScale, Readout };
  }

  // 10..12: Rotation Arc
  if (H >= GizmoHandle.RotateX && H <= GizmoHandle.RotateZ) {
    const Pl = HandlePlaneNormalAndBasis(H, DragState.ViewNormal);
    const Hit = EvaluatePlaneRayIntersection(
      DragState.Pivot,
      Pl.Normal,
      RayOrigin,
      RayDir,
    );
    if (Hit) {
      const Rel = Sub3(Hit, DragState.Pivot);
      if (Len3(Rel) > 1e-4) {
        const CurAngle = Math.atan2(Dot3(Rel, Pl.V), Dot3(Rel, Pl.U));
        let DeltaDeg = (CurAngle - DragState.StartAngleRad) * RAD_TO_DEG;
        while (DeltaDeg > 180.0) DeltaDeg -= 360.0;
        while (DeltaDeg < -180.0) DeltaDeg += 360.0;
        if (SnapEnabled) DeltaDeg = SnapStep(DeltaDeg, 15.0);
        let NewDeg = DragState.StartRotation[Pl.AxisIdx] + DeltaDeg;
        while (NewDeg > 180.0) NewDeg -= 360.0;
        while (NewDeg < -180.0) NewDeg += 360.0;
        NextRot[Pl.AxisIdx] = +NewDeg.toFixed(1);
        Readout = `${Pl.Tag} rot ${SignedFmt(DeltaDeg, 1)}°`;
      }
    }
    return { Position: NextPos, Rotation: NextRot, Scale: NextScale, Readout };
  }

  return { Position: NextPos, Rotation: NextRot, Scale: NextScale, Readout };
}

//==========================================================================================================================================
//                                            WEBGL2 GIZMO SHADERS & VIEW MATRIX
//==========================================================================================================================================
export const GIZMO_VERTEX_SHADER = `#version 300 es
precision highp float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aMeta; // .x = handleId, .y = shadingWeight

uniform mat4 uViewProj;

out vec3 vNormal;
out vec4 vColor;
out float vHandleId;
out float vShadingWeight;

void main() {
  gl_Position = uViewProj * vec4(aPosition, 1.0);
  vNormal = aNormal;
  vColor = aColor;
  vHandleId = aMeta.x;
  vShadingWeight = aMeta.y;
}
`;

export const GIZMO_FRAGMENT_SHADER = `#version 300 es
precision highp float;

in vec3 vNormal;
in vec4 vColor;
in float vHandleId;
in float vShadingWeight;

uniform float uHighlightedHandle;

out vec4 fragColor;

void main() {
  vec3 N = normalize(vNormal);
  vec3 lightDir = normalize(vec3(0.45, 0.72, 0.52));
  float lambert = 0.62 + 0.38 * clamp(abs(dot(N, lightDir)), 0.0, 1.0);
  vec3 shaded = vColor.rgb * mix(1.0, lambert, vShadingWeight);

  bool hovered = (uHighlightedHandle > 0.5) && (abs(vHandleId - uHighlightedHandle) < 0.25);
  vec3 finalRgb = hovered ? clamp(shaded + vec3(0.33333334), 0.0, 1.0) : shaded;
  float finalAlpha = hovered && (vColor.a < 0.99) ? 0.55 : vColor.a;

  fragColor = vec4(finalRgb, finalAlpha);
}
`;

function MultiplyMat4(A, B) {
  const Out = new Float32Array(16);
  for (let Col = 0; Col < 4; ++Col) {
    for (let Row = 0; Row < 4; ++Row) {
      Out[Col * 4 + Row] =
        A[0 * 4 + Row] * B[Col * 4 + 0] +
        A[1 * 4 + Row] * B[Col * 4 + 1] +
        A[2 * 4 + Row] * B[Col * 4 + 2] +
        A[3 * 4 + Row] * B[Col * 4 + 3];
    }
  }
  return Out;
}

export function BuildColumnMajorViewProj(Basis, CameraState, Aspect, Near = 0.02, Far = 80.0) {
  const R = Basis.Right;
  const U = Basis.Up;
  const F = Basis.Forward;
  const P = CameraState.Position;

  // OpenGL right-handed view matrix looking down -Z (so Forward maps to -Z_view)
  const View = new Float32Array([
    R[0], U[0], -F[0], 0,
    R[1], U[1], -F[1], 0,
    R[2], U[2], -F[2], 0,
    -Dot3(R, P), -Dot3(U, P), Dot3(F, P), 1,
  ]);

  const SafeAspect = Math.max(0.1, Aspect);
  const Proj = new Float32Array(16);
  if (CameraState.Projection === "Perspective") {
    const FovRad = ((CameraState.FovDegrees || 52) * Math.PI) / 180.0;
    const Focal = 1.0 / Math.tan(FovRad * 0.5);
    Proj[0] = Focal / SafeAspect;
    Proj[5] = Focal;
    Proj[10] = (Far + Near) / (Near - Far);
    Proj[11] = -1.0;
    Proj[14] = (2.0 * Far * Near) / (Near - Far);
  } else {
    const HalfH = Math.max(0.15, CameraState.OrthoHalfHeight || 2.15);
    const HalfW = HalfH * SafeAspect;
    Proj[0] = 1.0 / HalfW;
    Proj[5] = 1.0 / HalfH;
    Proj[10] = -2.0 / (Far - Near);
    Proj[14] = -(Far + Near) / (Far - Near);
    Proj[15] = 1.0;
  }

  return MultiplyMat4(Proj, View);
}

// WebGPU / Vulkan [0, 1] clip-space depth variant of BuildColumnMajorViewProj
export function BuildColumnMajorViewProjZeroToOne(Basis, CameraState, Aspect, Near = 0.02, Far = 80.0) {
  const R = Basis.Right;
  const U = Basis.Up;
  const F = Basis.Forward;
  const P = CameraState.Position;

  const View = new Float32Array([
    R[0], U[0], -F[0], 0,
    R[1], U[1], -F[1], 0,
    R[2], U[2], -F[2], 0,
    -Dot3(R, P), -Dot3(U, P), Dot3(F, P), 1,
  ]);

  const SafeAspect = Math.max(0.1, Aspect);
  const Proj = new Float32Array(16);
  if (CameraState.Projection === "Perspective") {
    const FovRad = ((CameraState.FovDegrees || 52) * Math.PI) / 180.0;
    const Focal = 1.0 / Math.tan(FovRad * 0.5);
    Proj[0] = Focal / SafeAspect;
    Proj[5] = Focal;
    Proj[10] = Far / (Near - Far);
    Proj[11] = -1.0;
    Proj[14] = (Far * Near) / (Near - Far);
  } else {
    const HalfH = Math.max(0.15, CameraState.OrthoHalfHeight || 2.15);
    const HalfW = HalfH * SafeAspect;
    Proj[0] = 1.0 / HalfW;
    Proj[5] = 1.0 / HalfH;
    Proj[10] = -1.0 / (Far - Near);
    Proj[14] = -Near / (Far - Near);
    Proj[15] = 1.0;
  }

  return MultiplyMat4(Proj, View);
}
