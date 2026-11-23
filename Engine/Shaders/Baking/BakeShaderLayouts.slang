/*============================================================================================================================================
                                                           BAKESHADERLAYOUTS.SLANG
============================================================================================================================================*/
// 🧩 Shared GPU structures, mathematical kernels, Fibonacci hemisphere sampling & SDF colormaps for GTX BVH4 + RTX RayQuery baking.

#ifndef FRONTIER_BAKE_SHADER_LAYOUTS_SLANG
#define FRONTIER_BAKE_SHADER_LAYOUTS_SLANG

struct GpuBakeTriangle
{
    float3 CornerA;
    uint   PartOrdinal;
    float3 CornerB;
    uint   IslandOrdinal;
    float3 CornerC;
    uint   MaterialOrdinal;
    float3 NormalA;
    float  AreaWeight;
    float3 NormalB;
    float  PadB;
    float3 NormalC;
    float  PadC;
    float3 GeometricNormal;
    float  PadGeom;
};

struct GpuLowSurfaceVertex
{
    float3 Position;
    float  PartOrdinal;
    float3 SmoothNormal;
    float  IslandOrdinal;
    float3 Tangent;
    float  Handedness;
    float3 Bitangent;
    float  UvCoordU;
    float  UvCoordV;
    float  BarycentricX;
    float  BarycentricY;
    float  WireMask;
};

struct GpuSurfaceBakeConfig
{
    uint   ResolutionPx;
    uint   HighTriangleCount;
    uint   WideEntryCount;
    uint   AoSampleCount;
    float  FrontCageMeters;
    float  RearCageMeters;
    float  AoMaxRayDistanceMeters;
    float  TangentSignY;
    float3 BoundsMin;
    float  BoundsDiagMeters;
    float3 BoundsMax;
    float  BevelSigmaMeters;
    uint   ActiveTargetBitmask;
    uint   DilationRadiusPx;
    uint   DilationStepIndex;
    uint   Pad0;
};

struct GpuVolumeBakeConfig
{
    uint   GridDim;
    uint   HighTriangleCount;
    uint   WideEntryCount;
    uint   SignedMode;
    float3 BoundsMin;
    float  VoxelStepMeters;
    float3 BoundsMax;
    float  BoundsDiagMeters;
    uint   SectionAxis;
    float  SectionDepthNormalized;
    uint   SectionSizePx;
    uint   Pad0;
};

struct GpuPreviewConfig
{
    float4x4 ViewProjTransform;
    float4   CameraEyeMode;     // xyz = CameraWorldPos, w = InspectChannelIdx (0..17)
    float4   OverlayFlagsPacked;      // x = ShowLitShading (1/0), y = Unused, z = CageOffsetMeters, w = HighWireOpacity
    float4   ViewportSizeTime;  // xy = Width/Height, z = OverlayAlpha, w = ActiveWireMode (0=Lit, 1=HighWire, 2=Cage)
    float4   OverlayColor;
    float4   BoundsMinPacked;
    float4   BoundsMaxPacked;
};

float BoxPointDistanceSq(float3 P, float3 BoxMin, float3 BoxMax)
{
    float3 Delta = max(BoxMin - P, max(P - BoxMax, float3(0.0, 0.0, 0.0)));
    return dot(Delta, Delta);
}

float PointToTriangleDistanceSq(float3 P, float3 A, float3 B, float3 C)
{
    float3 AB = B - A;
    float3 AC = C - A;
    float3 AP = P - A;
    float  D1 = dot(AB, AP);
    float  D2 = dot(AC, AP);
    if (D1 <= 0.0 && D2 <= 0.0)
    {
        return dot(AP, AP);
    }

    float3 BP = P - B;
    float  D3 = dot(AB, BP);
    float  D4 = dot(AC, BP);
    if (D3 >= 0.0 && D4 <= D3)
    {
        return dot(BP, BP);
    }

    float VC = D1 * D4 - D3 * D2;
    if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
    {
        float V = D1 / max(1.0e-12, D1 - D3);
        float3 Q = A + AB * V - P;
        return dot(Q, Q);
    }

    float3 CP = P - C;
    float  D5 = dot(AB, CP);
    float  D6 = dot(AC, CP);
    if (D6 >= 0.0 && D5 <= D6)
    {
        return dot(CP, CP);
    }

    float VB = D5 * D2 - D1 * D6;
    if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
    {
        float W = D2 / max(1.0e-12, D2 - D6);
        float3 Q = A + AC * W - P;
        return dot(Q, Q);
    }

    float VA = D3 * D6 - D5 * D4;
    if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
    {
        float W = (D4 - D3) / max(1.0e-12, (D4 - D3) + (D5 - D6));
        float3 Q = B + (C - B) * W - P;
        return dot(Q, Q);
    }

    float Denom = 1.0 / max(1.0e-12, VA + VB + VC);
    float V     = VB * Denom;
    float W     = VC * Denom;
    float3 Q    = A + AB * V + AC * W - P;
    return dot(Q, Q);
}

void BuildFrisvadBasis(float3 N, out float3 T, out float3 B)
{
    if (N.z < -0.999999)
    {
        T = float3(0.0, -1.0, 0.0);
        B = float3(-1.0, 0.0, 0.0);
        return;
    }
    float A  = 1.0 / (1.0 + N.z);
    float BB = -N.x * N.y * A;
    T = float3(1.0 - N.x * N.x * A, BB, -N.x);
    B = float3(BB, 1.0 - N.y * N.y * A, -N.y);
}

float TexelHashPhase(uint2 PixelCoord)
{
    uint H = PixelCoord.x * 1973u + PixelCoord.y * 9277u + 26699u;
    H = (H ^ (H >> 13u)) * 1274126177u;
    H = H ^ (H >> 16u);
    return float(H & 0x00FFFFFFu) / 16777216.0;
}

float3 EvaluateFibonacciHemisphereDir(uint SampleIdx, uint SampleCount, float3 N, float Phase)
{
    float U         = (float(SampleIdx) + 0.5) / max(1.0, float(SampleCount));
    float SinTheta  = sqrt(max(0.0, U));
    float CosTheta  = sqrt(max(0.0, 1.0 - U));
    float Phi       = 6.28318530718 * frac(float(SampleIdx) * 0.61803398875 + Phase);

    float3 T;
    float3 B;
    BuildFrisvadBasis(N, T, B);

    float3 LocalDir = float3(cos(Phi) * SinTheta, sin(Phi) * SinTheta, CosTheta);
    return normalize(T * LocalDir.x + B * LocalDir.y + N * LocalDir.z);
}

float3 IndexSwatchColor(uint Index)
{
    float Hue = frac(float(Index + 1u) * 0.61803398875 + 0.13);
    float3 RawRgb = clamp(abs(frac(Hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return 0.54 + (RawRgb - 0.5) * 0.62;
}

float3 ColorizeSdfDistance(float DistMeters, float ScaleSpan, bool SignedMode)
{
    float SafeSpan = max(1.0e-4, ScaleSpan);
    float SignedD  = SignedMode ? DistMeters : abs(DistMeters);
    float NormD    = clamp(SignedD / SafeSpan, -1.0, 1.0);

    float IsoBands = 0.88 + 0.12 * cos(SignedD / max(1.0e-4, SafeSpan * 0.032) * 6.2831853);
    float3 ZeroCol = float3(0.937, 0.957, 0.812); // Studio iso-zero highlight cream

    float3 Color;
    if (NormD < 0.0)
    {
        float T = saturate(-NormD * 2.1);
        float3 InnerWarm = float3(0.84, 0.34, 0.21);
        float3 DeepInterior = float3(0.49, 0.14, 0.10);
        Color = lerp(InnerWarm, DeepInterior, T) * IsoBands;
    }
    else
    {
        float T = saturate(NormD * 1.45);
        float3 NearSea  = float3(0.24, 0.54, 0.78);
        float3 DeepVoid = float3(0.08, 0.14, 0.24);
        Color = lerp(NearSea, DeepVoid, T) * IsoBands;
    }

    float IsoWidth = max(1.0e-4, SafeSpan * 0.014);
    float IsoMask  = 1.0 - smoothstep(0.0, IsoWidth, abs(SignedD));
    return lerp(Color, ZeroCol, IsoMask);
}

#endif
