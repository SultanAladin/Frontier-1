// Experimental/LoftedVehicleSdfWorkbench/LoftedVehicleShaders.js
// 🧩 Paired WebGPU WGSL + WebGL2 GLSL ES 3.00 shaders for the Lofted Bi-Directional Curve SDF vs. 3D Voxel Grid SDF Workbench:
//      1. ShowroomSurface shader: Crease-smoothed 25,068-triangle ControlVehicle + Showroom Floor & Cyclorama Wall
//         with live SDF Cone-Traced Soft Shadows, SDF Underbody Contact AO, Glossy SDF Floor Reflections,
//         and Interactive 3D Signed Distance Cross-Section Isoline Plane!
//      2. RayMarchIsoSurface shader: Direct sphere-traced d(x) = 0 iso-surface renderer with analytical/gradient normals,
//         clearcoat automotive shading, and Showroom Zebra Reflection-Bar curvature inspection!
//      3. CageOverlay shader: Depth-tested 3D Bi-Directional Rib & Rail Splines / Voxel Lattice line overlay!

const SharedWgslSdfLibrary = /* wgsl */ `
struct SceneUniforms {
    ViewProj    : mat4x4<f32>,
    InvViewProj : mat4x4<f32>,
    Params0     : vec4<f32>, // xyz = CamPos, w = ViewportW
    Params1     : vec4<f32>, // xyz = SunDir, w = ViewportH
    Params2     : vec4<f32>, // x = ActiveSdfMode (0=QuadPatch, 1=CylLoft, 2=Vox), y = SplitToggle, z = DisplayMode, w = ZebraToggle
    Params3     : vec4<f32>, // x = RibsM, y = RailsK, z = WheelSamples, w = WheelHalfWidth
    Params4     : vec4<f32>, // x = LoftZMin, y = LoftZMax, z = LoftZSpan, w = CutCoord
    Params5     : vec4<f32>, // xyz = VoxMin, w = CutAxisMode (0=Z, 1=X)
    Params6     : vec4<f32>, // xyz = VoxMax, w = VoxelRes
    Params7     : vec4<f32>, // x = ShadowSoftness, y = FloorReflect, z = CutClipToggle, w = ElapsedSec
    Params8     : vec4<f32>, // x = QuadKnotSpan, y = QuadHaloLimit, z = SplitRightMode (2=Vox, 1=CylLoft), w = QuadWheelSamples
};

@group(0) @binding(0) var<uniform> Uni : SceneUniforms;
@group(0) @binding(1) var LoftTex      : texture_2d<f32>;
@group(0) @binding(2) var VoxelTex     : texture_3d<f32>;
@group(0) @binding(3) var LinearSamp   : sampler;
@group(0) @binding(4) var QuadTex      : texture_2d<f32>;

fn FractPositive(X : f32) -> f32 {
    return X - floor(X);
}

fn EvalSpineY(U : f32) -> f32 {
    let Diff = (U - 0.46) / 0.28;
    return 0.78 + 0.18 * exp(-Diff * Diff);
}

fn EvalSpineDerivDu(U : f32) -> f32 {
    let Diff = (U - 0.46) / 0.28;
    return 0.18 * exp(-Diff * Diff) * (-2.0 * Diff / 0.28);
}

fn HermiteWeights(T : f32) -> vec4<f32> {
    let T2 = T * T;
    let T3 = T2 * T;
    return vec4<f32>(
        2.0 * T3 - 3.0 * T2 + 1.0,
        T3 - 2.0 * T2 + T,
        -2.0 * T3 + 3.0 * T2,
        T3 - T2
    );
}

fn HermiteDerivWeights(T : f32) -> vec4<f32> {
    let T2 = T * T;
    return vec4<f32>(
        6.0 * T2 - 6.0 * T,
        3.0 * T2 - 4.0 * T + 1.0,
        -6.0 * T2 + 6.0 * T,
        3.0 * T2 - 2.0 * T
    );
}

// Fast radius-only bicubic Hermite patch read (4 integer L1 texel loads)
fn EvalLoftRadiusOnly(UIn : f32, VIn : f32) -> f32 {
    let MCount = i32(Uni.Params3.x);
    let KCount = i32(Uni.Params3.y);
    let U = clamp(UIn, 0.0, 1.0);
    let V = FractPositive(VIn);

    let ScaledU = U * f32(MCount - 1);
    let I0 = min(MCount - 2, max(0, i32(floor(ScaledU))));
    let I1 = I0 + 1;
    let Tu = ScaledU - f32(I0);

    let ScaledV = V * f32(KCount);
    let J0 = i32(floor(ScaledV)) % KCount;
    let J1 = (J0 + 1) % KCount;
    let Tv = ScaledV - floor(ScaledV);

    let Hu = HermiteWeights(Tu);
    let Hv = HermiteWeights(Tv);

    let C00 = textureLoad(LoftTex, vec2<i32>(J0, I0), 0);
    let C10 = textureLoad(LoftTex, vec2<i32>(J0, I1), 0);
    let C01 = textureLoad(LoftTex, vec2<i32>(J1, I0), 0);
    let C11 = textureLoad(LoftTex, vec2<i32>(J1, I1), 0);

    let RJ0  = Hu.x * C00.x + Hu.y * C00.y + Hu.z * C10.x + Hu.w * C10.y;
    let RvJ0 = Hu.x * C00.z + Hu.y * C00.w + Hu.z * C10.z + Hu.w * C10.w;
    let RJ1  = Hu.x * C01.x + Hu.y * C01.y + Hu.z * C11.x + Hu.w * C11.y;
    let RvJ1 = Hu.x * C01.z + Hu.y * C01.w + Hu.z * C11.z + Hu.w * C11.w;

    return Hv.x * RJ0 + Hv.y * RvJ0 + Hv.z * RJ1 + Hv.w * RvJ1;
}

struct LoftPatchHit {
    Pos : vec3<f32>,
    Su  : vec3<f32>,
    Sv  : vec3<f32>,
};

fn EvalLoftPatchFull(UIn : f32, VIn : f32) -> LoftPatchHit {
    let MCount = i32(Uni.Params3.x);
    let KCount = i32(Uni.Params3.y);
    let U = clamp(UIn, 0.0, 1.0);
    let V = FractPositive(VIn);

    let ScaledU = U * f32(MCount - 1);
    let I0 = min(MCount - 2, max(0, i32(floor(ScaledU))));
    let I1 = I0 + 1;
    let Tu = ScaledU - f32(I0);

    let ScaledV = V * f32(KCount);
    let J0 = i32(floor(ScaledV)) % KCount;
    let J1 = (J0 + 1) % KCount;
    let Tv = ScaledV - floor(ScaledV);

    let Hu  = HermiteWeights(Tu);
    let Dhu = HermiteDerivWeights(Tu);
    let Hv  = HermiteWeights(Tv);
    let Dhv = HermiteDerivWeights(Tv);

    let C00 = textureLoad(LoftTex, vec2<i32>(J0, I0), 0);
    let C10 = textureLoad(LoftTex, vec2<i32>(J0, I1), 0);
    let C01 = textureLoad(LoftTex, vec2<i32>(J1, I0), 0);
    let C11 = textureLoad(LoftTex, vec2<i32>(J1, I1), 0);

    let RJ0   = Hu.x  * C00.x + Hu.y  * C00.y + Hu.z  * C10.x + Hu.w  * C10.y;
    let DRuJ0 = Dhu.x * C00.x + Dhu.y * C00.y + Dhu.z * C10.x + Dhu.w * C10.y;
    let RvJ0  = Hu.x  * C00.z + Hu.y  * C00.w + Hu.z  * C10.z + Hu.w  * C10.w;
    let DRuv0 = Dhu.x * C00.z + Dhu.y * C00.w + Dhu.z * C10.z + Dhu.w * C10.w;

    let RJ1   = Hu.x  * C01.x + Hu.y  * C01.y + Hu.z  * C11.x + Hu.w  * C11.y;
    let DRuJ1 = Dhu.x * C01.x + Dhu.y * C01.y + Dhu.z * C11.x + Dhu.w * C11.y;
    let RvJ1  = Hu.x  * C01.z + Hu.y  * C01.w + Hu.z  * C11.z + Hu.w  * C11.w;
    let DRuv1 = Dhu.x * C01.z + Dhu.y * C01.w + Dhu.z * C11.z + Dhu.w * C11.w;

    let R  = Hv.x * RJ0 + Hv.y * RvJ0 + Hv.z * RJ1 + Hv.w * RvJ1;
    let Ru = (Hv.x * DRuJ0 + Hv.y * DRuv0 + Hv.z * DRuJ1 + Hv.w * DRuv1) * f32(MCount - 1);
    let Rv = (Dhv.x * RJ0 + Dhv.y * RvJ0 + Dhv.z * RJ1 + Dhv.w * RvJ1) * f32(KCount);

    let TwoPi = 6.283185307;
    let Theta = V * TwoPi;
    let CosT  = cos(Theta);
    let SinT  = sin(Theta);
    let Yc    = EvalSpineY(U);
    let DycDu = EvalSpineDerivDu(U);

    var Out : LoftPatchHit;
    Out.Pos = vec3<f32>(R * CosT, Yc + R * SinT, Uni.Params4.x + Uni.Params4.z * U);
    Out.Su  = vec3<f32>(Ru * CosT, DycDu + Ru * SinT, Uni.Params4.z);
    Out.Sv  = vec3<f32>(Rv * CosT - R * TwoPi * SinT, Rv * SinT + R * TwoPi * CosT, 0.0);
    return Out;
}

fn EvalSymmetricWheelPairSdf(P : vec3<f32>, AxleZ : f32) -> f32 {
    let Dx  = 1.0475 - abs(P.x);
    let Dy  = P.y - 0.5086;
    let Dz  = P.z - AxleZ;
    let Rho = length(vec2<f32>(Dy, Dz));
    let Hw  = Uni.Params3.w;

    if (abs(Dx) > Hw + 0.35 && Rho > 0.86) {
        return length(vec2<f32>(abs(Dx) - Hw, max(0.0, Rho - 0.51)));
    }

    let WSamp = i32(Uni.Params3.z);
    let WheelRow = i32(Uni.Params3.x);
    let ClampedX = clamp(Dx, -Hw, Hw);
    let NormS = ((ClampedX + Hw) / (2.0 * Hw)) * f32(WSamp - 1);
    let I0 = min(WSamp - 2, max(0, i32(floor(NormS))));
    let T  = NormS - f32(I0);

    let H  = HermiteWeights(T);
    let W0 = textureLoad(LoftTex, vec2<i32>(I0, WheelRow), 0);
    let W1 = textureLoad(LoftTex, vec2<i32>(I0 + 1, WheelRow), 0);
    let ROuter = H.x * W0.x + H.y * W0.y + H.z * W1.x + H.w * W1.y;

    let DRad = Rho - ROuter;
    let DAx  = abs(Dx) - Hw;

    var DishRecess = 0.0;
    if (Rho < 0.31 && Dx < -Hw + 0.06) {
        let CenterDist = abs(Rho - 0.14);
        DishRecess = 0.032 * (1.0 - clamp(CenterDist / 0.19, 0.0, 1.0));
    }
    let EffAx = max(DAx, -(Hw + DishRecess) - Dx);
    let Outside = length(vec2<f32>(max(DRad, 0.0), max(EffAx, 0.0)));
    let Inside  = min(max(DRad, EffAx), 0.0);
    return Outside + Inside;
}

fn EvalLoftVehicleSdf(P : vec3<f32>) -> f32 {
    let BMin = vec3<f32>(-1.250, 0.000, -3.110);
    let BMax = vec3<f32>( 1.250, 1.769,  3.110);
    let BoxDelta = max(max(BMin - P, vec3<f32>(0.0)), P - BMax);
    let BoxDist  = length(BoxDelta);
    if (BoxDist > 0.65) {
        return BoxDist + 0.03;
    }

    let ZMin  = Uni.Params4.x;
    let ZMax  = Uni.Params4.y;
    let ZSpan = Uni.Params4.z;

    let URaw = (P.z - ZMin) / ZSpan;
    let U0   = clamp(URaw, 0.0, 1.0);
    let Yc0  = EvalSpineY(U0);
    let V0   = FractPositive(atan2(P.y - Yc0, P.x) / 6.283185307);

    // 5-point star seed around cylindrical coordinate (U0, V0)
    var BestU = U0;
    var BestV = V0;
    var BestSq = 1e9;
    var DuSeeds = array<f32, 5>(0.0, -0.045, 0.045, 0.0, 0.0);
    var DvSeeds = array<f32, 5>(0.0, 0.0, 0.0, -0.055, 0.055);

    for (var S = 0; S < 5; S = S + 1) {
        let Cu = clamp(U0 + DuSeeds[S], 0.0, 1.0);
        let Cv = FractPositive(V0 + DvSeeds[S]);
        let R  = EvalLoftRadiusOnly(Cu, Cv);
        let Th = Cv * 6.283185307;
        let Sp = vec3<f32>(R * cos(Th), EvalSpineY(Cu) + R * sin(Th), ZMin + ZSpan * Cu);
        let Diff = P - Sp;
        let Sq = dot(Diff, Diff);
        if (Sq < BestSq) {
            BestSq = Sq;
            BestU  = Cu;
            BestV  = Cv;
        }
    }

    var U = BestU;
    var V = BestV;

    // 2 steps of regularized 2x2 Gauss-Newton orthogonal projection
    for (var Step = 0; Step < 2; Step = Step + 1) {
        let Hit = EvalLoftPatchFull(U, V);
        let Res = P - Hit.Pos;
        let Gu  = dot(Res, Hit.Su);
        let Gv  = dot(Res, Hit.Sv);
        let A11 = dot(Hit.Su, Hit.Su) + 0.002;
        let A12 = dot(Hit.Su, Hit.Sv);
        let A22 = dot(Hit.Sv, Hit.Sv) + 0.002;
        let Det = A11 * A22 - A12 * A12;
        if (Det > 1e-6) {
            let Du = clamp(( A22 * Gu - A12 * Gv) / Det, -0.09, 0.09);
            let Dv = clamp((-A12 * Gu + A11 * Gv) / Det, -0.09, 0.09);
            U = clamp(U + Du, 0.0, 1.0);
            V = FractPositive(V + Dv);
        }
    }

    let FinalHit = EvalLoftPatchFull(U, V);
    let SkinDist = length(P - FinalHit.Pos);

    let RhoAtZ = length(vec2<f32>(P.x, P.y - Yc0));
    let RAtZ   = EvalLoftRadiusOnly(U0, V0);

    var DBody = SkinDist;
    if (P.z >= ZMin && P.z <= ZMax && RhoAtZ <= RAtZ) {
        let CapClear = min(P.z - ZMin, ZMax - P.z);
        DBody = -min(SkinDist, CapClear);
    } else if ((P.z < ZMin || P.z > ZMax) && RhoAtZ <= RAtZ) {
        let AxialOver = select(P.z - ZMax, ZMin - P.z, P.z < ZMin);
        DBody = min(SkinDist, AxialOver);
    }

    // Symmetric CSG wheel-arch cylinder cutouts (Front Z=+1.7274, Rear Z=-1.6686)
    let AbsX = abs(P.x);
    let RhoFront = length(vec2<f32>(P.y - 0.5086, P.z - 1.7274));
    let DfRad = RhoFront - 0.560;
    let DfAx  = max(0.750 - AbsX, AbsX - 1.35);
    let DArchFront = length(vec2<f32>(max(DfRad, 0.0), max(DfAx, 0.0))) + min(max(DfRad, DfAx), 0.0);
    DBody = max(DBody, -DArchFront);

    let RhoRear = length(vec2<f32>(P.y - 0.5086, P.z + 1.6686));
    let DrRad = RhoRear - 0.562;
    let DrAx  = max(0.885 - AbsX, AbsX - 1.35);
    let DArchRear = length(vec2<f32>(max(DrRad, 0.0), max(DrAx, 0.0))) + min(max(DrRad, DrAx), 0.0);
    DBody = max(DBody, -DArchRear);

    // Union 4 revolved wheel curve lofts (symmetric front & rear pairs)
    let DwFront = EvalSymmetricWheelPairSdf(P,  1.7274);
    let DwRear  = EvalSymmetricWheelPairSdf(P, -1.6686);
    return min(DBody, min(DwFront, DwRear));
}

//============================================================================================================================================
//      CLOSED 6-CHART 4-CORNER C² COONS-SPLINE QUAD-PATCH SDF (ZERO POLAR FAN SINGULARITIES!)
//============================================================================================================================================

struct QuadUnitRay {
    Q    : vec3<f32>,
    Qxi  : vec3<f32>,
    Qeta : vec3<f32>,
};

fn EvalQuadChartUnitQ(ChartIdx : i32, Xi : f32, Eta : f32) -> QuadUnitRay {
    let QuarterPi = 0.7853981634;
    let Cx  = cos(Xi * QuarterPi);
    let Cy  = cos(Eta * QuarterPi);
    let Tx  = sin(Xi * QuarterPi) / Cx;
    let Ty  = sin(Eta * QuarterPi) / Cy;
    let DTx = QuarterPi / (Cx * Cx);
    let DTy = QuarterPi / (Cy * Cy);

    var U    = vec3<f32>(0.0);
    var Dxi  = vec3<f32>(0.0);
    var Deta = vec3<f32>(0.0);

    if (ChartIdx == 0) {
        U = vec3<f32>(1.0, Ty, -Tx);
        Dxi = vec3<f32>(0.0, 0.0, -DTx);
        Deta = vec3<f32>(0.0, DTy, 0.0);
    } else if (ChartIdx == 1) {
        U = vec3<f32>(-1.0, Ty, Tx);
        Dxi = vec3<f32>(0.0, 0.0, DTx);
        Deta = vec3<f32>(0.0, DTy, 0.0);
    } else if (ChartIdx == 2) {
        U = vec3<f32>(Tx, 1.0, -Ty);
        Dxi = vec3<f32>(DTx, 0.0, 0.0);
        Deta = vec3<f32>(0.0, 0.0, -DTy);
    } else if (ChartIdx == 3) {
        U = vec3<f32>(Tx, -1.0, Ty);
        Dxi = vec3<f32>(DTx, 0.0, 0.0);
        Deta = vec3<f32>(0.0, 0.0, DTy);
    } else if (ChartIdx == 4) {
        U = vec3<f32>(Tx, Ty, 1.0);
        Dxi = vec3<f32>(DTx, 0.0, 0.0);
        Deta = vec3<f32>(0.0, DTy, 0.0);
    } else {
        U = vec3<f32>(-Tx, Ty, -1.0);
        Dxi = vec3<f32>(-DTx, 0.0, 0.0);
        Deta = vec3<f32>(0.0, DTy, 0.0);
    }

    let InvL = inverseSqrt(max(dot(U, U), 1e-12));
    let Q    = U * InvL;
    var Out  : QuadUnitRay;
    Out.Q    = Q;
    Out.Qxi  = (Dxi  - Q * dot(Q, Dxi))  * InvL;
    Out.Qeta = (Deta - Q * dot(Q, Deta)) * InvL;
    return Out;
}

struct ConfocalSeed {
    ChartIdx : i32,
    Xi       : f32,
    Eta      : f32,
    RPt      : f32,
};

fn WorldToConfocalSeed(P : vec3<f32>) -> ConfocalSeed {
    let Ax = 0.28;
    let Ay = 0.14;
    let Az = 2.48;
    let Y0 = 0.78;
    let Dx = P.x;
    let Dy = P.y - Y0;
    let Dz = P.z;

    var R = max(0.05, length(vec3<f32>(Dx / (Ax + 0.65), Dy / (Ay + 0.65), Dz / (Az + 0.65))) * 0.65);
    for (var It = 0; It < 4; It = It + 1) {
        let Rx = Ax + R;
        let Ry = Ay + R;
        let Rz = Az + R;
        let Ux = Dx / Rx;
        let Uy = Dy / Ry;
        let Uz = Dz / Rz;
        let Res = Ux * Ux + Uy * Uy + Uz * Uz - 1.0;
        let Der = -2.0 * (Ux * Ux / Rx + Uy * Uy / Ry + Uz * Uz / Rz);
        R = max(-0.12, R - Res / min(Der, -1e-6));
    }

    let Rx = max(0.02, Ax + R);
    let Ry = max(0.02, Ay + R);
    let Rz = max(0.02, Az + R);
    let Q  = normalize(vec3<f32>(Dx / Rx, Dy / Ry, Dz / Rz));
    let Aq = abs(Q);

    var ChartIdx = 0;
    var Tx = 0.0;
    var Ty = 0.0;
    if (Aq.x >= Aq.y && Aq.x >= Aq.z) {
        let Inv = 1.0 / max(Aq.x, 1e-8);
        if (Q.x >= 0.0) { ChartIdx = 0; Tx = -Q.z * Inv; Ty = Q.y * Inv; }
        else            { ChartIdx = 1; Tx =  Q.z * Inv; Ty = Q.y * Inv; }
    } else if (Aq.y >= Aq.x && Aq.y >= Aq.z) {
        let Inv = 1.0 / max(Aq.y, 1e-8);
        if (Q.y >= 0.0) { ChartIdx = 2; Tx = Q.x * Inv; Ty = -Q.z * Inv; }
        else            { ChartIdx = 3; Tx = Q.x * Inv; Ty =  Q.z * Inv; }
    } else {
        let Inv = 1.0 / max(Aq.z, 1e-8);
        if (Q.z >= 0.0) { ChartIdx = 4; Tx =  Q.x * Inv; Ty = Q.y * Inv; }
        else            { ChartIdx = 5; Tx = -Q.x * Inv; Ty = Q.y * Inv; }
    }

    var Out : ConfocalSeed;
    Out.ChartIdx = ChartIdx;
    Out.Xi       = atan(Tx) * 1.2732395447;
    Out.Eta      = atan(Ty) * 1.2732395447;
    Out.RPt      = R;
    return Out;
}

fn EvalQuadRadiusOnly(ChartIdx : i32, XiIn : f32, EtaIn : f32) -> f32 {
    let KSpan = i32(Uni.Params8.x);
    let HLim  = Uni.Params8.y;
    let Xi    = clamp(XiIn,  -HLim, HLim);
    let Eta   = clamp(EtaIn, -HLim, HLim);

    let Su = ((Xi  + HLim) / (2.0 * HLim)) * f32(KSpan - 1);
    let Sv = ((Eta + HLim) / (2.0 * HLim)) * f32(KSpan - 1);
    let I0 = min(KSpan - 2, max(0, i32(floor(Su))));
    let J0 = min(KSpan - 2, max(0, i32(floor(Sv))));
    let Hu = HermiteWeights(Su - f32(I0));
    let Hv = HermiteWeights(Sv - f32(J0));

    let Row0 = ChartIdx * KSpan + J0;
    let C00  = textureLoad(QuadTex, vec2<i32>(I0,     Row0),     0);
    let C10  = textureLoad(QuadTex, vec2<i32>(I0 + 1, Row0),     0);
    let C01  = textureLoad(QuadTex, vec2<i32>(I0,     Row0 + 1), 0);
    let C11  = textureLoad(QuadTex, vec2<i32>(I0 + 1, Row0 + 1), 0);

    let R0  = Hu.x * C00.x + Hu.y * C00.y + Hu.z * C10.x + Hu.w * C10.y;
    let Rv0 = Hu.x * C00.z + Hu.y * C00.w + Hu.z * C10.z + Hu.w * C10.w;
    let R1  = Hu.x * C01.x + Hu.y * C01.y + Hu.z * C11.x + Hu.w * C11.y;
    let Rv1 = Hu.x * C01.z + Hu.y * C01.w + Hu.z * C11.z + Hu.w * C11.w;
    return Hv.x * R0 + Hv.y * Rv0 + Hv.z * R1 + Hv.w * Rv1;
}

fn EvalQuadPatchFull(ChartIdx : i32, XiIn : f32, EtaIn : f32) -> LoftPatchHit {
    let KSpan = i32(Uni.Params8.x);
    let HLim  = Uni.Params8.y;
    let Xi    = clamp(XiIn,  -HLim, HLim);
    let Eta   = clamp(EtaIn, -HLim, HLim);

    let Su  = ((Xi  + HLim) / (2.0 * HLim)) * f32(KSpan - 1);
    let Sv  = ((Eta + HLim) / (2.0 * HLim)) * f32(KSpan - 1);
    let I0  = min(KSpan - 2, max(0, i32(floor(Su))));
    let J0  = min(KSpan - 2, max(0, i32(floor(Sv))));
    let Tu  = Su - f32(I0);
    let Tv  = Sv - f32(J0);
    let Hu  = HermiteWeights(Tu);
    let Dhu = HermiteDerivWeights(Tu);
    let Hv  = HermiteWeights(Tv);
    let Dhv = HermiteDerivWeights(Tv);

    let Row0 = ChartIdx * KSpan + J0;
    let C00  = textureLoad(QuadTex, vec2<i32>(I0,     Row0),     0);
    let C10  = textureLoad(QuadTex, vec2<i32>(I0 + 1, Row0),     0);
    let C01  = textureLoad(QuadTex, vec2<i32>(I0,     Row0 + 1), 0);
    let C11  = textureLoad(QuadTex, vec2<i32>(I0 + 1, Row0 + 1), 0);

    let R0    = Hu.x  * C00.x + Hu.y  * C00.y + Hu.z  * C10.x + Hu.w  * C10.y;
    let DRu0  = Dhu.x * C00.x + Dhu.y * C00.y + Dhu.z * C10.x + Dhu.w * C10.y;
    let Rv0   = Hu.x  * C00.z + Hu.y  * C00.w + Hu.z  * C10.z + Hu.w  * C10.w;
    let DRuv0 = Dhu.x * C00.z + Dhu.y * C00.w + Dhu.z * C10.z + Dhu.w * C10.w;

    let R1    = Hu.x  * C01.x + Hu.y  * C01.y + Hu.z  * C11.x + Hu.w  * C11.y;
    let DRu1  = Dhu.x * C01.x + Dhu.y * C01.y + Dhu.z * C11.x + Dhu.w * C11.y;
    let Rv1   = Hu.x  * C01.z + Hu.y  * C01.w + Hu.z  * C11.z + Hu.w  * C11.w;
    let DRuv1 = Dhu.x * C01.z + Dhu.y * C01.w + Dhu.z * C11.z + Dhu.w * C11.w;

    let DerivScale = f32(KSpan - 1) / (2.0 * HLim);
    let R    = Hv.x * R0 + Hv.y * Rv0 + Hv.z * R1 + Hv.w * Rv1;
    let Rxi  = (Hv.x * DRu0 + Hv.y * DRuv0 + Hv.z * DRu1 + Hv.w * DRuv1) * DerivScale;
    let Reta = (Dhv.x * R0  + Dhv.y * Rv0  + Dhv.z * R1  + Dhv.w * Rv1)  * DerivScale;

    let Ray = EvalQuadChartUnitQ(ChartIdx, Xi, Eta);
    let RadVec = vec3<f32>(0.28 + R, 0.14 + R, 2.48 + R);

    var Out : LoftPatchHit;
    Out.Pos = vec3<f32>(RadVec.x * Ray.Q.x, 0.78 + RadVec.y * Ray.Q.y, RadVec.z * Ray.Q.z);
    Out.Su  = Rxi  * Ray.Q + RadVec * Ray.Qxi;
    Out.Sv  = Reta * Ray.Q + RadVec * Ray.Qeta;
    return Out;
}

fn EvalQuadWheelPairSdf(P : vec3<f32>, AxleZ : f32) -> f32 {
    let Dx  = 1.0475 - abs(P.x);
    let Dy  = P.y - 0.5086;
    let Dz  = P.z - AxleZ;
    let Rho = length(vec2<f32>(Dy, Dz));
    let Hw  = Uni.Params3.w;

    if (abs(Dx) > Hw + 0.35 && Rho > 0.86) {
        return length(vec2<f32>(abs(Dx) - Hw, max(0.0, Rho - 0.51)));
    }

    let WSamp = i32(Uni.Params8.w);
    let WheelRow = 6 * i32(Uni.Params8.x);
    let ClampedX = clamp(Dx, -Hw, Hw);
    let NormS = ((ClampedX + Hw) / (2.0 * Hw)) * f32(WSamp - 1);
    let I0 = min(WSamp - 2, max(0, i32(floor(NormS))));
    let T  = NormS - f32(I0);

    let H  = HermiteWeights(T);
    let W0 = textureLoad(QuadTex, vec2<i32>(I0, WheelRow), 0);
    let W1 = textureLoad(QuadTex, vec2<i32>(I0 + 1, WheelRow), 0);
    let ROuter = H.x * W0.x + H.y * W0.y + H.z * W1.x + H.w * W1.y;

    let DRad = Rho - ROuter;
    let DAx  = abs(Dx) - Hw;

    var DishRecess = 0.0;
    if (Rho < 0.31 && Dx < -Hw + 0.06) {
        let CenterDist = abs(Rho - 0.14);
        DishRecess = 0.032 * (1.0 - clamp(CenterDist / 0.19, 0.0, 1.0));
    }
    let EffAx = max(DAx, -(Hw + DishRecess) - Dx);
    let Outside = length(vec2<f32>(max(DRad, 0.0), max(EffAx, 0.0)));
    let Inside  = min(max(DRad, EffAx), 0.0);
    return Outside + Inside;
}

fn EvalQuadPatchVehicleSdf(P : vec3<f32>) -> f32 {
    let BMin = vec3<f32>(-1.250, 0.000, -3.110);
    let BMax = vec3<f32>( 1.250, 1.769,  3.110);
    let BoxDelta = max(max(BMin - P, vec3<f32>(0.0)), P - BMax);
    let BoxDist  = length(BoxDelta);
    if (BoxDist > 0.65) {
        return BoxDist + 0.03;
    }

    let Seed = WorldToConfocalSeed(P);
    let ChartIdx = Seed.ChartIdx;
    let HLim = Uni.Params8.y;

    var BestXi  = Seed.Xi;
    var BestEta = Seed.Eta;
    var BestSq  = 1e9;
    var DXiSeeds  = array<f32, 5>(0.0, -0.07, 0.07, 0.0, 0.0);
    var DEtaSeeds = array<f32, 5>(0.0, 0.0, 0.0, -0.07, 0.07);

    for (var S = 0; S < 5; S = S + 1) {
        let Cxi  = clamp(Seed.Xi  + DXiSeeds[S],  -HLim, HLim);
        let Ceta = clamp(Seed.Eta + DEtaSeeds[S], -HLim, HLim);
        let R    = EvalQuadRadiusOnly(ChartIdx, Cxi, Ceta);
        let Q    = EvalQuadChartUnitQ(ChartIdx, Cxi, Ceta).Q;
        let Sp   = vec3<f32>((0.28 + R) * Q.x, 0.78 + (0.14 + R) * Q.y, (2.48 + R) * Q.z);
        let Diff = P - Sp;
        let Sq   = dot(Diff, Diff);
        if (Sq < BestSq) {
            BestSq  = Sq;
            BestXi  = Cxi;
            BestEta = Ceta;
        }
    }

    var Xi  = BestXi;
    var Eta = BestEta;
    for (var Step = 0; Step < 3; Step = Step + 1) {
        let Hit = EvalQuadPatchFull(ChartIdx, Xi, Eta);
        let Res = P - Hit.Pos;
        let G1  = dot(Res, Hit.Su);
        let G2  = dot(Res, Hit.Sv);
        let A11 = dot(Hit.Su, Hit.Su) + 0.002;
        let A12 = dot(Hit.Su, Hit.Sv);
        let A22 = dot(Hit.Sv, Hit.Sv) + 0.002;
        let Det = A11 * A22 - A12 * A12;
        if (Det > 1e-6) {
            let DXi  = clamp(( A22 * G1 - A12 * G2) / Det, -0.16, 0.16);
            let DEta = clamp((-A12 * G1 + A11 * G2) / Det, -0.16, 0.16);
            Xi  = clamp(Xi  + DXi,  -HLim, HLim);
            Eta = clamp(Eta + DEta, -HLim, HLim);
        }
    }

    let FinalHit = EvalQuadPatchFull(ChartIdx, Xi, Eta);
    let SkinDist = length(P - FinalHit.Pos);
    let RSurf    = EvalQuadRadiusOnly(ChartIdx, Seed.Xi, Seed.Eta);
    var DBody    = select(SkinDist, -SkinDist, Seed.RPt <= RSurf);

    let AbsX = abs(P.x);
    let RhoFront = length(vec2<f32>(P.y - 0.5086, P.z - 1.7274));
    let DfRad = RhoFront - 0.560;
    let DfAx  = max(0.750 - AbsX, AbsX - 1.35);
    let DArchFront = length(vec2<f32>(max(DfRad, 0.0), max(DfAx, 0.0))) + min(max(DfRad, DfAx), 0.0);
    DBody = max(DBody, -DArchFront);

    let RhoRear = length(vec2<f32>(P.y - 0.5086, P.z + 1.6686));
    let DrRad = RhoRear - 0.562;
    let DrAx  = max(0.885 - AbsX, AbsX - 1.35);
    let DArchRear = length(vec2<f32>(max(DrRad, 0.0), max(DrAx, 0.0))) + min(max(DrRad, DrAx), 0.0);
    DBody = max(DBody, -DArchRear);

    let DwFront = EvalQuadWheelPairSdf(P,  1.7274);
    let DwRear  = EvalQuadWheelPairSdf(P, -1.6686);
    return min(DBody, min(DwFront, DwRear));
}

fn EvalVoxelVehicleSdf(P : vec3<f32>) -> f32 {
    let BMin = Uni.Params5.xyz;
    let BMax = Uni.Params6.xyz;
    let Over = max(max(BMin - P, vec3<f32>(0.0)), P - BMax);
    let ExtDist = length(Over);
    if (ExtDist > 0.35) {
        return ExtDist + 0.30;
    }
    let Uvw = clamp((P - BMin) / (BMax - BMin), vec3<f32>(0.0), vec3<f32>(1.0));
    return textureSampleLevel(VoxelTex, LinearSamp, Uvw, 0.0).r + ExtDist;
}

fn ResolveSdfModeForScreenX(FragX : f32) -> i32 {
    if (Uni.Params2.y > 0.5) {
        if (FragX >= (Uni.Params0.w * 0.5)) {
            return i32(round(Uni.Params8.z));
        }
        return 0;
    }
    return i32(round(Uni.Params2.x));
}

fn EvalSelectedSdf(P : vec3<f32>, SdfMode : i32) -> f32 {
    if (SdfMode == 2) {
        return EvalVoxelVehicleSdf(P);
    }
    if (SdfMode == 1) {
        return EvalLoftVehicleSdf(P);
    }
    return EvalQuadPatchVehicleSdf(P);
}

fn EvalSdfGradientNormal(P : vec3<f32>, SdfMode : i32) -> vec3<f32> {
    let Eps = select(0.0045, 0.014, SdfMode == 2);
    let Dx = EvalSelectedSdf(P + vec3<f32>(Eps, 0.0, 0.0), SdfMode) - EvalSelectedSdf(P - vec3<f32>(Eps, 0.0, 0.0), SdfMode);
    let Dy = EvalSelectedSdf(P + vec3<f32>(0.0, Eps, 0.0), SdfMode) - EvalSelectedSdf(P - vec3<f32>(0.0, Eps, 0.0), SdfMode);
    let Dz = EvalSelectedSdf(P + vec3<f32>(0.0, 0.0, Eps), SdfMode) - EvalSelectedSdf(P - vec3<f32>(0.0, 0.0, Eps), SdfMode);
    return normalize(vec3<f32>(Dx, Dy, Dz) + vec3<f32>(1e-7));
}

fn IntersectCarAabb(Ro : vec3<f32>, Rd : vec3<f32>) -> vec2<f32> {
    let BMin = vec3<f32>(-1.52, -0.05, -3.38);
    let BMax = vec3<f32>( 1.52,  2.02,  3.38);
    let SafeRd = select(min(Rd, vec3<f32>(-1e-6)), max(Rd, vec3<f32>(1e-6)), Rd >= vec3<f32>(0.0));
    let InvD = 1.0 / SafeRd;
    let T0 = (BMin - Ro) * InvD;
    let T1 = (BMax - Ro) * InvD;
    let TSmaller = min(T0, T1);
    let TBigger  = max(T0, T1);
    let TEnter = max(max(TSmaller.x, TSmaller.y), max(TSmaller.z, 0.0));
    let TExit  = min(min(TBigger.x, TBigger.y), TBigger.z);
    return vec2<f32>(TEnter, TExit);
}

fn EvalSdfSoftShadow(Origin : vec3<f32>, RayDir : vec3<f32>, SdfMode : i32) -> f32 {
    let HitBox = IntersectCarAabb(Origin, RayDir);
    if (HitBox.x >= HitBox.y) {
        return 1.0;
    }
    let KSoft = max(2.0, Uni.Params7.x);
    var T = max(0.02, HitBox.x);
    var Vis = 1.0;
    for (var Step = 0; Step < 22; Step = Step + 1) {
        if (T > HitBox.y) {
            break;
        }
        let D = EvalSelectedSdf(Origin + RayDir * T, SdfMode);
        if (D < 0.002) {
            return 0.0;
        }
        Vis = min(Vis, KSoft * D / max(T, 0.12));
        T = T + clamp(D, 0.025, 0.35);
    }
    return clamp(Vis, 0.0, 1.0);
}

fn EvalSdfUnderbodyAo(WorldPos : vec3<f32>, WorldN : vec3<f32>, SdfMode : i32) -> f32 {
    if (abs(WorldPos.x) > 1.85 || abs(WorldPos.z) > 3.65 || WorldPos.y > 1.95) {
        return 1.0;
    }
    var Occ = 0.0;
    var Weight = 1.0;
    var TapDist = array<f32, 4>(0.08, 0.20, 0.38, 0.62);
    for (var K = 0; K < 4; K = K + 1) {
        let H = TapDist[K];
        let D = EvalSelectedSdf(WorldPos + WorldN * H, SdfMode);
        Occ = Occ + max(0.0, H - D) * Weight;
        Weight = Weight * 0.62;
    }
    return clamp(1.0 - 1.55 * Occ, 0.14, 1.0);
}

fn AcesFilmicTone(Hdr : vec3<f32>) -> vec3<f32> {
    let A = 2.51;
    let B = 0.03;
    let C = 2.43;
    let D = 0.59;
    let E = 0.14;
    let Mapped = clamp((Hdr * (A * Hdr + B)) / (Hdr * (C * Hdr + D) + E), vec3<f32>(0.0), vec3<f32>(1.0));
    return pow(max(Mapped, vec3<f32>(1e-4)), vec3<f32>(1.0 / 2.2));
}
`;

export const ShowroomSurfaceWgsl = SharedWgslSdfLibrary + /* wgsl */ `
struct VsIn {
    @location(0) Pos   : vec3<f32>,
    @location(1) Norm  : vec3<f32>,
    @location(2) Tint  : vec3<f32>,
    @location(3) Props : vec2<f32>,
};

struct VsOut {
    @builtin(position) ClipPos : vec4<f32>,
    @location(0) WorldPos      : vec3<f32>,
    @location(1) WorldNorm     : vec3<f32>,
    @location(2) Tint          : vec3<f32>,
    @location(3) Props         : vec2<f32>,
};

@vertex
fn VsMain(In : VsIn) -> VsOut {
    var Out : VsOut;
    Out.ClipPos   = Uni.ViewProj * vec4<f32>(In.Pos, 1.0);
    Out.WorldPos  = In.Pos;
    Out.WorldNorm = In.Norm;
    Out.Tint      = In.Tint;
    Out.Props     = In.Props;
    return Out;
}

@fragment
fn FsMain(In : VsOut) -> @location(0) vec4<f32> {
    let Tag = In.Props.y;
    let SdfMode = ResolveSdfModeForScreenX(In.ClipPos.x);

    // Optional cutaway clipping of vehicle triangles (Tag >= 1.0 && Tag <= 5.5) when CutClipToggle is active
    if (Uni.Params7.z > 0.5 && Tag > 0.5 && Tag < 5.6) {
        let CutCoord = Uni.Params4.w;
        if (Uni.Params5.w < 0.5) {
            if (In.WorldPos.z > CutCoord) { discard; }
        } else {
            if (In.WorldPos.x > CutCoord) { discard; }
        }
    }

    // Split-view center divider hairline
    if (Uni.Params2.y > 0.5 && abs(In.ClipPos.x - Uni.Params0.w * 0.5) < 1.2) {
        return vec4<f32>(0.96, 0.78, 0.34, 1.0);
    }

    // Tag 7.0 = Interactive 3D Signed Distance Cross-Section Isoline Inspection Plane!
    if (Tag > 6.5) {
        let Sd = EvalSelectedSdf(In.WorldPos, SdfMode);
        let Wave = 0.5 + 0.5 * cos(Sd * 58.0);
        let MajorBand = smoothstep(0.92, 0.995, Wave);
        let ZeroIso   = 1.0 - smoothstep(0.002, 0.011, abs(Sd));

        let InsideCol  = mix(vec3<f32>(0.88, 0.24, 0.16), vec3<f32>(0.98, 0.68, 0.20), clamp(1.0 + Sd * 2.2, 0.0, 1.0));
        let OutsideCol = mix(vec3<f32>(0.10, 0.58, 0.86), vec3<f32>(0.04, 0.14, 0.24), clamp(Sd * 0.85, 0.0, 1.0));
        var FieldCol   = select(OutsideCol, InsideCol, Sd < 0.0);
        FieldCol = mix(FieldCol, FieldCol * 1.65 + vec3<f32>(0.12), MajorBand * 0.65);
        FieldCol = mix(FieldCol, vec3<f32>(1.0, 0.99, 0.94), ZeroIso);

        let EdgeFade = smoothstep(2.35, 1.95, In.WorldPos.y);
        let Alpha = clamp(0.76 * EdgeFade + ZeroIso * 0.24, 0.15, 0.96);
        return vec4<f32>(AcesFilmicTone(FieldCol), Alpha);
    }

    let N = normalize(In.WorldNorm);
    let V = normalize(Uni.Params0.xyz - In.WorldPos);
    let L = normalize(Uni.Params1.xyz);
    let H = normalize(V + L);

    let Rough = clamp(In.Props.x, 0.04, 1.0);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), 0.0);
    let NdotH = max(dot(N, H), 0.0);

    // Emissive LED Headlamp / Taillamp Strips
    if (Tag > 4.5 && Tag < 5.5) {
        return vec4<f32>(AcesFilmicTone(In.Tint), 1.0);
    }

    var Shadow = 1.0;
    var Ao     = 1.0;
    if (Tag < 0.5 || Tag > 5.5) {
        // Showroom Floor & Backdrop receive live SDF Soft Shadows + SDF Underbody Contact AO!
        Shadow = EvalSdfSoftShadow(In.WorldPos + N * 0.018, L, SdfMode);
        Ao     = EvalSdfUnderbodyAo(In.WorldPos, N, SdfMode);
    } else {
        Ao = clamp(0.55 + 0.45 * N.y, 0.35, 1.0);
    }

    // Showroom key + sky dome + rim light
    let SkyCol = mix(vec3<f32>(0.05, 0.07, 0.10), vec3<f32>(0.20, 0.28, 0.40), clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    let SunCol = vec3<f32>(2.65, 2.52, 2.34);

    let SpecPow = mix(220.0, 12.0, Rough * Rough);
    let SpecInt = mix(1.35, 0.08, Rough);
    let Fresnel = pow(max(1.0 - NdotV, 1e-4), 4.0);

    var Hdr = In.Tint * (SkyCol * Ao + SunCol * NdotL * Shadow * 0.88);
    Hdr = Hdr + SunCol * pow(max(NdotH, 1e-4), SpecPow) * SpecInt * Shadow;

    // Automotive clearcoat sky reflection on car body, glass, and rims
    if (Tag > 0.5 && Tag < 4.5) {
        let ReflDir = reflect(-V, N);
        let LightBar = smoothstep(0.62, 0.92, sin(ReflDir.y * 9.0 + ReflDir.x * 2.5));
        let EnvRefl = mix(vec3<f32>(0.03, 0.05, 0.08), vec3<f32>(0.75, 0.86, 1.0), clamp(ReflDir.y * 0.5 + 0.5, 0.0, 1.0))
                    + vec3<f32>(0.55, 0.62, 0.72) * LightBar * (1.0 - Rough);
        Hdr = Hdr + EnvRefl * (0.08 + 0.58 * Fresnel) * (1.0 - Rough * 0.75);
    }

    // Showroom Turntable Floor: Subtle concentric turntable rings + Live SDF Ray-Marched Vehicle Reflection!
    if (Tag < 0.5) {
        let Rad = length(In.WorldPos.xz);
        let RingLine = 1.0 - smoothstep(0.0, 0.018, abs(FractPositive(Rad * 0.5) - 0.5) * 2.0);
        Hdr = Hdr + vec3<f32>(0.020, 0.035, 0.052) * RingLine * exp(-Rad * 0.22);

        let ReflStrength = Uni.Params7.y;
        if (ReflStrength > 0.01) {
            let RDir = reflect(-V, vec3<f32>(0.0, 1.0, 0.0));
            let BoxHit = IntersectCarAabb(In.WorldPos + vec3<f32>(0.0, 0.015, 0.0), RDir);
            if (BoxHit.x < BoxHit.y) {
                var Rt = max(0.02, BoxHit.x);
                var HitCar = false;
                for (var Step = 0; Step < 18; Step = Step + 1) {
                    if (Rt > BoxHit.y) { break; }
                    let Rd = EvalSelectedSdf(In.WorldPos + RDir * Rt, SdfMode);
                    if (Rd < 0.008) {
                        HitCar = true;
                        break;
                    }
                    Rt = Rt + max(Rd, 0.025);
                }
                if (HitCar) {
                    let HitP = In.WorldPos + RDir * Rt;
                    let HitN = EvalSdfGradientNormal(HitP, SdfMode);
                    let CarCoat = select(vec3<f32>(0.045, 0.26, 0.56), vec3<f32>(0.04, 0.045, 0.055), HitP.y < 0.56);
                    let ReflLit = CarCoat * (0.28 + 0.95 * max(dot(HitN, L), 0.0)) + vec3<f32>(0.18, 0.24, 0.32) * max(HitN.y, 0.0);
                    let DistAtten = exp(-Rt * 0.42);
                    Hdr = mix(Hdr, ReflLit, clamp(ReflStrength * (0.18 + 0.55 * Fresnel) * DistAtten, 0.0, 0.68));
                }
            }
        }
    }

    return vec4<f32>(AcesFilmicTone(Hdr), 1.0);
}
`;

export const RayMarchIsoSurfaceWgsl = SharedWgslSdfLibrary + /* wgsl */ `
struct IsoVsOut {
    @builtin(position) ClipPos : vec4<f32>,
    @location(0) NdcXY         : vec2<f32>,
};

@vertex
fn VsMain(@builtin(vertex_index) Vid : u32) -> IsoVsOut {
    var Positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );
    let P = Positions[Vid];
    var Out : IsoVsOut;
    Out.ClipPos = vec4<f32>(P, 0.0, 1.0);
    Out.NdcXY   = P;
    return Out;
}

struct IsoFsOut {
    @location(0) Color        : vec4<f32>,
    @builtin(frag_depth) Depth : f32,
};

@fragment
fn FsMain(In : IsoVsOut) -> IsoFsOut {
    let CamPos = Uni.Params0.xyz;
    let FarH   = Uni.InvViewProj * vec4<f32>(In.NdcXY, 1.0, 1.0);
    let FarWorld = FarH.xyz / FarH.w;
    let RayDir = normalize(FarWorld - CamPos);

    let SdfMode = ResolveSdfModeForScreenX(In.ClipPos.x);
    let BoxHit  = IntersectCarAabb(CamPos, RayDir);
    if (BoxHit.x >= BoxHit.y) {
        discard;
    }

    var T = max(0.05, BoxHit.x);
    var Hit = false;
    var PrevT = T;
    var PrevD = 1.0;

    for (var Step = 0; Step < 46; Step = Step + 1) {
        if (T > BoxHit.y) { break; }
        let P = CamPos + RayDir * T;
        let D = EvalSelectedSdf(P, SdfMode);
        if (D < 0.0022) {
            // Linear secant refinement for crisp sub-millimeter zero-crossing
            let Denom = PrevD - D;
            if (Step > 0 && abs(Denom) > 1e-6) {
                T = clamp(PrevT + (T - PrevT) * (PrevD / Denom), PrevT, T);
            }
            Hit = true;
            break;
        }
        PrevT = T;
        PrevD = D;
        T = T + max(D * 0.92, 0.014);
    }

    if (!Hit) {
        discard;
    }

    let HitPos = CamPos + RayDir * T;

    // Optional cutaway clipping on ray-marched iso-surface
    if (Uni.Params7.z > 0.5) {
        let CutCoord = Uni.Params4.w;
        if (Uni.Params5.w < 0.5) {
            if (HitPos.z > CutCoord) { discard; }
        } else {
            if (HitPos.x > CutCoord) { discard; }
        }
    }

    let N = EvalSdfGradientNormal(HitPos, SdfMode);
    let V = -RayDir;
    let L = normalize(Uni.Params1.xyz);
    let H = normalize(V + L);

    // Region tinting (wheel vs body vs canopy) on the reconstructed SDF iso-surface
    let NearFrontWheel = length(vec2<f32>(HitPos.y - 0.5086, HitPos.z - 1.7274)) < 0.53 && abs(HitPos.x) > 0.80;
    let NearRearWheel  = length(vec2<f32>(HitPos.y - 0.5086, HitPos.z + 1.6686)) < 0.53 && abs(HitPos.x) > 0.80;
    let TouchWheel = NearFrontWheel || NearRearWheel;

    var Coat = select(vec3<f32>(0.045, 0.275, 0.590), vec3<f32>(0.020, 0.032, 0.052), HitPos.y > 1.24 && HitPos.z > -1.25 && HitPos.z < 1.10);
    if (TouchWheel) {
        let RimZone = (length(vec2<f32>(HitPos.y - 0.5086, HitPos.z - select(-1.6686, 1.7274, HitPos.z > 0.0))) < 0.34) && (abs(HitPos.x) > 1.16);
        Coat = select(vec3<f32>(0.032, 0.035, 0.040), vec3<f32>(0.66, 0.70, 0.76), RimZone);
    }

    var Hdr : vec3<f32>;
    if (Uni.Params2.w > 0.5) {
        // Showroom Class-A Zebra Reflection-Bar Surface Continuity Inspection!
        let Refl = reflect(-V, N);
        let Stripe = smoothstep(-0.12, 0.12, sin(Refl.y * 28.0 + Refl.x * 7.0 + Refl.z * 3.0));
        let LitShade = 0.22 + 0.78 * max(dot(N, L), 0.0);
        var TintBand = vec3<f32>(0.10, 0.80, 0.98); // Cyan = Closed 4-Corner C² Quad-Patch
        if (SdfMode == 1) {
            TintBand = vec3<f32>(0.96, 0.74, 0.24); // Gold = Cylindrical Polar-Fan Loft
        } else if (SdfMode == 2) {
            TintBand = vec3<f32>(0.96, 0.52, 0.28); // Coral = Uniform 3D Voxel Grid
        }
        Hdr = mix(vec3<f32>(0.025, 0.032, 0.044), TintBand * 1.55, Stripe) * LitShade;
    } else {
        let Rough = select(0.11, 0.55, TouchWheel);
        let NdotL = max(dot(N, L), 0.0);
        let NdotV = max(dot(N, V), 0.0);
        let NdotH = max(dot(N, H), 0.0);
        let Fresnel = pow(max(1.0 - NdotV, 1e-4), 4.0);
        let Refl = reflect(-V, N);
        let LightBar = smoothstep(0.55, 0.92, sin(Refl.y * 10.0 + Refl.x * 3.0));
        let EnvCol = mix(vec3<f32>(0.04, 0.06, 0.10), vec3<f32>(0.80, 0.90, 1.05), clamp(Refl.y * 0.5 + 0.5, 0.0, 1.0))
                   + vec3<f32>(0.65, 0.72, 0.82) * LightBar * (1.0 - Rough);
        let SunCol = vec3<f32>(2.65, 2.52, 2.34);
        let SpecPow = mix(200.0, 18.0, Rough);
        Hdr = Coat * (vec3<f32>(0.12, 0.16, 0.22) + SunCol * NdotL * 0.88)
            + SunCol * pow(max(NdotH, 1e-4), SpecPow) * (1.0 - Rough * 0.7)
            + EnvCol * (0.10 + 0.62 * Fresnel) * (1.0 - Rough * 0.65);
    }

    let Clip = Uni.ViewProj * vec4<f32>(HitPos, 1.0);
    var Out : IsoFsOut;
    if (Uni.Params2.y > 0.5 && abs(In.ClipPos.x - Uni.Params0.w * 0.5) < 1.2) {
        Out.Color = vec4<f32>(0.96, 0.78, 0.34, 1.0);
    } else {
        Out.Color = vec4<f32>(AcesFilmicTone(Hdr), 1.0);
    }
    Out.Depth = clamp(Clip.z / Clip.w, 0.0, 1.0);
    return Out;
}
`;

export const CageOverlayWgsl = /* wgsl */ `
struct SceneUniforms {
    ViewProj    : mat4x4<f32>,
    InvViewProj : mat4x4<f32>,
    Params0     : vec4<f32>,
    Params1     : vec4<f32>,
    Params2     : vec4<f32>,
    Params3     : vec4<f32>,
    Params4     : vec4<f32>,
    Params5     : vec4<f32>,
    Params6     : vec4<f32>,
    Params7     : vec4<f32>,
    Params8     : vec4<f32>,
};

@group(0) @binding(0) var<uniform> Uni : SceneUniforms;

struct LineVsIn {
    @location(0) Pos  : vec3<f32>,
    @location(1) Rgba : vec4<f32>,
};

struct LineVsOut {
    @builtin(position) ClipPos : vec4<f32>,
    @location(0) Rgba          : vec4<f32>,
};

@vertex
fn VsMain(In : LineVsIn) -> LineVsOut {
    var Out : LineVsOut;
    var Clip = Uni.ViewProj * vec4<f32>(In.Pos, 1.0);
    Clip.z = Clip.z - 0.00035 * Clip.w;
    Out.ClipPos = Clip;
    Out.Rgba    = In.Rgba;
    return Out;
}

@fragment
fn FsMain(In : LineVsOut) -> @location(0) vec4<f32> {
    return In.Rgba;
}
`;

//============================================================================================================================================
//                                  WEBGL 2.0 (GLSL ES 3.00) FALLBACK SHADERS (IDENTICAL MATH!)
//============================================================================================================================================

const SharedGlslSdfLibrary = /* glsl */ `
uniform mat4 uViewProj;
uniform mat4 uInvViewProj;
uniform vec4 uParams[9];
uniform highp sampler2D uLoftTex;
uniform highp sampler3D uVoxelTex;
uniform highp sampler2D uQuadTex;

float FractPositive(float X) {
    return X - floor(X);
}

float EvalSpineY(float U) {
    float Diff = (U - 0.46) / 0.28;
    return 0.78 + 0.18 * exp(-Diff * Diff);
}

float EvalSpineDerivDu(float U) {
    float Diff = (U - 0.46) / 0.28;
    return 0.18 * exp(-Diff * Diff) * (-2.0 * Diff / 0.28);
}

vec4 HermiteWeights(float T) {
    float T2 = T * T;
    float T3 = T2 * T;
    return vec4(
        2.0 * T3 - 3.0 * T2 + 1.0,
        T3 - 2.0 * T2 + T,
        -2.0 * T3 + 3.0 * T2,
        T3 - T2
    );
}

vec4 HermiteDerivWeights(float T) {
    float T2 = T * T;
    return vec4(
        6.0 * T2 - 6.0 * T,
        3.0 * T2 - 4.0 * T + 1.0,
        -6.0 * T2 + 6.0 * T,
        3.0 * T2 - 2.0 * T
    );
}

float EvalLoftRadiusOnly(float UIn, float VIn) {
    int MCount = int(uParams[3].x);
    int KCount = int(uParams[3].y);
    float U = clamp(UIn, 0.0, 1.0);
    float V = FractPositive(VIn);

    float ScaledU = U * float(MCount - 1);
    int I0 = min(MCount - 2, max(0, int(floor(ScaledU))));
    int I1 = I0 + 1;
    float Tu = ScaledU - float(I0);

    float ScaledV = V * float(KCount);
    int J0 = int(floor(ScaledV)) % KCount;
    int J1 = (J0 + 1) % KCount;
    float Tv = ScaledV - floor(ScaledV);

    vec4 Hu = HermiteWeights(Tu);
    vec4 Hv = HermiteWeights(Tv);

    vec4 C00 = texelFetch(uLoftTex, ivec2(J0, I0), 0);
    vec4 C10 = texelFetch(uLoftTex, ivec2(J0, I1), 0);
    vec4 C01 = texelFetch(uLoftTex, ivec2(J1, I0), 0);
    vec4 C11 = texelFetch(uLoftTex, ivec2(J1, I1), 0);

    float RJ0  = Hu.x * C00.x + Hu.y * C00.y + Hu.z * C10.x + Hu.w * C10.y;
    float RvJ0 = Hu.x * C00.z + Hu.y * C00.w + Hu.z * C10.z + Hu.w * C10.w;
    float RJ1  = Hu.x * C01.x + Hu.y * C01.y + Hu.z * C11.x + Hu.w * C11.y;
    float RvJ1 = Hu.x * C01.z + Hu.y * C01.w + Hu.z * C11.z + Hu.w * C11.w;

    return Hv.x * RJ0 + Hv.y * RvJ0 + Hv.z * RJ1 + Hv.w * RvJ1;
}

void EvalLoftPatchFull(float UIn, float VIn, out vec3 OutPos, out vec3 OutSu, out vec3 OutSv) {
    int MCount = int(uParams[3].x);
    int KCount = int(uParams[3].y);
    float U = clamp(UIn, 0.0, 1.0);
    float V = FractPositive(VIn);

    float ScaledU = U * float(MCount - 1);
    int I0 = min(MCount - 2, max(0, int(floor(ScaledU))));
    int I1 = I0 + 1;
    float Tu = ScaledU - float(I0);

    float ScaledV = V * float(KCount);
    int J0 = int(floor(ScaledV)) % KCount;
    int J1 = (J0 + 1) % KCount;
    float Tv = ScaledV - floor(ScaledV);

    vec4 Hu  = HermiteWeights(Tu);
    vec4 Dhu = HermiteDerivWeights(Tu);
    vec4 Hv  = HermiteWeights(Tv);
    vec4 Dhv = HermiteDerivWeights(Tv);

    vec4 C00 = texelFetch(uLoftTex, ivec2(J0, I0), 0);
    vec4 C10 = texelFetch(uLoftTex, ivec2(J0, I1), 0);
    vec4 C01 = texelFetch(uLoftTex, ivec2(J1, I0), 0);
    vec4 C11 = texelFetch(uLoftTex, ivec2(J1, I1), 0);

    float RJ0   = Hu.x  * C00.x + Hu.y  * C00.y + Hu.z  * C10.x + Hu.w  * C10.y;
    float DRuJ0 = Dhu.x * C00.x + Dhu.y * C00.y + Dhu.z * C10.x + Dhu.w * C10.y;
    float RvJ0  = Hu.x  * C00.z + Hu.y  * C00.w + Hu.z  * C10.z + Hu.w  * C10.w;
    float DRuv0 = Dhu.x * C00.z + Dhu.y * C00.w + Dhu.z * C10.z + Dhu.w * C10.w;

    float RJ1   = Hu.x  * C01.x + Hu.y  * C01.y + Hu.z  * C11.x + Hu.w  * C11.y;
    float DRuJ1 = Dhu.x * C01.x + Dhu.y * C01.y + Dhu.z * C11.x + Dhu.w * C11.y;
    float RvJ1  = Hu.x  * C01.z + Hu.y  * C01.w + Hu.z  * C11.z + Hu.w  * C11.w;
    float DRuv1 = Dhu.x * C01.z + Dhu.y * C01.w + Dhu.z * C11.z + Dhu.w * C11.w;

    float R  = Hv.x * RJ0 + Hv.y * RvJ0 + Hv.z * RJ1 + Hv.w * RvJ1;
    float Ru = (Hv.x * DRuJ0 + Hv.y * DRuv0 + Hv.z * DRuJ1 + Hv.w * DRuv1) * float(MCount - 1);
    float Rv = (Dhv.x * RJ0 + Dhv.y * RvJ0 + Dhv.z * RJ1 + Dhv.w * RvJ1) * float(KCount);

    float TwoPi = 6.283185307;
    float Theta = V * TwoPi;
    float CosT  = cos(Theta);
    float SinT  = sin(Theta);
    float Yc    = EvalSpineY(U);
    float DycDu = EvalSpineDerivDu(U);

    OutPos = vec3(R * CosT, Yc + R * SinT, uParams[4].x + uParams[4].z * U);
    OutSu  = vec3(Ru * CosT, DycDu + Ru * SinT, uParams[4].z);
    OutSv  = vec3(Rv * CosT - R * TwoPi * SinT, Rv * SinT + R * TwoPi * CosT, 0.0);
}

float EvalSymmetricWheelPairSdf(vec3 P, float AxleZ) {
    float Dx  = 1.0475 - abs(P.x);
    float Dy  = P.y - 0.5086;
    float Dz  = P.z - AxleZ;
    float Rho = length(vec2(Dy, Dz));
    float Hw  = uParams[3].w;

    if (abs(Dx) > Hw + 0.35 && Rho > 0.86) {
        return length(vec2(abs(Dx) - Hw, max(0.0, Rho - 0.51)));
    }

    int WSamp = int(uParams[3].z);
    int WheelRow = int(uParams[3].x);
    float ClampedX = clamp(Dx, -Hw, Hw);
    float NormS = ((ClampedX + Hw) / (2.0 * Hw)) * float(WSamp - 1);
    int I0 = min(WSamp - 2, max(0, int(floor(NormS))));
    float T = NormS - float(I0);

    vec4 H  = HermiteWeights(T);
    vec4 W0 = texelFetch(uLoftTex, ivec2(I0, WheelRow), 0);
    vec4 W1 = texelFetch(uLoftTex, ivec2(I0 + 1, WheelRow), 0);
    float ROuter = H.x * W0.x + H.y * W0.y + H.z * W1.x + H.w * W1.y;

    float DRad = Rho - ROuter;
    float DAx  = abs(Dx) - Hw;

    float DishRecess = 0.0;
    if (Rho < 0.31 && Dx < -Hw + 0.06) {
        float CenterDist = abs(Rho - 0.14);
        DishRecess = 0.032 * (1.0 - clamp(CenterDist / 0.19, 0.0, 1.0));
    }
    float EffAx = max(DAx, -(Hw + DishRecess) - Dx);
    float Outside = length(vec2(max(DRad, 0.0), max(EffAx, 0.0)));
    float Inside  = min(max(DRad, EffAx), 0.0);
    return Outside + Inside;
}

float EvalLoftVehicleSdf(vec3 P) {
    vec3 BMin = vec3(-1.250, 0.000, -3.110);
    vec3 BMax = vec3( 1.250, 1.769,  3.110);
    vec3 BoxDelta = max(max(BMin - P, vec3(0.0)), P - BMax);
    float BoxDist = length(BoxDelta);
    if (BoxDist > 0.65) {
        return BoxDist + 0.03;
    }

    float ZMin  = uParams[4].x;
    float ZMax  = uParams[4].y;
    float ZSpan = uParams[4].z;

    float URaw = (P.z - ZMin) / ZSpan;
    float U0   = clamp(URaw, 0.0, 1.0);
    float Yc0  = EvalSpineY(U0);
    float V0   = FractPositive(atan(P.y - Yc0, P.x) / 6.283185307);

    float BestU = U0;
    float BestV = V0;
    float BestSq = 1e9;
    float DuSeeds[5] = float[5](0.0, -0.045, 0.045, 0.0, 0.0);
    float DvSeeds[5] = float[5](0.0, 0.0, 0.0, -0.055, 0.055);

    for (int S = 0; S < 5; ++S) {
        float Cu = clamp(U0 + DuSeeds[S], 0.0, 1.0);
        float Cv = FractPositive(V0 + DvSeeds[S]);
        float R  = EvalLoftRadiusOnly(Cu, Cv);
        float Th = Cv * 6.283185307;
        vec3 Sp = vec3(R * cos(Th), EvalSpineY(Cu) + R * sin(Th), ZMin + ZSpan * Cu);
        vec3 Diff = P - Sp;
        float Sq = dot(Diff, Diff);
        if (Sq < BestSq) {
            BestSq = Sq;
            BestU  = Cu;
            BestV  = Cv;
        }
    }

    float U = BestU;
    float V = BestV;
    vec3 HitPos, HitSu, HitSv;

    for (int Step = 0; Step < 2; ++Step) {
        EvalLoftPatchFull(U, V, HitPos, HitSu, HitSv);
        vec3 Res = P - HitPos;
        float Gu = dot(Res, HitSu);
        float Gv = dot(Res, HitSv);
        float A11 = dot(HitSu, HitSu) + 0.002;
        float A12 = dot(HitSu, HitSv);
        float A22 = dot(HitSv, HitSv) + 0.002;
        float Det = A11 * A22 - A12 * A12;
        if (Det > 1e-6) {
            float Du = clamp(( A22 * Gu - A12 * Gv) / Det, -0.09, 0.09);
            float Dv = clamp((-A12 * Gu + A11 * Gv) / Det, -0.09, 0.09);
            U = clamp(U + Du, 0.0, 1.0);
            V = FractPositive(V + Dv);
        }
    }

    EvalLoftPatchFull(U, V, HitPos, HitSu, HitSv);
    float SkinDist = length(P - HitPos);

    float RhoAtZ = length(vec2(P.x, P.y - Yc0));
    float RAtZ   = EvalLoftRadiusOnly(U0, V0);

    float DBody = SkinDist;
    if (P.z >= ZMin && P.z <= ZMax && RhoAtZ <= RAtZ) {
        float CapClear = min(P.z - ZMin, ZMax - P.z);
        DBody = -min(SkinDist, CapClear);
    } else if ((P.z < ZMin || P.z > ZMax) && RhoAtZ <= RAtZ) {
        float AxialOver = (P.z < ZMin) ? (ZMin - P.z) : (P.z - ZMax);
        DBody = min(SkinDist, AxialOver);
    }

    float AbsX = abs(P.x);
    float RhoFront = length(vec2(P.y - 0.5086, P.z - 1.7274));
    float DfRad = RhoFront - 0.560;
    float DfAx  = max(0.750 - AbsX, AbsX - 1.35);
    float DArchFront = length(vec2(max(DfRad, 0.0), max(DfAx, 0.0))) + min(max(DfRad, DfAx), 0.0);
    DBody = max(DBody, -DArchFront);

    float RhoRear = length(vec2(P.y - 0.5086, P.z + 1.6686));
    float DrRad = RhoRear - 0.562;
    float DrAx  = max(0.885 - AbsX, AbsX - 1.35);
    float DArchRear = length(vec2(max(DrRad, 0.0), max(DrAx, 0.0))) + min(max(DrRad, DrAx), 0.0);
    DBody = max(DBody, -DArchRear);

    float DwFront = EvalSymmetricWheelPairSdf(P,  1.7274);
    float DwRear  = EvalSymmetricWheelPairSdf(P, -1.6686);
    return min(DBody, min(DwFront, DwRear));
}

struct QuadUnitRay {
    vec3 Q;
    vec3 Qxi;
    vec3 Qeta;
};

QuadUnitRay EvalQuadChartUnitQ(int ChartIdx, float Xi, float Eta) {
    float QuarterPi = 0.7853981634;
    float Cx  = cos(Xi * QuarterPi);
    float Cy  = cos(Eta * QuarterPi);
    float Tx  = sin(Xi * QuarterPi) / Cx;
    float Ty  = sin(Eta * QuarterPi) / Cy;
    float DTx = QuarterPi / (Cx * Cx);
    float DTy = QuarterPi / (Cy * Cy);

    vec3 U    = vec3(0.0);
    vec3 Dxi  = vec3(0.0);
    vec3 Deta = vec3(0.0);

    if (ChartIdx == 0) {
        U = vec3(1.0, Ty, -Tx);
        Dxi = vec3(0.0, 0.0, -DTx);
        Deta = vec3(0.0, DTy, 0.0);
    } else if (ChartIdx == 1) {
        U = vec3(-1.0, Ty, Tx);
        Dxi = vec3(0.0, 0.0, DTx);
        Deta = vec3(0.0, DTy, 0.0);
    } else if (ChartIdx == 2) {
        U = vec3(Tx, 1.0, -Ty);
        Dxi = vec3(DTx, 0.0, 0.0);
        Deta = vec3(0.0, 0.0, -DTy);
    } else if (ChartIdx == 3) {
        U = vec3(Tx, -1.0, Ty);
        Dxi = vec3(DTx, 0.0, 0.0);
        Deta = vec3(0.0, 0.0, DTy);
    } else if (ChartIdx == 4) {
        U = vec3(Tx, Ty, 1.0);
        Dxi = vec3(DTx, 0.0, 0.0);
        Deta = vec3(0.0, DTy, 0.0);
    } else {
        U = vec3(-Tx, Ty, -1.0);
        Dxi = vec3(-DTx, 0.0, 0.0);
        Deta = vec3(0.0, DTy, 0.0);
    }

    float InvL = inversesqrt(max(dot(U, U), 1e-12));
    vec3 Q     = U * InvL;
    QuadUnitRay Out;
    Out.Q    = Q;
    Out.Qxi  = (Dxi  - Q * dot(Q, Dxi))  * InvL;
    Out.Qeta = (Deta - Q * dot(Q, Deta)) * InvL;
    return Out;
}

struct ConfocalSeed {
    int   ChartIdx;
    float Xi;
    float Eta;
    float RPt;
};

ConfocalSeed WorldToConfocalSeed(vec3 P) {
    float Ax = 0.28;
    float Ay = 0.14;
    float Az = 2.48;
    float Y0 = 0.78;
    float Dx = P.x;
    float Dy = P.y - Y0;
    float Dz = P.z;

    float R = max(0.05, length(vec3(Dx / (Ax + 0.65), Dy / (Ay + 0.65), Dz / (Az + 0.65))) * 0.65);
    for (int It = 0; It < 4; ++It) {
        float Rx = Ax + R;
        float Ry = Ay + R;
        float Rz = Az + R;
        float Ux = Dx / Rx;
        float Uy = Dy / Ry;
        float Uz = Dz / Rz;
        float Res = Ux * Ux + Uy * Uy + Uz * Uz - 1.0;
        float Der = -2.0 * (Ux * Ux / Rx + Uy * Uy / Ry + Uz * Uz / Rz);
        R = max(-0.12, R - Res / min(Der, -1e-6));
    }

    float Rx = max(0.02, Ax + R);
    float Ry = max(0.02, Ay + R);
    float Rz = max(0.02, Az + R);
    vec3 Q   = normalize(vec3(Dx / Rx, Dy / Ry, Dz / Rz));
    vec3 Aq  = abs(Q);

    int ChartIdx = 0;
    float Tx = 0.0;
    float Ty = 0.0;
    if (Aq.x >= Aq.y && Aq.x >= Aq.z) {
        float Inv = 1.0 / max(Aq.x, 1e-8);
        if (Q.x >= 0.0) { ChartIdx = 0; Tx = -Q.z * Inv; Ty = Q.y * Inv; }
        else            { ChartIdx = 1; Tx =  Q.z * Inv; Ty = Q.y * Inv; }
    } else if (Aq.y >= Aq.x && Aq.y >= Aq.z) {
        float Inv = 1.0 / max(Aq.y, 1e-8);
        if (Q.y >= 0.0) { ChartIdx = 2; Tx = Q.x * Inv; Ty = -Q.z * Inv; }
        else            { ChartIdx = 3; Tx = Q.x * Inv; Ty =  Q.z * Inv; }
    } else {
        float Inv = 1.0 / max(Aq.z, 1e-8);
        if (Q.z >= 0.0) { ChartIdx = 4; Tx =  Q.x * Inv; Ty = Q.y * Inv; }
        else            { ChartIdx = 5; Tx = -Q.x * Inv; Ty = Q.y * Inv; }
    }

    ConfocalSeed Out;
    Out.ChartIdx = ChartIdx;
    Out.Xi       = atan(Tx) * 1.2732395447;
    Out.Eta      = atan(Ty) * 1.2732395447;
    Out.RPt      = R;
    return Out;
}

float EvalQuadRadiusOnly(int ChartIdx, float XiIn, float EtaIn) {
    int KSpan  = int(uParams[8].x);
    float HLim = uParams[8].y;
    float Xi   = clamp(XiIn,  -HLim, HLim);
    float Eta  = clamp(EtaIn, -HLim, HLim);

    float Su = ((Xi  + HLim) / (2.0 * HLim)) * float(KSpan - 1);
    float Sv = ((Eta + HLim) / (2.0 * HLim)) * float(KSpan - 1);
    int I0   = min(KSpan - 2, max(0, int(floor(Su))));
    int J0   = min(KSpan - 2, max(0, int(floor(Sv))));
    vec4 Hu  = HermiteWeights(Su - float(I0));
    vec4 Hv  = HermiteWeights(Sv - float(J0));

    int Row0 = ChartIdx * KSpan + J0;
    vec4 C00 = texelFetch(uQuadTex, ivec2(I0,     Row0),     0);
    vec4 C10 = texelFetch(uQuadTex, ivec2(I0 + 1, Row0),     0);
    vec4 C01 = texelFetch(uQuadTex, ivec2(I0,     Row0 + 1), 0);
    vec4 C11 = texelFetch(uQuadTex, ivec2(I0 + 1, Row0 + 1), 0);

    float R0  = Hu.x * C00.x + Hu.y * C00.y + Hu.z * C10.x + Hu.w * C10.y;
    float Rv0 = Hu.x * C00.z + Hu.y * C00.w + Hu.z * C10.z + Hu.w * C10.w;
    float R1  = Hu.x * C01.x + Hu.y * C01.y + Hu.z * C11.x + Hu.w * C11.y;
    float Rv1 = Hu.x * C01.z + Hu.y * C01.w + Hu.z * C11.z + Hu.w * C11.w;
    return Hv.x * R0 + Hv.y * Rv0 + Hv.z * R1 + Hv.w * Rv1;
}

LoftPatchHit EvalQuadPatchFull(int ChartIdx, float XiIn, float EtaIn) {
    int KSpan  = int(uParams[8].x);
    float HLim = uParams[8].y;
    float Xi   = clamp(XiIn,  -HLim, HLim);
    float Eta  = clamp(EtaIn, -HLim, HLim);

    float Su  = ((Xi  + HLim) / (2.0 * HLim)) * float(KSpan - 1);
    float Sv  = ((Eta + HLim) / (2.0 * HLim)) * float(KSpan - 1);
    int I0    = min(KSpan - 2, max(0, int(floor(Su))));
    int J0    = min(KSpan - 2, max(0, int(floor(Sv))));
    float Tu  = Su - float(I0);
    float Tv  = Sv - float(J0);
    vec4 Hu   = HermiteWeights(Tu);
    vec4 Dhu  = HermiteDerivWeights(Tu);
    vec4 Hv   = HermiteWeights(Tv);
    vec4 Dhv  = HermiteDerivWeights(Tv);

    int Row0 = ChartIdx * KSpan + J0;
    vec4 C00 = texelFetch(uQuadTex, ivec2(I0,     Row0),     0);
    vec4 C10 = texelFetch(uQuadTex, ivec2(I0 + 1, Row0),     0);
    vec4 C01 = texelFetch(uQuadTex, ivec2(I0,     Row0 + 1), 0);
    vec4 C11 = texelFetch(uQuadTex, ivec2(I0 + 1, Row0 + 1), 0);

    float R0    = Hu.x  * C00.x + Hu.y  * C00.y + Hu.z  * C10.x + Hu.w  * C10.y;
    float DRu0  = Dhu.x * C00.x + Dhu.y * C00.y + Dhu.z * C10.x + Dhu.w * C10.y;
    float Rv0   = Hu.x  * C00.z + Hu.y  * C00.w + Hu.z  * C10.z + Hu.w  * C10.w;
    float DRuv0 = Dhu.x * C00.z + Dhu.y * C00.w + Dhu.z * C10.z + Dhu.w * C10.w;

    float R1    = Hu.x  * C01.x + Hu.y  * C01.y + Hu.z  * C11.x + Hu.w  * C11.y;
    float DRu1  = Dhu.x * C01.x + Dhu.y * C01.y + Dhu.z * C11.x + Dhu.w * C11.y;
    float Rv1   = Hu.x  * C01.z + Hu.y  * C01.w + Hu.z  * C11.z + Hu.w  * C11.w;
    float DRuv1 = Dhu.x * C01.z + Dhu.y * C01.w + Dhu.z * C11.z + Dhu.w * C11.w;

    float DerivScale = float(KSpan - 1) / (2.0 * HLim);
    float R    = Hv.x * R0 + Hv.y * Rv0 + Hv.z * R1 + Hv.w * Rv1;
    float Rxi  = (Hv.x * DRu0 + Hv.y * DRuv0 + Hv.z * DRu1 + Hv.w * DRuv1) * DerivScale;
    float Reta = (Dhv.x * R0  + Dhv.y * Rv0  + Dhv.z * R1  + Dhv.w * Rv1)  * DerivScale;

    QuadUnitRay Ray = EvalQuadChartUnitQ(ChartIdx, Xi, Eta);
    vec3 RadVec = vec3(0.28 + R, 0.14 + R, 2.48 + R);

    LoftPatchHit Out;
    Out.Pos = vec3(RadVec.x * Ray.Q.x, 0.78 + RadVec.y * Ray.Q.y, RadVec.z * Ray.Q.z);
    Out.Su  = Rxi  * Ray.Q + RadVec * Ray.Qxi;
    Out.Sv  = Reta * Ray.Q + RadVec * Ray.Qeta;
    return Out;
}

float EvalQuadWheelPairSdf(vec3 P, float AxleZ) {
    float Dx  = 1.0475 - abs(P.x);
    float Dy  = P.y - 0.5086;
    float Dz  = P.z - AxleZ;
    float Rho = length(vec2(Dy, Dz));
    float Hw  = uParams[3].w;

    if (abs(Dx) > Hw + 0.35 && Rho > 0.86) {
        return length(vec2(abs(Dx) - Hw, max(0.0, Rho - 0.51)));
    }

    int WSamp = int(uParams[8].w);
    int WheelRow = 6 * int(uParams[8].x);
    float ClampedX = clamp(Dx, -Hw, Hw);
    float NormS = ((ClampedX + Hw) / (2.0 * Hw)) * float(WSamp - 1);
    int I0 = min(WSamp - 2, max(0, int(floor(NormS))));
    float T = NormS - float(I0);

    vec4 H  = HermiteWeights(T);
    vec4 W0 = texelFetch(uQuadTex, ivec2(I0, WheelRow), 0);
    vec4 W1 = texelFetch(uQuadTex, ivec2(I0 + 1, WheelRow), 0);
    float ROuter = H.x * W0.x + H.y * W0.y + H.z * W1.x + H.w * W1.y;

    float DRad = Rho - ROuter;
    float DAx  = abs(Dx) - Hw;

    float DishRecess = 0.0;
    if (Rho < 0.31 && Dx < -Hw + 0.06) {
        float CenterDist = abs(Rho - 0.14);
        DishRecess = 0.032 * (1.0 - clamp(CenterDist / 0.19, 0.0, 1.0));
    }
    float EffAx = max(DAx, -(Hw + DishRecess) - Dx);
    float Outside = length(vec2(max(DRad, 0.0), max(EffAx, 0.0)));
    float Inside  = min(max(DRad, EffAx), 0.0);
    return Outside + Inside;
}

float EvalQuadPatchVehicleSdf(vec3 P) {
    vec3 BMin = vec3(-1.250, 0.000, -3.110);
    vec3 BMax = vec3( 1.250, 1.769,  3.110);
    vec3 BoxDelta = max(max(BMin - P, vec3(0.0)), P - BMax);
    float BoxDist = length(BoxDelta);
    if (BoxDist > 0.65) {
        return BoxDist + 0.03;
    }

    ConfocalSeed Seed = WorldToConfocalSeed(P);
    int ChartIdx = Seed.ChartIdx;
    float HLim   = uParams[8].y;

    float BestXi  = Seed.Xi;
    float BestEta = Seed.Eta;
    float BestSq  = 1e9;
    float DXiSeeds[5]  = float[5](0.0, -0.07, 0.07, 0.0, 0.0);
    float DEtaSeeds[5] = float[5](0.0, 0.0, 0.0, -0.07, 0.07);

    for (int S = 0; S < 5; ++S) {
        float Cxi  = clamp(Seed.Xi  + DXiSeeds[S],  -HLim, HLim);
        float Ceta = clamp(Seed.Eta + DEtaSeeds[S], -HLim, HLim);
        float R    = EvalQuadRadiusOnly(ChartIdx, Cxi, Ceta);
        vec3 Q     = EvalQuadChartUnitQ(ChartIdx, Cxi, Ceta).Q;
        vec3 Sp    = vec3((0.28 + R) * Q.x, 0.78 + (0.14 + R) * Q.y, (2.48 + R) * Q.z);
        vec3 Diff  = P - Sp;
        float Sq   = dot(Diff, Diff);
        if (Sq < BestSq) {
            BestSq  = Sq;
            BestXi  = Cxi;
            BestEta = Ceta;
        }
    }

    float Xi  = BestXi;
    float Eta = BestEta;
    for (int Step = 0; Step < 3; ++Step) {
        LoftPatchHit Hit = EvalQuadPatchFull(ChartIdx, Xi, Eta);
        vec3 Res  = P - Hit.Pos;
        float G1  = dot(Res, Hit.Su);
        float G2  = dot(Res, Hit.Sv);
        float A11 = dot(Hit.Su, Hit.Su) + 0.002;
        float A12 = dot(Hit.Su, Hit.Sv);
        float A22 = dot(Hit.Sv, Hit.Sv) + 0.002;
        float Det = A11 * A22 - A12 * A12;
        if (Det > 1e-6) {
            float DXi  = clamp(( A22 * G1 - A12 * G2) / Det, -0.16, 0.16);
            float DEta = clamp((-A12 * G1 + A11 * G2) / Det, -0.16, 0.16);
            Xi  = clamp(Xi  + DXi,  -HLim, HLim);
            Eta = clamp(Eta + DEta, -HLim, HLim);
        }
    }

    LoftPatchHit FinalHit = EvalQuadPatchFull(ChartIdx, Xi, Eta);
    float SkinDist = length(P - FinalHit.Pos);
    float RSurf    = EvalQuadRadiusOnly(ChartIdx, Seed.Xi, Seed.Eta);
    float DBody    = (Seed.RPt <= RSurf) ? -SkinDist : SkinDist;

    float AbsX = abs(P.x);
    float RhoFront = length(vec2(P.y - 0.5086, P.z - 1.7274));
    float DfRad = RhoFront - 0.560;
    float DfAx  = max(0.750 - AbsX, AbsX - 1.35);
    float DArchFront = length(vec2(max(DfRad, 0.0), max(DfAx, 0.0))) + min(max(DfRad, DfAx), 0.0);
    DBody = max(DBody, -DArchFront);

    float RhoRear = length(vec2(P.y - 0.5086, P.z + 1.6686));
    float DrRad = RhoRear - 0.562;
    float DrAx  = max(0.885 - AbsX, AbsX - 1.35);
    float DArchRear = length(vec2(max(DrRad, 0.0), max(DrAx, 0.0))) + min(max(DrRad, DrAx), 0.0);
    DBody = max(DBody, -DArchRear);

    float DwFront = EvalQuadWheelPairSdf(P,  1.7274);
    float DwRear  = EvalQuadWheelPairSdf(P, -1.6686);
    return min(DBody, min(DwFront, DwRear));
}

float EvalVoxelVehicleSdf(vec3 P) {
    vec3 BMin = uParams[5].xyz;
    vec3 BMax = uParams[6].xyz;
    vec3 Over = max(max(BMin - P, vec3(0.0)), P - BMax);
    float ExtDist = length(Over);
    if (ExtDist > 0.35) {
        return ExtDist + 0.30;
    }
    vec3 Uvw = clamp((P - BMin) / (BMax - BMin), vec3(0.0), vec3(1.0));
    return textureLod(uVoxelTex, Uvw, 0.0).r + ExtDist;
}

int ResolveSdfModeForScreenX(float FragX) {
    if (uParams[2].y > 0.5) {
        return (FragX >= (uParams[0].w * 0.5)) ? int(round(uParams[8].z)) : 0;
    }
    return int(round(uParams[2].x));
}

float EvalSelectedSdf(vec3 P, int SdfMode) {
    if (SdfMode == 2) return EvalVoxelVehicleSdf(P);
    if (SdfMode == 1) return EvalLoftVehicleSdf(P);
    return EvalQuadPatchVehicleSdf(P);
}

vec3 EvalSdfGradientNormal(vec3 P, int SdfMode) {
    float Eps = (SdfMode == 2) ? 0.014 : 0.0045;
    float Dx = EvalSelectedSdf(P + vec3(Eps, 0.0, 0.0), SdfMode) - EvalSelectedSdf(P - vec3(Eps, 0.0, 0.0), SdfMode);
    float Dy = EvalSelectedSdf(P + vec3(0.0, Eps, 0.0), SdfMode) - EvalSelectedSdf(P - vec3(0.0, Eps, 0.0), SdfMode);
    float Dz = EvalSelectedSdf(P + vec3(0.0, 0.0, Eps), SdfMode) - EvalSelectedSdf(P - vec3(0.0, 0.0, Eps), SdfMode);
    return normalize(vec3(Dx, Dy, Dz) + vec3(1e-7));
}

vec2 IntersectCarAabb(vec3 Ro, vec3 Rd) {
    vec3 BMin = vec3(-1.52, -0.05, -3.38);
    vec3 BMax = vec3( 1.52,  2.02,  3.38);
    vec3 SafeRd = vec3(
        Rd.x >= 0.0 ? max(Rd.x, 1e-6) : min(Rd.x, -1e-6),
        Rd.y >= 0.0 ? max(Rd.y, 1e-6) : min(Rd.y, -1e-6),
        Rd.z >= 0.0 ? max(Rd.z, 1e-6) : min(Rd.z, -1e-6)
    );
    vec3 InvD = 1.0 / SafeRd;
    vec3 T0 = (BMin - Ro) * InvD;
    vec3 T1 = (BMax - Ro) * InvD;
    vec3 TSmaller = min(T0, T1);
    vec3 TBigger  = max(T0, T1);
    float TEnter = max(max(TSmaller.x, TSmaller.y), max(TSmaller.z, 0.0));
    float TExit  = min(min(TBigger.x, TBigger.y), TBigger.z);
    return vec2(TEnter, TExit);
}

float EvalSdfSoftShadow(vec3 Origin, vec3 RayDir, int SdfMode) {
    vec2 HitBox = IntersectCarAabb(Origin, RayDir);
    if (HitBox.x >= HitBox.y) return 1.0;
    float KSoft = max(2.0, uParams[7].x);
    float T = max(0.02, HitBox.x);
    float Vis = 1.0;
    for (int Step = 0; Step < 22; ++Step) {
        if (T > HitBox.y) break;
        float D = EvalSelectedSdf(Origin + RayDir * T, SdfMode);
        if (D < 0.002) return 0.0;
        Vis = min(Vis, KSoft * D / max(T, 0.12));
        T += clamp(D, 0.025, 0.35);
    }
    return clamp(Vis, 0.0, 1.0);
}

float EvalSdfUnderbodyAo(vec3 WorldPos, vec3 WorldN, int SdfMode) {
    if (abs(WorldPos.x) > 1.85 || abs(WorldPos.z) > 3.65 || WorldPos.y > 1.95) return 1.0;
    float Occ = 0.0;
    float Weight = 1.0;
    float TapDist[4] = float[4](0.08, 0.20, 0.38, 0.62);
    for (int K = 0; K < 4; ++K) {
        float H = TapDist[K];
        float D = EvalSelectedSdf(WorldPos + WorldN * H, SdfMode);
        Occ += max(0.0, H - D) * Weight;
        Weight *= 0.62;
    }
    return clamp(1.0 - 1.55 * Occ, 0.14, 1.0);
}

vec3 AcesFilmicTone(vec3 Hdr) {
    float A = 2.51;
    float B = 0.03;
    float C = 2.43;
    float D = 0.59;
    float E = 0.14;
    vec3 Mapped = clamp((Hdr * (A * Hdr + B)) / (Hdr * (C * Hdr + D) + E), vec3(0.0), vec3(1.0));
    return pow(max(Mapped, vec3(1e-4)), vec3(1.0 / 2.2));
}
`;

export const ShowroomSurfaceGlslVert = /* glsl */ `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec3 aTint;
layout(location = 3) in vec2 aProps;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vWorldNorm;
out vec3 vTint;
out vec2 vProps;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vWorldPos  = aPos;
    vWorldNorm = aNorm;
    vTint      = aTint;
    vProps     = aProps;
}
`;

export const ShowroomSurfaceGlslFrag = /* glsl */ `#version 300 es
precision highp float;
precision highp sampler2D;
precision highp sampler3D;

in vec3 vWorldPos;
in vec3 vWorldNorm;
in vec3 vTint;
in vec2 vProps;

out vec4 oFragColor;
` + SharedGlslSdfLibrary + /* glsl */ `
void main() {
    float Tag = vProps.y;
    int SdfMode = ResolveSdfModeForScreenX(gl_FragCoord.x);

    if (uParams[7].z > 0.5 && Tag > 0.5 && Tag < 5.6) {
        float CutCoord = uParams[4].w;
        if (uParams[5].w < 0.5) {
            if (vWorldPos.z > CutCoord) discard;
        } else {
            if (vWorldPos.x > CutCoord) discard;
        }
    }

    if (uParams[2].y > 0.5 && abs(gl_FragCoord.x - uParams[0].w * 0.5) < 1.2) {
        oFragColor = vec4(0.96, 0.78, 0.34, 1.0);
        return;
    }

    if (Tag > 6.5) {
        float Sd = EvalSelectedSdf(vWorldPos, SdfMode);
        float Wave = 0.5 + 0.5 * cos(Sd * 58.0);
        float MajorBand = smoothstep(0.92, 0.995, Wave);
        float ZeroIso   = 1.0 - smoothstep(0.002, 0.011, abs(Sd));

        vec3 InsideCol  = mix(vec3(0.88, 0.24, 0.16), vec3(0.98, 0.68, 0.20), clamp(1.0 + Sd * 2.2, 0.0, 1.0));
        vec3 OutsideCol = mix(vec3(0.10, 0.58, 0.86), vec3(0.04, 0.14, 0.24), clamp(Sd * 0.85, 0.0, 1.0));
        vec3 FieldCol   = (Sd < 0.0) ? InsideCol : OutsideCol;
        FieldCol = mix(FieldCol, FieldCol * 1.65 + vec3(0.12), MajorBand * 0.65);
        FieldCol = mix(FieldCol, vec3(1.0, 0.99, 0.94), ZeroIso);

        float EdgeFade = smoothstep(2.35, 1.95, vWorldPos.y);
        float Alpha = clamp(0.76 * EdgeFade + ZeroIso * 0.24, 0.15, 0.96);
        oFragColor = vec4(AcesFilmicTone(FieldCol), Alpha);
        return;
    }

    vec3 N = normalize(vWorldNorm);
    vec3 V = normalize(uParams[0].xyz - vWorldPos);
    vec3 L = normalize(uParams[1].xyz);
    vec3 H = normalize(V + L);

    float Rough = clamp(vProps.x, 0.04, 1.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    if (Tag > 4.5 && Tag < 5.5) {
        oFragColor = vec4(AcesFilmicTone(vTint), 1.0);
        return;
    }

    float Shadow = 1.0;
    float Ao     = 1.0;
    if (Tag < 0.5 || Tag > 5.5) {
        Shadow = EvalSdfSoftShadow(vWorldPos + N * 0.018, L, SdfMode);
        Ao     = EvalSdfUnderbodyAo(vWorldPos, N, SdfMode);
    } else {
        Ao = clamp(0.55 + 0.45 * N.y, 0.35, 1.0);
    }

    vec3 SkyCol = mix(vec3(0.05, 0.07, 0.10), vec3(0.20, 0.28, 0.40), clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    vec3 SunCol = vec3(2.65, 2.52, 2.34);

    float SpecPow = mix(220.0, 12.0, Rough * Rough);
    float SpecInt = mix(1.35, 0.08, Rough);
    float Fresnel = pow(max(1.0 - NdotV, 1e-4), 4.0);

    vec3 Hdr = vTint * (SkyCol * Ao + SunCol * NdotL * Shadow * 0.88);
    Hdr += SunCol * pow(max(NdotH, 1e-4), SpecPow) * SpecInt * Shadow;

    if (Tag > 0.5 && Tag < 4.5) {
        vec3 ReflDir = reflect(-V, N);
        float LightBar = smoothstep(0.62, 0.92, sin(ReflDir.y * 9.0 + ReflDir.x * 2.5));
        vec3 EnvRefl = mix(vec3(0.03, 0.05, 0.08), vec3(0.75, 0.86, 1.0), clamp(ReflDir.y * 0.5 + 0.5, 0.0, 1.0))
                     + vec3(0.55, 0.62, 0.72) * LightBar * (1.0 - Rough);
        Hdr += EnvRefl * (0.08 + 0.58 * Fresnel) * (1.0 - Rough * 0.75);
    }

    if (Tag < 0.5) {
        float Rad = length(vWorldPos.xz);
        float RingLine = 1.0 - smoothstep(0.0, 0.018, abs(FractPositive(Rad * 0.5) - 0.5) * 2.0);
        Hdr += vec3(0.020, 0.035, 0.052) * RingLine * exp(-Rad * 0.22);

        float ReflStrength = uParams[7].y;
        if (ReflStrength > 0.01) {
            vec3 RDir = reflect(-V, vec3(0.0, 1.0, 0.0));
            vec2 BoxHit = IntersectCarAabb(vWorldPos + vec3(0.0, 0.015, 0.0), RDir);
            if (BoxHit.x < BoxHit.y) {
                float Rt = max(0.02, BoxHit.x);
                bool HitCar = false;
                for (int Step = 0; Step < 18; ++Step) {
                    if (Rt > BoxHit.y) break;
                    float Rd = EvalSelectedSdf(vWorldPos + RDir * Rt, SdfMode);
                    if (Rd < 0.008) {
                        HitCar = true;
                        break;
                    }
                    Rt += max(Rd, 0.025);
                }
                if (HitCar) {
                    vec3 HitP = vWorldPos + RDir * Rt;
                    vec3 HitN = EvalSdfGradientNormal(HitP, SdfMode);
                    vec3 CarCoat = (HitP.y < 0.56) ? vec3(0.04, 0.045, 0.055) : vec3(0.045, 0.26, 0.56);
                    vec3 ReflLit = CarCoat * (0.28 + 0.95 * max(dot(HitN, L), 0.0)) + vec3(0.18, 0.24, 0.32) * max(HitN.y, 0.0);
                    float DistAtten = exp(-Rt * 0.42);
                    Hdr = mix(Hdr, ReflLit, clamp(ReflStrength * (0.18 + 0.55 * Fresnel) * DistAtten, 0.0, 0.68));
                }
            }
        }
    }

    oFragColor = vec4(AcesFilmicTone(Hdr), 1.0);
}
`;

export const RayMarchIsoSurfaceGlslVert = /* glsl */ `#version 300 es
precision highp float;
out vec2 vNdcXY;

void main() {
    vec2 P = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    gl_Position = vec4(P, 0.0, 1.0);
    vNdcXY = P;
}
`;

export const RayMarchIsoSurfaceGlslFrag = /* glsl */ `#version 300 es
precision highp float;
precision highp sampler2D;
precision highp sampler3D;

in vec2 vNdcXY;
out vec4 oFragColor;
` + SharedGlslSdfLibrary + /* glsl */ `
void main() {
    vec3 CamPos = uParams[0].xyz;
    vec4 FarH   = uInvViewProj * vec4(vNdcXY, 1.0, 1.0);
    vec3 FarWorld = FarH.xyz / FarH.w;
    vec3 RayDir = normalize(FarWorld - CamPos);

    int SdfMode = ResolveSdfModeForScreenX(gl_FragCoord.x);
    vec2 BoxHit = IntersectCarAabb(CamPos, RayDir);
    if (BoxHit.x >= BoxHit.y) discard;

    float T = max(0.05, BoxHit.x);
    bool Hit = false;
    float PrevT = T;
    float PrevD = 1.0;

    for (int Step = 0; Step < 46; ++Step) {
        if (T > BoxHit.y) break;
        vec3 P = CamPos + RayDir * T;
        float D = EvalSelectedSdf(P, SdfMode);
        if (D < 0.0022) {
            float Denom = PrevD - D;
            if (Step > 0 && abs(Denom) > 1e-6) {
                T = clamp(PrevT + (T - PrevT) * (PrevD / Denom), PrevT, T);
            }
            Hit = true;
            break;
        }
        PrevT = T;
        PrevD = D;
        T += max(D * 0.92, 0.014);
    }

    if (!Hit) discard;

    vec3 HitPos = CamPos + RayDir * T;
    if (uParams[7].z > 0.5) {
        float CutCoord = uParams[4].w;
        if (uParams[5].w < 0.5) {
            if (HitPos.z > CutCoord) discard;
        } else {
            if (HitPos.x > CutCoord) discard;
        }
    }

    vec3 N = EvalSdfGradientNormal(HitPos, SdfMode);
    vec3 V = -RayDir;
    vec3 L = normalize(uParams[1].xyz);
    vec3 H = normalize(V + L);

    bool NearFrontWheel = (length(vec2(HitPos.y - 0.5086, HitPos.z - 1.7274)) < 0.53) && (abs(HitPos.x) > 0.80);
    bool NearRearWheel  = (length(vec2(HitPos.y - 0.5086, HitPos.z + 1.6686)) < 0.53) && (abs(HitPos.x) > 0.80);
    bool TouchWheel = NearFrontWheel || NearRearWheel;

    vec3 Coat = (HitPos.y > 1.24 && HitPos.z > -1.25 && HitPos.z < 1.10)
        ? vec3(0.020, 0.032, 0.052)
        : vec3(0.045, 0.275, 0.590);
    if (TouchWheel) {
        float RefAxleZ = (HitPos.z > 0.0) ? 1.7274 : -1.6686;
        bool RimZone = (length(vec2(HitPos.y - 0.5086, HitPos.z - RefAxleZ)) < 0.34) && (abs(HitPos.x) > 1.16);
        Coat = RimZone ? vec3(0.66, 0.70, 0.76) : vec3(0.032, 0.035, 0.040);
    }

    vec3 Hdr;
    if (uParams[2].w > 0.5) {
        vec3 Refl = reflect(-V, N);
        float Stripe = smoothstep(-0.12, 0.12, sin(Refl.y * 28.0 + Refl.x * 7.0 + Refl.z * 3.0));
        float LitShade = 0.22 + 0.78 * max(dot(N, L), 0.0);
        vec3 TintBand = vec3(0.10, 0.80, 0.98);
        if (SdfMode == 1) TintBand = vec3(0.96, 0.74, 0.24);
        else if (SdfMode == 2) TintBand = vec3(0.96, 0.52, 0.28);
        Hdr = mix(vec3(0.025, 0.032, 0.044), TintBand * 1.55, Stripe) * LitShade;
    } else {
        float Rough = TouchWheel ? 0.55 : 0.11;
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float Fresnel = pow(max(1.0 - NdotV, 1e-4), 4.0);
        vec3 Refl = reflect(-V, N);
        float LightBar = smoothstep(0.55, 0.92, sin(Refl.y * 10.0 + Refl.x * 3.0));
        vec3 EnvCol = mix(vec3(0.04, 0.06, 0.10), vec3(0.80, 0.90, 1.05), clamp(Refl.y * 0.5 + 0.5, 0.0, 1.0))
                    + vec3(0.65, 0.72, 0.82) * LightBar * (1.0 - Rough);
        vec3 SunCol = vec3(2.65, 2.52, 2.34);
        float SpecPow = mix(200.0, 18.0, Rough);
        Hdr = Coat * (vec3(0.12, 0.16, 0.22) + SunCol * NdotL * 0.88)
            + SunCol * pow(max(NdotH, 1e-4), SpecPow) * (1.0 - Rough * 0.7)
            + EnvCol * (0.10 + 0.62 * Fresnel) * (1.0 - Rough * 0.65);
    }

    vec4 Clip = uViewProj * vec4(HitPos, 1.0);
    if (uParams[2].y > 0.5 && abs(gl_FragCoord.x - uParams[0].w * 0.5) < 1.2) {
        oFragColor = vec4(0.96, 0.78, 0.34, 1.0);
    } else {
        oFragColor = vec4(AcesFilmicTone(Hdr), 1.0);
    }
    gl_FragDepth = clamp((Clip.z / Clip.w) * 0.5 + 0.5, 0.0, 1.0);
}
`;

export const CageOverlayGlslVert = /* glsl */ `#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aRgba;

uniform mat4 uViewProj;
out vec4 vRgba;

void main() {
    vec4 Clip = uViewProj * vec4(aPos, 1.0);
    Clip.z -= 0.0007 * Clip.w;
    gl_Position = Clip;
    vRgba = aRgba;
}
`;

export const CageOverlayGlslFrag = /* glsl */ `#version 300 es
precision highp float;
in vec4 vRgba;
out vec4 oFragColor;

void main() {
    oFragColor = vRgba;
}
`;
