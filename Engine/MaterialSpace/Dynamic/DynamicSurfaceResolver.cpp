/*============================================================================================================================================
                                                          DYNAMICSURFACERESOLVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of calibrated CPU reference resolver with exact parity against Slang GPU procedural field & seam kernels

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/MaterialSpace/Dynamic/DynamicSurfaceResolver.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <cmath>

namespace Frontier::MaterialSpace::Dynamic
{

namespace
{

[[nodiscard]] float FractScalar(float X) noexcept
{
    return X - std::floor(X);
}

[[nodiscard]] float SmoothstepScalar(float Edge0, float Edge1, float X) noexcept
{
    if (std::fabs(Edge1 - Edge0) < 1.0e-6f)
    {
        return (X < Edge0) ? 0.0f : 1.0f;
    }
    const float T = std::clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] float LerpScalar(float A, float B, float T) noexcept
{
    return A + (B - A) * T;
}

[[nodiscard]] Mathematics::LinearRGB LerpRgb(Mathematics::LinearRGB A, Mathematics::LinearRGB B, float T) noexcept
{
    return { LerpScalar(A.R, B.R, T), LerpScalar(A.G, B.G, T), LerpScalar(A.B, B.B, T) };
}

[[nodiscard]] float Hash21(float X, float Y) noexcept
{
    const float H = std::sin(X * 127.1f + Y * 311.7f) * 43758.5453123f;
    return FractScalar(H);
}

[[nodiscard]] float Hash31(float X, float Y, float Z) noexcept
{
    const float H = std::sin(X * 127.1f + Y * 311.7f + Z * 74.7f) * 43758.5453123f;
    return FractScalar(H);
}

[[nodiscard]] float LatticeNoise2D(float X, float Y) noexcept
{
    const float IX = std::floor(X);
    const float IY = std::floor(Y);
    const float FX = X - IX;
    const float FY = Y - IY;
    const float UX = FX * FX * (3.0f - 2.0f * FX);
    const float UY = FY * FY * (3.0f - 2.0f * FY);

    const float A = Hash21(IX,        IY);
    const float B = Hash21(IX + 1.0f, IY);
    const float C = Hash21(IX,        IY + 1.0f);
    const float D = Hash21(IX + 1.0f, IY + 1.0f);
    return LerpScalar(LerpScalar(A, B, UX), LerpScalar(C, D, UX), UY);
}

[[nodiscard]] float Fbm2D(float X, float Y, int Octaves = 4) noexcept
{
    float Sum = 0.0f;
    float Amp = 0.5f;
    float PX  = X;
    float PY  = Y;
    for (int I = 0; I < Octaves; ++I)
    {
        Sum += Amp * LatticeNoise2D(PX, PY);
        const float NX = 1.62f * PX - 1.21f * PY + 11.3f;
        const float NY = 1.21f * PX + 1.62f * PY + 7.1f;
        PX  = NX;
        PY  = NY;
        Amp *= 0.5f;
    }
    return Sum;
}

struct VoronoiEdgeSample
{
    float EdgeDistance = 1.0f;
    float CellId       = 0.0f;
};

[[nodiscard]] VoronoiEdgeSample EvaluateVoronoiEdge(float X, float Y) noexcept
{
    const float IX = std::floor(X);
    const float IY = std::floor(Y);
    const float FX = X - IX;
    const float FY = Y - IY;

    float BestDist   = 8.0f;
    float SecondDist = 8.0f;
    float BestId     = 0.0f;

    for (int DY = -1; DY <= 1; ++DY)
    {
        for (int DX = -1; DX <= 1; ++DX)
        {
            const float GX = IX + static_cast<float>(DX);
            const float GY = IY + static_cast<float>(DY);
            const float OX = 0.14f + 0.72f * Hash21(GX, GY);
            const float OY = 0.14f + 0.72f * Hash21(GX + 19.7f, GY + 41.3f);
            const float RX = static_cast<float>(DX) + OX - FX;
            const float RY = static_cast<float>(DY) + OY - FY;
            const float Dist = std::sqrt(RX * RX + RY * RY);
            if (Dist < BestDist)
            {
                SecondDist = BestDist;
                BestDist   = Dist;
                BestId     = Hash21(GX + 7.3f, GY + 13.1f);
            }
            else if (Dist < SecondDist)
            {
                SecondDist = Dist;
            }
        }
    }

    return { std::max(0.0f, SecondDist - BestDist), BestId };
}

struct HexCellSample
{
    float EdgeDistance = 0.5f;
    float CellHash     = 0.5f;
};

// Exact Particle Studio / HTML Energy Shield hex grid math
[[nodiscard]] HexCellSample EvaluateHexLattice(float PX, float PY) noexcept
{
    const float SX = 1.0f;
    const float SY = 1.7320508f;
    const float HX = 0.5f;
    const float HY = 0.8660254f;

    const auto ModWrap = [](float V, float Period) noexcept -> float
    {
        return V - Period * std::floor(V / Period);
    };

    const float AX = ModWrap(PX, SX) - HX;
    const float AY = ModWrap(PY, SY) - HY;
    const float BX = ModWrap(PX - HX, SX) - HX;
    const float BitangentY = ModWrap(PY - HY, SY) - HY;

    const float DA = AX * AX + AY * AY;
    const float DB = BX * BX + BitangentY * BitangentY;
    const float GX = (DA < DB) ? AX : BX;
    const float GY = (DA < DB) ? AY : BitangentY;

    const float IdX = std::floor(PX - GX + 0.5f);
    const float IdY = std::floor(PY - GY + 0.5f);

    const float AbsX = std::fabs(GX);
    const float AbsY = std::fabs(GY);
    const float EdgeDist = 0.5f - std::max(AbsX * 0.5f + AbsY * 0.8660254f, AbsX);
    return { std::max(0.0f, EdgeDist), Hash21(IdX, IdY) };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateMagmaBlackbody(float Heat) noexcept
{
    const float H = std::clamp(Heat, 0.0f, 1.0f);
    const Mathematics::LinearRGB DarkCrust { 0.14f, 0.015f, 0.002f };
    const Mathematics::LinearRGB DeepRed   { 0.92f, 0.12f,  0.01f  };
    const Mathematics::LinearRGB MoltenOrg { 1.00f, 0.48f,  0.04f  };
    const Mathematics::LinearRGB WhiteHot  { 1.00f, 0.94f,  0.64f  };

    if (H < 0.35f)      return LerpRgb(DarkCrust, DeepRed, H / 0.35f);
    else if (H < 0.72f) return LerpRgb(DeepRed, MoltenOrg, (H - 0.35f) / 0.37f);
    return LerpRgb(MoltenOrg, WhiteHot, (H - 0.72f) / 0.28f);
}

[[nodiscard]] float EvaluateScalarReliefField(
    DynamicPresetIdentity        Preset,
    const DynamicParameterBlock& P,
    float                        U,
    float                        V,
    Mathematics::Axis3D          NormPos) noexcept
{
    const float T     = P.ClockSeconds * P.AnimationSpeed;
    const float Scale = P.DetailScale;

    switch (Preset)
    {
        case DynamicPresetIdentity::VolcanicPahoehoeMagma:
        {
            const float WarpX = (Fbm2D(U * Scale * 2.2f + 1.7f, V * Scale * 2.2f - 2.3f, 3) - 0.5f) * 1.35f;
            const float WarpY = (Fbm2D(U * Scale * 2.2f - 4.1f, V * Scale * 2.2f + 3.2f, 3) - 0.5f) * 1.35f;
            const auto VorA   = EvaluateVoronoiEdge(U * Scale * 3.0f + WarpX, V * Scale * 1.85f + WarpY);
            const float Slab  = SmoothstepScalar(0.015f, 0.14f, VorA.EdgeDistance);
            const float Rope  = Fbm2D(U * Scale * 7.5f + WarpX * 1.6f, V * Scale * 7.5f + WarpY * 1.6f, 4);
            return Slab * 0.075f + (Rope - 0.5f) * 0.036f * Slab;
        }
        case DynamicPresetIdentity::PhysicalOceanWater:
        {
            const float Amp = P.WaveAmplitude * 0.082f;
            const float W1  = std::sin(U * Scale * 4.2f + V * Scale * 2.2f + T * 1.7f);
            const float W2  = std::sin(-U * Scale * 3.1f + V * Scale * 5.0f - T * 1.35f) * 0.68f;
            const float W3  = std::sin(U * Scale * 7.4f - V * Scale * 4.1f + T * 2.4f) * 0.42f;
            const float Cap = (Fbm2D(U * Scale * 6.5f + T * 0.45f, V * Scale * 6.5f - T * 0.35f, 3) - 0.5f) * 0.60f;
            return (W1 + W2 + W3 + Cap) * Amp;
        }
        case DynamicPresetIdentity::RainStreakedSmokedGlass:
        {
            const float GX = U * Scale * 4.4f;
            const float GY = V * Scale * 2.6f;
            const float CX = std::floor(GX);
            const float CY = std::floor(GY);
            const float JitterX = (Hash21(CX + 3.1f, CY + 7.7f) - 0.5f) * 0.62f;
            const float Speed = 0.35f + 0.65f * Hash21(CX, 13.7f);
            const float YSlide = GY - T * Speed + Hash21(CX, 29.1f) * 11.0f;
            const float DX = (FractScalar(GX) - 0.5f - JitterX * 0.55f) * 1.65f;
            const float DY = (FractScalar(YSlide) - 0.5f) * 1.25f;
            const float Drop1 = SmoothstepScalar(0.24f, 0.0f, std::sqrt(DX * DX + DY * DY));

            const float G2X = U * Scale * 11.5f;
            const float G2Y = V * Scale * 11.5f;
            const float C2X = std::floor(G2X);
            const float C2Y = std::floor(G2Y);
            const float L2X = FractScalar(G2X) - 0.5f - (Hash21(C2X, C2Y) - 0.5f) * 0.58f;
            const float L2Y = FractScalar(G2Y) - 0.5f - (Hash21(C2X + 17.3f, C2Y + 41.9f) - 0.5f) * 0.58f;
            const float Present = (Hash21(C2X + 5.1f, C2Y + 2.7f) > 0.42f) ? 1.0f : 0.0f;
            const float Micro = SmoothstepScalar(0.22f, 0.0f, std::sqrt(L2X * L2X + L2Y * L2Y)) * Present;
            return (Drop1 * 0.055f + Micro * 0.024f) * P.WeatherRainIntensity;
        }
        case DynamicPresetIdentity::WetAsphaltRainPuddles:
        {
            const float Puddle = SmoothstepScalar(0.58f - 0.25f * P.WeatherRainIntensity, 0.72f - 0.20f * P.WeatherRainIntensity,
                Fbm2D(U * Scale * 0.65f, V * Scale * 0.65f, 3));
            const float Grit   = (Fbm2D(U * Scale * 12.0f, V * Scale * 12.0f, 3) - 0.5f) * 0.025f * (1.0f - Puddle);
            const float RipCellX = std::floor(U * Scale * 2.5f);
            const float RipCellY = std::floor(V * Scale * 2.5f);
            const float FX = FractScalar(U * Scale * 2.5f) - 0.5f;
            const float FY = FractScalar(V * Scale * 2.5f) - 0.5f;
            const float Rad = std::sqrt(FX * FX + FY * FY);
            const float Ph  = FractScalar(T * 0.85f + Hash21(RipCellX, RipCellY));
            const float Rip = std::sin((Rad - Ph * 0.55f) * 34.0f) * std::exp(-std::pow((Rad - Ph * 0.55f) * 12.0f, 2.0f)) * (1.0f - Ph);
            return Grit + Puddle * Rip * 0.022f;
        }
        case DynamicPresetIdentity::LiquidChromeMercury:
        {
            const float R1 = std::sin(NormPos.X * Scale * 3.2f + NormPos.Y * Scale * 1.9f + T * 2.1f);
            const float R2 = std::cos(NormPos.Y * Scale * 3.8f - NormPos.Z * Scale * 2.4f - T * 1.8f);
            const float R3 = std::sin((NormPos.X + NormPos.Z) * Scale * 2.7f + T * 1.5f);
            return (R1 * 0.45f + R2 * 0.35f + R3 * 0.20f) * 0.032f * P.WaveAmplitude;
        }
        case DynamicPresetIdentity::HoarfrostSparkleSnow:
        {
            const float Drift = Fbm2D(U * Scale * 1.4f, V * Scale * 0.75f, 4);
            return (Drift - 0.5f) * 0.035f;
        }
        case DynamicPresetIdentity::SurfaceMaterialTransition:
        {
            const float Brush = LatticeNoise2D(U * 96.0f, V * 14.0f);
            return (Brush - 0.5f) * 0.012f;
        }
        default:
            return 0.0f;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

ResolvedDynamicSurface ResolveDynamicSurface(
    const DynamicMaterialRecipe& Recipe,
    const DynamicParameterBlock& Parameters,
    const DynamicSurfaceQuery&   Query) noexcept
{
    ResolvedDynamicSurface Out {};
    Out.Material       = Recipe.PrimaryFinish;
    Out.ShadingNormal  = Mathematics::NormalizeAxis(Query.WorldNormal, { 0.0f, 0.0f, 1.0f });
    Out.SurfaceOpacity = Parameters.OpacityScale;

    const Mathematics::Axis3D NormPos = NormalizeAssetCoordinate(Parameters, Query.LocalPosition);
    const Mathematics::Axis3D UnitN   = Out.ShadingNormal;
    const Mathematics::Axis3D UnitV   = Mathematics::NormalizeAxis(Query.WorldViewDir, { 0.0f, 0.0f, 1.0f });
    const float NdotV   = std::clamp(Mathematics::EvaluateDot(UnitN, UnitV), 0.0f, 1.0f);
    const float Fresnel = std::pow(1.0f - NdotV, 3.0f);

    // Distortion-free coordinates: use planar XZ/XY projection on flat faces (zero polar pinch on planes/boxes/cloth!)
    float SphU = 0.5f + std::atan2(NormPos.X, NormPos.Z) / Constants::TauFloat;
    float SphV = 0.5f - std::asin(std::clamp(NormPos.Y / std::max(0.001f, std::sqrt(
        NormPos.X * NormPos.X + NormPos.Y * NormPos.Y + NormPos.Z * NormPos.Z)), -1.0f, 1.0f)) / Constants::PiFloat;
    if (std::fabs(UnitN.Y) > 0.92f)
    {
        SphU = NormPos.X * 0.36f + 0.5f;
        SphV = NormPos.Z * 0.36f + 0.5f;
    }
    else if (std::fabs(UnitN.Z) > 0.92f && std::fabs(NormPos.Z) < 0.72f)
    {
        SphU = NormPos.X * 0.36f + 0.5f;
        SphV = NormPos.Y * 0.36f + 0.5f;
    }

    const float T     = Parameters.ClockSeconds * Parameters.AnimationSpeed;
    const float Scale = Parameters.DetailScale;

    // Finite-difference normal perturbation for tactile procedural relief & waves
    const float ReliefAmp = Parameters.ReliefStrength;
    if (ReliefAmp > 1.0e-3f)
    {
        constexpr float Eps = 0.0045f;
        const float H0 = EvaluateScalarReliefField(Recipe.PresetIdentity, Parameters, SphU,       SphV,       NormPos);
        const float HU = EvaluateScalarReliefField(Recipe.PresetIdentity, Parameters, SphU + Eps, SphV,       NormPos);
        const float HV = EvaluateScalarReliefField(Recipe.PresetIdentity, Parameters, SphU,       SphV + Eps, NormPos);
        const float DDu = ((HU - H0) / Eps) * ReliefAmp * 0.14f;
        const float DDv = ((HV - H0) / Eps) * ReliefAmp * 0.14f;
        Out.ShadingNormal = Mathematics::NormalizeAxis(
            UnitN - Query.WorldTangent * DDu - Query.WorldBitangent * DDv,
            UnitN);
    }

    const Mathematics::LinearRGB PrimaryCol   { Parameters.PrimaryColorRgba[0],   Parameters.PrimaryColorRgba[1],   Parameters.PrimaryColorRgba[2]   };
    const Mathematics::LinearRGB SecondaryCol { Parameters.SecondaryColorRgba[0], Parameters.SecondaryColorRgba[1], Parameters.SecondaryColorRgba[2] };
    const Mathematics::LinearRGB TertiaryCol  { Parameters.TertiaryColorRgba[0],  Parameters.TertiaryColorRgba[1],  Parameters.TertiaryColorRgba[2]  };

    switch (Recipe.PresetIdentity)
    {
        case DynamicPresetIdentity::LatticeVoxelDissolve:
        {
            const float Grid = std::max(4.0f, Scale * 1.25f);
            const float CX   = std::floor(NormPos.X * Grid);
            const float CY   = std::floor(NormPos.Y * Grid);
            const float CZ   = std::floor(NormPos.Z * Grid);
            const float CellHash = Hash31(CX, CY, CZ);

            const float SweepFront = LerpScalar(-1.10f, 1.10f, Parameters.TransitionProgress);
            const float SignedDist = (NormPos.X * Parameters.SweepAxisWidth[0] +
                                      NormPos.Y * Parameters.SweepAxisWidth[1] +
                                      (CellHash - 0.5f) * 0.42f) - SweepFront;

            if (SignedDist < -0.18f && CellHash < 0.72f)
            {
                Out.CutoutDiscarded = true;
                Out.SurfaceOpacity  = 0.0f;
                return Out;
            }

            const float EdgeBand   = std::exp(-std::pow((SignedDist + 0.04f) * 7.5f, 2.0f));
            const float HotCell    = SmoothstepScalar(0.32f, 0.0f, std::fabs(SignedDist + 0.02f)) * (0.45f + 0.55f * CellHash);
            const float Fx = std::fabs(FractScalar(NormPos.X * Grid) - 0.5f);
            const float Fy = std::fabs(FractScalar(NormPos.Y * Grid) - 0.5f);
            const float Wire = SmoothstepScalar(0.38f, 0.49f, std::max(Fx, Fy)) * SmoothstepScalar(0.55f, 0.0f, std::fabs(SignedDist));

            Out.Material.Diffuse.BaseColor = LerpRgb(PrimaryCol, SecondaryCol, HotCell * 0.45f);
            Out.Material.Specular.Roughness = std::clamp(0.055f + Parameters.RoughnessBias, 0.02f, 0.95f);
            Out.Material.Clearcoat.CoatWeight = 0.92f;
            const Mathematics::LinearRGB SeamTint = LerpRgb(TertiaryCol, { 1.0f, 1.0f, 1.0f }, EdgeBand * 0.75f);
            const float Glow = (EdgeBand * 1.85f + HotCell * 0.95f + Wire * 1.45f) * Parameters.SeamGlowIntensity;
            Out.IncandescentRadiance = SeamTint * Glow;
            break;
        }

        case DynamicPresetIdentity::VolcanicPahoehoeMagma:
        {
            // Domain-warped tectonic basalt plates + narrow recessed glowing fissures
            const float WarpX = (Fbm2D(SphU * Scale * 2.2f + 1.7f, SphV * Scale * 2.2f - 2.3f, 3) - 0.5f) * 1.35f;
            const float WarpY = (Fbm2D(SphU * Scale * 2.2f - 4.1f, SphV * Scale * 2.2f + 3.2f, 3) - 0.5f) * 1.35f;
            const auto VorMain = EvaluateVoronoiEdge(SphU * Scale * 3.0f + WarpX, SphV * Scale * 1.85f + WarpY);
            const auto VorSub  = EvaluateVoronoiEdge(SphU * Scale * 6.5f - WarpY * 0.7f, SphV * Scale * 4.0f + WarpX * 0.7f);

            const float BasaltTex = Fbm2D(SphU * Scale * 9.5f + WarpX * 1.2f, SphV * Scale * 9.5f + WarpY * 1.2f, 4);
            const float AshDust   = SmoothstepScalar(0.42f, 0.80f, Fbm2D(SphU * Scale * 4.2f, SphV * Scale * 4.2f, 3));

            const float MainCrack = 1.0f - SmoothstepScalar(0.008f, 0.052f, VorMain.EdgeDistance);
            const float FineCrack = (1.0f - SmoothstepScalar(0.005f, 0.028f, VorSub.EdgeDistance)) * SmoothstepScalar(0.04f, 0.28f, VorMain.EdgeDistance) * 0.42f;
            const float Pulse     = 0.78f + 0.22f * std::sin(T * 1.85f + VorMain.CellId * 6.283f);
            const float Heat      = std::clamp((MainCrack * 1.15f + FineCrack * 0.55f) * Pulse, 0.0f, 1.0f);

            const Mathematics::LinearRGB RockCol = LerpRgb(PrimaryCol * 0.85f, SecondaryCol * 0.92f, BasaltTex * 0.65f + AshDust * 0.35f);
            Out.Material.Diffuse.BaseColor        = RockCol * (1.0f - Heat * 0.90f);
            Out.Material.Diffuse.DiffuseRoughness = 0.90f;
            Out.Material.Specular.Roughness       = std::clamp(LerpScalar(0.92f, 0.32f, Heat) + Parameters.RoughnessBias, 0.06f, 0.98f);
            Out.Material.Specular.Metalness       = 0.02f;
            Out.Material.Clearcoat.CoatWeight     = 0.0f;

            if (Heat > 0.04f)
            {
                const auto Blackbody = EvaluateMagmaBlackbody(Heat);
                Out.IncandescentRadiance = Blackbody * (Heat * Heat * Parameters.SeamGlowIntensity * 1.65f);
            }
            break;
        }

        case DynamicPresetIdentity::HexagonalEnergyBarrier:
        {
            // 1:1 parity with Particle Studio / HTML Energy Shield Hex Barrier
            const float SpinU = SphU + T * 0.032f;
            const auto Hex = EvaluateHexLattice(SpinU * Scale * 9.4f, SphV * Scale * 5.2f);
            const float HexWire = 1.0f - SmoothstepScalar(0.015f, 0.062f, Hex.EdgeDistance);
            const float Shimmer = 0.5f + 0.5f * std::sin(T * 2.6f + Hex.CellHash * 6.283f);
            const float RimShell = std::pow(1.0f - NdotV, 2.55f);

            // Live geodesic expanding shockwave ripples + impact hot flashes from up to 4 world impact pulses
            float RippleSum = 0.0f;
            float FlashSum  = 0.0f;
            for (const auto& Impact : Parameters.ImpactOriginsAge)
            {
                const float Age = Impact[3];
                if (Age < 0.0f) continue;
                const Mathematics::Axis3D OriginDir = Mathematics::NormalizeAxis({ Impact[0], Impact[1], Impact[2] }, { 0.0f, 0.0f, 1.0f });
                const float CosAngle = std::clamp(Mathematics::EvaluateDot(UnitN, OriginDir), -1.0f, 1.0f);
                const float GeodesicDist = std::acos(CosAngle);
                const float Cycle = FractScalar(Age * 0.46f);
                const float Ring  = std::exp(-std::pow((GeodesicDist - Cycle * 2.15f) * 9.0f, 2.0f)) * (1.0f - Cycle);
                const float Flash = std::exp(-GeodesicDist * GeodesicDist * 14.0f) * std::exp(-Cycle * 4.2f);
                RippleSum += Ring;
                FlashSum  += Flash;
            }

            Out.Material.Diffuse.BaseColor    = PrimaryCol * 0.04f;
            Out.Material.Specular.Roughness   = 0.08f;
            Out.Material.Specular.Metalness   = 0.05f;
            Out.Material.Clearcoat.CoatWeight = 0.25f;

            const Mathematics::LinearRGB ShellGlow = LerpRgb(SecondaryCol, PrimaryCol, 0.35f + 0.65f * RimShell);
            const Mathematics::LinearRGB WireGlow = LerpRgb(PrimaryCol, TertiaryCol, 0.45f + 0.55f * Shimmer);
            const Mathematics::LinearRGB ShockCol { 0.86f, 0.98f, 1.00f };

            Out.IncandescentRadiance =
                ShellGlow * (RimShell * 0.95f + 0.055f) +
                WireGlow * (HexWire * (0.38f + 0.62f * RimShell + 0.28f * Shimmer) * 1.05f) +
                ShockCol * ((RippleSum * 1.35f + FlashSum * 1.85f) * Parameters.SeamGlowIntensity * 0.55f);

            const float Alpha = std::clamp(
                (0.08f + RimShell * 0.68f + HexWire * 0.34f + RippleSum * 0.42f + FlashSum * 0.46f) * Parameters.OpacityScale,
                0.06f, 0.94f);
            Out.SurfaceOpacity = Alpha;
            break;
        }

        case DynamicPresetIdentity::SurfaceMaterialTransition:
        {
            // Modular Material A (Brushed Titanium) <-> Material B (Liquid Obsidian Clearcoat) across crisp ionized hex sweep
            const float SweepCenter = LerpScalar(-0.85f, 0.85f, Parameters.TransitionProgress);
            const float Organic     = (Fbm2D(SphU * Scale * 1.45f, SphV * Scale * 1.45f, 3) - 0.5f) * 0.14f;
            const auto  Hex         = EvaluateHexLattice(SphU * Scale * 6.8f, SphV * Scale * 3.8f);
            const float HexJitter   = (Hex.CellHash - 0.5f) * 0.08f;
            const float FrontDist   = (NormPos.X * Parameters.SweepAxisWidth[0] +
                                       NormPos.Y * Parameters.SweepAxisWidth[1] + Organic + HexJitter) - SweepCenter;
            const float BlendB      = SmoothstepScalar(-0.032f, 0.032f, FrontDist);

            const float SeamLine = std::exp(-std::pow(FrontDist * 28.0f, 2.0f));
            const float HexEdge  = (1.0f - SmoothstepScalar(0.015f, 0.085f, Hex.EdgeDistance)) * std::exp(-std::pow(FrontDist * 9.5f, 2.0f));

            const float Brush = LatticeNoise2D(SphU * 110.0f, SphV * 16.0f);
            const Mathematics::LinearRGB ColA = LerpRgb(PrimaryCol * 0.84f, PrimaryCol * 1.12f, Brush);
            const float Flake = SmoothstepScalar(0.82f, 0.995f, Hash31(
                std::floor(NormPos.X * 195.0f), std::floor(NormPos.Y * 195.0f), std::floor(NormPos.Z * 195.0f)));
            const Mathematics::LinearRGB ColB = SecondaryCol + Mathematics::LinearRGB { 0.025f, 0.055f, 0.105f } * (Flake + Fresnel * 0.35f);

            Out.Material.Diffuse.BaseColor       = LerpRgb(ColA, ColB, BlendB);
            Out.Material.Specular.Roughness      = std::clamp(LerpScalar(0.24f, 0.05f, BlendB) + Parameters.RoughnessBias, 0.02f, 0.95f);
            Out.Material.Specular.Metalness      = std::clamp(LerpScalar(0.92f, 0.22f, BlendB) + Parameters.MetalnessBias, 0.0f, 1.0f);
            Out.Material.Clearcoat.CoatWeight    = LerpScalar(0.04f, 0.96f, BlendB);
            Out.Material.Clearcoat.CoatRoughness = 0.035f;

            const Mathematics::LinearRGB SeamCol = LerpRgb(TertiaryCol, { 1.0f, 1.0f, 1.0f }, SeamLine * 0.78f);
            Out.IncandescentRadiance = SeamCol * ((SeamLine * 1.85f + HexEdge * 1.35f) * Parameters.SeamGlowIntensity * 0.68f);
            break;
        }

        case DynamicPresetIdentity::HolographicTelemetryScan:
        {
            const float ScanY = LerpScalar(-0.78f, 0.78f, Parameters.TransitionProgress);
            const float BeamDistance = NormPos.Y - ScanY;

            const float LaserBeamPeak = std::exp(-std::pow(BeamDistance * 26.0f, 2.0f));
            const float WakeMask  = SmoothstepScalar(-0.85f, 0.0f, BeamDistance) * SmoothstepScalar(0.05f, -0.01f, BeamDistance);

            const float LatLines  = 1.0f - SmoothstepScalar(0.03f, 0.11f, std::fabs(FractScalar(NormPos.Y * Scale * 2.2f) - 0.5f) * 2.0f);
            const float LonLines  = 1.0f - SmoothstepScalar(0.03f, 0.11f, std::fabs(FractScalar(SphU * Scale * 3.6f) - 0.5f) * 2.0f);
            const float GridWire  = std::max(LatLines, LonLines) * WakeMask;
            const float ScanFill  = WakeMask * 0.22f;
            const float HoloRim   = std::pow(1.0f - NdotV, 2.6f);

            Out.Material.Diffuse.BaseColor    = PrimaryCol;
            Out.Material.Specular.Roughness   = 0.08f;
            Out.Material.Specular.Metalness   = 0.28f;
            Out.Material.Clearcoat.CoatWeight = 0.85f;

            const Mathematics::LinearRGB BeamCol = LerpRgb(TertiaryCol, { 1.0f, 1.0f, 1.0f }, LaserBeamPeak * 0.82f);
            Out.IncandescentRadiance =
                BeamCol * (LaserBeamPeak * 2.6f + GridWire * 1.35f) * Parameters.SeamGlowIntensity * 0.65f +
                TertiaryCol * ((ScanFill + HoloRim * 0.85f) * 0.85f);
            break;
        }

        case DynamicPresetIdentity::PhysicalOceanWater:
        {
            const float WaterFresnel = 0.02f + 0.98f * std::pow(1.0f - NdotV, 5.0f);
            const float CrestH = EvaluateScalarReliefField(DynamicPresetIdentity::PhysicalOceanWater, Parameters, SphU, SphV, NormPos);
            const float CrestMask = SmoothstepScalar(0.012f, 0.048f, CrestH);
            const float FoamNoise = SmoothstepScalar(0.58f, 0.86f, Fbm2D(SphU * Scale * 7.0f + T * 0.35f, SphV * Scale * 7.0f - T * 0.25f, 3));
            const float FoamMask  = CrestMask * FoamNoise * 0.65f;

            Mathematics::LinearRGB WaterCol = LerpRgb(PrimaryCol, SecondaryCol, CrestMask * 0.72f + (1.0f - WaterFresnel) * 0.22f);
            WaterCol = LerpRgb(WaterCol, TertiaryCol, FoamMask);

            Out.Material.Diffuse.BaseColor              = WaterCol;
            Out.Material.Specular.Roughness             = std::clamp(LerpScalar(0.045f, 0.42f, FoamMask) + Parameters.RoughnessBias, 0.02f, 0.90f);
            Out.Material.Specular.Metalness             = 0.0f;
            Out.Material.Specular.DielectricIOR         = Parameters.OpticalIor;
            Out.Material.Clearcoat.CoatWeight           = 0.55f * (1.0f - FoamMask);
            Out.Material.Clearcoat.CoatRoughness        = 0.025f;
            Out.Material.Transmission.TransmissionWeight = 0.62f * (1.0f - FoamMask);
            Out.BacklitCrestScatter                     = CrestMask * (1.0f - FoamMask);
            Out.IncandescentRadiance                    = SecondaryCol * (Out.BacklitCrestScatter * 0.28f);
            Out.SurfaceOpacity                          = std::clamp(0.76f + WaterFresnel * 0.24f, 0.5f, 1.0f);
            break;
        }

        case DynamicPresetIdentity::LiquidObsidianClearcoat:
        {
            const float Flake = SmoothstepScalar(0.72f, 0.99f, Hash31(
                std::floor(NormPos.X * 85.0f), std::floor(NormPos.Y * 85.0f), std::floor(NormPos.Z * 85.0f)));
            Out.Material.Diffuse.BaseColor = PrimaryCol + SecondaryCol * (Fresnel * 0.35f) + TertiaryCol * (Flake * 0.14f);
            Out.Material.Specular.Roughness = std::clamp(0.055f + Parameters.RoughnessBias, 0.02f, 0.85f);
            Out.Material.Specular.Metalness = 0.26f;
            Out.Material.Clearcoat.CoatWeight = 0.98f;
            Out.Material.Clearcoat.CoatRoughness = 0.03f;
            break;
        }

        case DynamicPresetIdentity::RainStreakedSmokedGlass:
        {
            const float DropH = EvaluateScalarReliefField(DynamicPresetIdentity::RainStreakedSmokedGlass, Parameters, SphU, SphV, NormPos);
            const float Wet   = SmoothstepScalar(0.004f, 0.028f, DropH);
            const float ColX  = SphU * Scale * 3.6f;
            const float Track = (1.0f - SmoothstepScalar(0.06f, 0.22f, std::fabs(FractScalar(ColX) - 0.5f))) * Parameters.WeatherRainIntensity;
            const float Gloss = std::clamp(Wet + Track * 0.65f, 0.0f, 1.0f);

            Out.Material.Diffuse.BaseColor   = LerpRgb(PrimaryCol, SecondaryCol, 0.25f * (1.0f - Gloss));
            Out.Material.Specular.Roughness  = std::clamp(LerpScalar(0.28f, 0.025f, Gloss) + Parameters.RoughnessBias, 0.015f, 0.90f);
            Out.Material.Specular.Metalness  = 0.05f;
            Out.Material.Clearcoat.CoatWeight = 0.45f + 0.55f * Gloss;
            Out.Material.Clearcoat.CoatRoughness = LerpScalar(0.18f, 0.02f, Gloss);
            break;
        }

        case DynamicPresetIdentity::WetAsphaltRainPuddles:
        {
            const float PuddleNoise = Fbm2D(SphU * Scale * 0.65f, SphV * Scale * 0.65f, 3);
            const float Threshold   = 0.64f - 0.26f * Parameters.WeatherRainIntensity;
            const float DampMask    = SmoothstepScalar(Threshold - 0.14f, Threshold + 0.02f, PuddleNoise);
            const float PuddleMask  = SmoothstepScalar(Threshold, Threshold + 0.11f, PuddleNoise);
            const float Aggregate   = Fbm2D(SphU * Scale * 11.0f, SphV * Scale * 11.0f, 3);

            const Mathematics::LinearRGB DryCol  = PrimaryCol * (0.72f + 0.55f * Aggregate);
            const Mathematics::LinearRGB DampCol = SecondaryCol * (0.65f + 0.40f * Aggregate);
            Out.Material.Diffuse.BaseColor       = LerpRgb(DryCol, DampCol, DampMask);
            Out.Material.Specular.Roughness      = std::clamp(LerpScalar(0.86f, 0.025f, PuddleMask) + Parameters.RoughnessBias, 0.02f, 0.98f);
            Out.Material.Clearcoat.CoatWeight    = PuddleMask * 0.95f;
            Out.Material.Clearcoat.CoatRoughness = 0.02f;
            break;
        }

        case DynamicPresetIdentity::HoarfrostSparkleSnow:
        {
            const float Drift  = Fbm2D(SphU * Scale * 1.4f, SphV * Scale * 0.75f, 4);
            const float Sparkle = SmoothstepScalar(0.93f, 0.995f, Hash31(
                std::floor(NormPos.X * 110.0f), std::floor(NormPos.Y * 110.0f), std::floor(NormPos.Z * 110.0f))) *
                std::pow(std::max(0.0f, NdotV), 0.7f);
            Out.Material.Diffuse.BaseColor       = LerpRgb(SecondaryCol, PrimaryCol, 0.58f + 0.42f * Drift);
            Out.Material.Specular.Roughness      = std::clamp(0.42f - 0.25f * Sparkle + Parameters.RoughnessBias, 0.05f, 0.90f);
            Out.Material.Subsurface.SubsurfaceWeight = 0.52f;
            Out.IncandescentRadiance             = TertiaryCol * (Sparkle * 1.15f);
            break;
        }

        case DynamicPresetIdentity::SubmergedLimestoneCaustics:
        {
            const auto Pebble = EvaluateVoronoiEdge(SphU * Scale * 1.8f, SphV * Scale * 1.8f);
            const auto CausA  = EvaluateVoronoiEdge(SphU * Scale * 2.1f + T * 0.25f, SphV * Scale * 2.1f - T * 0.18f);
            const auto CausB  = EvaluateVoronoiEdge(SphU * Scale * 2.4f - T * 0.21f, SphV * Scale * 2.4f + T * 0.24f);
            const float CausticWeb = std::pow(1.0f - SmoothstepScalar(0.0f, 0.18f, std::min(CausA.EdgeDistance, CausB.EdgeDistance)), 2.2f);
            const float Stone = 0.65f + 0.35f * Pebble.CellId;

            Out.Material.Diffuse.BaseColor       = LerpRgb(SecondaryCol, PrimaryCol, Stone);
            Out.Material.Specular.Roughness      = 0.34f;
            Out.Material.Clearcoat.CoatWeight    = 0.45f;
            Out.IncandescentRadiance             = TertiaryCol * (CausticWeb * Parameters.SeamGlowIntensity * 0.62f);
            break;
        }

        case DynamicPresetIdentity::LiquidChromeMercury:
        {
            Out.Material.Diffuse.BaseColor   = PrimaryCol;
            Out.Material.Specular.Metalness  = std::clamp(0.98f + Parameters.MetalnessBias, 0.0f, 1.0f);
            Out.Material.Specular.Roughness  = std::clamp(0.055f + Parameters.RoughnessBias, 0.015f, 0.80f);
            Out.Material.Clearcoat.CoatWeight = 0.0f;
            break;
        }

        case DynamicPresetIdentity::AnodizedThinFilmIridescence:
        {
            const float FilmDrift = (Fbm2D(SphU * Scale * 1.1f + T * 0.14f, SphV * Scale * 1.1f - T * 0.10f, 3) - 0.5f) * 2.0f;
            Out.Material.Diffuse.BaseColor                    = PrimaryCol;
            Out.Material.Specular.Metalness                   = 0.92f;
            Out.Material.Specular.Roughness                   = std::clamp(0.18f + Parameters.RoughnessBias, 0.03f, 0.85f);
            Out.Material.ThinFilm.ThinFilmWeight              = 1.0f;
            Out.Material.ThinFilm.ThinFilmThicknessNanometers = std::clamp(
                Parameters.ThinFilmNanometers + FilmDrift * Parameters.ThinFilmSpread, 120.0f, 1200.0f);
            Out.Material.ThinFilm.ThinFilmIOR                 = 2.15f;
            break;
        }
    }

    SanitizeSurfaceMaterial(Out.Material);
    return Out;
}

} // namespace Frontier::MaterialSpace::Dynamic
