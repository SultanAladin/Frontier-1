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
    uint32_t N = static_cast<uint32_t>(IX) * 374761393u +
                 static_cast<uint32_t>(IY) * 668265263u +
                 static_cast<uint32_t>(IZ) * 1442695041u;
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return static_cast<float>(N) * (1.0f / 4294967296.0f);
}

[[nodiscard]] float SmoothStepCel(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] Mathematics::Axis3D RaDecDegToUnit(float RaHours, float DecDeg) noexcept
{
    const float RaRad  = RaHours * (15.0f * Constants::DegToRadFloat);
    const float DecRad = DecDeg * Constants::DegToRadFloat;
    const float CosD   = std::cos(DecRad);
    return { CosD * std::cos(RaRad), CosD * std::sin(RaRad), std::sin(DecRad) };
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
    float                  ShellThickness = 0.0f;
};

[[nodiscard]] LunarExosphereParams QueryLunarExosphereParams(const LunarBodyConfiguration& Moon) noexcept
{
    LunarExosphereParams P {};
    switch (Moon.SurfacePreset)
    {
        case LunarSurfacePreset::Luna:    P = { { 0.56f, 0.78f, 1.00f }, 0.00f, 0.04f, 0.00f }; break;
        case LunarSurfacePreset::Ember:   P = { { 1.00f, 0.44f, 0.16f }, 0.55f, 0.20f, 0.18f }; break;
        case LunarSurfacePreset::Glacier: P = { { 0.42f, 0.76f, 1.00f }, 0.68f, 0.22f, 0.20f }; break;
        case LunarSurfacePreset::Sulfur:  P = { { 1.00f, 0.84f, 0.32f }, 0.58f, 0.18f, 0.17f }; break;
        case LunarSurfacePreset::Shroud:  P = { { 0.54f, 0.74f, 1.00f }, 0.96f, 0.28f, 0.24f }; break;
        case LunarSurfacePreset::Shard:   P = { { 0.44f, 0.94f, 0.86f }, 0.54f, 0.18f, 0.16f }; break;
    }

    if (Moon.HasAtmosphere)
    {
        const float Inten = std::clamp(Moon.AtmosphereIntensity, 0.0f, 3.0f);
        const float Thick = std::clamp(Moon.AtmosphereThicknessScale, 0.04f, 0.65f);
        P.LimbTint       = Moon.AtmosphereTint;
        P.LimbStrength   = std::max(P.LimbStrength, 0.95f) * Inten;
        P.ShellThickness = std::max(P.ShellThickness, Thick);
        P.TerminatorWrap = std::max(P.TerminatorWrap, 0.26f);
    }
    else
    {
        P.LimbStrength *= std::clamp(Moon.HaloGlowMultiplier, 0.0f, 2.0f);
    }
    return P;
}

} // namespace

Mathematics::LinearRGB LunarStarfieldSequence::EvaluateStarColorFromBv(float BvIndex) noexcept
{
    const float BV = std::clamp(BvIndex, -0.40f, 2.00f);
    float T = 0.0f;
    float R = 1.0f, G = 1.0f, B = 1.0f;
    if (BV < 0.0f)
    {
        T = (BV + 0.40f) / 0.40f;
        R = 0.61f + 0.22f * T;
        G = 0.73f + 0.19f * T;
        B = 1.00f;
    }
    else if (BV < 0.40f)
    {
        T = BV / 0.40f;
        R = 0.83f + 0.17f * T;
        G = 0.92f + 0.07f * T;
        B = 1.00f - 0.05f * T;
    }
    else if (BV < 1.00f)
    {
        T = (BV - 0.40f) / 0.60f;
        R = 1.00f;
        G = 0.99f - 0.17f * T;
        B = 0.95f - 0.42f * T;
    }
    else
    {
        T = (BV - 1.00f) / 1.00f;
        R = 1.00f;
        G = 0.82f - 0.34f * T;
        B = 0.53f - 0.38f * T;
    }
    return { std::pow(std::clamp(R, 0.0f, 1.0f), 1.8f),
             std::pow(std::clamp(G, 0.0f, 1.0f), 1.8f),
             std::pow(std::clamp(B, 0.0f, 1.0f), 1.8f) };
}

std::span<const BrightCatalogStar> LunarStarfieldSequence::QueryJ2000BrightStarCatalog() noexcept
{
    static const std::array<BrightCatalogStar, 40u> Catalog {{
        { RaDecDegToUnit( 6.7525f, -16.7161f), -1.46f,  0.00f }, // Sirius
        { RaDecDegToUnit( 6.3992f, -52.6957f), -0.74f,  0.15f }, // Canopus
        { RaDecDegToUnit(14.6601f, -60.8340f), -0.27f,  0.71f }, // Alpha Centauri
        { RaDecDegToUnit(14.2610f,  19.1824f), -0.05f,  1.23f }, // Arcturus
        { RaDecDegToUnit(18.6156f,  38.7837f),  0.03f,  0.00f }, // Vega
        { RaDecDegToUnit( 5.2781f,  45.9980f),  0.08f,  0.80f }, // Capella
        { RaDecDegToUnit( 5.2423f,  -8.2016f),  0.13f, -0.03f }, // Rigel
        { RaDecDegToUnit( 7.6550f,   5.2250f),  0.34f,  0.42f }, // Procyon
        { RaDecDegToUnit( 1.6286f, -57.2368f),  0.46f, -0.16f }, // Achernar
        { RaDecDegToUnit( 5.9195f,   7.4071f),  0.50f,  1.85f }, // Betelgeuse
        { RaDecDegToUnit(14.0637f, -60.3730f),  0.61f, -0.23f }, // Hadar
        { RaDecDegToUnit(19.8464f,   8.8683f),  0.76f,  0.22f }, // Altair
        { RaDecDegToUnit(12.4433f, -63.0991f),  0.77f, -0.24f }, // Acrux
        { RaDecDegToUnit( 4.5987f,  16.5093f),  0.86f,  1.54f }, // Aldebaran
        { RaDecDegToUnit(13.4199f, -11.1613f),  0.97f, -0.23f }, // Spica
        { RaDecDegToUnit(16.4901f, -26.4320f),  1.06f,  1.83f }, // Antares
        { RaDecDegToUnit( 7.7553f,  28.0262f),  1.14f,  1.00f }, // Pollux
        { RaDecDegToUnit(22.9608f, -29.6222f),  1.16f,  0.09f }, // Fomalhaut
        { RaDecDegToUnit(20.6905f,  45.2803f),  1.25f,  0.09f }, // Deneb
        { RaDecDegToUnit(12.7953f, -59.6888f),  1.25f, -0.23f }, // Mimosa
        { RaDecDegToUnit(10.1395f,  11.9672f),  1.35f, -0.11f }, // Regulus
        { RaDecDegToUnit( 6.9771f, -28.9721f),  1.50f, -0.21f }, // Adhara
        { RaDecDegToUnit( 7.5767f,  31.8883f),  1.58f,  0.03f }, // Castor
        { RaDecDegToUnit(17.5601f, -37.1038f),  1.62f, -0.22f }, // Shaula
        { RaDecDegToUnit( 5.4189f,   6.3497f),  1.64f, -0.22f }, // Bellatrix
        { RaDecDegToUnit( 5.4382f,  28.6075f),  1.65f, -0.13f }, // Elnath
        { RaDecDegToUnit( 5.6036f,  -1.2019f),  1.69f, -0.18f }, // Alnilam
        { RaDecDegToUnit( 5.6793f,  -1.9426f),  1.77f, -0.21f }, // Alnitak
        { RaDecDegToUnit( 5.5334f,  -0.2991f),  2.23f, -0.22f }, // Mintaka
        { RaDecDegToUnit(12.9005f,  55.9598f),  1.77f,  0.02f }, // Alioth
        { RaDecDegToUnit(11.0621f,  61.7510f),  1.79f,  1.07f }, // Dubhe
        { RaDecDegToUnit( 3.4054f,  49.8612f),  1.80f,  0.48f }, // Mirfak
        { RaDecDegToUnit(13.7923f,  49.3133f),  1.86f, -0.19f }, // Alkaid
        { RaDecDegToUnit( 2.5302f,  89.2641f),  1.98f,  0.60f }, // Polaris
        { RaDecDegToUnit( 2.1196f,  23.4624f),  2.00f,  1.15f }, // Hamal
        { RaDecDegToUnit( 0.1398f,  29.0904f),  2.06f, -0.11f }, // Alpheratz
        { RaDecDegToUnit( 5.7959f,  -9.6696f),  2.06f, -0.17f }, // Saiph
        { RaDecDegToUnit( 4.1000f,  24.1000f),  2.87f, -0.09f }, // Alcyone (Pleiades)
        { RaDecDegToUnit(21.7364f,   9.8750f),  2.39f,  1.53f }, // Enif
        { RaDecDegToUnit(18.9211f, -26.2967f),  2.05f, -0.20f }, // Nunki
    }};
    return Catalog;
}

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
    const Mathematics::Axis3D&            RayDirection,
    const Mathematics::Axis3D&            SunDirection,
    std::span<const LunarBodyConfiguration> Moons,
    const StarfieldConfiguration&         Stars,
    float                                 SimulationSeconds,
    const AtmosphericMediumConfiguration* AtmosphereMedium,
    std::span<const BrightCatalogStar>    ExtraCatalogStars) noexcept
{
    Mathematics::LinearRGB Out { 0.0f, 0.0f, 0.0f };
    const auto& Atlas = QueryLunarSurfaceAtlasRgba8();

    const AtmosphericMediumConfiguration DefaultMedium {};
    const AtmosphericMediumConfiguration& Med = (AtmosphereMedium != nullptr) ? *AtmosphereMedium : DefaultMedium;
    const bool OrbitalSpaceView = (AtmosphereMedium != nullptr) && AtmosphereMedium->OrbitalViewEnabled;
    const bool ApplyAtmo = !OrbitalSpaceView && ((AtmosphereMedium == nullptr) || AtmosphereMedium->AtmosphereEnabled);
    const Mathematics::LinearRGB AtmoTrans = (ApplyAtmo && RayDirection.Z > -0.05f)
        ? AtmosphericSkySequence::ComputeSolarTransmittance(
              Mathematics::NormalizeAxis({ RayDirection.X, RayDirection.Y, std::max(0.01f, RayDirection.Z) }),
              Med)
        : Mathematics::LinearRGB{ 1.0f, 1.0f, 1.0f };
    const float DayVis = OrbitalSpaceView ? 0.0f : std::clamp((SunDirection.Z + 0.06f) * 6.5f, 0.0f, 1.0f);

    // 1. Evaluate up to 6 Moon slots (real 2K equirectangular atlas + physical volumetric off-limb atmosphere shell)
    const std::size_t MoonCount = std::min<std::size_t>(Moons.size(), MaxMoonSlots);
    for (std::size_t MIdx = 0u; MIdx < MoonCount; ++MIdx)
    {
        const LunarBodyConfiguration& Moon = Moons[MIdx];
        if (!Moon.Visible || (!OrbitalSpaceView && RayDirection.Z < -0.05f))
        {
            continue;
        }
        const auto MoonDir = ResolveMoonWorldDirection(Moon, SunDirection);
        const float CosM   = std::clamp(Mathematics::EvaluateDot(RayDirection, MoonDir), -1.0f, 1.0f);
        const float AngR   = std::max(0.001f, Moon.AngularDiameterDegrees * 0.5f * Constants::DegToRadFloat);
        const auto  Exo    = QueryLunarExosphereParams(Moon);
        const float MaxShellMul = 1.0f + std::max(0.0f, Exo.ShellThickness);
        const float CosShellEdge = std::cos(std::min(Constants::PiFloat * 0.48f, AngR * MaxShellMul * 1.05f));
        if (CosM <= CosShellEdge)
        {
            continue;
        }

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
        const float RNorm = std::sqrt(R2);

        const float PhaseAng = (Moon.SynodicPhaseFraction - 0.5f) * Constants::TauFloat;
        const float LX = std::sin(PhaseAng);
        const float LY = 0.18f;
        const float LZ = std::cos(PhaseAng);
        const float LL = std::max(1.0e-4f, std::sqrt(LX * LX + LY * LY + LZ * LZ));
        const float SunX = LX / LL, SunY = LY / LL, SunZ = LZ / LL;

        float DiscTransR = 1.0f, DiscTransG = 1.0f, DiscTransB = 1.0f;

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

            const float RawDot = PX * SunX + PY * SunY + PZ * SunZ;
            const float NdotL  = std::max(0.0f, RawDot);

            const float EdgeAa = SmoothStepCel(1.0f, 0.965f, RNorm);
            // Physical Lommel-Seeliger + Hapke regolith BRDF (preserves crater relief without blowing out highlights)
            const float Earthshine = 0.014f * (1.0f - 0.85f * DayVis);
            const float Lommel = NdotL / std::max(0.08f, NdotL + 0.45f * PZ + 0.08f);
            const float PhaseCos = std::clamp(SunZ, -1.0f, 1.0f);
            const float OppSurge = 1.0f + 0.25f * std::exp(-std::acos(PhaseCos) * 5.0f);
            const float Lum = (Earthshine + Lommel * 1.48f * OppSurge) * Moon.BrightnessMultiplier * EdgeAa;

            // On-disc atmospheric scattering column (grows toward limb on sunlit & twilight side)
            const float OnDiscAtmo = (Exo.LimbStrength > 0.001f)
                ? (std::pow(1.0f - PZ, 2.2f) * Exo.LimbStrength *
                   SmoothStepCel(-Exo.TerminatorWrap - 0.05f, 0.55f, RawDot) *
                   Moon.BrightnessMultiplier * EdgeAa * 0.65f)
                : 0.0f;

            // Atmospheric absorption slightly tints/dims surface near limb on thick atmospheres
            const float LimbTau = Exo.LimbStrength * std::pow(1.0f - PZ, 1.6f) * 0.42f;
            DiscTransR = std::exp(-LimbTau * (1.15f - 0.5f * Exo.LimbTint.R));
            DiscTransG = std::exp(-LimbTau * (1.15f - 0.5f * Exo.LimbTint.G));
            DiscTransB = std::exp(-LimbTau * (1.15f - 0.5f * Exo.LimbTint.B));

            Out.R += (TexAlbedo.R * Lum * DiscTransR + Exo.LimbTint.R * OnDiscAtmo) * AtmoTrans.R;
            Out.G += (TexAlbedo.G * Lum * DiscTransG + Exo.LimbTint.G * OnDiscAtmo) * AtmoTrans.G;
            Out.B += (TexAlbedo.B * Lum * DiscTransB + Exo.LimbTint.B * OnDiscAtmo) * AtmoTrans.B;
        }

        // Physical off-limb volumetric atmospheric shell (exponential scale-height chord integral, solar-phase modulated)
        if (Exo.LimbStrength > 0.001f && Exo.ShellThickness > 0.001f && RNorm >= 0.96f && RNorm < MaxShellMul)
        {
            const float LimbAlt = std::max(0.0f, RNorm - 1.0f);
            const float ScaleH  = std::max(0.02f, Exo.ShellThickness * 0.30f);
            const float ChordGeom = std::sqrt(std::max(0.0f, 1.0f - (LimbAlt / Exo.ShellThickness) * (LimbAlt / Exo.ShellThickness)));
            const float RadialDensity = std::exp(-LimbAlt / ScaleH) * ChordGeom * SmoothStepCel(0.955f, 1.005f, RNorm);

            // Limb normal & tangent solar illumination (creates realistic crescent atmosphere glow, dark on night limb)
            const float InvR = 1.0f / std::max(1.0e-4f, RNorm);
            const float NX = PX * InvR, NY = PY * InvR;
            const float LimbDotSun = NX * SunX + NY * SunY + 0.20f * SunZ;
            const float TwilightScatter = SmoothStepCel(-Exo.TerminatorWrap - 0.16f, 0.58f, LimbDotSun);
            const float ForwardScatter  = 0.72f + 0.55f * std::pow(std::max(0.0f, 0.5f - 0.5f * SunZ), 1.6f);

            const float ShellLum = RadialDensity * TwilightScatter * ForwardScatter * Exo.LimbStrength * Moon.BrightnessMultiplier * 0.92f;
            Out.R += Exo.LimbTint.R * ShellLum * AtmoTrans.R;
            Out.G += Exo.LimbTint.G * ShellLum * AtmoTrans.G;
            Out.B += Exo.LimbTint.B * ShellLum * AtmoTrans.B;
        }
    }

    // 2. Pinpoint Optical Starfield (J2000 bright stars + 2-tier procedural cubemap + Planckian B-V color + Milky Way band)
    if (Stars.StarfieldEnabled && (OrbitalSpaceView || RayDirection.Z > -0.02f))
    {
        const float DayFade = OrbitalSpaceView ? 1.0f : std::clamp(1.0f - (SunDirection.Z + 0.08f) * 5.0f, 0.0f, 1.0f);
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

            const float AltFade = OrbitalSpaceView ? 1.0f : std::clamp((RayDirection.Z + 0.01f) * 4.5f, 0.0f, 1.0f);

            // Tilted Milky Way Galactic Plane coordinate
            constexpr float GalPoleX = -0.34f, GalPoleY = 0.74f, GalPoleZ = 0.58f;
            const float GalLat = RotDir.X * GalPoleX + RotDir.Y * GalPoleY + RotDir.Z * GalPoleZ;
            const float GalBandMask = std::exp(-GalLat * GalLat * 18.0f);
            if (Stars.EnableGalacticBand)
            {
                const float Rift = SmoothLatticeNoise3(RotDir.X * 6.0f + 1.4f, RotDir.Y * 6.0f - 0.9f, RotDir.Z * 6.0f) * 0.68f +
                                   SmoothLatticeNoise3(RotDir.X * 14.5f - 0.7f, RotDir.Y * 14.5f + 2.1f, RotDir.Z * 14.5f + 0.5f) * 0.32f;
                const float DustLane = SmoothStepCel(0.34f, 0.66f, Rift) * std::exp(-GalLat * GalLat * 85.0f);
                const float GalClouds = SmoothLatticeNoise3(RotDir.X * 3.4f, RotDir.Y * 3.4f, RotDir.Z * 3.4f);
                const float GalLum = GalBandMask * (0.55f + 0.70f * GalClouds) * (1.0f - 0.65f * DustLane) *
                                     0.032f * Stars.BrightnessMultiplier * DayFade * AltFade;
                Out.R += 0.72f * GalLum * AtmoTrans.R;
                Out.G += 0.80f * GalLum * AtmoTrans.G;
                Out.B += 1.05f * GalLum * AtmoTrans.B;
            }

            const float PixelAng = 1.05f / 480.0f;
            const float CoreBase = 0.000145f * std::clamp(Stars.PointSizeScale, 0.35f, 2.5f);
            const float SigmaCore = std::max(CoreBase, PixelAng * 0.45f);
            const float EnergyNorm = std::clamp((CoreBase * CoreBase * 5.0f) / (SigmaCore * SigmaCore), 0.38f, 1.0f);

            auto AccumulateOpticalStar = [&](
                const Mathematics::Axis3D& StarDir,
                float                      Magnitude,
                float                      BvIndex,
                float                      TwinkleSeed) noexcept
            {
                if (Magnitude > Stars.LimitingMagnitude) return;
                const float Chord = Mathematics::EvaluateLength(RotDir - StarDir);
                if (Chord > SigmaCore * 14.0f) return;

                const float NormR = Chord / SigmaCore;
                const float AiryCore = std::exp(-2.6f * NormR * NormR);
                const float AiryHalo = 0.095f * std::exp(-1.05f * NormR);

                // Subtle 4-blade optical diffraction glint ONLY on bright stars (Mag < 1.8), zero chunky crosses
                float Spike = 0.0f;
                if (Magnitude < 1.8f && Chord < SigmaCore * 13.0f)
                {
                    const Mathematics::Axis3D UpS = (std::abs(StarDir.Z) < 0.95f)
                        ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
                        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
                    const Mathematics::Axis3D StU = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UpS, StarDir));
                    const Mathematics::Axis3D StV = Mathematics::EvaluateCross(StarDir, StU);
                    const Mathematics::Axis3D Delta = RotDir - StarDir;
                    const float DU = Mathematics::EvaluateDot(Delta, StU);
                    const float DV = Mathematics::EvaluateDot(Delta, StV);
                    const float RotU = DU * 0.7071f - DV * 0.7071f;
                    const float RotV = DU * 0.7071f + DV * 0.7071f;
                    const float BladeW = SigmaCore * 0.42f;
                    const float BladeL = SigmaCore * (4.5f + 2.5f * std::max(0.0f, 1.8f - Magnitude));
                    const float S1 = std::exp(-(RotU * RotU) / (BladeW * BladeW)) * std::exp(-std::abs(RotV) / BladeL);
                    const float S2 = std::exp(-(RotV * RotV) / (BladeW * BladeW)) * std::exp(-std::abs(RotU) / BladeL);
                    Spike = (S1 + S2) * 0.22f * std::clamp((1.8f - Magnitude) / 2.5f, 0.0f, 1.0f);
                }

                const float Psf = AiryCore + AiryHalo + Spike;
                if (Psf < 1.0e-4f) return;

                const float Flux = std::pow(10.0f, -0.4f * (Magnitude - 1.2f));
                const float Twinkle = (!OrbitalSpaceView && Stars.ScintillationEnabled)
                    ? (1.0f - (Stars.ScintillationDepthPercent * 0.0075f) *
                          (0.5f + 0.5f * std::sin(SimulationSeconds * Stars.ScintillationRateHertz * 5.0f + TwinkleSeed * 40.0f)))
                    : 1.0f;
                const auto Col = EvaluateStarColorFromBv(BvIndex);
                const float Intensity = std::min(12.0f, Flux * 1.35f) * Stars.BrightnessMultiplier * DayFade * Twinkle * Psf * AltFade * EnergyNorm;
                Out.R += Col.R * Intensity * AtmoTrans.R;
                Out.G += Col.G * Intensity * AtmoTrans.G;
                Out.B += Col.B * Intensity * AtmoTrans.B;
            };

            // 2A. Built-in J2000 & optional custom catalog stars
            if (Stars.EnableBrightStarCatalog)
            {
                for (const BrightCatalogStar& S : QueryJ2000BrightStarCatalog())
                {
                    AccumulateOpticalStar(S.DirectionUnit, S.ApparentMagnitude, S.BvColorIndex, S.DirectionUnit.X * 19.0f + S.DirectionUnit.Y * 7.0f);
                }
            }
            for (const BrightCatalogStar& S : ExtraCatalogStars)
            {
                AccumulateOpticalStar(S.DirectionUnit, S.ApparentMagnitude, S.BvColorIndex, S.DirectionUnit.X * 19.0f + S.DirectionUnit.Y * 7.0f);
            }

            // 2B. Two-tier procedural cubemap deep-field pinpoint stars (concentrated along Galactic Band)
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

            auto EvalCubemapTier = [&](int32_t GridCount, float BaseProb, float MagMin, float MagMax, int32_t SeedBase) noexcept
            {
                const float GridN = static_cast<float>(GridCount);
                const int32_t CX = std::clamp(static_cast<int32_t>(std::floor((FU * 0.5f + 0.5f) * GridN)), 0, GridCount - 1);
                const int32_t CY = std::clamp(static_cast<int32_t>(std::floor((FV * 0.5f + 0.5f) * GridN)), 0, GridCount - 1);
                const float BandBoost = Stars.EnableGalacticBand ? (1.0f + 1.35f * GalBandMask) : 1.0f;
                const float StarProb = std::min(0.65f, BaseProb * BandBoost);

                for (int32_t DY = -1; DY <= 1; ++DY)
                {
                    const int32_t NY = CY + DY;
                    if (NY < 0 || NY >= GridCount) continue;
                    for (int32_t DX = -1; DX <= 1; ++DX)
                    {
                        const int32_t NX = CX + DX;
                        if (NX < 0 || NX >= GridCount) continue;

                        const float H1 = HashInt3(Face * 193 + NX, NY, SeedBase);
                        if (H1 <= 1.0f - StarProb) continue;

                        const float H2 = HashInt3(Face * 193 + NX + 19, NY - 7, SeedBase + 14);
                        const float H3 = HashInt3(Face * 193 + NX - 13, NY + 29, SeedBase + 36);
                        const float H4 = HashInt3(Face * 193 + NX + 41, NY - 23, SeedBase + 72);
                        const float SU = ((static_cast<float>(NX) + 0.18f + 0.64f * H2) / GridN) * 2.0f - 1.0f;
                        const float SV = ((static_cast<float>(NY) + 0.18f + 0.64f * H3) / GridN) * 2.0f - 1.0f;

                        Mathematics::Axis3D StarDir{};
                        if      (Face == 0) StarDir = Mathematics::NormalizeAxis({  1.0f,  SV, -SU });
                        else if (Face == 1) StarDir = Mathematics::NormalizeAxis({ -1.0f,  SV,  SU });
                        else if (Face == 2) StarDir = Mathematics::NormalizeAxis({  SU,  1.0f, -SV });
                        else if (Face == 3) StarDir = Mathematics::NormalizeAxis({  SU, -1.0f,  SV });
                        else if (Face == 4) StarDir = Mathematics::NormalizeAxis({  SU,  SV,  1.0f });
                        else                StarDir = Mathematics::NormalizeAxis({  SU, -SV, -1.0f });

                        const float Rank = std::clamp((H1 - (1.0f - StarProb)) / std::max(1.0e-4f, StarProb), 0.0f, 1.0f);
                        const float Mag  = MagMax - (MagMax - MagMin) * std::pow(Rank, 2.2f);
                        const float BV   = -0.28f + 2.05f * std::pow(H4, 1.35f);
                        AccumulateOpticalStar(StarDir, Mag, BV, H2);
                    }
                }
            };

            const float MagScale = std::clamp(Stars.LimitingMagnitude / 5.5f, 0.15f, 1.8f);
            EvalCubemapTier(72,  0.14f * MagScale, 1.35f, 4.2f, 17);
            EvalCubemapTier(196, 0.28f * MagScale, 3.60f, std::max(4.0f, Stars.LimitingMagnitude), 101);
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster
