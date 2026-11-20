/*============================================================================================================================================
                                                    SDFEMISSIVEPENUMBRACOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified GPU compute kernel for all 6 Emissive-Sphere SDF Shadow + Penumbra + 3D Voxel Clipmap + Cone GI + Sobol Spherical-Cap modes

struct GpuSdfPenumbraUniforms
{
    uint4  ViewportModeSamples; // x = Width, y = Height, z = Mode (0..5), w = CapSamples (20 or 96)
    float4 CameraEyeExposure;   // xyz = CameraEye, w = ExposureScale
    float4 CameraForwardTanY;   // xyz = Fwd, w = TanHalfFovY
    float4 CameraRightAspect;   // xyz = Right, w = Aspect
    float4 CameraUpPad;         // xyz = TrueUp, w = 0
    float4 EmissiveCenterRad;   // xyz = SphereCenter, w = SphereRadius
    float4 EmissiveRadiancePad; // xyz = Radiance, w = 0
    uint4  FineClipDims;        // xyz = DimX, DimY, DimZ, w = CoarseDimX
    uint4  CoarseClipDimsYZ;    // x = CoarseDimY, y = CoarseDimZ, z = 0, w = 0
    float4 FineBoundsMin;       // xyz = FineMin
    float4 FineBoundsMax;       // xyz = FineMax
    float4 CoarseBoundsMin;     // xyz = CoarseMin
    float4 CoarseBoundsMax;     // xyz = CoarseMax
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuSdfPenumbraUniforms> PenumbraUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<float>                FineDistanceGrid;
[[vk::binding(2, 0)]] StructuredBuffer<float>                CoarseDistanceGrid;
[[vk::binding(3, 0)]] StructuredBuffer<float4>               CoarseRadiosityOpacity; // rgb = Radiosity, a = Opacity
[[vk::binding(4, 0)]] RWStructuredBuffer<float4>             OutLinearHdrVis;        // rgb = LinearHdr, a = PrimaryVisibility

static const float PiConst  = 3.14159265358979323846;
static const float TauConst = 6.28318530717958647692;

float SmoothStep01(float Edge0, float Edge1, float X)
{
    float T = clamp((X - Edge0) / max(1.0e-6, Edge1 - Edge0), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

float SoftMinSdf(float A, float B, float K)
{
    float H = max(K - abs(A - B), 0.0) / K;
    return min(A, B) - H * H * K * 0.25;
}

uint ReverseBits32Gpu(uint X)
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

uint Avalanche32Gpu(uint Input)
{
    uint M = Input;
    M ^= M >> 16u;
    M *= 0x7FEB352Du;
    M ^= M >> 15u;
    M *= 0x846CA68Bu;
    M ^= M >> 16u;
    return M;
}

uint NestedPermuteGpu(uint Input, uint Key)
{
    uint R = ReverseBits32Gpu(Input) + Key;
    R ^= R * 0x6C50B47Cu;
    R ^= R * 0xB82F1E52u;
    R ^= R * 0xC7AFE638u;
    R ^= R * 0x8D22F6E6u;
    return ReverseBits32Gpu(R);
}

float2 SampleSobol02Gpu(uint SampleIdx, uint PixelKey, uint PairIdx)
{
    uint PairKey  = Avalanche32Gpu(PairIdx * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    uint Shuffled = NestedPermuteGpu(SampleIdx, PairKey);
    uint D0       = NestedPermuteGpu(ReverseBits32Gpu(Shuffled), Avalanche32Gpu(PairKey ^ 0xA511E9B3u));
    uint B        = Shuffled;
    B ^= (B & 0xAAAAAAAAu) >> 1u;
    B ^= (B & 0xCCCCCCCCu) >> 2u;
    B ^= (B & 0xF0F0F0F0u) >> 4u;
    B ^= (B & 0xFF00FF00u) >> 8u;
    B ^= (B & 0xFFFF0000u) >> 16u;
    uint D1 = NestedPermuteGpu(ReverseBits32Gpu(B), Avalanche32Gpu(PairKey ^ 0x63D83595u));
    return float2(float(D0 >> 8u), float(D1 >> 8u)) * (1.0 / 16777216.0);
}

void BuildDuffBasisGpu(float3 N, out float3 TanU, out float3 BitV)
{
    float SignZ = select(N.z >= 0.0, 1.0, -1.0);
    float A     = -1.0 / (SignZ + N.z);
    float B     = N.x * N.y * A;
    TanU = float3(1.0 + SignZ * N.x * N.x * A, SignZ * B, -SignZ * N.x);
    BitV = float3(B, SignZ + N.y * N.y * A, -N.y);
}

float EvalSignedBoxGpu(float3 P, float3 Ctr, float3 HalfExt)
{
    float3 Q = abs(P - Ctr) - HalfExt;
    return length(max(Q, float3(0.0))) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
}

float EvalCappedCylinderZGpu(float3 P, float3 BaseCtr, float HeightZ, float Radius)
{
    float HalfH = 0.5 * HeightZ;
    float RadD  = length(P.xy - BaseCtr.xy) - Radius;
    float VertD = abs(P.z - (BaseCtr.z + HalfH)) - HalfH;
    float2 Out2 = max(float2(RadD, VertD), float2(0.0));
    return min(max(RadD, VertD), 0.0) + length(Out2);
}

float EvalTorusGpu(float3 P, float3 Ctr, float MajorR, float MinorR)
{
    float3 Rel = P - Ctr;
    float Ring = length(Rel.xy) - MajorR;
    return length(float2(Ring, Rel.z)) - MinorR;
}

float EvaluateOccluderSdfGpu(float3 P)
{
    float D = EvalCappedCylinderZGpu(P, float3(-0.50, 0.36, 0.0), 1.68, 0.155);
    D = min(D, EvalSignedBoxGpu(P, float3(-0.12, -0.38, 0.36), float3(0.19, 0.19, 0.36)));
    D = min(D, EvalTorusGpu(P, float3(-0.12, -0.38, 0.90), 0.29, 0.105));
    D = min(D, length(P - float3(0.52, 0.56, 0.52)) - 0.52);
    return D;
}

float EvaluateFullSceneSdfGpu(float3 P, out uint OutSlot)
{
    float BestD = EvalSignedBoxGpu(P, float3(0.0, 0.20, -0.10), float3(2.80, 2.65, 0.10));
    uint  Slot  = 0u;

    float D1 = EvalSignedBoxGpu(P, float3(0.0, 2.72, 1.60), float3(2.80, 0.10, 1.70));
    Slot  = select(D1 < BestD, 1u, Slot); BestD = min(BestD, D1);

    float D2 = EvalSignedBoxGpu(P, float3(-2.74, 0.20, 1.60), float3(0.10, 2.65, 1.70));
    Slot  = select(D2 < BestD, 2u, Slot); BestD = min(BestD, D2);

    float D3 = EvalSignedBoxGpu(P, float3(2.74, 0.20, 1.60), float3(0.10, 2.65, 1.70));
    Slot  = select(D3 < BestD, 3u, Slot); BestD = min(BestD, D3);

    float D4 = EvalSignedBoxGpu(P, float3(0.0, 0.20, 3.26), float3(2.80, 2.65, 0.10));
    Slot  = select(D4 < BestD, 4u, Slot); BestD = min(BestD, D4);

    float D5 = min(
        EvalCappedCylinderZGpu(P, float3(-0.50, 0.36, 0.0), 1.68, 0.155),
        EvalSignedBoxGpu(P, float3(-0.12, -0.38, 0.36), float3(0.19, 0.19, 0.36)));
    Slot  = select(D5 < BestD, 5u, Slot); BestD = min(BestD, D5);

    float D6 = EvalTorusGpu(P, float3(-0.12, -0.38, 0.90), 0.29, 0.105);
    Slot  = select(D6 < BestD, 6u, Slot); BestD = min(BestD, D6);

    float D8 = length(P - float3(0.52, 0.56, 0.52)) - 0.52;
    Slot  = select(D8 < BestD, 8u, Slot); BestD = min(BestD, D8);

    float D7 = length(P - PenumbraUniforms.EmissiveCenterRad.xyz) - PenumbraUniforms.EmissiveCenterRad.w;
    Slot  = select(D7 < BestD, 7u, Slot); BestD = min(BestD, D7);

    OutSlot = Slot;
    return BestD;
}

float3 EvaluateGradientNormalGpu(float3 P)
{
    uint Dummy = 0u;
    float Eps = 1.5e-3;
    float DX = EvaluateFullSceneSdfGpu(P + float3(Eps, 0.0, 0.0), Dummy) - EvaluateFullSceneSdfGpu(P - float3(Eps, 0.0, 0.0), Dummy);
    float DY = EvaluateFullSceneSdfGpu(P + float3(0.0, Eps, 0.0), Dummy) - EvaluateFullSceneSdfGpu(P - float3(0.0, Eps, 0.0), Dummy);
    float DZ = EvaluateFullSceneSdfGpu(P + float3(0.0, 0.0, Eps), Dummy) - EvaluateFullSceneSdfGpu(P - float3(0.0, 0.0, Eps), Dummy);
    return normalize(float3(DX, DY, DZ));
}

float SampleGridHermiteGpu(StructuredBuffer<float> Grid, uint3 Dims, float3 BMin, float3 BMax, float3 P)
{
    float3 Outside = max(float3(0.0), max(BMin - P, P - BMax));
    float  BoxD    = length(Outside);
    float3 Spacing = (BMax - BMin) / float3(Dims - uint3(1u));
    float3 FCoord  = clamp((P - BMin) / Spacing, float3(0.0), float3(Dims - uint3(1u)));
    uint3  I0      = min(Dims - uint3(2u), uint3(FCoord));
    float3 L       = FCoord - float3(I0);
    float3 T       = L * L * (float3(3.0) - 2.0 * L);
    uint   SY      = Dims.x;
    uint   SZ      = Dims.x * Dims.y;
    uint   Base    = I0.z * SZ + I0.y * SY + I0.x;
    float C00 = lerp(Grid[Base],           Grid[Base + 1u],           T.x);
    float C10 = lerp(Grid[Base + SY],      Grid[Base + SY + 1u],      T.x);
    float C01 = lerp(Grid[Base + SZ],      Grid[Base + SZ + 1u],      T.x);
    float C11 = lerp(Grid[Base + SZ + SY], Grid[Base + SZ + SY + 1u], T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z) + BoxD;
}

float SampleClipmapDistanceGpu(float3 P)
{
    uint3 FineD   = PenumbraUniforms.FineClipDims.xyz;
    uint3 CoarseD = uint3(PenumbraUniforms.FineClipDims.w, PenumbraUniforms.CoarseClipDimsYZ.x, PenumbraUniforms.CoarseClipDimsYZ.y);
    float3 FMin   = PenumbraUniforms.FineBoundsMin.xyz;
    float3 FMax   = PenumbraUniforms.FineBoundsMax.xyz;
    float3 FCtr   = (FMin + FMax) * 0.5;
    float3 FHalf  = (FMax - FMin) * 0.5;
    float3 NormC  = abs(P - FCtr) / max(float3(1.0e-3), FHalf);
    float  MaxN   = max(NormC.x, max(NormC.y, NormC.z));
    float  DFine  = SampleGridHermiteGpu(FineDistanceGrid, FineD, FMin, FMax, P);
    if (MaxN <= 0.78) return DFine;
    float  DCoarse = SampleGridHermiteGpu(CoarseDistanceGrid, CoarseD, PenumbraUniforms.CoarseBoundsMin.xyz, PenumbraUniforms.CoarseBoundsMax.xyz, P);
    float  Blend   = SmoothStep01(0.78, 0.98, MaxN);
    return lerp(DFine, DCoarse, Blend);
}

float4 SampleCoarseRadiosityGpu(float3 P)
{
    float3 BMin = PenumbraUniforms.CoarseBoundsMin.xyz;
    float3 BMax = PenumbraUniforms.CoarseBoundsMax.xyz;
    if (any(P < BMin) || any(P > BMax)) return float4(0.0);
    uint3  Dims    = uint3(PenumbraUniforms.FineClipDims.w, PenumbraUniforms.CoarseClipDimsYZ.x, PenumbraUniforms.CoarseClipDimsYZ.y);
    float3 Spacing = (BMax - BMin) / float3(Dims - uint3(1u));
    float3 FCoord  = clamp((P - BMin) / Spacing, float3(0.0), float3(Dims - uint3(1u)));
    uint3  I0      = min(Dims - uint3(2u), uint3(FCoord));
    float3 F       = FCoord - float3(I0);
    uint   SY      = Dims.x;
    uint   SZ      = Dims.x * Dims.y;
    uint   Base    = I0.z * SZ + I0.y * SY + I0.x;
    float4 C00 = lerp(CoarseRadiosityOpacity[Base],           CoarseRadiosityOpacity[Base + 1u],           F.x);
    float4 C10 = lerp(CoarseRadiosityOpacity[Base + SY],      CoarseRadiosityOpacity[Base + SY + 1u],      F.x);
    float4 C01 = lerp(CoarseRadiosityOpacity[Base + SZ],      CoarseRadiosityOpacity[Base + SZ + 1u],      F.x);
    float4 C11 = lerp(CoarseRadiosityOpacity[Base + SZ + SY], CoarseRadiosityOpacity[Base + SZ + SY + 1u], F.x);
    return lerp(lerp(C00, C10, F.y), lerp(C01, C11, F.y), F.z);
}

float3 SampleNormalizedSurfaceRadiosityGpu(float3 HitP, float ApertureRadius)
{
    float R = clamp(ApertureRadius, 0.10, 0.65);
    static const float3 Offsets[7] = {
        float3( 0.0,  0.0,  0.0),
        float3( 1.0,  0.0,  0.0), float3(-1.0,  0.0,  0.0),
        float3( 0.0,  1.0,  0.0), float3( 0.0, -1.0,  0.0),
        float3( 0.0,  0.0,  1.0), float3( 0.0,  0.0, -1.0)
    };
    float3 SumPremul = float3(0.0);
    float  SumOp     = 0.0;
    for (uint J = 0u; J < 7u; ++J)
    {
        float  TapW = select(J == 0u, 2.0, 1.0);
        float4 RadO = SampleCoarseRadiosityGpu(HitP + Offsets[J] * R);
        SumPremul  += RadO.rgb * TapW;
        SumOp      += RadO.a * TapW;
    }
    if (SumOp <= 1.0e-4) return float3(0.0);
    return SumPremul / SumOp;
}

float3 EvaluateVoxelConeGiGpu(float3 Pos, float3 Nrm)
{
    static const float3 WorldCones[16] = {
        float3( 0.000000,  0.000000,  0.968750),
        float3(-0.355181,  0.324964,  0.876438),
        float3( 0.072949, -0.663417,  0.744675),
        float3( 0.546948,  0.594726,  0.589275),
        float3(-0.892399, -0.114912,  0.436390),
        float3( 0.697458, -0.642322,  0.317992),
        float3(-0.105917,  0.978177,  0.178415),
        float3(-0.630510, -0.774011,  0.056895),
        float3( 0.971825,  0.223346, -0.076190),
        float3(-0.785989,  0.593691, -0.172502),
        float3( 0.163627, -0.942641, -0.290953),
        float3( 0.546722,  0.728969, -0.411942),
        float3(-0.832541, -0.194251, -0.518789),
        float3( 0.562411, -0.491089, -0.665226),
        float3(-0.044225,  0.627963, -0.776985),
        float3(-0.289784, -0.295140, -0.910449)
    };

    float3 Origin       = Pos + Nrm * 0.05;
    float3 AccumExit    = float3(0.0);
    float  CosWeightSum = 0.0;
    for (uint K = 0u; K < 16u; ++K)
    {
        float3 ConeDir  = WorldCones[K];
        float  CosTheta = dot(Nrm, ConeDir);
        if (CosTheta <= 0.02) continue;

        float SafeX = select(abs(ConeDir.x) > 1.0e-5, ConeDir.x, select(ConeDir.x >= 0.0, 1.0e-5, -1.0e-5));
        float SafeY = select(abs(ConeDir.y) > 1.0e-5, ConeDir.y, select(ConeDir.y >= 0.0, 1.0e-5, -1.0e-5));
        float SafeZ = select(abs(ConeDir.z) > 1.0e-5, ConeDir.z, select(ConeDir.z >= 0.0, 1.0e-5, -1.0e-5));
        float TX    = (select(ConeDir.x > 0.0,  2.64, -2.64) - Origin.x) / SafeX;
        float TY    = (select(ConeDir.y > 0.0,  2.62, -2.45) - Origin.y) / SafeY;
        float TZ    = (select(ConeDir.z > 0.0,  3.16,  0.00) - Origin.z) / SafeZ;
        float SlabT = clamp(min(select(TX > 0.0, TX, 20.0), min(select(TY > 0.0, TY, 20.0), select(TZ > 0.0, TZ, 20.0))), 0.12, 8.5);

        float ConeVis = 1.0;
        for (uint Step = 1u; Step <= 8u; ++Step)
        {
            float DistT    = SlabT * (float(Step) / 9.0);
            float SelfFade = SmoothStep01(0.10, 0.42, DistT);
            if (SelfFade > 1.0e-3)
            {
                float OccD  = SampleClipmapDistanceGpu(Origin + ConeDir * DistT);
                float ConeR = max(0.14, 0.68 * DistT);
                float StepV = SmoothStep01(-0.95, 0.95, OccD / ConeR);
                ConeVis     = min(ConeVis, 1.0 - SelfFade * (1.0 - StepV));
            }
        }
        float3 WallHitP = Origin + ConeDir * SlabT;
        float3 WallExit = SampleNormalizedSurfaceRadiosityGpu(WallHitP, 0.44 * SlabT);
        float  Transfer = 0.34 + 0.66 * ConeVis;
        AccumExit      += WallExit * (CosTheta * Transfer);
        CosWeightSum   += CosTheta;
    }
    if (CosWeightSum <= 1.0e-5) return float3(0.0);
    return AccumExit * (PiConst / CosWeightSum);
}

void QueryMaterialGpu(uint Slot, float3 P, out float3 Albedo, out float Rough, out float Metal)
{
    Albedo = float3(0.72, 0.70, 0.65);
    Rough  = 0.80;
    Metal  = 0.0;
    if (Slot == 0u)
    {
        int2 C = int2(floor(P.xy / 0.58));
        bool Even = (((C.x + C.y) & 1) == 0);
        Albedo = select(Even, float3(0.75, 0.73, 0.68), float3(0.49, 0.49, 0.49));
        Rough  = select(Even, 0.56, 0.66);
    }
    else if (Slot == 2u) { Albedo = float3(0.78, 0.22, 0.10); }
    else if (Slot == 3u) { Albedo = float3(0.14, 0.46, 0.24); }
    else if (Slot == 4u) { Albedo = float3(0.44, 0.44, 0.44); Rough = 0.85; }
    else if (Slot == 5u) { Albedo = float3(0.74, 0.71, 0.66); Rough = 0.56; }
    else if (Slot == 6u) { Albedo = float3(0.64, 0.43, 0.22); Rough = 0.30; Metal = 0.25; }
    else if (Slot == 8u) { Albedo = float3(0.65, 0.65, 0.67); Rough = 0.52; }
}

bool TraceOccluderBlockedGpu(float3 Orig, float3 Dir, float MinT, float MaxT)
{
    float T = MinT;
    for (uint S = 0u; S < 96u && T < MaxT; ++S)
    {
        float D = EvaluateOccluderSdfGpu(Orig + Dir * T);
        if (D < 1.8e-3) return true;
        T += max(2.5e-3, D * 0.88);
    }
    return false;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchId : SV_DispatchThreadID)
{
    uint Width  = PenumbraUniforms.ViewportModeSamples.x;
    uint Height = PenumbraUniforms.ViewportModeSamples.y;
    if (DispatchId.x >= Width || DispatchId.y >= Height) return;

    uint   Mode     = PenumbraUniforms.ViewportModeSamples.z;
    uint   PixelIdx = DispatchId.y * Width + DispatchId.x;
    uint   PixelKey = Avalanche32Gpu(DispatchId.x * 1973u + DispatchId.y * 9277u + 0x68BC21EBu);

    float  NdcX   = 2.0 * ((float(DispatchId.x) + 0.5) / float(Width)) - 1.0;
    float  NdcY   = 1.0 - 2.0 * ((float(DispatchId.y) + 0.5) / float(Height));
    float  TanY   = PenumbraUniforms.CameraForwardTanY.w;
    float  Aspect = PenumbraUniforms.CameraRightAspect.w;
    float3 RayDir = normalize(
        PenumbraUniforms.CameraForwardTanY.xyz +
        PenumbraUniforms.CameraRightAspect.xyz * (NdcX * Aspect * TanY) +
        PenumbraUniforms.CameraUpPad.xyz * (NdcY * TanY));

    float3 Eye = PenumbraUniforms.CameraEyeExposure.xyz;
    float  T   = 0.01;
    uint   Slot = 0u;
    bool   Hit  = false;
    float3 HitP = float3(0.0);
    for (uint Step = 0u; Step < 128u && T < 16.0; ++Step)
    {
        float3 P = Eye + RayDir * T;
        float  D = EvaluateFullSceneSdfGpu(P, Slot);
        if (D < 1.2e-3)
        {
            Hit  = true;
            HitP = P;
            break;
        }
        T += max(1.2e-3, D * 0.92);
    }

    if (!Hit)
    {
        OutLinearHdrVis[PixelIdx] = float4(0.002, 0.002, 0.003, 0.0);
        return;
    }
    if (Slot == 7u)
    {
        OutLinearHdrVis[PixelIdx] = float4(PenumbraUniforms.EmissiveRadiancePad.xyz, 1.0);
        return;
    }

    float3 HitN = EvaluateGradientNormalGpu(HitP);
    float3 Albedo;
    float  Rough, Metal;
    QueryMaterialGpu(Slot, HitP, Albedo, Rough, Metal);

    float3 SphereC = PenumbraUniforms.EmissiveCenterRad.xyz;
    float  SphereR = PenumbraUniforms.EmissiveCenterRad.w;
    float3 SphereL = PenumbraUniforms.EmissiveRadiancePad.xyz;
    float3 ToS     = SphereC - HitP;
    float  Dist    = length(ToS);
    float3 L       = ToS / max(1.0e-4, Dist);
    float  EndT    = Dist - SphereR - 0.015;
    float  SinMax  = clamp(SphereR / max(SphereR + 1.0e-3, Dist), 1.0e-4, 0.9995);

    if (Mode == 4u || Mode == 5u)
    {
        uint   Samples     = PenumbraUniforms.ViewportModeSamples.w;
        float  CosThetaMax = sqrt(max(0.0, 1.0 - SinMax * SinMax));
        float  Omega       = TauConst * (1.0 - CosThetaMax);
        float3 TanU, BitV;
        BuildDuffBasisGpu(L, TanU, BitV);
        float3 Accum = float3(0.0);
        uint   ValidCnt = 0u, UnoccCnt = 0u;
        for (uint S = 0u; S < Samples; ++S)
        {
            float2 U    = SampleSobol02Gpu(S, PixelKey, 1u);
            float  CosT = 1.0 - U.x * (1.0 - CosThetaMax);
            float  SinT = sqrt(max(0.0, 1.0 - CosT * CosT));
            float  Phi  = TauConst * U.y;
            float3 Wi   = normalize(TanU * (SinT * cos(Phi)) + BitV * (SinT * sin(Phi)) + L * CosT);
            float  NoL  = dot(HitN, Wi);
            if (NoL <= 1.0e-5) continue;
            ++ValidCnt;
            if (!TraceOccluderBlockedGpu(HitP + HitN * 5.0e-3, Wi, 0.008, max(0.02, EndT)))
            {
                ++UnoccCnt;
                Accum += SphereL * Albedo * ((1.0 - Metal) / PiConst) * (NoL * Omega);
            }
        }
        float3 Hdr = Accum / float(max(1u, Samples));
        if (Mode == 5u)
        {
            float3 ConeE = EvaluateVoxelConeGiGpu(HitP, HitN);
            Hdr += Albedo * ((1.0 - Metal) / PiConst) * ConeE;
        }
        float Vis = select(ValidCnt > 0u, float(UnoccCnt) / float(max(1u, ValidCnt)), 0.0);
        OutLinearHdrVis[PixelIdx] = float4(Hdr, Vis);
        return;
    }

    float  Lift     = max(0.0, SinMax * 0.85 - dot(HitN, L));
    float3 ConeDirL = normalize(L + HitN * Lift);

    float Vis = 1.0;
    if (Mode == 0u)
    {
        bool Blocked = (dot(HitN, L) <= 0.0) || TraceOccluderBlockedGpu(HitP + HitN * 6.0e-3, L, 0.012, EndT);
        Vis = select(Blocked, 0.0, 1.0);
    }
    else if (Mode == 1u)
    {
        float TanCone  = SphereR / max(SphereR + 0.04, Dist);
        float MinRatio = 2.0;
        float Travel   = 0.03;
        for (uint S = 0u; S < 112u && Travel < EndT; ++S)
        {
            float3 SP    = HitP + HitN * 8.0e-3 + ConeDirL * Travel;
            float  PFade = SmoothStep01(0.015, 0.11, dot(SP - HitP, HitN));
            float  OD    = EvaluateOccluderSdfGpu(SP);
            if (PFade > 1.0e-4)
            {
                float ConeR = max(0.008, Travel * TanCone);
                float Ratio = clamp(OD / ConeR, -1.25, 1.25) * PFade + 1.25 * (1.0 - PFade);
                MinRatio    = SoftMinSdf(MinRatio, Ratio, 0.08);
                if (MinRatio <= -1.02) break;
            }
            Travel += clamp(abs(OD) * 0.45, 0.015, 0.12);
        }
        Vis = SmoothStep01(-1.0, 1.0, MinRatio);
    }
    else
    {
        float TanCone  = SphereR / max(SphereR + 0.04, Dist);
        float MinRatio = 2.0;
        float Travel   = 0.04;
        for (uint S = 0u; S < 96u && Travel < EndT; ++S)
        {
            float3 SP    = HitP + HitN * 1.0e-2 + ConeDirL * Travel;
            float  PFade = SmoothStep01(0.018, 0.12, dot(SP - HitP, HitN));
            float  CD    = SampleClipmapDistanceGpu(SP);
            if (PFade > 1.0e-4)
            {
                float ConeR = max(0.014, Travel * TanCone * 0.86);
                float Ratio = clamp(CD / ConeR, -1.25, 1.25) * PFade + 1.25 * (1.0 - PFade);
                MinRatio    = SoftMinSdf(MinRatio, Ratio, 0.10);
                if (MinRatio <= -1.02) break;
            }
            Travel += clamp(abs(CD) * 0.45, 0.020, 0.14);
        }
        Vis = SmoothStep01(-1.0, 1.0, MinRatio);
    }

    float CosN = dot(HitN, L);
    float FF   = 0.0;
    if (CosN > SinMax)
    {
        FF = SinMax * SinMax * CosN;
    }
    else if (CosN > -SinMax)
    {
        float TWrap = (CosN + SinMax) / (2.0 * SinMax);
        FF = SinMax * SinMax * SinMax * TWrap * TWrap * (2.0 - TWrap);
    }
    float3 Hdr = SphereL * Albedo * ((1.0 - Metal) * FF * Vis);
    if (Mode == 3u)
    {
        float3 ConeE = EvaluateVoxelConeGiGpu(HitP, HitN);
        Hdr += Albedo * ((1.0 - Metal) * 0.96 / PiConst) * ConeE;
    }
    OutLinearHdrVis[PixelIdx] = float4(Hdr, Vis);
}
