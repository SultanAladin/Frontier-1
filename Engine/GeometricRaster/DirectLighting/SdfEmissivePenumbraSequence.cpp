/*============================================================================================================================================
                                                    SDFEMISSIVEPENUMBRASEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Unified 6-Mode SDF Shadow, 3D Voxel SDF Clipmap Penumbra, Voxel Cone GI, Sobol Spherical-Cap, & 96-SPP SDF Path-Tracing Sequence

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
    const float HalfH = 0.5f * HeightZ;
    const float DX    = P.X - BaseCenter.X;
    const float DY    = P.Y - BaseCenter.Y;
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
    const float B = Mathematics::EvaluateDot(OC, UnitDir);
    const float C = Mathematics::EvaluateDot(OC, OC) - SphereRadius * SphereRadius;
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

// Dupuy & Benyoub (2023) Spherical-Cap GGX VNDF reflection direction sampler in tangent frame
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
    float R = 0.0f;
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

    const float DiffScale = (1.0f - Mat.Metalness);
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
    const auto Map = [Exposure](float C) noexcept -> float
    {
        const float X = std::max(0.0f, C * Exposure);
        return std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Map(Hdr.R), Map(Hdr.G), Map(Hdr.B) };
}

[[nodiscard]] uint8_t EncodeSrgbByte(float LinearC) noexcept
{
    const float C = std::clamp(LinearC, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(S * 255.0f), 0.0f, 255.0f));
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
        M.BaseColor = Even ? Mathematics::LinearRGB { 0.75f, 0.73f, 0.68f }
                           : Mathematics::LinearRGB { 0.49f, 0.49f, 0.49f };
        M.Roughness = Even ? 0.56f : 0.66f;
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
    OutScene.Materials.resize(8u);

    // 0: Checkerboard floor
    OutScene.Materials[0].CheckerFloor  = true;
    OutScene.Materials[0].CheckerScaleM = 0.58f;
    OutScene.Materials[0].Roughness     = 0.60f;

    // 1: Neutral warm back wall
    OutScene.Materials[1].BaseColor = { 0.72f, 0.70f, 0.65f };
    OutScene.Materials[1].Roughness = 0.80f;

    // 2: Terracotta crimson left wall
    OutScene.Materials[2].BaseColor = { 0.78f, 0.22f, 0.10f };
    OutScene.Materials[2].Roughness = 0.80f;

    // 3: Forest green right wall
    OutScene.Materials[3].BaseColor = { 0.14f, 0.46f, 0.24f };
    OutScene.Materials[3].Roughness = 0.80f;

    // 4: Dark neutral ceiling
    OutScene.Materials[4].BaseColor = { 0.44f, 0.44f, 0.44f };
    OutScene.Materials[4].Roughness = 0.85f;

    // 5: Warm stone cylinder + pedestal
    OutScene.Materials[5].BaseColor = { 0.74f, 0.71f, 0.66f };
    OutScene.Materials[5].Roughness = 0.56f;

    // 6: Warm bronze torus
    OutScene.Materials[6].BaseColor = { 0.64f, 0.43f, 0.22f };
    OutScene.Materials[6].Roughness = 0.30f;
    OutScene.Materials[6].Metalness = 0.25f;

    // 7: Emissive sphere luminaire
    OutScene.Materials[7].BaseColor  = { 1.0f, 0.98f, 0.94f };
    OutScene.Materials[7].Emission   = { 19.5f, 17.2f, 13.8f };
    OutScene.Materials[7].IsEmissive = true;

    // Matte grey sphere slot
    SdfSurfaceMaterialSpec MatteSphere {};
    MatteSphere.BaseColor = { 0.65f, 0.65f, 0.67f };
    MatteSphere.Roughness = 0.52f;
    OutScene.Materials.push_back(MatteSphere); // Slot 8

    // Cornell Room Slabs (CastsShadow = false so receivers never self-occlude; full scene SDF & Voxel GI bounce include them!)
    OutScene.Boxes.push_back({ {  0.00f,  0.20f, -0.10f }, { 2.80f, 2.65f, 0.10f }, 0u, false }); // Floor Z = 0
    OutScene.Boxes.push_back({ {  0.00f,  2.72f,  1.60f }, { 2.80f, 0.10f, 1.70f }, 1u, false }); // Back wall
    OutScene.Boxes.push_back({ { -2.74f,  0.20f,  1.60f }, { 0.10f, 2.65f, 1.70f }, 2u, false }); // Crimson left wall
    OutScene.Boxes.push_back({ {  2.74f,  0.20f,  1.60f }, { 0.10f, 2.65f, 1.70f }, 3u, false }); // Green right wall
    OutScene.Boxes.push_back({ {  0.00f,  0.20f,  3.26f }, { 2.80f, 2.65f, 0.10f }, 4u, false }); // Ceiling

    // Shadow-casting interior SDF objects matching PROOF 11 layout
    OutScene.Cylinders.push_back({ { -0.50f,  0.36f, 0.0f }, 1.68f, 0.155f, 5u, true });          // Vertical column
    OutScene.Boxes.push_back({     { -0.12f, -0.38f, 0.36f }, { 0.19f, 0.19f, 0.36f }, 5u, true }); // Pedestal box
    OutScene.Tori.push_back({      { -0.12f, -0.38f, 0.90f }, 0.29f, 0.105f, 0.0f, 6u, true });     // Bronze torus
    OutScene.Spheres.push_back({   {  0.52f,  0.56f, 0.52f }, 0.52f, 8u, true });                   // Grey sphere

    // Sole Light Source: Emissive Sphere ONLY (NO sun, NO area quad, NO spot, NO IES!)
    OutScene.EmissiveActive              = true;
    OutScene.EmissiveSphere.Center       = { -1.38f, 0.48f, 1.02f };
    OutScene.EmissiveSphere.Radius       = 0.40f;
    OutScene.EmissiveSphere.Radiance     = { 19.5f, 17.2f, 13.8f };
    OutScene.EmissiveSphere.MaterialSlot = 7u;
}

//------------------------------------------------------------------------------------------------------------------------
//                           2-CASCADE 3D VOXEL SDF CLIPMAP + MULTI-BOUNCE RADIOSITY CACHE
//------------------------------------------------------------------------------------------------------------------------

std::size_t VoxelSdfClipmapCascade::QueryAllocatedBytes() const noexcept
{
    return DistanceGrid.size() * sizeof(float) +
           RadiosityGrid.size() * sizeof(Mathematics::LinearRGB) +
           OpacityGrid.size() * sizeof(float);
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

    // Smooth C1 Hermite weights eliminate piecewise-linear voxel Mach bands in penumbrae
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
    const float Val = (C00 * (1.0f - TY) + C10 * TY) * (1.0f - TZ) + (C01 * (1.0f - TY) + C11 * TY) * TZ;
    return Val + BoxDist;
}

Mathematics::LinearRGB VoxelSdfClipmapCascade::SampleRadiosityTrilinear(
    Mathematics::Axis3D WorldPos,
    float&              OutOpacity) const noexcept
{
    OutOpacity = 0.0f;
    if (RadiosityGrid.empty() || OpacityGrid.empty() || DimX < 2u || DimY < 2u || DimZ < 2u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    if (WorldPos.X < BoundsMin.X || WorldPos.X > BoundsMax.X ||
        WorldPos.Y < BoundsMin.Y || WorldPos.Y > BoundsMax.Y ||
        WorldPos.Z < BoundsMin.Z || WorldPos.Z > BoundsMax.Z)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float FX = std::clamp((WorldPos.X - BoundsMin.X) / VoxelSpacing.X, 0.0f, static_cast<float>(DimX - 1u));
    const float FY = std::clamp((WorldPos.Y - BoundsMin.Y) / VoxelSpacing.Y, 0.0f, static_cast<float>(DimY - 1u));
    const float FZ = std::clamp((WorldPos.Z - BoundsMin.Z) / VoxelSpacing.Z, 0.0f, static_cast<float>(DimZ - 1u));

    const uint32_t IX0 = std::min(DimX - 2u, static_cast<uint32_t>(FX));
    const uint32_t IY0 = std::min(DimY - 2u, static_cast<uint32_t>(FY));
    const uint32_t IZ0 = std::min(DimZ - 2u, static_cast<uint32_t>(FZ));
    const float    TX  = FX - static_cast<float>(IX0);
    const float    TY  = FY - static_cast<float>(IY0);
    const float    TZ  = FZ - static_cast<float>(IZ0);

    Mathematics::LinearRGB AccumR { 0.0f, 0.0f, 0.0f };
    float AccumO = 0.0f;
    for (uint32_t DZ = 0u; DZ < 2u; ++DZ)
    {
        const float WZ = (DZ == 0u) ? (1.0f - TZ) : TZ;
        for (uint32_t DY = 0u; DY < 2u; ++DY)
        {
            const float WYZ = WZ * ((DY == 0u) ? (1.0f - TY) : TY);
            for (uint32_t DX = 0u; DX < 2u; ++DX)
            {
                const float W = WYZ * ((DX == 0u) ? (1.0f - TX) : TX);
                const std::size_t Idx = (static_cast<std::size_t>(IZ0 + DZ) * DimY + (IY0 + DY)) * DimX + (IX0 + DX);
                AccumR.R += RadiosityGrid[Idx].R * W;
                AccumR.G += RadiosityGrid[Idx].G * W;
                AccumR.B += RadiosityGrid[Idx].B * W;
                AccumO   += OpacityGrid[Idx] * W;
            }
        }
    }
    OutOpacity = AccumO;
    return AccumR;
}

float SdfVoxelClipmapHierarchy::SampleClipmapDistance(Mathematics::Axis3D WorldPos) const noexcept
{
    if (FineCascade.DistanceGrid.empty())
    {
        return CoarseCascade.SampleDistanceHermite(WorldPos);
    }

    const Mathematics::Axis3D FineCtr  = (FineCascade.BoundsMin + FineCascade.BoundsMax) * 0.5f;
    const Mathematics::Axis3D FineHalf = (FineCascade.BoundsMax - FineCascade.BoundsMin) * 0.5f;
    const float NX = std::fabs(WorldPos.X - FineCtr.X) / std::max(1.0e-3f, FineHalf.X);
    const float NY = std::fabs(WorldPos.Y - FineCtr.Y) / std::max(1.0e-3f, FineHalf.Y);
    const float NZ = std::fabs(WorldPos.Z - FineCtr.Z) / std::max(1.0e-3f, FineHalf.Z);
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

void SdfVoxelClipmapHierarchy::BuildFromShowcaseScene(const SdfShowcaseScene& Scene)
{
    const auto PopulateCascade = [&](VoxelSdfClipmapCascade& C,
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
        const std::size_t Total = static_cast<std::size_t>(DX) * DY * DZ;
        C.DistanceGrid.assign(Total, 20.0f);
        C.RadiosityGrid.assign(Total, { 0.0f, 0.0f, 0.0f });
        C.OpacityGrid.assign(Total, 0.0f);

        const float VoxelHalfDiag = 0.75f * std::max({ C.VoxelSpacing.X, C.VoxelSpacing.Y, C.VoxelSpacing.Z });
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

                        // 1. Interior shadow-casting SDF distance
                        C.DistanceGrid[Idx] = Scene.EvaluateOccluderSignedDistance(P);

                        // 2. Full-scene surface shell radiosity injection (1st bounce from EmissiveSphere)
                        uint32_t MatSlot = 0u;
                        const float FullD = Scene.EvaluateFullSceneSignedDistance(P, MatSlot);
                        if (std::fabs(FullD) <= VoxelHalfDiag)
                        {
                            const SdfSurfaceMaterialSpec Mat = Scene.SampleMaterialAt(MatSlot, P);
                            const float ShellW = 1.0f - std::clamp(std::fabs(FullD) / VoxelHalfDiag, 0.0f, 1.0f);
                            C.OpacityGrid[Idx] = Mat.IsEmissive ? 0.0f : ShellW;
                            if (!Mat.IsEmissive && Scene.EmissiveActive)
                            {
                                const Mathematics::Axis3D N = Scene.EvaluateFullSceneGradientNormal(P);
                                const Mathematics::Axis3D ToS = Scene.EmissiveSphere.Center - P;
                                const float Dist = Mathematics::EvaluateLength(ToS);
                                if (Dist > Scene.EmissiveSphere.Radius + 0.02f)
                                {
                                    const Mathematics::Axis3D L = ToS * (1.0f / Dist);
                                    const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(N, L));
                                    if (NdotL > 0.0f)
                                    {
                                        // Quick 20-step SDF shadow check to seed direct voxel radiosity
                                        float T = 0.06f;
                                        const float EndT = Dist - Scene.EmissiveSphere.Radius - 0.03f;
                                        float Vis = 1.0f;
                                        for (uint32_t S = 0u; S < 20u && T < EndT; ++S)
                                        {
                                            const float OD = Scene.EvaluateOccluderSignedDistance(P + N * 0.03f + L * T);
                                            if (OD < -0.01f) { Vis = 0.0f; break; }
                                            const float ConeR = std::max(0.02f, T * (Scene.EmissiveSphere.Radius / Dist));
                                            Vis = std::min(Vis, SmoothHermite01(-0.5f, 0.85f, OD / ConeR));
                                            T  += std::clamp(std::fabs(OD) * 0.65f, 0.05f, 0.28f);
                                        }
                                        const float SinMax = std::clamp(Scene.EmissiveSphere.Radius / Dist, 0.0f, 0.99f);
                                        const float FormF  = SinMax * SinMax * NdotL * Vis;
                                        const float DiffK  = (1.0f - Mat.Metalness) * 0.96f;
                                        // Premultiplied Lambertian surface radiance (L_wall * ShellW) paired with OpacityGrid = ShellW
                                        C.RadiosityGrid[Idx] = {
                                            Scene.EmissiveSphere.Radiance.R * Mat.BaseColor.R * DiffK * FormF * ShellW,
                                            Scene.EmissiveSphere.Radiance.G * Mat.BaseColor.G * DiffK * FormF * ShellW,
                                            Scene.EmissiveSphere.Radiance.B * Mat.BaseColor.B * DiffK * FormF * ShellW,
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
    };

    const auto SmoothCascadeRadiosity3D = [](VoxelSdfClipmapCascade& C)
    {
        if (C.RadiosityGrid.empty() || C.DimX < 3u || C.DimY < 3u || C.DimZ < 3u)
        {
            return;
        }
        std::vector<Mathematics::LinearRGB> NextRad(C.RadiosityGrid.size(), { 0.0f, 0.0f, 0.0f });
        std::vector<float>                  NextOp(C.OpacityGrid.size(), 0.0f);
        static constexpr float Kernel1D[3] = { 0.25f, 0.50f, 0.25f };

        for (uint32_t IZ = 0u; IZ < C.DimZ; ++IZ)
        {
            for (uint32_t IY = 0u; IY < C.DimY; ++IY)
            {
                for (uint32_t IX = 0u; IX < C.DimX; ++IX)
                {
                    Mathematics::LinearRGB SumR { 0.0f, 0.0f, 0.0f };
                    float SumO = 0.0f;
                    for (int KZ = -1; KZ <= 1; ++KZ)
                    {
                        const uint32_t SZ = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IZ) + KZ, 0, static_cast<int>(C.DimZ) - 1));
                        const float WZ = Kernel1D[KZ + 1];
                        for (int KY = -1; KY <= 1; ++KY)
                        {
                            const uint32_t SY = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IY) + KY, 0, static_cast<int>(C.DimY) - 1));
                            const float WYZ = WZ * Kernel1D[KY + 1];
                            for (int KX = -1; KX <= 1; ++KX)
                            {
                                const uint32_t SX = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(IX) + KX, 0, static_cast<int>(C.DimX) - 1));
                                const float W = WYZ * Kernel1D[KX + 1];
                                const std::size_t SIdx = (static_cast<std::size_t>(SZ) * C.DimY + SY) * C.DimX + SX;
                                SumR.R += C.RadiosityGrid[SIdx].R * W;
                                SumR.G += C.RadiosityGrid[SIdx].G * W;
                                SumR.B += C.RadiosityGrid[SIdx].B * W;
                                SumO   += C.OpacityGrid[SIdx] * W;
                            }
                        }
                    }
                    const std::size_t DIdx = (static_cast<std::size_t>(IZ) * C.DimY + IY) * C.DimX + IX;
                    NextRad[DIdx] = SumR;
                    NextOp[DIdx]  = SumO;
                }
            }
        }
        C.RadiosityGrid = std::move(NextRad);
        C.OpacityGrid   = std::move(NextOp);
    };

    // Cascade 0 (Fine 56x56x40): tight 6.5 cm interior pitch around cylinder, pedestal, torus, and sphere
    PopulateCascade(FineCascade,   56u, 56u, 40u, { -1.95f, -1.55f, -0.15f }, { 1.85f, 2.25f, 2.45f });
    // Cascade 1 (Coarse 40x40x32): full Cornell room bounds capturing red/green walls, floor, & ceiling bounce
    PopulateCascade(CoarseCascade, 40u, 40u, 32u, { -2.95f, -2.55f, -0.25f }, { 2.95f, 2.95f, 3.40f });

    // Second-bounce radiosity propagation in CoarseCascade using 14 isotropic + diagonal directions (zero axis-aligned laser banding!)
    std::vector<Mathematics::LinearRGB> BounceCopy = CoarseCascade.RadiosityGrid;
    static constexpr std::array<Mathematics::Axis3D, 14> GatherDirs {{
        {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        {  0.57735f,  0.57735f,  0.57735f }, { -0.57735f,  0.57735f,  0.57735f },
        {  0.57735f, -0.57735f,  0.57735f }, { -0.57735f, -0.57735f,  0.57735f },
        {  0.57735f,  0.57735f, -0.57735f }, { -0.57735f,  0.57735f, -0.57735f },
        {  0.57735f, -0.57735f, -0.57735f }, { -0.57735f, -0.57735f, -0.57735f },
    }};
    for (uint32_t IZ = 0u; IZ < CoarseCascade.DimZ; ++IZ)
    {
        for (uint32_t IY = 0u; IY < CoarseCascade.DimY; ++IY)
        {
            for (uint32_t IX = 0u; IX < CoarseCascade.DimX; ++IX)
            {
                const std::size_t Idx = (static_cast<std::size_t>(IZ) * CoarseCascade.DimY + IY) * CoarseCascade.DimX + IX;
                if (CoarseCascade.OpacityGrid[Idx] <= 0.05f) continue;
                const Mathematics::Axis3D P {
                    CoarseCascade.BoundsMin.X + static_cast<float>(IX) * CoarseCascade.VoxelSpacing.X,
                    CoarseCascade.BoundsMin.Y + static_cast<float>(IY) * CoarseCascade.VoxelSpacing.Y,
                    CoarseCascade.BoundsMin.Z + static_cast<float>(IZ) * CoarseCascade.VoxelSpacing.Z,
                };
                uint32_t MatSlot = 0u;
                (void)Scene.EvaluateFullSceneSignedDistance(P, MatSlot);
                const SdfSurfaceMaterialSpec Mat = Scene.SampleMaterialAt(MatSlot, P);
                if (Mat.IsEmissive) continue;
                const Mathematics::Axis3D SurfN = Scene.EvaluateFullSceneGradientNormal(P);

                Mathematics::LinearRGB Gather { 0.0f, 0.0f, 0.0f };
                float WeightTotal = 0.0f;
                for (const auto& Dir : GatherDirs)
                {
                    const float CosW = Mathematics::EvaluateDot(SurfN, Dir);
                    if (CosW <= 0.05f) continue;
                    WeightTotal += CosW;
                    for (uint32_t Step = 2u; Step <= 13u; ++Step)
                    {
                        float Op = 0.0f;
                        const auto PremulRad = CoarseCascade.SampleRadiosityTrilinear(P + Dir * (0.42f * static_cast<float>(Step)), Op);
                        if (Op > 0.08f)
                        {
                            const float InvOp = 1.0f / Op;
                            Gather.R += PremulRad.R * InvOp * CosW;
                            Gather.G += PremulRad.G * InvOp * CosW;
                            Gather.B += PremulRad.B * InvOp * CosW;
                            break;
                        }
                    }
                }
                if (WeightTotal > 1.0e-4f)
                {
                    const float ShellW = CoarseCascade.OpacityGrid[Idx];
                    const float Norm   = (0.88f / WeightTotal) * ShellW;
                    BounceCopy[Idx].R += Gather.R * Mat.BaseColor.R * Norm;
                    BounceCopy[Idx].G += Gather.G * Mat.BaseColor.G * Norm;
                    BounceCopy[Idx].B += Gather.B * Mat.BaseColor.B * Norm;
                }
            }
        }
    }
    CoarseCascade.RadiosityGrid = std::move(BounceCopy);

    // Single isotropic 3x3x3 binomial prefilter smooths voxel radiosity without bloating interior shells
    SmoothCascadeRadiosity3D(FineCascade);
    SmoothCascadeRadiosity3D(CoarseCascade);
}

void SdfVoxelClipmapHierarchy::BuildFromHybridEnvironment(
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
        const std::size_t Total = static_cast<std::size_t>(DX) * DY * DZ;
        C.DistanceGrid.assign(Total, 20.0f);
        C.RadiosityGrid.clear();
        C.OpacityGrid.clear();

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

Mathematics::LinearRGB SdfVoxelClipmapHierarchy::EvaluateVoxelConeGi(
    Mathematics::Axis3D WorldPos,
    Mathematics::Axis3D ShadingNormal) const noexcept
{
    if (CoarseCascade.RadiosityGrid.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // 16 world-fixed Golden-Spiral unit directions: zero tangent-frame rotation on curved spheres/tori!
    static constexpr std::array<Mathematics::Axis3D, 16> WorldCones {{
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

    const auto SampleNormalizedSurfaceRadiosity = [&](Mathematics::Axis3D HitP, float ApertureRadius) noexcept -> Mathematics::LinearRGB
    {
        const float R = std::clamp(ApertureRadius, 0.14f, 0.72f);
        static constexpr std::array<Mathematics::Axis3D, 7> Offsets {{
            {  0.0f,  0.0f,  0.0f },
            {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
            {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
            {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f },
        }};
        Mathematics::LinearRGB SumPremul { 0.0f, 0.0f, 0.0f };
        float SumOp = 0.0f;
        for (std::size_t J = 0u; J < Offsets.size(); ++J)
        {
            const float TapW = (J == 0u) ? 2.0f : 1.0f;
            float Op = 0.0f;
            const auto PRad = CoarseCascade.SampleRadiosityTrilinear(HitP + Offsets[J] * R, Op);
            SumPremul.R += PRad.R * TapW;
            SumPremul.G += PRad.G * TapW;
            SumPremul.B += PRad.B * TapW;
            SumOp       += Op * TapW;
        }
        if (SumOp <= 1.0e-4f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
        const float InvOp = 1.0f / SumOp;
        return { SumPremul.R * InvOp, SumPremul.G * InvOp, SumPremul.B * InvOp };
    };

    Mathematics::LinearRGB AccumExitance { 0.0f, 0.0f, 0.0f };
    float CosWeightSum = 0.0f;
    const Mathematics::Axis3D Origin = WorldPos + ShadingNormal * 0.05f;

    for (const auto& ConeDir : WorldCones)
    {
        const float CosTheta = Mathematics::EvaluateDot(ShadingNormal, ConeDir);
        if (CosTheta <= 0.02f)
        {
            continue;
        }

        // Exact forward ray-box exit distance to the Cornell enclosure [-2.64..2.64] x [-2.45..2.62] x [0.00..3.16]
        const float InvX  = 1.0f / ((std::fabs(ConeDir.X) > 1.0e-5f) ? ConeDir.X : ((ConeDir.X >= 0.0f) ? 1.0e-5f : -1.0e-5f));
        const float InvY  = 1.0f / ((std::fabs(ConeDir.Y) > 1.0e-5f) ? ConeDir.Y : ((ConeDir.Y >= 0.0f) ? 1.0e-5f : -1.0e-5f));
        const float InvZ  = 1.0f / ((std::fabs(ConeDir.Z) > 1.0e-5f) ? ConeDir.Z : ((ConeDir.Z >= 0.0f) ? 1.0e-5f : -1.0e-5f));
        const float TX    = ((ConeDir.X > 0.0f ?  2.64f : -2.64f) - Origin.X) * InvX;
        const float TY    = ((ConeDir.Y > 0.0f ?  2.62f : -2.45f) - Origin.Y) * InvY;
        const float TZ    = ((ConeDir.Z > 0.0f ?  3.16f :  0.00f) - Origin.Z) * InvZ;
        const float SlabT = std::clamp(std::min({ TX > 0.0f ? TX : 20.0f, TY > 0.0f ? TY : 20.0f, TZ > 0.0f ? TZ : 20.0f }), 0.12f, 8.5f);

        float ConeVis = 1.0f;
        for (uint32_t Step = 1u; Step <= 8u; ++Step)
        {
            const float DistT    = SlabT * (static_cast<float>(Step) / 9.0f);
            const float SelfFade = SmoothHermite01(0.10f, 0.42f, DistT);
            if (SelfFade > 1.0e-3f)
            {
                const float OccD  = SampleClipmapDistance(Origin + ConeDir * DistT);
                const float ConeR = std::max(0.14f, 0.68f * DistT);
                const float StepV = SmoothHermite01(-0.95f, 0.95f, OccD / ConeR);
                ConeVis = std::min(ConeVis, 1.0f - SelfFade * (1.0f - StepV));
            }
        }

        const Mathematics::Axis3D WallHitP = Origin + ConeDir * SlabT;
        const Mathematics::LinearRGB WallExitance = SampleNormalizedSurfaceRadiosity(WallHitP, 0.44f * SlabT);
        const float EffectiveTransfer = 0.34f + 0.66f * ConeVis;
        const float W = CosTheta * EffectiveTransfer;
        AccumExitance.R += WallExitance.R * W;
        AccumExitance.G += WallExitance.G * W;
        AccumExitance.B += WallExitance.B * W;
        CosWeightSum    += CosTheta;
    }

    if (CosWeightSum <= 1.0e-5f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float Scale = Constants::PiFloat / CosWeightSum;
    return { AccumExitance.R * Scale, AccumExitance.G * Scale, AccumExitance.B * Scale };
}

//------------------------------------------------------------------------------------------------------------------------
//                           WORLD DIRECTIONAL SDF SHADOW DISPATCH (MODES 0..5 ON GTXLOWEND)
//------------------------------------------------------------------------------------------------------------------------

float EvaluateWorldDirectionalSdfShadow(
    SdfShadowPenumbraMode            Mode,
    const HybridOccluderEnvironment& Env,
    const SdfVoxelClipmapHierarchy*  OptionalClipmap,
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

    // Mode 1 is Candidate 1 Hybrid Cone Shadow (default GtxLowEnd baseline)
    if (Mode == SdfShadowPenumbraMode::AnalyticSdfConePenumbra)
    {
        HybridConeShadowSetup Setup {};
        Setup.TanSunRadius = TanAngularRadius;
        return EvaluateGtxHybridDirectionalShadow(Env, ShadingPos, ShadingNormal, DirectionToLight, Setup);
    }

    // Mode 0: Point-Sample SDF (binary hard shadow, zero penumbra)
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

    // Modes 2 & 3: 3D Voxel SDF Clipmap Penumbra
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

    // Modes 4 & 5: Hybrid SDF + Owen-Scrambled Sobol Spherical-Cap quadrature (16 vs 96 samples)
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
    const SdfVoxelClipmapHierarchy& Clipmap,
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

    // Aim soft-shadow cone at the horizon-visible upper cap of the spherical luminaire so curved receivers never self-occlude at grazing angles
    const float SinCap  = std::clamp(Scene.EmissiveSphere.Radius / CenterDist, 1.0e-4f, 0.99f);
    const float NdotL0  = Mathematics::EvaluateDot(Normal, L);
    const float Lift    = std::max(0.0f, SinCap * 0.85f - NdotL0);
    const Mathematics::Axis3D ConeDirL = Mathematics::NormalizeAxis(L + Normal * Lift, L);

    // Mode 1: Analytic SDF Cone Penumbra (full [-ConeR, +ConeR] physical penumbra wedge)
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
                const float ConeR = std::max(0.014f, Travel * TanCone * 0.86f);
                const float Ratio = std::clamp(ClipD / ConeR, -1.25f, 1.25f) * PlaneFade + 1.25f * (1.0f - PlaneFade);
                MinRatio = SoftMinimum(MinRatio, Ratio, 0.10f);
                if (MinRatio <= -1.02f) return 0.0f;
            }
            Travel += std::clamp(std::fabs(ClipD) * 0.45f, 0.020f, 0.14f);
        }
        return SmoothHermite01(-1.0f, 1.0f, MinRatio);
    }

    // Modes 4 & 5 visibility scalar audit: Sobol Spherical-Cap solid-angle fraction
    const uint32_t CapSamples = (Mode == SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp) ? 64u : 20u;
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

// Mode 4: Hybrid Contact + Owen-Scrambled Sobol (0,2) Spherical-Cap Direct Integrator
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
    uint32_t Unoccluded = 0u;
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

        // Short-range exact contact check [0.008, 0.28] + far-field SDF sphere march
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
    if (PrimMat.IsEmissive)
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
            // 1. Spherical-Cap Next-Event Estimation toward the Emissive Sphere (MIS with BSDF)
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

            // 2. Sample continuation ray via Dupuy-Benyoub (2023) Spherical-Cap GGX VNDF + Cosine Hemisphere
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

            if (NextMat.IsEmissive)
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

SdfEmissiveRenderFrame RenderEmissiveSphereSdfFrame(
    SdfShadowPenumbraMode           Mode,
    const SdfShowcaseScene&         Scene,
    const SdfVoxelClipmapHierarchy& Clipmap,
    const SdfShowcaseCamera&        Camera,
    uint32_t                        MonteCarloSamplesOverride)
{
    SdfEmissiveRenderFrame Frame {};
    Frame.WidthPx  = std::max(1u, Camera.WidthPx);
    Frame.HeightPx = std::max(1u, Camera.HeightPx);
    Frame.Mode     = Mode;

    const std::size_t TotalPixels = static_cast<std::size_t>(Frame.WidthPx) * Frame.HeightPx;
    Frame.LinearHdr.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Frame.DisplayRgba8.assign(TotalPixels * 4u, 255u);
    Frame.PrimaryVisibility.assign(TotalPixels, 0.0f);

    const Mathematics::Axis3D Fwd    = Mathematics::NormalizeAxis(Camera.Target - Camera.Eye, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Right  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Camera.Up), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D TrueUp = Mathematics::EvaluateCross(Right, Fwd);
    const float TanHalfY = std::tan(0.5f * Camera.VerticalFovRad);
    const float Aspect   = static_cast<float>(Frame.WidthPx) / static_cast<float>(Frame.HeightPx);

    const uint32_t ThreadBudget = std::min(Frame.HeightPx, std::max(1u, std::thread::hardware_concurrency()));
    const auto RenderBand = [&](uint32_t YBegin, uint32_t YEnd)
    {
        for (uint32_t PY = YBegin; PY < YEnd; ++PY)
        {
            const float NdcY = 1.0f - 2.0f * ((static_cast<float>(PY) + 0.5f) / static_cast<float>(Frame.HeightPx));
            for (uint32_t PX = 0u; PX < Frame.WidthPx; ++PX)
            {
                const float NdcX = 2.0f * ((static_cast<float>(PX) + 0.5f) / static_cast<float>(Frame.WidthPx)) - 1.0f;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(
                    Fwd + Right * (NdcX * Aspect * TanHalfY) + TrueUp * (NdcY * TanHalfY), Fwd);

                const std::size_t Off = static_cast<std::size_t>(PY) * Frame.WidthPx + PX;
                const uint32_t PixelKey = AvalancheKey32(PX * 1973u + PY * 9277u + 0x68BC21EBu);

                Mathematics::Axis3D HitPos {}, HitNrm {};
                uint32_t HitSlot = 0u;
                Mathematics::LinearRGB Hdr { 0.002f, 0.002f, 0.003f };

                if (TraceShowcasePrimarySdfRay(Scene, Camera.Eye, RayDir, 16.0f, HitPos, HitNrm, HitSlot))
                {
                    const SdfSurfaceMaterialSpec Mat = Scene.SampleMaterialAt(HitSlot, HitPos);
                    const Mathematics::Axis3D ViewDir = RayDir * -1.0f;

                    if (Mat.IsEmissive)
                    {
                        Hdr = Mat.Emission;
                        Frame.PrimaryVisibility[Off] = 1.0f;
                    }
                    else if (Mode == SdfShadowPenumbraMode::HybridSdfSobolSphericalCap)
                    {
                        float Vis = 0.0f;
                        Hdr = EvaluateHybridSobolSphericalCapDirect(Scene, Mat, HitPos, HitNrm, ViewDir, PixelKey, 24u, Vis);
                        const Mathematics::LinearRGB ConeIrrad = Clipmap.EvaluateVoxelConeGi(HitPos, HitNrm);
                        const float DiffK = (1.0f - Mat.Metalness) * (1.0f - 0.04f) / Constants::PiFloat;
                        Hdr.R += Mat.BaseColor.R * DiffK * ConeIrrad.R;
                        Hdr.G += Mat.BaseColor.G * DiffK * ConeIrrad.G;
                        Hdr.B += Mat.BaseColor.B * DiffK * ConeIrrad.B;
                        Frame.PrimaryVisibility[Off] = Vis;
                    }
                    else if (Mode == SdfShadowPenumbraMode::SdfMonteCarloPtRef96Spp)
                    {
                        Hdr = EvaluateSdfMonteCarloPathTracePixel(
                            Scene, HitPos, HitNrm, HitSlot, ViewDir, PixelKey, std::max(1u, MonteCarloSamplesOverride));
                        Frame.PrimaryVisibility[Off] = EvaluateEmissiveSphereVisibilityMode(Mode, Scene, Clipmap, HitPos, HitNrm, PixelKey);
                    }
                    else
                    {
                        const float Vis = EvaluateEmissiveSphereVisibilityMode(Mode, Scene, Clipmap, HitPos, HitNrm, PixelKey);
                        Frame.PrimaryVisibility[Off] = Vis;
                        const Mathematics::LinearRGB Unocc = EvaluateAnalyticSphereDirectUnoccluded(
                            Mat, HitPos, HitNrm, ViewDir, Scene.EmissiveSphere);
                        Hdr = { Unocc.R * Vis, Unocc.G * Vis, Unocc.B * Vis };

                        const float DiffK = (1.0f - Mat.Metalness) * (1.0f - 0.04f) / Constants::PiFloat;
                        if (Mode == SdfShadowPenumbraMode::VoxelSdfPenumbraPlusConeGi)
                        {
                            const Mathematics::LinearRGB ConeIrrad = Clipmap.EvaluateVoxelConeGi(HitPos, HitNrm);
                            Hdr.R += Mat.BaseColor.R * DiffK * ConeIrrad.R;
                            Hdr.G += Mat.BaseColor.G * DiffK * ConeIrrad.G;
                            Hdr.B += Mat.BaseColor.B * DiffK * ConeIrrad.B;
                        }
                        else if (Scene.EmissiveActive)
                        {
                            // Emissive-sphere-driven 5-step SDF ambient occlusion + Cornell enclosure radiosity balance
                            float SdfAo = 0.0f;
                            for (uint32_t A = 1u; A <= 5u; ++A)
                            {
                                const float StepD = 0.08f * static_cast<float>(A);
                                const float OccD  = std::max(0.0f, Scene.EvaluateOccluderSignedDistance(HitPos + HitNrm * StepD));
                                SdfAo += std::clamp(OccD / StepD, 0.0f, 1.0f);
                            }
                            SdfAo *= 0.20f;
                            const float SpherePower = (4.0f * Constants::PiFloat * Scene.EmissiveSphere.Radius * Scene.EmissiveSphere.Radius) / 215.0f;
                            const float RedBleed    = std::max(0.0f,  HitNrm.X) * 0.36f + 0.24f;
                            const float GreenBleed  = std::max(0.0f, -HitNrm.X) * 0.28f + 0.26f;
                            const Mathematics::LinearRGB EnclosureIrrad {
                                Scene.EmissiveSphere.Radiance.R * SpherePower * (0.58f + 0.52f * RedBleed) * Constants::PiFloat * SdfAo,
                                Scene.EmissiveSphere.Radiance.G * SpherePower * (0.52f + 0.42f * GreenBleed) * Constants::PiFloat * SdfAo,
                                Scene.EmissiveSphere.Radiance.B * SpherePower * 0.54f * Constants::PiFloat * SdfAo,
                            };
                            Hdr.R += Mat.BaseColor.R * DiffK * EnclosureIrrad.R;
                            Hdr.G += Mat.BaseColor.G * DiffK * EnclosureIrrad.G;
                            Hdr.B += Mat.BaseColor.B * DiffK * EnclosureIrrad.B;
                        }
                    }
                }

                Frame.LinearHdr[Off] = Hdr;
                const Mathematics::LinearRGB Mapped = ApplyAcesFilmicCurve(Hdr, Camera.ExposureScale);
                Frame.DisplayRgba8[Off * 4u + 0u] = EncodeSrgbByte(Mapped.R);
                Frame.DisplayRgba8[Off * 4u + 1u] = EncodeSrgbByte(Mapped.G);
                Frame.DisplayRgba8[Off * 4u + 2u] = EncodeSrgbByte(Mapped.B);
                Frame.DisplayRgba8[Off * 4u + 3u] = 255u;
            }
        }
    };

    if (ThreadBudget <= 1u)
    {
        RenderBand(0u, Frame.HeightPx);
    }
    else
    {
        std::vector<std::thread> Pool;
        const uint32_t Chunk = (Frame.HeightPx + ThreadBudget - 1u) / ThreadBudget;
        for (uint32_t W = 0u; W < ThreadBudget; ++W)
        {
            const uint32_t Y0 = W * Chunk;
            const uint32_t Y1 = std::min(Frame.HeightPx, Y0 + Chunk);
            if (Y0 < Y1) Pool.emplace_back([&, Y0, Y1]() { RenderBand(Y0, Y1); });
        }
        for (auto& Th : Pool) Th.join();
    }

    return Frame;
}

void MeasureFloorShadowPenumbraProfile(
    const SdfShowcaseScene&         Scene,
    const SdfVoxelClipmapHierarchy& Clipmap,
    SdfShadowPenumbraMode           Mode,
    float&                          OutPenumbraWidthMeters,
    float&                          OutMinimumUmbraVisibility) noexcept
{
    // Scan perpendicular floor transect at X = 2.25 m behind GreySphere (0.52, 0.56, 0.52),
    // sweeping Y from umbra center (+0.64 m) out into full unimpeded floor illumination (+2.45 m):
    constexpr uint32_t TransectSamples = 360u;
    constexpr float    ScanX           = 2.25f;
    constexpr float    YUmbraStart     = 0.64f;
    constexpr float    YEnd            = 2.45f;
    const Mathematics::Axis3D FloorN { 0.0f, 0.0f, 1.0f };

    float MinVis = 1.0f;
    float YAt05  = -99.0f;
    float YAt95  = -99.0f;

    for (uint32_t I = 0u; I <= TransectSamples; ++I)
    {
        const float Y   = YUmbraStart + (YEnd - YUmbraStart) * (static_cast<float>(I) / static_cast<float>(TransectSamples));
        const float Vis = EvaluateEmissiveSphereVisibilityMode(
            Mode, Scene, Clipmap, { ScanX, Y, 0.002f }, FloorN, AvalancheKey32(I + 911u));
        MinVis = std::min(MinVis, Vis);
        if (YAt05 < -50.0f && Vis >= 0.05f)
        {
            YAt05 = Y;
        }
        if (YAt05 > -50.0f && YAt95 < -50.0f && Vis >= 0.95f)
        {
            YAt95 = Y;
        }
    }

    OutMinimumUmbraVisibility = MinVis;
    const float StepWidth = (YEnd - YUmbraStart) / static_cast<float>(TransectSamples);
    const float RawSpan   = (YAt05 > -50.0f && YAt95 >= YAt05) ? (YAt95 - YAt05) : 0.0f;
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
