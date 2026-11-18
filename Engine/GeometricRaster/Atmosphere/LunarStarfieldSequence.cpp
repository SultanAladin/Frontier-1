/*============================================================================================================================================
                                                      LUNARSTARFIELDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (real 2K texture atlas, 29.53-day phase, roll/pitch) and deterministic twinkling Starfield evaluation

#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

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

[[nodiscard]] float SrgbByteToLinear(uint8_t V) noexcept
{
    const float S = static_cast<float>(V) * (1.0f / 255.0f);
    return (S <= 0.04045f) ? (S / 12.92f) : std::pow((S + 0.055f) / 1.055f, 2.2f);
}

[[nodiscard]] Mathematics::LinearRGB SampleAtlasEquirectangular(
    const std::vector<uint8_t>& Atlas,
    uint32_t                    PresetIdx,
    float                       LonU,
    float                       LatV) noexcept
{
    const uint32_t W = LunarStarfieldSequence::AtlasTileWidthPx;
    const uint32_t H = LunarStarfieldSequence::AtlasTileHeightPx;
    if (Atlas.size() < static_cast<std::size_t>(W) * H * LunarStarfieldSequence::AtlasPresetCount * 4u)
    {
        return { 0.8f, 0.8f, 0.8f };
    }
    const uint32_t P = std::min(PresetIdx, LunarStarfieldSequence::AtlasPresetCount - 1u);

    float WrappedU = LonU - std::floor(LonU);
    if (WrappedU < 0.0f) WrappedU += 1.0f;
    const float ClampedV = std::clamp(LatV, 0.0f, 1.0f);

    const float FX = WrappedU * static_cast<float>(W) - 0.5f;
    const float FY = ClampedV * static_cast<float>(H - 1u);
    const int32_t X0 = ((static_cast<int32_t>(std::floor(FX)) % static_cast<int32_t>(W)) + static_cast<int32_t>(W)) % static_cast<int32_t>(W);
    const int32_t X1 = (X0 + 1) % static_cast<int32_t>(W);
    const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, static_cast<int32_t>(H - 1u));
    const int32_t Y1 = std::min(Y0 + 1, static_cast<int32_t>(H - 1u));
    const float TX   = FX - std::floor(FX);
    const float TY   = FY - std::floor(FY);

    const uint32_t RowOffset = P * H;
    const auto Fetch = [&](int32_t IX, int32_t IY) noexcept -> Mathematics::LinearRGB
    {
        const std::size_t Idx = (static_cast<std::size_t>(RowOffset + static_cast<uint32_t>(IY)) * W + static_cast<uint32_t>(IX)) * 4u;
        return {
            SrgbByteToLinear(Atlas[Idx + 0u]),
            SrgbByteToLinear(Atlas[Idx + 1u]),
            SrgbByteToLinear(Atlas[Idx + 2u]),
        };
    };

    const auto C00 = Fetch(X0, Y0);
    const auto C10 = Fetch(X1, Y0);
    const auto C01 = Fetch(X0, Y1);
    const auto C11 = Fetch(X1, Y1);

    const auto LerpRgb = [](const Mathematics::LinearRGB& A, const Mathematics::LinearRGB& B, float T) noexcept -> Mathematics::LinearRGB
    {
        return { A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T };
    };
    return LerpRgb(LerpRgb(C00, C10, TX), LerpRgb(C01, C11, TX), TY);
}

void FillProceduralTile(std::vector<uint8_t>& Pixels, uint32_t PresetIdx) noexcept
{
    const uint32_t W = LunarStarfieldSequence::AtlasTileWidthPx;
    const uint32_t H = LunarStarfieldSequence::AtlasTileHeightPx;
    const auto Tint = LunarStarfieldSequence::QueryPresetTint(static_cast<LunarSurfacePreset>(PresetIdx));
    const float SeedOff = static_cast<float>(PresetIdx) * 7.13f;

    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
        const float Theta = V * Constants::PiFloat;
        const float SinT = std::sin(Theta);
        const float CosT = std::cos(Theta);
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(W);
            const float Phi = U * Constants::TauFloat;
            const float SX = SinT * std::cos(Phi);
            const float SY = SinT * std::sin(Phi);
            const float SZ = CosT;

            const float MacroMare = SmoothLatticeNoise3(SX * 2.4f + SeedOff, SY * 2.4f - 1.3f, SZ * 2.4f);
            const float Craters   = SmoothLatticeNoise3(SX * 9.5f - SeedOff, SY * 9.5f + 3.1f, SZ * 9.5f);
            const float MicroDust = SmoothLatticeNoise3(SX * 22.0f, SY * 22.0f, SZ * 22.0f + SeedOff);
            const float AlbedoMul = std::clamp(0.56f + 0.32f * MacroMare + 0.14f * Craters - 0.06f * MicroDust, 0.30f, 1.0f);

            const std::size_t Idx = (static_cast<std::size_t>(PresetIdx * H + Y) * W + X) * 4u;
            Pixels[Idx + 0u] = static_cast<uint8_t>(std::clamp(Tint.R * AlbedoMul * 255.0f, 0.0f, 255.0f));
            Pixels[Idx + 1u] = static_cast<uint8_t>(std::clamp(Tint.G * AlbedoMul * 255.0f, 0.0f, 255.0f));
            Pixels[Idx + 2u] = static_cast<uint8_t>(std::clamp(Tint.B * AlbedoMul * 255.0f, 0.0f, 255.0f));
            Pixels[Idx + 3u] = 255u;
        }
    }
}

struct LunarExosphereParams
{
    Mathematics::LinearRGB LimbTint       {};
    float                  LimbStrength   = 0.0f;
    float                  TerminatorWrap = 0.0f;
};

[[nodiscard]] LunarExosphereParams QueryLunarExosphereParams(LunarSurfacePreset Preset) noexcept
{
    switch (Preset)
    {
        case LunarSurfacePreset::Luna:    return { { 0.85f, 0.88f, 0.94f }, 0.05f, 0.03f };
        case LunarSurfacePreset::Ember:   return { { 1.00f, 0.46f, 0.18f }, 0.42f, 0.16f };
        case LunarSurfacePreset::Glacier: return { { 0.44f, 0.74f, 1.00f }, 0.56f, 0.18f };
        case LunarSurfacePreset::Sulfur:  return { { 1.00f, 0.84f, 0.34f }, 0.48f, 0.16f };
        case LunarSurfacePreset::Shroud:  return { { 0.48f, 0.68f, 0.96f }, 0.78f, 0.24f };
        case LunarSurfacePreset::Shard:   return { { 0.46f, 0.95f, 0.86f }, 0.44f, 0.14f };
    }
    return { { 0.85f, 0.88f, 0.94f }, 0.05f, 0.03f };
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
        FillProceduralTile(Pixels, P);
    }
    return Pixels;
}

std::vector<uint8_t> LunarStarfieldSequence::BuildLunarSurfaceAtlasRgba8()
{
    const uint32_t TotalH = AtlasTileHeightPx * AtlasPresetCount;
    std::vector<uint8_t> Pixels(static_cast<std::size_t>(AtlasTileWidthPx) * TotalH * 4u, 255u);

    static constexpr std::array<const char*, AtlasPresetCount> TextureFiles {{
        "luna_2k.jpg",
        "ember_2k.jpg",
        "glacier_2k.jpg",
        "sulfur_2k.jpg",
        "shroud_2k.jpg",
        "shard_2k.jpg",
    }};

    const std::filesystem::path MoonTexDir =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/PhotometricEditor/Textures/Moons";

    for (uint32_t P = 0u; P < AtlasPresetCount; ++P)
    {
        const std::filesystem::path TexPath = MoonTexDir / TextureFiles[P];
        int SrcW = 0, SrcH = 0, SrcComp = 0;
        uint8_t* SrcRgba = std::filesystem::exists(TexPath)
            ? stbi_load(TexPath.string().c_str(), &SrcW, &SrcH, &SrcComp, 4)
            : nullptr;

        if (SrcRgba == nullptr || SrcW <= 0 || SrcH <= 0)
        {
            if (SrcRgba != nullptr) stbi_image_free(SrcRgba);
            FillProceduralTile(Pixels, P);
            continue;
        }

        // High-quality area-box downsample from 2048x1024 -> 512x256 per tile (preserves sharp craters without aliasing)
        const float ScaleX = static_cast<float>(SrcW) / static_cast<float>(AtlasTileWidthPx);
        const float ScaleY = static_cast<float>(SrcH) / static_cast<float>(AtlasTileHeightPx);

        for (uint32_t DY = 0u; DY < AtlasTileHeightPx; ++DY)
        {
            const uint32_t SY0 = static_cast<uint32_t>(std::floor(static_cast<float>(DY) * ScaleY));
            const uint32_t SY1 = std::max(SY0 + 1u, static_cast<uint32_t>(std::ceil(static_cast<float>(DY + 1u) * ScaleY)));
            const uint32_t ClampY1 = std::min<uint32_t>(SY1, static_cast<uint32_t>(SrcH));

            for (uint32_t DX = 0u; DX < AtlasTileWidthPx; ++DX)
            {
                const uint32_t SX0 = static_cast<uint32_t>(std::floor(static_cast<float>(DX) * ScaleX));
                const uint32_t SX1 = std::max(SX0 + 1u, static_cast<uint32_t>(std::ceil(static_cast<float>(DX + 1u) * ScaleX)));
                const uint32_t ClampX1 = std::min<uint32_t>(SX1, static_cast<uint32_t>(SrcW));

                uint32_t SumR = 0u, SumG = 0u, SumB = 0u, Count = 0u;
                for (uint32_t SY = SY0; SY < ClampY1; ++SY)
                {
                    for (uint32_t SX = SX0; SX < ClampX1; ++SX)
                    {
                        const std::size_t SIdx = (static_cast<std::size_t>(SY) * static_cast<uint32_t>(SrcW) + SX) * 4u;
                        SumR += SrcRgba[SIdx + 0u];
                        SumG += SrcRgba[SIdx + 1u];
                        SumB += SrcRgba[SIdx + 2u];
                        ++Count;
                    }
                }
                const uint32_t Div = std::max(1u, Count);
                const std::size_t DIdx = (static_cast<std::size_t>(P * AtlasTileHeightPx + DY) * AtlasTileWidthPx + DX) * 4u;
                Pixels[DIdx + 0u] = static_cast<uint8_t>(SumR / Div);
                Pixels[DIdx + 1u] = static_cast<uint8_t>(SumG / Div);
                Pixels[DIdx + 2u] = static_cast<uint8_t>(SumB / Div);
                Pixels[DIdx + 3u] = 255u;
            }
        }
        stbi_image_free(SrcRgba);
    }

    return Pixels;
}

const std::vector<uint8_t>& LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8()
{
    static const std::vector<uint8_t> CachedAtlas = BuildLunarSurfaceAtlasRgba8();
    return CachedAtlas;
}

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateCelestialField(
    const Mathematics::Axis3D&                  RayDirection,
    const Mathematics::Axis3D&                  SunDirection,
    std::span<const LunarBodyConfiguration, 4u> Moons,
    const StarfieldConfiguration&               Stars,
    float                                       SimulationSeconds,
    const AtmosphericMediumConfiguration*       AtmosphereMedium) noexcept
{
    Mathematics::LinearRGB Out { 0.0f, 0.0f, 0.0f };
    const auto& Atlas = QueryLunarSurfaceAtlasRgba8();

    const AtmosphericMediumConfiguration DefaultMedium {};
    const AtmosphericMediumConfiguration& Med = (AtmosphereMedium != nullptr) ? *AtmosphereMedium : DefaultMedium;
    const bool ApplyAtmo = (AtmosphereMedium == nullptr) || AtmosphereMedium->AtmosphereEnabled;
    const Mathematics::LinearRGB AtmoTrans = (ApplyAtmo && RayDirection.Z > -0.05f)
        ? AtmosphericSkySequence::ComputeSolarTransmittance(
              Mathematics::NormalizeAxis({ RayDirection.X, RayDirection.Y, std::max(0.01f, RayDirection.Z) }),
              Med)
        : Mathematics::LinearRGB{ 1.0f, 1.0f, 1.0f };
    const float HorizonFactor = std::exp(-std::max(0.0f, RayDirection.Z) * 9.0f);
    const float DayVis        = std::clamp((SunDirection.Z + 0.06f) * 6.5f, 0.0f, 1.0f);

    // 1. Evaluate active Moon discs + atmospheric limb/halo using real equirectangular 2K texture atlas
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
        const auto  Exo    = QueryLunarExosphereParams(Moon.SurfacePreset);

        // Earth atmosphere Mie aureole + lunar exosphere corona around disc
        const float GlowBoost = 1.0f + (ApplyAtmo ? (0.45f * Med.MieStrength * HorizonFactor) : 0.0f);
        const float Glow = std::pow(std::max(0.0f, CosM), 380.0f) * Moon.HaloGlowMultiplier * 0.24f * Moon.BrightnessMultiplier * GlowBoost;
        const float HaloR = Tint.R * (1.0f - Exo.LimbStrength * 0.35f) + Exo.LimbTint.R * (Exo.LimbStrength * 0.35f);
        const float HaloG = Tint.G * (1.0f - Exo.LimbStrength * 0.35f) + Exo.LimbTint.G * (Exo.LimbStrength * 0.35f);
        const float HaloB = Tint.B * (1.0f - Exo.LimbStrength * 0.35f) + Exo.LimbTint.B * (Exo.LimbStrength * 0.35f);
        Out.R += HaloR * Glow * AtmoTrans.R;
        Out.G += HaloG * Glow * AtmoTrans.G;
        Out.B += HaloB * Glow * AtmoTrans.B;

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

                // Equirectangular spherical coordinates on the visible lunar hemisphere
                const float LonU = std::atan2(PX, SurfZ) / Constants::TauFloat + 0.5f;
                const float LatV = std::acos(std::clamp(SurfY, -1.0f, 1.0f)) / Constants::PiFloat;
                const auto  TexAlbedo = SampleAtlasEquirectangular(
                    Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU, LatV);

                const float PhaseAng = (Moon.SynodicPhaseFraction - 0.5f) * Constants::TauFloat;
                const float LX = std::sin(PhaseAng);
                const float LY = 0.18f;
                const float LZ = std::cos(PhaseAng);
                const float LL = std::sqrt(LX * LX + LY * LY + LZ * LZ);
                const float RawDot = (PX * LX + PY * LY + PZ * LZ) / std::max(1.0e-4f, LL);
                const float NdotL  = std::max(0.0f, RawDot);

                const float EdgeAa = std::clamp((1.0f - std::sqrt(R2)) * 22.0f, 0.0f, 1.0f);
                // Earthshine masked by daytime foreground sky so the dark lunar side blends naturally into blue sky
                const float Earthshine = 0.022f * (1.0f - 0.78f * DayVis);
                const float LimbBoost  = 0.78f + 0.22f * PZ;
                const float Lum = (Earthshine + NdotL * 1.95f) * LimbBoost * Moon.BrightnessMultiplier * EdgeAa;

                // Per-moon exosphere / planetary limb Rayleigh + Mie terminator scattering
                const float TermWrap = std::clamp((RawDot + Exo.TerminatorWrap) / std::max(0.05f, 1.0f + Exo.TerminatorWrap), 0.0f, 1.0f);
                const float RimAtmo  = std::pow(1.0f - PZ, 2.35f) * Exo.LimbStrength * TermWrap * Moon.BrightnessMultiplier * EdgeAa * 0.75f;

                const float DiscR = TexAlbedo.R * Lum + Exo.LimbTint.R * RimAtmo;
                const float DiscG = TexAlbedo.G * Lum + Exo.LimbTint.G * RimAtmo;
                const float DiscB = TexAlbedo.B * Lum + Exo.LimbTint.B * RimAtmo;

                Out.R += DiscR * AtmoTrans.R;
                Out.G += DiscG * AtmoTrans.G;
                Out.B += DiscB * AtmoTrans.B;
            }
        }
    }

    // 2. Deterministic procedural Starfield (Crisp pinpoint PSF — zero popping under camera rotation!)
    if (Stars.StarfieldEnabled && RayDirection.Z > -0.02f)
    {
        const float DayFade = std::clamp(1.0f - (SunDirection.Z + 0.08f) * 5.0f, 0.0f, 1.0f);
        if (DayFade > 0.01f)
        {
            const float RotRad = Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
            const float CR = std::cos(RotRad);
            const float SR = std::sin(RotRad);
            const Mathematics::Axis3D RotDir = Mathematics::NormalizeAxis(
                Mathematics::Axis3D{
                    RayDirection.X * CR - RayDirection.Y * SR,
                    RayDirection.X * SR + RayDirection.Y * CR,
                    RayDirection.Z,
                });

            const float AX = std::fabs(RotDir.X);
            const float AY = std::fabs(RotDir.Y);
            const float AZ = std::fabs(RotDir.Z);
            int32_t Face = 0;
            float FU = 0.0f;
            float FV = 0.0f;
            if (AX >= AY && AX >= AZ)
            {
                if (RotDir.X >= 0.0f) { Face = 0; FU = -RotDir.Z / AX; FV = RotDir.Y / AX; }
                else                  { Face = 1; FU =  RotDir.Z / AX; FV = RotDir.Y / AX; }
            }
            else if (AY >= AX && AY >= AZ)
            {
                if (RotDir.Y >= 0.0f) { Face = 2; FU = RotDir.X / AY; FV = -RotDir.Z / AY; }
                else                  { Face = 3; FU = RotDir.X / AY; FV =  RotDir.Z / AY; }
            }
            else
            {
                if (RotDir.Z >= 0.0f) { Face = 4; FU = RotDir.X / AZ; FV =  RotDir.Y / AZ; }
                else                  { Face = 5; FU = RotDir.X / AZ; FV = -RotDir.Y / AZ; }
            }

            constexpr int32_t GridCount = 116;
            constexpr float   GridN     = 116.0f;
            const int32_t CX = std::clamp(static_cast<int32_t>(std::floor((FU * 0.5f + 0.5f) * GridN)), 0, GridCount - 1);
            const int32_t CY = std::clamp(static_cast<int32_t>(std::floor((FV * 0.5f + 0.5f) * GridN)), 0, GridCount - 1);

            const float MagThresh  = std::clamp(Stars.LimitingMagnitude / 8.5f, 0.05f, 0.98f);
            const float StarProb   = 0.24f * MagThresh;
            const float PixelAng   = 0.85f / 720.0f;
            const float BaseAng    = 0.00018f * std::clamp(Stars.PointSizeScale, 0.35f, 2.5f);
            const float SigmaAng   = std::max(BaseAng, PixelAng * 0.34f);
            const float EnergyNorm = std::clamp((BaseAng * BaseAng) / (SigmaAng * SigmaAng), 0.14f, 1.0f);
            const float AltFade    = std::clamp((RayDirection.Z + 0.01f) * 4.5f, 0.0f, 1.0f);

            for (int32_t DY = -1; DY <= 1; ++DY)
            {
                const int32_t NY = CY + DY;
                if (NY < 0 || NY >= GridCount) continue;
                for (int32_t DX = -1; DX <= 1; ++DX)
                {
                    const int32_t NX = CX + DX;
                    if (NX < 0 || NX >= GridCount) continue;

                    const float H1 = HashInt3(Face * 131 + NX, NY, 17);
                    if (H1 <= 1.0f - StarProb) continue;

                    const float H2 = HashInt3(Face * 131 + NX + 19, NY - 7, 31);
                    const float H3 = HashInt3(Face * 131 + NX - 13, NY + 29, 53);
                    const float SU = ((static_cast<float>(NX) + 0.20f + 0.60f * H2) / GridN) * 2.0f - 1.0f;
                    const float SV = ((static_cast<float>(NY) + 0.20f + 0.60f * H3) / GridN) * 2.0f - 1.0f;

                    Mathematics::Axis3D StarDir{};
                    if      (Face == 0) StarDir = Mathematics::NormalizeAxis({  1.0f,  SV, -SU });
                    else if (Face == 1) StarDir = Mathematics::NormalizeAxis({ -1.0f,  SV,  SU });
                    else if (Face == 2) StarDir = Mathematics::NormalizeAxis({  SU,  1.0f, -SV });
                    else if (Face == 3) StarDir = Mathematics::NormalizeAxis({  SU, -1.0f,  SV });
                    else if (Face == 4) StarDir = Mathematics::NormalizeAxis({  SU,  SV,  1.0f });
                    else                StarDir = Mathematics::NormalizeAxis({  SU, -SV, -1.0f });

                    const float Chord = Mathematics::EvaluateLength(RotDir - StarDir);
                    if (Chord < SigmaAng * 2.3f)
                    {
                        const float NormR   = Chord / SigmaAng;
                        const float Falloff = std::exp(-1.15f * NormR * NormR);
                        const float Twinkle = Stars.ScintillationEnabled
                            ? (1.0f - (Stars.ScintillationDepthPercent * 0.008f) *
                                  (0.5f + 0.5f * std::sin(SimulationSeconds * Stars.ScintillationRateHertz * 5.0f + H2 * 40.0f)))
                            : 1.0f;
                        const float MagRank   = std::clamp((H1 - (1.0f - StarProb)) / std::max(1.0e-4f, StarProb), 0.0f, 1.0f);
                        const float Intensity = (0.22f + 2.60f * std::pow(MagRank, 2.5f)) * Stars.BrightnessMultiplier * DayFade * Twinkle * Falloff * AltFade * EnergyNorm;
                        Out.R += (0.78f + 0.42f * H2) * Intensity * AtmoTrans.R;
                        Out.G += (0.88f + 0.12f * H2) * Intensity * AtmoTrans.G;
                        Out.B += (1.20f - 0.35f * H2) * Intensity * AtmoTrans.B;
                    }
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
