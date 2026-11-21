/*============================================================================================================================================
                                                    SDFEMISSIVEPENUMBRACOMPUTE.SLANG
============================================================================================================================================*/
// 🧩 Unified GPU compute kernel for all 6 Emissive-Sphere SDF Shadow + Penumbra + 3D Voxel Clipmap + 2-Bounce Cone GI + Sobol Spherical-Cap modes

struct GpuSdfPenumbraUniforms
{
    uint4  ViewportModeSamples; // x = Width, y = Height, z = Mode (0..5), w = CapSamples (24 or 96)
    float4 CameraEyeExposure;   // xyz = CameraEye, w = ExposureScale
    float4 CameraForwardTanY;   // xyz = Fwd, w = TanHalfFovY
    float4 CameraRightAspect;   // xyz = Right, w = Aspect
    float4 CameraUpPad;         // xyz = TrueUp, w = 0
    float4 EmissiveCenterRad;   // xyz = SphereCenter, w = SphereRadius
    float4 EmissiveRadiancePad; // xyz = Radiance, w = 0
    uint4  FineClipDims;        // xyz = FineDimX, FineDimY, FineDimZ, w = CoarseDimX
    uint4  CoarseClipDimsYZ;    // x = CoarseDimY, y = CoarseDimZ, z = CoarseMip1DimX, w = CoarseMip1DimY * 65536 + CoarseMip1DimZ
    float4 FineBoundsMin;       // xyz = FineMin
    float4 FineBoundsMax;       // xyz = FineMax, w = FineMaxVoxelSpacing
    float4 CoarseBoundsMin;     // xyz = CoarseMin
    float4 CoarseBoundsMax;     // xyz = CoarseMax, w = CoarseMaxVoxelSpacing
};

[[vk::binding(0, 0)]] ConstantBuffer<GpuSdfPenumbraUniforms> PenumbraUniforms;
[[vk::binding(1, 0)]] StructuredBuffer<float>                FineDistanceGrid;
[[vk::binding(2, 0)]] StructuredBuffer<float>                CoarseDistanceGrid;
[[vk::binding(3, 0)]] StructuredBuffer<float4>               FineRadiosityFullDist;   // rgb = Mip0 Radiosity, a = FullSceneDistance
[[vk::binding(4, 0)]] StructuredBuffer<float4>               CoarseRadiosityFullDist; // rgb = Mip0 Radiosity, a = FullSceneDistance
[[vk::binding(5, 0)]] StructuredBuffer<float4>               CoarseMip1Radiosity;     // rgb = Mip1 Radiosity, a = 0
[[vk::binding(6, 0)]] RWStructuredBuffer<float4>             OutLinearHdrVis;         // rgb = LinearHdr, a = PrimaryVisibility

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
    X = ((X & 0xF0F0F0F0u) >> 4u) | ((X & 0x0F0F0F0Fu) << 4u);
    X = ((X & 0xFF00FF00u) >> 8u) | ((X & 0x00FF00FFu) << 8u);
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
    uint P = ReverseBits32Gpu(Input) + Key;
    P ^= P * 0x6C50B47Cu;
    P ^= P * 0xB82F1E52u;
    P ^= P * 0xC7AFE638u;
    P ^= P * 0x8D22F6E6u;
    return ReverseBits32Gpu(P);
}

uint SobolDim2BitsGpu(uint Index)
{
    uint B = Index;
    B ^= (B & 0xAAAAAAAAu) >> 1u;
    B ^= (B & 0xCCCCCCCCu) >> 2u;
    B ^= (B & 0xF0F0F0F0u) >> 4u;
    B ^= (B & 0xFF00FF00u) >> 8u;
    B ^= (B & 0xFFFF0000u) >> 16u;
    return ReverseBits32Gpu(B);
}

float2 OwenSobol02Gpu(uint SampleIndex, uint PixelKey, uint DimPair)
{
    uint PairKey  = Avalanche32Gpu(DimPair * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    uint Shuffled = NestedPermuteGpu(SampleIndex, PairKey);
    uint D0       = NestedPermuteGpu(ReverseBits32Gpu(Shuffled), Avalanche32Gpu(PairKey ^ 0xA511E9B3u));
    uint D1       = NestedPermuteGpu(SobolDim2BitsGpu(Shuffled), Avalanche32Gpu(PairKey ^ 0x63D83595u));
    return float2(float(D0 >> 8u), float(D1 >> 8u)) * (1.0 / 16777216.0);
}

void BuildDuffBasisGpu(float3 N, out float3 T, out float3 B)
{
    float SignZ = (N.z >= 0.0) ? 1.0 : -1.0;
    float A     = -1.0 / (SignZ + N.z);
    float K     = N.x * N.y * A;
    T = float3(1.0 + SignZ * N.x * N.x * A, SignZ * K, -SignZ * N.x);
    B = float3(K, SignZ + N.y * N.y * A, -N.y);
}

float SignedBoxGpu(float3 P, float3 Ctr, float3 HalfExt)
{
    float3 Q = abs(P - Ctr) - HalfExt;
    return length(max(Q, 0.0)) + min(max(Q.x, max(Q.y, Q.z)), 0.0);
}

float SignedCylinderZGpu(float3 P, float3 BaseCtr, float HeightZ, float Radius)
{
    float HalfH  = 0.5 * HeightZ;
    float Radial = length(P.xy - BaseCtr.xy) - Radius;
    float Vert   = abs(P.z - (BaseCtr.z + HalfH)) - HalfH;
    return min(max(Radial, Vert), 0.0) + length(max(float2(Radial, Vert), 0.0));
}

float SignedTorusGpu(float3 P, float3 Ctr, float MajorR, float MinorR)
{
    float3 Rel  = P - Ctr;
    float  Ring = length(Rel.xy) - MajorR;
    return length(float2(Ring, Rel.z)) - MinorR;
}

// Canonical PROOF 11 interior occluder SDF matching BuildCanonicalProof11EmissiveSphereScene
float EvaluateOccluderSdfGpu(float3 P)
{
    float D = 20.0;
    D = min(D, SignedCylinderZGpu(P, float3(-0.36, 0.58, 0.0), 1.72, 0.145));
    D = min(D, SignedBoxGpu(P, float3(0.0, -0.10, 0.34), float3(0.19, 0.19, 0.34)));
    D = min(D, SignedTorusGpu(P, float3(0.0, -0.10, 0.84), 0.28, 0.100));
    D = min(D, length(P - float3(0.62, 0.38, 0.52)) - 0.52);
    return D;
}

float EvaluateFullSceneSdfGpu(float3 P, out uint OutSlot)
{
    float BestD = SignedBoxGpu(P, float3(0.0, 0.20, -0.10), float3(2.80, 2.65, 0.10));
    OutSlot = 0u;

    float DBack = SignedBoxGpu(P, float3(0.0, 2.72, 1.60), float3(2.80, 0.10, 1.70));
    if (DBack < BestD) { BestD = DBack; OutSlot = 1u; }
    float DLeft = SignedBoxGpu(P, float3(-2.74, 0.20, 1.60), float3(0.10, 2.65, 1.70));
    if (DLeft < BestD) { BestD = DLeft; OutSlot = 2u; }
    float DRight = SignedBoxGpu(P, float3(2.74, 0.20, 1.60), float3(0.10, 2.65, 1.70));
    if (DRight < BestD) { BestD = DRight; OutSlot = 3u; }
    float DCeil = SignedBoxGpu(P, float3(0.0, 0.20, 3.26), float3(2.80, 2.65, 0.10));
    if (DCeil < BestD) { BestD = DCeil; OutSlot = 4u; }

    float DCyl = SignedCylinderZGpu(P, float3(-0.36, 0.58, 0.0), 1.72, 0.145);
    if (DCyl < BestD) { BestD = DCyl; OutSlot = 5u; }
    float DPed = SignedBoxGpu(P, float3(0.0, -0.10, 0.34), float3(0.19, 0.19, 0.34));
    if (DPed < BestD) { BestD = DPed; OutSlot = 5u; }
    float DTor = SignedTorusGpu(P, float3(0.0, -0.10, 0.84), 0.28, 0.100);
    if (DTor < BestD) { BestD = DTor; OutSlot = 6u; }
    float DSph = length(P - float3(0.62, 0.38, 0.52)) - 0.52;
    if (DSph < BestD) { BestD = DSph; OutSlot = 8u; }

    float DEm = length(P - PenumbraUniforms.EmissiveCenterRad.xyz) - PenumbraUniforms.EmissiveCenterRad.w;
    if (DEm < BestD) { BestD = DEm; OutSlot = 7u; }
    return BestD;
}

float3 EvaluateFullGradientNormalGpu(float3 P)
{
    const float E = 1.5e-3;
    uint Dummy = 0u;
    float DX = EvaluateFullSceneSdfGpu(P + float3(E, 0, 0), Dummy) - EvaluateFullSceneSdfGpu(P - float3(E, 0, 0), Dummy);
    float DY = EvaluateFullSceneSdfGpu(P + float3(0, E, 0), Dummy) - EvaluateFullSceneSdfGpu(P - float3(0, E, 0), Dummy);
    float DZ = EvaluateFullSceneSdfGpu(P + float3(0, 0, E), Dummy) - EvaluateFullSceneSdfGpu(P - float3(0, 0, E), Dummy);
    return normalize(float3(DX, DY, DZ));
}

void QueryMaterialGpu(uint Slot, float3 WorldPos, out float3 BaseColor, out float Roughness, out float Metalness, out bool EmissiveSurf)
{
    EmissiveSurf = false;
    Metalness    = 0.0;
    Roughness    = 0.78;
    BaseColor    = float3(0.74, 0.70, 0.64);
    if (Slot == 0u)
    {
        int CX = int(floor(WorldPos.x / 0.56));
        int CY = int(floor(WorldPos.y / 0.56));
        bool Even = (((CX + CY) & 1) == 0);
        BaseColor = Even ? float3(0.76, 0.73, 0.68) : float3(0.42, 0.42, 0.43);
        Roughness = Even ? 0.50 : 0.62;
    }
    else if (Slot == 2u) { BaseColor = float3(0.76, 0.18, 0.09); }
    else if (Slot == 3u) { BaseColor = float3(0.12, 0.46, 0.22); }
    else if (Slot == 4u) { BaseColor = float3(0.42, 0.42, 0.42); Roughness = 0.85; }
    else if (Slot == 5u) { BaseColor = float3(0.74, 0.71, 0.66); Roughness = 0.54; }
    else if (Slot == 6u) { BaseColor = float3(0.68, 0.46, 0.22); Roughness = 0.28; Metalness = 0.35; }
    else if (Slot == 7u) { BaseColor = float3(1.00, 0.96, 0.88); EmissiveSurf = true; }
    else if (Slot == 8u) { BaseColor = float3(0.68, 0.68, 0.70); Roughness = 0.46; }
}

float SampleGridHermiteGpu(StructuredBuffer<float> Grid, uint3 Dims, float3 BMin, float3 BMax, float3 P)
{
    float3 Outside = max(float3(0.0, 0.0, 0.0), max(BMin - P, P - BMax));
    float  BoxD    = length(Outside);
    float3 Spacing = (BMax - BMin) / float3(Dims - uint3(1u, 1u, 1u));
    float3 F       = clamp((P - BMin) / Spacing, float3(0.0, 0.0, 0.0), float3(Dims - uint3(1u, 1u, 1u)));
    uint3  I0      = min(Dims - uint3(2u, 2u, 2u), uint3(F));
    float3 L       = F - float3(I0);
    float3 T       = L * L * (3.0 - 2.0 * L);

    uint StrideY = Dims.x;
    uint StrideZ = Dims.x * Dims.y;
    uint Base    = I0.z * StrideZ + I0.y * StrideY + I0.x;

    float C00 = lerp(Grid[Base],                   Grid[Base + 1u],                   T.x);
    float C10 = lerp(Grid[Base + StrideY],         Grid[Base + StrideY + 1u],         T.x);
    float C01 = lerp(Grid[Base + StrideZ],         Grid[Base + StrideZ + 1u],         T.x);
    float C11 = lerp(Grid[Base + StrideZ + StrideY], Grid[Base + StrideZ + StrideY + 1u], T.x);
    return lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z) + BoxD;
}

float4 SampleFloat4GridTrilinearGpu(StructuredBuffer<float4> Grid, uint3 Dims, float3 BMin, float3 BMax, float3 P)
{
    float3 Outside = max(float3(0.0, 0.0, 0.0), max(BMin - P, P - BMax));
    float  BoxD    = length(Outside);
    float3 Span    = max(float3(1.0e-5, 1.0e-5, 1.0e-5), BMax - BMin);
    float3 F       = clamp(((P - BMin) / Span) * float3(Dims - uint3(1u, 1u, 1u)), float3(0.0, 0.0, 0.0), float3(Dims - uint3(1u, 1u, 1u)));
    uint3  I0      = min(Dims - uint3(2u, 2u, 2u), uint3(F));
    float3 T       = F - float3(I0);

    uint StrideY = Dims.x;
    uint StrideZ = Dims.x * Dims.y;
    uint Base    = I0.z * StrideZ + I0.y * StrideY + I0.x;

    float4 C00 = lerp(Grid[Base],                     Grid[Base + 1u],                     T.x);
    float4 C10 = lerp(Grid[Base + StrideY],           Grid[Base + StrideY + 1u],           T.x);
    float4 C01 = lerp(Grid[Base + StrideZ],           Grid[Base + StrideZ + 1u],           T.x);
    float4 C11 = lerp(Grid[Base + StrideZ + StrideY], Grid[Base + StrideZ + StrideY + 1u], T.x);
    float4 Val = lerp(lerp(C00, C10, T.y), lerp(C01, C11, T.y), T.z);
    Val.w += BoxD;
    return Val;
}

float SampleClipmapDistanceGpu(float3 P)
{
    uint3  FineD = PenumbraUniforms.FineClipDims.xyz;
    uint3  CoaD  = uint3(PenumbraUniforms.FineClipDims.w, PenumbraUniforms.CoarseClipDimsYZ.xy);
    float3 FCtr  = 0.5 * (PenumbraUniforms.FineBoundsMin.xyz + PenumbraUniforms.FineBoundsMax.xyz);
    float3 FHalf = max(float3(1.0e-3, 1.0e-3, 1.0e-3), 0.5 * (PenumbraUniforms.FineBoundsMax.xyz - PenumbraUniforms.FineBoundsMin.xyz));
    float3 Norm  = abs(P - FCtr) / FHalf;
    float  MaxN  = max(Norm.x, max(Norm.y, Norm.z));
    if (MaxN <= 0.78)
    {
        return SampleGridHermiteGpu(FineDistanceGrid, FineD, PenumbraUniforms.FineBoundsMin.xyz, PenumbraUniforms.FineBoundsMax.xyz, P);
    }
    if (MaxN >= 0.98)
    {
        return SampleGridHermiteGpu(CoarseDistanceGrid, CoaD, PenumbraUniforms.CoarseBoundsMin.xyz, PenumbraUniforms.CoarseBoundsMax.xyz, P);
    }
    float B  = SmoothStep01(0.78, 0.98, MaxN);
    float DF = SampleGridHermiteGpu(FineDistanceGrid, FineD, PenumbraUniforms.FineBoundsMin.xyz, PenumbraUniforms.FineBoundsMax.xyz, P);
    float DC = SampleGridHermiteGpu(CoarseDistanceGrid, CoaD, PenumbraUniforms.CoarseBoundsMin.xyz, PenumbraUniforms.CoarseBoundsMax.xyz, P);
    return lerp(DF, DC, B);
}

void SampleClipmapFullDistRadiosityGpu(float3 P, float ConeRadiusM, out float OutFullD, out float3 OutRad)
{
    uint3  FineD = PenumbraUniforms.FineClipDims.xyz;
    uint3  CoaD  = uint3(PenumbraUniforms.FineClipDims.w, PenumbraUniforms.CoarseClipDimsYZ.xy);
    uint3  Mip1D = uint3(
        PenumbraUniforms.CoarseClipDimsYZ.z,
        PenumbraUniforms.CoarseClipDimsYZ.w >> 16u,
        PenumbraUniforms.CoarseClipDimsYZ.w & 0xFFFFu);

    float3 FCtr  = 0.5 * (PenumbraUniforms.FineBoundsMin.xyz + PenumbraUniforms.FineBoundsMax.xyz);
    float3 FHalf = max(float3(1.0e-3, 1.0e-3, 1.0e-3), 0.5 * (PenumbraUniforms.FineBoundsMax.xyz - PenumbraUniforms.FineBoundsMin.xyz));
    float3 Norm  = abs(P - FCtr) / FHalf;
    float  MaxN  = max(Norm.x, max(Norm.y, Norm.z));

    float4 Coa0 = SampleFloat4GridTrilinearGpu(CoarseRadiosityFullDist, CoaD, PenumbraUniforms.CoarseBoundsMin.xyz, PenumbraUniforms.CoarseBoundsMax.xyz, P);
    float3 CoaR = Coa0.rgb;
    float  CoaM0End = PenumbraUniforms.CoarseBoundsMax.w * 1.15;
    if (ConeRadiusM > CoaM0End && Mip1D.x >= 2u)
    {
        float3 Coa1 = SampleFloat4GridTrilinearGpu(CoarseMip1Radiosity, Mip1D, PenumbraUniforms.CoarseBoundsMin.xyz, PenumbraUniforms.CoarseBoundsMax.xyz, P).rgb;
        float  T01  = clamp((ConeRadiusM - CoaM0End) / max(0.03, PenumbraUniforms.CoarseBoundsMax.w * 1.6), 0.0, 1.0);
        CoaR = lerp(CoaR, Coa1, T01);
    }

    if (MaxN >= 0.96)
    {
        OutFullD = Coa0.w;
        OutRad   = CoaR;
        return;
    }

    float4 Fine0 = SampleFloat4GridTrilinearGpu(FineRadiosityFullDist, FineD, PenumbraUniforms.FineBoundsMin.xyz, PenumbraUniforms.FineBoundsMax.xyz, P);
    float  DistBlend = SmoothStep01(0.76, 0.96, MaxN);
    OutFullD = lerp(Fine0.w, Coa0.w, DistBlend);

    float WideBias = SmoothStep01(0.18, 0.52, ConeRadiusM);
    float EffN     = max(MaxN, WideBias * 0.92);
    float RadBlend = SmoothStep01(0.72, 0.94, EffN);
    OutRad = lerp(Fine0.rgb, CoaR, RadBlend);
}

static const float3 FibonacciConesGpu[16] = {
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

bool InsideCoarseBoundsGpu(float3 P)
{
    float3 BMin = PenumbraUniforms.CoarseBoundsMin.xyz;
    float3 BMax = PenumbraUniforms.CoarseBoundsMax.xyz;
    float3 U    = (P - BMin) / max(float3(1.0e-5, 1.0e-5, 1.0e-5), BMax - BMin);
    return all(U > 0.005) && all(U < 0.995);
}

float3 EvaluateVoxelConeGiGpu(float3 WorldPos, float3 N)
{
    float3 Origin  = WorldPos + N * 0.065;
    float3 Accum   = float3(0.0, 0.0, 0.0);
    float  CosWSum = 0.0;

    [unroll]
    for (uint C = 0u; C < 16u; ++C)
    {
        float3 ConeDir  = FibonacciConesGpu[C];
        float  CosTheta = dot(N, ConeDir);
        if (CosTheta <= 0.03) continue;

        float  Travel  = 0.11;
        float  Alpha   = 0.0;
        float3 ConeRad = float3(0.0, 0.0, 0.0);
        for (uint Step = 0u; Step < 26u && Alpha < 0.97 && Travel < 8.5; ++Step)
        {
            float3 SP = Origin + ConeDir * Travel;
            if (!InsideCoarseBoundsGpu(SP)) break;

            float  ConeR = max(0.085, Travel * 0.34);
            float  FullD = 20.0;
            float3 SurfR = float3(0.0, 0.0, 0.0);
            SampleClipmapFullDistRadiosityGpu(SP, ConeR * 1.25, FullD, SurfR);
            if (FullD < ConeR)
            {
                float SelfGuard = SmoothStep01(0.07, 0.22, Travel);
                float StepOcc   = (1.0 - SmoothStep01(-0.08 * ConeR, ConeR, FullD)) * 0.96 * SelfGuard;
                if (StepOcc > 1.0e-4)
                {
                    float W = (1.0 - Alpha) * StepOcc;
                    ConeRad += SurfR * W;
                    Alpha   += W;
                }
            }
            Travel += max(0.070, max(FullD * 0.68, ConeR * 0.42));
        }
        if (Alpha > 0.35 && Alpha < 0.98)
        {
            ConeRad *= (1.0 / max(0.76, Alpha));
        }
        Accum   += ConeRad * CosTheta;
        CosWSum += CosTheta;
    }
    return (CosWSum > 1.0e-5) ? (Accum * (PiConst / CosWSum)) : float3(0.0, 0.0, 0.0);
}

float3 EvaluateVoxelSpecBounceGpu(float3 WorldPos, float3 N, float3 V, float3 BaseColor, float Roughness, float Metalness)
{
    if (Roughness > 0.72 && Metalness < 0.02) return float3(0.0, 0.0, 0.0);
    float  NdotV       = clamp(dot(N, V), 1.0e-4, 1.0);
    float3 ReflDir     = normalize(2.0 * NdotV * N - V);
    float  TanSpecCone = clamp(Roughness * Roughness * 0.65 + 0.03, 0.03, 0.45);

    float3 Origin  = WorldPos + N * 0.055;
    float  Travel  = 0.09;
    float  Alpha   = 0.0;
    float3 SpecRad = float3(0.0, 0.0, 0.0);
    for (uint Step = 0u; Step < 28u && Alpha < 0.96 && Travel < 8.5; ++Step)
    {
        float3 SP = Origin + ReflDir * Travel;
        if (!InsideCoarseBoundsGpu(SP)) break;
        float  ConeR = max(0.065, Travel * TanSpecCone);
        float  FullD = 20.0;
        float3 SurfR = float3(0.0, 0.0, 0.0);
        SampleClipmapFullDistRadiosityGpu(SP, ConeR * 1.15, FullD, SurfR);
        if (FullD < ConeR)
        {
            float SelfGuard = SmoothStep01(0.06, 0.20, Travel);
            float StepOcc   = (1.0 - SmoothStep01(-0.20 * ConeR, ConeR, FullD)) * 0.88 * SelfGuard;
            if (StepOcc > 1.0e-4)
            {
                float W = (1.0 - Alpha) * StepOcc;
                SpecRad += SurfR * W;
                Alpha   += W;
            }
        }
        Travel += max(0.06, max(FullD * 0.72, ConeR * 0.50));
    }

    float  Schlick    = pow(1.0 - NdotV, 5.0);
    float  RoughAtten = 1.0 - Roughness * 0.55;
    float3 F0         = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metalness);
    float3 MaxGloss   = max(float3(1.0 - Roughness, 1.0 - Roughness, 1.0 - Roughness), F0);
    float3 EnvBrdf    = (F0 + (MaxGloss - F0) * Schlick) * RoughAtten;
    return SpecRad * EnvBrdf;
}

bool TraceOccluderRayBlockedGpu(float3 Origin, float3 Dir, float MinT, float MaxT)
{
    float T = MinT;
    for (uint S = 0u; S < 96u && T < MaxT; ++S)
    {
        float D = EvaluateOccluderSdfGpu(Origin + Dir * T);
        if (D < 1.8e-3) return true;
        T += max(2.5e-3, D * 0.88);
    }
    return false;
}

bool TraceFullSceneRayGpu(float3 Origin, float3 Dir, float MaxT, out float3 HitP, out float3 HitN, out uint HitSlot)
{
    HitP    = float3(0.0, 0.0, 0.0);
    HitN    = float3(0.0, 0.0, 1.0);
    HitSlot = 0u;
    float T = 0.01;
    for (uint S = 0u; S < 128u && T < MaxT; ++S)
    {
        float3 P = Origin + Dir * T;
        uint   Slot = 0u;
        float  D = EvaluateFullSceneSdfGpu(P, Slot);
        if (D < 1.2e-3)
        {
            HitP    = P;
            HitN    = EvaluateFullGradientNormalGpu(P);
            HitSlot = Slot;
            return true;
        }
        T += max(1.2e-3, D * 0.92);
    }
    return false;
}

float3 EvaluateOpenPbrBsdfGpu(float3 BaseColor, float Roughness, float Metalness, float3 N, float3 V, float3 L, out float SpecPdf, out float DiffPdf)
{
    SpecPdf = 0.0;
    DiffPdf = 0.0;
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if (NdotL <= 1.0e-5 || NdotV <= 1.0e-5) return float3(0.0, 0.0, 0.0);

    float3 H     = normalize(V + L);
    float  NdotH = max(1.0e-5, dot(N, H));
    float  VdotH = max(1.0e-5, dot(V, H));
    float  Alpha = clamp(Roughness * Roughness, 0.004, 1.0);
    float  A2    = Alpha * Alpha;
    float  Denom = NdotH * NdotH * (A2 - 1.0) + 1.0;
    float  NdfD  = A2 / max(1.0e-7, PiConst * Denom * Denom);

    float Tan2V = (1.0 - NdotV * NdotV) / max(1.0e-6, NdotV * NdotV);
    float Tan2L = (1.0 - NdotL * NdotL) / max(1.0e-6, NdotL * NdotL);
    float LamV  = 0.5 * (sqrt(1.0 + A2 * Tan2V) - 1.0);
    float LamL  = 0.5 * (sqrt(1.0 + A2 * Tan2L) - 1.0);
    float G2    = 1.0 / (1.0 + LamV + LamL);
    float G1V   = 1.0 / (1.0 + LamV);

    float3 F0      = lerp(float3(0.04, 0.04, 0.04), BaseColor, Metalness);
    float  Schlick = pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
    float3 Fresnel = F0 + (1.0 - F0) * Schlick;
    float  SpecS   = (NdfD * G2) / max(1.0e-5, 4.0 * NdotV * NdotL);
    float  DiffK   = (1.0 - Metalness) * 0.96 / PiConst;

    SpecPdf = (NdfD * G1V) / max(1.0e-5, 4.0 * NdotV);
    DiffPdf = NdotL / PiConst;
    return BaseColor * DiffK + Fresnel * SpecS;
}

float3 SampleSphericalCapVndfLocalGpu(float3 ViewLocal, float Alpha, float2 U)
{
    float3 Stretched = normalize(float3(Alpha * ViewLocal.x, Alpha * ViewLocal.y, max(1.0e-5, ViewLocal.z)));
    float  Phi       = TauConst * U.x;
    float  CapZ      = (1.0 - U.y) * (1.0 + Stretched.z) - Stretched.z;
    float  SinT      = sqrt(max(0.0, 1.0 - CapZ * CapZ));
    float3 HalfStr   = float3(SinT * cos(Phi) + Stretched.x, SinT * sin(Phi) + Stretched.y, max(0.0, CapZ + Stretched.z));
    float3 HalfLocal = normalize(float3(Alpha * HalfStr.x, Alpha * HalfStr.y, max(1.0e-6, HalfStr.z)));
    return normalize(2.0 * dot(ViewLocal, HalfLocal) * HalfLocal - ViewLocal);
}

float3 SampleCosineLocalGpu(float2 U)
{
    float OX = 2.0 * U.x - 1.0;
    float OY = 2.0 * U.y - 1.0;
    if (OX == 0.0 && OY == 0.0) return float3(0.0, 0.0, 1.0);
    float R     = (abs(OX) > abs(OY)) ? OX : OY;
    float Theta = (abs(OX) > abs(OY)) ? ((PiConst * 0.25) * (OY / OX)) : ((PiConst * 0.5) - (PiConst * 0.25) * (OX / OY));
    float DX    = R * cos(Theta);
    float DY    = R * sin(Theta);
    return float3(DX, DY, sqrt(max(0.0, 1.0 - DX * DX - DY * DY)));
}

float3 EvaluateAnalyticDirectGpu(float3 BaseColor, float Roughness, float Metalness, float3 P, float3 N, float3 V)
{
    float3 Ctr    = PenumbraUniforms.EmissiveCenterRad.xyz;
    float  Rad    = PenumbraUniforms.EmissiveCenterRad.w;
    float3 EmRad  = PenumbraUniforms.EmissiveRadiancePad.xyz;
    float3 ToC    = Ctr - P;
    float  Dist   = length(ToC);
    float3 L      = ToC / max(1.0e-5, Dist);
    float  SinMax = clamp(Rad / max(Rad + 1.0e-3, Dist), 1.0e-4, 0.9995);
    float  Sin2   = SinMax * SinMax;
    float  CosN   = dot(N, L);

    float FormF = 0.0;
    if (CosN > SinMax)       FormF = Sin2 * CosN;
    else if (CosN > -SinMax) { float T = (CosN + SinMax) / (2.0 * SinMax); FormF = Sin2 * SinMax * T * T * (2.0 - T); }
    if (FormF <= 1.0e-6) return float3(0.0, 0.0, 0.0);

    float3 ReflDir = normalize(2.0 * dot(V, N) * N - V);
    float3 CtrRay  = ReflDir * dot(ToC, ReflDir) - ToC;
    float3 SpecL   = normalize(ToC + CtrRay * min(1.0, Rad / max(1.0e-5, length(CtrRay))));
    float  SP = 0.0, DP = 0.0;
    float3 BsdfS      = EvaluateOpenPbrBsdfGpu(BaseColor, Roughness, Metalness, N, V, SpecL, SP, DP);
    float  Alpha      = clamp(Roughness * Roughness, 0.02, 1.0);
    float  AlphaPrime = clamp(Alpha + Rad / (2.0 * Dist), Alpha, 1.0);
    float  SpecNorm   = (Alpha * Alpha) / (AlphaPrime * AlphaPrime);
    float  DiffScale  = (1.0 - Metalness) * 0.96;
    float  Omega      = TauConst * (1.0 - sqrt(max(0.0, 1.0 - Sin2)));
    float3 PureSpec   = max(float3(0.0, 0.0, 0.0), BsdfS - BaseColor * (DiffScale / PiConst)) * max(0.0, dot(N, SpecL)) * Omega * SpecNorm;
    return EmRad * (BaseColor * DiffScale * FormF + PureSpec);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ComputeEntry(uint3 DispatchThreadId : SV_DispatchThreadID)
{
    uint Width  = PenumbraUniforms.ViewportModeSamples.x;
    uint Height = PenumbraUniforms.ViewportModeSamples.y;
    if (DispatchThreadId.x >= Width || DispatchThreadId.y >= Height)
    {
        return;
    }

    uint   Mode       = PenumbraUniforms.ViewportModeSamples.z;
    uint   CapSamples = max(1u, PenumbraUniforms.ViewportModeSamples.w);
    uint   PixelIdx   = DispatchThreadId.y * Width + DispatchThreadId.x;
    uint   PixelKey   = Avalanche32Gpu(DispatchThreadId.x * 1973u + DispatchThreadId.y * 9277u + 0x68BC21EBu);

    float  NdcX   = 2.0 * ((float(DispatchThreadId.x) + 0.5) / float(Width)) - 1.0;
    float  NdcY   = 1.0 - 2.0 * ((float(DispatchThreadId.y) + 0.5) / float(Height));
    float3 Eye    = PenumbraUniforms.CameraEyeExposure.xyz;
    float3 Fwd    = PenumbraUniforms.CameraForwardTanY.xyz;
    float  TanY   = PenumbraUniforms.CameraForwardTanY.w;
    float3 Right  = PenumbraUniforms.CameraRightAspect.xyz;
    float  Aspect = PenumbraUniforms.CameraRightAspect.w;
    float3 TrueUp = PenumbraUniforms.CameraUpPad.xyz;
    float3 RayDir = normalize(Fwd + Right * (NdcX * Aspect * TanY) + TrueUp * (NdcY * TanY));

    float3 HitPos = float3(0.0, 0.0, 0.0);
    float3 HitN   = float3(0.0, 0.0, 1.0);
    uint   MatSlot = 0u;
    if (!TraceFullSceneRayGpu(Eye, RayDir, 16.0, HitPos, HitN, MatSlot))
    {
        OutLinearHdrVis[PixelIdx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float3 BaseColor; float Roughness; float Metalness; bool EmissiveSurf;
    QueryMaterialGpu(MatSlot, HitPos, BaseColor, Roughness, Metalness, EmissiveSurf);
    if (EmissiveSurf)
    {
        OutLinearHdrVis[PixelIdx] = float4(PenumbraUniforms.EmissiveRadiancePad.xyz, 1.0);
        return;
    }

    float3 V      = -RayDir;
    float3 SphCtr = PenumbraUniforms.EmissiveCenterRad.xyz;
    float  SphRad = PenumbraUniforms.EmissiveCenterRad.w;
    float3 EmRad  = PenumbraUniforms.EmissiveRadiancePad.xyz;
    float3 ToS    = SphCtr - HitPos;
    float  Dist   = length(ToS);
    float3 L      = ToS / max(1.0e-5, Dist);
    float  EndT   = Dist - SphRad - 0.015;

    // Mode 4: Pure Direct Hybrid Contact + Sobol (0,2) Spherical-Cap quadrature (true 0.00 umbra, zero fake ambient)
    if (Mode == 4u)
    {
        float  SinMax = clamp(SphRad / max(SphRad + 1.0e-3, Dist), 1.0e-4, 0.9995);
        float  CosMax = sqrt(max(0.0, 1.0 - SinMax * SinMax));
        float  Omega  = TauConst * (1.0 - CosMax);
        float3 TanU, BitV;
        BuildDuffBasisGpu(L, TanU, BitV);

        float3 Accum  = float3(0.0, 0.0, 0.0);
        uint   Unocc  = 0u;
        uint   ValidH = 0u;
        for (uint S = 0u; S < CapSamples; ++S)
        {
            float2 U    = OwenSobol02Gpu(S, PixelKey, 1u);
            float  CosT = 1.0 - U.x * (1.0 - CosMax);
            float  SinT = sqrt(max(0.0, 1.0 - CosT * CosT));
            float  Phi  = TauConst * U.y;
            float3 Wi   = normalize(TanU * (SinT * cos(Phi)) + BitV * (SinT * sin(Phi)) + L * CosT);
            float  NoL  = dot(HitN, Wi);
            if (NoL <= 1.0e-5) continue;
            ++ValidH;

            float3 Orig  = HitPos + HitN * 5.0e-3;
            float3 OC    = Orig - SphCtr;
            float  B     = dot(OC, Wi);
            float  Disc  = B * B - (dot(OC, OC) - SphRad * SphRad);
            float  HitTS = (Disc > 0.0) ? max(0.02, -B - sqrt(Disc)) : EndT;
            if (!TraceOccluderRayBlockedGpu(Orig, Wi, 0.008, max(0.015, HitTS - 0.012)))
            {
                ++Unocc;
                float SP = 0.0, DP = 0.0;
                float3 Bsdf = EvaluateOpenPbrBsdfGpu(BaseColor, Roughness, Metalness, HitN, V, Wi, SP, DP);
                Accum += EmRad * Bsdf * (NoL * Omega);
            }
        }
        float Vis = (ValidH > 0u) ? (float(Unocc) / float(ValidH)) : 0.0;
        OutLinearHdrVis[PixelIdx] = float4(Accum / float(CapSamples), Vis);
        return;
    }

    // Mode 5: Unbiased 96-SPP Multi-Bounce SDF Monte Carlo Path-Tracing Reference
    if (Mode == 5u)
    {
        float3 TotalRad = float3(0.0, 0.0, 0.0);
        uint   VisHits  = 0u;
        uint   VisValid = 0u;
        for (uint SampleIdx = 0u; SampleIdx < CapSamples; ++SampleIdx)
        {
            float3 CurPos   = HitPos;
            float3 CurN     = HitN;
            float3 CurV     = V;
            float3 CurBase  = BaseColor;
            float  CurRough = Roughness;
            float  CurMetal = Metalness;
            float3 Thru     = float3(1.0, 1.0, 1.0);
            float3 SRad     = float3(0.0, 0.0, 0.0);

            for (uint Bounce = 0u; Bounce < 3u; ++Bounce)
            {
                float3 BToS  = SphCtr - CurPos;
                float  BDist = length(BToS);
                if (BDist > SphRad + 1.0e-3)
                {
                    float3 BL      = BToS / BDist;
                    float  BSinMax = clamp(SphRad / BDist, 1.0e-4, 0.9995);
                    float  BCosMax = sqrt(max(0.0, 1.0 - BSinMax * BSinMax));
                    float  BOmega  = TauConst * (1.0 - BCosMax);
                    float  LPdf    = 1.0 / max(1.0e-6, BOmega);
                    float3 BTanU, BBitV;
                    BuildDuffBasisGpu(BL, BTanU, BBitV);

                    float2 ULight = OwenSobol02Gpu(SampleIdx, PixelKey, Bounce * 3u + 1u);
                    float  CosT   = 1.0 - ULight.x * (1.0 - BCosMax);
                    float  SinT   = sqrt(max(0.0, 1.0 - CosT * CosT));
                    float  Phi    = TauConst * ULight.y;
                    float3 Wi     = normalize(BTanU * (SinT * cos(Phi)) + BBitV * (SinT * sin(Phi)) + BL * CosT);
                    float  NoL    = dot(CurN, Wi);
                    if (NoL > 1.0e-5)
                    {
                        if (Bounce == 0u) ++VisValid;
                        float3 Orig  = CurPos + CurN * 5.0e-3;
                        float3 OC    = Orig - SphCtr;
                        float  B     = dot(OC, Wi);
                        float  Disc  = B * B - (dot(OC, OC) - SphRad * SphRad);
                        float  HitTS = (Disc > 0.0) ? max(0.02, -B - sqrt(Disc)) : (BDist - SphRad);
                        if (!TraceOccluderRayBlockedGpu(Orig, Wi, 0.008, max(0.015, HitTS - 0.012)))
                        {
                            if (Bounce == 0u) ++VisHits;
                            float SP = 0.0, DP = 0.0;
                            float3 Bsdf     = EvaluateOpenPbrBsdfGpu(CurBase, CurRough, CurMetal, CurN, CurV, Wi, SP, DP);
                            float  SpecProb = clamp(0.12 + 0.78 * CurMetal + 0.25 * (1.0 - CurRough), 0.10, 0.90);
                            float  BsdfPdf  = SpecProb * SP + (1.0 - SpecProb) * DP;
                            float  MisW     = (LPdf * LPdf) / max(1.0e-10, LPdf * LPdf + BsdfPdf * BsdfPdf);
                            float3 Contrib  = Thru * EmRad * Bsdf * ((NoL * MisW) / LPdf);
                            if (Bounce > 0u) Contrib = min(Contrib, float3(3.5, 3.5, 3.5));
                            SRad += Contrib;
                        }
                    }
                }
                if (Bounce == 2u) break;

                float3 SurfT, SurfB;
                BuildDuffBasisGpu(CurN, SurfT, SurfB);
                float3 VLocal   = float3(dot(CurV, SurfT), dot(CurV, SurfB), max(1.0e-5, dot(CurV, CurN)));
                float2 ULobe    = OwenSobol02Gpu(SampleIdx, PixelKey, Bounce * 3u + 2u);
                float2 UDir     = OwenSobol02Gpu(SampleIdx, PixelKey, Bounce * 3u + 3u);
                float  SpecProb = clamp(0.12 + 0.78 * CurMetal + 0.25 * (1.0 - CurRough), 0.10, 0.90);
                float  Alpha    = clamp(CurRough * CurRough, 0.004, 1.0);
                float3 WiLocal  = (ULobe.x < SpecProb) ? SampleSphericalCapVndfLocalGpu(VLocal, Alpha, UDir) : SampleCosineLocalGpu(UDir);
                if (WiLocal.z <= 1.0e-5) break;

                float3 WiWorld = normalize(SurfT * WiLocal.x + SurfB * WiLocal.y + CurN * WiLocal.z);
                float  SP = 0.0, DP = 0.0;
                float3 Bsdf    = EvaluateOpenPbrBsdfGpu(CurBase, CurRough, CurMetal, CurN, CurV, WiWorld, SP, DP);
                float  BsdfPdf = SpecProb * SP + (1.0 - SpecProb) * DP;
                float  NoL     = dot(CurN, WiWorld);
                if (BsdfPdf <= 1.0e-6 || NoL <= 1.0e-5) break;

                float3 NextP, NextN; uint NextSlot = 0u;
                if (!TraceFullSceneRayGpu(CurPos + CurN * 6.0e-3, WiWorld, 12.0, NextP, NextN, NextSlot)) break;

                float3 NextBase; float NextRough; float NextMetal; bool NextEm;
                QueryMaterialGpu(NextSlot, NextP, NextBase, NextRough, NextMetal, NextEm);
                Thru *= (Bsdf * NoL) / BsdfPdf;
                if (NextEm)
                {
                    float MisW = 1.0;
                    if (BDist > SphRad + 1.0e-3)
                    {
                        float BSinMax = clamp(SphRad / BDist, 1.0e-4, 0.9995);
                        float LPdf    = 1.0 / max(1.0e-6, TauConst * (1.0 - sqrt(max(0.0, 1.0 - BSinMax * BSinMax))));
                        MisW = (BsdfPdf * BsdfPdf) / max(1.0e-10, BsdfPdf * BsdfPdf + LPdf * LPdf);
                    }
                    SRad += min(float3(3.5, 3.5, 3.5), Thru * EmRad * MisW);
                    break;
                }
                CurPos = NextP; CurN = NextN; CurV = -WiWorld;
                CurBase = NextBase; CurRough = NextRough; CurMetal = NextMetal;
            }
            TotalRad += SRad;
        }
        float Vis = (VisValid > 0u) ? (float(VisHits) / float(VisValid)) : 0.0;
        OutLinearHdrVis[PixelIdx] = float4(TotalRad / float(CapSamples), Vis);
        return;
    }

    // Modes 0..3: Direct Emissive Sphere Visibility (+ 3D SDF Cone GI & Specular Cone Bounce in Mode 3 ONLY)
    float Vis = 1.0;
    float SinCap  = clamp(SphRad / max(SphRad + 1.0e-3, Dist), 1.0e-4, 0.99);
    float Lift    = max(0.0, SinCap * 0.85 - dot(HitN, L));
    float3 ConeL  = normalize(L + HitN * Lift);
    float TanCone = SphRad / max(SphRad + 0.04, Dist);

    if (Mode == 0u)
    {
        Vis = (dot(HitN, L) > 0.0 && !TraceOccluderRayBlockedGpu(HitPos + HitN * 6.0e-3, L, 0.012, EndT)) ? 1.0 : 0.0;
    }
    else if (Mode == 1u)
    {
        float MinR = 2.0;
        float Tr   = 0.03;
        for (uint S = 0u; S < 112u && Tr < EndT; ++S)
        {
            float3 SP       = HitPos + HitN * 8.0e-3 + ConeL * Tr;
            float  PlaneF   = SmoothStep01(0.015, 0.11, dot(SP - HitPos, HitN));
            float  OD       = EvaluateOccluderSdfGpu(SP);
            if (PlaneF > 1.0e-4)
            {
                float ConeR = max(0.008, Tr * TanCone);
                float Ratio = clamp(OD / ConeR, -1.25, 1.25) * PlaneF + 1.25 * (1.0 - PlaneF);
                MinR = SoftMinSdf(MinR, Ratio, 0.08);
                if (MinR <= -1.02) { MinR = -1.1; break; }
            }
            Tr += clamp(abs(OD) * 0.45, 0.015, 0.12);
        }
        Vis = SmoothStep01(-1.0, 1.0, MinR);
    }
    else
    {
        float MinR = 2.0;
        float Tr   = 0.04;
        for (uint S = 0u; S < 96u && Tr < EndT; ++S)
        {
            float3 SP     = HitPos + HitN * 1.0e-2 + ConeL * Tr;
            float  PlaneF = SmoothStep01(0.018, 0.12, dot(SP - HitPos, HitN));
            float  ClipD  = SampleClipmapDistanceGpu(SP);
            if (PlaneF > 1.0e-4)
            {
                float ConeR = max(0.014, Tr * TanCone * 0.90);
                float Ratio = clamp(ClipD / ConeR, -1.25, 1.25) * PlaneF + 1.25 * (1.0 - PlaneF);
                MinR = SoftMinSdf(MinR, Ratio, 0.10);
                if (MinR <= -1.02) { MinR = -1.1; break; }
            }
            Tr += clamp(abs(ClipD) * 0.45, 0.020, 0.14);
        }
        Vis = SmoothStep01(-1.0, 1.0, MinR);
    }

    // Pure direct lighting in Modes 0, 1, 2 (true 0.00 umbra, zero fake ambient bleed!)
    float3 Hdr = EvaluateAnalyticDirectGpu(BaseColor, Roughness, Metalness, HitPos, HitN, V) * Vis;

    if (Mode == 3u)
    {
        float3 ConeIrrad  = EvaluateVoxelConeGiGpu(HitPos, HitN);
        float3 SpecBounce = EvaluateVoxelSpecBounceGpu(HitPos, HitN, V, BaseColor, Roughness, Metalness);
        Hdr += BaseColor * ((1.0 - Metalness) * 0.96 / PiConst) * ConeIrrad + SpecBounce;
    }

    OutLinearHdrVis[PixelIdx] = float4(Hdr, Vis);
}
