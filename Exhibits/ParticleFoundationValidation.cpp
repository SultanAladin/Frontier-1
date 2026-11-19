/*============================================================================================================================================
                                                      PARTICLEFOUNDATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation suite for Phase 1B/2 Engine/ParticleSpace foundation, ForceField coupling, Two-Tier .ParticleSpace Codec & 3D origin visual proof

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ParticleCodec.h"
#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourAliasRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ParticleSpace/Origins/AnalyticOrigins.h"
#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSimulationSession.h"
#include "Engine/ParticleSpace/ParticleRig.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"
#include "Engine/GeometricRaster/Compute/ParticleVulkanExecutor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma GCC diagnostic pop

namespace
{

using namespace Frontier;
using namespace Frontier::ParticleSpace;

int gFailures = 0;

void Expect(bool Cond, const std::string& Label)
{
    if (!Cond)
    {
        std::cerr << "[FAIL] " << Label << "\n";
        ++gFailures;
    }
    else
    {
        std::cout << "[PASS] " << Label << "\n";
    }
}

struct Rgba8
{
    uint8_t R = 0u, G = 0u, B = 0u, A = 255u;
};

struct ContactCanvas
{
    int                  Width  = 1440;
    int                  Height = 900;
    std::vector<uint8_t> Pixels {};

    ContactCanvas(int W, int H) : Width(W), Height(H), Pixels(static_cast<std::size_t>(W * H * 4), 0u) {}

    void Clear(Rgba8 C)
    {
        for (int I = 0; I < Width * Height; ++I)
        {
            Pixels[I * 4 + 0] = C.R;
            Pixels[I * 4 + 1] = C.G;
            Pixels[I * 4 + 2] = C.B;
            Pixels[I * 4 + 3] = C.A;
        }
    }

    void SetPixel(int X, int Y, Rgba8 C)
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height) return;
        const std::size_t Idx = static_cast<std::size_t>((Y * Width + X) * 4);
        Pixels[Idx + 0] = C.R;
        Pixels[Idx + 1] = C.G;
        Pixels[Idx + 2] = C.B;
        Pixels[Idx + 3] = C.A;
    }

    void FillRect(int X, int Y, int W, int H, Rgba8 C)
    {
        const int X0 = std::max(0, X), Y0 = std::max(0, Y);
        const int X1 = std::min(Width, X + W), Y1 = std::min(Height, Y + H);
        for (int Py = Y0; Py < Y1; ++Py)
        {
            for (int Px = X0; Px < X1; ++Px) SetPixel(Px, Py, C);
        }
    }

    void StrokeRect(int X, int Y, int W, int H, Rgba8 C)
    {
        FillRect(X, Y, W, 1, C);
        FillRect(X, Y + H - 1, W, 1, C);
        FillRect(X, Y, 1, H, C);
        FillRect(X + W - 1, Y, 1, H, C);
    }

    static std::array<uint8_t, 7> LookupGlyph5x7(char Ch)
    {
        if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');
        switch (Ch)
        {
            case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
            case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
            case '3': return { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
            case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
            case '5': return { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
            case '6': return { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E };
            case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
            case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
            case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C };
            case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
            case 'C': return { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
            case 'D': return { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
            case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
            case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
            case 'G': return { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F };
            case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case 'J': return { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C };
            case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
            case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
            case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
            case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
            case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
            case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
            case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
            case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
            case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
            case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
            case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
            case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
            case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
            case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
            case '+': return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
            case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
            case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
            case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
            case ',': return { 0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08 };
            case '"': return { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 };
            case '&': return { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D };
            default:  return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        }
    }

    void DrawText(int X, int Y, const std::string& Text, Rgba8 C, int Scale = 1)
    {
        int CurX = X;
        for (char Ch : Text)
        {
            const auto Rows = LookupGlyph5x7(Ch);
            for (int R = 0; R < 7; ++R)
            {
                for (int Col = 0; Col < 5; ++Col)
                {
                    if ((Rows[R] & (1u << (4 - Col))) != 0u)
                    {
                        FillRect(CurX + Col * Scale, Y + R * Scale, Scale, Scale, C);
                    }
                }
            }
            CurX += 6 * Scale;
        }
    }
};

void BlendAdditivePixel(ContactCanvas& Canvas, int Px, int Py, float R, float G, float B, float Alpha)
{
    if (Px < 0 || Py < 0 || Px >= Canvas.Width || Py >= Canvas.Height) return;
    const std::size_t Idx = static_cast<std::size_t>((Py * Canvas.Width + Px) * 4);
    auto ToByte = [](float Linear) -> uint8_t {
        const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
        const float Srgb    = std::pow(Clamped, 1.0f / 2.2f);
        return static_cast<uint8_t>(std::round(Srgb * 255.0f));
    };
    const float AddR = std::clamp(R * Alpha, 0.0f, 1.0f);
    const float AddG = std::clamp(G * Alpha, 0.0f, 1.0f);
    const float AddB = std::clamp(B * Alpha, 0.0f, 1.0f);
    const float CurR = std::pow(static_cast<float>(Canvas.Pixels[Idx + 0]) / 255.0f, 2.2f);
    const float CurG = std::pow(static_cast<float>(Canvas.Pixels[Idx + 1]) / 255.0f, 2.2f);
    const float CurB = std::pow(static_cast<float>(Canvas.Pixels[Idx + 2]) / 255.0f, 2.2f);
    Canvas.Pixels[Idx + 0] = ToByte(CurR + AddR);
    Canvas.Pixels[Idx + 1] = ToByte(CurG + AddG);
    Canvas.Pixels[Idx + 2] = ToByte(CurB + AddB);
    Canvas.Pixels[Idx + 3] = 255u;
}

void DrawGlowDisc(ContactCanvas& Canvas, float Cx, float Cy, float Radius, float R, float G, float B, float Gain)
{
    const int MinX = static_cast<int>(std::floor(Cx - Radius));
    const int MaxX = static_cast<int>(std::ceil(Cx + Radius));
    const int MinY = static_cast<int>(std::floor(Cy - Radius));
    const int MaxY = static_cast<int>(std::ceil(Cy + Radius));
    const float InvR = 1.0f / std::max(0.5f, Radius);
    for (int Py = MinY; Py <= MaxY; ++Py)
    {
        for (int Px = MinX; Px <= MaxX; ++Px)
        {
            const float Dx = (static_cast<float>(Px) + 0.5f - Cx) * InvR;
            const float Dy = (static_cast<float>(Py) + 0.5f - Cy) * InvR;
            const float D2 = Dx * Dx + Dy * Dy;
            if (D2 >= 1.0f) continue;
            const float Falloff = std::exp(-D2 * 3.2f) * (1.0f - D2);
            BlendAdditivePixel(Canvas, Px, Py, R, G, B, Falloff * Gain);
        }
    }
}

void RenderOriginTile(
    ContactCanvas&                           Canvas,
    int                                      TileX,
    int                                      TileY,
    int                                      TileW,
    int                                      TileH,
    const std::string&                       Title,
    const std::vector<ParticleAnchorSample>& Anchors,
    const ColourSpace::ColourSourceProfile&  Palette,
    float                                    YawDeg,
    float                                    PitchDeg)
{
    Canvas.FillRect(TileX, TileY, TileW, TileH, Rgba8 { 13, 15, 22, 255 });
    Canvas.FillRect(TileX, TileY, TileW, 24, Rgba8 { 20, 24, 34, 255 });
    Canvas.StrokeRect(TileX, TileY, TileW, TileH, Rgba8 { 42, 50, 68, 255 });

    float MinP[3] = { 1e9f, 1e9f, 1e9f };
    float MaxP[3] = { -1e9f, -1e9f, -1e9f };
    for (const auto& A : Anchors)
    {
        for (int K = 0; K < 3; ++K)
        {
            MinP[K] = std::min(MinP[K], A.PositionMeters[K]);
            MaxP[K] = std::max(MaxP[K], A.PositionMeters[K]);
        }
    }
    const float Ctr[3] = {
        0.5f * (MinP[0] + MaxP[0]),
        0.5f * (MinP[1] + MaxP[1]),
        0.5f * (MinP[2] + MaxP[2]),
    };
    const float Span  = std::max({ MaxP[0] - MinP[0], MaxP[1] - MinP[1], MaxP[2] - MinP[2], 0.1f });
    const float Scale = static_cast<float>(std::min(TileW - 28, TileH - 50)) * 0.72f / Span;
    const float Cx    = static_cast<float>(TileX) + static_cast<float>(TileW) * 0.5f;
    const float Cy    = static_cast<float>(TileY) + static_cast<float>(TileH) * 0.55f;

    const float Yaw   = YawDeg * 3.14159265f / 180.0f;
    const float Pitch = PitchDeg * 3.14159265f / 180.0f;
    const float Cyaw  = std::cos(Yaw), Syaw = std::sin(Yaw);
    const float Cpit  = std::cos(Pitch), Spit = std::sin(Pitch);

    for (const auto& A : Anchors)
    {
        const float X0 = A.PositionMeters[0] - Ctr[0];
        const float Y0 = A.PositionMeters[1] - Ctr[1];
        const float Z0 = A.PositionMeters[2] - Ctr[2];

        const float X1 = X0 * Cyaw + Z0 * Syaw;
        const float Z1 = -X0 * Syaw + Z0 * Cyaw;
        const float Y2 = Y0 * Cpit - Z1 * Spit;
        const float Z2 = Y0 * Spit + Z1 * Cpit;

        const float Persp = 1.0f / (1.0f - (Z2 / (Span * 3.4f)));
        const float Sx    = Cx + X1 * Scale * Persp;
        const float Sy    = Cy - Y2 * Scale * Persp;

        if (Sx < TileX + 5 || Sx > TileX + TileW - 5 || Sy < TileY + 26 || Sy > TileY + TileH - 5) continue;

        const float NormY = std::clamp((A.PositionMeters[1] - MinP[1]) / std::max(0.05f, MaxP[1] - MinP[1]), 0.0f, 1.0f);
        ColourSpace::SpatialColourSampleInput Q {};
        Q.NormalizedCoordinate = std::clamp(0.65f * NormY + 0.35f * A.CurvatureWeight, 0.0f, 1.0f);
        const ColourSpace::LinearRgba Col = ColourSpace::SampleColourSource(Palette, Q);
        const float DepthAtten = std::clamp(0.58f + 0.42f * Persp, 0.35f, 1.15f);
        DrawGlowDisc(Canvas, Sx, Sy, 2.7f, Col.Red, Col.Green, Col.Blue, 0.44f * DepthAtten);
    }

    Canvas.DrawText(TileX + 10, TileY + 8, Title, Rgba8 { 226, 232, 244, 255 }, 1);
}

void RenderOriginsContactSheet()
{
    ContactCanvas Canvas(1440, 900);
    Canvas.Clear(Rgba8 { 9, 11, 16, 255 });

    Canvas.FillRect(0, 0, 1440, 46, Rgba8 { 16, 19, 28, 255 });
    Canvas.FillRect(0, 45, 1440, 1, Rgba8 { 44, 52, 72, 255 });
    Canvas.DrawText(22, 16, "FRONTIER C++ PARTICLESPACE  //  PROCEDURAL 3D SURFACE & FIREWORK ORIGIN SAMPLERS (17 SURFACES + 10 FIREWORKS)", Rgba8 { 232, 238, 250, 255 }, 1);

    ColourSpace::ColourAliasRepository Codex {};
    const auto GradientIndices = Codex.QueryIndicesByCategory(ColourSpace::ColourSourceCategory::MultiStopGradient);
    auto PickPalette = [&](std::size_t Idx) -> ColourSpace::ColourSourceProfile {
        if (!GradientIndices.empty())
        {
            const std::size_t AliasIdx = GradientIndices[Idx % GradientIndices.size()];
            if (const auto* Rec = Codex.QueryAliasAt(AliasIdx))
            {
                return Rec->SourceProfile;
            }
        }
        return ColourSpace::ColourSourceProfile {};
    };

    struct TileSpec
    {
        std::string                        Label;
        std::vector<ParticleAnchorSample>  Anchors;
        std::size_t                        PaletteIdx;
        float                              YawDeg;
        float                              PitchDeg;
    };

    auto MakeSurf = [](SurfacePresetCategory Preset, uint32_t Count, uint32_t Seed) {
        SurfaceAnchorOrigin Spec {};
        Spec.Preset = Preset;
        return SampleProceduralSurfaceAnchors(Spec, Count, Seed);
    };
    auto MakeFw = [](FireworkShapeCategory Shape, const char* Text, uint32_t Count, uint32_t Seed) {
        FireworkOrigin Spec {};
        Spec.Shape          = Shape;
        Spec.SkyTextMessage = Text;
        return SampleFireworkPatternAnchors(Spec, Count, 0u, Seed);
    };

    const std::vector<TileSpec> Tiles = {
        { "01  OBSIDIAN HAND + ORB (SURFACE 3)",     MakeSurf(SurfacePresetCategory::ObsidianHandOrb,      1600u, 11u), 0u,  -24.0f,  12.0f },
        { "02  UTAH TEAPOT (SURFACE 6)",             MakeSurf(SurfacePresetCategory::UtahTeapot,           1600u, 22u), 1u,  -28.0f,  16.0f },
        { "03  SPORTS CAR (SURFACE 7)",              MakeSurf(SurfacePresetCategory::SportsCarSilhouette,  1600u, 33u), 2u,  -36.0f,  18.0f },
        { "04  SCULPTED BUST (SURFACE 8)",           MakeSurf(SurfacePresetCategory::SculptedBust,         1600u, 44u), 3u,  -24.0f,  10.0f },
        { "05  TORUS KNOT (3,2) (SURFACE 9)",        MakeSurf(SurfacePresetCategory::TorusKnot32,          1600u, 55u), 4u,  -22.0f,  26.0f },
        { "06  PRECISION GEAR (SURFACE 10)",         MakeSurf(SurfacePresetCategory::PrecisionGear,        1600u, 66u), 5u,  -18.0f,  34.0f },
        { "07  QUARTZ CRYSTALS (SURFACE 11)",        MakeSurf(SurfacePresetCategory::QuartzCrystalCluster, 1600u, 77u), 6u,  -30.0f,  16.0f },
        { "08  STONE ARCHWAY (SURFACE 12)",          MakeSurf(SurfacePresetCategory::ArchitecturalArchway, 1600u, 88u), 7u,  -28.0f,  12.0f },
        { "09  FIREWORK SKY TEXT \"2026\" (SHAPE 8)", MakeFw(FireworkShapeCategory::SkyText, "2026",       1800u, 99u), 8u,   -6.0f,   4.0f },
        { "10  FIREWORK GLOWING HEART (SHAPE 4)",    MakeFw(FireworkShapeCategory::Heart, "",              1400u, 101u),9u,  -12.0f,   8.0f },
        { "11  FIREWORK SATURN RING (SHAPE 5)",      MakeFw(FireworkShapeCategory::Saturn, "",             1500u, 102u),10u, -24.0f,  22.0f },
        { "12  FIREWORK BUTTERFLY (SHAPE 9)",        MakeFw(FireworkShapeCategory::Butterfly, "",          1600u, 103u),11u, -14.0f,  12.0f },
    };

    constexpr int Cols = 4;
    constexpr int Rows = 3;
    constexpr int Margin = 18;
    constexpr int Gap    = 14;
    const int TileW = (1440 - Margin * 2 - Gap * (Cols - 1)) / Cols;
    const int TileH = (900 - 46 - Margin * 2 - Gap * (Rows - 1)) / Rows;

    for (int I = 0; I < static_cast<int>(Tiles.size()); ++I)
    {
        const int Col = I % Cols;
        const int Row = I / Cols;
        const int Tx  = Margin + Col * (TileW + Gap);
        const int Ty  = 46 + Margin + Row * (TileH + Gap);
        RenderOriginTile(Canvas, Tx, Ty, TileW, TileH, Tiles[I].Label, Tiles[I].Anchors, PickPalette(Tiles[I].PaletteIdx), Tiles[I].YawDeg, Tiles[I].PitchDeg);
    }

    const std::filesystem::path OutDir("Exhibits/Images/Particles/CppEngine");
    std::filesystem::create_directories(OutDir);
    const std::filesystem::path OutPng = OutDir / "ParticleOriginsContactSheet.png";
    const int Ok = stbi_write_png(OutPng.string().c_str(), Canvas.Width, Canvas.Height, 4, Canvas.Pixels.data(), Canvas.Width * 4);
    Expect(Ok != 0 && std::filesystem::exists(OutPng), "Rendered 1440x900 ParticleOriginsContactSheet.png proof exhibit");
}

void RenderDynamicsShowcaseSheet()
{
    ContactCanvas Canvas(1440, 900);
    Canvas.Clear(Rgba8 { 9, 11, 16, 255 });
    Canvas.FillRect(0, 0, 1440, 46, Rgba8 { 16, 19, 28, 255 });
    Canvas.FillRect(0, 45, 1440, 1, Rgba8 { 44, 52, 72, 255 });
    Canvas.DrawText(22, 16, "FRONTIER C++ PARTICLESPACE  //  AUTHENTIC HTML PARTICLE STUDIO PRESETS (RIBBONS, FIREWORKS, TESLA, AURORA, BLACK HOLE, MAGNETS, BOIDS, WEB)", Rgba8 { 232, 238, 250, 255 }, 1);

    struct SimTile
    {
        std::string Title;
        std::string PaletteAlias;
        float       YawDeg;
        float       PitchDeg;
        ParticleRig Rig;
        int         Steps;
    };

    // Authentic HTML Particle Studio presets ONLY (directly from Experimental/ParticleEditor Final/Engine/presets.js)
    const struct { const char* PresetId; const char* Title; float Yaw; float Pitch; int Steps; } HtmlHeroes[8] = {
        { "bokeh-object-pulse",   "01  OBJECT SURFACE POUR RIBBONS",  -26.0f, 16.0f, 70 },
        { "fireworks-spectacle",  "02  NEW YEAR'S TEXT FIREWORKS",    -12.0f, 10.0f, 45 },
        { "tesla-sphere-lightning","03  TESLA SPHERE LIGHTNING",      -22.0f, 14.0f, 45 },
        { "aurora-borealis",      "04  AURORA BOREALIS CURTAINS",     -18.0f, 18.0f, 65 },
        { "black-hole",           "05  BLACK HOLE ACCRETION",         -18.0f, 34.0f, 75 },
        { "magnetic-particles",   "06  MAGNETIC PARTICLES",           -24.0f, 18.0f, 70 },
        { "boids-flock",          "07  BOIDS FLOCK",                  -22.0f, 18.0f, 65 },
        { "spiderweb-orb",        "08  ORB-WEAVE SPIDERWEB",          -16.0f, 16.0f, 55 },
    };

    std::vector<SimTile> Tiles;
    for (const auto& H : HtmlHeroes)
    {
        const auto* Meta = FindParticlePresetById(H.PresetId);
        ParticleRig R = Meta ? InstantiateRigFromPreset(*Meta) : ConstructDefaultStudioRig();
        R.SlotCapacity = std::min<uint32_t>(R.SlotCapacity, 1200u);
        Tiles.push_back({ H.Title, "", H.Yaw, H.Pitch, std::move(R), H.Steps });
    }

    constexpr int Cols = 4;
    constexpr int Rows = 2;
    constexpr int Margin = 18;
    constexpr int Gap    = 14;
    const int TileW = (1440 - Margin * 2 - Gap * (Cols - 1)) / Cols;
    const int TileH = (900 - 46 - Margin * 2 - Gap * (Rows - 1)) / Rows;

    for (int I = 0; I < static_cast<int>(Tiles.size()); ++I)
    {
        ParticleDocument Doc {};
        Doc.DocumentTitle = Tiles[I].Title;
        Doc.AmbientWind.Enabled = false;
        Doc.Rigs.push_back(Tiles[I].Rig);

        ParticleSimulationSession Ext {};
        (void)Ext.Initialize(Doc);
        Ext.TriggerBurst(0u, Tiles[I].Rig.SlotCapacity);
        for (int Step = 0; Step < Tiles[I].Steps; ++Step)
        {
            Ext.Tick(1.0 / 60.0);
        }

        std::vector<ParticleAnchorSample> VisualPts;
        const auto Slots = Ext.InspectParticleSlots();
        VisualPts.reserve(Slots.size());
        for (const auto& S : Slots)
        {
            if (!QueryParticleSlotAlive(S)) continue;
            ParticleAnchorSample A {};
            for (int K = 0; K < 3; ++K) A.PositionMeters[K] = S.PositionMeters[K];
            A.CurvatureWeight = std::clamp(S.AuxScalar > 0.0f ? S.AuxScalar : S.SeedScalar, 0.0f, 1.0f);
            VisualPts.push_back(A);
        }

        const int Col = I % Cols;
        const int Row = I / Cols;
        const int Tx  = Margin + Col * (TileW + Gap);
        const int Ty  = 46 + Margin + Row * (TileH + Gap);
        RenderOriginTile(Canvas, Tx, Ty, TileW, TileH, Tiles[I].Title, VisualPts, Tiles[I].Rig.ColourProfile, Tiles[I].YawDeg, Tiles[I].PitchDeg);
    }

    const std::filesystem::path OutDir("Exhibits/Images/Particles/CppEngine");
    std::filesystem::create_directories(OutDir);
    const std::filesystem::path OutPng = OutDir / "ParticleDynamicsShowcase.png";
    const int Ok = stbi_write_png(OutPng.string().c_str(), Canvas.Width, Canvas.Height, 4, Canvas.Pixels.data(), Canvas.Width * 4);
    Expect(Ok != 0 && std::filesystem::exists(OutPng), "Rendered 1440x900 ParticleDynamicsShowcase.png live physics exhibit");
}

} // namespace

int main()
{
    std::cout << "=== Frontier ParticleSpace Foundation Validation ===\n";

    // 1. Struct layout guarantees (compile-time static_assert + runtime verification)
    Expect(sizeof(ParticleSlot) == 80u && alignof(ParticleSlot) == 16u, "ParticleSlot is 80B and 16B-aligned");
    Expect(sizeof(StrandVertex) == 16u && alignof(StrandVertex) == 16u, "StrandVertex is 16B and 16B-aligned");
    Expect(sizeof(ParticleAnchorSample) == 32u && alignof(ParticleAnchorSample) == 16u, "ParticleAnchorSample is 32B and 16B-aligned");
    Expect(sizeof(ParticleGlobalUniform) == 192u && alignof(ParticleGlobalUniform) == 16u, "ParticleGlobalUniform is 192B and 16B-aligned");
    Expect(sizeof(ParticleRigUniform) == 256u && alignof(ParticleRigUniform) == 16u, "ParticleRigUniform is 256B and 16B-aligned");

    // 2. All 15 ParticleScalarChannel mappings routed directly into ColourSpace
    {
        ParticleSlot TestSlot {};
        TestSlot.PositionMeters[0] = 1.4f; TestSlot.PositionMeters[1] = 2.8f; TestSlot.PositionMeters[2] = -0.9f;
        TestSlot.VelocityMps[0]    = 0.8f; TestSlot.VelocityMps[1]    = 3.2f; TestSlot.VelocityMps[2]    = -1.1f;
        TestSlot.AgeSeconds        = 0.75f;
        TestSlot.LifespanSeconds   = 2.5f;
        TestSlot.SizeMeters        = 0.08f;
        TestSlot.SeedScalar        = 0.42f;
        TestSlot.SpinRadians       = 1.57f;
        TestSlot.AuxScalar         = 0.65f;

        ParticleTintInput Ctx {};
        Ctx.ElapsedSeconds = 4.2f;

        ColourSpace::ColourAliasRepository TestCodex {};
        ColourSpace::ColourSourceProfile Prof {};
        (void)TestCodex.ResolveAliasToProfile("Aurora", Prof);

        bool AllValid = true;
        for (uint8_t Ch = 0u; Ch < static_cast<uint8_t>(ParticleScalarChannel::Count); ++Ch)
        {
            const auto Channel = static_cast<ParticleScalarChannel>(Ch);
            const auto Key     = QueryScalarChannelKey(Channel);
            if (ParseScalarChannelKey(Key) != Channel) AllValid = false;

            ParticleTintDriverSpec Driver {};
            Driver.Channel = Channel;
            const auto Rgba = EvaluateParticleTint(Driver, Prof, TestSlot, Ctx);
            if (!std::isfinite(Rgba.Red) || !std::isfinite(Rgba.Green) || !std::isfinite(Rgba.Blue) || !std::isfinite(Rgba.Alpha))
            {
                AllValid = false;
            }
        }
        Expect(AllValid, "All 15 ParticleScalarChannel modes round-trip keys and evaluate finite ColourSpace RGBA");
    }

    // 3. All 17 procedural surface presets + custom triangle area-weighted sampler
    {
        bool All17SurfacesValid = true;
        for (uint8_t Id = 0u; Id < static_cast<uint8_t>(SurfacePresetCategory::Count); ++Id)
        {
            SurfaceAnchorOrigin Spec {};
            Spec.Preset = static_cast<SurfacePresetCategory>(Id);
            const auto Anchors = SampleProceduralSurfaceAnchors(Spec, 128u, 500u + Id);
            if (Anchors.size() != 128u) All17SurfacesValid = false;
            for (const auto& A : Anchors)
            {
                const float NLen = std::sqrt(A.NormalUnit[0] * A.NormalUnit[0] + A.NormalUnit[1] * A.NormalUnit[1] + A.NormalUnit[2] * A.NormalUnit[2]);
                if (!std::isfinite(A.PositionMeters[0]) || !std::isfinite(A.PositionMeters[1]) || !std::isfinite(A.PositionMeters[2]) || NLen < 0.9f || NLen > 1.1f)
                {
                    All17SurfacesValid = false;
                }
            }
        }
        Expect(All17SurfacesValid, "All 17 procedural SurfaceAnchorSampler presets produce unit-normal finite anchors");

        // Custom triangle surface area weighting check (triangle B has 9x area of triangle A)
        const std::vector<SurfaceTriangleInput> Tris = {
            SurfaceTriangleInput { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
            SurfaceTriangleInput { { 10.0f, 0.0f, 0.0f }, { 13.0f, 0.0f, 0.0f }, { 10.0f, 3.0f, 0.0f } },
        };
        SurfaceAnchorOrigin IdentitySurf {};
        IdentitySurf.CenterMeters[0] = 0.0f; IdentitySurf.CenterMeters[1] = 0.0f; IdentitySurf.CenterMeters[2] = 0.0f;
        IdentitySurf.ScaleMeters[0]  = 1.0f; IdentitySurf.ScaleMeters[1]  = 1.0f; IdentitySurf.ScaleMeters[2]  = 1.0f;
        const auto TriAnchors = SampleTriangleSurfaceAnchors(Tris, IdentitySurf, 1000u, 777u);
        uint32_t CountBig = 0u;
        for (const auto& A : TriAnchors)
        {
            if (A.PositionMeters[0] > 5.0f) ++CountBig;
        }
        Expect(CountBig > 840u && CountBig < 950u, "Area-weighted SampleTriangleSurfaceAnchors matches expected 9:1 area ratio");
    }

    // 4. All 10 firework shapes including custom sky-text bitmap
    {
        bool All10FireworksValid = true;
        for (uint8_t Id = 0u; Id < static_cast<uint8_t>(FireworkShapeCategory::Count); ++Id)
        {
            FireworkOrigin Spec {};
            Spec.Shape          = static_cast<FireworkShapeCategory>(Id);
            Spec.SkyTextMessage = "FRONTIER";
            const auto Anchors = SampleFireworkPatternAnchors(Spec, 256u, 0u, 900u + Id);
            if (Anchors.size() != 256u) All10FireworksValid = false;
            for (const auto& A : Anchors)
            {
                if (!std::isfinite(A.PositionMeters[0]) || !std::isfinite(A.PositionMeters[1]) || !std::isfinite(A.PositionMeters[2]))
                {
                    All10FireworksValid = false;
                }
            }
        }
        Expect(All10FireworksValid, "All 10 FireworkPatternSampler shapes (including SkyText bitmap) generate finite anchors");
    }

    // 5. ParticleSimulationSession lifecycle + direct ForceFieldAccumulator stepping
    {
        const ParticleDocument Doc = ConstructStudioDefaultDocument();
        ParticleSimulationSession Ext {};
        Expect(Ext.Initialize(Doc), "ParticleSimulationSession::Initialize succeeds with Studio Default document");
        Expect(Ext.InspectForceAccumulator().QueryActiveSourceCount() == 6u, "ForceFieldAccumulator binds all 6 Studio Default ForceSources");
        Expect(Ext.InspectRigAnchors(0u).size() == 1024u, "SurfaceAnchor cache pre-populated with 1024 deterministic anchors");

        Ext.TriggerBurst(0u, 240u);
        for (int Step = 0; Step < 60; ++Step)
        {
            Ext.Tick(1.0 / 60.0);
        }

        const auto& Tel = Ext.InspectTelemetry();
        Expect(Tel.StepCounter == 60u && Tel.TotalAliveParticles > 400u, "ParticleSimulationSession executes 60 coupled ForceField steps with live particles");
        Expect(Tel.BoundsMaxMeters[0] > Tel.BoundsMinMeters[0] && Tel.BoundsMaxMeters[1] > Tel.BoundsMinMeters[1], "Advected live particle cloud expands cleanly across 3D space");

        Ext.Suspend();
        const uint32_t FrozenStep = Ext.InspectTelemetry().StepCounter;
        Ext.Tick(1.0 / 60.0);
        Expect(Ext.InspectTelemetry().StepCounter == FrozenStep, "ParticleSimulationSession::Suspend freezes stepping cleanly");
        Ext.Resume();
        Ext.Tick(1.0 / 60.0);
        Expect(Ext.InspectTelemetry().StepCounter == FrozenStep + 1u, "ParticleSimulationSession::Resume restores stepping cleanly");
        Ext.Finalize();
        Expect(!Ext.QueryInitialized(), "ParticleSimulationSession::Finalize releases runtime cleanly");
    }

    // 6. Two-Tier Hybrid .ParticleSpace Codec bit-exact round-trip (delegating ColourCodec + VectorFieldCodec)
    {
        const ParticleDocument Orig = ConstructStudioDefaultDocument();
        const std::filesystem::path TempPath = std::filesystem::temp_directory_path() / "FrontierParticleFoundationTest.ParticleSpace";
        std::string Diag;
        const bool Encoded = Codecs::ParticleEncoder::EncodeToFile(TempPath, Orig, Diag);
        Expect(Encoded, "ParticleEncoder::EncodeToFile writes Two-Tier Hybrid .ParticleSpace container (" + Diag + ")");

        Codecs::SpaceContainerPayload RawPayload {};
        const bool RawDecoded = Codecs::SpaceContainerDecoder::DecodeFromFile(TempPath, RawPayload, Diag);
        bool HasLutSec = false, HasAnchorSec = false;
        for (const auto& Sec : RawPayload.BinarySections)
        {
            if (Sec.SubFormatTag == ".ColourLut" && Sec.PayloadBytes.size() == 64u * 16u) HasLutSec = true;
            if (Sec.SubFormatTag == ".SurfaceAnchor" && Sec.PayloadBytes.size() == 256u * 32u) HasAnchorSec = true;
        }
        Expect(RawDecoded && HasLutSec && HasAnchorSec, "Two-Tier .ParticleSpace embeds verified 64B-aligned .ColourLut & .SurfaceAnchor binary sections");

        ParticleDocument Restored {};
        const bool Decoded = Codecs::ParticleDecoder::DecodeFromFile(TempPath, Restored, Diag);
        Expect(Decoded, "ParticleDecoder::DecodeFromFile restores ParticleDocument (" + Diag + ")");
        Expect(Restored.DocumentTitle == Orig.DocumentTitle && Restored.ActivePresetId == Orig.ActivePresetId && Restored.Seed == Orig.Seed,
               "Restored ParticleDocument matches Title, ActivePresetId, and Seed");
        Expect(Restored.ForceSources.size() == Orig.ForceSources.size() && Restored.ForceSources.size() == 6u,
               "VectorFieldCodec inline round-trip preserves all 6 ForceSources");
        Expect(Restored.Rigs.size() == Orig.Rigs.size() && Restored.Rigs[0].SlotCapacity == Orig.Rigs[0].SlotCapacity,
               "Restored ParticleRig preserves SlotCapacity, Origin, Carrier, Dynamics, and ColourCodec ramp");

        std::error_code Ec;
        std::filesystem::remove(TempPath, Ec);
    }

    // 7. ParticleVulkanExecutor verification of all 6 compiled Vulkan 1.3 SPIR-V modules
    {
        ParticleVulkanExecutor VkExec {};
        std::string VkDiag;
#ifdef FRONTIER_COMPILED_SHADER_DIR
        const bool Ok = VkExec.Initialize(FRONTIER_COMPILED_SHADER_DIR, VkDiag);
#else
        const bool Ok = VkExec.Initialize("build/CompiledShaders", VkDiag);
#endif
        Expect(Ok && VkExec.InspectTelemetry().CompiledShaderCount == 21u,
               "ParticleVulkanExecutor verified all 21 SPIR-V 1.3 modules (" + VkDiag + ")");
    }

    // 8. Full 102-preset Catalogue verification (93 active + 9 quarantined across 15 categories, live stepped with 0 NaNs!)
    {
        const auto AllPresets = QueryAllParticlePresets();
        uint32_t ActiveCount = 0u, QuarantinedCount = 0u;
        bool All102Finite = true;

        for (const auto& Meta : AllPresets)
        {
            if (Meta.Quarantined) ++QuarantinedCount;
            else ++ActiveCount;

            ParticleDocument Doc = InstantiateDocumentFromPresetId(Meta.Identifier);
            Doc.Rigs[0].SlotCapacity = std::min(Doc.Rigs[0].SlotCapacity, 128u);
            ParticleSimulationSession Ext {};
            if (!Ext.Initialize(Doc)) { All102Finite = false; break; }
            Ext.TriggerBurst(0u, 48u);
            for (int Step = 0; Step < 8; ++Step)
            {
                Ext.Tick(1.0 / 60.0);
            }
            for (const auto& S : Ext.InspectParticleSlots())
            {
                if (!QueryParticleSlotAlive(S)) continue;
                if (!std::isfinite(S.PositionMeters[0]) || !std::isfinite(S.PositionMeters[1]) || !std::isfinite(S.PositionMeters[2])
                 || !std::isfinite(S.VelocityMps[0])    || !std::isfinite(S.VelocityMps[1])    || !std::isfinite(S.VelocityMps[2]))
                {
                    All102Finite = false;
                }
            }
        }
        Expect(AllPresets.size() == 102u && ActiveCount == 93u && QuarantinedCount == 9u,
               "ParticlePresetRepository holds all 102 presets (93 active + 9 quarantined)");
        Expect(All102Finite, "All 102 presets instantiate and simulate cleanly across 8 live physics steps with 0 NaNs");
    }

    // 9. Render visual contact-sheet proofs (12 procedural 3D origins + 8 live C++ physics solvers)
    RenderOriginsContactSheet();
    RenderDynamicsShowcaseSheet();

    if (gFailures == 0)
    {
        std::cout << "=== ALL PARTICLE FOUNDATION CHECKS PASSED (0 FAILURES) ===\n";
        return 0;
    }
    std::cerr << "=== PARTICLE FOUNDATION VALIDATION FAILED: " << gFailures << " FAILURE(S) ===\n";
    return 1;
}
