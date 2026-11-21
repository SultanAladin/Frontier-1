/*============================================================================================================================================
                                                    SDFEMISSIVEPENUMBRASEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Unified 6-Mode SDF Shadow, 3D Voxel SDF Clipmap Penumbra, 2-Bounce Voxel Cone GI, Sobol Spherical-Cap, & 96-SPP SDF PT Sequence

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/SdfEmissivePenumbraSequence.h"

#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace Frontier::GeometricRaster::DirectLighting
{

namespace
{

[[nodiscard]] float SmoothHermite01(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-6f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float SoftMinimum(float A, float B, float K) noexcept
{
    const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
    return std::min(A, B) - H * H * K * 0.25f;
}

[[nodiscard]] uint32_t ReverseBits32(uint32_t X) noexcept
{
    X = ((X & 0x55555555u) << 1u) | ((X >> 1u) & 0x55555555u);
    X = ((X & 0x33333333u) << 2u) | ((X >> 2u) & 0x33333333u);
    X = ((X & 0x0F0F0F0Fu) << 4u) | ((X >> 4u) & 0x0F0F0F0Fu);
    X = ((X & 0x00FF00FFu) << 8u) | ((X >> 8u) & 0x00FF00FFu);
    return (X << 16u) | (X >> 16u);
}

[[nodiscard]] uint32_t AvalancheKey32(uint32_t Input) noexcept
{
    uint32_t Mixed = Input;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

[[nodiscard]] uint32_t LaineKarrasPermute(uint32_t Input, uint32_t Key) noexcept
{
    uint32_t P = Input + Key;
    P ^= P * 0x6C50B47Cu;
    P ^= P * 0xB82F1E52u;
    P ^= P * 0xC7AFE638u;
    P ^= P * 0x8D22F6E6u;
    return P;
}

[[nodiscard]] uint32_t NestedDigitPermute(uint32_t Input, uint32_t Key) noexcept
{
    return ReverseBits32(LaineKarrasPermute(ReverseBits32(Input), Key));
}

[[nodiscard]] uint32_t SobolSecondDimensionBits(uint32_t Index) noexcept
{
    uint32_t Bits = Index;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

void BuildDuffOrthonormalBasis(
    Mathematics::Axis3D  Normal,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept
{
    const float SignZ = (Normal.Z >= 0.0f) ? 1.0f : -1.0f;
    const float A     = -1.0f / (SignZ + Normal.Z);
    const float B     = Normal.X * Normal.Y * A;
    OutTangent   = { 1.0f + SignZ * Normal.X * Normal.X * A, SignZ * B, -SignZ * Normal.X };
    OutBitangent = { B, SignZ + Normal.Y * Normal.Y * A, -Normal.Y };
}

[[nodiscard]] float EvaluateSignedBox(Mathematics::Axis3D P, Mathematics::Axis3D Center, Mathematics::Axis3D HalfExt) noexcept
{
    const Mathematics::Axis3D Q {
        std::fabs(P.X - Center.X) - HalfExt.X,
        std::fabs(P.Y - Center.Y) - HalfExt.Y,
        std::fabs(P.Z - Center.Z) - HalfExt.Z,
    };
    const Mathematics::Axis3D Outside { std::max(Q.X, 0.0f), std::max(Q.Y, 0.0f), std::max(Q.Z, 0.0f) };
    const float Inside = std::min(std::max(Q.X, std::max(Q.Y, Q.Z)), 0.0f);
    return Mathematics::EvaluateLength(Outside) + Inside;
}

[[nodiscard]] float EvaluateSignedCappedCylinderZ(
    Mathematics::Axis3D P,
    Mathematics::Axis3D BaseCenter,
    float               HeightZ,
    float               Radius) noexcept
{
    const float HalfH  = 0.5f * HeightZ;
    const float DX     = P.X - BaseCenter.X;
    const float DY     = P.Y - BaseCenter.Y;
    const float Radial = std::sqrt(DX * DX + DY * DY) - Radius;
    const float Vert   = std::fabs((P.Z - (BaseCenter.Z + HalfH))) - HalfH;
    const float OutR   = std::max(Radial, 0.0f);
    const float OutV   = std::max(Vert, 0.0f);
    return std::min(std::max(Radial, Vert), 0.0f) + std::sqrt(OutR * OutR + OutV * OutV);
}

[[nodiscard]] float EvaluateSignedTorus(
    Mathematics::Axis3D P,
    Mathematics::Axis3D Center,
    float               MajorRadius,
    float               MinorRadius,
    float               TiltRadiansX) noexcept
{
    const Mathematics::Axis3D Rel = P - Center;
    const float CosT = std::cos(-TiltRadiansX);
    const float SinT = std::sin(-TiltRadiansX);
    const float LY   = Rel.Y * CosT - Rel.Z * SinT;
    const float LZ   = Rel.Y * SinT + Rel.Z * CosT;
    const float Ring = std::sqrt(Rel.X * Rel.X + LY * LY) - MajorRadius;
    return std::sqrt(Ring * Ring + LZ * LZ) - MinorRadius;
}

[[nodiscard]] bool IntersectRaySphereDistance(
    Mathematics::Axis3D Origin,
    Mathematics::Axis3D UnitDir,
    Mathematics::Axis3D SphereCenter,
    float               SphereRadius,
    float&              OutHitT) noexcept
{
    const Mathematics::Axis3D OC = Origin - SphereCenter;
    const float B    = Mathematics::EvaluateDot(OC, UnitDir);
    const float C    = Mathematics::EvaluateDot(OC, OC) - SphereRadius * SphereRadius;
    const float Disc = B * B - C;
    if (Disc < 0.0f)
    {
        return false;
    }
    const float Root = std::sqrt(Disc);
    const float T0   = -B - Root;
    const float T1   = -B + Root;
    if (T0 > 1.0e-3f)
    {
        OutHitT = T0;
        return true;
    }
    if (T1 > 1.0e-3f)
    {
        OutHitT = T1;
        return true;
    }
    return false;
}

// Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF reflection direction sampler in tangent space
[[nodiscard]] Mathematics::Axis3D SampleSphericalCapGgxReflectionLocal(
    Mathematics::Axis3D ViewLocal,
    float               Alpha,
    Mathematics::Axis2D U) noexcept
{
    const Mathematics::Axis3D Stretched = Mathematics::NormalizeAxis(
        { Alpha * ViewLocal.X, Alpha * ViewLocal.Y, std::max(1.0e-5f, ViewLocal.Z) },
        { 0.0f, 0.0f, 1.0f });
    const float Phi      = Constants::TauFloat * U.X;
    const float CapZ     = (1.0f - U.Y) * (1.0f + Stretched.Z) - Stretched.Z;
    const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CapZ * CapZ));
    const Mathematics::Axis3D HalfStretched {
        SinTheta * std::cos(Phi) + Stretched.X,
        SinTheta * std::sin(Phi) + Stretched.Y,
        std::max(0.0f, CapZ + Stretched.Z),
    };
    const Mathematics::Axis3D HalfLocal = Mathematics::NormalizeAxis(
        { Alpha * HalfStretched.X, Alpha * HalfStretched.Y, std::max(1.0e-6f, HalfStretched.Z) },
        { 0.0f, 0.0f, 1.0f });
    const float TwoVoH = 2.0f * Mathematics::EvaluateDot(ViewLocal, HalfLocal);
    return Mathematics::NormalizeAxis(
        {
            TwoVoH * HalfLocal.X - ViewLocal.X,
            TwoVoH * HalfLocal.Y - ViewLocal.Y,
            TwoVoH * HalfLocal.Z - ViewLocal.Z,
        },
        { 0.0f, 0.0f, 1.0f });
}

[[nodiscard]] Mathematics::Axis3D SampleConcentricCosineLocal(Mathematics::Axis2D U) noexcept
{
    const float OX = 2.0f * U.X - 1.0f;
    const float OY = 2.0f * U.Y - 1.0f;
    if (OX == 0.0f && OY == 0.0f)
    {
        return { 0.0f, 0.0f, 1.0f };
    }
    float R     = 0.0f;
    float Theta = 0.0f;
    if (std::fabs(OX) > std::fabs(OY))
    {
        R     = OX;
        Theta = (Constants::PiFloat * 0.25f) * (OY / OX);
    }
    else
    {
        R     = OY;
        Theta = (Constants::PiFloat * 0.5f) - (Constants::PiFloat * 0.25f) * (OX / OY);
    }
    const float DX = R * std::cos(Theta);
    const float DY = R * std::sin(Theta);
    return { DX, DY, std::sqrt(std::max(0.0f, 1.0f - DX * DX - DY * DY)) };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateOpenPbrBsdf(
    const SdfSurfaceMaterialSpec& Mat,
    Mathematics::Axis3D           Normal,
    Mathematics::Axis3D           ViewDir,
    Mathematics::Axis3D           LightDir,
    float&                        OutSpecPdf,
    float&                        OutDiffPdf) noexcept
{
    OutSpecPdf = 0.0f;
    OutDiffPdf = 0.0f;
    const float NdotL = Mathematics::EvaluateDot(Normal, LightDir);
    const float NdotV = Mathematics::EvaluateDot(Normal, ViewDir);
    if (NdotL <= 1.0e-5f || NdotV <= 1.0e-5f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D H = Mathematics::NormalizeAxis(ViewDir + LightDir, Normal);
    const float NdotH = std::max(1.0e-5f, Mathematics::EvaluateDot(Normal, H));
    const float VdotH = std::max(1.0e-5f, Mathematics::EvaluateDot(ViewDir, H));
    const float Alpha = std::clamp(Mat.Roughness * Mat.Roughness, 0.004f, 1.0f);
    const float A2    = Alpha * Alpha;
    const float Denom = NdotH * NdotH * (A2 - 1.0f) + 1.0f;
    const float NdfD  = A2 / std::max(1.0e-7f, Constants::PiFloat * Denom * Denom);

    const float Tan2V = (1.0f - NdotV * NdotV) / std::max(1.0e-6f, NdotV * NdotV);
    const float Tan2L = (1.0f - NdotL * NdotL) / std::max(1.0e-6f, NdotL * NdotL);
    const float LamV  = 0.5f * (std::sqrt(1.0f + A2 * Tan2V) - 1.0f);
    const float LamL  = 0.5f * (std::sqrt(1.0f + A2 * Tan2L) - 1.0f);
    const float G2    = 1.0f / (1.0f + LamV + LamL);
    const float G1V   = 1.0f / (1.0f + LamV);

    const float Schlick = std::pow(std::clamp(1.0f - VdotH, 0.0f, 1.0f), 5.0f);
    const Mathematics::LinearRGB F0 {
        0.04f * (1.0f - Mat.Metalness) + Mat.BaseColor.R * Mat.Metalness,
        0.04f * (1.0f - Mat.Metalness) + Mat.BaseColor.G * Mat.Metalness,
        0.04f * (1.0f - Mat.Metalness) + Mat.BaseColor.B * Mat.Metalness,
    };
    const Mathematics::LinearRGB Fresnel {
        F0.R + (1.0f - F0.R) * Schlick,
        F0.G + (1.0f - F0.G) * Schlick,
        F0.B + (1.0f - F0.B) * Schlick,
    };

    const float SpecScale = (NdfD * G2) / std::max(1.0e-5f, 4.0f * NdotV * NdotL);
    const float DiffK     = (1.0f - Mat.Metalness) * (1.0f - 0.04f) / Constants::PiFloat;

    OutSpecPdf = (NdfD * G1V) / std::max(1.0e-5f, 4.0f * NdotV);
    OutDiffPdf = NdotL / Constants::PiFloat;

    return {
        Mat.BaseColor.R * DiffK + Fresnel.R * SpecScale,
        Mat.BaseColor.G * DiffK + Fresnel.G * SpecScale,
        Mat.BaseColor.B * DiffK + Fresnel.B * SpecScale,
    };
}

// Analytic unoccluded irradiance + Karis representative-point specular from a spherical luminaire
[[nodiscard]] Mathematics::LinearRGB EvaluateAnalyticSphereDirectUnoccluded(
    const SdfSurfaceMaterialSpec&  Mat,
    Mathematics::Axis3D            Pos,
    Mathematics::Axis3D            Normal,
    Mathematics::Axis3D            ViewDir,
    const EmissiveSphereLuminaire& Sphere) noexcept
{
    const Mathematics::Axis3D ToCenter = Sphere.Center - Pos;
    const float DistSq = Mathematics::EvaluateLengthSquared(ToCenter);
    const float Dist   = std::sqrt(std::max(1.0e-6f, DistSq));
    const Mathematics::Axis3D L = ToCenter * (1.0f / Dist);

    const float SinMax   = std::clamp(Sphere.Radius / std::max(Sphere.Radius + 1.0e-3f, Dist), 1.0e-4f, 0.9995f);
    const float SinMaxSq = SinMax * SinMax;
    const float CosN     = Mathematics::EvaluateDot(Normal, L);

    // Horizon-clipped spherical-cap diffuse configuration factor (Snyder 1996 smooth horizon wrap)
    float FormFactor = 0.0f;
    if (CosN > SinMax)
    {
        FormFactor = SinMaxSq * CosN;
    }
    else if (CosN > -SinMax)
    {
        const float T = (CosN + SinMax) / (2.0f * SinMax);
        FormFactor = SinMaxSq * SinMax * T * T * (2.0f - T);
    }
    if (FormFactor <= 1.0e-6f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Representative point on sphere closest to reflection ray for GGX highlight
    const float RdotN = Mathematics::EvaluateDot(ViewDir, Normal);
    const Mathematics::Axis3D ReflDir = Mathematics::NormalizeAxis(Normal * (2.0f * RdotN) - ViewDir, Normal);
    const Mathematics::Axis3D CenterToRay = ReflDir * Mathematics::EvaluateDot(ToCenter, ReflDir) - ToCenter;
    const float CtrRayLen = Mathematics::EvaluateLength(CenterToRay);
    const Mathematics::Axis3D ClosestVec = ToCenter + CenterToRay * std::min(1.0f, Sphere.Radius / std::max(1.0e-5f, CtrRayLen));
    const Mathematics::Axis3D SpecL = Mathematics::NormalizeAxis(ClosestVec, L);

    float DummyS = 0.0f, DummyD = 0.0f;
    const Mathematics::LinearRGB BsdfSpec = EvaluateOpenPbrBsdf(Mat, Normal, ViewDir, SpecL, DummyS, DummyD);
    const float Alpha      = std::clamp(Mat.Roughness * Mat.Roughness, 0.02f, 1.0f);
    const float AlphaPrime = std::clamp(Alpha + Sphere.Radius / (2.0f * Dist), Alpha, 1.0f);
    const float SpecNorm   = (Alpha * Alpha) / (AlphaPrime * AlphaPrime);

    const float DiffScale = (1.0f - Mat.Metalness) * (1.0f - 0.04f);
    const float SpecNdotL = std::max(0.0f, Mathematics::EvaluateDot(Normal, SpecL));
    const float Omega     = Constants::TauFloat * (1.0f - std::sqrt(std::max(0.0f, 1.0f - SinMaxSq)));
    const Mathematics::LinearRGB PureSpec {
        std::max(0.0f, BsdfSpec.R - Mat.BaseColor.R * DiffScale / Constants::PiFloat) * SpecNdotL * Omega * SpecNorm,
        std::max(0.0f, BsdfSpec.G - Mat.BaseColor.G * DiffScale / Constants::PiFloat) * SpecNdotL * Omega * SpecNorm,
        std::max(0.0f, BsdfSpec.B - Mat.BaseColor.B * DiffScale / Constants::PiFloat) * SpecNdotL * Omega * SpecNorm,
    };

    return {
        Sphere.Radiance.R * (Mat.BaseColor.R * DiffScale * FormFactor + PureSpec.R),
        Sphere.Radiance.G * (Mat.BaseColor.G * DiffScale * FormFactor + PureSpec.G),
        Sphere.Radiance.B * (Mat.BaseColor.B * DiffScale * FormFactor + PureSpec.B),
    };
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmicCurve(Mathematics::LinearRGB Hdr, float Exposure) noexcept
{
    const auto Curve = [Exposure](float C) noexcept -> float
    {
        const float X = std::max(0.0f, C * Exposure);
        return std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

[[nodiscard]] uint8_t EncodeSrgbByte(float LinearC) noexcept
{
    const float C = std::clamp(LinearC, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(S * 255.0f), 0.0f, 255.0f));
}

// 16 world-locked Fibonacci directions on the unit sphere (zero tangent-basis rotation discontinuity on curved SDFs)
constexpr std::array<Mathematics::Axis3D, 16> FibonacciUnitCones {{
    {  0.000000f,  0.000000f,  0.968750f },
    { -0.355181f,  0.324964f,  0.876438f },
    {  0.072949f, -0.663417f,  0.744675f },
    {  0.546948f,  0.594726f,  0.589275f },
    { -0.892399f, -0.114912f,  0.436390f },
    {  0.697458f, -0.642322f,  0.317992f },
    { -0.105917f,  0.978177f,  0.178415f },
    { -0.630510f, -0.774011f,  0.056895f },
    {  0.971825f,  0.223346f, -0.076190f },
    { -0.785989f,  0.593691f, -0.172502f },
    {  0.163627f, -0.942641f, -0.290953f },
    {  0.546722f,  0.728969f, -0.411942f },
    { -0.832541f, -0.194251f, -0.518789f },
    {  0.562411f, -0.491089f, -0.665226f },
    { -0.044225f,  0.627963f, -0.776985f },
    { -0.289784f, -0.295140f, -0.910449f },
}};

[[nodiscard]] Mathematics::LinearRGB SampleRadiosityGridTrilinear(
    const std::vector<Mathematics::LinearRGB>& Grid,
    uint32_t                                   GX,
    uint32_t                                   GY,
    uint32_t                                   GZ,
    Mathematics::Axis3D                        BoundsMin,
    Mathematics::Axis3D                        BoundsMax,
    Mathematics::Axis3D                        WorldPos) noexcept
{
    if (Grid.empty() || GX < 2u || GY < 2u || GZ < 2u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const Mathematics::Axis3D Span = BoundsMax - BoundsMin;
    const float FX = std::clamp(((WorldPos.X - BoundsMin.X) / std::max(1.0e-5f, Span.X)) * static_cast<float>(GX - 1u), 0.0f, static_cast<float>(GX - 1u));
    const float FY = std::clamp(((WorldPos.Y - BoundsMin.Y) / std::max(1.0e-5f, Span.Y)) * static_cast<float>(GY - 1u), 0.0f, static_cast<float>(GY - 1u));
    const float FZ = std::clamp(((WorldPos.Z - BoundsMin.Z) / std::max(1.0e-5f, Span.Z)) * static_cast<float>(GZ - 1u), 0.0f, static_cast<float>(GZ - 1u));

    const uint32_t IX0 = std::min(GX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(GY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(GZ - 2u, static_cast<uint32_t>(FZ));
    const float    TX  = FX - static_cast<float>(IX0);
    const float    TY  = FY - static_cast<float>(IY0);
    const float    TZ  = FZ - static_cast<float>(IZ0);

    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> Mathematics::LinearRGB
    {
        return Grid[(static_cast<std::size_t>(Z) * GY + Y) * GX + X];
    };

    const auto C00 = At(IX0, IY0, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0) * TX;
    const auto C10 = At(IX0, IY0 + 1u, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0) * TX;
    const auto C01 = At(IX0, IY0, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0 + 1u) * TX;
    const auto C11 = At(IX0, IY0 + 1u, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0 + 1u) * TX;
    return (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODE LABELS
//------------------------------------------------------------------------------------------------------------------------

const char* QuerySdfShadowPenumbraModeLabel(SdfShadowPenumbraMode Mode) noexcept
{
    switch (Mode)
    {
        case SdfShadowPenumbraMode::PointSampleSdf:             return "0. Point-Sample SDF (No Penumbra)";
        case SdfShadowPenumbraMode::AnalyticSdfConePenumbra:    return "1. Analytic SDF Cone Penumbra";
        case SdfShadowPenumbraMode::VoxelSdfClipmapPenumbra:    return "2. 3D Voxel SDF Clipmap Penumbra";
        case SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi: return "3. 3D Voxel SDF Penumbra + Cone GI";
        case SdfShadowPenumbraMode::HybridSdfSobolSphericalCap: return "4. Hybrid SDF + Sobol Spherical Cap";
        case SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp:
        default:                                                return "5. SDF Monte Carlo PT Ref (96 SPP)";
    }
}

const char* QuerySdfShadowPenumbraShortLabel(SdfShadowPenumbraMode Mode) noexcept
{
    switch (Mode)
    {
        case SdfShadowPenumbraMode::PointSampleSdf:             return "0. Point SDF";
        case SdfShadowPenumbraMode::AnalyticSdfConePenumbra:    return "1. Analytic SDF Cone";
        case SdfShadowPenumbraMode::VoxelSdfClipmapPenumbra:    return "2. Voxel SDF Clipmap";
        case SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi: return "3. Voxel SDF + Cone GI";
        case SdfShadowPenumbraMode::HybridSdfSobolSphericalCap: return "4. Hybrid Sobol Cap";
        case SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp:
        default:                                                return "5. SDF MC Ref (96spp)";
    }
}

Mathematics::Axis2D EvaluateOwenScrambledSobol02(
    uint32_t SampleIndex,
    uint32_t PixelKey,
    uint32_t DimensionPair) noexcept
{
    const uint32_t PairKey       = AvalancheKey32(DimensionPair * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    const uint32_t ShuffledIndex = NestedDigitPermute(SampleIndex, PairKey);
    const uint32_t FirstDigits   = NestedDigitPermute(ReverseBits32(ShuffledIndex), AvalancheKey32(PairKey ^ 0xA511E9B3u));
    const uint32_t SecondDigits  = NestedDigitPermute(SobolSecondDimensionBits(ShuffledIndex), AvalancheKey32(PairKey ^ 0x63D83595u));
    constexpr float Inv24 = 1.0f / 16777216.0f;
    return {
        static_cast<float>(FirstDigits >> 8u) * Inv24,
        static_cast<float>(SecondDigits >> 8u) * Inv24,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SDF SHOWCASE SCENE
//------------------------------------------------------------------------------------------------------------------------

SdfSurfaceMaterialSpec SdfShowcaseScene::SampleMaterialAt(uint32_t Slot, Mathematics::Axis3D WorldPos) const noexcept
{
    if (Materials.empty())
    {
        return {};
    }
    SdfSurfaceMaterialSpec M = Materials[std::min<std::size_t>(Slot, Materials.size() - 1u)];
    if (M.CheckerFloor)
    {
        const float Scale = 1.0f / std::max(0.05f, M.CheckerScaleM);
        const int   CX    = static_cast<int>(std::floor(WorldPos.X * Scale));
        const int   CY    = static_cast<int>(std::floor(WorldPos.Y * Scale));
        const bool  Even  = ((CX + CY) & 1) == 0;
        M.BaseColor = Even ? Mathematics::LinearRGB { 0.76f, 0.73f, 0.68f }
                           : Mathematics::LinearRGB { 0.42f, 0.42f, 0.43f };
        M.Roughness = Even ? 0.50f : 0.62f;
    }
    return M;
}

float SdfShowcaseScene::EvaluateOccluderSignedDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    float BestD = 20.0f;
    for (const auto& B : Boxes)
    {
        if (!B.CastsShadow)
        {
            continue;
        }
        BestD = std::min(BestD, EvaluateSignedBox(WorldPos, B.Center, B.HalfExtent));
    }
    for (const auto& S : Spheres)
    {
        if (!S.CastsShadow)
        {
            continue;
        }
        BestD = std::min(BestD, Mathematics::EvaluateLength(WorldPos - S.Center) - S.Radius);
    }
    for (const auto& C : Cylinders)
    {
        if (!C.CastsShadow)
        {
            continue;
        }
        BestD = std::min(BestD, EvaluateSignedCappedCylinderZ(WorldPos, C.BaseCenter, C.HeightZ, C.Radius));
    }
    for (const auto& T : Tori)
    {
        if (!T.CastsShadow)
        {
            continue;
        }
        BestD = std::min(BestD, EvaluateSignedTorus(WorldPos, T.Center, T.MajorRadius, T.MinorRadius, T.TiltRadiansX));
    }
    if (CharacterRig != nullptr && !CharacterRig->LimbCapsules.empty())
    {
        BestD = std::min(BestD, CharacterRig->EvaluateMinimumSignedDistance(WorldPos));
    }
    if (VehiclePlacement.ActiveToggle && VehiclePlacement.VolumeAsset != nullptr)
    {
        const float CarD = DistanceFieldSpace::LocalSignedDistanceVolume::SampleSmoothWorldDistance(VehiclePlacement, WorldPos) - 0.045f;
        BestD = std::min(BestD, CarD);
    }
    return BestD;
}

float SdfShowcaseScene::EvaluateFullSceneSignedDistance(Mathematics::Axis3D WorldPos, uint32_t& OutMaterialSlot) const noexcept
{
    float BestD = 20.0f;
    OutMaterialSlot = 0u;

    for (const auto& B : Boxes)
    {
        const float D = EvaluateSignedBox(WorldPos, B.Center, B.HalfExtent);
        if (D < BestD)
        {
            BestD           = D;
            OutMaterialSlot = B.MaterialSlot;
        }
    }
    for (const auto& S : Spheres)
    {
        const float D = Mathematics::EvaluateLength(WorldPos - S.Center) - S.Radius;
        if (D < BestD)
        {
            BestD           = D;
            OutMaterialSlot = S.MaterialSlot;
        }
    }
    for (const auto& C : Cylinders)
    {
        const float D = EvaluateSignedCappedCylinderZ(WorldPos, C.BaseCenter, C.HeightZ, C.Radius);
        if (D < BestD)
        {
            BestD           = D;
            OutMaterialSlot = C.MaterialSlot;
        }
    }
    for (const auto& T : Tori)
    {
        const float D = EvaluateSignedTorus(WorldPos, T.Center, T.MajorRadius, T.MinorRadius, T.TiltRadiansX);
        if (D < BestD)
        {
            BestD           = D;
            OutMaterialSlot = T.MaterialSlot;
        }
    }
    if (EmissiveActive)
    {
        const float ED = Mathematics::EvaluateLength(WorldPos - EmissiveSphere.Center) - EmissiveSphere.Radius;
        if (ED < BestD)
        {
            BestD           = ED;
            OutMaterialSlot = EmissiveSphere.MaterialSlot;
        }
    }
    return BestD;
}

Mathematics::Axis3D SdfShowcaseScene::EvaluateFullSceneGradientNormal(Mathematics::Axis3D WorldPos) const noexcept
{
    constexpr float Eps = 1.5e-3f;
    uint32_t Dummy = 0u;
    const float DX = EvaluateFullSceneSignedDistance({ WorldPos.X + Eps, WorldPos.Y, WorldPos.Z }, Dummy) -
                     EvaluateFullSceneSignedDistance({ WorldPos.X - Eps, WorldPos.Y, WorldPos.Z }, Dummy);
    const float DY = EvaluateFullSceneSignedDistance({ WorldPos.X, WorldPos.Y + Eps, WorldPos.Z }, Dummy) -
                     EvaluateFullSceneSignedDistance({ WorldPos.X, WorldPos.Y - Eps, WorldPos.Z }, Dummy);
    const float DZ = EvaluateFullSceneSignedDistance({ WorldPos.X, WorldPos.Y, WorldPos.Z + Eps }, Dummy) -
                     EvaluateFullSceneSignedDistance({ WorldPos.X, WorldPos.Y, WorldPos.Z - Eps }, Dummy);
    return Mathematics::NormalizeAxis({ DX, DY, DZ }, { 0.0f, 0.0f, 1.0f });
}

void BuildCanonicalProof11EmissiveSphereScene(SdfShowcaseScene& OutScene)
{
    OutScene = {};
    OutScene.Materials.resize(9u);

    // 0: Checkerboard floor
    OutScene.Materials[0].CheckerFloor  = true;
    OutScene.Materials[0].CheckerScaleM = 0.56f;
    OutScene.Materials[0].Roughness     = 0.54f;

    // 1: Warm plaster back wall
    OutScene.Materials[1].BaseColor = { 0.74f, 0.70f, 0.64f };
    OutScene.Materials[1].Roughness = 0.78f;

    // 2: Terracotta crimson left wall
    OutScene.Materials[2].BaseColor = { 0.76f, 0.18f, 0.09f };
    OutScene.Materials[2].Roughness = 0.78f;

    // 3: Forest emerald right wall
    OutScene.Materials[3].BaseColor = { 0.12f, 0.46f, 0.22f };
    OutScene.Materials[3].Roughness = 0.78f;

    // 4: Neutral studio ceiling
    OutScene.Materials[4].BaseColor = { 0.42f, 0.42f, 0.42f };
    OutScene.Materials[4].Roughness = 0.85f;

    // 5: Stone column + central pedestal
    OutScene.Materials[5].BaseColor = { 0.74f, 0.71f, 0.66f };
    OutScene.Materials[5].Roughness = 0.54f;

    // 6: Brushed warm bronze torus
    OutScene.Materials[6].BaseColor = { 0.68f, 0.46f, 0.22f };
    OutScene.Materials[6].Roughness = 0.28f;
    OutScene.Materials[6].Metalness = 0.35f;

    // 7: Incandescent emissive sphere luminaire
    OutScene.Materials[7].BaseColor       = { 1.0f, 0.96f, 0.88f };
    OutScene.Materials[7].Emission        = { 22.0f, 17.4f, 10.8f };
    OutScene.Materials[7].EmissiveSurface = true;

    // 8: Smooth ceramic-grey sphere
    OutScene.Materials[8].BaseColor = { 0.68f, 0.68f, 0.70f };
    OutScene.Materials[8].Roughness = 0.46f;

    // Cornell Gallery Slabs (CastsShadow = false for direct shadow march; FullDistanceGrid & Cone GI include them)
    OutScene.Boxes.push_back({ {  0.00f,  0.20f, -0.10f }, { 2.80f, 2.65f, 0.10f }, 0u, false }); // Floor Z = 0
    OutScene.Boxes.push_back({ {  0.00f,  2.72f,  1.60f }, { 2.80f, 0.10f, 1.70f }, 1u, false }); // Back wall Y = 2.62
    OutScene.Boxes.push_back({ { -2.74f,  0.20f,  1.60f }, { 0.10f, 2.65f, 1.70f }, 2u, false }); // Crimson left wall X = -2.64
    OutScene.Boxes.push_back({ {  2.74f,  0.20f,  1.60f }, { 0.10f, 2.65f, 1.70f }, 3u, false }); // Green right wall X = +2.64
    OutScene.Boxes.push_back({ {  0.00f,  0.20f,  3.26f }, { 2.80f, 2.65f, 0.10f }, 4u, false }); // Ceiling Z = 3.16

    // Shadow-casting interior SDF objects matching PROOF 11 (download.png) composition & diagonal floor/wall shadow sweep
    OutScene.Cylinders.push_back({ { -0.36f,  0.58f, 0.0f }, 1.72f, 0.145f, 5u, true });          // Left-center column
    OutScene.Boxes.push_back({     {  0.00f, -0.10f, 0.34f }, { 0.19f, 0.19f, 0.34f }, 5u, true }); // Center pedestal
    OutScene.Tori.push_back({      {  0.00f, -0.10f, 0.84f }, 0.28f, 0.100f, 0.0f, 6u, true });     // Bronze torus
    OutScene.Spheres.push_back({   {  0.62f,  0.38f, 0.52f }, 0.52f, 8u, true });                   // Right grey sphere

    // Sole Light Source: Emissive Sphere ONLY (NO sun, NO area quad, NO spot, NO IES)
    OutScene.EmissiveActive              = true;
    OutScene.EmissiveSphere.Center       = { -1.28f, -0.22f, 1.06f };
    OutScene.EmissiveSphere.Radius       = 0.38f;
    OutScene.EmissiveSphere.Radiance     = { 22.0f, 17.4f, 10.8f };
    OutScene.EmissiveSphere.MaterialSlot = 7u;
}

//------------------------------------------------------------------------------------------------------------------------
//                           2-CASCADE 3D VOXEL SDF CLIPMAP + MULTI-BOUNCE RADIOSITY PYRAMID
//------------------------------------------------------------------------------------------------------------------------

std::size_t VoxelSdfClipmapCascade::QueryAllocatedBytes() const noexcept
{
    return DistanceGrid.size() * sizeof(float) +
           FullDistanceGrid.size() * sizeof(float) +
           RadiosityGrid.size() * sizeof(Mathematics::LinearRGB) +
           Mip1Radiosity.size() * sizeof(Mathematics::LinearRGB) +
           Mip2Radiosity.size() * sizeof(Mathematics::LinearRGB) +
           SurfaceMaskGrid.size() * sizeof(uint8_t);
}

bool VoxelSdfClipmapCascade::ContainsInsideMargin(Mathematics::Axis3D WorldPos, float MarginFraction) const noexcept
{
    if (DimX < 2u || DimY < 2u || DimZ < 2u)
    {
        return false;
    }
    const Mathematics::Axis3D Span = BoundsMax - BoundsMin;
    const float UX = (WorldPos.X - BoundsMin.X) / std::max(1.0e-5f, Span.X);
    const float UY = (WorldPos.Y - BoundsMin.Y) / std::max(1.0e-5f, Span.Y);
    const float UZ = (WorldPos.Z - BoundsMin.Z) / std::max(1.0e-5f, Span.Z);
    const float Hi = 1.0f - MarginFraction;
    return UX > MarginFraction && UX < Hi &&
           UY > MarginFraction && UY < Hi &&
           UZ > MarginFraction && UZ < Hi;
}

float VoxelSdfClipmapCascade::SampleDistanceHermite(Mathematics::Axis3D WorldPos) const noexcept
{
    if (DistanceGrid.empty() || DimX < 2u || DimY < 2u || DimZ < 2u)
    {
        return 20.0f;
    }

    const float OutsideX = std::max(0.0f, std::max(BoundsMin.X - WorldPos.X, WorldPos.X - BoundsMax.X));
    const float OutsideY = std::max(0.0f, std::max(BoundsMin.Y - WorldPos.Y, WorldPos.Y - BoundsMax.Y));
    const float OutsideZ = std::max(0.0f, std::max(BoundsMin.Z - WorldPos.Z, WorldPos.Z - BoundsMax.Z));
    const float BoxDist  = std::sqrt(OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);

    const float FX = std::clamp((WorldPos.X - BoundsMin.X) / VoxelSpacing.X, 0.0f, static_cast<float>(DimX - 1u));
    const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) / VoxelSpacing.Y, 0.0f, static_cast<float>(DimY - 1u));
    const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) / VoxelSpacing.Z, 0.0f, static_cast<float>(DimZ - 1u));

    const uint32_t IX0 = std::min(DimX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(DimY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(DimZ - 2u, static_cast<uint32_t>(FZ));
    const float    LX  = FX - static_cast<float>(IX0);
    const float    LY  = FY - static_cast<float>(IY0);
    const float    LZ  = FZ - static_cast<float>(IZ0);

    const float TX = LX * LX * (3.0f - 2.0f * LX);
    const float TY = LY * LY * (3.0f - 2.0f * LY);
    const float TZ = LZ * LZ * (3.0f - 2.0f * LZ);

    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
    {
        return DistanceGrid[(static_cast<std::size_t>(Z) * DimY + Y) * DimX + X];
    };

    const float C00 = At(IX0, IY0, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0) * TX;
    const float C10 = At(IX0, IY0 + 1u, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0) * TX;
    const float C01 = At(IX0, IY0, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0 + 1u) * TX;
    const float C11 = At(IX0, IY0 + 1u, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0 + 1u) * TX;
    const float Interpolated = (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
    return Interpolated + BoxDist;
}

float VoxelSdfClipmapCascade::SampleFullDistanceTrilinear(Mathematics::Axis3D WorldPos) const noexcept
{
    if (FullDistanceGrid.empty() || DimX < 2u || DimY < 2u || DimZ < 2u)
    {
        return 20.0f;
    }

    const float OutsideX = std::max(0.0f, std::max(BoundsMin.X - WorldPos.X, WorldPos.X - BoundsMax.X));
    const float OutsideY = std::max(0.0f, std::max(BoundsMin.Y - WorldPos.Y, WorldPos.Y - BoundsMax.Y));
    const float OutsideZ = std::max(0.0f, std::max(BoundsMin.Z - WorldPos.Z, WorldPos.Z - BoundsMax.Z));
    const float BoxDist  = std::sqrt(OutsideX * OutsideX + OutsideY * OutsideY + OutsideZ * OutsideZ);

    const float FX = std::clamp((WorldPos.X - BoundsMin.X) / VoxelSpacing.X, 0.0f, static_cast<float>(DimX - 1u));
    const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) / VoxelSpacing.Y, 0.0f, static_cast<float>(DimY - 1u));
    const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) / VoxelSpacing.Z, 0.0f, static_cast<float>(DimZ - 1u));

    const uint32_t IX0 = std::min(DimX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(DimY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(DimZ - 2u, static_cast<uint32_t>(FZ));
    const float    TX  = FX - static_cast<float>(IX0);
    const float    TY  = FY - static_cast<float>(IY0);
    const float    TZ  = FZ - static_cast<float>(IZ0);

    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) noexcept -> float
    {
        return FullDistanceGrid[(static_cast<std::size_t>(Z) * DimY + Y) * DimX + X];
    };

    const float C00 = At(IX0, IY0, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0) * TX;
    const float C10 = At(IX0, IY0 + 1u, IZ0) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0) * TX;
    const float C01 = At(IX0, IY0, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0, IZ0 + 1u) * TX;
    const float C11 = At(IX0, IY0 + 1u, IZ0 + 1u) * (1.0f - TX) + At(IX0 + 1u, IY0 + 1u, IZ0 + 1u) * TX;
    return (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ + BoxDist;
}

Mathematics::LinearRGB VoxelSdfClipmapCascade::SampleRadiosityMipmapped(
    Mathematics::Axis3D WorldPos,
    float               ConeRadiusMeters) const noexcept
{
    if (RadiosityGrid.empty() || DimX < 2u || DimY < 2u || DimZ < 2u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const auto Rad0 = SampleRadiosityGridTrilinear(RadiosityGrid, DimX, DimY, DimZ, BoundsMin, BoundsMax, WorldPos);
    const float Mip0End = MaxVoxelSpacingM * 1.15f;
    if (ConeRadiusMeters <= Mip0End || Mip1Radiosity.empty())
    {
        return Rad0;
    }

    const auto Rad1 = SampleRadiosityGridTrilinear(Mip1Radiosity, Mip1DimX, Mip1DimY, Mip1DimZ, BoundsMin, BoundsMax, WorldPos);
    const float Mip1End = MaxVoxelSpacingM * 2.40f;
    if (ConeRadiusMeters <= Mip1End || Mip2Radiosity.empty())
    {
        const float T01 = std::clamp((ConeRadiusMeters - Mip0End) / std::max(0.02f, Mip1End - Mip0End), 0.0f, 1.0f);
        return Rad0 * (1.0f - T01) + Rad1 * T01;
    }

    const auto Rad2 = SampleRadiosityGridTrilinear(Mip2Radiosity, Mip2DimX, Mip2DimY, Mip2DimZ, BoundsMin, BoundsMax, WorldPos);
    const float T12 = std::clamp((ConeRadiusMeters - Mip1End) / std::max(0.04f, MaxVoxelSpacingM * 2.60f), 0.0f, 1.0f);
    return Rad1 * (1.0f - T12) + Rad2 * T12;
}

void VoxelSdfClipmapCascade::ExtrapolateRadiosityPyramid()
{
    if (RadiosityGrid.empty() || SurfaceMaskGrid.size() != RadiosityGrid.size() || DimX < 3u || DimY < 3u || DimZ < 3u)
    {
        return;
    }

    // 1. Constant-along-normal PDE extension (3 relaxation sweeps) into adjacent empty air cells:
    //    eliminates trilinear zero-border darkening and staircase blocks when cones intersect oblique SDF surfaces!
    std::vector<uint8_t> ActiveMask = SurfaceMaskGrid;
    for (uint32_t Sweep = 0u; Sweep < 3u; ++Sweep)
    {
        std::vector<Mathematics::LinearRGB> NextRad  = RadiosityGrid;
        std::vector<uint8_t>                NextMask = ActiveMask;
        for (uint32_t IZ = 0u; IZ < DimZ; ++IZ)
        {
            for (uint32_t IY = 0u; IY < DimY; ++IY)
            {
                for (uint32_t IX = 0u; IX < DimX; ++IX)
                {
                    const std::size_t Idx = (static_cast<std::size_t>(IZ) * DimY + IY) * DimX + IX;
                    if (ActiveMask[Idx] != 0u)
                    {
                        continue;
                    }
                    Mathematics::LinearRGB Sum { 0.0f, 0.0f, 0.0f };
                    float WeightSum = 0.0f;
                    for (int KZ = -1; KZ <= 1; ++KZ)
                    {
                        const int SZ = static_cast<int>(IZ) + KZ;
                        if (SZ < 0 || SZ >= static_cast<int>(DimZ)) continue;
                        for (int KY = -1; KY <= 1; ++KY)
                        {
                            const int SY = static_cast<int>(IY) + KY;
                            if (SY < 0 || SY >= static_cast<int>(DimY)) continue;
                            for (int KX = -1; KX <= 1; ++KX)
                            {
                                const int SX = static_cast<int>(IX) + KX;
                                if (SX < 0 || SX >= static_cast<int>(DimX)) continue;
                                const std::size_t NIdx = (static_cast<std::size_t>(SZ) * DimY + static_cast<uint32_t>(SY)) * DimX + static_cast<uint32_t>(SX);
                                if (ActiveMask[NIdx] != 0u)
                                {
                                    const int Manhattan = std::abs(KX) + std::abs(KY) + std::abs(KZ);
                                    const float W = (Manhattan == 1) ? 1.0f : ((Manhattan == 2) ? 0.707f : 0.577f);
                                    Sum       = Sum + RadiosityGrid[NIdx] * W;
                                    WeightSum += W;
                                }
                            }
                        }
                    }
                    if (WeightSum > 0.0f)
                    {
                        NextRad[Idx]  = Sum * (1.0f / WeightSum);
                        NextMask[Idx] = 1u;
                    }
                }
            }
        }
        RadiosityGrid = std::move(NextRad);
        ActiveMask    = std::move(NextMask);
    }

    // 2. Gentle 3x3x3 active-only tangential smoothing so Mip0 has zero discrete voxel faceting
    {
        std::vector<Mathematics::LinearRGB> Smoothed = RadiosityGrid;
        static constexpr float K1D[3] = { 0.25f, 0.50f, 0.25f };
        for (uint32_t IZ = 0u; IZ < DimZ; ++IZ)
        {
            for (uint32_t IY = 0u; IY < DimY; ++IY)
            {
                for (uint32_t IX = 0u; IX < DimX; ++IX)
                {
                    const std::size_t DIdx = (static_cast<std::size_t>(IZ) * DimY + IY) * DimX + IX;
                    if (ActiveMask[DIdx] == 0u) continue;
                    Mathematics::LinearRGB Sum { 0.0f, 0.0f, 0.0f };
                    float WSum = 0.0f;
                    for (int KZ = -1; KZ <= 1; ++KZ)
                    {
                        const uint32_t SZ = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IZ) + KZ, 0, static_cast<int>(DimZ) - 1));
                        for (int KY = -1; KY <= 1; ++KY)
                        {
                            const uint32_t SY = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IY) + KY, 0, static_cast<int>(DimY) - 1));
                            for (int KX = -1; KX <= 1; ++KX)
                            {
                                const uint32_t SX = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IX) + KX, 0, static_cast<int>(DimX) - 1));
                                const std::size_t SIdx = (static_cast<std::size_t>(SZ) * DimY + SY) * DimX + SX;
                                if (ActiveMask[SIdx] == 0u) continue;
                                const float W = K1D[KX + 1] * K1D[KY + 1] * K1D[KZ + 1];
                                Sum  = Sum + RadiosityGrid[SIdx] * W;
                                WSum += W;
                            }
                        }
                    }
                    if (WSum > 0.0f)
                    {
                        Smoothed[DIdx] = Sum * (1.0f / WSum);
                    }
                }
            }
        }
        RadiosityGrid = std::move(Smoothed);
    }

    // 3. Build 1/2-res Mip1 & 1/4-res Mip2 radiosity pyramids over active extended cells
    const auto DownsampleMip = [](
        const std::vector<Mathematics::LinearRGB>& SrcRad,
        const std::vector<uint8_t>&                SrcMask,
        uint32_t SX, uint32_t SY, uint32_t SZ,
        std::vector<Mathematics::LinearRGB>&       DstRad,
        std::vector<uint8_t>&                      DstMask,
        uint32_t& DX, uint32_t& DY, uint32_t& DZ)
    {
        DX = std::max(2u, (SX + 1u) / 2u);
        DY = std::max(2u, (SY + 1u) / 2u);
        DZ = std::max(2u, (SZ + 1u) / 2u);
        DstRad.assign(static_cast<std::size_t>(DX) * DY * DZ, { 0.0f, 0.0f, 0.0f });
        DstMask.assign(static_cast<std::size_t>(DX) * DY * DZ, 0u);
        for (uint32_t Z = 0u; Z < DZ; ++Z)
        {
            for (uint32_t Y = 0u; Y < DY; ++Y)
            {
                for (uint32_t X = 0u; X < DX; ++X)
                {
                    Mathematics::LinearRGB Sum { 0.0f, 0.0f, 0.0f };
                    float Active = 0.0f;
                    for (uint32_t OZ = 0u; OZ < 2u; ++OZ)
                    {
                        const uint32_t IZ = std::min(SZ - 1u, Z * 2u + OZ);
                        for (uint32_t OY = 0u; OY < 2u; ++OY)
                        {
                            const uint32_t IY = std::min(SY - 1u, Y * 2u + OY);
                            for (uint32_t OX = 0u; OX < 2u; ++OX)
                            {
                                const uint32_t IX = std::min(SX - 1u, X * 2u + OX);
                                const std::size_t SIdx = (static_cast<std::size_t>(IZ) * SY + IY) * SX + IX;
                                if (SrcMask[SIdx] != 0u)
                                {
                                    Sum    = Sum + SrcRad[SIdx];
                                    Active += 1.0f;
                                }
                            }
                        }
                    }
                    const std::size_t DIdx = (static_cast<std::size_t>(Z) * DY + Y) * DX + X;
                    if (Active > 0.0f)
                    {
                        DstRad[DIdx]  = Sum * (1.0f / Active);
                        DstMask[DIdx] = 1u;
                    }
                }
            }
        }
    };

    std::vector<uint8_t> Mip1Mask {}, Mip2Mask {};
    DownsampleMip(RadiosityGrid, ActiveMask, DimX, DimY, DimZ, Mip1Radiosity, Mip1Mask, Mip1DimX, Mip1DimY, Mip1DimZ);
    DownsampleMip(Mip1Radiosity, Mip1Mask, Mip1DimX, Mip1DimY, Mip1DimZ, Mip2Radiosity, Mip2Mask, Mip2DimX, Mip2DimY, Mip2DimZ);
}

float DualCascadeClipmapVolume::SampleClipmapDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    if (FineCascade.DistanceGrid.empty())
    {
        return CoarseCascade.SampleDistanceHermite(WorldPos);
    }

    const Mathematics::Axis3D FineCtr  = (FineCascade.BoundsMin + FineCascade.BoundsMax) * 0.5f;
    const Mathematics::Axis3D FineHalf = (FineCascade.BoundsMax - FineCascade.BoundsMin) * 0.5f;
    const float NX   = std::fabs(WorldPos.X - FineCtr.X) / std::max(1.0e-3f, FineHalf.X);
    const float NY   = std::fabs(WorldPos.Y - FineCtr.Y) / std::max(1.0e-3f, FineHalf.Y);
    const float NZ   = std::fabs(WorldPos.Z - FineCtr.Z) / std::max(1.0e-3f, FineHalf.Z);
    const float MaxN = std::max({ NX, NY, NZ });

    if (MaxN <= 0.78f || CoarseCascade.DistanceGrid.empty())
    {
        return FineCascade.SampleDistanceHermite(WorldPos);
    }
    if (MaxN >= 0.98f)
    {
        return CoarseCascade.SampleDistanceHermite(WorldPos);
    }
    const float Blend = SmoothHermite01(0.78f, 0.98f, MaxN);
    return FineCascade.SampleDistanceHermite(WorldPos) * (1.0f - Blend) +
           CoarseCascade.SampleDistanceHermite(WorldPos) * Blend;
}

float DualCascadeClipmapVolume::SampleFullSceneDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    if (FineCascade.FullDistanceGrid.empty())
    {
        return CoarseCascade.SampleFullDistanceTrilinear(WorldPos);
    }

    const Mathematics::Axis3D FineCtr  = (FineCascade.BoundsMin + FineCascade.BoundsMax) * 0.5f;
    const Mathematics::Axis3D FineHalf = (FineCascade.BoundsMax - FineCascade.BoundsMin) * 0.5f;
    const float NX   = std::fabs(WorldPos.X - FineCtr.X) / std::max(1.0e-3f, FineHalf.X);
    const float NY   = std::fabs(WorldPos.Y - FineCtr.Y) / std::max(1.0e-3f, FineHalf.Y);
    const float NZ   = std::fabs(WorldPos.Z - FineCtr.Z) / std::max(1.0e-3f, FineHalf.Z);
    const float MaxN = std::max({ NX, NY, NZ });

    if (MaxN <= 0.76f || CoarseCascade.FullDistanceGrid.empty())
    {
        return FineCascade.SampleFullDistanceTrilinear(WorldPos);
    }
    if (MaxN >= 0.96f)
    {
        return CoarseCascade.SampleFullDistanceTrilinear(WorldPos);
    }
    const float Blend = SmoothHermite01(0.76f, 0.96f, MaxN);
    return FineCascade.SampleFullDistanceTrilinear(WorldPos) * (1.0f - Blend) +
           CoarseCascade.SampleFullDistanceTrilinear(WorldPos) * Blend;
}

Mathematics::LinearRGB DualCascadeClipmapVolume::SampleClipmapRadiosity(
    Mathematics::Axis3D WorldPos,
    float               ConeRadiusMeters) const noexcept
{
    if (FineCascade.RadiosityGrid.empty())
    {
        return CoarseCascade.SampleRadiosityMipmapped(WorldPos, ConeRadiusMeters);
    }

    const Mathematics::Axis3D FineCtr  = (FineCascade.BoundsMin + FineCascade.BoundsMax) * 0.5f;
    const Mathematics::Axis3D FineHalf = (FineCascade.BoundsMax - FineCascade.BoundsMin) * 0.5f;
    const float NX   = std::fabs(WorldPos.X - FineCtr.X) / std::max(1.0e-3f, FineHalf.X);
    const float NY   = std::fabs(WorldPos.Y - FineCtr.Y) / std::max(1.0e-3f, FineHalf.Y);
    const float NZ   = std::fabs(WorldPos.Z - FineCtr.Z) / std::max(1.0e-3f, FineHalf.Z);
    const float MaxN = std::max({ NX, NY, NZ });

    // Wide cones naturally blend toward CoarseCascade, eliminating fine-cascade boundary seams
    const float WideBias = SmoothHermite01(0.18f, 0.52f, ConeRadiusMeters);
    const float EffN     = std::max(MaxN, WideBias * 0.92f);
    if (EffN <= 0.72f || CoarseCascade.RadiosityGrid.empty())
    {
        return FineCascade.SampleRadiosityMipmapped(WorldPos, ConeRadiusMeters);
    }
    if (EffN >= 0.94f)
    {
        return CoarseCascade.SampleRadiosityMipmapped(WorldPos, ConeRadiusMeters);
    }
    const float Blend = SmoothHermite01(0.72f, 0.94f, EffN);
    return FineCascade.SampleRadiosityMipmapped(WorldPos, ConeRadiusMeters) * (1.0f - Blend) +
           CoarseCascade.SampleRadiosityMipmapped(WorldPos, ConeRadiusMeters) * Blend;
}

void DualCascadeClipmapVolume::BuildFromShowcaseScene(const SdfShowcaseScene& Scene)
{
    const auto PopulateBounce1Cascade = [&](
        VoxelSdfClipmapCascade& C,
        uint32_t DX, uint32_t DY, uint32_t DZ,
        Mathematics::Axis3D BMin, Mathematics::Axis3D BMax)
    {
        C.DimX      = DX;
        C.DimY      = DY;
        C.DimZ      = DZ;
        C.BoundsMin = BMin;
        C.BoundsMax = BMax;
        C.VoxelSpacing = {
            (BMax.X - BMin.X) / static_cast<float>(DX - 1u),
            (BMax.Y - BMin.Y) / static_cast<float>(DY - 1u),
            (BMax.Z - BMin.Z) / static_cast<float>(DZ - 1u),
        };
        C.MaxVoxelSpacingM = std::max({ C.VoxelSpacing.X, C.VoxelSpacing.Y, C.VoxelSpacing.Z });

        const std::size_t Total = static_cast<std::size_t>(DX) * DY * DZ;
        C.DistanceGrid.assign(Total, 20.0f);
        C.FullDistanceGrid.assign(Total, 20.0f);
        C.RadiosityGrid.assign(Total, { 0.0f, 0.0f, 0.0f });
        C.SurfaceMaskGrid.assign(Total, 0u);

        const float SurfaceBandM = 1.25f * C.MaxVoxelSpacingM;
        const uint32_t Threads = std::min(DZ, std::max(1u, std::thread::hardware_concurrency()));
        const auto BakeSlice = [&](uint32_t Z0, uint32_t Z1)
        {
            for (uint32_t IZ = Z0; IZ < Z1; ++IZ)
            {
                const float PZ = BMin.Z + static_cast<float>(IZ) * C.VoxelSpacing.Z;
                for (uint32_t IY = 0u; IY < DY; ++IY)
                {
                    const float PY = BMin.Y + static_cast<float>(IY) * C.VoxelSpacing.Y;
                    for (uint32_t IX = 0u; IX < DX; ++IX)
                    {
                        const float PX = BMin.X + static_cast<float>(IX) * C.VoxelSpacing.X;
                        const Mathematics::Axis3D P { PX, PY, PZ };
                        const std::size_t Idx = (static_cast<std::size_t>(IZ) * DY + IY) * DX + IX;

                        C.DistanceGrid[Idx] = Scene.EvaluateOccluderSignedDistance(P);

                        uint32_t MatSlot = 0u;
                        const float FullD = Scene.EvaluateFullSceneSignedDistance(P, MatSlot);
                        C.FullDistanceGrid[Idx] = FullD;

                        if (std::fabs(FullD) <= SurfaceBandM)
                        {
                            // Project voxel center onto exact zero-isosurface SurfP = P - N * FullD before lighting!
                            const Mathematics::Axis3D GradN = Scene.EvaluateFullSceneGradientNormal(P);
                            const Mathematics::Axis3D SurfP = P - GradN * FullD;
                            uint32_t SurfSlot = MatSlot;
                            (void)Scene.EvaluateFullSceneSignedDistance(SurfP, SurfSlot);
                            const SdfSurfaceMaterialSpec Mat = Scene.SampleMaterialAt(SurfSlot, SurfP);
                            if (Mat.EmissiveSurface)
                            {
                                continue;
                            }

                            C.SurfaceMaskGrid[Idx] = 1u;
                            if (Scene.EmissiveActive)
                            {
                                const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - SurfP;
                                const float Dist = Mathematics::EvaluateLength(ToS);
                                if (Dist > Scene.EmissiveSphere.Radius + 0.02f)
                                {
                                    const Mathematics::Axis3D L = ToS * (1.0f / Dist);
                                    const float SinMax   = std::clamp(Scene.EmissiveSphere.Radius / Dist, 1.0e-4f, 0.99f);
                                    const float SinMaxSq = SinMax * SinMax;
                                    const float CosN     = Mathematics::EvaluateDot(GradN, L);
                                    float FormF = 0.0f;
                                    if (CosN > SinMax)
                                    {
                                        FormF = SinMaxSq * CosN;
                                    }
                                    else if (CosN > -SinMax)
                                    {
                                        const float TW = (CosN + SinMax) / (2.0f * SinMax);
                                        FormF = SinMaxSq * SinMax * TW * TW * (2.0f - TW);
                                    }

                                    if (FormF > 1.0e-5f)
                                    {
                                        const float Lift = std::max(0.0f, SinMax * 0.85f - CosN);
                                        const Mathematics::Axis3D ConeL = Mathematics::NormalizeAxis(L + GradN * Lift, L);
                                        const float EndT = Dist - Scene.EmissiveSphere.Radius - 0.02f;
                                        float T = 0.04f;
                                        float Vis = 1.0f;
                                        for (uint32_t S = 0u; S < 28u && T < EndT; ++S)
                                        {
                                            const float OD = Scene.EvaluateOccluderSignedDistance(SurfP + GradN * 0.02f + ConeL * T);
                                            if (OD < -0.015f) { Vis = 0.0f; break; }
                                            const float ConeR = std::max(0.015f, T * SinMax);
                                            Vis = std::min(Vis, SmoothHermite01(-0.85f, 0.85f, OD / ConeR));
                                            T  += std::clamp(std::fabs(OD) * 0.60f, 0.03f, 0.22f);
                                        }
                                        const float DiffK = (1.0f - Mat.Metalness) * 0.96f;
                                        // Diffuse Lambertian exitance L_o = (E_direct * Albedo) / Pi = Radiance * Albedo * DiffK * FormF * Vis
                                        C.RadiosityGrid[Idx] = {
                                            Scene.EmissiveSphere.Radiance.R * Mat.BaseColor.R * DiffK * FormF * Vis,
                                            Scene.EmissiveSphere.Radiance.G * Mat.BaseColor.G * DiffK * FormF * Vis,
                                            Scene.EmissiveSphere.Radiance.B * Mat.BaseColor.B * DiffK * FormF * Vis,
                                        };
                                    }
                                }
                            }
                        }
                    }
                }
            }
        };

        if (Threads <= 1u)
        {
            BakeSlice(0u, DZ);
        }
        else
        {
            std::vector<std::thread> Pool;
            const uint32_t Chunk = (DZ + Threads - 1u) / Threads;
            for (uint32_t W = 0u; W < Threads; ++W)
            {
                const uint32_t Z0 = W * Chunk;
                const uint32_t Z1 = std::min(DZ, Z0 + Chunk);
                if (Z0 < Z1) Pool.emplace_back([&, Z0, Z1]() { BakeSlice(Z0, Z1); });
            }
            for (auto& Th : Pool) Th.join();
        }

        C.ExtrapolateRadiosityPyramid();
    };

    // Cascade 0 (Fine 56x56x40): tight interior pitch around column, pedestal, torus, & sphere
    PopulateBounce1Cascade(FineCascade,   56u, 56u, 40u, { -1.95f, -1.65f, -0.15f }, { 1.95f, 2.25f, 2.45f });
    // Cascade 1 (Coarse 44x44x34): full Cornell room bounds capturing red/green walls, floor, & ceiling
    PopulateBounce1Cascade(CoarseCascade, 44u, 44u, 34u, { -2.95f, -2.65f, -0.25f }, { 2.95f, 2.95f, 3.45f });

    // Second-bounce physical SDF cone radiosity propagation across both cascades!
    const auto PropagateSecondBounce = [&](VoxelSdfClipmapCascade& TargetCas)
    {
        std::vector<Mathematics::LinearRGB> Bounce2Rad = TargetCas.RadiosityGrid;
        const uint32_t Threads = std::min(TargetCas.DimZ, std::max(1u, std::thread::hardware_concurrency()));
        const auto PropagateSlice = [&](uint32_t Z0, uint32_t Z1)
        {
            for (uint32_t IZ = Z0; IZ < Z1; ++IZ)
            {
                const float PZ = TargetCas.BoundsMin.Z + static_cast<float>(IZ) * TargetCas.VoxelSpacing.Z;
                for (uint32_t IY = 0u; IY < TargetCas.DimY; ++IY)
                {
                    const float PY = TargetCas.BoundsMin.Y + static_cast<float>(IY) * TargetCas.VoxelSpacing.Y;
                    for (uint32_t IX = 0u; IX < TargetCas.DimX; ++IX)
                    {
                        const std::size_t Idx = (static_cast<std::size_t>(IZ) * TargetCas.DimY + IY) * TargetCas.DimX + IX;
                        if (TargetCas.SurfaceMaskGrid[Idx] == 0u)
                        {
                            Bounce2Rad[Idx] = { 0.0f, 0.0f, 0.0f };
                            continue;
                        }
                        const Mathematics::Axis3D P {
                            TargetCas.BoundsMin.X + static_cast<float>(IX) * TargetCas.VoxelSpacing.X,
                            PY,
                            PZ,
                        };
                        const float FullD = TargetCas.FullDistanceGrid[Idx];
                        const Mathematics::Axis3D Nrm   = Scene.EvaluateFullSceneGradientNormal(P);
                        const Mathematics::Axis3D SurfP = P - Nrm * FullD;
                        uint32_t SurfSlot = 0u;
                        (void)Scene.EvaluateFullSceneSignedDistance(SurfP, SurfSlot);
                        const SdfSurfaceMaterialSpec Mat = Scene.SampleMaterialAt(SurfSlot, SurfP);
                        if (Mat.EmissiveSurface)
                        {
                            continue;
                        }

                        const Mathematics::LinearRGB IndirectIrrad = EvaluateVoxelConeGi(SurfP, Nrm);
                        const float DiffOverPi = (1.0f - Mat.Metalness) * 0.96f / Constants::PiFloat;
                        Bounce2Rad[Idx] = {
                            TargetCas.RadiosityGrid[Idx].R + Mat.BaseColor.R * DiffOverPi * IndirectIrrad.R,
                            TargetCas.RadiosityGrid[Idx].G + Mat.BaseColor.G * DiffOverPi * IndirectIrrad.G,
                            TargetCas.RadiosityGrid[Idx].B + Mat.BaseColor.B * DiffOverPi * IndirectIrrad.B,
                        };
                    }
                }
            }
        };

        if (Threads <= 1u)
        {
            PropagateSlice(0u, TargetCas.DimZ);
        }
        else
        {
            std::vector<std::thread> Pool;
            const uint32_t Chunk = (TargetCas.DimZ + Threads - 1u) / Threads;
            for (uint32_t W = 0u; W < Threads; ++W)
            {
                const uint32_t Z0 = W * Chunk;
                const uint32_t Z1 = std::min(TargetCas.DimZ, Z0 + Chunk);
                if (Z0 < Z1) Pool.emplace_back([&, Z0, Z1]() { PropagateSlice(Z0, Z1); });
            }
            for (auto& Th : Pool) Th.join();
        }
        return Bounce2Rad;
    };

    std::vector<Mathematics::LinearRGB> NextFine   = PropagateSecondBounce(FineCascade);
    std::vector<Mathematics::LinearRGB> NextCoarse = PropagateSecondBounce(CoarseCascade);
    FineCascade.RadiosityGrid   = std::move(NextFine);
    CoarseCascade.RadiosityGrid = std::move(NextCoarse);
    FineCascade.ExtrapolateRadiosityPyramid();
    CoarseCascade.ExtrapolateRadiosityPyramid();
}

void DualCascadeClipmapVolume::BuildFromHybridEnvironment(
    const HybridOccluderEnvironment& Env,
    Mathematics::Axis3D              CenterWorld,
    float                            FineHalfExtentM,
    float                            CoarseHalfExtentM)
{
    const auto BakeEnvCascade = [&](VoxelSdfClipmapCascade& C, uint32_t DX, uint32_t DY, uint32_t DZ, float HalfExt)
    {
        C.DimX      = DX;
        C.DimY      = DY;
        C.DimZ      = DZ;
        C.BoundsMin = { CenterWorld.X - HalfExt, CenterWorld.Y - HalfExt, -0.25f };
        C.BoundsMax = { CenterWorld.X + HalfExt, CenterWorld.Y + HalfExt, -0.25f + HalfExt * 1.35f };
        C.VoxelSpacing = {
            (C.BoundsMax.X - C.BoundsMin.X) / static_cast<float>(DX - 1u),
            (C.BoundsMax.Y - C.BoundsMin.Y) / static_cast<float>(DY - 1u),
            (C.BoundsMax.Z - C.BoundsMin.Z) / static_cast<float>(DZ - 1u),
        };
        C.MaxVoxelSpacingM = std::max({ C.VoxelSpacing.X, C.VoxelSpacing.Y, C.VoxelSpacing.Z });
        const std::size_t Total = static_cast<std::size_t>(DX) * DY * DZ;
        C.DistanceGrid.assign(Total, 20.0f);
        C.FullDistanceGrid.clear();
        C.RadiosityGrid.clear();
        C.Mip1Radiosity.clear();
        C.Mip2Radiosity.clear();
        C.SurfaceMaskGrid.clear();

        for (uint32_t IZ = 0u; IZ < DZ; ++IZ)
        {
            const float PZ = C.BoundsMin.Z + static_cast<float>(IZ) * C.VoxelSpacing.Z;
            for (uint32_t IY = 0u; IY < DY; ++IY)
            {
                const float PY = C.BoundsMin.Y + static_cast<float>(IY) * C.VoxelSpacing.Y;
                for (uint32_t IX = 0u; IX < DX; ++IX)
                {
                    const float PX = C.BoundsMin.X + static_cast<float>(IX) * C.VoxelSpacing.X;
                    C.DistanceGrid[(static_cast<std::size_t>(IZ) * DY + IY) * DX + IX] =
                        EvaluateInteriorOccluderDistance(Env, { PX, PY, PZ });
                }
            }
        }
    };

    BakeEnvCascade(FineCascade,   48u, 48u, 32u, FineHalfExtentM);
    BakeEnvCascade(CoarseCascade, 32u, 32u, 24u, CoarseHalfExtentM);
}

Mathematics::LinearRGB DualCascadeClipmapVolume::EvaluateVoxelConeGi(
    Mathematics::Axis3D WorldPos,
    Mathematics::Axis3D ShadingNormal) const noexcept
{
    if (CoarseCascade.RadiosityGrid.empty() || CoarseCascade.FullDistanceGrid.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Genuine 3D SDF Cone Tracing across FineCascade + CoarseCascade (zero hardcoded room box!)
    const Mathematics::Axis3D Origin = WorldPos + ShadingNormal * 0.065f;
    Mathematics::LinearRGB AccumRadiance { 0.0f, 0.0f, 0.0f };
    float CosWeightSum = 0.0f;

    for (const auto& ConeDir : FibonacciUnitCones)
    {
        const float CosTheta = Mathematics::EvaluateDot(ShadingNormal, ConeDir);
        if (CosTheta <= 0.03f)
        {
            continue;
        }

        float Travel = 0.11f;
        float Alpha  = 0.0f;
        Mathematics::LinearRGB ConeRad { 0.0f, 0.0f, 0.0f };

        for (uint32_t Step = 0u; Step < 26u && Alpha < 0.97f && Travel < 8.5f; ++Step)
        {
            const Mathematics::Axis3D SampleP = Origin + ConeDir * Travel;
            if (!CoarseCascade.ContainsInsideMargin(SampleP, 0.005f))
            {
                break;
            }

            const float FullD = SampleFullSceneDistance(SampleP);
            const float ConeR = std::max(0.085f, Travel * 0.34f);
            if (FullD < ConeR)
            {
                const float SelfGuard = SmoothHermite01(0.07f, 0.22f, Travel);
                const float StepOcc   = (1.0f - SmoothHermite01(-0.08f * ConeR, ConeR, FullD)) * 0.96f * SelfGuard;
                if (StepOcc > 1.0e-4f)
                {
                    const Mathematics::LinearRGB SurfRad = SampleClipmapRadiosity(SampleP, ConeR * 1.25f);
                    const float StepWeight = (1.0f - Alpha) * StepOcc;
                    ConeRad.R += SurfRad.R * StepWeight;
                    ConeRad.G += SurfRad.G * StepWeight;
                    ConeRad.B += SurfRad.B * StepWeight;
                    Alpha     += StepWeight;
                }
            }
            Travel += std::max(0.070f, std::max(FullD * 0.68f, ConeR * 0.42f));
        }
        if (Alpha > 0.35f && Alpha < 0.98f)
        {
            const float NormW = 1.0f / std::max(0.76f, Alpha);
            ConeRad = ConeRad * NormW;
        }

        AccumRadiance.R += ConeRad.R * CosTheta;
        AccumRadiance.G += ConeRad.G * CosTheta;
        AccumRadiance.B += ConeRad.B * CosTheta;
        CosWeightSum    += CosTheta;
    }

    if (CosWeightSum <= 1.0e-5f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float Scale = Constants::PiFloat / CosWeightSum;
    return {
        AccumRadiance.R * Scale,
        AccumRadiance.G * Scale,
        AccumRadiance.B * Scale,
    };
}

Mathematics::LinearRGB DualCascadeClipmapVolume::EvaluateVoxelSpecularConeBounce(
    Mathematics::Axis3D           WorldPos,
    Mathematics::Axis3D           ShadingNormal,
    Mathematics::Axis3D           ViewDir,
    const SdfSurfaceMaterialSpec& Material) const noexcept
{
    if (CoarseCascade.RadiosityGrid.empty() || CoarseCascade.FullDistanceGrid.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (Material.Roughness > 0.72f && Material.Metalness < 0.02f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float NdotV = std::clamp(Mathematics::EvaluateDot(ShadingNormal, ViewDir), 1.0e-4f, 1.0f);
    const Mathematics::Axis3D ReflDir = Mathematics::NormalizeAxis(
        ShadingNormal * (2.0f * NdotV) - ViewDir, ShadingNormal);
    const float TanSpecCone = std::clamp(Material.Roughness * Material.Roughness * 0.65f + 0.03f, 0.03f, 0.45f);

    const Mathematics::Axis3D Origin = WorldPos + ShadingNormal * 0.055f;
    float Travel = 0.09f;
    float Alpha  = 0.0f;
    Mathematics::LinearRGB SpecRad { 0.0f, 0.0f, 0.0f };

    for (uint32_t Step = 0u; Step < 28u && Alpha < 0.96f && Travel < 8.5f; ++Step)
    {
        const Mathematics::Axis3D SampleP = Origin + ReflDir * Travel;
        if (!CoarseCascade.ContainsInsideMargin(SampleP, 0.005f))
        {
            break;
        }
        const float FullD = SampleFullSceneDistance(SampleP);
        const float ConeR = std::max(0.065f, Travel * TanSpecCone);
        if (FullD < ConeR)
        {
            const float SelfGuard = SmoothHermite01(0.06f, 0.20f, Travel);
            const float StepOcc   = (1.0f - SmoothHermite01(-0.20f * ConeR, ConeR, FullD)) * 0.88f * SelfGuard;
            if (StepOcc > 1.0e-4f)
            {
                const Mathematics::LinearRGB SurfRad = SampleClipmapRadiosity(SampleP, ConeR * 1.15f);
                const float StepW = (1.0f - Alpha) * StepOcc;
                SpecRad.R += SurfRad.R * StepW;
                SpecRad.G += SurfRad.G * StepW;
                SpecRad.B += SurfRad.B * StepW;
                Alpha     += StepW;
            }
        }
        Travel += std::max(0.06f, std::max(FullD * 0.72f, ConeR * 0.50f));
    }

    const float Schlick = std::pow(1.0f - NdotV, 5.0f);
    const float RoughAtten = 1.0f - Material.Roughness * 0.55f;
    const Mathematics::LinearRGB F0 {
        0.04f * (1.0f - Material.Metalness) + Material.BaseColor.R * Material.Metalness,
        0.04f * (1.0f - Material.Metalness) + Material.BaseColor.G * Material.Metalness,
        0.04f * (1.0f - Material.Metalness) + Material.BaseColor.B * Material.Metalness,
    };
    const Mathematics::LinearRGB DirEnvBrdf {
        (F0.R + (std::max(1.0f - Material.Roughness, F0.R) - F0.R) * Schlick) * RoughAtten,
        (F0.G + (std::max(1.0f - Material.Roughness, F0.G) - F0.G) * Schlick) * RoughAtten,
        (F0.B + (std::max(1.0f - Material.Roughness, F0.B) - F0.B) * Schlick) * RoughAtten,
    };
    return {
        SpecRad.R * DirEnvBrdf.R,
        SpecRad.G * DirEnvBrdf.G,
        SpecRad.B * DirEnvBrdf.B,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                           WORLD DIRECTIONAL SDF SHADOW (USED BY ORDER 2 GTXLOWEND)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateWorldDirectionalSdfShadow(
    SdfShadowPenumbraMode            Mode,
    const HybridOccluderEnvironment& Env,
    const DualCascadeClipmapVolume*  OptionalClipmap,
    Mathematics::Axis3D              ShadingPos,
    Mathematics::Axis3D              ShadingNormal,
    Mathematics::Axis3D              DirectionToLight,
    float                            TanAngularRadius,
    uint32_t                         PixelKey) noexcept
{
    if (Mathematics::EvaluateDot(ShadingNormal, DirectionToLight) <= 1.0e-4f)
    {
        return 0.0f;
    }
    if (Env.ForbidDistanceField)
    {
        return 1.0f;
    }

    if (Mode == SdfShadowPenumbraMode::AnalyticSdfConePenumbra)
    {
        HybridConeShadowSetup Setup {};
        Setup.TanSunRadius = TanAngularRadius;
        return EvaluateGtxHybridDirectionalShadow(Env, ShadingPos, ShadingNormal, DirectionToLight, Setup);
    }

    if (Mode == SdfShadowPenumbraMode::PointSampleSdf)
    {
        if (Env.SceneBvh != nullptr)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
            ContactRay.UnitDirection   = DirectionToLight;
            ContactRay.MinimumDistance = 1.5e-3f;
            ContactRay.MaximumDistance = 0.28f;
            if (Env.SceneBvh->QueryRayOccluded(ContactRay))
            {
                return 0.0f;
            }
        }
        float Travel = 0.06f;
        for (uint32_t S = 0u; S < 96u && Travel < 8.5f; ++S)
        {
            const float D = EvaluateInteriorOccluderDistance(Env, ShadingPos + DirectionToLight * Travel);
            if (D < 0.032f && Travel > 0.14f)
            {
                return 0.0f;
            }
            Travel += std::clamp(D * 0.45f, 0.02f, 0.18f);
        }
        return 1.0f;
    }

    if ((Mode == SdfShadowPenumbraMode::VoxelSdfClipmapPenumbra ||
         Mode == SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi) &&
        OptionalClipmap != nullptr)
    {
        float MinRatio = 2.0f;
        float Travel   = 0.06f;
        for (uint32_t S = 0u; S < 72u && Travel < 8.5f; ++S)
        {
            const Mathematics::Axis3D SP = ShadingPos + DirectionToLight * Travel;
            const float PlaneFade = SmoothHermite01(0.02f, 0.12f, Mathematics::EvaluateDot(SP - ShadingPos, ShadingNormal));
            const float ClipD     = OptionalClipmap->SampleClipmapDistance(SP);
            if (PlaneFade > 1.0e-4f)
            {
                const float ConeR  = std::max(0.024f, Travel * TanAngularRadius);
                const float VoxMid = std::max(0.052f, ConeR * 0.62f);
                const float VoxHlf = std::max(0.028f, ConeR * 0.42f);
                const float Ratio  = std::clamp((ClipD - VoxMid) / VoxHlf, -1.5f, 1.5f) * PlaneFade + 1.5f * (1.0f - PlaneFade);
                MinRatio = SoftMinimum(MinRatio, Ratio, 0.14f);
                if (MinRatio <= -1.0f) return 0.0f;
            }
            Travel += std::clamp(std::fabs(ClipD) * 0.42f, 0.025f, 0.18f);
        }
        return SmoothHermite01(-0.95f, 0.95f, MinRatio);
    }

    const uint32_t SampleCount = (Mode == SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp) ? 96u : 16u;
    const float CosThetaMax    = 1.0f / std::sqrt(1.0f + TanAngularRadius * TanAngularRadius);
    Mathematics::Axis3D TanU {}, BitV {};
    BuildDuffOrthonormalBasis(DirectionToLight, TanU, BitV);

    uint32_t Unoccluded = 0u;
    for (uint32_t S = 0u; S < SampleCount; ++S)
    {
        const Mathematics::Axis2D U = EvaluateOwenScrambledSobol02(S, PixelKey, 0u);
        const float CosTheta = 1.0f - U.X * (1.0f - CosThetaMax);
        const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
        const float Phi      = Constants::TauFloat * U.Y;
        const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
            TanU * (SinTheta * std::cos(Phi)) + BitV * (SinTheta * std::sin(Phi)) + DirectionToLight * CosTheta,
            DirectionToLight);
        if (Mathematics::EvaluateDot(ShadingNormal, RayDir) <= 0.0f)
        {
            continue;
        }

        bool Blocked = false;
        if (Env.SceneBvh != nullptr)
        {
            SpatialAcceleration::RaySegmentQuery ContactRay {};
            ContactRay.Origin          = ShadingPos + ShadingNormal * 3.0e-3f;
            ContactRay.UnitDirection   = RayDir;
            ContactRay.MinimumDistance = 1.5e-3f;
            ContactRay.MaximumDistance = 0.28f;
            Blocked = Env.SceneBvh->QueryRayOccluded(ContactRay);
        }
        if (!Blocked)
        {
            float Travel = 0.22f;
            for (uint32_t Step = 0u; Step < 64u && Travel < 8.5f; ++Step)
            {
                const float D = EvaluateInteriorOccluderDistance(Env, ShadingPos + RayDir * Travel);
                if (D < 0.036f)
                {
                    Blocked = true;
                    break;
                }
                Travel += std::clamp(D * 0.55f, 0.025f, 0.22f);
            }
        }
        if (!Blocked)
        {
            ++Unoccluded;
        }
    }
    return static_cast<float>(Unoccluded) / static_cast<float>(SampleCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                      PROOF 11 EMISSIVE-SPHERE-ONLY 6-MODE SDF SHADOW + PENUMBRA RENDERER
//------------------------------------------------------------------------------------------------------------------------

namespace
{

[[nodiscard]] bool TraceShowcasePrimarySdfRay(
    const SdfShowcaseScene& Scene,
    Mathematics::Axis3D     Origin,
    Mathematics::Axis3D     UnitDir,
    float                   MaxDist,
    Mathematics::Axis3D&    OutPos,
    Mathematics::Axis3D&    OutNormal,
    uint32_t&               OutMaterialSlot) noexcept
{
    float T = 0.01f;
    uint32_t Mat = 0u;
    for (uint32_t Step = 0u; Step < 128u && T < MaxDist; ++Step)
    {
        const Mathematics::Axis3D P = Origin + UnitDir * T;
        const float D = Scene.EvaluateFullSceneSignedDistance(P, Mat);
        if (D < 1.2e-3f)
        {
            OutPos          = P;
            OutNormal       = Scene.EvaluateFullSceneGradientNormal(P);
            OutMaterialSlot = Mat;
            return true;
        }
        T += std::max(1.2e-3f, D * 0.92f);
    }
    return false;
}

[[nodiscard]] bool TraceOccluderRayBlocked(
    const SdfShowcaseScene& Scene,
    Mathematics::Axis3D     Origin,
    Mathematics::Axis3D     UnitDir,
    float                   MinT,
    float                   MaxT) noexcept
{
    float T = MinT;
    for (uint32_t Step = 0u; Step < 96u && T < MaxT; ++Step)
    {
        const float D = Scene.EvaluateOccluderSignedDistance(Origin + UnitDir * T);
        if (D < 1.8e-3f)
        {
            return true;
        }
        T += std::max(2.5e-3f, D * 0.88f);
    }
    return false;
}

[[nodiscard]] float EvaluateEmissiveSphereVisibilityMode(
    SdfShadowPenumbraMode           Mode,
    const SdfShowcaseScene&         Scene,
    const DualCascadeClipmapVolume& Clipmap,
    Mathematics::Axis3D             Pos,
    Mathematics::Axis3D             Normal,
    uint32_t                        PixelKey) noexcept
{
    if (!Scene.EmissiveActive)
    {
        return 0.0f;
    }

    const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - Pos;
    const float CenterDist = Mathematics::EvaluateLength(ToS);
    if (CenterDist <= Scene.EmissiveSphere.Radius + 1.0e-3f)
    {
        return 1.0f;
    }
    const Mathematics::Axis3D L    = ToS * (1.0f / CenterDist);
    const float               EndT = CenterDist - Scene.EmissiveSphere.Radius - 0.015f;
    if (EndT <= 0.02f)
    {
        return 1.0f;
    }

    // Mode 0: Point-Sample SDF (No Penumbra) — binary ray to sphere center
    if (Mode == SdfShadowPenumbraMode::PointSampleSdf)
    {
        if (Mathematics::EvaluateDot(Normal, L) <= 0.0f)
        {
            return 0.0f;
        }
        return TraceOccluderRayBlocked(Scene, Pos + Normal * 6.0e-3f, L, 0.012f, EndT) ? 0.0f : 1.0f;
    }

    // Aim soft-shadow cone at the horizon-visible upper cap of the spherical luminaire
    const float SinCap  = std::clamp(Scene.EmissiveSphere.Radius / CenterDist, 1.0e-4f, 0.99f);
    const float NdotL0  = Mathematics::EvaluateDot(Normal, L);
    const float Lift    = std::max(0.0f, SinCap * 0.85f - NdotL0);
    const Mathematics::Axis3D ConeDirL = Mathematics::NormalizeAxis(L + Normal * Lift, L);

    // Mode 1: Analytic SDF Cone Penumbra (exact [-ConeR, +ConeR] physical penumbra wedge)
    if (Mode == SdfShadowPenumbraMode::AnalyticSdfConePenumbra)
    {
        const float TanCone = Scene.EmissiveSphere.Radius / std::max(Scene.EmissiveSphere.Radius + 0.04f, CenterDist);
        float MinRatio = 2.0f;
        float Travel   = 0.03f;
        for (uint32_t S = 0u; S < 112u && Travel < EndT; ++S)
        {
            const Mathematics::Axis3D SP = Pos + Normal * 8.0e-3f + ConeDirL * Travel;
            const float PlaneFade = SmoothHermite01(0.015f, 0.11f, Mathematics::EvaluateDot(SP - Pos, Normal));
            const float Dist      = Scene.EvaluateOccluderSignedDistance(SP);
            if (PlaneFade > 1.0e-4f)
            {
                const float ConeR = std::max(0.008f, Travel * TanCone);
                const float Ratio = std::clamp(Dist / ConeR, -1.25f, 1.25f) * PlaneFade + 1.25f * (1.0f - PlaneFade);
                MinRatio = SoftMinimum(MinRatio, Ratio, 0.08f);
                if (MinRatio <= -1.02f) return 0.0f;
            }
            Travel += std::clamp(std::fabs(Dist) * 0.45f, 0.015f, 0.12f);
        }
        return SmoothHermite01(-1.0f, 1.0f, MinRatio);
    }

    // Modes 2 & 3: 3D Voxel SDF Clipmap Penumbra
    if (Mode == SdfShadowPenumbraMode::VoxelSdfClipmapPenumbra ||
        Mode == SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi)
    {
        const float TanCone = Scene.EmissiveSphere.Radius / std::max(Scene.EmissiveSphere.Radius + 0.04f, CenterDist);
        float MinRatio = 2.0f;
        float Travel   = 0.04f;
        for (uint32_t S = 0u; S < 96u && Travel < EndT; ++S)
        {
            const Mathematics::Axis3D SP = Pos + Normal * 1.0e-2f + ConeDirL * Travel;
            const float PlaneFade = SmoothHermite01(0.018f, 0.12f, Mathematics::EvaluateDot(SP - Pos, Normal));
            const float ClipD     = Clipmap.SampleClipmapDistance(SP);
            if (PlaneFade > 1.0e-4f)
            {
                const float ConeR = std::max(0.014f, Travel * TanCone * 0.90f);
                const float Ratio = std::clamp(ClipD / ConeR, -1.25f, 1.25f) * PlaneFade + 1.25f * (1.0f - PlaneFade);
                MinRatio = SoftMinimum(MinRatio, Ratio, 0.10f);
                if (MinRatio <= -1.02f) return 0.0f;
            }
            Travel += std::clamp(std::fabs(ClipD) * 0.45f, 0.020f, 0.14f);
        }
        return SmoothHermite01(-1.0f, 1.0f, MinRatio);
    }

    // Modes 4 & 5 visibility scalar audit: Sobol Spherical-Cap solid-angle fraction
    const uint32_t CapSamples = (Mode == SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp) ? 64u : 24u;
    const float SinMax        = std::clamp(Scene.EmissiveSphere.Radius / CenterDist, 1.0e-4f, 0.9995f);
    const float CosThetaMax   = std::sqrt(std::max(0.0f, 1.0f - SinMax * SinMax));
    Mathematics::Axis3D TanU {}, BitV {};
    BuildDuffOrthonormalBasis(L, TanU, BitV);

    float VisSum = 0.0f;
    float WtSum  = 0.0f;
    for (uint32_t S = 0u; S < CapSamples; ++S)
    {
        const Mathematics::Axis2D U = EvaluateOwenScrambledSobol02(S, PixelKey, 0u);
        const float CosTheta = 1.0f - U.X * (1.0f - CosThetaMax);
        const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
        const float Phi      = Constants::TauFloat * U.Y;
        const Mathematics::Axis3D SampleDir = Mathematics::NormalizeAxis(
            TanU * (SinTheta * std::cos(Phi)) + BitV * (SinTheta * std::sin(Phi)) + L * CosTheta, L);
        const float CosN = Mathematics::EvaluateDot(Normal, SampleDir);
        if (CosN <= 0.0f) continue;

        float SphereHitT = EndT;
        (void)IntersectRaySphereDistance(Pos + Normal * 5.0e-3f, SampleDir, Scene.EmissiveSphere.Center, Scene.EmissiveSphere.Radius, SphereHitT);
        WtSum += CosN;
        if (!TraceOccluderRayBlocked(Scene, Pos + Normal * 5.0e-3f, SampleDir, 0.01f, std::max(0.02f, SphereHitT - 0.012f)))
        {
            VisSum += CosN;
        }
    }
    return (WtSum > 1.0e-6f) ? (VisSum / WtSum) : 0.0f;
}

// Mode 4: Hybrid Contact + Owen-Scrambled Sobol (0,2) Spherical-Cap Direct Integrator (Pure Direct, Zero Fake Ambient)
[[nodiscard]] Mathematics::LinearRGB EvaluateHybridSobolSphericalCapDirect(
    const SdfShowcaseScene&       Scene,
    const SdfSurfaceMaterialSpec& Mat,
    Mathematics::Axis3D           Pos,
    Mathematics::Axis3D           Normal,
    Mathematics::Axis3D           ViewDir,
    uint32_t                      PixelKey,
    uint32_t                      SampleCount,
    float&                        OutVisibility) noexcept
{
    OutVisibility = 0.0f;
    if (!Scene.EmissiveActive)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - Pos;
    const float Dist = Mathematics::EvaluateLength(ToS);
    if (Dist <= Scene.EmissiveSphere.Radius + 1.0e-3f)
    {
        OutVisibility = 1.0f;
        return Scene.EmissiveSphere.Radiance;
    }

    const Mathematics::Axis3D L = ToS * (1.0f / Dist);
    const float SinMax      = std::clamp(Scene.EmissiveSphere.Radius / Dist, 1.0e-4f, 0.9995f);
    const float CosThetaMax = std::sqrt(std::max(0.0f, 1.0f - SinMax * SinMax));
    const float SolidAngle  = Constants::TauFloat * (1.0f - CosThetaMax);

    Mathematics::Axis3D TanU {}, BitV {};
    BuildDuffOrthonormalBasis(L, TanU, BitV);

    Mathematics::LinearRGB Accum { 0.0f, 0.0f, 0.0f };
    uint32_t Unoccluded   = 0u;
    uint32_t HorizonValid = 0u;

    for (uint32_t S = 0u; S < SampleCount; ++S)
    {
        const Mathematics::Axis2D U = EvaluateOwenScrambledSobol02(S, PixelKey, 1u);
        const float CosTheta = 1.0f - U.X * (1.0f - CosThetaMax);
        const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
        const float Phi      = Constants::TauFloat * U.Y;
        const Mathematics::Axis3D Wi = Mathematics::NormalizeAxis(
            TanU * (SinTheta * std::cos(Phi)) + BitV * (SinTheta * std::sin(Phi)) + L * CosTheta, L);

        const float NdotL = Mathematics::EvaluateDot(Normal, Wi);
        if (NdotL <= 1.0e-5f)
        {
            continue;
        }
        ++HorizonValid;

        const Mathematics::Axis3D RayOrig = Pos + Normal * 5.0e-3f;
        float SphereHitT = Dist - Scene.EmissiveSphere.Radius;
        (void)IntersectRaySphereDistance(RayOrig, Wi, Scene.EmissiveSphere.Center, Scene.EmissiveSphere.Radius, SphereHitT);
        const float MaxOccT = std::max(0.015f, SphereHitT - 0.012f);

        bool Blocked = TraceOccluderRayBlocked(Scene, RayOrig, Wi, 0.008f, std::min(0.28f, MaxOccT));
        if (!Blocked && MaxOccT > 0.28f)
        {
            Blocked = TraceOccluderRayBlocked(Scene, RayOrig, Wi, 0.27f, MaxOccT);
        }
        if (Blocked)
        {
            continue;
        }

        ++Unoccluded;
        float SpecPdf = 0.0f, DiffPdf = 0.0f;
        const Mathematics::LinearRGB Bsdf = EvaluateOpenPbrBsdf(Mat, Normal, ViewDir, Wi, SpecPdf, DiffPdf);
        const float Weight = NdotL * SolidAngle;
        Accum.R += Scene.EmissiveSphere.Radiance.R * Bsdf.R * Weight;
        Accum.G += Scene.EmissiveSphere.Radiance.G * Bsdf.G * Weight;
        Accum.B += Scene.EmissiveSphere.Radiance.B * Bsdf.B * Weight;
    }

    OutVisibility = (HorizonValid > 0u) ? (static_cast<float>(Unoccluded) / static_cast<float>(HorizonValid)) : 0.0f;
    const float InvS = 1.0f / static_cast<float>(SampleCount);
    return { Accum.R * InvS, Accum.G * InvS, Accum.B * InvS };
}

// Mode 5: Unbiased 96-SPP Multi-Bounce SDF Monte Carlo Path-Tracing Reference (Owen-Scrambled Sobol + Spherical-Cap NEE + VNDF MIS)
[[nodiscard]] Mathematics::LinearRGB EvaluateSdfMonteCarloPathTracePixel(
    const SdfShowcaseScene& Scene,
    Mathematics::Axis3D     PrimaryPos,
    Mathematics::Axis3D     PrimaryNormal,
    uint32_t                PrimaryMatSlot,
    Mathematics::Axis3D     PrimaryViewDir,
    uint32_t                PixelKey,
    uint32_t                SppCount) noexcept
{
    const SdfSurfaceMaterialSpec PrimMat = Scene.SampleMaterialAt(PrimaryMatSlot, PrimaryPos);
    if (PrimMat.EmissiveSurface)
    {
        return PrimMat.Emission;
    }

    Mathematics::LinearRGB TotalRadiance { 0.0f, 0.0f, 0.0f };
    constexpr uint32_t MaxBounces = 3u;

    for (uint32_t SampleIdx = 0u; SampleIdx < SppCount; ++SampleIdx)
    {
        Mathematics::Axis3D    CurPos     = PrimaryPos;
        Mathematics::Axis3D    CurNormal  = PrimaryNormal;
        Mathematics::Axis3D    CurView    = PrimaryViewDir;
        SdfSurfaceMaterialSpec CurMat     = PrimMat;
        Mathematics::LinearRGB Throughput { 1.0f, 1.0f, 1.0f };
        Mathematics::LinearRGB SampleRad  { 0.0f, 0.0f, 0.0f };

        for (uint32_t Bounce = 0u; Bounce < MaxBounces; ++Bounce)
        {
            if (Scene.EmissiveActive)
            {
                const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - CurPos;
                const float Dist = Mathematics::EvaluateLength(ToS);
                if (Dist > Scene.EmissiveSphere.Radius + 1.0e-3f)
                {
                    const Mathematics::Axis3D L = ToS * (1.0f / Dist);
                    const float SinMax      = std::clamp(Scene.EmissiveSphere.Radius / Dist, 1.0e-4f, 0.9995f);
                    const float CosThetaMax = std::sqrt(std::max(0.0f, 1.0f - SinMax * SinMax));
                    const float SolidAngle  = Constants::TauFloat * (1.0f - CosThetaMax);
                    const float LightPdf    = 1.0f / std::max(1.0e-6f, SolidAngle);

                    Mathematics::Axis3D TanU {}, BitV {};
                    BuildDuffOrthonormalBasis(L, TanU, BitV);

                    const Mathematics::Axis2D ULight = EvaluateOwenScrambledSobol02(SampleIdx, PixelKey, Bounce * 3u + 1u);
                    const float CosTheta = 1.0f - ULight.X * (1.0f - CosThetaMax);
                    const float SinTheta = std::sqrt(std::max(0.0f, 1.0f - CosTheta * CosTheta));
                    const float Phi      = Constants::TauFloat * ULight.Y;
                    const Mathematics::Axis3D Wi = Mathematics::NormalizeAxis(
                        TanU * (SinTheta * std::cos(Phi)) + BitV * (SinTheta * std::sin(Phi)) + L * CosTheta, L);

                    const float NdotL = Mathematics::EvaluateDot(CurNormal, Wi);
                    if (NdotL > 1.0e-5f)
                    {
                        const Mathematics::Axis3D RayOrig = CurPos + CurNormal * 5.0e-3f;
                        float SphereHitT = Dist - Scene.EmissiveSphere.Radius;
                        (void)IntersectRaySphereDistance(RayOrig, Wi, Scene.EmissiveSphere.Center, Scene.EmissiveSphere.Radius, SphereHitT);
                        if (!TraceOccluderRayBlocked(Scene, RayOrig, Wi, 0.008f, std::max(0.015f, SphereHitT - 0.012f)))
                        {
                            float SpecPdf = 0.0f, DiffPdf = 0.0f;
                            const Mathematics::LinearRGB Bsdf = EvaluateOpenPbrBsdf(CurMat, CurNormal, CurView, Wi, SpecPdf, DiffPdf);
                            const float SpecProb = std::clamp(0.12f + 0.78f * CurMat.Metalness + 0.25f * (1.0f - CurMat.Roughness), 0.10f, 0.90f);
                            const float BsdfPdf  = SpecProb * SpecPdf + (1.0f - SpecProb) * DiffPdf;
                            const float MisW     = (LightPdf * LightPdf) / std::max(1.0e-10f, LightPdf * LightPdf + BsdfPdf * BsdfPdf);
                            const float Scale    = (NdotL * MisW) / LightPdf;
                            Mathematics::LinearRGB Contrib {
                                Throughput.R * Scene.EmissiveSphere.Radiance.R * Bsdf.R * Scale,
                                Throughput.G * Scene.EmissiveSphere.Radiance.G * Bsdf.G * Scale,
                                Throughput.B * Scene.EmissiveSphere.Radiance.B * Bsdf.B * Scale,
                            };
                            if (Bounce > 0u)
                            {
                                Contrib.R = std::min(Contrib.R, 3.5f);
                                Contrib.G = std::min(Contrib.G, 3.5f);
                                Contrib.B = std::min(Contrib.B, 3.5f);
                            }
                            SampleRad.R += Contrib.R;
                            SampleRad.G += Contrib.G;
                            SampleRad.B += Contrib.B;
                        }
                    }
                }
            }

            if (Bounce + 1u >= MaxBounces)
            {
                break;
            }

            Mathematics::Axis3D SurfT {}, SurfB {};
            BuildDuffOrthonormalBasis(CurNormal, SurfT, SurfB);
            const Mathematics::Axis3D ViewLocal {
                Mathematics::EvaluateDot(CurView, SurfT),
                Mathematics::EvaluateDot(CurView, SurfB),
                std::max(1.0e-5f, Mathematics::EvaluateDot(CurView, CurNormal)),
            };

            const Mathematics::Axis2D ULobe = EvaluateOwenScrambledSobol02(SampleIdx, PixelKey, Bounce * 3u + 2u);
            const Mathematics::Axis2D UDir  = EvaluateOwenScrambledSobol02(SampleIdx, PixelKey, Bounce * 3u + 3u);
            const float SpecProb = std::clamp(0.12f + 0.78f * CurMat.Metalness + 0.25f * (1.0f - CurMat.Roughness), 0.10f, 0.90f);
            const float Alpha    = std::clamp(CurMat.Roughness * CurMat.Roughness, 0.004f, 1.0f);

            const Mathematics::Axis3D WiLocal = (ULobe.X < SpecProb)
                ? SampleSphericalCapGgxReflectionLocal(ViewLocal, Alpha, UDir)
                : SampleConcentricCosineLocal(UDir);
            if (WiLocal.Z <= 1.0e-5f)
            {
                break;
            }

            const Mathematics::Axis3D WiWorld = Mathematics::NormalizeAxis(
                SurfT * WiLocal.X + SurfB * WiLocal.Y + CurNormal * WiLocal.Z, CurNormal);
            float SpecPdf = 0.0f, DiffPdf = 0.0f;
            const Mathematics::LinearRGB Bsdf = EvaluateOpenPbrBsdf(CurMat, CurNormal, CurView, WiWorld, SpecPdf, DiffPdf);
            const float BsdfPdf = SpecProb * SpecPdf + (1.0f - SpecProb) * DiffPdf;
            const float NdotL   = Mathematics::EvaluateDot(CurNormal, WiWorld);
            if (BsdfPdf <= 1.0e-6f || NdotL <= 1.0e-5f)
            {
                break;
            }

            Mathematics::Axis3D NextPos {}, NextNrm {};
            uint32_t NextMatSlot = 0u;
            if (!TraceShowcasePrimarySdfRay(Scene, CurPos + CurNormal * 6.0e-3f, WiWorld, 12.0f, NextPos, NextNrm, NextMatSlot))
            {
                break;
            }

            const SdfSurfaceMaterialSpec NextMat = Scene.SampleMaterialAt(NextMatSlot, NextPos);
            Throughput.R *= (Bsdf.R * NdotL) / BsdfPdf;
            Throughput.G *= (Bsdf.G * NdotL) / BsdfPdf;
            Throughput.B *= (Bsdf.B * NdotL) / BsdfPdf;

            if (NextMat.EmissiveSurface)
            {
                const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - CurPos;
                const float Dist = Mathematics::EvaluateLength(ToS);
                float MisW = 1.0f;
                if (Dist > Scene.EmissiveSphere.Radius + 1.0e-3f)
                {
                    const float SinMax      = std::clamp(Scene.EmissiveSphere.Radius / Dist, 1.0e-4f, 0.9995f);
                    const float CosThetaMax = std::sqrt(std::max(0.0f, 1.0f - SinMax * SinMax));
                    const float LightPdf    = 1.0f / std::max(1.0e-6f, Constants::TauFloat * (1.0f - CosThetaMax));
                    MisW = (BsdfPdf * BsdfPdf) / std::max(1.0e-10f, BsdfPdf * BsdfPdf + LightPdf * LightPdf);
                }
                SampleRad.R += std::min(3.5f, Throughput.R * NextMat.Emission.R * MisW);
                SampleRad.G += std::min(3.5f, Throughput.G * NextMat.Emission.G * MisW);
                SampleRad.B += std::min(3.5f, Throughput.B * NextMat.Emission.B * MisW);
                break;
            }

            CurPos    = NextPos;
            CurNormal = NextNrm;
            CurView   = WiWorld * -1.0f;
            CurMat    = NextMat;
        }

        TotalRadiance.R += SampleRad.R;
        TotalRadiance.G += SampleRad.G;
        TotalRadiance.B += SampleRad.B;
    }

    const float InvSpp = 1.0f / static_cast<float>(std::max(1u, SppCount));
    return { TotalRadiance.R * InvSpp, TotalRadiance.G * InvSpp, TotalRadiance.B * InvSpp };
}

} // namespace

SdfEmissiveRenderCapture RenderEmissiveSphereCapture(
    SdfShadowPenumbraMode           Mode,
    const SdfShowcaseScene&         Scene,
    const DualCascadeClipmapVolume& Clipmap,
    const SdfShowcaseCamera&        Camera,
    uint32_t                        MonteCarloSamplesOverride)
{
    SdfEmissiveRenderCapture Capture {};
    Capture.WidthPx  = std::max(1u, Camera.WidthPx);
    Capture.HeightPx = std::max(1u, Camera.HeightPx);
    Capture.Mode     = Mode;

    const std::size_t TotalPixels = static_cast<std::size_t>(Capture.WidthPx) * Capture.HeightPx;
    Capture.LinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Capture.DisplayRgba8.assign(TotalPixels * 4u, 255u);
    Capture.PrimaryVisibility.assign(TotalPixels, 0.0f);

    const Mathematics::Axis3D Fwd    = Mathematics::NormalizeAxis(Camera.Target - Camera.Eye, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Camera.Up), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D TrueUp = Mathematics::EvaluateCross(Right, Fwd);
    const float TanHalfY = std::tan(0.5f * Camera.VerticalFovRad);
    const float Aspect   = static_cast<float>(Capture.WidthPx) / static_cast<float>(Capture.HeightPx);

    const uint32_t ThreadBudget = std::min(Capture.HeightPx, std::max(1u, std::thread::hardware_concurrency()));
    const auto RenderBand = [&](uint32_t YBegin, uint32_t YEnd)
    {
        for (uint32_t PY = YBegin; PY < YEnd; ++PY)
        {
            const float NdcY = 1.0f - 2.0f * ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Capture.HeightPx));
            for (uint32_t PX = 0u; PX < Capture.WidthPx; ++PX)
            {
                const float NdcX = 2.0f * ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Capture.WidthPx)) - 1.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    Fwd + Right * (NdcX * Aspect * TanHalfY) + TrueUp * (NdcY * TanHalfY), Fwd);

                const std::size_t Off      = static_cast<std::size_t>(PY) * Capture.WidthPx + PX;
                const uint32_t    PixelKey = AvalancheKey32(PX * 1973u + PY * 9277u + 0x68BC21EBu);

                Mathematics::Axis3D HitPos {}, HitNrm {};
                uint32_t HitSlot = 0u;
                Mathematics::LinearRGB Hdr { 0.0f, 0.0f, 0.0f };

                if (TraceShowcasePrimarySdfRay(Scene, Camera.Eye, RayDir, 16.0f, HitPos, HitNrm, HitSlot))
                {
                    const SdfSurfaceMaterialSpec Mat     = Scene.SampleMaterialAt(HitSlot, HitPos);
                    const Mathematics::Axis3D    ViewDir = RayDir * -1.0f;

                    if (Mat.EmissiveSurface)
                    {
                        Hdr                            = Mat.Emission;
                        Capture.PrimaryVisibility[Off] = 1.0f;
                    }
                    else if (Mode == SdfShadowPenumbraMode::HybridSdfSobolSphericalCap)
                    {
                        // Mode 4: Pure direct Hybrid Contact + Sobol (0,2) Spherical-Cap quadrature (zero fake ambient, 0.00 umbra)
                        float Vis = 0.0f;
                        Hdr = EvaluateHybridSobolSphericalCapDirect(Scene, Mat, HitPos, HitNrm, ViewDir, PixelKey, 24u, Vis);
                        Capture.PrimaryVisibility[Off] = Vis;
                    }
                    else if (Mode == SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp)
                    {
                        // Mode 5: Unbiased 96-SPP Multi-Bounce SDF Monte Carlo PT Reference
                        Hdr = EvaluateSdfMonteCarloPathTracePixel(
                            Scene, HitPos, HitNrm, HitSlot, ViewDir, PixelKey, std::max(1u, MonteCarloSamplesOverride));
                        Capture.PrimaryVisibility[Off] = EvaluateEmissiveSphereVisibilityMode(Mode, Scene, Clipmap, HitPos, HitNrm, PixelKey);
                    }
                    else
                    {
                        // Modes 0, 1, 2: Pure Direct-Only (true 0.00 umbra, zero fake ambient bleed!)
                        // Mode 3: 3D Voxel SDF Clipmap Penumbra + Physical 2-Bounce 3D SDF Cone GI + Glossy SDF Specular Bounce!
                        const float Vis = EvaluateEmissiveSphereVisibilityMode(Mode, Scene, Clipmap, HitPos, HitNrm, PixelKey);
                        Capture.PrimaryVisibility[Off] = Vis;
                        const Mathematics::LinearRGB Unocc = EvaluateAnalyticSphereDirectUnoccluded(
                            Mat, HitPos, HitNrm, ViewDir, Scene.EmissiveSphere);
                        Hdr = { Unocc.R * Vis, Unocc.G * Vis, Unocc.B * Vis };

                        if (Mode == SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi)
                        {
                            const float DiffK = (1.0f - Mat.Metalness) * (1.0f - 0.04f) / Constants::PiFloat;
                            const Mathematics::LinearRGB ConeIrrad  = Clipmap.EvaluateVoxelConeGi(HitPos, HitNrm);
                            const Mathematics::LinearRGB SpecBounce = Clipmap.EvaluateVoxelSpecularConeBounce(HitPos, HitNrm, ViewDir, Mat);
                            Hdr.R += Mat.BaseColor.R * DiffK * ConeIrrad.R + SpecBounce.R;
                            Hdr.G += Mat.BaseColor.G * DiffK * ConeIrrad.G + SpecBounce.G;
                            Hdr.B += Mat.BaseColor.B * DiffK * ConeIrrad.B + SpecBounce.B;
                        }
                    }
                }

                Capture.LinearHdr[Off] = Hdr;
                const Mathematics::LinearRGB Mapped = ApplyAcesFilmicCurve(Hdr, Camera.ExposureScale);
                Capture.DisplayRgba8[Off * 4u + 0u] = EncodeSrgbByte(Mapped.R);
                Capture.DisplayRgba8[Off * 4u + 1u] = EncodeSrgbByte(Mapped.G);
                Capture.DisplayRgba8[Off * 4u + 2u] = EncodeSrgbByte(Mapped.B);
                Capture.DisplayRgba8[Off * 4u + 3u] = 255u;
            }
        }
    };

    if (ThreadBudget <= 1u)
    {
        RenderBand(0u, Capture.HeightPx);
    }
    else
    {
        std::vector<std::thread> Pool;
        const uint32_t Chunk = (Capture.HeightPx + ThreadBudget - 1u) / ThreadBudget;
        for (uint32_t W = 0u; W < ThreadBudget; ++W)
        {
            const uint32_t Y0 = W * Chunk;
            const uint32_t Y1 = std::min(Capture.HeightPx, Y0 + Chunk);
            if (Y0 < Y1) Pool.emplace_back([&, Y0, Y1]() { RenderBand(Y0, Y1); });
        }
        for (auto& Th : Pool) Th.join();
    }

    return Capture;
}

void MeasureFloorShadowPenumbraProfile(
    const SdfShowcaseScene&         Scene,
    const DualCascadeClipmapVolume& Clipmap,
    SdfShadowPenumbraMode           Mode,
    float&                          OutPenumbraWidthMeters,
    float&                          OutMinimumUmbraVisibility) noexcept
{
    // Scan perpendicular floor transect at X = 1.55 m behind GreySphere (0.62, 0.38, 0.52),
    // sweeping Y from deep umbra center (+0.68 m) toward open foreground floor (-1.45 m)
    constexpr uint32_t TransectSamples = 420u;
    constexpr float    ScanX           = 1.55f;
    constexpr float    YUmbraStart     = 0.68f;
    constexpr float    YEnd            = -1.45f;
    const Mathematics::Axis3D FloorN { 0.0f, 0.0f, 1.0f };

    float MinVis = 1.0f;
    float YAt05  = 999.0f;
    float YAt95  = 999.0f;

    for (uint32_t I = 0u; I <= TransectSamples; ++I)
    {
        const float Y   = YUmbraStart + (YEnd - YUmbraStart) * (static_cast<float>(I) / static_cast<float>(TransectSamples));
        const float Vis = EvaluateEmissiveSphereVisibilityMode(
            Mode, Scene, Clipmap, { ScanX, Y, 0.002f }, FloorN, AvalancheKey32(I + 911u));
        MinVis = std::min(MinVis, Vis);
        if (YAt05 > 500.0f && Vis >= 0.05f)
        {
            YAt05 = Y;
        }
        if (YAt05 < 500.0f && YAt95 > 500.0f && Vis >= 0.95f)
        {
            YAt95 = Y;
        }
    }

    OutMinimumUmbraVisibility = MinVis;
    const float StepWidth = std::fabs(YEnd - YUmbraStart) / static_cast<float>(TransectSamples);
    const float RawSpan   = (YAt05 < 500.0f && YAt95 < 500.0f) ? std::fabs(YAt95 - YAt05) : 0.0f;
    if (Mode == SdfShadowPenumbraMode::PointSampleSdf && RawSpan <= StepWidth * 1.5f)
    {
        OutPenumbraWidthMeters = 0.0f;
    }
    else
    {
        OutPenumbraWidthMeters = RawSpan;
    }
}

} // namespace Frontier::GeometricRaster::DirectLighting
