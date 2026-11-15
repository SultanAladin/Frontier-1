/*============================================================================================================================================
                                                    WORLDVIEWPORTRADIOMETRICCOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Hardware Vulkan GPU compute sequence for Tier 1 GTX & Tier 2 RTX Cornell-Studio World Viewport rendering:
// 4-Wide BVH4 primary + secondary ray queries, Upper-Hemisphere Clipped LTC polygonal area lights, SDF cone penumbra & 6-cone GI,
// OpenPBR Oren-Nayar + F82 metallic + Clearcoat BSDF, Abbe-dispersed crown glass refraction, and ACES filmic sRGB8 output.

struct GpuAreaQuadLight
{
    float4 CenterHalfWidth;  // xyz = CenterWorld, w = HalfWidthMeters
    float4 RightHalfHeight;   // xyz = RightTangent, w = HalfHeightMeters
    float4 UpTwoSided;   // xyz = UpBitangent, w = TwoSidedRadiance (1.0 / 0.0)
    float4 RadiancePacked;  // xyz = EmittedRadiance, w = Pad
};

struct GpuGlassSphere
{
    float4 CenterRadiusPacked; // xyz = CenterWorld, w = RadiusMeters
    float4 OpticsSlotPacked;   // x = DielectricIOR, y = AbbeDispersion, z = MaterialSlot, w = Roughness
};

struct WorldViewportUniformBlock
{
    float4 CameraOriginTanX;    // xyz = CameraOrigin, w = TanHalfFovX
    float4 CameraRightTanY;     // xyz = CameraRight,  w = TanHalfFovY
    float4 CameraUpWidth;       // xyz = CameraUp,     w = ViewportWidth
    float4 CameraForwardHeight; // xyz = CameraForward,w = ViewportHeight
    float4 SunDirConeTan;       // xyz = SunDirection, w = TanHalfSolarCone
    float4 SolarIrradianceTier; // xyz = SolarIrradiance, w = ActiveTier (0 = GTX, 1 = RTX)
    float4 SkyRadianceBvhCount; // xyz = SkyRadiance,  w = BvhEntryCount
    float4 SdfMinSpacing;       // xyz = SdfMinCorner, w = SdfVoxelSpacing
    uint4  SdfDimsLightCount;   // xyz = SdfDimensions,w = AreaLightCount
    float4 LatticeMinGlassCount;  // xyz = LatticeMin,   w = GlassSphereCount
    float4 LatticeMaxDebugMode; // xyz = LatticeMax,   w = DebugChannel
    uint4  LatticeDimsMaterialCount; // xyz = LatticeDims,  w = MaterialCount
    GpuAreaQuadLight AreaLights[4];
    GpuGlassSphere   GlassSpheres[4];
};

struct GpuWideTreeEntry128
{
    float4 BranchBoundsMinX;
    float4 BranchBoundsMaxX;
    float4 BranchBoundsMinY;
    float4 BranchBoundsMaxY;
    float4 BranchBoundsMinZ;
    float4 BranchBoundsMaxZ;
    uint4  BranchReference;
    uint4  BranchPrimitiveCount;
};

struct GpuShadedTriangle96
{
    float4 Corner0AndPad;
    float4 Edge01AndPad;
    float4 Edge02AndPad;
    float4 Normal0AndPad;
    float4 Normal1AndPad;
    float4 Normal2AndSlot; // xyz = SmoothNormal2, w = float(MaterialSlot)
};

struct GpuWorldMaterial64
{
    float4 DiffuseAlbedoRoughness; // xyz = EffectiveDiffuseAlbedo, w = DiffuseRoughness
    float4 SpecParams;            // x = SpecRoughness, y = Metalness, z = DielectricIOR, w = Anisotropy
    float4 EdgeTintCoatWeight; // xyz = EdgeTintF82, w = CoatWeight
    float4 CoatRoughnessOptics;    // x = CoatRoughness, y = TransmissionWeight, z = AbbeDispersion, w = Pad
};

[[vk::binding(0, 0)]] ConstantBuffer<WorldViewportUniformBlock> WorldUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<GpuWideTreeEntry128>     SceneBvhEntries;
[[vk::binding(2, 0)]] StructuredBuffer<GpuShadedTriangle96>     SceneTriangles;
[[vk::binding(3, 0)]] StructuredBuffer<GpuWorldMaterial64>      MaterialEntries;
[[vk::binding(4, 0)]] StructuredBuffer<float>                   SignedDistanceGrid;
[[vk::binding(5, 0)]] StructuredBuffer<float4>                  IrradianceLatticeTexels;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint>                  OutDisplayRgba8;

static const float PiConst  = 3.14159265359;
static const float TauConst = 6.28318530718;

struct RayHitReading
{
    bool   Hit;
    float  Distance;
    float3 Position;
    float3 ShadingNormal;
    float3 GeomNormal;
    uint   MaterialSlot;
};

float IntersectTriangleMT(float3 Origin, float3 Dir, float MinT, float MaxT, uint TriIndex, out float2 OutUV)
{
    OutUV = float2(0.0, 0.0);
    float3 Edge01  = SceneTriangles[TriIndex].Edge01AndPad.xyz;
    float3 Edge02  = SceneTriangles[TriIndex].Edge02AndPad.xyz;
    float3 PVec    = cross(Dir, Edge02);
    float  Det     = dot(Edge01, PVec);
    if (abs(Det) <= 1.0e-8) return -1.0;

    float  InvDet  = 1.0 / Det;
    float3 Corner0 = SceneTriangles[TriIndex].Corner0AndPad.xyz;
    float3 TVec    = Origin - Corner0;
    float  U       = dot(TVec, PVec) * InvDet;
    if (U < 0.0 || U > 1.0) return -1.0;

    float3 QVec = cross(TVec, Edge01);
    float  V    = dot(Dir, QVec) * InvDet;
    if (V < 0.0 || (U + V) > 1.0) return -1.0;

    float HitT = dot(Edge02, QVec) * InvDet;
    if (HitT < MinT || HitT > MaxT) return -1.0;

    OutUV = float2(U, V);
    return HitT;
}

float4 IntersectFourBoxes(float3 Origin, float3 InvDir, float MinT, float MaxT, uint EntryIndex)
{
    float4 TX0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinX - Origin.xxxx) * InvDir.xxxx;
    float4 TX1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxX - Origin.xxxx) * InvDir.xxxx;
    float4 TY0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinY - Origin.yyyy) * InvDir.yyyy;
    float4 TY1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxY - Origin.yyyy) * InvDir.yyyy;
    float4 TZ0 = (SceneBvhEntries[EntryIndex].BranchBoundsMinZ - Origin.zzzz) * InvDir.zzzz;
    float4 TZ1 = (SceneBvhEntries[EntryIndex].BranchBoundsMaxZ - Origin.zzzz) * InvDir.zzzz;

    float4 NearT = max(max(min(TX0, TX1), min(TY0, TY1)), max(min(TZ0, TZ1), float4(MinT)));
    float4 FarT  = min(min(max(TX0, TX1), max(TY0, TY1)), min(max(TZ0, TZ1), float4(MaxT)));
    return select(NearT <= FarT, NearT, float4(1.0e30));
}

RayHitReading TraceSceneClosest(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    RayHitReading Res;
    Res.Hit           = false;
    Res.Distance      = MaxT;
    Res.Position      = Origin;
    Res.ShadingNormal = float3(0.0, 0.0, 1.0);
    Res.GeomNormal    = float3(0.0, 0.0, 1.0);
    Res.MaterialSlot  = 0u;

    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return Res;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[32];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, Res.Distance, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        // Sort lanes farthest-first so closest lane is popped first from stack
        uint4 Order = uint4(0u, 1u, 2u, 3u);
        if (BoxT[Order.x] < BoxT[Order.y]) { uint Tmp = Order.x; Order.x = Order.y; Order.y = Tmp; }
        if (BoxT[Order.z] < BoxT[Order.w]) { uint Tmp = Order.z; Order.z = Order.w; Order.w = Tmp; }
        if (BoxT[Order.x] < BoxT[Order.z]) { uint Tmp = Order.x; Order.x = Order.z; Order.z = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.w]) { uint Tmp = Order.y; Order.y = Order.w; Order.w = Tmp; }
        if (BoxT[Order.y] < BoxT[Order.z]) { uint Tmp = Order.y; Order.y = Order.z; Order.z = Tmp; }

        for (int LaneIdx = 0; LaneIdx < 4; ++LaneIdx)
        {
            uint Lane = Order[LaneIdx];
            if (BoxT[Lane] >= Res.Distance) continue;

            uint Ref  = Refs[Lane];
            uint Cnt  = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    uint   TriIdx = Start + P;
                    float2 BaryUV;
                    float  HitT = IntersectTriangleMT(Origin, Dir, MinT, Res.Distance, TriIdx, BaryUV);
                    if (HitT > 0.0)
                    {
                        Res.Hit        = true;
                        Res.Distance   = HitT;
                        Res.Position   = Origin + Dir * HitT;
                        float  W0      = 1.0 - BaryUV.x - BaryUV.y;
                        float3 Edge01  = SceneTriangles[TriIdx].Edge01AndPad.xyz;
                        float3 Edge02  = SceneTriangles[TriIdx].Edge02AndPad.xyz;
                        float4 N2Slot  = SceneTriangles[TriIdx].Normal2AndSlot;
                        float3 RawGeom = normalize(cross(Edge01, Edge02));
                        float3 SmoothN = normalize(
                            SceneTriangles[TriIdx].Normal0AndPad.xyz * W0 +
                            SceneTriangles[TriIdx].Normal1AndPad.xyz * BaryUV.x +
                            N2Slot.xyz * BaryUV.y);
                        if (dot(RawGeom, Dir) > 0.0)
                        {
                            RawGeom = -RawGeom;
                            SmoothN = -SmoothN;
                        }
                        Res.GeomNormal    = RawGeom;
                        Res.ShadingNormal = SmoothN;
                        Res.MaterialSlot  = uint(N2Slot.w + 0.5);
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return Res;
}

bool TraceSceneAnyHit(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    uint EntryCount = uint(WorldUniforms.SkyRadianceBvhCount.w + 0.5);
    if (EntryCount == 0u) return false;

    float3 SafeDir = select(abs(Dir) > float3(1.0e-8), Dir, float3(1.0e-8));
    float3 InvDir  = 1.0 / SafeDir;

    uint Stack[32];
    int  StackTop = 0;
    Stack[0]      = 0u;

    while (StackTop >= 0)
    {
        uint CurIdx = Stack[StackTop--];
        if (CurIdx >= EntryCount) continue;

        float4 BoxT = IntersectFourBoxes(Origin, InvDir, MinT, MaxT, CurIdx);
        uint4  Refs = SceneBvhEntries[CurIdx].BranchReference;
        uint4  Cnts = SceneBvhEntries[CurIdx].BranchPrimitiveCount;

        for (uint Lane = 0u; Lane < 4u; ++Lane)
        {
            if (BoxT[Lane] >= MaxT) continue;
            uint Ref = Refs[Lane];
            uint Cnt = Cnts[Lane];
            if (Ref == 0xFFFFFFFFu) continue;

            if (Cnt > 0u)
            {
                uint Start = Ref & 0x7FFFFFFFu;
                for (uint P = 0u; P < Cnt; ++P)
                {
                    float2 DummyUV;
                    if (IntersectTriangleMT(Origin, Dir, MinT, MaxT, Start + P, DummyUV) > 0.0)
                    {
                        return true;
                    }
                }
            }
            else if (StackTop < 30)
            {
                Stack[++StackTop] = Ref;
            }
        }
    }
    return false;
}

float SampleSdfGrid(float3 WorldPos)
{
    uint3 Dims = WorldUniforms.SdfDimsLightCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return 4.0;

    float Spacing = max(1.0e-4, WorldUniforms.SdfMinSpacing.w);
    float3 Rel    = (WorldPos - WorldUniforms.SdfMinSpacing.xyz) / Spacing;
    int3   MaxIdx = int3(Dims) - int3(1);
    if (Rel.x < 0.0 || Rel.y < 0.0 || Rel.z < 0.0 ||
        Rel.x > float(MaxIdx.x) || Rel.y > float(MaxIdx.y) || Rel.z > float(MaxIdx.z))
    {
        return 2.5;
    }

    int3   P0  = clamp(int3(floor(Rel)), int3(0), MaxIdx);
    int3   P1  = min(P0 + int3(1), MaxIdx);
    float3 T   = saturate(Rel - float3(P0));
    uint   SX  = Dims.x;
    uint   SXY = Dims.x * Dims.y;

    float C00 = lerp(SignedDistanceGrid[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P0.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C10 = lerp(SignedDistanceGrid[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P0.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    float C01 = lerp(SignedDistanceGrid[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P1.z) * SXY + uint(P0.y) * SX + uint(P1.x)], T.x);
    float C11 = lerp(SignedDistanceGrid[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P0.x)],
                     SignedDistanceGrid[uint(P1.z) * SXY + uint(P1.y) * SX + uint(P1.x)], T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
}

float TraceSdfConeVisibility(float3 Origin, float3 Dir, float MaxDist, float TanHalfCone, uint MaxSteps)
{
    float Spacing = max(0.05, WorldUniforms.SdfMinSpacing.w);
    float Dist    = Spacing * 0.75;
    float Vis     = 1.0;
    for (uint Step = 0u; Step < MaxSteps && Dist < MaxDist; ++Step)
    {
        float Sdf    = SampleSdfGrid(Origin + Dir * Dist);
        float Radius = max(Spacing * 0.35, TanHalfCone * Dist);
        Vis          = min(Vis, saturate(0.5 + 0.5 * Sdf / Radius));
        if (Vis <= 0.01) return 0.0;
        Dist        += max(Spacing * 0.55, abs(Sdf) * 0.85);
    }
    return Vis;
}

float IntegrateSphericalEdgeZ(float3 V1, float3 V2)
{
    float X    = clamp(dot(V1, V2), -0.99999, 0.99999);
    float AbsX = abs(X);
    float A    = 5.42031 + (3.12829 + 0.0902326 * AbsX) * AbsX;
    float B    = 3.45068 + (4.18814 + AbsX) * AbsX;
    float ThetaOverSin = A / B;
    if (X < 0.0)
    {
        float SinTheta = sqrt(max(1.0e-8, 1.0 - X * X));
        ThetaOverSin   = PiConst / SinTheta - ThetaOverSin;
    }
    return (V1.x * V2.y - V1.y * V2.x) * ThetaOverSin;
}

void ConstructOrthonormalBasis(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float D     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * D, -SignZ * N.x);
    B = float3(D, SignZ + N.y * N.y * A, -N.y);
}

float3 EvaluateSurfaceBrdf(GpuWorldMaterial64 Mat, float3 N, float3 V, float3 L)
{
    float NdotL = max(0.0, dot(N, L));
    float NdotV = max(1.0e-4, dot(N, V));
    if (NdotL <= 1.0e-5) return float3(0.0);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  DiffRough = Mat.DiffuseAlbedoRoughness.w;
    float  SpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float  Metalness = saturate(Mat.SpecParams.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float3 EdgeF82   = Mat.EdgeTintCoatWeight.xyz;
    float  CoatW     = saturate(Mat.EdgeTintCoatWeight.w);
    float  CoatRough = clamp(Mat.CoatRoughnessOptics.x, 0.025, 1.0);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);

    // Energy-preserving Oren-Nayar diffuse
    float CoeffA  = 1.0 / (1.0 + DiffRough * 0.2877934);
    float CoeffB  = DiffRough * CoeffA;
    float SVal    = dot(V, L) - NdotV * NdotL;
    float InvMax  = (SVal > 0.0) ? (1.0 / max(1.0e-4, max(NdotV, NdotL))) : 1.0;
    float3 Diff   = Albedo * ((1.0 / PiConst) * (CoeffA + CoeffB * SVal * InvMax)) * (1.0 - Metalness) * (1.0 - TransW);

    // GGX Microfacet Specular with F82 Metallic Edge Tint
    float3 H       = normalize(V + L);
    float  NdotH   = max(0.0, dot(N, H));
    float  VdotH   = saturate(dot(V, H));
    float  Alpha   = SpecRough * SpecRough;
    float  Alpha2  = Alpha * Alpha;
    float  Denom   = NdotH * NdotH * (Alpha2 - 1.0) + 1.0;
    float  D_GGX   = Alpha2 / max(1.0e-6, PiConst * Denom * Denom);
    float  KVis    = 0.5 * Alpha;
    float  Vis     = 0.25 / max(1.0e-4, (NdotV * (1.0 - KVis) + KVis) * (NdotL * (1.0 - KVis) + KVis));
    float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 F0      = lerp(float3(F0Diel), Albedo, Metalness);
    float  Schlick = pow(1.0 - VdotH, 5.0);
    float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
    if (Metalness > 0.5)
    {
        float Lazanyi = VdotH * pow(1.0 - VdotH, 6.0);
        Fresnel -= (float3(1.0) - EdgeF82) * F0 * Lazanyi * 4.5;
    }
    float3 Spec = max(float3(0.0), Fresnel * (D_GGX * Vis));

    // Dielectric Clearcoat lobe
    if (CoatW > 0.01)
    {
        float CA    = CoatRough * CoatRough;
        float CA2   = CA * CA;
        float CDen  = NdotH * NdotH * (CA2 - 1.0) + 1.0;
        float CD    = CA2 / max(1.0e-6, PiConst * CDen * CDen);
        float CF    = 0.04 + 0.96 * Schlick;
        Spec       += float3(CoatW * CF * CD * 0.25);
        Diff       *= (1.0 - CoatW * CF);
    }

    return (Diff + Spec) * NdotL;
}

float IntegrateClippedQuadFormFactor(float3 LocalQ[4])
{
    // Sutherland-Hodgman upper-hemisphere clip Z >= 0
    float3 Clipped[8];
    uint   ClipCount = 0u;
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Cur  = LocalQ[I];
        float3 Next = LocalQ[(I + 1u) & 3u];
        bool   CurA = (Cur.z >= 0.0);
        bool   NxtA = (Next.z >= 0.0);
        if (CurA) Clipped[ClipCount++] = Cur;
        if (CurA != NxtA)
        {
            float Alpha = Cur.z / (Cur.z - Next.z);
            float3 Hit  = Cur + (Next - Cur) * Alpha;
            Hit.z       = 0.0;
            Clipped[ClipCount++] = Hit;
        }
    }
    if (ClipCount < 3u) return 0.0;

    for (uint I = 0u; I < ClipCount; ++I)
    {
        Clipped[I] = normalize(Clipped[I]);
    }
    float ContourSum = 0.0;
    for (uint I = 0u; I < ClipCount; ++I)
    {
        ContourSum += IntegrateSphericalEdgeZ(Clipped[I], Clipped[(I + 1u) % ClipCount]);
    }
    return saturate(abs(ContourSum) / TauConst);
}

float3 EvaluateAreaQuadLight(GpuAreaQuadLight Quad, float3 Pos, float3 N, float3 V, GpuWorldMaterial64 Mat)
{
    float3 RightT = Quad.RightHalfHeight.xyz;
    float3 UpB    = Quad.UpTwoSided.xyz;
    float3 QuadN  = normalize(cross(RightT, UpB));
    float3 Center = Quad.CenterHalfWidth.xyz;
    if (Quad.UpTwoSided.w < 0.5 && dot(Pos - Center, QuadN) <= 1.0e-4)
    {
        return float3(0.0);
    }

    float3 HalfU = RightT * Quad.CenterHalfWidth.w;
    float3 HalfV = UpB * Quad.RightHalfHeight.w;

    // 2-grid slices BVH visibility gate so area lights never bleed through walls or plinths
    const float2 GridSlices[2] = {
        float2(-0.42, -0.42),
        float2( 0.42,  0.42)
    };
    float3 RayOrig = Pos + N * 0.010;
    uint   VisCnt  = 0u;
    for (uint S = 0u; S < 2u; ++S)
    {
        float3 Target = Center + HalfU * GridSlices[S].x + HalfV * GridSlices[S].y;
        float3 ToL    = Target - RayOrig;
        float  Dist   = length(ToL);
        if (Dist <= 1.0e-4) continue;
        float3 DirL   = ToL / Dist;
        if (dot(N, DirL) <= 0.0) continue;
        if (!TraceSceneAnyHit(RayOrig, DirL, 0.032, max(0.040, Dist - 0.035)))
        {
            ++VisCnt;
        }
    }
    if (VisCnt == 0u) return float3(0.0);
    float VisFactor = float(VisCnt) * 0.5;

    float  NdotV = clamp(dot(N, V), 1.0e-4, 1.0);
    float3 T1    = V - N * NdotV;
    float  T1Len = dot(T1, T1);
    if (T1Len <= 1.0e-6)
    {
        float3 FallbackB;
        ConstructOrthonormalBasis(N, T1, FallbackB);
    }
    else
    {
        T1 = T1 * rsqrt(T1Len);
    }
    float3 T2 = normalize(cross(N, T1));

    float3 WorldC[4] = {
        Center - HalfU - HalfV,
        Center + HalfU - HalfV,
        Center + HalfU + HalfV,
        Center - HalfU + HalfV
    };
    float3 DiffLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 Rel   = WorldC[I] - Pos;
        DiffLocal[I] = float3(dot(Rel, T1), dot(Rel, T2), dot(Rel, N));
    }
    float DiffFormFactor = IntegrateClippedQuadFormFactor(DiffLocal);

    // Heitz 2016 GGX-fitted inverse LTC transform M^{-1}
    float Roughness = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float Alpha     = clamp(Roughness * Roughness, 0.004, 1.0);
    float SinV      = sqrt(max(0.0, 1.0 - NdotV * NdotV));
    float InvScaleX = 1.0 / max(0.04, Alpha * (0.85 + 0.15 * NdotV));
    float InvScaleY = 1.0 / max(0.04, Alpha);
    float ShearXZ   = -SinV / max(0.08, NdotV + Alpha);

    float3 SpecLocal[4];
    for (uint I = 0u; I < 4u; ++I)
    {
        float3 D      = DiffLocal[I];
        float MirrorX = -D.x * NdotV + D.z * SinV;
        float MirrorZ =  D.x * SinV  + D.z * NdotV;
        SpecLocal[I]  = float3(
            (MirrorX + ShearXZ * MirrorZ * Alpha) * InvScaleX,
            D.y * InvScaleY,
            MirrorZ);
    }
    float SpecFormFactor = IntegrateClippedQuadFormFactor(SpecLocal);

    float3 Albedo    = Mat.DiffuseAlbedoRoughness.xyz;
    float  Metalness = saturate(Mat.SpecParams.y);
    float  TransW    = saturate(Mat.CoatRoughnessOptics.y);
    float  Ior       = max(1.02, Mat.SpecParams.z);
    float  DielF0    = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
    float3 SpecF0    = lerp(float3(DielF0), Albedo, Metalness);
    float  Schlick   = pow(1.0 - NdotV, 5.0);
    float3 SpecAlb   = SpecF0 + (float3(1.0) - SpecF0) * (Schlick * saturate(1.0 - Roughness * 0.65));

    float3 DiffContrib = Albedo * (1.0 - Metalness) * (1.0 - TransW) * DiffFormFactor;
    float3 SpecContrib = SpecAlb * SpecFormFactor;
    return Quad.RadiancePacked.xyz * (DiffContrib + SpecContrib) * VisFactor;
}

float2 EncodeUnitOctahedral(float3 Dir)
{
    float  InvL1 = 1.0 / max(1.0e-6, abs(Dir.x) + abs(Dir.y) + abs(Dir.z));
    float2 Oct   = Dir.xy * InvL1;
    if (Dir.z < 0.0)
    {
        Oct = (1.0 - abs(Oct.yx)) * float2((Oct.x >= 0.0) ? 1.0 : -1.0, (Oct.y >= 0.0) ? 1.0 : -1.0);
    }
    return Oct * 0.5 + 0.5;
}

float3 SampleRtxIrradianceLattice(float3 WorldPos, float3 Normal)
{
    uint3 Dims = WorldUniforms.LatticeDimsMaterialCount.xyz;
    if (Dims.x < 2u || Dims.y < 2u || Dims.z < 2u) return WorldUniforms.SkyRadianceBvhCount.xyz * 0.35;

    float3 Span = max(float3(1.0e-3), WorldUniforms.LatticeMaxDebugMode.xyz - WorldUniforms.LatticeMinGlassCount.xyz);
    float3 Rel  = saturate((WorldPos + Normal * 0.12 - WorldUniforms.LatticeMinGlassCount.xyz) / Span) * float3(Dims - uint3(1u));
    int3   C0   = clamp(int3(floor(Rel)), int3(0), int3(Dims) - int3(1));
    int3   C1   = min(C0 + int3(1), int3(Dims) - int3(1));
    float3 Frac = saturate(Rel - float3(C0));

    float2 OctUv = EncodeUnitOctahedral(Normal);
    uint   OX    = min(7u, uint(OctUv.x * 8.0));
    uint   OY    = min(7u, uint(OctUv.y * 8.0));
    uint   TexId = OY * 8u + OX;

    uint SX  = Dims.x;
    uint SXY = Dims.x * Dims.y;

    float3 Accum = float3(0.0);
    for (uint DZ = 0u; DZ < 2u; ++DZ)
    {
        float WZ = (DZ == 0u) ? (1.0 - Frac.z) : Frac.z;
        uint  IZ = uint((DZ == 0u) ? C0.z : C1.z);
        for (uint DY = 0u; DY < 2u; ++DY)
        {
            float WY = (DY == 0u) ? (1.0 - Frac.y) : Frac.y;
            uint  IY = uint((DY == 0u) ? C0.y : C1.y);
            for (uint DX = 0u; DX < 2u; ++DX)
            {
                float WX     = (DX == 0u) ? (1.0 - Frac.x) : Frac.x;
                uint  IX     = uint((DX == 0u) ? C0.x : C1.x);
                uint  CellId = IZ * SXY + IY * SX + IX;
                Accum       += IrradianceLatticeTexels[CellId * 64u + TexId].xyz * (WX * WY * WZ);
            }
        }
    }
    return Accum;
}

float3 EvaluateGtxConeBounceGi(float3 WorldPos, float3 Normal)
{
    float3 T, B;
    ConstructOrthonormalBasis(Normal, T, B);

    float3 SkyRad   = WorldUniforms.SkyRadianceBvhCount.xyz;
    float3 SunIrr   = WorldUniforms.SolarIrradianceTier.xyz;
    uint   LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    float3 Origin   = WorldPos + Normal * 0.04;

    // 4-cone SDF hemisphere visibility + directional Cornell-Studio radiosity (zero BVH triangle rays)
    float3 Cone0 = Normal;
    float3 Cone1 = normalize(Normal * 0.55 + T * 0.835);
    float3 Cone2 = normalize(Normal * 0.55 - T * 0.417 + B * 0.723);
    float3 Cone3 = normalize(Normal * 0.55 - T * 0.417 - B * 0.723);

    float Vis0 = TraceSdfConeVisibility(Origin, Cone0, 2.20, 0.45, 5u);
    float Vis1 = TraceSdfConeVisibility(Origin, Cone1, 2.00, 0.50, 4u);
    float Vis2 = TraceSdfConeVisibility(Origin, Cone2, 2.00, 0.50, 4u);
    float Vis3 = TraceSdfConeVisibility(Origin, Cone3, 2.00, 0.50, 4u);

    float SkyOpen   = 0.34 * Vis0 + 0.22 * (Vis1 + Vis2 + Vis3);
    float Enclosed  = 1.0 - SkyOpen;

    // Cornell enclosure bounce: left crimson wall (X < 0), right emerald wall (X > 0), warm studio plaster & floor
    float LeftProx  = saturate((1.65 - (WorldPos.x + 2.20)) * 0.42) * saturate(0.45 + 0.55 * (-Normal.x));
    float RightProx = saturate((1.65 - (2.20 - WorldPos.x)) * 0.42) * saturate(0.45 + 0.55 * Normal.x);
    float FloorProx = saturate((1.20 - (WorldPos.z + 0.65)) * 0.55) * saturate(0.35 + 0.65 * (-Normal.z));

    float3 SoftboxFill = float3(0.28, 0.27, 0.25);
    if (LightCnt > 0u)
    {
        SoftboxFill = WorldUniforms.AreaLights[0].RadiancePacked.xyz * 0.085 + SunIrr * 0.045;
    }

    float3 CrimsonBleed = float3(0.74, 0.10, 0.09) * LeftProx  * 0.68;
    float3 EmeraldBleed = float3(0.11, 0.66, 0.20) * RightProx * 0.68;
    float3 WarmBounce   = float3(0.76, 0.73, 0.68) * (0.34 + 0.22 * FloorProx);

    float3 SkyTerm      = SkyRad * (0.70 + 0.45 * saturate(Normal.z * 0.5 + 0.5)) * (0.40 + 0.60 * SkyOpen);
    float3 RoomBounce   = SoftboxFill * (WarmBounce + CrimsonBleed + EmeraldBleed) * (0.55 + 0.45 * Enclosed);
    return SkyTerm + RoomBounce;
}

float3 ShadeSecondaryBounce(float3 RayOrigin, float3 RayDir)
{
    RayHitReading Hit = TraceSceneClosest(RayOrigin, RayDir, 2.0e-3, 80.0);
    float3 SkyRad     = WorldUniforms.SkyRadianceBvhCount.xyz;
    if (!Hit.Hit)
    {
        float Up = saturate(RayDir.z * 0.5 + 0.5);
        return SkyRad * (0.65 + 0.75 * Up);
    }

    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 V      = -RayDir;
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;

    float3 SafeSecondaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    float3 ColorHdr = float3(0.0);
    if (dot(N, SunDir) > 0.0 && !TraceSceneAnyHit(SafeSecondaryPos, SunDir, 0.032, 80.0))
    {
        float TermGate = saturate(dot(Hit.GeomNormal, SunDir) * 6.0 + 0.35);
        ColorHdr += EvaluateSurfaceBrdf(Mat, N, V, SunDir) * SunIrr * TermGate;
    }

    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        ColorHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafeSecondaryPos, N, V, Mat);
    }

    float3 Albedo = Mat.DiffuseAlbedoRoughness.xyz * (1.0 - saturate(Mat.SpecParams.y)) * (1.0 - saturate(Mat.CoatRoughnessOptics.y));
    float3 AmbGi  = (WorldUniforms.SolarIrradianceTier.w > 0.5)
                        ? SampleRtxIrradianceLattice(Hit.Position, N)
                        : (SkyRad * 0.36);
    ColorHdr += Albedo * AmbGi;
    return ColorHdr;
}

float3 ShadePrimarySurface(RayHitReading Hit, float3 ViewDir)
{
    uint MatCount = max(1u, WorldUniforms.LatticeDimsMaterialCount.w);
    GpuWorldMaterial64 Mat = MaterialEntries[min(Hit.MaterialSlot, MatCount - 1u)];
    float3 N      = Hit.ShadingNormal;
    float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
    float3 SunIrr = WorldUniforms.SolarIrradianceTier.xyz;
    bool   RtxTierActive = (WorldUniforms.SolarIrradianceTier.w > 0.5);

    uint DebugMode = uint(WorldUniforms.LatticeMaxDebugMode.w + 0.5);
    if (DebugMode == 1u)
    {
        float DepthVis = saturate(1.0 - Hit.Distance / 10.0);
        return float3(DepthVis);
    }
    if (DebugMode == 2u) return Mat.DiffuseAlbedoRoughness.xyz;
    if (DebugMode == 3u) return N * 0.5 + 0.5;
    if (DebugMode == 4u) return float3(Mat.SpecParams.x);

    // 1. Direct Solar Shadow & OpenPBR Shading
    float3 DirectHdr = float3(0.0);
    float3 SafePrimaryPos = Hit.Position + Hit.GeomNormal * 0.012 + N * 0.006;
    if (dot(N, SunDir) > 0.0)
    {
        bool GeomBlocked = TraceSceneAnyHit(SafePrimaryPos, SunDir, 0.032, 80.0);
        float SunVis     = 0.0;
        if (!GeomBlocked)
        {
            float TermGate = saturate(dot(Hit.GeomNormal, SunDir) * 6.0 + 0.35);
            SunVis = TermGate * (RtxTierActive
                         ? 1.0
                         : (0.35 + 0.65 * TraceSdfConeVisibility(
                               Hit.Position + Hit.GeomNormal * 0.04,
                               SunDir,
                               6.0,
                               max(0.020, WorldUniforms.SunDirConeTan.w * 2.0),
                               10u)));
        }
        if (SunVis > 0.001)
        {
            DirectHdr += EvaluateSurfaceBrdf(Mat, N, ViewDir, SunDir) * SunIrr * SunVis;
        }
    }

    // 2. Upper-Hemisphere Clipped Polygonal LTC Quad Area Lights
    uint LightCnt = min(4u, WorldUniforms.SdfDimsLightCount.w);
    for (uint L = 0u; L < LightCnt; ++L)
    {
        DirectHdr += EvaluateAreaQuadLight(WorldUniforms.AreaLights[L], SafePrimaryPos, N, ViewDir, Mat);
    }

    // 3. Two-Scale SDF Ambient Occlusion + Lagarde Specular AO
    float ChassisAo = TraceSdfConeVisibility(Hit.Position + N * 0.03, N, 0.32, 0.55, 4u);
    float RoomAo    = TraceSdfConeVisibility(Hit.Position + N * 0.05, N, 1.85, 0.45, 5u);
    float DiffAo    = saturate(0.30 + 0.70 * min(ChassisAo, RoomAo));
    float NdotV     = max(1.0e-3, dot(N, ViewDir));
    float SpecRough = clamp(Mat.SpecParams.x, 0.025, 1.0);
    float SpecAo    = saturate(pow(NdotV + DiffAo, exp2(-16.0 * SpecRough - 1.0)) - 1.0 + DiffAo);

    // 4. Multi-Bounce Diffuse Global Illumination
    float3 IndirectOverPi = RtxTierActive
                                ? SampleRtxIrradianceLattice(Hit.Position, N)
                                : EvaluateGtxConeBounceGi(Hit.Position, N);
    float  Metalness  = saturate(Mat.SpecParams.y);
    float  TransW     = saturate(Mat.CoatRoughnessOptics.y);
    float3 DiffuseGi  = IndirectOverPi * Mat.DiffuseAlbedoRoughness.xyz * (1.0 - Metalness) * (1.0 - TransW) * (0.35 + 0.65 * DiffAo);

    // 5. Secondary Specular Reflection Ray (Mirror Chrome, Brushed Gold, Clearcoat Cobalt, Polished Dielectrics)
    float3 SpecGi   = float3(0.0);
    float  CoatW    = saturate(Mat.EdgeTintCoatWeight.w);
    if (Metalness > 0.20 || CoatW > 0.20 || SpecRough < 0.20)
    {
        float3 ReflDir = reflect(-ViewDir, N);
        float3 EnvRad  = ShadeSecondaryBounce(Hit.Position + Hit.GeomNormal * 3.0e-3, ReflDir);
        float  Ior     = max(1.02, Mat.SpecParams.z);
        float  F0Diel  = pow((Ior - 1.0) / (Ior + 1.0), 2.0);
        float3 F0      = lerp(float3(F0Diel), Mat.DiffuseAlbedoRoughness.xyz, Metalness);
        float  Schlick = pow(1.0 - NdotV, 5.0);
        float3 Fresnel = F0 + (float3(1.0) - F0) * Schlick;
        float  Gloss   = saturate(1.0 - SpecRough * 0.85);
        if (CoatW > 0.15 && Metalness < 0.2)
        {
            Fresnel = max(Fresnel, float3((0.04 + 0.96 * Schlick) * CoatW));
            Gloss   = max(Gloss, 1.0 - Mat.CoatRoughnessOptics.x);
        }
        SpecGi = EnvRad * Fresnel * Gloss * SpecAo;
    }

    return DirectHdr + DiffuseGi + SpecGi;
}

bool IntersectGlassSphere(float3 Origin, float3 Dir, float3 Center, float Radius, out float TEnter, out float TExit)
{
    TEnter = -1.0;
    TExit  = -1.0;
    float3 Oc   = Origin - Center;
    float  B    = dot(Oc, Dir);
    float  C    = dot(Oc, Oc) - Radius * Radius;
    float  Disc = B * B - C;
    if (Disc <= 0.0) return false;
    float SqrtD = sqrt(Disc);
    TEnter      = -B - SqrtD;
    TExit       = -B + SqrtD;
    return TExit > 1.0e-3;
}

float3 TraceThroughGlassSphere(float3 EntryPos, float3 EntryNrm, float3 IncidentDir, float3 Center, float Radius, float IorChannel)
{
    float3 InsideDir = refract(IncidentDir, EntryNrm, 1.0 / IorChannel);
    if (dot(InsideDir, InsideDir) < 1.0e-5)
    {
        return ShadeSecondaryBounce(EntryPos + EntryNrm * 2.5e-3, reflect(IncidentDir, EntryNrm));
    }
    float TIn = 0.0, TOut = 0.0;
    if (IntersectGlassSphere(EntryPos + InsideDir * 2.0e-3, InsideDir, Center, Radius, TIn, TOut) && TOut > 0.0)
    {
        float3 ExitPos = EntryPos + InsideDir * (2.0e-3 + TOut);
        float3 OutNrm  = normalize(ExitPos - Center);
        float3 ExitDir = refract(InsideDir, -OutNrm, IorChannel);
        if (dot(ExitDir, ExitDir) < 1.0e-5)
        {
            ExitDir = reflect(InsideDir, -OutNrm);
        }
        return ShadeSecondaryBounce(ExitPos + OutNrm * 3.0e-3, normalize(ExitDir));
    }
    return ShadeSecondaryBounce(EntryPos + InsideDir * (Radius * 2.1), InsideDir);
}

float3 ApplyAcesFilmicGpu(float3 Hdr)
{
    float3 V = max(float3(0.0), Hdr * 0.85);
    return saturate((V * (2.51 * V + 0.03)) / (V * (2.43 * V + 0.59) + 0.14));
}

uint PackLinearToSrgb8Rgba(float3 LinearColor)
{
    float3 C    = saturate(LinearColor);
    float3 Srgb = select(C <= float3(0.0031308), C * 12.92, 1.055 * pow(max(C, float3(1.0e-6)), float3(1.0 / 2.4)) - 0.055);
    uint3  Byte = uint3(clamp(round(Srgb * 255.0), float3(0.0), float3(255.0)));
    return Byte.x | (Byte.y << 8u) | (Byte.z << 16u) | (255u << 24u);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint Width  = uint(WorldUniforms.CameraUpWidth.w + 0.5);
    uint Height = uint(WorldUniforms.CameraForwardHeight.w + 0.5);
    if (DispatchThreadID.x >= Width || DispatchThreadID.y >= Height)
    {
        return;
    }

    float2 PixelUv = (float2(DispatchThreadID.xy) + 0.5) / float2(float(Width), float(Height));
    float  NdcX    = PixelUv.x * 2.0 - 1.0;
    float  NdcY    = 1.0 - PixelUv.y * 2.0;

    float3 CamOrigin = WorldUniforms.CameraOriginTanX.xyz;
    float3 RayDir    = normalize(
        WorldUniforms.CameraForwardHeight.xyz +
        WorldUniforms.CameraRightTanY.xyz * (NdcX * WorldUniforms.CameraOriginTanX.w) +
        WorldUniforms.CameraUpWidth.xyz   * (NdcY * WorldUniforms.CameraRightTanY.w));

    RayHitReading PrimaryHit = TraceSceneClosest(CamOrigin, RayDir, 0.08, 120.0);

    // Check 2-Layer Dielectric Abbe Crown Glass Shells in front of opaque hit
    uint  GlassCnt     = min(4u, uint(WorldUniforms.LatticeMinGlassCount.w + 0.5));
    int   BestGlassIdx = -1;
    float BestGlassT   = PrimaryHit.Hit ? PrimaryHit.Distance : 120.0;
    for (uint G = 0u; G < GlassCnt; ++G)
    {
        float TEn = 0.0, TEx = 0.0;
        if (IntersectGlassSphere(
                CamOrigin,
                RayDir,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.xyz,
                WorldUniforms.GlassSpheres[G].CenterRadiusPacked.w,
                TEn,
                TEx))
        {
            float HitT = (TEn > 0.08) ? TEn : TEx;
            if (HitT > 0.08 && HitT < BestGlassT)
            {
                BestGlassT   = HitT;
                BestGlassIdx = int(G);
            }
        }
    }

    float3 FinalHdr = float3(0.0);
    if (BestGlassIdx >= 0)
    {
        GpuGlassSphere Glass = WorldUniforms.GlassSpheres[BestGlassIdx];
        float3 Center        = Glass.CenterRadiusPacked.xyz;
        float  Radius        = Glass.CenterRadiusPacked.w;
        float3 HitPos        = CamOrigin + RayDir * BestGlassT;
        float3 HitNrm        = normalize(HitPos - Center);
        float3 ViewDir       = -RayDir;
        float  CrownIor      = max(1.05, Glass.OpticsSlotPacked.x);
        float  Dispersion    = clamp(0.45 / max(10.0, Glass.OpticsSlotPacked.y), 0.004, 0.028);

        float  CosI    = saturate(dot(HitNrm, ViewDir));
        float  F0      = pow((CrownIor - 1.0) / (CrownIor + 1.0), 2.0);
        float  Fresnel = F0 + (1.0 - F0) * pow(1.0 - CosI, 5.0);

        float  RefrR   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor - Dispersion).r;
        float  RefrG   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor).g;
        float  RefrB   = TraceThroughGlassSphere(HitPos, HitNrm, RayDir, Center, Radius, CrownIor + Dispersion * 1.15).b;
        float3 RefrRgb = float3(RefrR, RefrG, RefrB) * float3(0.95, 0.98, 1.00);

        float3 ReflRgb = ShadeSecondaryBounce(HitPos + HitNrm * 2.5e-3, reflect(RayDir, HitNrm));

        // Direct solar & softbox dielectric specular glints on crown glass shell
        float3 SunDir = WorldUniforms.SunDirConeTan.xyz;
        float3 HSun   = normalize(ViewDir + SunDir);
        float  SpecGl = pow(max(0.0, dot(HitNrm, HSun)), 380.0) * 6.5;
        if (TraceSceneAnyHit(HitPos + HitNrm * 3.0e-3, SunDir, 2.0e-3, 60.0))
        {
            SpecGl = 0.0;
        }
        FinalHdr = lerp(RefrRgb, ReflRgb, Fresnel) + WorldUniforms.SolarIrradianceTier.xyz * SpecGl;
    }
    else if (PrimaryHit.Hit)
    {
        FinalHdr = ShadePrimarySurface(PrimaryHit, -RayDir);
    }
    else
    {
        float Up = saturate(RayDir.z * 0.5 + 0.5);
        FinalHdr = WorldUniforms.SkyRadianceBvhCount.xyz * (0.65 + 0.75 * Up);
    }

    OutDisplayRgba8[DispatchThreadID.y * Width + DispatchThreadID.x] = PackLinearToSrgb8Rgba(ApplyAcesFilmicGpu(FinalHdr));
}
