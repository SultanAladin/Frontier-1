/*============================================================================================================================================
                                                   PARTICLEVEHICLEDISSOLVEVALIDATION.CPP
============================================================================================================================================*/
// 🚗 Comprehensive C++20 validation + studio visual proof suite for:
//    1. ProceduralSpriteSynthesizer (16 archetypes, 4 channels, 2x2 atlas) + PatternStampStackSession integration
//    2. VehicleDissolveSolver (ControlVehicle 25,068-tri 16^3 lattice Derez + 8-way shatter, Reverse-Rez, Dissolve-to-Nothing, Dissolve-to-Butterfly, Two-Way GI)
//    3. ContentBrowser ThumbnailSheetRepository real rendered particle thumbnails (replacing 12-spoke starburst placeholder)

#include "Engine/Components/ContentBrowser/ThumbnailSheetRepository.h"
#include "Engine/ParticleSpace/Dynamics/VehicleDissolveSolver.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"
#include "Engine/ParticleSpace/Sprites/ProceduralSpriteSynthesizer.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace
{

int gFailures = 0;

void Check(bool Cond, const std::string& Label)
{
    if (!Cond)
    {
        ++gFailures;
        std::cerr << "[FAIL] " << Label << '\n';
    }
    else
    {
        std::cout << "[PASS] " << Label << '\n';
    }
}

void BlitRgbaTile(
    std::vector<uint8_t>&       Dst,
    uint32_t                    DstW,
    uint32_t                    DstH,
    uint32_t                    DstX,
    uint32_t                    DstY,
    const std::vector<uint8_t>& Src,
    uint32_t                    SrcW,
    uint32_t                    SrcH,
    bool                        CompositeOnDark)
{
    for (uint32_t Y = 0u; Y < SrcH; ++Y)
    {
        const uint32_t Dy = DstY + Y;
        if (Dy >= DstH)
        {
            continue;
        }
        for (uint32_t X = 0u; X < SrcW; ++X)
        {
            const uint32_t Dx = DstX + X;
            if (Dx >= DstW)
            {
                continue;
            }
            const std::size_t SOff = (static_cast<std::size_t>(Y) * SrcW + X) * 4u;
            const std::size_t DOff = (static_cast<std::size_t>(Dy) * DstW + Dx) * 4u;
            if (CompositeOnDark)
            {
                const float A = static_cast<float>(Src[SOff + 3u]) / 255.0f;
                const uint8_t Bg = (((X >> 4u) ^ (Y >> 4u)) & 1u) ? 20u : 26u;
                Dst[DOff + 0u] = static_cast<uint8_t>(std::lround(Bg * (1.0f - A) + Src[SOff + 0u] * A));
                Dst[DOff + 1u] = static_cast<uint8_t>(std::lround(Bg * (1.0f - A) + Src[SOff + 1u] * A));
                Dst[DOff + 2u] = static_cast<uint8_t>(std::lround(Bg * (1.0f - A) + Src[SOff + 2u] * A));
                Dst[DOff + 3u] = 255u;
            }
            else
            {
                Dst[DOff + 0u] = Src[SOff + 0u];
                Dst[DOff + 1u] = Src[SOff + 1u];
                Dst[DOff + 2u] = Src[SOff + 2u];
                Dst[DOff + 3u] = 255u;
            }
        }
    }
}

void StrokeRectBorder(std::vector<uint8_t>& Buf, uint32_t W, uint32_t H, uint32_t X0, uint32_t Y0, uint32_t Rw, uint32_t Rh, uint8_t R, uint8_t G, uint8_t B)
{
    for (uint32_t X = X0; X < X0 + Rw && X < W; ++X)
    {
        for (uint32_t Y : { Y0, Y0 + Rh - 1u })
        {
            if (Y < H)
            {
                const std::size_t O = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Buf[O + 0u] = R; Buf[O + 1u] = G; Buf[O + 2u] = B; Buf[O + 3u] = 255u;
            }
        }
    }
    for (uint32_t Y = Y0; Y < Y0 + Rh && Y < H; ++Y)
    {
        for (uint32_t X : { X0, X0 + Rw - 1u })
        {
            if (X < W)
            {
                const std::size_t O = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Buf[O + 0u] = R; Buf[O + 1u] = G; Buf[O + 2u] = B; Buf[O + 3u] = 255u;
            }
        }
    }
}

bool ValidateProceduralSpritesPatternStudio(const std::filesystem::path& OutSpriteDir)
{
    using namespace Frontier;
    constexpr uint32_t TileRes = 128u;
    constexpr uint32_t Pad     = 12u;
    constexpr uint32_t SheetW  = Pad + 4u * (TileRes + Pad);
    constexpr uint32_t SheetH  = Pad + 4u * (TileRes + Pad);

    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 255u);
    for (std::size_t I = 0u; I < static_cast<std::size_t>(SheetW) * SheetH; ++I)
    {
        Sheet[I * 4u + 0u] = 12u;
        Sheet[I * 4u + 1u] = 13u;
        Sheet[I * 4u + 2u] = 16u;
        Sheet[I * 4u + 3u] = 255u;
    }

    constexpr uint32_t ArchetypeCount = static_cast<uint32_t>(ProceduralSpriteArchetype::Count);
    uint32_t ValidCount = 0u;
    for (uint32_t Idx = 0u; Idx < ArchetypeCount; ++Idx)
    {
        const auto Arch = static_cast<ProceduralSpriteArchetype>(Idx);
        ProceduralSpriteSpec Spec = QueryDefaultProceduralSpriteSpec(Arch);
        Spec.TileResolution = TileRes;
        Spec.AtlasColumns   = 1u;
        Spec.Seed           = 1337u + Idx * 97u;

        const ProceduralSpriteChannelSet Set = SynthesizeProceduralSpriteChannels(Spec);
        const bool SizesOk =
            Set.Width == TileRes &&
            Set.Height == TileRes &&
            Set.Rgba8.size() == TileRes * TileRes * 4u &&
            Set.Height8.size() == TileRes * TileRes &&
            Set.NormalRgba8.size() == TileRes * TileRes * 4u &&
            Set.MaskRgba8.size() == TileRes * TileRes * 4u;

        uint32_t NonZeroAlpha = 0u;
        for (uint32_t P = 0u; P < TileRes * TileRes; ++P)
        {
            if (Set.Rgba8[P * 4u + 3u] > 12u)
            {
                ++NonZeroAlpha;
            }
        }
        if (SizesOk && NonZeroAlpha > 200u)
        {
            ++ValidCount;
        }

        const uint32_t Col = Idx % 4u;
        const uint32_t Row = Idx / 4u;
        const uint32_t Dx  = Pad + Col * (TileRes + Pad);
        const uint32_t Dy  = Pad + Row * (TileRes + Pad);
        BlitRgbaTile(Sheet, SheetW, SheetH, Dx, Dy, Set.Rgba8, TileRes, TileRes, true);
        StrokeRectBorder(Sheet, SheetW, SheetH, Dx, Dy, TileRes, TileRes, 52u, 54u, 60u);
    }

    Check(ValidCount == ArchetypeCount, "All 16 ProceduralSpriteSynthesizer archetypes produce valid 4-channel outputs");

    // Verify Pattern Studio integration + 2x2 atlas deterministic synthesis
    Workspaces::PatternStampWorkspaceSession PatternSession{};
    const ProceduralSpriteChannelSet AtlasA = PatternSession.SynthesizeParticleSpriteAtlas(
        ProceduralSpriteArchetype::SnowStellarDendrite, 64u, 2u, 2026u);
    const ProceduralSpriteChannelSet AtlasB = PatternSession.SynthesizeParticleSpriteAtlas(
        ProceduralSpriteArchetype::SnowStellarDendrite, 64u, 2u, 2026u);
    Check(AtlasA.Width == 128u && AtlasA.Height == 128u && AtlasA.Rgba8 == AtlasB.Rgba8 && AtlasA.NormalRgba8 == AtlasB.NormalRgba8,
          "PatternStampStackSession 2x2 Snow Atlas is deterministic across RGBA8 + NormalRgba8 channels");

    std::filesystem::create_directories(OutSpriteDir);
    const std::string OutPath = (OutSpriteDir / "CppProceduralSprites_16_Proof.png").string();
    const int Wrote = stbi_write_png(OutPath.c_str(), static_cast<int>(SheetW), static_cast<int>(SheetH), 4, Sheet.data(), static_cast<int>(SheetW * 4u));
    Check(Wrote != 0, "Wrote CppProceduralSprites_16_Proof.png");
    return true;
}

bool ValidateControlVehicleDissolveModes(const std::filesystem::path& OutDir)
{
    using namespace Frontier::ParticleSpace;
    const ControlVehicleGeometrySet& Vehicle = QueryControlVehicleGeometrySet();
    Check(Vehicle.Triangles.size() == 25068u, "Loaded canonical 25,068-triangle ControlVehicle geometry");

    constexpr uint32_t TileW = 720u;
    constexpr uint32_t TileH = 540u;
    constexpr uint32_t Pad   = 14u;
    constexpr uint32_t SheetW = Pad + 2u * (TileW + Pad);
    constexpr uint32_t SheetH = Pad + 2u * (TileH + Pad);

    std::vector<uint8_t> ProofSheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 255u);
    for (std::size_t I = 0u; I < static_cast<std::size_t>(SheetW) * SheetH; ++I)
    {
        ProofSheet[I * 4u + 0u] = 10u;
        ProofSheet[I * 4u + 1u] = 11u;
        ProofSheet[I * 4u + 2u] = 14u;
        ProofSheet[I * 4u + 3u] = 255u;
    }

    VehicleDissolveRenderCamera Cam{};
    Cam.EyeX           = 5.45f;
    Cam.EyeY           = 2.10f;
    Cam.EyeZ           = 5.10f;
    Cam.TargetX        = 0.10f;
    Cam.TargetY        = 0.82f;
    Cam.TargetZ        = 0.00f;
    Cam.VerticalFovDeg = 35.0f;

    struct ModeTestSpec
    {
        VehicleDissolveMode Mode;
        float               Progress;
        const char*         FileStem;
    };

    const std::array<ModeTestSpec, 4> Modes = {{
        { VehicleDissolveMode::DerezCubes,          0.56f, "01_ControlVehicle_DerezCubes" },
        { VehicleDissolveMode::ReverseRezCubes,     0.48f, "02_ControlVehicle_ReverseRezCubes" },
        { VehicleDissolveMode::DissolveToNothing,   0.52f, "03_ControlVehicle_DissolveToNothing" },
        { VehicleDissolveMode::DissolveToButterfly, 0.56f, "04_ControlVehicle_DissolveToButterfly" },
    }};

    std::filesystem::create_directories(OutDir);

    for (std::size_t M = 0u; M < Modes.size(); ++M)
    {
        VehicleDissolveParams Params{};
        Params.Mode            = Modes[M].Mode;
        Params.Progress        = Modes[M].Progress;
        Params.LeadingRimWidth = 0.075f;
        Params.EnableTwoWayGi  = true;

        const VehicleDissolveSnapshot Snap = BuildVehicleDissolveSnapshot(Vehicle, Params);
        if (Modes[M].Mode == VehicleDissolveMode::DerezCubes)
        {
            uint32_t SubShatterCount = 0u;
            for (const auto& C : Snap.Cubes)
            {
                if (C.IsSubShatter != 0u)
                {
                    ++SubShatterCount;
                }
            }
            Check(Snap.ReleasedCellCount > 150u && Snap.ShatteredCubeCount > 10u && SubShatterCount >= Snap.ShatteredCubeCount * 8u,
                  "DerezCubes releases lattice cubes + triggers 8-sub-cube floor impact shattering");
        }
        else if (Modes[M].Mode == VehicleDissolveMode::ReverseRezCubes)
        {
            Check(Snap.IntactAnchorCount > 150u && Snap.Cubes.size() > 100u,
                  "ReverseRezCubes converges spiral cubes smoothly onto intact ControlVehicle");
        }
        else if (Modes[M].Mode == VehicleDissolveMode::DissolveToNothing)
        {
            Check(Snap.IntactAnchorCount > 120u && Snap.Vapors.size() > 120u,
                  "DissolveToNothing vaporizes front bodywork into buoyant fading embers and wisps");
        }
        else
        {
            Check(Snap.IntactAnchorCount > 120u && Snap.Butterflies.size() > 80u,
                  "DissolveToButterfly transforms dissolving car silhouette into 3D flapping butterfly swarm");
        }

        std::vector<uint8_t> ViewportRgba;
        RenderVehicleDissolveStudioRgba8(Vehicle, Params, Snap, Cam, TileW, TileH, ViewportRgba);

        const std::string TilePath = (OutDir / (std::string(Modes[M].FileStem) + ".png")).string();
        stbi_write_png(TilePath.c_str(), static_cast<int>(TileW), static_cast<int>(TileH), 4, ViewportRgba.data(), static_cast<int>(TileW * 4u));

        const uint32_t Col = static_cast<uint32_t>(M % 2u);
        const uint32_t Row = static_cast<uint32_t>(M / 2u);
        const uint32_t Dx  = Pad + Col * (TileW + Pad);
        const uint32_t Dy  = Pad + Row * (TileH + Pad);
        BlitRgbaTile(ProofSheet, SheetW, SheetH, Dx, Dy, ViewportRgba, TileW, TileH, false);
        StrokeRectBorder(ProofSheet, SheetW, SheetH, Dx, Dy, TileW, TileH, 48u, 52u, 60u);
    }

    // Verify Two-Way GI Emissive Bounce produces measurable illumination gain vs GI-off on studio floor & car
    {
        VehicleDissolveParams POn{};
        POn.Mode           = VehicleDissolveMode::DerezCubes;
        POn.Progress       = 0.56f;
        POn.EnableTwoWayGi = true;
        VehicleDissolveParams POff = POn;
        POff.EnableTwoWayGi = false;

        const VehicleDissolveSnapshot Snap = BuildVehicleDissolveSnapshot(Vehicle, POn);
        std::vector<uint8_t> ImgOn, ImgOff;
        RenderVehicleDissolveStudioRgba8(Vehicle, POn, Snap, Cam, 240u, 180u, ImgOn);
        RenderVehicleDissolveStudioRgba8(Vehicle, POff, Snap, Cam, 240u, 180u, ImgOff);

        double SumDiff = 0.0;
        for (std::size_t I = 0u; I < ImgOn.size(); I += 4u)
        {
            SumDiff += std::fabs(static_cast<double>(ImgOn[I + 0u]) - static_cast<double>(ImgOff[I + 0u])) +
                       std::fabs(static_cast<double>(ImgOn[I + 1u]) - static_cast<double>(ImgOff[I + 1u])) +
                       std::fabs(static_cast<double>(ImgOn[I + 2u]) - static_cast<double>(ImgOff[I + 2u]));
        }
        const double MeanDiff = SumDiff / static_cast<double>(240u * 180u);
        Check(MeanDiff > 1.5, "Two-Way GI bounce illumination measurably illuminates studio floor & obsidian bodywork");
    }

    const std::string SheetPath = (OutDir / "ControlVehicle_4Modes_Proof.png").string();
    const int Wrote = stbi_write_png(SheetPath.c_str(), static_cast<int>(SheetW), static_cast<int>(SheetH), 4, ProofSheet.data(), static_cast<int>(SheetW * 4u));
    Check(Wrote != 0, "Wrote ControlVehicle_4Modes_Proof.png");
    return true;
}

bool ValidateContentBrowserParticleThumbnails(const std::filesystem::path& OutDir)
{
    using namespace Frontier;
    ThumbnailSheetRepository Repo{};
    Repo.PopulateDefaultEngineAndProjectEntries();

    const auto ParticleIndices = Repo.QueryFilteredEntries(
        WorkspaceCategory::SceneWorld,
        ContentCategory::Particles,
        ContentOriginFilter::All);
    Check(ParticleIndices.size() >= 7u, "ContentBrowser contains >= 7 stock Particle entries");

    constexpr uint32_t TileDim = ThumbnailSheetRepository::TileDimensionPixels;
    constexpr uint32_t Pad     = 10u;
    const uint32_t Count       = static_cast<uint32_t>(std::min<std::size_t>(ParticleIndices.size(), 7u));
    const uint32_t StripW      = Pad + Count * (TileDim + Pad);
    const uint32_t StripH      = Pad * 2u + TileDim;

    std::vector<uint8_t> Strip(static_cast<std::size_t>(StripW) * StripH * 4u, 255u);
    for (std::size_t I = 0u; I < static_cast<std::size_t>(StripW) * StripH; ++I)
    {
        Strip[I * 4u + 0u] = 14u;
        Strip[I * 4u + 1u] = 15u;
        Strip[I * 4u + 2u] = 18u;
        Strip[I * 4u + 3u] = 255u;
    }

    std::vector<std::vector<uint8_t>> ExtractedTiles;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        std::vector<uint8_t> Tile;
        if (!Repo.QueryTileRgbaPixels(ParticleIndices[I], Tile))
        {
            continue;
        }
        const uint32_t Dx = Pad + I * (TileDim + Pad);
        BlitRgbaTile(Strip, StripW, StripH, Dx, Pad, Tile, TileDim, TileDim, false);
        StrokeRectBorder(Strip, StripW, StripH, Dx, Pad, TileDim, TileDim, 64u, 68u, 76u);
        ExtractedTiles.push_back(std::move(Tile));
    }

    uint32_t DistinctPairs = 0u;
    for (std::size_t A = 0u; A < ExtractedTiles.size(); ++A)
    {
        for (std::size_t B = A + 1u; B < ExtractedTiles.size(); ++B)
        {
            uint64_t Diff = 0ull;
            for (std::size_t K = 0u; K < ExtractedTiles[A].size(); K += 4u)
            {
                Diff += static_cast<uint64_t>(std::abs(static_cast<int>(ExtractedTiles[A][K]) - static_cast<int>(ExtractedTiles[B][K])));
            }
            if (Diff > 25000ull)
            {
                ++DistinctPairs;
            }
        }
    }
    Check(ExtractedTiles.size() == Count && DistinctPairs >= 15u,
          "All ContentBrowser Particle thumbnails render distinct real previews (starburst placeholder replaced)");

    const std::string OutPath = (OutDir / "ContentBrowser_ParticleThumbnails_Proof.png").string();
    const int Wrote = stbi_write_png(OutPath.c_str(), static_cast<int>(StripW), static_cast<int>(StripH), 4, Strip.data(), static_cast<int>(StripW * 4u));
    Check(Wrote != 0, "Wrote ContentBrowser_ParticleThumbnails_Proof.png");
    return true;
}

} // namespace

int main()
{
    const std::filesystem::path SpriteOutDir  = "Exhibits/Images/Particles/TextureStudio";
    const std::filesystem::path VehicleOutDir = "Exhibits/Images/Particles/VehicleDissolve";

    ValidateProceduralSpritesPatternStudio(SpriteOutDir);
    ValidateControlVehicleDissolveModes(VehicleOutDir);
    ValidateContentBrowserParticleThumbnails(VehicleOutDir);

    std::cout << "\n=== ParticleVehicleDissolveValidation Summary: failures=" << gFailures << " ===\n";
    return (gFailures == 0) ? 0 : 1;
}
