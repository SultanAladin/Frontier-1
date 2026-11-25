// Experimental/ThinLeafSdfWorkbench/ThinLeafWgslShaders.js
// 🧩 Spec-Compliant WebGPU WGSL Shaders (@vertex / @fragment) + WebGL2 Fallback Shaders:
//      ① Sunlit Botanical Foliage + Two-Level 16^3 Canopy Brick + 5 x 2D Thin-Sheet Extrusion SDF Shadows
//      ② Comparison Modes (Standard 32^3 Signed Voxel SDF, Dilated 32^3 Voxel SDF, 32^3 Opacity UDF, Exact Ground Truth)
//      ③ 3-Way Live Split (Exact GT | Active SDF | Signed Error) & Step-Count / Cone-LOD Diagnostic
//      ④ 3D Visual Debug Wireframes (Occupied Canopy Bricks, Curved Leaf Sheet Outlines, Live Sun Shadow Ray Steps)

export const FoliageSurfaceWgsl = /* wgsl */ `
struct SceneUniforms
{
    ViewProjTransform : mat4x4<f32>,
    CameraEyeCone     : vec4<f32>, // .xyz = CameraEye, .w = TanSunCone
    SunDirMode        : vec4<f32>, // .xyz = NormalizedSunDir, .w = f32(SdfMode: 0..3)
    BoundsMinThick    : vec4<f32>, // .xyz = VolumeBoundsMin, .w = LeafHalfThicknessM
    BoundsMaxDiag     : vec4<f32>, // .xyz = VolumeBoundsMax, .w = VoxelDiag
    ViewportVis       : vec4<f32>, // .xy = ViewportPx, .z = f32(DisplayMode: 0..4), .w = SplitNormX
    SceneCountsMask   : vec4<f32>, // .x = f32(CapsuleCount), .y = f32(LeafCount), .z = LodBiasScale, .w = DebugLayerMask
    BarkParams        : vec4<f32>, // .x = BarkNoiseAmp, .y = BarkNoiseFreq, .z = BranchGnarl, .w = Reserved
};

@group(0) @binding(0) var<uniform> Params               : SceneUniforms;
@group(0) @binding(1) var          LinearClampSampler   : sampler;
@group(0) @binding(2) var          LeafArchetype2dTex   : texture_2d_array<f32>;
@group(0) @binding(3) var          MacroSdf3dTex        : texture_3d<f32>;
@group(0) @binding(4) var          ComparisonVoxel3dTex : texture_3d<f32>;
@group(0) @binding(5) var<storage, read> BrickHeaders   : array<vec4<f32>>;
@group(0) @binding(6) var<storage, read> BrickLeafRefs  : array<u32>;
@group(0) @binding(7) var<storage, read> LeafInstances  : array<vec4<f32>>;
@group(0) @binding(8) var<storage, read> WoodCapsules   : array<vec4<f32>>;

struct SurfaceVertexIn
{
    @location(0) PosMat   : vec4<f32>,
    @location(1) NormArch : vec4<f32>,
    @location(2) AlbedoAo : vec4<f32>,
    @location(3) LeafUv   : vec4<f32>,
};

struct SurfaceVertexOut
{
    @builtin(position) ClipPos  : vec4<f32>,
    @location(0)       WorldPos : vec3<f32>,
    @location(1)       MatTag   : f32,
    @location(2)       Normal   : vec3<f32>,
    @location(3)       ArchId   : f32,
    @location(4)       AlbedoAo : vec4<f32>,
    @location(5)       LeafUv   : vec4<f32>,
};

@vertex
fn VsMain(In : SurfaceVertexIn) -> SurfaceVertexOut
{
    var Out : SurfaceVertexOut;
    Out.ClipPos  = Params.ViewProjTransform * vec4<f32>(In.PosMat.xyz, 1.0);
    Out.WorldPos = In.PosMat.xyz;
    Out.MatTag   = In.PosMat.w;
    Out.Normal   = In.NormArch.xyz;
    Out.ArchId   = In.NormArch.w;
    Out.AlbedoAo = In.AlbedoAo;
    Out.LeafUv   = In.LeafUv;
    return Out;
}

fn WorldToVolumeUvw(P : vec3<f32>) -> vec3<f32>
{
    let Span = max(Params.BoundsMaxDiag.xyz - Params.BoundsMinThick.xyz, vec3<f32>(1e-4));
    return clamp((P - Params.BoundsMinThick.xyz) / Span, vec3<f32>(0.0), vec3<f32>(1.0));
}

fn SampleLeaf2dArchetype(U : f32, W : f32, LayerIdx : i32) -> f32
{
    let Uv = vec2<f32>(U * 0.5 + 0.5, W * 0.5 + 0.5);
    let ClampedUv = clamp(Uv, vec2<f32>(0.0078125), vec2<f32>(0.9921875));
    let Sampled = textureSampleLevel(LeafArchetype2dTex, LinearClampSampler, ClampedUv, LayerIdx, 0.0).r;
    let OverU = max(0.0, abs(U) - 1.0);
    let OverW = max(0.0, abs(W) - 1.0);
    if (OverU > 0.0 || OverW > 0.0)
    {
        return max(Sampled, 0.0) + sqrt(OverU * OverU + OverW * OverW);
    }
    return Sampled;
}

fn IntersectRayVolumeAabb(Ro : vec3<f32>, Rd : vec3<f32>) -> vec2<f32>
{
    let SafeZero = select(vec3<f32>(-1e-7), vec3<f32>(1e-7), Rd >= vec3<f32>(0.0));
    let SafeRd   = select(SafeZero, Rd, abs(Rd) >= vec3<f32>(1e-7));
    let InvD     = 1.0 / SafeRd;
    let T0       = (Params.BoundsMinThick.xyz - Ro) * InvD;
    let T1       = (Params.BoundsMaxDiag.xyz  - Ro) * InvD;
    let TMinV    = min(T0, T1);
    let TMaxV    = max(T0, T1);
    let TEnter   = max(0.0, max(TMinV.x, max(TMinV.y, TMinV.z)));
    let TExit    = min(TMaxV.x, min(TMaxV.y, TMaxV.z));
    return vec2<f32>(TEnter, TExit);
}

fn HashLattice3d(Ix : f32, Iy : f32, Iz : f32) -> f32
{
    let Wx = Ix - floor(Ix * (1.0 / 289.0)) * 289.0;
    let Wy = Iy - floor(Iy * (1.0 / 289.0)) * 289.0;
    let Wz = Iz - floor(Iz * (1.0 / 289.0)) * 289.0;
    var P3 = fract(vec3<f32>(Wx, Wy, Wz) * vec3<f32>(0.1031, 0.1030, 0.0973));
    let D  = dot(P3, P3.yzx + vec3<f32>(19.19));
    P3     = P3 + vec3<f32>(D);
    return fract((P3.x + P3.y) * P3.z);
}

fn EvalSmoothNoise3d(Q : vec3<f32>) -> f32
{
    let I = floor(Q);
    let F = Q - I;
    let U = F * F * (vec3<f32>(3.0) - 2.0 * F);

    let N000 = HashLattice3d(I.x,       I.y,       I.z);
    let N100 = HashLattice3d(I.x + 1.0, I.y,       I.z);
    let N010 = HashLattice3d(I.x,       I.y + 1.0, I.z);
    let N110 = HashLattice3d(I.x + 1.0, I.y + 1.0, I.z);
    let N001 = HashLattice3d(I.x,       I.y,       I.z + 1.0);
    let N101 = HashLattice3d(I.x + 1.0, I.y,       I.z + 1.0);
    let N011 = HashLattice3d(I.x,       I.y + 1.0, I.z + 1.0);
    let N111 = HashLattice3d(I.x + 1.0, I.y + 1.0, I.z + 1.0);

    let Nx00 = mix(N000, N100, U.x);
    let Nx10 = mix(N010, N110, U.x);
    let Nx01 = mix(N001, N101, U.x);
    let Nx11 = mix(N011, N111, U.x);
    let Nxy0 = mix(Nx00, Nx10, U.y);
    let Nxy1 = mix(Nx01, Nx11, U.y);
    return mix(Nxy0, Nxy1, U.z);
}

fn EvalBarkFbm3d(P : vec3<f32>, Freq : f32) -> f32
{
    let Oct1 = EvalSmoothNoise3d(vec3<f32>(P.x * Freq, P.y * Freq * 0.46, P.z * Freq)) * 2.0 - 1.0;
    let F2   = Freq * 2.13;
    let Oct2 = EvalSmoothNoise3d(vec3<f32>(P.z * F2 + 11.3, P.x * 0.62 * F2 + 7.1, P.y * F2 + 5.7)) * 2.0 - 1.0;
    return Oct1 * 0.68 + Oct2 * 0.32;
}

fn EvalRayWoodCapsuleShadow(Ro : vec3<f32>, Rd : vec3<f32>, TStart : f32, TEnd : f32, TanCone : f32, CapIdx : u32) -> f32
{
    let C0 = WoodCapsules[CapIdx * 2u + 0u];
    let C1 = WoodCapsules[CapIdx * 2u + 1u];
    let P0 = C0.xyz;
    let R0 = C0.w;
    let U  = C1.xyz - P0;
    let R1 = C1.w;
    let W  = Ro - P0;

    let B = dot(Rd, U);
    let C = dot(U, U);
    let D = dot(Rd, W);
    let E = dot(U, W);
    let Denom = max(0.0, C - B * B);

    var S = select(0.0, clamp((E - B * D) / max(Denom, 1e-7), 0.0, 1.0), Denom > 1e-7);
    var T = clamp(B * S - D, TStart, TEnd);
    S = select(0.0, clamp((E + T * B) / max(C, 1e-7), 0.0, 1.0), C > 1e-7);
    T = clamp(B * S - D, TStart, TEnd);

    var BestS = S;
    var BestT = T;
    var BestSmooth = length(W + Rd * T - U * S) - (R0 + (R1 - R0) * S);

    let TCap0 = clamp(-D, TStart, TEnd);
    let D0    = length(W + Rd * TCap0) - R0;
    if (D0 < BestSmooth)
    {
        BestSmooth = D0;
        BestS      = 0.0;
        BestT      = TCap0;
    }

    let TCap1 = clamp(B - D, TStart, TEnd);
    let D1    = length(W + Rd * TCap1 - U) - R1;
    if (D1 < BestSmooth)
    {
        BestSmooth = D1;
        BestS      = 1.0;
        BestT      = TCap1;
    }

    let SmoothRad    = R0 + (R1 - R0) * BestS;
    let ConeR        = max(BestT * TanCone, 0.0015);
    let BarkNoiseAmp = Params.BarkParams.x;
    let BarkFreq     = Params.BarkParams.y;
    let MaxBarkBulge = SmoothRad * 0.18 * BarkNoiseAmp;

    if (BestSmooth > MaxBarkBulge + ConeR)
    {
        return 1.0;
    }
    if (BarkNoiseAmp <= 1e-4)
    {
        return select(clamp(BestSmooth / ConeR, 0.0, 1.0), 0.0, BestSmooth <= 0.0);
    }
    if (BestSmooth < -MaxBarkBulge)
    {
        return 0.0;
    }

    let StepT = SmoothRad * 0.42;
    var MinVis = 1.0;
    for (var Tap = -1; Tap <= 1; Tap = Tap + 1)
    {
        let TSample = clamp(BestT + f32(Tap) * StepT, TStart, TEnd);
        let SSample = select(0.0, clamp((E + TSample * B) / max(C, 1e-7), 0.0, 1.0), C > 1e-7);
        let Rel     = W + Rd * TSample - U * SSample;
        let DistAx  = length(Rel);
        let LocRad  = R0 + (R1 - R0) * SSample;
        let SkinPt  = P0 + U * SSample + Rel * (LocRad / max(DistAx, 1e-6));
        let NoisyR  = LocRad * (1.0 + 0.18 * BarkNoiseAmp * EvalBarkFbm3d(SkinPt, BarkFreq));
        let NoisyD  = DistAx - NoisyR;
        if (NoisyD <= 0.0)
        {
            return 0.0;
        }
        let TapCone = max(TSample * TanCone, 0.0015);
        MinVis = min(MinVis, clamp(NoisyD / TapCone, 0.0, 1.0));
    }
    return MinVis;
}

fn EvalAllWoodCapsules(Ro : vec3<f32>, Rd : vec3<f32>, TStart : f32, TEnd : f32, TanCone : f32) -> f32
{
    var WoodVis = 1.0;
    let CapCount = u32(Params.SceneCountsMask.x + 0.5);
    for (var C = 0u; C < CapCount; C = C + 1u)
    {
        WoodVis = min(WoodVis, EvalRayWoodCapsuleShadow(Ro, Rd, TStart, TEnd, TanCone, C));
        if (WoodVis <= 0.0)
        {
            return 0.0;
        }
    }
    return WoodVis;
}

// 🟢 2.5D Thin-Sheet Extrusion Intersection:
//    Locates curved-sheet crossing t* in [T0, T1] + hardware-filtered 64x64 2D Leaf Archetype SDF!
//    Thin 1.2 mm leaves NEVER vanish at ANY voxel size and NEVER bloat into spheres!
fn EvalThinLeafInstanceSegment(Ro : vec3<f32>, Rd : vec3<f32>, T0 : f32, T1 : f32, TanCone : f32, LeafIdx : u32) -> f32
{
    let Off = LeafIdx * 5u;
    let V0  = LeafInstances[Off + 0u]; // Center.xyz, InvScale
    let V1  = LeafInstances[Off + 1u]; // XAxis.xyz,  Scale
    let V2  = LeafInstances[Off + 2u]; // YAxis.xyz,  CamberX
    let V3  = LeafInstances[Off + 3u]; // ZAxis.xyz,  CamberZ
    let V4  = LeafInstances[Off + 4u]; // ArchetypeId

    let Center   = V0.xyz;
    let InvScale = V0.w;
    let Scale    = V1.w;
    let CamberX  = V2.w;
    let CamberZ  = V3.w;
    let ArchId   = i32(V4.x + 0.5);

    let MidT = clamp(dot(Center - Ro, Rd), T0, T1);
    let NearVec = (Ro + Rd * MidT) - Center;
    if (dot(NearVec, NearVec) > (Scale * 1.48) * (Scale * 1.48))
    {
        return 1.0;
    }

    let RelA = (Ro + Rd * T0) - Center;
    let RelB = (Ro + Rd * T1) - Center;

    let Ua = dot(RelA, V1.xyz) * InvScale;
    let Ya = dot(RelA, V2.xyz) * InvScale;
    let Wa = dot(RelA, V3.xyz) * InvScale;

    let Ub = dot(RelB, V1.xyz) * InvScale;
    let Yb = dot(RelB, V2.xyz) * InvScale;
    let Wb = dot(RelB, V3.xyz) * InvScale;

    let Ha = Ya - (CamberX * Ua * Ua + CamberZ * Wa * Wa);
    let Hb = Yb - (CamberX * Ub * Ub + CamberZ * Wb * Wb);

    var Alpha = 0.5;
    var CrossedSheet = false;
    let HalfThickNorm = Params.BoundsMinThick.w * InvScale;

    if ((Ha <= 0.0 && Hb >= 0.0) || (Ha >= 0.0 && Hb <= 0.0))
    {
        let Denom = Ha - Hb;
        Alpha = select(0.5, clamp(Ha / Denom, 0.0, 1.0), abs(Denom) > 1e-6);
        let Um = Ua + (Ub - Ua) * Alpha;
        let Ym = Ya + (Yb - Ya) * Alpha;
        let Wm = Wa + (Wb - Wa) * Alpha;
        let Hm = Ym - (CamberX * Um * Um + CamberZ * Wm * Wm);
        let Slope = Hb - Ha;
        if (abs(Slope) > 1e-5)
        {
            Alpha = clamp(Alpha - Hm / Slope, 0.0, 1.0);
        }
        CrossedSheet = true;
    }
    else
    {
        Alpha = select(1.0, 0.0, abs(Ha) < abs(Hb));
        if (min(abs(Ha), abs(Hb)) <= HalfThickNorm)
        {
            CrossedSheet = true;
        }
    }

    let UHit = Ua + (Ub - Ua) * Alpha;
    let YHit = Ya + (Yb - Ya) * Alpha;
    let WHit = Wa + (Wb - Wa) * Alpha;
    if (abs(UHit) > 1.35 || abs(WHit) > 1.35)
    {
        return 1.0;
    }

    let D2dWorld = SampleLeaf2dArchetype(UHit, WHit, ArchId) * Scale;
    let THit     = T0 + (T1 - T0) * Alpha;
    let ConeRad  = max(THit * TanCone, 0.0018);

    if (CrossedSheet)
    {
        if (D2dWorld <= 0.0)
        {
            return 0.0;
        }
        return clamp(D2dWorld / ConeRad, 0.0, 1.0);
    }

    let DSheet = abs(YHit - (CamberX * UHit * UHit + CamberZ * WHit * WHit)) * Scale - Params.BoundsMinThick.w;
    let D2dPos = max(D2dWorld, 0.0);
    let MissDist = sqrt(D2dPos * D2dPos + max(DSheet, 0.0) * max(DSheet, 0.0));
    return clamp(MissDist / ConeRad, 0.0, 1.0);
}

fn EvaluateExactGroundTruth(Ro : vec3<f32>, Rd : vec3<f32>, TanCone : f32) -> f32
{
    let Hit = IntersectRayVolumeAabb(Ro, Rd);
    if (Hit.x > Hit.y)
    {
        return 1.0;
    }
    let TStart = max(0.015, Hit.x);
    let TEnd   = Hit.y;

    var Vis = EvalAllWoodCapsules(Ro, Rd, TStart, TEnd, TanCone);
    if (Vis <= 0.0)
    {
        return 0.0;
    }

    let LeafCount = u32(Params.SceneCountsMask.y + 0.5);
    for (var L = 0u; L < LeafCount; L = L + 1u)
    {
        Vis = min(Vis, EvalThinLeafInstanceSegment(Ro, Rd, TStart, TEnd, TanCone, L));
        if (Vis <= 0.0)
        {
            return 0.0;
        }
    }
    return Vis;
}

struct SdfShadowResult
{
    Visibility : f32,
    Steps      : f32,
    MaxLod     : f32,
};

fn EvaluateSdfShadow(Ro : vec3<f32>, Rd : vec3<f32>, TanCone : f32, Mode : i32) -> SdfShadowResult
{
    var Res : SdfShadowResult;
    Res.Visibility = 1.0;
    Res.Steps      = 0.0;
    Res.MaxLod     = 0.0;

    let Hit = IntersectRayVolumeAabb(Ro, Rd);
    if (Hit.x > Hit.y)
    {
        return Res;
    }

    let TStart0 = max(0.015, Hit.x);
    var T = TStart0;
    let TEnd = Hit.y;
    let VoxelDiag = Params.BoundsMaxDiag.w;

    // Evaluate continuous Tapered-Capsule + 3D Bark-Noise Skeleton upfront so wooden limbs NEVER show missing pieces!
    let WoodVis = EvalAllWoodCapsules(Ro, Rd, TStart0, TEnd, TanCone);
    if (WoodVis <= 0.0)
    {
        Res.Visibility = 0.0;
        Res.Steps      = 1.0;
        return Res;
    }

    if (Mode == 0 || Mode == 1)
    {
        var LeafVis = 1.0;
        for (var Iter = 0; Iter < 56; Iter = Iter + 1)
        {
            if (T >= TEnd) { break; }
            Res.Steps = Res.Steps + 1.0;
            let P   = Ro + Rd * T;
            let Uvw = WorldToVolumeUvw(P);
            let Vox = textureSampleLevel(ComparisonVoxel3dTex, LinearClampSampler, Uvw, 0.0);
            let D   = select(Vox.g, Vox.r, Mode == 0);
            if (D <= 0.0005)
            {
                Res.Visibility = 0.0;
                return Res;
            }
            let ConeR = max(T * TanCone, 0.002);
            LeafVis = min(LeafVis, clamp(D / ConeR, 0.0, 1.0));
            T = T + max(D * 0.85, 0.028);
        }
        Res.Visibility = min(WoodVis, LeafVis);
        return Res;
    }

    if (Mode == 2)
    {
        var LeafVis = 1.0;
        let StepMin = VoxelDiag * 0.35;
        for (var Iter = 0; Iter < 56; Iter = Iter + 1)
        {
            if (T >= TEnd) { break; }
            Res.Steps = Res.Steps + 1.0;
            let P     = Ro + Rd * T;
            let Uvw   = WorldToVolumeUvw(P);
            let Vox   = textureSampleLevel(ComparisonVoxel3dTex, LinearClampSampler, Uvw, 0.0);
            let UDist = Vox.b;
            let Opac  = Vox.a;
            if (UDist < VoxelDiag * 0.72 && Opac > 0.005)
            {
                LeafVis = LeafVis * exp(-Opac * 3.2 * (StepMin / VoxelDiag));
                if (min(WoodVis, LeafVis) < 0.01)
                {
                    Res.Visibility = 0.0;
                    return Res;
                }
                T = T + StepMin;
            }
            else
            {
                T = T + max((UDist - VoxelDiag * 0.5) * 0.8, StepMin);
            }
        }
        Res.Visibility = min(WoodVis, LeafVis);
        return Res;
    }

    // 🟢 Mode 3: Two-Level 16^3 Canopy Brick + 5 x 2D Thin-Sheet Extrusion SDF + Continuous Cone-Width LOD (+ Noisy Capsule Limbs)!
    var LeafVis = 1.0;
    let BMin = Params.BoundsMinThick.xyz;
    let BMax = Params.BoundsMaxDiag.xyz;
    let Span = max(BMax - BMin, vec3<f32>(1e-4));
    let BrickSz = Span / 16.0;
    let LodScale = max(Params.SceneCountsMask.z, 0.25);

    for (var Iter = 0; Iter < 48; Iter = Iter + 1)
    {
        if (T >= TEnd) { break; }
        Res.Steps = Res.Steps + 1.0;

        let P     = Ro + Rd * T;
        let Uvw   = WorldToVolumeUvw(P);
        let Macro = textureSampleLevel(MacroSdf3dTex, LinearClampSampler, Uvw, 0.0);
        let ConeR = max(T * TanCone, 0.0015);

        let DHull = Macro.g;
        if (DHull > ConeR + 0.01)
        {
            T = T + max((DHull - ConeR) * 0.88, 0.025);
            continue;
        }

        let RelCell = clamp(((P - BMin) / Span) * 16.0, vec3<f32>(0.0), vec3<f32>(15.999));
        let Bx = i32(floor(RelCell.x));
        let By = i32(floor(RelCell.y));
        let Bz = i32(floor(RelCell.z));

        var TBrickOut = TEnd;
        let CellBoundX = BMin.x + f32(select(Bx, Bx + 1, Rd.x >= 0.0)) * BrickSz.x;
        let CellBoundY = BMin.y + f32(select(By, By + 1, Rd.y >= 0.0)) * BrickSz.y;
        let CellBoundZ = BMin.z + f32(select(Bz, Bz + 1, Rd.z >= 0.0)) * BrickSz.z;
        if (abs(Rd.x) > 1e-6) { TBrickOut = min(TBrickOut, (CellBoundX - Ro.x) / Rd.x); }
        if (abs(Rd.y) > 1e-6) { TBrickOut = min(TBrickOut, (CellBoundY - Ro.y) / Rd.y); }
        if (abs(Rd.z) > 1e-6) { TBrickOut = min(TBrickOut, (CellBoundZ - Ro.z) / Rd.z); }
        TBrickOut = max(T + 0.010, min(TEnd, TBrickOut));

        let Flat = u32((Bz * 16 + By) * 16 + Bx);
        let Hdr0 = BrickHeaders[Flat * 2u + 0u];
        let Count = u32(Hdr0.y + 0.5);

        if (Count > 0u)
        {
            let AvgScale = 0.135;
            let EffCone  = ConeR * LodScale;
            let LodBlend = clamp((EffCone - AvgScale * 0.55) / (AvgScale * 0.85), 0.0, 1.0);
            Res.MaxLod   = max(Res.MaxLod, LodBlend);

            var FineBrickVis = LeafVis;
            if (LodBlend < 0.999)
            {
                let StartOff = u32(Hdr0.x + 0.5);
                let TPad0 = max(0.015, T - 0.04);
                let TPad1 = min(TEnd, TBrickOut + 0.04);
                for (var K = 0u; K < Count; K = K + 1u)
                {
                    let LeafIdx = BrickLeafRefs[StartOff + K];
                    FineBrickVis = min(FineBrickVis, EvalThinLeafInstanceSegment(Ro, Rd, TPad0, TPad1, TanCone, LeafIdx));
                    if (FineBrickVis <= 0.0) { break; }
                }
            }

            var FarBeerVis = LeafVis;
            if (LodBlend > 0.001)
            {
                let Hdr1 = BrickHeaders[Flat * 2u + 1u];
                let Extinction = abs(Rd.x) * Hdr1.x + abs(Rd.y) * Hdr1.y + abs(Rd.z) * Hdr1.z;
                FarBeerVis = LeafVis * exp(-Extinction * (TBrickOut - T));
            }

            LeafVis = mix(FineBrickVis, FarBeerVis, LodBlend);
            if (min(WoodVis, LeafVis) <= 1e-4)
            {
                Res.Visibility = 0.0;
                return Res;
            }
        }

        T = TBrickOut + 0.0015;
    }

    Res.Visibility = min(WoodVis, LeafVis);
    return Res;
}

fn ShadeSunlitBotanicalSurface(
    WorldPos  : vec3<f32>,
    NormalIn  : vec3<f32>,
    MatTag    : f32,
    AlbedoAo  : vec4<f32>,
    LeafD2d   : f32,
    ShadowVis : f32,
    SunDir    : vec3<f32>,
    CameraEye : vec3<f32>
) -> vec3<f32>
{
    var N = normalize(NormalIn);
    let ViewDir = normalize(CameraEye - WorldPos);
    if (MatTag > 1.5 && dot(N, ViewDir) < 0.0)
    {
        N = -N;
    }

    var Albedo = AlbedoAo.rgb;

    if (MatTag < 0.5 && abs(N.y) > 0.8)
    {
        let Grid = abs(fract(WorldPos.xz + 0.5) - 0.5);
        let Seam = smoothstep(0.0, 0.018, min(Grid.x, Grid.y));
        Albedo = Albedo * (0.91 + 0.09 * Seam);
    }
    if (MatTag > 0.5 && MatTag < 1.5 && Params.BarkParams.x > 0.01)
    {
        let MicroGrain = EvalBarkFbm3d(WorldPos * 1.15, Params.BarkParams.y * 2.2);
        Albedo = Albedo * (0.94 + 0.16 * Params.BarkParams.x * MicroGrain);
    }
    if (MatTag > 1.5)
    {
        let MarginRim = smoothstep(-0.08, 0.0, LeafD2d);
        Albedo = mix(Albedo * 0.94, Albedo * vec3<f32>(1.14, 1.12, 0.95), MarginRim * 0.45);
    }

    let NdotL = max(dot(N, SunDir), 0.0);
    let SkyHemi = 0.5 + 0.5 * N.y;
    let SkyAmb = mix(vec3<f32>(0.21, 0.19, 0.16), vec3<f32>(0.26, 0.34, 0.48), SkyHemi) * AlbedoAo.a;
    let SunColor = vec3<f32>(1.38, 1.29, 1.12);

    var Direct = Albedo * SunColor * NdotL * ShadowVis;

    if (MatTag > 1.5)
    {
        let BackSun = max(dot(-N, SunDir), 0.0);
        let FwdScatter = clamp(dot(-ViewDir, SunDir) * 0.5 + 0.5, 1e-4, 1.0);
        let Translucency = pow(FwdScatter, 2.2) * 0.55 + BackSun * 0.45;
        Direct = Direct + Albedo * vec3<f32>(0.95, 1.15, 0.42) * Translucency * (0.25 + 0.75 * ShadowVis);
    }

    let HalfVec = normalize(SunDir + ViewDir);
    let SpecPow = select(28.0, 18.0, MatTag > 1.5);
    let SpecAmt = select(0.05, 0.11, MatTag > 1.5);
    let Spec = pow(max(dot(N, HalfVec), 1e-4), SpecPow) * SpecAmt * ShadowVis * step(0.01, NdotL);

    let LinearRgb = Albedo * SkyAmb + Direct + vec3<f32>(Spec);
    let Mapped = (LinearRgb * (2.51 * LinearRgb + 0.03)) / (LinearRgb * (2.43 * LinearRgb + 0.59) + 0.14);
    return pow(clamp(Mapped, vec3<f32>(1e-5), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2));
}

@fragment
fn FsMain(In : SurfaceVertexOut) -> @location(0) vec4<f32>
{
    var LeafD2d = -0.1;
    if (In.MatTag > 1.5)
    {
        LeafD2d = SampleLeaf2dArchetype(In.LeafUv.x, In.LeafUv.y, i32(In.ArchId + 0.5));
        if (LeafD2d > 0.0)
        {
            discard;
        }
    }

    let SunDir    = normalize(Params.SunDirMode.xyz);
    let SdfMode   = i32(Params.SunDirMode.w + 0.5);
    let DispMode  = i32(Params.ViewportVis.z + 0.5);
    let TanCone   = Params.CameraEyeCone.w;
    let CameraEye = Params.CameraEyeCone.xyz;

    let SurfN = normalize(In.Normal);
    let FaceN = select(SurfN, -SurfN, dot(SurfN, SunDir) < 0.0 && In.MatTag > 1.5);
    let Bias  = select(select(0.014, 0.028, In.MatTag > 0.5), 0.024, In.MatTag > 1.5);
    let SunShift = select(0.008, 0.016, In.MatTag > 0.5 && In.MatTag < 1.5);
    let RayOrigin = In.WorldPos + FaceN * Bias + SunDir * SunShift;

    let ScreenU = In.ClipPos.x / max(Params.ViewportVis.x, 1.0);

    if (DispMode == 1)
    {
        let GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
        let Col = ShadeSunlitBotanicalSurface(In.WorldPos, In.Normal, In.MatTag, In.AlbedoAo, LeafD2d, GtVis, SunDir, CameraEye);
        return vec4<f32>(Col, 1.0);
    }

    let SdfRes = EvaluateSdfShadow(RayOrigin, SunDir, TanCone, SdfMode);

    if (DispMode == 0)
    {
        let Col = ShadeSunlitBotanicalSurface(In.WorldPos, In.Normal, In.MatTag, In.AlbedoAo, LeafD2d, SdfRes.Visibility, SunDir, CameraEye);
        return vec4<f32>(Col, 1.0);
    }

    if (DispMode == 2)
    {
        let SplitA = 0.3333;
        let SplitB = 0.6667;
        let DivPx  = min(abs(ScreenU - SplitA), abs(ScreenU - SplitB)) * Params.ViewportVis.x;
        if (DivPx < 1.6)
        {
            return vec4<f32>(0.96, 0.78, 0.32, 1.0);
        }
        if (ScreenU < SplitA)
        {
            let GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
            let Col = ShadeSunlitBotanicalSurface(In.WorldPos, In.Normal, In.MatTag, In.AlbedoAo, LeafD2d, GtVis, SunDir, CameraEye);
            return vec4<f32>(Col, 1.0);
        }
        if (ScreenU < SplitB)
        {
            let Col = ShadeSunlitBotanicalSurface(In.WorldPos, In.Normal, In.MatTag, In.AlbedoAo, LeafD2d, SdfRes.Visibility, SunDir, CameraEye);
            return vec4<f32>(Col, 1.0);
        }
        let GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
        let SignedDiff = SdfRes.Visibility - GtVis;
        let ErrMag = clamp(abs(SignedDiff) * 2.5, 0.0, 1.0);
        let MissingLeafColor = mix(vec3<f32>(0.05, 0.06, 0.09), vec3<f32>(1.0, 0.24, 0.18), ErrMag);
        let BlobBloatColor   = mix(vec3<f32>(0.05, 0.06, 0.09), vec3<f32>(0.22, 0.78, 1.0), ErrMag);
        var DiffCol = select(BlobBloatColor, MissingLeafColor, SignedDiff >= 0.0);
        let SurfaceLum = dot(In.AlbedoAo.rgb, vec3<f32>(0.3, 0.59, 0.11));
        DiffCol = DiffCol + vec3<f32>(SurfaceLum * 0.09);
        return vec4<f32>(clamp(DiffCol, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
    }

    if (DispMode == 3)
    {
        let StepFrac = clamp(SdfRes.Steps / 24.0, 0.0, 1.0);
        let LowCol  = vec3<f32>(0.09, 0.20, 0.38);
        let MidCol  = vec3<f32>(0.18, 0.78, 0.56);
        let HighCol = vec3<f32>(0.98, 0.62, 0.18);
        var Heat = select(
            mix(MidCol, HighCol, (StepFrac - 0.5) * 2.0),
            mix(LowCol, MidCol, StepFrac * 2.0),
            StepFrac < 0.5
        );
        Heat = mix(Heat, vec3<f32>(0.82, 0.46, 0.98), SdfRes.MaxLod * 0.68);
        Heat = Heat * (0.42 + 0.58 * SdfRes.Visibility);
        return vec4<f32>(Heat, 1.0);
    }

    let Ivory = vec3<f32>(0.95, 0.93, 0.88);
    let Ink   = vec3<f32>(0.09, 0.11, 0.15);
    let Sheet = mix(Ink, Ivory, SdfRes.Visibility);
    return vec4<f32>(Sheet, 1.0);
}
`;

export const DebugWireframeWgsl = /* wgsl */ `
struct SceneUniforms
{
    ViewProjTransform : mat4x4<f32>,
    CameraEyeCone     : vec4<f32>,
    SunDirMode        : vec4<f32>,
    BoundsMinThick    : vec4<f32>,
    BoundsMaxDiag     : vec4<f32>,
    ViewportVis       : vec4<f32>,
    SceneCountsMask   : vec4<f32>, // .w = bitmask (1 = CanopyBricks, 2 = LeafSheetOutlines, 4 = RayMarchSteps, 8 = CapsuleSkeleton)
    BarkParams        : vec4<f32>,
};

@group(0) @binding(0) var<uniform> Params : SceneUniforms;

struct WireVertexIn
{
    @location(0) PosTag : vec4<f32>,
    @location(1) Color  : vec4<f32>,
};

struct WireVertexOut
{
    @builtin(position) ClipPos : vec4<f32>,
    @location(0)       Color   : vec4<f32>,
    @location(1)       Visible : f32,
};

@vertex
fn VsWire(In : WireVertexIn) -> WireVertexOut
{
    var Out : WireVertexOut;
    let LayerIdx = u32(In.PosTag.w + 0.5);
    let Mask     = u32(Params.SceneCountsMask.w + 0.5);
    let Enabled  = (Mask & (1u << LayerIdx)) != 0u;

    Out.ClipPos = select(vec4<f32>(2.0, 2.0, 2.0, 1.0), Params.ViewProjTransform * vec4<f32>(In.PosTag.xyz, 1.0), Enabled);
    Out.Color   = In.Color;
    Out.Visible = select(0.0, 1.0, Enabled);
    return Out;
}

@fragment
fn FsWire(In : WireVertexOut) -> @location(0) vec4<f32>
{
    if (In.Visible < 0.5)
    {
        discard;
    }
    return In.Color;
}
`;

// 🏷️ Automatic WebGL2 (#version 300 es) Fallback Shaders (used ONLY when navigator.gpu is unavailable in restricted/headless browsers)
//    Packs BrickHeaders + BrickLeafRefs + LeafInstances + WoodCapsules into a 256-wide RGBA32F lookup texture via texelFetch!
export const FoliageSurfaceGlslVert = `#version 300 es
precision highp float;
layout(location = 0) in vec4 aPosMat;
layout(location = 1) in vec4 aNormArch;
layout(location = 2) in vec4 aAlbedoAo;
layout(location = 3) in vec4 aLeafUv;

uniform mat4 uViewProjTransform;

out vec3 vWorldPos;
out float vMatTag;
out vec3 vNormal;
out float vArchId;
out vec4 vAlbedoAo;
out vec4 vLeafUv;

void main()
{
    gl_Position = uViewProjTransform * vec4(aPosMat.xyz, 1.0);
    vWorldPos = aPosMat.xyz;
    vMatTag   = aPosMat.w;
    vNormal   = aNormArch.xyz;
    vArchId   = aNormArch.w;
    vAlbedoAo = aAlbedoAo;
    vLeafUv   = aLeafUv;
}
`;

export const FoliageSurfaceGlslFrag = `#version 300 es
precision highp float;
precision highp sampler3D;
precision highp sampler2DArray;

in vec3 vWorldPos;
in float vMatTag;
in vec3 vNormal;
in float vArchId;
in vec4 vAlbedoAo;
in vec4 vLeafUv;

uniform vec4 uCameraEyeCone;
uniform vec4 uSunDirMode;
uniform vec4 uBoundsMinThick;
uniform vec4 uBoundsMaxDiag;
uniform vec4 uViewportVis;
uniform vec4 uSceneCountsMask;
uniform vec4 uBarkParams;
uniform ivec4 uPackedOffsets; // .x = RefStartVec4, .y = LeafStartVec4, .z = CapStartVec4, .w = TexWidth (256)

uniform sampler2DArray uLeafArchetype2dTex;
uniform sampler3D uMacroSdf3dTex;
uniform sampler3D uComparisonVoxel3dTex;
uniform sampler2D uPackedStorageTex;

out vec4 outFragColor;

vec4 FetchVec4(int VecIdx)
{
    int W = uPackedOffsets.w;
    return texelFetch(uPackedStorageTex, ivec2(VecIdx % W, VecIdx / W), 0);
}

int FetchLeafRef(int RefIdx)
{
    vec4 V = FetchVec4(uPackedOffsets.x + (RefIdx >> 2));
    int Comp = RefIdx & 3;
    float Raw = (Comp == 0) ? V.x : ((Comp == 1) ? V.y : ((Comp == 2) ? V.z : V.w));
    return int(Raw + 0.5);
}

vec3 WorldToVolumeUvw(vec3 P)
{
    vec3 Span = max(uBoundsMaxDiag.xyz - uBoundsMinThick.xyz, vec3(1e-4));
    return clamp((P - uBoundsMinThick.xyz) / Span, 0.0, 1.0);
}

float SampleLeaf2dArchetype(float U, float W, int LayerIdx)
{
    vec2 ClampedUv = clamp(vec2(U * 0.5 + 0.5, W * 0.5 + 0.5), vec2(0.0078125), vec2(0.9921875));
    float Sampled = textureLod(uLeafArchetype2dTex, vec3(ClampedUv, float(LayerIdx)), 0.0).r;
    float OverU = max(0.0, abs(U) - 1.0);
    float OverW = max(0.0, abs(W) - 1.0);
    if (OverU > 0.0 || OverW > 0.0)
    {
        return max(Sampled, 0.0) + sqrt(OverU * OverU + OverW * OverW);
    }
    return Sampled;
}

vec2 IntersectRayVolumeAabb(vec3 Ro, vec3 Rd)
{
    vec3 SafeRd = vec3(
        abs(Rd.x) < 1e-7 ? (Rd.x >= 0.0 ? 1e-7 : -1e-7) : Rd.x,
        abs(Rd.y) < 1e-7 ? (Rd.y >= 0.0 ? 1e-7 : -1e-7) : Rd.y,
        abs(Rd.z) < 1e-7 ? (Rd.z >= 0.0 ? 1e-7 : -1e-7) : Rd.z
    );
    vec3 InvD  = 1.0 / SafeRd;
    vec3 T0    = (uBoundsMinThick.xyz - Ro) * InvD;
    vec3 T1    = (uBoundsMaxDiag.xyz  - Ro) * InvD;
    vec3 TMinV = min(T0, T1);
    vec3 TMaxV = max(T0, T1);
    float TEnter = max(0.0, max(TMinV.x, max(TMinV.y, TMinV.z)));
    float TExit  = min(TMaxV.x, min(TMaxV.y, TMaxV.z));
    return vec2(TEnter, TExit);
}

float HashLattice3d(float Ix, float Iy, float Iz)
{
    float Wx = Ix - floor(Ix * (1.0 / 289.0)) * 289.0;
    float Wy = Iy - floor(Iy * (1.0 / 289.0)) * 289.0;
    float Wz = Iz - floor(Iz * (1.0 / 289.0)) * 289.0;
    vec3 P3 = fract(vec3(Wx, Wy, Wz) * vec3(0.1031, 0.1030, 0.0973));
    P3 += dot(P3, P3.yzx + 19.19);
    return fract((P3.x + P3.y) * P3.z);
}

float EvalSmoothNoise3d(vec3 Q)
{
    vec3 I = floor(Q);
    vec3 F = Q - I;
    vec3 U = F * F * (3.0 - 2.0 * F);

    float N000 = HashLattice3d(I.x,       I.y,       I.z);
    float N100 = HashLattice3d(I.x + 1.0, I.y,       I.z);
    float N010 = HashLattice3d(I.x,       I.y + 1.0, I.z);
    float N110 = HashLattice3d(I.x + 1.0, I.y + 1.0, I.z);
    float N001 = HashLattice3d(I.x,       I.y,       I.z + 1.0);
    float N101 = HashLattice3d(I.x + 1.0, I.y,       I.z + 1.0);
    float N011 = HashLattice3d(I.x,       I.y + 1.0, I.z + 1.0);
    float N111 = HashLattice3d(I.x + 1.0, I.y + 1.0, I.z + 1.0);

    float Nx00 = mix(N000, N100, U.x);
    float Nx10 = mix(N010, N110, U.x);
    float Nx01 = mix(N001, N101, U.x);
    float Nx11 = mix(N011, N111, U.x);
    float Nxy0 = mix(Nx00, Nx10, U.y);
    float Nxy1 = mix(Nx01, Nx11, U.y);
    return mix(Nxy0, Nxy1, U.z);
}

float EvalBarkFbm3d(vec3 P, float Freq)
{
    float Oct1 = EvalSmoothNoise3d(vec3(P.x * Freq, P.y * Freq * 0.46, P.z * Freq)) * 2.0 - 1.0;
    float F2   = Freq * 2.13;
    float Oct2 = EvalSmoothNoise3d(vec3(P.z * F2 + 11.3, P.x * 0.62 * F2 + 7.1, P.y * F2 + 5.7)) * 2.0 - 1.0;
    return Oct1 * 0.68 + Oct2 * 0.32;
}

float EvalRayWoodCapsuleShadow(vec3 Ro, vec3 Rd, float TStart, float TEnd, float TanCone, int CapIdx)
{
    vec4 C0 = FetchVec4(uPackedOffsets.z + CapIdx * 2 + 0);
    vec4 C1 = FetchVec4(uPackedOffsets.z + CapIdx * 2 + 1);
    vec3 P0 = C0.xyz;
    float R0 = C0.w;
    vec3 U  = C1.xyz - P0;
    float R1 = C1.w;
    vec3 W  = Ro - P0;
    float B = dot(Rd, U);
    float C = dot(U, U);
    float D = dot(Rd, W);
    float E = dot(U, W);
    float Denom = max(0.0, C - B * B);

    float S = Denom > 1e-7 ? clamp((E - B * D) / Denom, 0.0, 1.0) : 0.0;
    float T = clamp(B * S - D, TStart, TEnd);
    S = C > 1e-7 ? clamp((E + T * B) / C, 0.0, 1.0) : 0.0;
    T = clamp(B * S - D, TStart, TEnd);

    float BestS = S;
    float BestT = T;
    float BestSmooth = length(W + Rd * T - U * S) - (R0 + (R1 - R0) * S);

    float TCap0 = clamp(-D, TStart, TEnd);
    float D0    = length(W + Rd * TCap0) - R0;
    if (D0 < BestSmooth) { BestSmooth = D0; BestS = 0.0; BestT = TCap0; }

    float TCap1 = clamp(B - D, TStart, TEnd);
    float D1    = length(W + Rd * TCap1 - U) - R1;
    if (D1 < BestSmooth) { BestSmooth = D1; BestS = 1.0; BestT = TCap1; }

    float SmoothRad    = R0 + (R1 - R0) * BestS;
    float ConeR        = max(BestT * TanCone, 0.0015);
    float BarkNoiseAmp = uBarkParams.x;
    float BarkFreq     = uBarkParams.y;
    float MaxBarkBulge = SmoothRad * 0.18 * BarkNoiseAmp;

    if (BestSmooth > MaxBarkBulge + ConeR) return 1.0;
    if (BarkNoiseAmp <= 1e-4) return BestSmooth <= 0.0 ? 0.0 : clamp(BestSmooth / ConeR, 0.0, 1.0);
    if (BestSmooth < -MaxBarkBulge) return 0.0;

    float StepT  = SmoothRad * 0.42;
    float MinVis = 1.0;
    for (int Tap = -1; Tap <= 1; ++Tap)
    {
        float TSample = clamp(BestT + float(Tap) * StepT, TStart, TEnd);
        float SSample = C > 1e-7 ? clamp((E + TSample * B) / C, 0.0, 1.0) : 0.0;
        vec3 Rel      = W + Rd * TSample - U * SSample;
        float DistAx  = length(Rel);
        float LocRad  = R0 + (R1 - R0) * SSample;
        vec3 SkinPt   = P0 + U * SSample + Rel * (LocRad / max(DistAx, 1e-6));
        float NoisyR  = LocRad * (1.0 + 0.18 * BarkNoiseAmp * EvalBarkFbm3d(SkinPt, BarkFreq));
        float NoisyD  = DistAx - NoisyR;
        if (NoisyD <= 0.0) return 0.0;
        MinVis = min(MinVis, clamp(NoisyD / max(TSample * TanCone, 0.0015), 0.0, 1.0));
    }
    return MinVis;
}

float EvalAllWoodCapsules(vec3 Ro, vec3 Rd, float TStart, float TEnd, float TanCone)
{
    float WoodVis = 1.0;
    int CapCount = int(uSceneCountsMask.x + 0.5);
    for (int C = 0; C < 512; ++C)
    {
        if (C >= CapCount) break;
        WoodVis = min(WoodVis, EvalRayWoodCapsuleShadow(Ro, Rd, TStart, TEnd, TanCone, C));
        if (WoodVis <= 0.0) return 0.0;
    }
    return WoodVis;
}

float EvalThinLeafInstanceSegment(vec3 Ro, vec3 Rd, float T0, float T1, float TanCone, int LeafIdx)
{
    int Off = uPackedOffsets.y + LeafIdx * 5;
    vec4 V0 = FetchVec4(Off + 0);
    vec4 V1 = FetchVec4(Off + 1);
    vec4 V2 = FetchVec4(Off + 2);
    vec4 V3 = FetchVec4(Off + 3);
    vec4 V4 = FetchVec4(Off + 4);

    vec3 Center = V0.xyz;
    float InvScale = V0.w;
    float Scale = V1.w;
    float CamberX = V2.w;
    float CamberZ = V3.w;
    int ArchId = int(V4.x + 0.5);

    float MidT = clamp(dot(Center - Ro, Rd), T0, T1);
    vec3 NearVec = (Ro + Rd * MidT) - Center;
    if (dot(NearVec, NearVec) > (Scale * 1.48) * (Scale * 1.48)) return 1.0;

    vec3 RelA = (Ro + Rd * T0) - Center;
    vec3 RelB = (Ro + Rd * T1) - Center;
    float Ua = dot(RelA, V1.xyz) * InvScale;
    float Ya = dot(RelA, V2.xyz) * InvScale;
    float Wa = dot(RelA, V3.xyz) * InvScale;
    float Ub = dot(RelB, V1.xyz) * InvScale;
    float Yb = dot(RelB, V2.xyz) * InvScale;
    float Wb = dot(RelB, V3.xyz) * InvScale;

    float Ha = Ya - (CamberX * Ua * Ua + CamberZ * Wa * Wa);
    float Hb = Yb - (CamberX * Ub * Ub + CamberZ * Wb * Wb);

    float Alpha = 0.5;
    bool CrossedSheet = false;
    float HalfThickNorm = uBoundsMinThick.w * InvScale;

    if ((Ha <= 0.0 && Hb >= 0.0) || (Ha >= 0.0 && Hb <= 0.0))
    {
        float Denom = Ha - Hb;
        Alpha = abs(Denom) > 1e-6 ? clamp(Ha / Denom, 0.0, 1.0) : 0.5;
        float Um = Ua + (Ub - Ua) * Alpha;
        float Ym = Ya + (Yb - Ya) * Alpha;
        float Wm = Wa + (Wb - Wa) * Alpha;
        float Hm = Ym - (CamberX * Um * Um + CamberZ * Wm * Wm);
        float Slope = Hb - Ha;
        if (abs(Slope) > 1e-5) Alpha = clamp(Alpha - Hm / Slope, 0.0, 1.0);
        CrossedSheet = true;
    }
    else
    {
        Alpha = abs(Ha) < abs(Hb) ? 0.0 : 1.0;
        if (min(abs(Ha), abs(Hb)) <= HalfThickNorm) CrossedSheet = true;
    }

    float UHit = Ua + (Ub - Ua) * Alpha;
    float YHit = Ya + (Yb - Ya) * Alpha;
    float WHit = Wa + (Wb - Wa) * Alpha;
    if (abs(UHit) > 1.35 || abs(WHit) > 1.35) return 1.0;

    float D2dWorld = SampleLeaf2dArchetype(UHit, WHit, ArchId) * Scale;
    float THit = T0 + (T1 - T0) * Alpha;
    float ConeRad = max(THit * TanCone, 0.0018);

    if (CrossedSheet)
    {
        return D2dWorld <= 0.0 ? 0.0 : clamp(D2dWorld / ConeRad, 0.0, 1.0);
    }

    float DSheet = max(0.0, abs(YHit - (CamberX * UHit * UHit + CamberZ * WHit * WHit)) * Scale - uBoundsMinThick.w);
    float D2dPos = max(D2dWorld, 0.0);
    return clamp(sqrt(D2dPos * D2dPos + DSheet * DSheet) / ConeRad, 0.0, 1.0);
}

float EvaluateExactGroundTruth(vec3 Ro, vec3 Rd, float TanCone)
{
    vec2 Hit = IntersectRayVolumeAabb(Ro, Rd);
    if (Hit.x > Hit.y) return 1.0;
    float TStart = max(0.015, Hit.x);
    float TEnd   = Hit.y;
    float Vis    = EvalAllWoodCapsules(Ro, Rd, TStart, TEnd, TanCone);
    if (Vis <= 0.0) return 0.0;
    int LeafCount = int(uSceneCountsMask.y + 0.5);
    for (int L = 0; L < 1024; ++L)
    {
        if (L >= LeafCount) break;
        Vis = min(Vis, EvalThinLeafInstanceSegment(Ro, Rd, TStart, TEnd, TanCone, L));
        if (Vis <= 0.0) return 0.0;
    }
    return Vis;
}

vec3 EvaluateSdfShadow(vec3 Ro, vec3 Rd, float TanCone, int Mode)
{
    vec2 Hit = IntersectRayVolumeAabb(Ro, Rd);
    if (Hit.x > Hit.y) return vec3(1.0, 0.0, 0.0);

    float TStart0 = max(0.015, Hit.x);
    float T = TStart0;
    float TEnd = Hit.y;
    float VoxelDiag = uBoundsMaxDiag.w;
    float Steps = 0.0;
    float MaxLod = 0.0;

    float WoodVis = EvalAllWoodCapsules(Ro, Rd, TStart0, TEnd, TanCone);
    if (WoodVis <= 0.0) return vec3(0.0, 1.0, 0.0);

    if (Mode == 0 || Mode == 1)
    {
        float LeafVis = 1.0;
        for (int Iter = 0; Iter < 56; ++Iter)
        {
            if (T >= TEnd) break;
            Steps += 1.0;
            vec4 Vox = textureLod(uComparisonVoxel3dTex, WorldToVolumeUvw(Ro + Rd * T), 0.0);
            float D = (Mode == 0) ? Vox.r : Vox.g;
            if (D <= 0.0005) return vec3(0.0, Steps, 0.0);
            LeafVis = min(LeafVis, clamp(D / max(T * TanCone, 0.002), 0.0, 1.0));
            T += max(D * 0.85, 0.028);
        }
        return vec3(min(WoodVis, LeafVis), Steps, 0.0);
    }

    if (Mode == 2)
    {
        float LeafVis = 1.0;
        float StepMin = VoxelDiag * 0.35;
        for (int Iter = 0; Iter < 56; ++Iter)
        {
            if (T >= TEnd) break;
            Steps += 1.0;
            vec3 Uvw = WorldToVolumeUvw(Ro + Rd * T);
            vec4 Vox = textureLod(uComparisonVoxel3dTex, Uvw, 0.0);
            if (Vox.b < VoxelDiag * 0.72 && Vox.a > 0.005)
            {
                LeafVis *= exp(-Vox.a * 3.2 * (StepMin / VoxelDiag));
                if (min(WoodVis, LeafVis) < 0.01) return vec3(0.0, Steps, 0.0);
                T += StepMin;
            }
            else
            {
                T += max((Vox.b - VoxelDiag * 0.5) * 0.8, StepMin);
            }
        }
        return vec3(min(WoodVis, LeafVis), Steps, 0.0);
    }

    float LeafVis = 1.0;
    vec3 BMin = uBoundsMinThick.xyz;
    vec3 Span = max(uBoundsMaxDiag.xyz - BMin, vec3(1e-4));
    vec3 BrickSz = Span / 16.0;
    float LodScale = max(uSceneCountsMask.z, 0.25);

    for (int Iter = 0; Iter < 48; ++Iter)
    {
        if (T >= TEnd) break;
        Steps += 1.0;
        vec3 P = Ro + Rd * T;
        vec4 Macro = textureLod(uMacroSdf3dTex, WorldToVolumeUvw(P), 0.0);
        float ConeR = max(T * TanCone, 0.0015);

        if (Macro.g > ConeR + 0.01)
        {
            T += max((Macro.g - ConeR) * 0.88, 0.025);
            continue;
        }

        vec3 RelCell = clamp(((P - BMin) / Span) * 16.0, 0.0, 15.999);
        int Bx = int(floor(RelCell.x));
        int By = int(floor(RelCell.y));
        int Bz = int(floor(RelCell.z));

        float TBrickOut = TEnd;
        float CellX = BMin.x + float(Rd.x >= 0.0 ? Bx + 1 : Bx) * BrickSz.x;
        float CellY = BMin.y + float(Rd.y >= 0.0 ? By + 1 : By) * BrickSz.y;
        float CellZ = BMin.z + float(Rd.z >= 0.0 ? Bz + 1 : Bz) * BrickSz.z;
        if (abs(Rd.x) > 1e-6) TBrickOut = min(TBrickOut, (CellX - Ro.x) / Rd.x);
        if (abs(Rd.y) > 1e-6) TBrickOut = min(TBrickOut, (CellY - Ro.y) / Rd.y);
        if (abs(Rd.z) > 1e-6) TBrickOut = min(TBrickOut, (CellZ - Ro.z) / Rd.z);
        TBrickOut = max(T + 0.010, min(TEnd, TBrickOut));

        int Flat = (Bz * 16 + By) * 16 + Bx;
        vec4 Hdr0 = FetchVec4(Flat * 2 + 0);
        int Count = int(Hdr0.y + 0.5);

        if (Count > 0)
        {
            float AvgScale = 0.135;
            float LodBlend = clamp((ConeR * LodScale - AvgScale * 0.55) / (AvgScale * 0.85), 0.0, 1.0);
            MaxLod = max(MaxLod, LodBlend);

            float FineBrickVis = LeafVis;
            if (LodBlend < 0.999)
            {
                int StartOff = int(Hdr0.x + 0.5);
                float TPad0 = max(0.015, T - 0.04);
                float TPad1 = min(TEnd, TBrickOut + 0.04);
                for (int K = 0; K < 96; ++K)
                {
                    if (K >= Count) break;
                    int LeafIdx = FetchLeafRef(StartOff + K);
                    FineBrickVis = min(FineBrickVis, EvalThinLeafInstanceSegment(Ro, Rd, TPad0, TPad1, TanCone, LeafIdx));
                    if (FineBrickVis <= 0.0) break;
                }
            }

            float FarBeerVis = LeafVis;
            if (LodBlend > 0.001)
            {
                vec4 Hdr1 = FetchVec4(Flat * 2 + 1);
                float Extinction = abs(Rd.x) * Hdr1.x + abs(Rd.y) * Hdr1.y + abs(Rd.z) * Hdr1.z;
                FarBeerVis = LeafVis * exp(-Extinction * (TBrickOut - T));
            }

            LeafVis = mix(FineBrickVis, FarBeerVis, LodBlend);
            if (min(WoodVis, LeafVis) <= 1e-4) return vec3(0.0, Steps, MaxLod);
        }

        T = TBrickOut + 0.0015;
    }

    return vec3(min(WoodVis, LeafVis), Steps, MaxLod);
}

vec3 ShadeSunlitBotanicalSurface(vec3 WorldPos, vec3 NormalIn, float MatTag, vec4 AlbedoAo, float LeafD2d, float ShadowVis, vec3 SunDir, vec3 CameraEye)
{
    vec3 N = normalize(NormalIn);
    vec3 ViewDir = normalize(CameraEye - WorldPos);
    if (MatTag > 1.5 && dot(N, ViewDir) < 0.0) N = -N;

    vec3 Albedo = AlbedoAo.rgb;
    if (MatTag < 0.5 && abs(N.y) > 0.8)
    {
        vec2 Grid = abs(fract(WorldPos.xz + 0.5) - 0.5);
        Albedo *= (0.91 + 0.09 * smoothstep(0.0, 0.018, min(Grid.x, Grid.y)));
    }
    if (MatTag > 0.5 && MatTag < 1.5 && uBarkParams.x > 0.01)
    {
        float MicroGrain = EvalBarkFbm3d(WorldPos * 1.15, uBarkParams.y * 2.2);
        Albedo *= (0.94 + 0.16 * uBarkParams.x * MicroGrain);
    }
    if (MatTag > 1.5)
    {
        float MarginRim = smoothstep(-0.08, 0.0, LeafD2d);
        Albedo = mix(Albedo * 0.94, Albedo * vec3(1.14, 1.12, 0.95), MarginRim * 0.45);
    }

    float NdotL = max(dot(N, SunDir), 0.0);
    vec3 SkyAmb = mix(vec3(0.21, 0.19, 0.16), vec3(0.26, 0.34, 0.48), 0.5 + 0.5 * N.y) * AlbedoAo.a;
    vec3 Direct = Albedo * vec3(1.38, 1.29, 1.12) * NdotL * ShadowVis;

    if (MatTag > 1.5)
    {
        float FwdScatter = clamp(dot(-ViewDir, SunDir) * 0.5 + 0.5, 1e-4, 1.0);
        float Translucency = pow(FwdScatter, 2.2) * 0.55 + max(dot(-N, SunDir), 0.0) * 0.45;
        Direct += Albedo * vec3(0.95, 1.15, 0.42) * Translucency * (0.25 + 0.75 * ShadowVis);
    }

    vec3 HalfVec = normalize(SunDir + ViewDir);
    float Spec = pow(max(dot(N, HalfVec), 1e-4), MatTag > 1.5 ? 18.0 : 28.0) * (MatTag > 1.5 ? 0.11 : 0.05) * ShadowVis * step(0.01, NdotL);
    vec3 LinearRgb = Albedo * SkyAmb + Direct + vec3(Spec);
    vec3 Mapped = (LinearRgb * (2.51 * LinearRgb + 0.03)) / (LinearRgb * (2.43 * LinearRgb + 0.59) + 0.14);
    return pow(clamp(Mapped, vec3(1e-5), vec3(1.0)), vec3(1.0 / 2.2));
}

void main()
{
    float LeafD2d = -0.1;
    if (vMatTag > 1.5)
    {
        LeafD2d = SampleLeaf2dArchetype(vLeafUv.x, vLeafUv.y, int(vArchId + 0.5));
        if (LeafD2d > 0.0) discard;
    }

    vec3 SunDir    = normalize(uSunDirMode.xyz);
    int SdfMode    = int(uSunDirMode.w + 0.5);
    int DispMode   = int(uViewportVis.z + 0.5);
    float TanCone  = uCameraEyeCone.w;
    vec3 CameraEye = uCameraEyeCone.xyz;

    vec3 SurfN = normalize(vNormal);
    vec3 FaceN = (dot(SurfN, SunDir) < 0.0 && vMatTag > 1.5) ? -SurfN : SurfN;
    float Bias = (vMatTag > 1.5) ? 0.024 : ((vMatTag > 0.5) ? 0.028 : 0.014);
    float SunShift = (vMatTag > 0.5 && vMatTag < 1.5) ? 0.016 : 0.008;
    vec3 RayOrigin = vWorldPos + FaceN * Bias + SunDir * SunShift;
    float ScreenU = gl_FragCoord.x / max(uViewportVis.x, 1.0);

    if (DispMode == 1)
    {
        float GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
        outFragColor = vec4(ShadeSunlitBotanicalSurface(vWorldPos, vNormal, vMatTag, vAlbedoAo, LeafD2d, GtVis, SunDir, CameraEye), 1.0);
        return;
    }

    vec3 SdfRes = EvaluateSdfShadow(RayOrigin, SunDir, TanCone, SdfMode);

    if (DispMode == 0)
    {
        outFragColor = vec4(ShadeSunlitBotanicalSurface(vWorldPos, vNormal, vMatTag, vAlbedoAo, LeafD2d, SdfRes.x, SunDir, CameraEye), 1.0);
        return;
    }

    if (DispMode == 2)
    {
        float DivPx = min(abs(ScreenU - 0.3333), abs(ScreenU - 0.6667)) * uViewportVis.x;
        if (DivPx < 1.6) { outFragColor = vec4(0.96, 0.78, 0.32, 1.0); return; }
        if (ScreenU < 0.3333)
        {
            float GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
            outFragColor = vec4(ShadeSunlitBotanicalSurface(vWorldPos, vNormal, vMatTag, vAlbedoAo, LeafD2d, GtVis, SunDir, CameraEye), 1.0);
            return;
        }
        if (ScreenU < 0.6667)
        {
            outFragColor = vec4(ShadeSunlitBotanicalSurface(vWorldPos, vNormal, vMatTag, vAlbedoAo, LeafD2d, SdfRes.x, SunDir, CameraEye), 1.0);
            return;
        }
        float GtVis = EvaluateExactGroundTruth(RayOrigin, SunDir, TanCone);
        float SignedDiff = SdfRes.x - GtVis;
        float ErrMag = clamp(abs(SignedDiff) * 2.5, 0.0, 1.0);
        vec3 DiffCol = SignedDiff >= 0.0
            ? mix(vec3(0.05, 0.06, 0.09), vec3(1.0, 0.24, 0.18), ErrMag)
            : mix(vec3(0.05, 0.06, 0.09), vec3(0.22, 0.78, 1.0), ErrMag);
        DiffCol += vec3(dot(vAlbedoAo.rgb, vec3(0.3, 0.59, 0.11)) * 0.09);
        outFragColor = vec4(clamp(DiffCol, 0.0, 1.0), 1.0);
        return;
    }

    if (DispMode == 3)
    {
        float StepFrac = clamp(SdfRes.y / 24.0, 0.0, 1.0);
        vec3 Heat = StepFrac < 0.5
            ? mix(vec3(0.09, 0.20, 0.38), vec3(0.18, 0.78, 0.56), StepFrac * 2.0)
            : mix(vec3(0.18, 0.78, 0.56), vec3(0.98, 0.62, 0.18), (StepFrac - 0.5) * 2.0);
        Heat = mix(Heat, vec3(0.82, 0.46, 0.98), SdfRes.z * 0.68) * (0.42 + 0.58 * SdfRes.x);
        outFragColor = vec4(Heat, 1.0);
        return;
    }

    outFragColor = vec4(mix(vec3(0.09, 0.11, 0.15), vec3(0.95, 0.93, 0.88), SdfRes.x), 1.0);
}
`;

export const DebugWireframeGlslVert = `#version 300 es
precision highp float;
layout(location = 0) in vec4 aPosTag;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProjTransform;
uniform int uDebugMask;
out vec4 vColor;
out float vVisible;
void main()
{
    int LayerIdx = int(aPosTag.w + 0.5);
    bool Enabled = (uDebugMask & (1 << LayerIdx)) != 0;
    gl_Position = Enabled ? (uViewProjTransform * vec4(aPosTag.xyz, 1.0)) : vec4(2.0, 2.0, 2.0, 1.0);
    vColor   = aColor;
    vVisible = Enabled ? 1.0 : 0.0;
}
`;

export const DebugWireframeGlslFrag = `#version 300 es
precision highp float;
in vec4 vColor;
in float vVisible;
out vec4 outFragColor;
void main()
{
    if (vVisible < 0.5) discard;
    outFragColor = vColor;
}
`;
