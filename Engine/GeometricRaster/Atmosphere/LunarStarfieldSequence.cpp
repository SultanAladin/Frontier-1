/*============================================================================================================================================
                                                      LUNARSTARFIELDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Multi-instance Lunar discs (6 real 2K atlas presets, Lommel-Seeliger regolith, off-limb exospheres) & pinpoint blackbody Starfield

#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"
#include "Engine/GeometricRaster/Atmosphere/AtmosphericSkySequence.h"
#include "Engine/GeometricRaster/Atmosphere/J2000BrightStarRepository.h"
#include "Engine/Codecs/ImageCodec.h"

#if __has_include(<stb_image.h>)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>
#define FRONTIER_HAS_STB_IMAGE 1
#else
#define FRONTIER_HAS_STB_IMAGE 0
#endif

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float HashInt3(int32_t IX, int32_t IY, int32_t IZ) noexcept
{
    // Unsigned integer wrapping prevents signed-overflow UB across compilers
    const uint32_t UX = static_cast<uint32_t>(IX);
    const uint32_t UY = static_cast<uint32_t>(IY);
    const uint32_t UZ = static_cast<uint32_t>(IZ);
    uint32_t N = UX * 374761393u + UY * 668265263u + UZ * 2147483647u;
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
    const float N000 = HashInt3(IX, IY, IZ),         N100 = HashInt3(IX + 1, IY, IZ);
    const float N010 = HashInt3(IX, IY + 1, IZ),     N110 = HashInt3(IX + 1, IY + 1, IZ);
    const float N001 = HashInt3(IX, IY, IZ + 1),     N101 = HashInt3(IX + 1, IY, IZ + 1);
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

[[nodiscard]] bool TryLoadPpmIntoTile(
    const std::filesystem::path& Path,
    std::vector<uint8_t>&        Pixels,
    uint32_t                     PresetIdx)
{
    std::ifstream In(Path, std::ios::binary);
    if (!In.is_open())
    {
        return false;
    }
    std::string Magic;
    uint32_t SrcW = 0u, SrcH = 0u, MaxVal = 0u;
    In >> Magic;
    if (Magic != "P6")
    {
        return false;
    }
    auto SkipComments = [&]()
    {
        while (In >> std::ws && In.peek() == '#')
        {
            std::string Dummy;
            std::getline(In, Dummy);
        }
    };
    SkipComments();
    In >> SrcW;
    SkipComments();
    In >> SrcH;
    SkipComments();
    In >> MaxVal;
    In.get(); // consume single whitespace byte
    if (!In || SrcW == 0u || SrcH == 0u || MaxVal != 255u)
    {
        return false;
    }
    std::vector<uint8_t> SrcRgb(static_cast<std::size_t>(SrcW) * SrcH * 3u);
    In.read(reinterpret_cast<char*>(SrcRgb.data()), static_cast<std::streamsize>(SrcRgb.size()));
    if (!In)
    {
        return false;
    }

    const uint32_t W = LunarStarfieldSequence::AtlasTileWidthPx;
    const uint32_t H = LunarStarfieldSequence::AtlasTileHeightPx;
    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        const float FY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H) * static_cast<float>(SrcH) - 0.5f;
        const uint32_t Y0 = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(std::floor(FY)), 0, static_cast<int32_t>(SrcH) - 1));
        const uint32_t Y1 = std::min(Y0 + 1u, SrcH - 1u);
        const float TY = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float FX = (static_cast<float>(X) + 0.5f) / static_cast<float>(W) * static_cast<float>(SrcW) - 0.5f;
            const uint32_t X0 = static_cast<uint32_t>(((static_cast<int32_t>(std::floor(FX)) % static_cast<int32_t>(SrcW)) + static_cast<int32_t>(SrcW)) % static_cast<int32_t>(SrcW));
            const uint32_t X1 = (X0 + 1u) % SrcW;
            const float TX = std::clamp(FX - std::floor(FX), 0.0f, 1.0f);

            const std::size_t DstIdx = (static_cast<std::size_t>(PresetIdx * H + Y) * W + X) * 4u;
            for (uint32_t C = 0u; C < 3u; ++C)
            {
                const float P00 = static_cast<float>(SrcRgb[(static_cast<std::size_t>(Y0) * SrcW + X0) * 3u + C]);
                const float P10 = static_cast<float>(SrcRgb[(static_cast<std::size_t>(Y0) * SrcW + X1) * 3u + C]);
                const float P01 = static_cast<float>(SrcRgb[(static_cast<std::size_t>(Y1) * SrcW + X0) * 3u + C]);
                const float P11 = static_cast<float>(SrcRgb[(static_cast<std::size_t>(Y1) * SrcW + X1) * 3u + C]);
                const float Val = (P00 + (P10 - P00) * TX) * (1.0f - TY) + (P01 + (P11 - P01) * TX) * TY;
                Pixels[DstIdx + C] = static_cast<uint8_t>(std::clamp(std::lround(Val), 0l, 255l));
            }
            Pixels[DstIdx + 3u] = 255u;
        }
    }
    return true;
}

struct LunarExosphereParams
{
    Mathematics::LinearRGB LimbTint         {};
    float                  LimbStrength     = 0.0f;
    float                  TerminatorWrap   = 0.0f;
    float                  Roughness        = 0.55f;
    float                  LommelSeeligerW  = 0.70f;
    float                  OppositionAmp    = 0.50f;
    float                  OppositionWidth  = 0.075f;
    float                  ReliefStrength   = 1.25f;
    float                  IceSpecular      = 0.0f;
    float                  RayScaleFrac     = 0.25f; // fraction of ExoThick
    float                  MieScaleFrac     = 0.12f; // fraction of ExoThick
    float                  MieAsymmetry     = 0.65f;
    Mathematics::LinearRGB RayScatter       {};
    Mathematics::LinearRGB MieScatter       {};
    Mathematics::LinearRGB MieExtinction    {};
    float                  DetachedHazeFrac = 0.0f;  // fraction of ExoThick
    float                  DetachedWidthFrac = 0.08f;
    Mathematics::LinearRGB DetachedScatter  {};
};

[[nodiscard]] LunarExosphereParams QueryLunarExosphereParams(LunarSurfacePreset Preset) noexcept
{
    switch (Preset)
    {
        case LunarSurfacePreset::Luna:
            return {
                { 0.78f, 0.86f, 0.98f }, 0.06f, 0.04f,
                0.62f, 0.78f, 0.60f, 0.075f, 1.40f, 0.00f,
                0.33f, 0.13f, 0.62f,
                { 0.14f, 0.19f, 0.30f }, { 0.95f, 0.78f, 0.35f }, { 1.10f, 0.92f, 0.46f },
                0.00f, 0.06f, { 0.0f, 0.0f, 0.0f }
            };
        case LunarSurfacePreset::Ember:
            return {
                { 1.00f, 0.44f, 0.16f }, 0.48f, 0.18f,
                0.55f, 0.55f, 0.35f, 0.100f, 1.25f, 0.00f,
                0.16f, 0.07f, 0.70f,
                { 0.35f, 0.82f, 1.98f }, { 2.25f, 1.32f, 0.58f }, { 2.65f, 1.75f, 0.98f },
                0.00f, 0.06f, { 0.0f, 0.0f, 0.0f }
            };
        case LunarSurfacePreset::Glacier:
            return {
                { 0.40f, 0.74f, 1.00f }, 0.62f, 0.22f,
                0.30f, 0.35f, 0.45f, 0.050f, 1.10f, 0.45f,
                0.22f, 0.16f, 0.80f,
                { 0.28f, 0.92f, 1.75f }, { 0.44f, 0.64f, 0.82f }, { 0.48f, 0.68f, 0.86f },
                0.00f, 0.06f, { 0.0f, 0.0f, 0.0f }
            };
        case LunarSurfacePreset::Sulfur:
            return {
                { 1.00f, 0.84f, 0.34f }, 0.52f, 0.18f,
                0.48f, 0.60f, 0.40f, 0.090f, 1.00f, 0.00f,
                0.24f, 0.12f, 0.66f,
                { 0.58f, 0.34f, 1.48f }, { 1.75f, 1.24f, 0.34f }, { 1.98f, 1.54f, 0.72f },
                0.00f, 0.06f, { 0.0f, 0.0f, 0.0f }
            };
        case LunarSurfacePreset::Shroud:
            return {
                { 0.44f, 0.68f, 0.98f }, 0.84f, 0.26f,
                0.50f, 0.40f, 0.15f, 0.200f, 0.75f, 0.00f,
                0.14f, 0.15f, 0.64f,
                { 0.20f, 0.44f, 1.05f }, { 2.45f, 1.55f, 0.58f }, { 3.15f, 2.30f, 1.42f },
                0.56f, 0.14f, { 0.42f, 0.82f, 1.95f }
            };
        case LunarSurfacePreset::Shard:
            return {
                { 0.42f, 0.95f, 0.86f }, 0.48f, 0.16f,
                0.58f, 0.65f, 0.50f, 0.070f, 1.45f, 0.00f,
                0.20f, 0.10f, 0.70f,
                { 0.48f, 0.78f, 1.85f }, { 0.26f, 0.28f, 0.32f }, { 0.34f, 0.36f, 0.42f },
                0.44f, 0.13f, { 0.48f, 0.74f, 1.48f }
            };
    }
    return {
        { 0.78f, 0.86f, 0.98f }, 0.06f, 0.04f,
        0.62f, 0.78f, 0.60f, 0.075f, 1.40f, 0.00f,
        0.33f, 0.13f, 0.62f,
        { 0.14f, 0.19f, 0.30f }, { 0.95f, 0.78f, 0.35f }, { 1.10f, 0.92f, 0.46f },
        0.00f, 0.06f, { 0.0f, 0.0f, 0.0f }
    };
}

[[nodiscard]] float MoonSmoothStep(float Lo, float Hi, float X) noexcept
{
    const float T = std::clamp((X - Lo) / std::max(1.0e-7f, Hi - Lo), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

void ResolveRadialChordSegmentMoon(
    uint32_t Ordinal,
    uint32_t Count,
    float    Start,
    float    End,
    float    ImpactR,
    float&   OutLower,
    float&   OutUpper,
    float&   OutMiddle) noexcept
{
    const float ImpactSq   = ImpactR * ImpactR;
    const float Pivot      = std::clamp(0.0f, Start, End);
    const float PivotRad   = std::sqrt(ImpactSq + Pivot * Pivot);
    const float FrontRange = std::max(0.0f, std::sqrt(ImpactSq + Start * Start) - PivotRad);
    const float RearRange  = std::max(0.0f, std::sqrt(ImpactSq + End * End) - PivotRad);

    uint32_t FrontCount = 0u;
    if (FrontRange > 1.0e-6f && RearRange > 1.0e-6f)
    {
        FrontCount = static_cast<uint32_t>(std::clamp(
            static_cast<int32_t>(std::lround(static_cast<float>(Count) * FrontRange / (FrontRange + RearRange))),
            1,
            static_cast<int32_t>(Count) - 1));
    }
    else if (FrontRange > 1.0e-6f)
    {
        FrontCount = Count;
    }

    auto ParamAtR = [ImpactSq](float Radius, float Sign) noexcept -> float
    {
        return Sign * std::sqrt(std::max(0.0f, Radius * Radius - ImpactSq));
    };

    if (Ordinal < FrontCount)
    {
        const float StartRad = std::sqrt(ImpactSq + Start * Start);
        const float Inc      = FrontRange / static_cast<float>(FrontCount);
        OutLower  = (Ordinal == 0u) ? Start : ParamAtR(StartRad - Inc * static_cast<float>(Ordinal), -1.0f);
        OutUpper  = (Ordinal + 1u == FrontCount) ? Pivot : ParamAtR(StartRad - Inc * static_cast<float>(Ordinal + 1u), -1.0f);
        OutMiddle = ParamAtR(StartRad - Inc * (static_cast<float>(Ordinal) + 0.5f), -1.0f);
    }
    else
    {
        const uint32_t RearOrd   = Ordinal - FrontCount;
        const uint32_t RearCount = Count - FrontCount;
        const float    Inc       = RearRange / static_cast<float>(std::max(1u, RearCount));
        OutLower  = (RearOrd == 0u) ? Pivot : ParamAtR(PivotRad + Inc * static_cast<float>(RearOrd), 1.0f);
        OutUpper  = (RearOrd + 1u == RearCount) ? End : ParamAtR(PivotRad + Inc * static_cast<float>(RearOrd + 1u), 1.0f);
        OutMiddle = ParamAtR(PivotRad + Inc * (static_cast<float>(RearOrd) + 0.5f), 1.0f);
    }
    OutMiddle = std::clamp(OutMiddle, OutLower, OutUpper);
}

struct LunarMediumEval
{
    Mathematics::LinearRGB RayScatter  {};
    Mathematics::LinearRGB MieScatter  {};
    Mathematics::LinearRGB Scattering  {};
    Mathematics::LinearRGB Extinction  {};
};

[[nodiscard]] LunarMediumEval EvaluateLunarExosphereMedium(
    const LunarExosphereParams&   Exo,
    const Mathematics::LinearRGB& ExoTint,
    float                         ExoStr,
    float                         ExoThick,
    float                         AltitudeNorm) noexcept
{
    LunarMediumEval Med {};
    if (AltitudeNorm > ExoThick || ExoStr <= 1.0e-4f)
    {
        return Med;
    }
    const float Alt      = std::max(0.0f, AltitudeNorm);
    const float TBase    = 1.0f - MoonSmoothStep(ExoThick * 0.45f, ExoThick, Alt);
    const float Taper    = TBase * TBase;
    const float HR       = std::max(0.005f, Exo.RayScaleFrac * ExoThick);
    const float HM       = std::max(0.004f, Exo.MieScaleFrac * ExoThick);
    const float DenR     = std::exp(-Alt / HR) * Taper;
    const float DenM     = std::exp(-Alt / HM) * Taper;

    float DenH = 0.0f;
    if (Exo.DetachedHazeFrac > 0.0f)
    {
        const float HazeAlt   = Exo.DetachedHazeFrac * ExoThick;
        const float HazeWidth = std::max(0.005f, Exo.DetachedWidthFrac * ExoThick);
        const float Off       = (Alt - HazeAlt) / HazeWidth;
        DenH = std::exp(-Off * Off) * Taper;
    }

    // Scale normalized optical coefficients cleanly by ExoStr / ExoThick so varying thickness keeps column optical depth coherent
    const float Gain = (ExoStr * 1.15f) / std::max(0.05f, ExoThick);
    const float MixR = 0.55f + 0.45f * ExoTint.R;
    const float MixG = 0.55f + 0.45f * ExoTint.G;
    const float MixB = 0.55f + 0.45f * ExoTint.B;

    Med.RayScatter = {
        Exo.RayScatter.R * DenR * Gain * MixR,
        Exo.RayScatter.G * DenR * Gain * MixG,
        Exo.RayScatter.B * DenR * Gain * MixB,
    };
    const float MieR = (Exo.MieScatter.R * DenM + Exo.DetachedScatter.R * DenH) * Gain;
    const float MieG = (Exo.MieScatter.G * DenM + Exo.DetachedScatter.G * DenH) * Gain;
    const float MieB = (Exo.MieScatter.B * DenM + Exo.DetachedScatter.B * DenH) * Gain;
    Med.MieScatter  = { MieR * MixR, MieG * MixG, MieB * MixB };
    Med.Scattering  = {
        Med.RayScatter.R + Med.MieScatter.R,
        Med.RayScatter.G + Med.MieScatter.G,
        Med.RayScatter.B + Med.MieScatter.B,
    };
    Med.Extinction = {
        Med.RayScatter.R + (Exo.MieExtinction.R * DenM + Exo.DetachedScatter.R * DenH) * Gain,
        Med.RayScatter.G + (Exo.MieExtinction.G * DenM + Exo.DetachedScatter.G * DenH) * Gain,
        Med.RayScatter.B + (Exo.MieExtinction.B * DenM + Exo.DetachedScatter.B * DenH) * Gain,
    };
    return Med;
}

[[nodiscard]] Mathematics::LinearRGB EvaluateLunarSunTransmittanceMoon(
    const LunarExosphereParams&   Exo,
    const Mathematics::LinearRGB& ExoTint,
    float                         ExoStr,
    float                         ExoThick,
    const Mathematics::Axis3D&    SamplePos,
    const Mathematics::Axis3D&    SunLocal,
    uint32_t                      Steps) noexcept
{
    const float AlongSun  = SamplePos.X * SunLocal.X + SamplePos.Y * SunLocal.Y + SamplePos.Z * SunLocal.Z;
    const float RadSq     = SamplePos.X * SamplePos.X + SamplePos.Y * SamplePos.Y + SamplePos.Z * SamplePos.Z;
    const float ImpactSq  = std::max(0.0f, RadSq - AlongSun * AlongSun);

    // True 3D lunar shadow cone + soft solar angular penumbra (0.53 deg) behind dark limb
    float Umbra = 1.0f;
    if (AlongSun < 0.0f)
    {
        const float SunImpact = std::sqrt(ImpactSq);
        const float PenumbraW = 0.008f * std::abs(AlongSun) + 0.015f;
        Umbra = MoonSmoothStep(1.0f - PenumbraW, 1.0f + PenumbraW, SunImpact);
        if (Umbra <= 0.0f)
        {
            return { 0.0f, 0.0f, 0.0f };
        }
    }
    if (ExoStr <= 1.0e-4f)
    {
        return { Umbra, Umbra, Umbra };
    }

    const float TopR     = 1.0f + ExoThick;
    const float ExitDisc = AlongSun * AlongSun - (RadSq - TopR * TopR);
    if (ExitDisc <= 0.0f)
    {
        return { Umbra, Umbra, Umbra };
    }
    const float ExitDist = -AlongSun + std::sqrt(ExitDisc);
    if (ExitDist <= 0.0f)
    {
        return { Umbra, Umbra, Umbra };
    }

    const float SunImpact = std::sqrt(ImpactSq);
    const uint32_t NSteps = std::max(4u, Steps);
    float OptR = 0.0f, OptG = 0.0f, OptB = 0.0f;
    for (uint32_t S = 0u; S < NSteps; ++S)
    {
        float Lo = 0.0f, Hi = 0.0f, Mid = 0.0f;
        ResolveRadialChordSegmentMoon(S, NSteps, AlongSun, ExitDist + AlongSun, SunImpact, Lo, Hi, Mid);
        const float Delta = Hi - Lo;
        const float Shift = Mid - AlongSun;
        const float PX    = SamplePos.X + SunLocal.X * Shift;
        const float PY    = SamplePos.Y + SunLocal.Y * Shift;
        const float PZ    = SamplePos.Z + SunLocal.Z * Shift;
        const float Alt   = std::sqrt(PX * PX + PY * PY + PZ * PZ) - 1.0f;
        const auto  Med   = EvaluateLunarExosphereMedium(Exo, ExoTint, ExoStr, ExoThick, Alt);
        OptR += Med.Extinction.R * Delta;
        OptG += Med.Extinction.G * Delta;
        OptB += Med.Extinction.B * Delta;
    }
    return {
        Umbra * std::exp(-OptR),
        Umbra * std::exp(-OptG),
        Umbra * std::exp(-OptB),
    };
}

[[nodiscard]] float EvaluateLunarRegolithBrdf(
    const Mathematics::Axis3D& ShadingNormal,
    const Mathematics::Axis3D& SunLocal,
    const Mathematics::Axis3D& ViewLocal,
    float                      Roughness,
    float                      LommelSeeligerW) noexcept
{
    const float Mu0 = ShadingNormal.X * SunLocal.X + ShadingNormal.Y * SunLocal.Y + ShadingNormal.Z * SunLocal.Z;
    if (Mu0 <= 0.0f)
    {
        return 0.0f;
    }
    const float MuObs = std::max(1.0e-3f, ShadingNormal.X * ViewLocal.X + ShadingNormal.Y * ViewLocal.Y + ShadingNormal.Z * ViewLocal.Z);
    const float Ls    = 2.0f * Mu0 / (Mu0 + MuObs);

    const float Var   = Roughness * Roughness;
    const float TermA = 1.0f - 0.5f * Var / (Var + 0.33f);
    const float TermB = 0.45f * Var / (Var + 0.09f);
    const Mathematics::Axis3D SunT  = ShadingNormal * (-Mu0) + SunLocal;
    const Mathematics::Axis3D ViewT = ShadingNormal * (-MuObs) + ViewLocal;
    const float LenP  = Mathematics::EvaluateLength(SunT) * Mathematics::EvaluateLength(ViewT);
    const float AzCos = (LenP > 1.0e-6f) ? std::max(0.0f, Mathematics::EvaluateDot(SunT, ViewT) / LenP) : 0.0f;
    const float IncA  = std::acos(std::clamp(Mu0, 0.0f, 1.0f));
    const float EmA   = std::acos(std::clamp(MuObs, 0.0f, 1.0f));
    const float Alpha = std::max(IncA, EmA);
    const float Beta  = std::min(IncA, EmA);
    const float On    = Mu0 * (TermA + TermB * AzCos * std::sin(Alpha) * std::tan(std::min(Beta, 1.5f)));

    const float W = std::clamp(LommelSeeligerW, 0.0f, 1.0f);
    return W * Ls + (1.0f - W) * On;
}

constexpr float SiteSiderealZeroRad = 5.45f * (Constants::TauFloat / 24.0f);
constexpr float SiteObsLatRad       = -26.19f * Constants::DegToRadFloat;
constexpr uint32_t StarCubeCellsPerAxis = 48u;
constexpr uint32_t DeepFieldTotalCount  = 66000u;

[[nodiscard]] Mathematics::Axis3D ConstructEquatorialDir(float RaHours, float DecDeg) noexcept
{
    const float RaRad  = RaHours * (Constants::TauFloat / 24.0f);
    const float DecRad = DecDeg * Constants::DegToRadFloat;
    const float CosD   = std::cos(DecRad);
    return { CosD * std::cos(RaRad), CosD * std::sin(RaRad), std::sin(DecRad) };
}

[[nodiscard]] Mathematics::Axis3D TransformEquatorialToHorizon(
    const Mathematics::Axis3D& EqDir,
    float                      CelestialRotRad) noexcept
{
    const float Lst  = SiteSiderealZeroRad + CelestialRotRad;
    const float Ra   = std::atan2(EqDir.Y, EqDir.X);
    const float Dec  = std::asin(std::clamp(EqDir.Z, -1.0f, 1.0f));
    const float CosD = std::cos(Dec);
    const float H    = Lst - Ra;
    const float SinAlt = std::sin(Dec) * std::sin(SiteObsLatRad) + CosD * std::cos(SiteObsLatRad) * std::cos(H);
    const float Alt    = std::asin(std::clamp(SinAlt, -1.0f, 1.0f));
    const float CosAlt = std::max(1.0e-5f, std::cos(Alt));
    const float CosAz  = (std::sin(Dec) - std::sin(Alt) * std::sin(SiteObsLatRad)) / (CosAlt * std::cos(SiteObsLatRad));
    const float SinAz  = -CosD * std::sin(H) / CosAlt;
    const float AzRad  = std::atan2(SinAz, CosAz);
    return { CosAlt * std::cos(AzRad), -CosAlt * std::sin(AzRad), SinAlt };
}

[[nodiscard]] Mathematics::Axis3D TransformHorizonToEquatorial(
    const Mathematics::Axis3D& HorizonDir,
    float                      CelestialRotRad) noexcept
{
    // Horizon convention: +X = North (Az=0), -Y = East (Az=90), +Z = Up
    const float North = HorizonDir.X;
    const float Up    = HorizonDir.Z;
    const float SinPhi = std::sin(SiteObsLatRad);
    const float CosPhi = std::cos(SiteObsLatRad);

    const float SinDec  = std::clamp(SinPhi * Up + CosPhi * North, -1.0f, 1.0f);
    const float CosDecCosH = CosPhi * Up - SinPhi * North;
    const float CosDecSinH = HorizonDir.Y; // = -East

    const float Lst  = SiteSiderealZeroRad + CelestialRotRad;
    const float CosL = std::cos(Lst);
    const float SinL = std::sin(Lst);
    return Mathematics::NormalizeAxis({
        CosL * CosDecCosH + SinL * CosDecSinH,
        SinL * CosDecCosH - CosL * CosDecSinH,
        SinDec,
    });
}

[[nodiscard]] Mathematics::Axis3D EquatorialRaDecToHorizonDir(float RaHours, float DecDeg) noexcept
{
    return TransformEquatorialToHorizon(ConstructEquatorialDir(RaHours, DecDeg), 0.0f);
}

[[nodiscard]] float HashCelestialWord(uint32_t Word) noexcept
{
    Word ^= Word >> 16u;
    Word *= 0x7FEB352Du;
    Word ^= Word >> 15u;
    Word *= 0x846CA68Bu;
    Word ^= Word >> 16u;
    return static_cast<float>(Word >> 8u) * (1.0f / 16777216.0f);
}

[[nodiscard]] uint32_t HashGalacticCell(int32_t CX, int32_t CY, int32_t CZ, uint32_t Salt) noexcept
{
    uint32_t Mixed = static_cast<uint32_t>(CX) * 0x9E3779B1u ^
                     static_cast<uint32_t>(CY) * 0x85EBCA77u ^
                     static_cast<uint32_t>(CZ) * 0xC2B2AE3Du ^
                     Salt * 0x27D4EB2Fu;
    Mixed ^= Mixed >> 16u;
    Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u;
    Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

[[nodiscard]] float EvaluateGalacticQuintic3(const Mathematics::Axis3D& Pos, uint32_t Salt) noexcept
{
    const float FX = std::floor(Pos.X);
    const float FY = std::floor(Pos.Y);
    const float FZ = std::floor(Pos.Z);
    const int32_t IX = static_cast<int32_t>(FX);
    const int32_t IY = static_cast<int32_t>(FY);
    const int32_t IZ = static_cast<int32_t>(FZ);
    const float TX = Pos.X - FX;
    const float TY = Pos.Y - FY;
    const float TZ = Pos.Z - FZ;
    const auto Quintic = [](float T) noexcept -> float { return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f); };
    const float UX = Quintic(TX);
    const float UY = Quintic(TY);
    const float UZ = Quintic(TZ);

    const auto Lattice = [&](int32_t DX, int32_t DY, int32_t DZ) noexcept -> float
    {
        return static_cast<float>(HashGalacticCell(IX + DX, IY + DY, IZ + DZ, Salt) >> 8u) * (1.0f / 16777216.0f);
    };
    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float C00 = Mix(Lattice(0, 0, 0), Lattice(1, 0, 0), UX);
    const float C10 = Mix(Lattice(0, 1, 0), Lattice(1, 1, 0), UX);
    const float C01 = Mix(Lattice(0, 0, 1), Lattice(1, 0, 1), UX);
    const float C11 = Mix(Lattice(0, 1, 1), Lattice(1, 1, 1), UX);
    return Mix(Mix(C00, C10, UY), Mix(C01, C11, UY), UZ);
}

[[nodiscard]] float EvaluateGalacticFbm3(Mathematics::Axis3D Coord, uint32_t Octaves, uint32_t Salt) noexcept
{
    float Total = 0.0f;
    float Amp   = 0.5f;
    float WSum  = 0.0f;
    for (uint32_t Oct = 0u; Oct < Octaves; ++Oct)
    {
        Total += Amp * EvaluateGalacticQuintic3(Coord, Salt + Oct * 37u);
        WSum  += Amp;
        const Mathematics::Axis3D Rotated {
             0.80f * Coord.X - 0.48f * Coord.Y + 0.36f * Coord.Z,
             0.48f * Coord.X + 0.86f * Coord.Y + 0.16f * Coord.Z,
            -0.36f * Coord.X + 0.04f * Coord.Y + 0.93f * Coord.Z,
        };
        Coord = { Rotated.X * 2.03f + 13.7f, Rotated.Y * 2.03f - 7.1f, Rotated.Z * 2.03f + 5.3f };
        Amp  *= 0.5f;
    }
    return Total / std::max(1.0e-6f, WSum);
}

[[nodiscard]] Mathematics::Axis3D TransformEquatorialToGalactic(const Mathematics::Axis3D& Eq) noexcept
{
    // Exact IAU J2000 Galactic coordinate rotation basis
    return {
        Eq.X * -0.05487557873f + Eq.Y * -0.87343710661f + Eq.Z * -0.48383495212f,
        Eq.X *  0.49410939217f + Eq.Y * -0.44482958317f + Eq.Z *  0.74698221684f,
        Eq.X * -0.86766612530f + Eq.Y * -0.19807630777f + Eq.Z *  0.45598375797f,
    };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateGalacticBulgeRadiance(const Mathematics::Axis3D& Eq) noexcept
{
    const auto Axis = TransformEquatorialToGalactic(Eq);
    const float Lon = std::atan2(Axis.Y, Axis.X);
    const float Lat = std::asin(std::clamp(Axis.Z, -1.0f, 1.0f));
    const auto Lobe = [](float Off, float Wid) noexcept -> float { return std::exp(-(Off * Off) / (Wid * Wid)); };

    const float LonProfile = 0.20f
                           + 0.80f * Lobe(Lon, 0.95f)
                           + 0.32f * Lobe(Lon - 0.47f, 0.22f)
                           + 0.36f * Lobe(Lon - 1.38f, 0.38f)
                           + 0.34f * Lobe(Lon + 1.30f, 0.34f)
                           + 0.12f * Lobe(std::abs(Lon) - 2.6f, 0.6f);
    const float ScaleH    = 0.052f + 0.040f * Lobe(Lon, 0.6f);
    const float CoshVal   = std::cosh(std::clamp(Lat / ScaleH, -20.0f, 20.0f));
    const float DiscProf  = 1.0f / (CoshVal * CoshVal);
    const float ThickDisc = 0.35f * std::exp(-std::abs(Lat) / 0.22f);
    const float Bulge     = 1.45f * std::exp(-(Lon * Lon) / (2.0f * 0.19f * 0.19f) - (Lat * Lat) / (2.0f * 0.13f * 0.13f));

    const float Clouds   = EvaluateGalacticFbm3(Axis * 9.0f, 5u, 1201u);
    const float Fine     = EvaluateGalacticFbm3(Axis * 29.0f, 4u, 1307u);
    const float Clumping = 0.30f + 1.35f * std::pow(Clouds, 1.35f) + 0.45f * std::max(0.0f, Fine - 0.35f);
    const float DiscLum  = LonProfile * (DiscProf + ThickDisc) * Clumping;
    const float BulgeLum = Bulge * (0.45f + 1.05f * Clouds);

    const float RiftCenter = 0.012f + 0.022f * std::sin(2.4f * Lon + 0.6f) + 0.042f * (Clouds - 0.5f);
    const float RiftMask   = MoonSmoothStep(-0.75f, -0.35f, Lon) * (1.0f - MoonSmoothStep(1.35f, 1.65f, Lon));
    const float RiftNoise  = EvaluateGalacticFbm3(Axis * 14.0f, 5u, 1409u);
    const float RiftWidth  = 0.026f + 0.028f * RiftNoise;
    const float RiftDepth  = 2.6f * RiftMask * Lobe(Lat - RiftCenter, RiftWidth) * (0.20f + 1.25f * std::pow(RiftNoise, 1.35f));
    const float Coalsack   = 2.35f * Lobe(Lon + 1.00f, 0.052f) * Lobe(Lat - 0.006f, 0.048f);

    const Mathematics::Axis3D Stretched { Axis.X * 7.0f, Axis.Y * 7.0f, Axis.Z * 26.0f };
    const Mathematics::Axis3D Warp {
        EvaluateGalacticFbm3(Stretched * 0.8f, 3u, 1601u) - 0.5f,
        EvaluateGalacticFbm3(Stretched * 0.8f, 3u, 1607u) - 0.5f,
        EvaluateGalacticFbm3(Stretched * 0.8f, 3u, 1613u) - 0.5f,
    };
    const Mathematics::Axis3D Warped = Stretched + Warp * 1.9f;
    const float RidgeCoarse = 1.0f - std::abs(2.0f * EvaluateGalacticFbm3(Warped, 4u, 1511u) - 1.0f);
    const float RidgeFine   = 1.0f - std::abs(2.0f * EvaluateGalacticFbm3(Warped * 2.7f, 3u, 1523u) - 1.0f);
    const float Filaments   = MoonSmoothStep(0.56f, 0.94f, 0.68f * RidgeCoarse + 0.32f * RidgeFine);
    const float ComplexMask = MoonSmoothStep(0.36f, 0.66f, EvaluateGalacticFbm3(Axis * 4.5f, 3u, 1531u));
    const float Dust        = RiftDepth + Coalsack + 1.45f * Filaments * ComplexMask * Lobe(Lat, 0.13f);

    const float EmR = 0.66f * DiscLum + 1.18f * BulgeLum;
    const float EmG = 0.82f * DiscLum + 0.88f * BulgeLum;
    const float EmB = 1.12f * DiscLum + 0.54f * BulgeLum;
    return {
        EmR * std::exp(-Dust * 0.75f),
        EmG * std::exp(-Dust * 1.00f),
        EmB * std::exp(-Dust * 1.32f),
    };
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDeepSkyNebulae(const Mathematics::Axis3D& Eq) noexcept
{
    const auto ResolveTangent = [](const Mathematics::Axis3D& Dir, const Mathematics::Axis3D& Target, float& OutE, float& OutN) noexcept
    {
        const Mathematics::Axis3D East  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 0.0f, 1.0f }, Target));
        const Mathematics::Axis3D North = Mathematics::EvaluateCross(Target, East);
        const float Fwd = std::max(1.0e-4f, Mathematics::EvaluateDot(Dir, Target));
        OutE = Mathematics::EvaluateDot(Dir, East) / Fwd;
        OutN = Mathematics::EvaluateDot(Dir, North) / Fwd;
    };

    static const auto M42Orion     = ConstructEquatorialDir(5.588f,  -5.39f);
    static const auto M45Pleiades  = ConstructEquatorialDir(3.772f,  24.10f);
    static const auto M31Spiral    = ConstructEquatorialDir(0.712f,  41.27f);
    static const auto MagLarge     = ConstructEquatorialDir(5.392f, -69.76f);
    static const auto MagSmall     = ConstructEquatorialDir(0.877f, -72.83f);
    static const auto M8Lagoon     = ConstructEquatorialDir(18.06f, -24.38f);
    static const auto CarinaNebula = ConstructEquatorialDir(10.75f, -59.87f);

    Mathematics::LinearRGB Out {};
    const float Deg = Constants::DegToRadFloat;
    float East = 0.0f, North = 0.0f;

    if (Mathematics::EvaluateDot(Eq, M42Orion) > 0.995f)
    {
        ResolveTangent(Eq, M42Orion, East, North);
        const float D2  = (East * East + North * North) / (Deg * Deg);
        const float Fil = EvaluateGalacticFbm3(Eq * 900.0f, 4u, 1601u);
        const float Glw = 0.030f * std::exp(-D2 / 0.10f) + 0.010f * std::exp(-D2 / 0.55f) * (0.4f + 1.2f * Fil);
        const float Cav = 0.016f * std::exp(-D2 / 0.012f);
        Out.R += 1.00f * Glw + 0.42f * Cav;
        Out.G += 0.30f * Glw + 0.92f * Cav;
        Out.B += 0.46f * Glw + 0.86f * Cav;
    }
    if (Mathematics::EvaluateDot(Eq, M45Pleiades) > 0.997f)
    {
        ResolveTangent(Eq, M45Pleiades, East, North);
        const float D2   = (East * East + North * North) / (Deg * Deg);
        const float Wisp = EvaluateGalacticFbm3({ Eq.X * 1400.0f, Eq.Y * 380.0f, Eq.Z * 900.0f }, 4u, 1703u);
        const float G    = 0.0045f * std::exp(-D2 / 0.30f) * (0.3f + 1.4f * Wisp);
        Out.R += 0.42f * G;
        Out.G += 0.62f * G;
        Out.B += 1.00f * G;
    }
    if (Mathematics::EvaluateDot(Eq, M31Spiral) > 0.997f)
    {
        ResolveTangent(Eq, M31Spiral, East, North);
        const float PA  = 38.0f * Deg;
        const float Maj = (North * std::cos(PA) + East * std::sin(PA)) / (1.6f * Deg);
        const float Min = (East * std::cos(PA) - North * std::sin(PA)) / (0.42f * Deg);
        const float Ell = std::sqrt(Maj * Maj + Min * Min);
        const float Blg = 0.020f * std::exp(-Ell * Ell * 22.0f);
        const float Dsc = 0.0042f * std::exp(-Ell * 3.2f);
        Out.R += 1.00f * Blg + 0.86f * Dsc;
        Out.G += 0.86f * Blg + 0.88f * Dsc;
        Out.B += 0.70f * Blg + 1.00f * Dsc;
    }
    auto AppendSat = [&](const Mathematics::Axis3D& Target, float RadiusDeg, float Bright, uint32_t Salt)
    {
        if (Mathematics::EvaluateDot(Eq, Target) < 0.97f) return;
        ResolveTangent(Eq, Target, East, North);
        const float D2 = (East * East + North * North) / (Deg * Deg * RadiusDeg * RadiusDeg);
        const float G  = Bright * std::exp(-D2) * (0.35f + 1.3f * EvaluateGalacticFbm3(Eq * 160.0f, 5u, Salt));
        Out.R += 0.86f * G; Out.G += 0.90f * G; Out.B += 1.00f * G;
    };
    AppendSat(MagLarge, 3.4f, 0.0060f, 1801u);
    AppendSat(MagSmall, 1.8f, 0.0035f, 1907u);

    auto AppendKnot = [&](const Mathematics::Axis3D& Target, float RadiusDeg, float Bright, uint32_t Salt)
    {
        if (Mathematics::EvaluateDot(Eq, Target) < 0.996f) return;
        ResolveTangent(Eq, Target, East, North);
        const float D2 = (East * East + North * North) / (Deg * Deg * RadiusDeg * RadiusDeg);
        const float G  = Bright * std::exp(-D2) * (0.3f + 1.4f * EvaluateGalacticFbm3(Eq * 700.0f, 4u, Salt));
        Out.R += 1.00f * G; Out.G += 0.36f * G; Out.B += 0.50f * G;
    };
    AppendKnot(M8Lagoon, 0.45f, 0.010f, 2003u);
    AppendKnot(CarinaNebula, 0.80f, 0.012f, 2111u);
    return Out;
}

[[nodiscard]] float EvaluateStarErf(float Arg) noexcept
{
    const float V = 1.0f / (1.0f + 0.3275911f * std::abs(Arg));
    const float P = (((((1.061405429f * V - 1.453152027f) * V + 1.421413741f) * V - 0.284496736f) * V + 0.254829592f) * V);
    const float R = 1.0f - P * std::exp(-Arg * Arg);
    return (Arg < 0.0f) ? -R : R;
}

// Analytical Erf Airy core (88%) + 4-node Gauss-Legendre Moffat beta=2.5 wing (12%) normalized to unit pixel energy sum
[[nodiscard]] float EvaluateStellarErfMoffatMass(
    float DeltaPxX,
    float DeltaPxY,
    float PointScale) noexcept
{
    const float Scale   = std::clamp(PointScale, 0.35f, 2.5f);
    const float Sigma   = 0.44f * Scale;
    const float Alpha   = 2.10f * Scale;
    const float InvS2   = 1.0f / (1.4142135623730951f * std::max(1.0e-4f, Sigma));
    const float GaussX  = std::max(0.0f, 0.5f * (EvaluateStarErf((DeltaPxX + 0.5f) * InvS2) - EvaluateStarErf((DeltaPxX - 0.5f) * InvS2)));
    const float GaussY  = std::max(0.0f, 0.5f * (EvaluateStarErf((DeltaPxY + 0.5f) * InvS2) - EvaluateStarErf((DeltaPxY - 0.5f) * InvS2)));
    const float Airy    = GaussX * GaussY;

    constexpr std::array<float, 4> Abscissae {{ -0.430568155797f, -0.169990521792f, 0.169990521792f, 0.430568155797f }};
    constexpr std::array<float, 4> Weights   {{  0.173927422569f,  0.326072577431f, 0.326072577431f, 0.173927422569f }};
    const float AlphaSq  = Alpha * Alpha;
    const float MoffNorm = 1.5f / (Constants::PiFloat * AlphaSq);

    float Wing = 0.0f;
    if (std::abs(DeltaPxX) <= 6.0f && std::abs(DeltaPxY) <= 6.0f)
    {
        for (uint32_t GY = 0u; GY < 4u; ++GY)
        {
            const float OY = DeltaPxY + Abscissae[GY];
            for (uint32_t GX = 0u; GX < 4u; ++GX)
            {
                const float OX = DeltaPxX + Abscissae[GX];
                Wing += Weights[GX] * Weights[GY] * MoffNorm * std::pow(1.0f + (OX * OX + OY * OY) / AlphaSq, -2.5f);
            }
        }
    }
    else
    {
        Wing = MoffNorm * std::pow(1.0f + (DeltaPxX * DeltaPxX + DeltaPxY * DeltaPxY) / AlphaSq, -2.5f);
    }
    return 0.88f * Airy + 0.12f * Wing;
}

[[nodiscard]] float EvaluateKastenYoungAirmass(float ElevationSin) noexcept
{
    const float ElevRad = std::asin(std::clamp(ElevationSin, -1.0f, 1.0f));
    const float ElevDeg = std::max(-0.5f, ElevRad * Constants::RadToDegFloat);
    return 1.0f / (std::max(0.0f, ElevationSin) + 0.50572f * std::pow(ElevDeg + 6.07995f, -1.6364f));
}

struct IndexedSkyStar
{
    Mathematics::Axis3D    EquatorialAxis {};
    Mathematics::LinearRGB Chromaticity   {};
    float                  MagnitudeV     = 0.0f;
    float                  PogsonFlux     = 0.0f;
    float                  PhaseSeed      = 0.0f;
    float                  HaloReachRad   = 0.0f;
};

struct StellarCubeSpatialIndex
{
    std::vector<IndexedSkyStar> Stars;
    std::vector<uint32_t>       CellOffsets;
    std::vector<uint32_t>       CellIndices;
    uint32_t                    CatalogCount = 0u;
};

[[nodiscard]] uint32_t ResolveStarCubeCell(const Mathematics::Axis3D& Axis) noexcept
{
    const float AX = std::abs(Axis.X);
    const float AY = std::abs(Axis.Y);
    const float AZ = std::abs(Axis.Z);
    uint32_t Face = 0u;
    float U = 0.0f, V = 0.0f;
    if (AX >= AY && AX >= AZ)
    {
        Face = (Axis.X >= 0.0f) ? 0u : 1u;
        U = Axis.Y / AX;
        V = Axis.Z / AX;
    }
    else if (AY >= AZ)
    {
        Face = (Axis.Y >= 0.0f) ? 2u : 3u;
        U = Axis.X / AY;
        V = Axis.Z / AY;
    }
    else
    {
        Face = (Axis.Z >= 0.0f) ? 4u : 5u;
        U = Axis.X / AZ;
        V = Axis.Y / AZ;
    }
    const uint32_t CU = std::min(StarCubeCellsPerAxis - 1u, static_cast<uint32_t>(std::max(0.0f, (U + 1.0f) * 0.5f * StarCubeCellsPerAxis)));
    const uint32_t CV = std::min(StarCubeCellsPerAxis - 1u, static_cast<uint32_t>(std::max(0.0f, (V + 1.0f) * 0.5f * StarCubeCellsPerAxis)));
    return (Face * StarCubeCellsPerAxis + CV) * StarCubeCellsPerAxis + CU;
}

[[nodiscard]] float EvaluateMilkyWayStarDensityFactor(float Lon, float Lat) noexcept
{
    const float AbsLat = std::abs(Lat);
    const float Thick  = 0.10f + 0.10f * std::exp(-(Lon * Lon) / 0.6f);
    const float Disk   = std::exp(-AbsLat / Thick);
    const float Bulge  = std::exp(-(Lon * Lon) / 0.18f - (Lat * Lat) / 0.04f);
    return std::clamp(0.30f + 0.70f * Disk * (0.60f + 0.40f * std::cos(Lon * 0.5f)) + 0.35f * Bulge, 0.0f, 1.6f);
}

[[nodiscard]] const StellarCubeSpatialIndex& RetrieveStellarCubeSpatialIndex()
{
    static const StellarCubeSpatialIndex Index = []()
    {
        StellarCubeSpatialIndex Out {};
        Out.Stars.reserve(J2000BrightStarCount + DeepFieldTotalCount);

        auto PushStar = [&](const Mathematics::Axis3D& Eq, float MagV, float Bv, uint32_t SeedWord)
        {
            IndexedSkyStar E {};
            E.EquatorialAxis = Mathematics::NormalizeAxis(Eq);
            E.Chromaticity   = LunarStarfieldSequence::EvaluateStarColorFromBv(Bv);
            E.MagnitudeV     = MagV;
            E.PogsonFlux     = 14.0f * std::pow(10.0f, -0.4f * MagV);
            E.PhaseSeed      = HashCelestialWord(SeedWord * 0x9E3779B9u + 17u);
            E.HaloReachRad   = std::clamp(0.0055f * std::pow(10.0f, -0.18f * (MagV - 1.0f)), 0.0030f, 0.035f);
            Out.Stars.push_back(E);
        };

        for (std::size_t I = 0u; I < J2000BrightStarCount; ++I)
        {
            const auto& S = J2000BrightStars[I];
            PushStar(ConstructEquatorialDir(S.RightAscensionHours, S.DeclinationDegrees),
                     S.VisualMagnitude, S.ColorBv, static_cast<uint32_t>(I));
        }
        Out.CatalogCount = static_cast<uint32_t>(J2000BrightStarCount);

        uint32_t Attempt = 0u;
        const float MagSpan = std::pow(10.0f, 0.47f * (8.5f - 5.0f)) - 1.0f;
        while (Out.Stars.size() < J2000BrightStarCount + DeepFieldTotalCount && Attempt < 1200000u)
        {
            ++Attempt;
            const float Vert  = 2.0f * HashCelestialWord(Attempt * 3u + 1u) - 1.0f;
            const float Az    = Constants::TauFloat * HashCelestialWord(Attempt * 3u + 2u);
            const float Rad   = std::sqrt(std::max(0.0f, 1.0f - Vert * Vert));
            const Mathematics::Axis3D Gal { Rad * std::cos(Az), Rad * std::sin(Az), Vert };
            const float Lon = std::atan2(Gal.Y, Gal.X);
            const float Lat = std::asin(Vert);
            if (HashCelestialWord(Attempt * 3u + 3u) * 1.6f > EvaluateMilkyWayStarDensityFactor(Lon, Lat))
            {
                continue;
            }
            // Inverse IAU J2000 Galactic -> Equatorial rotation (transpose of TransformEquatorialToGalactic)
            const Mathematics::Axis3D Eq {
                Gal.X * -0.05487557873f + Gal.Y *  0.49410939217f + Gal.Z * -0.86766612530f,
                Gal.X * -0.87343710661f + Gal.Y * -0.44482958317f + Gal.Z * -0.19807630777f,
                Gal.X * -0.48383495212f + Gal.Y *  0.74698221684f + Gal.Z *  0.45598375797f,
            };
            const float MagV = 5.0f + std::log10(1.0f + HashCelestialWord(Attempt * 7u + 5u) * MagSpan) / 0.47f;
            const float Pick = HashCelestialWord(Attempt * 11u + 7u);
            const float GR   = std::sqrt(-2.0f * std::log(std::max(1.0e-6f, HashCelestialWord(Attempt * 13u + 3u))));
            const float GD   = GR * std::cos(Constants::TauFloat * HashCelestialWord(Attempt * 17u + 9u));
            float Mean = 1.05f, Spread = 0.15f;
            if      (Pick < 0.36f) { Mean = -0.04f; Spread = 0.14f; }
            else if (Pick < 0.74f) { Mean =  0.42f; Spread = 0.16f; }
            else if (Pick > 0.94f) { Mean =  1.55f; Spread = 0.12f; }
            const float Bv = std::clamp(Mean + Spread * GD, -0.35f, 2.0f);
            PushStar(Eq, MagV, Bv, Attempt + 100000u);
        }

        const uint32_t CellCount = 6u * StarCubeCellsPerAxis * StarCubeCellsPerAxis;
        std::vector<std::vector<uint32_t>> Buckets(CellCount);
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Out.Stars.size()); ++Idx)
        {
            const auto& E = Out.Stars[Idx];
            const Mathematics::Axis3D Ref = (std::abs(E.EquatorialAxis.Z) < 0.9f)
                ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
                : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
            const auto TU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Ref, E.EquatorialAxis));
            const auto TV = Mathematics::EvaluateCross(E.EquatorialAxis, TU);
            const float Reach = E.HaloReachRad + 0.005f;

            std::array<uint32_t, 9> Seen {};
            uint32_t SeenN = 0u;
            for (int32_t OV = -1; OV <= 1; ++OV)
            {
                for (int32_t OU = -1; OU <= 1; ++OU)
                {
                    const auto Probe = Mathematics::NormalizeAxis(
                        E.EquatorialAxis + TU * (Reach * static_cast<float>(OU)) + TV * (Reach * static_cast<float>(OV)));
                    const uint32_t Cell = ResolveStarCubeCell(Probe);
                    if (std::find(Seen.begin(), Seen.begin() + SeenN, Cell) == Seen.begin() + SeenN)
                    {
                        Seen[SeenN++] = Cell;
                    }
                }
            }
            for (uint32_t S = 0u; S < SeenN; ++S)
            {
                Buckets[Seen[S]].push_back(Idx);
            }
        }

        Out.CellOffsets.assign(CellCount + 1u, 0u);
        for (uint32_t C = 0u; C < CellCount; ++C)
        {
            Out.CellOffsets[C + 1u] = Out.CellOffsets[C] + static_cast<uint32_t>(Buckets[C].size());
        }
        Out.CellIndices.reserve(Out.CellOffsets[CellCount]);
        for (uint32_t C = 0u; C < CellCount; ++C)
        {
            Out.CellIndices.insert(Out.CellIndices.end(), Buckets[C].begin(), Buckets[C].end());
        }
        return Out;
    }();
    return Index;
}

[[nodiscard]] std::vector<StarfieldRepositoryEntry> BuildJ2000BrightStars()
{
    static constexpr std::array<const char*, 20u> FamousNames {{
        "Sirius", "Canopus", "Arcturus", "RigilKentaurus", "Vega",
        "Capella", "Rigel", "Procyon", "Achernar", "Betelgeuse",
        "Hadar", "Altair", "Acrux", "Aldebaran", "Antares",
        "Spica", "Pollux", "Fomalhaut", "Deneb", "Mimosa",
    }};
    std::vector<StarfieldRepositoryEntry> Catalog;
    Catalog.reserve(J2000BrightStarCount);
    for (std::size_t I = 0u; I < J2000BrightStarCount; ++I)
    {
        const auto& S = J2000BrightStars[I];
        std::string Name = (I < FamousNames.size()) ? FamousNames[I] : ("BSC5-" + std::to_string(I + 1u));
        Catalog.push_back({
            std::move(Name),
            EquatorialRaDecToHorizonDir(S.RightAscensionHours, S.DeclinationDegrees),
            S.VisualMagnitude,
            S.ColorBv,
            LunarStarfieldSequence::EvaluateStarColorFromBv(S.ColorBv),
        });
    }
    return Catalog;
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

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateStarColorFromBv(float BvIndex) noexcept
{
    // Ballesteros (2012) B-V -> effective temperature -> CIE 1931 2-deg Planckian Rec.709 unit-luminance LUT
    static const std::array<Mathematics::LinearRGB, 256u> Palette = []() noexcept
    {
        auto SpecGauss = [](float W, float C, float L, float R) noexcept -> float
        {
            const float D = (W - C) * ((W < C) ? L : R);
            return std::exp(-0.5f * D * D);
        };
        std::array<Mathematics::LinearRGB, 256u> Table {};
        for (uint32_t I = 0u; I < 256u; ++I)
        {
            const float Bv = -0.39f + (2.39f * static_cast<float>(I)) / 255.0f;
            const float Temp = 4600.0f * (1.0f / (0.92f * Bv + 1.70f) + 1.0f / (0.92f * Bv + 0.62f));
            float X = 0.0f, Y = 0.0f, Z = 0.0f;
            for (uint32_t S = 0u; S < 40u; ++S)
            {
                const float W = 385.0f + static_cast<float>(S) * 10.0f;
                const float P = std::pow(550.0f / W, 5.0f) / (std::exp(14387768.77f / (W * Temp)) - 1.0f);
                const float Cx = 1.056f * SpecGauss(W, 599.8f, 0.0264f, 0.0323f) + 0.362f * SpecGauss(W, 442.0f, 0.0624f, 0.0374f) - 0.065f * SpecGauss(W, 501.1f, 0.0490f, 0.0382f);
                const float Cy = 0.821f * SpecGauss(W, 568.8f, 0.0213f, 0.0247f) + 0.286f * SpecGauss(W, 530.9f, 0.0613f, 0.0322f);
                const float Cz = 1.217f * SpecGauss(W, 437.0f, 0.0845f, 0.0278f) + 0.681f * SpecGauss(W, 459.0f, 0.0385f, 0.0725f);
                X += Cx * P;
                Y += Cy * P;
                Z += Cz * P;
            }
            float R = std::max(0.0f,  3.2406f * X - 1.5372f * Y - 0.4986f * Z);
            float G = std::max(0.0f, -0.9689f * X + 1.8758f * Y + 0.0415f * Z);
            float B = std::max(0.0f,  0.0557f * X - 0.2040f * Y + 1.0570f * Z);
            const float MaxC = std::max({ 1.0e-6f, R, G, B });
            Table[I] = { R / MaxC, G / MaxC, B / MaxC };
        }
        return Table;
    }();
    const float U = std::clamp((BvIndex + 0.39f) / 2.39f, 0.0f, 1.0f) * 255.0f;
    const uint32_t I0 = std::min(254u, static_cast<uint32_t>(std::floor(U)));
    const float T = U - static_cast<float>(I0);
    const auto& C0 = Palette[I0];
    const auto& C1 = Palette[I0 + 1u];
    return { C0.R + (C1.R - C0.R) * T, C0.G + (C1.G - C0.G) * T, C0.B + (C1.B - C0.B) * T };
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

    // 1. Primary content-folder path: EngineContent/Skies/LunarSurfaceAtlas.ImageSpace (all 6 real 2K lunar textures)
    {
        const std::array<std::filesystem::path, 3u> ContentCandidates {{
            std::filesystem::path("EngineContent/Skies/LunarSurfaceAtlas.ImageSpace"),
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/Skies/LunarSurfaceAtlas.ImageSpace",
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Content/Optics/LunarSurfaceSheet.ImageSpace",
        }};
        for (const auto& Candidate : ContentCandidates)
        {
            std::error_code Ec {};
            if (!std::filesystem::exists(Candidate, Ec))
            {
                continue;
            }
            Codecs::ImageSpaceDocumentModel Decoded {};
            std::string Diag {};
            if (Codecs::ImageDecoder::DecodeFromFile(Candidate, Decoded, Diag) &&
                Decoded.NominalWidthPx == AtlasTileWidthPx &&
                Decoded.NominalHeightPx == TotalH &&
                Decoded.LinearRgbaPreview.size() == Pixels.size())
            {
                for (std::size_t I = 0u; I < Pixels.size(); ++I)
                {
                    Pixels[I] = static_cast<uint8_t>(std::clamp(std::lround(Decoded.LinearRgbaPreview[I] * 255.0f), 0l, 255l));
                }
                return Pixels;
            }
        }
    }

#if FRONTIER_HAS_STB_IMAGE
    static constexpr std::array<const char*, AtlasPresetCount> TextureFiles {{
        "luna_2k.jpg",
        "ember_2k.jpg",
        "glacier_2k.jpg",
        "sulfur_2k.jpg",
        "shroud_2k.jpg",
        "shard_2k.jpg",
    }};
#endif
    static constexpr std::array<const char*, AtlasPresetCount> PpmFiles {{
        "luna_512x256.ppm",
        "ember_512x256.ppm",
        "glacier_512x256.ppm",
        "sulfur_512x256.ppm",
        "shroud_512x256.ppm",
        "shard_512x256.ppm",
    }};

    const std::filesystem::path MoonTexDir =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/PhotometricEditor/Textures/Moons";

    for (uint32_t P = 0u; P < AtlasPresetCount; ++P)
    {
        bool Loaded = false;
#if FRONTIER_HAS_STB_IMAGE
        const std::filesystem::path TexPath = MoonTexDir / TextureFiles[P];
        int SrcW = 0, SrcH = 0, SrcComp = 0;
        uint8_t* SrcRgba = stbi_load(TexPath.string().c_str(), &SrcW, &SrcH, &SrcComp, 4);
        if (SrcRgba != nullptr && SrcW > 0 && SrcH > 0)
        {
            const uint32_t W = AtlasTileWidthPx;
            const uint32_t H = AtlasTileHeightPx;
            for (uint32_t Y = 0u; Y < H; ++Y)
            {
                const float FY = (static_cast<float>(Y) + 0.5f) / static_cast<float>(H) * static_cast<float>(SrcH) - 0.5f;
                const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, SrcH - 1);
                const int32_t Y1 = std::min(Y0 + 1, SrcH - 1);
                const float TY = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);
                for (uint32_t X = 0u; X < W; ++X)
                {
                    const float FX = (static_cast<float>(X) + 0.5f) / static_cast<float>(W) * static_cast<float>(SrcW) - 0.5f;
                    const int32_t X0 = ((static_cast<int32_t>(std::floor(FX)) % SrcW) + SrcW) % SrcW;
                    const int32_t X1 = (X0 + 1) % SrcW;
                    const float TX = std::clamp(FX - std::floor(FX), 0.0f, 1.0f);

                    const std::size_t DstIdx = (static_cast<std::size_t>(P * H + Y) * W + X) * 4u;
                    for (uint32_t C = 0u; C < 4u; ++C)
                    {
                        const float P00 = static_cast<float>(SrcRgba[(static_cast<std::size_t>(Y0) * SrcW + X0) * 4u + C]);
                        const float P10 = static_cast<float>(SrcRgba[(static_cast<std::size_t>(Y0) * SrcW + X1) * 4u + C]);
                        const float P01 = static_cast<float>(SrcRgba[(static_cast<std::size_t>(Y1) * SrcW + X0) * 4u + C]);
                        const float P11 = static_cast<float>(SrcRgba[(static_cast<std::size_t>(Y1) * SrcW + X1) * 4u + C]);
                        const float Top = P00 + (P10 - P00) * TX;
                        const float Bot = P01 + (P11 - P01) * TX;
                        Pixels[DstIdx + C] = static_cast<uint8_t>(std::clamp(std::lround(Top + (Bot - Top) * TY), 0l, 255l));
                    }
                }
            }
            stbi_image_free(SrcRgba);
            Loaded = true;
        }
#endif
        if (!Loaded)
        {
            Loaded = TryLoadPpmIntoTile(MoonTexDir / PpmFiles[P], Pixels, P) ||
                     TryLoadPpmIntoTile(std::filesystem::path("/tmp/frontier_audit_proof/moons") / PpmFiles[P], Pixels, P);
        }
        if (!Loaded)
        {
            FillProceduralTile(Pixels, P);
        }
    }

    return Pixels;
}

const std::vector<uint8_t>& LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8()
{
    static const std::vector<uint8_t> CachedAtlas = BuildLunarSurfaceAtlasRgba8();
    return CachedAtlas;
}

std::span<const StarfieldRepositoryEntry> LunarStarfieldSequence::QueryJ2000BrightStarRepository() noexcept
{
    static const std::vector<StarfieldRepositoryEntry> Catalog = BuildJ2000BrightStars();
    return Catalog;
}

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateZodiacalLight(
    const Mathematics::Axis3D& EquatorialDirection,
    const Mathematics::Axis3D& SolarEquatorialDirection) noexcept
{
    static const Mathematics::Axis3D EclipticPole = ConstructEquatorialDir(18.0f, 66.56071f);
    static constexpr float ProfileLongitudes[] = {
        15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f, 45.0f, 60.0f,
        75.0f, 90.0f, 105.0f, 120.0f, 140.0f, 160.0f, 170.0f, 180.0f
    };
    static constexpr float ProfileS10[] = {
        9000.0f, 5000.0f, 3500.0f, 2690.0f, 2000.0f, 1440.0f, 1080.0f, 588.0f,
        371.0f, 261.0f, 210.0f, 185.0f, 165.0f, 170.0f, 185.0f, 200.0f
    };
    constexpr float PoleS10 = 12.0f;

    const float LatitudeSin = std::clamp(Mathematics::EvaluateDot(EquatorialDirection, EclipticPole), -1.0f, 1.0f);
    const float LatitudeDeg = std::abs(std::asin(LatitudeSin)) * Constants::RadToDegFloat;
    const Mathematics::Axis3D ViewPlane  = EquatorialDirection - EclipticPole * LatitudeSin;
    const float SolarLatSin = Mathematics::EvaluateDot(SolarEquatorialDirection, EclipticPole);
    const Mathematics::Axis3D SolarPlane = SolarEquatorialDirection - EclipticPole * SolarLatSin;
    const float PlaneLengths = Mathematics::EvaluateLength(ViewPlane) * Mathematics::EvaluateLength(SolarPlane);
    if (PlaneLengths < 1.0e-6f)
    {
        return { PoleS10, 0.93f * PoleS10, 0.80f * PoleS10 };
    }

    const float LongitudeDeg = std::acos(std::clamp(Mathematics::EvaluateDot(ViewPlane, SolarPlane) / PlaneLengths, -1.0f, 1.0f)) * Constants::RadToDegFloat;
    constexpr uint32_t ProfileCount = sizeof(ProfileLongitudes) / sizeof(ProfileLongitudes[0]);
    float EclipticS10 = ProfileS10[0];
    for (uint32_t Knot = 1u; Knot < ProfileCount; ++Knot)
    {
        if (LongitudeDeg <= ProfileLongitudes[Knot])
        {
            const float Span = std::log(ProfileLongitudes[Knot] / ProfileLongitudes[Knot - 1u]);
            const float Mix  = std::clamp(std::log(std::max(LongitudeDeg, ProfileLongitudes[0]) / ProfileLongitudes[Knot - 1u]) / Span, 0.0f, 1.0f);
            EclipticS10 = std::exp(std::log(ProfileS10[Knot - 1u]) + Mix * std::log(ProfileS10[Knot] / ProfileS10[Knot - 1u]));
            break;
        }
    }

    const float AntiSolar   = 180.0f - LongitudeDeg;
    const float Gegenschein = 45.0f * std::exp(-(AntiSolar * AntiSolar + LatitudeDeg * LatitudeDeg) / (2.0f * 7.0f * 7.0f));
    const float Width       = 20.0f + 0.10f * LongitudeDeg;
    const float SmoothLat   = std::sqrt(LatitudeDeg * LatitudeDeg + 16.0f) - 4.0f;
    const float CosLat      = std::sqrt(std::max(0.0f, 1.0f - LatitudeSin * LatitudeSin));
    const float GlareT      = std::clamp((LongitudeDeg - 10.0f) / 12.0f, 0.0f, 1.0f);
    const float GlareFade   = GlareT * GlareT * (3.0f - 2.0f * GlareT);
    const float S10         = PoleS10 + (EclipticS10 - PoleS10) * std::exp(-SmoothLat / Width) * CosLat * GlareFade + Gegenschein;
    return { S10, 0.93f * S10, 0.80f * S10 };
}

StarfieldPresetSpecification LunarStarfieldSequence::BuildStarfieldPreset(StarfieldPresetIdentifier Preset) noexcept
{
    StarfieldPresetSpecification Spec {};
    Spec.Preset                       = Preset;
    Spec.Stars.StarfieldEnabled       = true;
    Spec.Stars.ScintillationEnabled   = true;
    Spec.Stars.LimitingMagnitude      = 7.8f;
    Spec.Stars.BrightnessMultiplier   = 1.85f;
    Spec.Stars.PointSizeScale         = 1.05f;
    Spec.Stars.ScintillationDepthPercent = 28.0f;
    Spec.Stars.ScintillationRateHertz = 1.20f;
    Spec.Stars.EnableBrightStarCatalog = true;
    Spec.Stars.EnableGalacticBand     = true;
    Spec.Stars.ZodiacalLightEnabled   = true;
    Spec.Stars.ZodiacalRadianceScale  = 1.0f;

    switch (Preset)
    {
        case StarfieldPresetIdentifier::GalacticBulge:
            Spec.Title                        = "Galactic Bulge & Great Rift";
            Spec.OutputSlug                   = "GalacticBulge";
            Spec.Stars.CelestialRotationDegrees = 188.25f; // LST 18.0h
            Spec.SunAzimuthDegrees            = 270.0f;
            Spec.SunElevationDegrees          = -40.0f;
            Spec.CameraAzimuthDegrees         = 180.0f;
            Spec.CameraElevationDegrees       = 68.0f;
            Spec.HorizontalFovDegrees         = 82.0f;
            Spec.RecommendedExposure          = 1.85f;
            break;

        case StarfieldPresetIdentifier::ZodiacalLightDusk:
            Spec.Title                        = "Zodiacal Light Dusk";
            Spec.OutputSlug                   = "ZodiacalLightDusk";
            Spec.Stars.CelestialRotationDegrees = 188.25f; // LST 18.0h (steep spring evening ecliptic at -26.19 deg)
            Spec.Stars.ZodiacalRadianceScale  = 1.35f;
            Spec.SunAzimuthDegrees            = 270.0f;  // West horizon (-19 deg astronomical twilight)
            Spec.SunElevationDegrees          = -19.0f;
            Spec.CameraAzimuthDegrees         = 270.0f;
            Spec.CameraElevationDegrees       = 26.0f;
            Spec.HorizontalFovDegrees         = 110.0f;
            Spec.RecommendedExposure          = 1.75f;
            break;

        case StarfieldPresetIdentifier::SouthernCrossCoalsack:
            Spec.Title                        = "Southern Cross & Coalsack";
            Spec.OutputSlug                   = "SouthernCrossCoalsack";
            Spec.Stars.CelestialRotationDegrees = 105.75f; // LST 12.5h (Crux + Coalsack + Alpha/Beta Centauri due South)
            Spec.SunAzimuthDegrees            = 90.0f;
            Spec.SunElevationDegrees          = -40.0f;
            Spec.CameraAzimuthDegrees         = 180.0f;  // South
            Spec.CameraElevationDegrees       = 48.0f;
            Spec.HorizontalFovDegrees         = 70.0f;
            Spec.RecommendedExposure          = 1.80f;
            break;

        case StarfieldPresetIdentifier::WinterHunterBetelgeuse:
            Spec.Title                        = "Winter Hunter Betelgeuse";
            Spec.OutputSlug                   = "WinterHunterBetelgeuse";
            Spec.Stars.CelestialRotationDegrees = 2.25f;   // LST 5.6h (Orion, Betelgeuse, Rigel, Belt, M42, Sirius, Pleiades due North)
            Spec.SunAzimuthDegrees            = 180.0f;
            Spec.SunElevationDegrees          = -40.0f;
            Spec.CameraAzimuthDegrees         = 0.0f;    // North
            Spec.CameraElevationDegrees       = 46.0f;
            Spec.HorizontalFovDegrees         = 72.0f;
            Spec.RecommendedExposure          = 1.80f;
            break;

        case StarfieldPresetIdentifier::MagellanicClouds:
            Spec.Title                        = "Magellanic Clouds";
            Spec.OutputSlug                   = "MagellanicClouds";
            Spec.Stars.CelestialRotationDegrees = 323.25f; // LST 3.0h (LMC, SMC, Achernar, Canopus high in Southern sky)
            Spec.SunAzimuthDegrees            = 0.0f;
            Spec.SunElevationDegrees          = -40.0f;
            Spec.CameraAzimuthDegrees         = 180.0f;  // South
            Spec.CameraElevationDegrees       = 40.0f;
            Spec.HorizontalFovDegrees         = 90.0f;
            Spec.RecommendedExposure          = 1.85f;
            break;
    }

    return Spec;
}

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateCelestialField(
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    std::span<const LunarBodyConfiguration> Moons,
    const StarfieldConfiguration&         Stars,
    float                                 SimulationSeconds,
    const AtmosphericMediumConfiguration* AtmosphereMedium,
    std::span<const StarfieldRepositoryEntry> ExtraRepositoryStars,
    float                                 PixelAngularRad,
    Mathematics::LinearRGB*               OutForegroundTransmittance) noexcept
{
    Mathematics::LinearRGB Out { 0.0f, 0.0f, 0.0f };
    if (OutForegroundTransmittance != nullptr)
    {
        *OutForegroundTransmittance = { 1.0f, 1.0f, 1.0f };
    }
    const auto& Atlas = QueryLunarSurfaceAtlasRgba8();

    const AtmosphericMediumConfiguration DefaultMedium {};
    const AtmosphericMediumConfiguration& Med = (AtmosphereMedium != nullptr) ? *AtmosphereMedium : DefaultMedium;
    const bool OrbitalSpaceView = Med.OrbitalViewEnabled || (Med.ObserverAltitudeMeters > Med.AtmosphereThicknessMeters);
    const bool ApplyAtmo = !OrbitalSpaceView && ((AtmosphereMedium == nullptr) || AtmosphereMedium->AtmosphereEnabled);
    const Mathematics::LinearRGB AtmoTrans = (ApplyAtmo && RayDirection.Z > -0.05f)
        ? AtmosphericSkySequence::ComputeSolarTransmittance(
              Mathematics::NormalizeAxis({ RayDirection.X, RayDirection.Y, std::max(0.01f, RayDirection.Z) }),
              Med,
              Med.ObserverAltitudeMeters)
        : Mathematics::LinearRGB{ 1.0f, 1.0f, 1.0f };
    const float HorizonFactor = OrbitalSpaceView ? 0.0f : std::exp(-std::max(0.0f, RayDirection.Z) * 9.0f);
    const float DayVis        = OrbitalSpaceView ? 0.0f : std::clamp((SunDirection.Z + 0.06f) * 6.5f, 0.0f, 1.0f);

    // 1. Evaluate active Moon slots (up to 6 simultaneously):
    //    eos 3D self-shadowed exosphere + a1dad11b 20-step radial quadrature + Beer-Lambert ViewTransmittance
    //    + Rayleigh / Cornette-Shanks Mie + 4x4 stratified edge Lommel-Seeliger/Oren-Nayar/Hapke regolith AA
    Mathematics::LinearRGB StarTransmittance { 1.0f, 1.0f, 1.0f };
    const std::size_t MoonCount = std::min<std::size_t>(Moons.size(), MaxMoonSlots);
    for (std::size_t MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        const LunarBodyConfiguration& Moon = Moons[MIdx];
        if (!Moon.Visible || (!OrbitalSpaceView && RayDirection.Z < -0.06f))
        {
            continue;
        }
        const auto  MoonDir = ResolveMoonWorldDirection(Moon, SunDirection);
        const float CosM    = std::clamp(Mathematics::EvaluateDot(RayDirection, MoonDir), -1.0f, 1.0f);
        const float AngR    = std::max(0.001f, Moon.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        const float SinAng  = std::max(1.0e-4f, std::sin(AngR));
        const auto  Tint    = QueryPresetTint(Moon.SurfacePreset);
        const auto  ExoDef  = QueryLunarExosphereParams(Moon.SurfacePreset);

        const bool  HasExo   = Moon.HasAtmosphere && (Moon.AtmosphereIntensity > 0.0f);
        const float ExoStr   = HasExo ? std::max(ExoDef.LimbStrength, 0.65f * Moon.AtmosphereIntensity) : 0.0f;
        const float ExoThick = HasExo ? std::clamp(Moon.AtmosphereThicknessScale, 0.06f, 0.45f) : 0.0f;
        const Mathematics::LinearRGB ExoTint {
            Moon.HasAtmosphere ? (0.55f * ExoDef.LimbTint.R + 0.45f * Moon.AtmosphereTint.R) : ExoDef.LimbTint.R,
            Moon.HasAtmosphere ? (0.55f * ExoDef.LimbTint.G + 0.45f * Moon.AtmosphereTint.G) : ExoDef.LimbTint.G,
            Moon.HasAtmosphere ? (0.55f * ExoDef.LimbTint.B + 0.45f * Moon.AtmosphereTint.B) : ExoDef.LimbTint.B,
        };

        // Soft terrestrial Mie atmospheric aureole around bright moons (zero in space vacuum!)
        if (ApplyAtmo)
        {
            const float GlowBoost = 0.45f + 0.55f * std::min(1.8f, Med.MieStrength * (0.4f + 0.6f * HorizonFactor));
            const float Glow = std::pow(std::max(0.0f, CosM), 640.0f) * Moon.HaloGlowMultiplier * 0.065f * Moon.BrightnessMultiplier * GlowBoost;
            Out.R += StarTransmittance.R * (Tint.R * 0.65f + ExoTint.R * 0.35f) * Glow * AtmoTrans.R;
            Out.G += StarTransmittance.G * (Tint.G * 0.65f + ExoTint.G * 0.35f) * Glow * AtmoTrans.G;
            Out.B += StarTransmittance.B * (Tint.B * 0.65f + ExoTint.B * 0.35f) * Glow * AtmoTrans.B;
        }

        const float PixelNorm  = std::max(1.0e-4f, PixelAngularRad) / SinAng;
        const float MaxOuterAng = AngR * (1.0f + ExoThick) + PixelAngularRad;
        if (CosM > std::cos(MaxOuterAng))
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

            const float PX    = Mathematics::EvaluateDot(RayDirection, T) / SinAng;
            const float PY    = Mathematics::EvaluateDot(RayDirection, B) / SinAng;
            const float R2    = PX * PX + PY * PY;
            const float RNorm = std::sqrt(R2);

            const float PhaseAng = (Moon.SynodicPhaseFraction - 0.5f) * Constants::TauFloat;
            const Mathematics::Axis3D SunLocal = Mathematics::NormalizeAxis({ std::sin(PhaseAng), 0.18f, std::cos(PhaseAng) });
            const Mathematics::Axis3D ViewLocal { 0.0f, 0.0f, 1.0f };
            const float Earthshine = 0.016f * (1.0f - 0.78f * DayVis);

            // 1a. 3D Self-Shadowed Exosphere Volumetric Quadrature (20 radius-uniform steps + Beer-Lambert ViewTransmittance)
            const float TopR = 1.0f + ExoThick;
            const float AtmoHalfChord = (RNorm < TopR) ? std::sqrt(std::max(0.0f, TopR * TopR - R2)) : 0.0f;
            const bool  SolidCenterHit = (R2 < 1.0f);
            const float SolidEntry     = SolidCenterHit ? -std::sqrt(std::max(0.0f, 1.0f - R2)) : 0.0f;

            Mathematics::LinearRGB ExoScattered { 0.0f, 0.0f, 0.0f };
            Mathematics::LinearRGB ViewTransmittance { 1.0f, 1.0f, 1.0f };
            if (ExoStr > 0.01f && AtmoHalfChord > 1.0e-5f)
            {
                const float SegStart = -AtmoHalfChord;
                const float SegEnd   = SolidCenterHit ? SolidEntry : AtmoHalfChord;
                const float SegLen   = std::max(0.0f, SegEnd - SegStart);
                if (SegLen > 1.0e-5f)
                {
                    constexpr uint32_t StepCount = 20u;
                    // Ray travels along -ViewLocal = (0, 0, -1), so scattering cosine against DirToSun = -SunLocal.Z
                    const float CosScatter = -SunLocal.Z;
                    const float PhaseR     = (3.0f / (16.0f * Constants::PiFloat)) * (1.0f + CosScatter * CosScatter);
                    const float G          = std::clamp(ExoDef.MieAsymmetry, 0.0f, 0.96f);
                    const float G2         = G * G;
                    const float Denom      = std::max(1.0e-3f, 1.0f + G2 - 2.0f * G * CosScatter);
                    const float PhaseM     = (3.0f / (8.0f * Constants::PiFloat)) * ((1.0f - G2) * (1.0f + CosScatter * CosScatter)) /
                                             ((2.0f + G2) * std::pow(Denom, 1.5f));
                    constexpr float IsoPhase = 1.0f / (4.0f * Constants::PiFloat);

                    for (uint32_t S = 0u; S < StepCount; ++S)
                    {
                        float Lo = 0.0f, Hi = 0.0f, Mid = 0.0f;
                        ResolveRadialChordSegmentMoon(S, StepCount, SegStart, SegEnd, RNorm, Lo, Hi, Mid);
                        const float Delta = Hi - Lo;
                        if (Delta <= 1.0e-6f)
                        {
                            continue;
                        }
                        // SamplePoint in moon-centered frame (s = -Z along line of sight -> s = -AtmoHalfChord is near viewer)
                        const Mathematics::Axis3D SamplePos { PX, PY, -Mid };
                        const float Alt = std::sqrt(SamplePos.X * SamplePos.X + SamplePos.Y * SamplePos.Y + SamplePos.Z * SamplePos.Z) - 1.0f;
                        const auto MedEval = EvaluateLunarExosphereMedium(ExoDef, ExoTint, ExoStr, ExoThick, Alt);
                        if (MedEval.Extinction.R + MedEval.Extinction.G + MedEval.Extinction.B <= 1.0e-9f)
                        {
                            continue;
                        }

                        const auto SunT = EvaluateLunarSunTransmittanceMoon(ExoDef, ExoTint, ExoStr, ExoThick, SamplePos, SunLocal, 8u);
                        const float SunMean = (SunT.R + SunT.G + SunT.B) * 0.333333f;

                        const float SegTR = std::exp(-MedEval.Extinction.R * Delta);
                        const float SegTG = std::exp(-MedEval.Extinction.G * Delta);
                        const float SegTB = std::exp(-MedEval.Extinction.B * Delta);
                        const float IntR  = (1.0f - SegTR) / std::max(MedEval.Extinction.R, 1.0e-8f);
                        const float IntG  = (1.0f - SegTG) / std::max(MedEval.Extinction.G, 1.0e-8f);
                        const float IntB  = (1.0f - SegTB) / std::max(MedEval.Extinction.B, 1.0e-8f);

                        const float SrcR = (MedEval.RayScatter.R * PhaseR + MedEval.MieScatter.R * PhaseM) * SunT.R +
                                           0.30f * MedEval.Scattering.R * IsoPhase * SunMean;
                        const float SrcG = (MedEval.RayScatter.G * PhaseR + MedEval.MieScatter.G * PhaseM) * SunT.G +
                                           0.30f * MedEval.Scattering.G * IsoPhase * SunMean;
                        const float SrcB = (MedEval.RayScatter.B * PhaseR + MedEval.MieScatter.B * PhaseM) * SunT.B +
                                           0.30f * MedEval.Scattering.B * IsoPhase * SunMean;

                        ExoScattered.R += ViewTransmittance.R * SrcR * IntR * Moon.BrightnessMultiplier;
                        ExoScattered.G += ViewTransmittance.G * SrcG * IntG * Moon.BrightnessMultiplier;
                        ExoScattered.B += ViewTransmittance.B * SrcB * IntB * Moon.BrightnessMultiplier;

                        ViewTransmittance.R *= SegTR;
                        ViewTransmittance.G *= SegTG;
                        ViewTransmittance.B *= SegTB;
                    }
                }
            }

            // 1b. Regolith Disc Surface + Edge 4x4 Stratified Lommel-Seeliger/Oren-Nayar/Hapke AA
            const float PitchRad = Moon.SurfacePitchDegrees * Constants::DegToRadFloat;
            const float CosP     = std::cos(PitchRad);
            const float SinP     = std::sin(PitchRad);

            auto EvaluateDiscSample = [&](float SX, float SY) noexcept -> Mathematics::LinearRGB
            {
                const float SZ    = std::sqrt(std::max(0.0f, 1.0f - SX * SX - SY * SY));
                const float SurfY = SY * CosP - SZ * SinP;
                const float SurfZ = SY * SinP + SZ * CosP;

                const float LonU = std::atan2(SX, SurfZ) / Constants::TauFloat + 0.5f;
                const float LatV = std::acos(std::clamp(SurfY, -1.0f, 1.0f)) / Constants::PiFloat;
                const auto  TexAlbedo = SampleAtlasEquirectangular(
                    Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU, LatV);

                // Central-difference luminance-gradient relief normal perturbed along surface East/North
                constexpr float TexU = 1.0f / static_cast<float>(AtlasTileWidthPx);
                constexpr float TexV = 1.0f / static_cast<float>(AtlasTileHeightPx);
                auto TexLum = [](const Mathematics::LinearRGB& C) noexcept -> float
                {
                    return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
                };
                const float HEast  = TexLum(SampleAtlasEquirectangular(Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU + TexU, LatV));
                const float HWest  = TexLum(SampleAtlasEquirectangular(Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU - TexU, LatV));
                const float HNorth = TexLum(SampleAtlasEquirectangular(Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU, std::clamp(LatV - TexV, 0.0f, 1.0f)));
                const float HSouth = TexLum(SampleAtlasEquirectangular(Atlas, static_cast<uint32_t>(Moon.SurfacePreset), LonU, std::clamp(LatV + TexV, 0.0f, 1.0f)));

                const float CosLat = std::max(0.25f, std::sqrt(std::max(0.0f, 1.0f - SurfY * SurfY)));
                const float GradE  = (HEast - HWest) * 0.5f * ExoDef.ReliefStrength * 1.4f / CosLat;
                const float GradN  = (HNorth - HSouth) * 0.5f * ExoDef.ReliefStrength * 1.4f;

                const float Phi     = std::atan2(SX, SurfZ);
                const float SinPhi  = std::sin(Phi);
                const float CosPhi  = std::cos(Phi);
                const Mathematics::Axis3D BodyNormal { SX, SurfY, SurfZ };
                const Mathematics::Axis3D EastBody { CosPhi, 0.0f, -SinPhi };
                const Mathematics::Axis3D NorthBody = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(BodyNormal, EastBody));
                const Mathematics::Axis3D PerturbedBody = Mathematics::NormalizeAxis(
                    BodyNormal - EastBody * GradE - NorthBody * GradN);
                // Rotate PerturbedBody back from pitched coordinates into camera-local moon frame
                const Mathematics::Axis3D ShadingNormal {
                    PerturbedBody.X,
                    PerturbedBody.Y * CosP + PerturbedBody.Z * SinP,
                    -PerturbedBody.Y * SinP + PerturbedBody.Z * CosP,
                };

                const Mathematics::Axis3D SurfaceNormal { SX, SY, SZ };
                const float GeoDot   = Mathematics::EvaluateDot(SurfaceNormal, SunLocal);
                const float TermGate = MoonSmoothStep(-0.06f, 0.02f, GeoDot);
                const float PhaseAbs = std::acos(std::clamp(Mathematics::EvaluateDot(SunLocal, ViewLocal), -1.0f, 1.0f));
                const float HalfTan  = std::tan(std::clamp(0.5f * PhaseAbs, 0.0f, 1.55f));
                const float HapkeOpp = 1.0f + ExoDef.OppositionAmp / (1.0f + HalfTan / std::max(1.0e-3f, ExoDef.OppositionWidth));
                const float Brdf     = EvaluateLunarRegolithBrdf(
                    ShadingNormal, SunLocal, ViewLocal, ExoDef.Roughness, ExoDef.LommelSeeligerW);
                const auto  SurfSunT = (ExoStr > 0.01f)
                    ? EvaluateLunarSunTransmittanceMoon(ExoDef, ExoTint, ExoStr, ExoThick, SurfaceNormal, SunLocal, 6u)
                    : Mathematics::LinearRGB{ 1.0f, 1.0f, 1.0f };

                float Spec = 0.0f;
                if (ExoDef.IceSpecular > 0.0f && GeoDot > 0.0f)
                {
                    const auto HalfV = Mathematics::NormalizeAxis(SunLocal + ViewLocal);
                    const float NH   = std::max(0.0f, Mathematics::EvaluateDot(ShadingNormal, HalfV));
                    const float A2   = 0.22f * 0.22f;
                    const float D    = A2 / (Constants::PiFloat * std::pow(NH * NH * (A2 - 1.0f) + 1.0f, 2.0f));
                    const float VDot = std::clamp(Mathematics::EvaluateDot(ViewLocal, HalfV), 0.0f, 1.0f);
                    const float F    = 0.02f + 0.98f * std::pow(1.0f - VDot, 5.0f);
                    Spec = ExoDef.IceSpecular * D * F * TermGate * 0.25f;
                }

                const float DirectGain = Brdf * HapkeOpp * TermGate * 0.88f * Moon.BrightnessMultiplier;
                const float NightGain  = Earthshine * Moon.BrightnessMultiplier;
                return {
                    (TexAlbedo.R * (DirectGain * SurfSunT.R + NightGain) + Spec * SurfSunT.R * Moon.BrightnessMultiplier),
                    (TexAlbedo.G * (DirectGain * SurfSunT.G + NightGain) + Spec * SurfSunT.G * Moon.BrightnessMultiplier),
                    (TexAlbedo.B * (DirectGain * SurfSunT.B + NightGain) + Spec * SurfSunT.B * Moon.BrightnessMultiplier),
                };
            };

            Mathematics::LinearRGB SurfaceAvg { 0.0f, 0.0f, 0.0f };
            float DiscCoverage = 0.0f;
            const float EdgeBandHalf = 0.75f * PixelNorm;
            if (RNorm <= 1.0f - EdgeBandHalf)
            {
                SurfaceAvg   = EvaluateDiscSample(PX, PY);
                DiscCoverage = 1.0f;
            }
            else if (RNorm <= 1.0f + EdgeBandHalf)
            {
                constexpr uint32_t EdgeGrid = 4u;
                uint32_t HitSamples = 0u;
                for (uint32_t GY = 0u; GY < EdgeGrid; ++GY)
                {
                    const float OY = ((static_cast<float>(GY) + 0.5f) / static_cast<float>(EdgeGrid) - 0.5f) * PixelNorm;
                    for (uint32_t GX = 0u; GX < EdgeGrid; ++GX)
                    {
                        const float OX = ((static_cast<float>(GX) + 0.5f) / static_cast<float>(EdgeGrid) - 0.5f) * PixelNorm;
                        const float SX = PX + OX;
                        const float SY = PY + OY;
                        if (SX * SX + SY * SY >= 1.0f)
                        {
                            continue;
                        }
                        const auto SCol = EvaluateDiscSample(SX, SY);
                        SurfaceAvg.R += SCol.R * (1.0f / 16.0f);
                        SurfaceAvg.G += SCol.G * (1.0f / 16.0f);
                        SurfaceAvg.B += SCol.B * (1.0f / 16.0f);
                        ++HitSamples;
                    }
                }
                DiscCoverage = static_cast<float>(HitSamples) * (1.0f / 16.0f);
            }

            const float OffLimbFrac = 1.0f - DiscCoverage;
            const Mathematics::LinearRGB SlotCol {
                ExoScattered.R + ViewTransmittance.R * SurfaceAvg.R,
                ExoScattered.G + ViewTransmittance.G * SurfaceAvg.G,
                ExoScattered.B + ViewTransmittance.B * SurfaceAvg.B,
            };
            Out.R += StarTransmittance.R * SlotCol.R * AtmoTrans.R;
            Out.G += StarTransmittance.G * SlotCol.G * AtmoTrans.G;
            Out.B += StarTransmittance.B * SlotCol.B * AtmoTrans.B;

            StarTransmittance.R *= OffLimbFrac * ViewTransmittance.R;
            StarTransmittance.G *= OffLimbFrac * ViewTransmittance.G;
            StarTransmittance.B *= OffLimbFrac * ViewTransmittance.B;
        }
    }

    // 2. a1dad11b Galactic Bulge + Great Rift Dust + Nebulae + Kasten-Young X^1.5 Scintillation + 1640 BSC5 / 66K Deep-Field Erf-Moffat PSF
    if (Stars.StarfieldEnabled && (OrbitalSpaceView || RayDirection.Z > -0.02f) &&
        (StarTransmittance.R + StarTransmittance.G + StarTransmittance.B) > 1.0e-5f)
    {
        const float DayFade = OrbitalSpaceView ? 1.0f : std::clamp(1.0f - (SunDirection.Z + 0.08f) * 5.0f, 0.0f, 1.0f);
        if (DayFade > 0.01f)
        {
            const float RotRad  = Stars.CelestialRotationDegrees * Constants::DegToRadFloat;
            const auto  EqDir   = TransformHorizonToEquatorial(RayDirection, RotRad);
            const float AltFade = OrbitalSpaceView ? 1.0f : std::clamp((RayDirection.Z + 0.01f) * 4.5f, 0.0f, 1.0f);
            const float PixelAng = std::max(0.00025f, PixelAngularRad);

            // Orthonormal equatorial sky tangent frame at EqDir for exact sub-pixel Erf + Moffat integration
            const Mathematics::Axis3D RefEq = (std::abs(EqDir.Z) < 0.90f)
                ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
                : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
            const auto TangU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefEq, EqDir));
            const auto TangV = Mathematics::EvaluateCross(EqDir, TangU);

            // Kolmogorov scintillation amplitude proportional to X^1.5 (Kasten & Young 1989 airmass); strictly zero in space vacuum
            float ScintAmp = 0.0f;
            if (Stars.ScintillationEnabled && !OrbitalSpaceView && Stars.ScintillationDepthPercent > 0.0f)
            {
                const float Airmass = EvaluateKastenYoungAirmass(RayDirection.Z);
                ScintAmp = std::clamp((Stars.ScintillationDepthPercent * 0.01f) * 0.12f * std::pow(std::min(Airmass, 38.0f), 1.5f), 0.0f, 0.85f);
            }
            const float ScintFreq = std::max(0.1f, Stars.ScintillationRateHertz);

            // 2a. Galactic Bulge + Great Rift / Coalsack Dust Lanes + Deep-Sky Emission & Reflection Nebulae
            if (Stars.EnableGalacticBand)
            {
                const auto BulgeCol  = EvaluateGalacticBulgeRadiance(EqDir);
                const auto NebulaCol = EvaluateDeepSkyNebulae(EqDir);
                const float Gain     = Stars.BrightnessMultiplier * DayFade * AltFade;
                const float GalR     = (BulgeCol.R * 0.0135f + NebulaCol.R * 1.45f) * Gain;
                const float GalG     = (BulgeCol.G * 0.0135f + NebulaCol.G * 1.45f) * Gain;
                const float GalB     = (BulgeCol.B * 0.0135f + NebulaCol.B * 1.45f) * Gain;
                Out.R += StarTransmittance.R * GalR * AtmoTrans.R;
                Out.G += StarTransmittance.G * GalG * AtmoTrans.G;
                Out.B += StarTransmittance.B * GalB * AtmoTrans.B;
            }

            // 2b. Interplanetary Zodiacal Dust Scatter (Leinert et al. 1998 S10 profile) + Gegenschein Ridge
            if (Stars.ZodiacalLightEnabled && Stars.ZodiacalRadianceScale > 0.0f)
            {
                constexpr float ZodiacalS10Gain = 0.0135f / 533.1f;
                const auto SolarEq  = TransformHorizonToEquatorial(SunDirection, RotRad);
                const auto Zodiacal = EvaluateZodiacalLight(EqDir, SolarEq);
                const float ZGain   = ZodiacalS10Gain * Stars.ZodiacalRadianceScale * Stars.BrightnessMultiplier * DayFade * AltFade;
                Out.R += StarTransmittance.R * Zodiacal.R * ZGain * AtmoTrans.R;
                Out.G += StarTransmittance.G * Zodiacal.G * ZGain * AtmoTrans.G;
                Out.B += StarTransmittance.B * Zodiacal.B * ZGain * AtmoTrans.B;
            }

            auto AccumulateErfMoffatStar = [&](
                const Mathematics::Axis3D& StarEqDir,
                float                      PogsonFlux,
                const Mathematics::LinearRGB& Chromaticity,
                float                      PhaseSeed,
                float                      ReachRad)
            {
                if (Mathematics::EvaluateDot(EqDir, StarEqDir) < 0.9985f)
                {
                    return;
                }
                const float HalfChord = 0.5f * Mathematics::EvaluateLength(EqDir - StarEqDir);
                const float SepRad    = 2.0f * std::asin(std::clamp(HalfChord, 0.0f, 1.0f));
                const float MaxReach  = std::max(ReachRad, 9.0f * PixelAng);
                if (SepRad > MaxReach)
                {
                    return;
                }

                const float DX     = Mathematics::EvaluateDot(StarEqDir, TangU) / PixelAng;
                const float DY     = Mathematics::EvaluateDot(StarEqDir, TangV) / PixelAng;
                const float Mass   = EvaluateStellarErfMoffatMass(DX, DY, Stars.PointSizeScale);

                float Twinkle = 1.0f;
                float Chroma  = 0.0f;
                if (ScintAmp > 0.0f)
                {
                    const float Phase   = PhaseSeed * Constants::TauFloat;
                    const float Flicker = 0.62f * std::sin(SimulationSeconds * ScintFreq * 11.4f + Phase * 3.0f)
                                        + 0.38f * std::sin(SimulationSeconds * ScintFreq * 24.4f + Phase * 7.0f);
                    Twinkle = std::max(0.0f, 1.0f + ScintAmp * Flicker);
                    Chroma  = 0.35f * ScintAmp * std::sin(SimulationSeconds * ScintFreq * 17.6f + Phase * 5.0f);
                }

                const float Lum = PogsonFlux * Stars.BrightnessMultiplier * DayFade * Twinkle * AltFade * Mass;
                Out.R += StarTransmittance.R * Lum * Chromaticity.R * (1.0f + Chroma) * AtmoTrans.R;
                Out.G += StarTransmittance.G * Lum * Chromaticity.G * AtmoTrans.G;
                Out.B += StarTransmittance.B * Lum * Chromaticity.B * (1.0f - Chroma) * AtmoTrans.B;
            };

            // 2b. Cubemap-Indexed 1640 J2000 BSC5 Stars + 66,000 Pogson Deep-Field Stars
            const auto& CubeIndex = RetrieveStellarCubeSpatialIndex();
            const uint32_t Cell   = ResolveStarCubeCell(EqDir);
            const uint32_t First  = CubeIndex.CellOffsets[Cell];
            const uint32_t Last   = CubeIndex.CellOffsets[Cell + 1u];
            const float MagLimit  = Stars.LimitingMagnitude + 0.5f;
            for (uint32_t Cur = First; Cur < Last; ++Cur)
            {
                const auto& E = CubeIndex.Stars[CubeIndex.CellIndices[Cur]];
                const bool IsCatalog = (CubeIndex.CellIndices[Cur] < CubeIndex.CatalogCount);
                if (IsCatalog && !Stars.EnableBrightStarCatalog)
                {
                    continue;
                }
                if (E.MagnitudeV > MagLimit)
                {
                    continue;
                }
                AccumulateErfMoffatStar(E.EquatorialAxis, E.PogsonFlux, E.Chromaticity, E.PhaseSeed, E.HaloReachRad);
            }

            // 2c. Optional Caller-Supplied Horizon Star Catalog Entries
            for (std::size_t I = 0u; I < ExtraRepositoryStars.size(); ++I)
            {
                const auto& S = ExtraRepositoryStars[I];
                if (S.MagnitudeV > MagLimit)
                {
                    continue;
                }
                const auto ExtraEq = TransformHorizonToEquatorial(S.Direction, 0.0f);
                const float Flux   = 14.0f * std::pow(10.0f, -0.4f * S.MagnitudeV);
                const float Reach  = std::clamp(0.0055f * std::pow(10.0f, -0.18f * (S.MagnitudeV - 1.0f)), 0.0030f, 0.035f);
                AccumulateErfMoffatStar(ExtraEq, Flux, S.ColorLinear, static_cast<float>(I) * 0.37f, Reach);
            }
        }
    }

    if (OutForegroundTransmittance != nullptr)
    {
        *OutForegroundTransmittance = StarTransmittance;
    }
    return Out;
}

} // namespace Frontier::GeometricRaster
