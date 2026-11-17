/*============================================================================================================================================
                                                      LUNARSTARFIELDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (6 surface presets, 29.53-day phase, roll/pitch) and deterministic twinkling Starfield evaluation

#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float HashInt3(int32_t IX, int32_t IY, int32_t IZ) noexcept
{
    uint32_t N = static_cast<uint32_t>(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return static_cast<float>(N) * (1.0f / 4294967296.0f);
}

[[nodiscard]] float SmoothLatticeNoise3(float X, float Y, float Z) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const int32_t IZ = static_cast<int32_t>(std::floor(Z));
    const float FX   = X - static_cast<float>(IX);
    const float FY   = Y - static_cast<float>(IY);
    const float FZ   = Z - static_cast<float>(IZ);
    const float UX   = FX * FX * (3.0f - 2.0f * FX);
    const float UY   = FY * FY * (3.0f - 2.0f * FY);
    const float UZ   = FZ * FZ * (3.0f - 2.0f * FZ);

    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float N000 = HashInt3(IX, IY, IZ),     N100 = HashInt3(IX + 1, IY, IZ);
    const float N010 = HashInt3(IX, IY + 1, IZ), N110 = HashInt3(IX + 1, IY + 1, IZ);
    const float N001 = HashInt3(IX, IY, IZ + 1), N101 = HashInt3(IX + 1, IY, IZ + 1);
    const float N011 = HashInt3(IX, IY + 1, IZ + 1), N111 = HashInt3(IX + 1, IY + 1, IZ + 1);

    return Mix(
        Mix(Mix(N000, N100, UX), Mix(N010, N110, UX), UY),
        Mix(Mix(N001, N101, UX), Mix(N011, N111, UX), UY),
        UZ);
}

} // namespace

Mathematics::LinearRGB LunarStarfieldSequence::QueryPresetTint(LunarSurfacePreset Preset) noexcept
{
    switch (Preset)
    {
        case LunarSurfacePreset::Luna:    return { 0.94f, 0.93f, 0.90f };
        case LunarSurfacePreset::Ember:   return { 1.00f, 0.56f, 0.34f };
        case LunarSurfacePreset::Glacier: return { 0.76f, 0.90f, 1.00f };
        case LunarSurfacePreset::Sulfur:  return { 0.98f, 0.88f, 0.42f };
        case LunarSurfacePreset::Shroud:  return { 0.72f, 0.74f, 0.82f };
        case LunarSurfacePreset::Shard:   return { 0.84f, 0.96f, 0.94f };
    }
    return { 0.94f, 0.93f, 0.90f };
}

Mathematics::Axis3D LunarStarfieldSequence::ResolveMoonWorldDirection(
    const LunarBodyConfiguration& Moon,
    const Mathematics::Axis3D&    SunDirection) noexcept
{
    if (Moon.FollowSkyOppositeSun)
    {
        return Mathematics::NormalizeAxis({ -SunDirection.X, -SunDirection.Y, -SunDirection.Z });
    }
    return CelestialEphemerisSolver::ConvertAzimuthElevationToWorldAxis(Moon.AzimuthDegrees, Moon.ElevationDegrees);
}

std::vector<uint8_t> LunarStarfieldSequence::BuildProceduralLunarSurfaceAtlasRgba8()
{
    const uint32_t TotalH = AtlasTileHeightPx * AtlasPresetCount;
    std::vector<uint8_t> Pixels(static_cast<std::size_t>(AtlasTileWidthPx) * TotalH * 4u, 255u);

    for (uint32_t P = 0u; P < AtlasPresetCount; ++P)
    {
        const auto Tint = QueryPresetTint(static_cast<LunarSurfacePreset>(P));
        const float SeedOff = static_cast<float>(P) * 7.13f;
        for (uint32_t Y = 0u; Y < AtlasTileHeightPx; ++Y)
        {
            const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(AtlasTileHeightPx);
            const float Theta = V * Constants::PiFloat;
            const float SinT = std::sin(Theta);
            const float CosT = std::cos(Theta);
            for (uint32_t X = 0u; X < AtlasTileWidthPx; ++X)
            {
                const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(AtlasTileWidthPx);
                const float Phi = U * Constants::TauFloat;
                const float SX = SinT * std::cos(Phi);
                const float SY = SinT * std::sin(Phi);
                const float SZ = CosT;

                const float MacroMare = SmoothLatticeNoise3(SX * 2.4f + SeedOff, SY * 2.4f - 1.3f, SZ * 2.4f);
                const float Craters   = SmoothLatticeNoise3(SX * 9.5f - SeedOff, SY * 9.5f + 3.1f, SZ * 9.5f);
                const float MicroDust = SmoothLatticeNoise3(SX * 22.0f, SY * 22.0f, SZ * 22.0f + SeedOff);
                const float AlbedoMul = std::clamp(0.56f + 0.32f * MacroMare + 0.14f * Craters - 0.06f * MicroDust, 0.30f, 1.0f);

                const std::size_t Idx = (static_cast<std::size_t>(P * AtlasTileHeightPx + Y) * AtlasTileWidthPx + X) * 4u;
                Pixels[Idx + 0u] = static_cast<uint8_t>(std::clamp(Tint.R * AlbedoMul * 255.0f, 0.0f, 255.0f));
                Pixels[Idx + 1u] = static_cast<uint8_t>(std::clamp(Tint.G * AlbedoMul * 255.0f, 0.0f, 255.0f));
                Pixels[Idx + 2u] = static_cast<uint8_t>(std::clamp(Tint.B * AlbedoMul * 255.0f, 0.0f, 255.0f));
                Pixels[Idx + 3u] = 255u;
            }
        }
    }
    return Pixels;
}

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateCelestialField(
    const Mathematics::Axis3D&                  RayDirection,
    const Mathematics::Axis3D&                  SunDirection,
    std::span<const LunarBodyConfiguration, 4u> Moons,
    const StarfieldConfiguration&               Stars,
    float                                       SimulationSeconds) noexcept
{
    Mathematics::LinearRGB Out { 0.0f, 0.0f, 0.0f };

    // 1. Evaluate active Moon discs + halos
    for (const LunarBodyConfiguration& Moon : Moons)
    {
        if (!Moon.Visible || RayDirection.Z < -0.05f)
        {
            continue;
        }
        const auto MoonDir = ResolveMoonWorldDirection(Moon, SunDirection);
        const float CosM   = std::clamp(Mathematics::EvaluateDot(RayDirection, MoonDir), -1.0f, 1.0f);
        const float AngR   = std::max(0.001f, Moon.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        const float CosEdge = std::cos(AngR);
        const auto  Tint   = QueryPresetTint(Moon.SurfacePreset);

        const float Glow = std::pow(std::max(0.0f, CosM), 420.0f) * Moon.HaloGlowMultiplier * 0.28f * Moon.BrightnessMultiplier;
        Out.R += Tint.R * Glow;
        Out.G += Tint.G * Glow;
        Out.B += Tint.B * Glow;

        if (CosM > CosEdge)
        {
            const Mathematics::Axis3D UpRef = (std::abs(MoonDir.Z) < 0.98f)
                ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
                : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
            const Mathematics::Axis3D TangentT = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UpRef, MoonDir));
            const Mathematics::Axis3D TangentB = Mathematics::EvaluateCross(MoonDir, TangentT);

            const float RollRad = Moon.SurfaceRollDegrees * Constants::DegToRadFloat;
            const float CosR = std::cos(RollRad);
            const float SinR = std::sin(RollRad);
            const Mathematics::Axis3D T = TangentT * CosR + TangentB * SinR;
            const Mathematics::Axis3D B = TangentB * CosR - TangentT * SinR;

            const float SinAng = std::max(1.0e-4f, std::sin(AngR));
            const float PX = Mathematics::EvaluateDot(RayDirection, T) / SinAng;
            const float PY = Mathematics::EvaluateDot(RayDirection, B) / SinAng;
            const float R2 = PX * PX + PY * PY;
            if (R2 < 1.0f)
            {
                const float PZ = std::sqrt(std::max(0.0f, 1.0f - R2));
                const float PitchRad = Moon.SurfacePitchDegrees * Constants::DegToRadFloat;
                const float CosP = std::cos(PitchRad);
                const float SinP = std::sin(PitchRad);
                const float SurfY = PY * CosP - PZ * SinP;
                const float SurfZ = PY * SinP + PZ * CosP;

                const float PhaseAng = (Moon.SynodicPhaseFraction - 0.5f) * Constants::TauFloat;
                const float LX = std::sin(PhaseAng);
                const float LY = 0.18f;
                const float LZ = std::cos(PhaseAng);
                const float LL = std::sqrt(LX * LX + LY * LY + LZ * LZ);
                const float NdotL = std::max(0.0f, (PX * LX + PY * LY + PZ * LZ) / std::max(1.0e-4f, LL));

                const float PresetSeed = static_cast<float>(static_cast<uint32_t>(Moon.SurfacePreset)) * 3.17f;
                const float Mare = 0.62f + 0.38f * SmoothLatticeNoise3(PX * 3.8f + 4.1f + PresetSeed, SurfY * 3.8f - 2.3f, SurfZ * 3.8f);
                const float EdgeAa = std::clamp((1.0f - std::sqrt(R2)) * 14.0f, 0.0f, 1.0f);
                const float Lum = (0.015f + NdotL * 1.45f) * Mare * Moon.BrightnessMultiplier * EdgeAa;
                Out.R += Tint.R * Lum;
                Out.G += Tint.G * Lum;
                Out.B += Tint.B * Lum;
            }
        }
    }

    // 2. Deterministic procedural Starfield with daylight extinction & scintillation
    if (Stars.StarfieldEnabled && RayDirection.Z > 0.005f)
    {
        const float DayFade = std::clamp(1.0f - (SunDirection.Z + 0.08f) * 5.0f, 0.0f, 1.0f);
        if (DayFade > 0.01f)
        {
            const float RotRad = Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
            const float CR = std::cos(RotRad);
            const float SR = std::sin(RotRad);
            const float RX = RayDirection.X * CR - RayDirection.Y * SR;
            const float RY = RayDirection.X * SR + RayDirection.Y * CR;
            const float RZ = RayDirection.Z;

            constexpr float GridScale = 175.0f;
            const float GX = RX * GridScale;
            const float GY = RY * GridScale;
            const float GZ = RZ * GridScale;
            const int32_t IX = static_cast<int32_t>(std::floor(GX));
            const int32_t IY = static_cast<int32_t>(std::floor(GY));
            const int32_t IZ = static_cast<int32_t>(std::floor(GZ));

            const float H1 = HashInt3(IX, IY, IZ);
            const float H2 = HashInt3(IX + 19, IY - 7, IZ + 31);
            const float H3 = HashInt3(IX - 13, IY + 29, IZ - 5);
            const float MagThresh = std::clamp(Stars.LimitingMagnitude / 8.5f, 0.05f, 0.98f);

            if (H1 > 1.0f - 0.18f * MagThresh)
            {
                const float CX = static_cast<float>(IX) + 0.2f + 0.6f * H2;
                const float CY = static_cast<float>(IY) + 0.2f + 0.6f * H3;
                const float CZ = static_cast<float>(IZ) + 0.2f + 0.6f * std::fmod(H1 * 7.13f, 1.0f);
                const float DX = GX - CX;
                const float DY = GY - CY;
                const float DZ = GZ - CZ;
                const float D2 = DX * DX + DY * DY + DZ * DZ;
                const float Rad = 0.08f * std::clamp(Stars.PointSizeScale, 0.4f, 3.0f);

                if (D2 < Rad * Rad)
                {
                    const float Falloff = std::exp(-D2 / (Rad * Rad * 0.35f));
                    const float Twinkle = Stars.ScintillationEnabled
                        ? (1.0f - (Stars.ScintillationDepthPercent * 0.008f) *
                              (0.5f + 0.5f * std::sin(SimulationSeconds * Stars.ScintillationRateHertz * 5.0f + H2 * 40.0f)))
                        : 1.0f;
                    const float AltFade = std::clamp(RayDirection.Z * 4.0f, 0.0f, 1.0f);
                    const float Intensity = (0.45f + 1.55f * H3 * H3) * Stars.BrightnessMultiplier * DayFade * Twinkle * Falloff * AltFade;
                    Out.R += Intensity * (0.78f + 0.42f * H2);
                    Out.G += Intensity * (0.88f + 0.12f * H2);
                    Out.B += Intensity * (1.20f - 0.35f * H2);
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
