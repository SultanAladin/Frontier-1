/*============================================================================================================================================
                                                           LUNARINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Moon (Lunar) Inspector (3 tabs: Overview, Appearance, Position)

#include "Engine/Editor/Inspectors/LunarInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Atmosphere/LunarStarfieldSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteLu(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

void SampleLunarAtlasBilinear(
    const std::vector<uint8_t>& Atlas,
    uint32_t PresetIdx, float U, float V,
    float& OutR, float& OutG, float& OutB) noexcept
{
    const uint32_t W = GeometricRaster::LunarStarfieldSequence::AtlasTileWidthPx;
    const uint32_t H = GeometricRaster::LunarStarfieldSequence::AtlasTileHeightPx;
    const uint32_t SafePreset = std::min(PresetIdx, GeometricRaster::LunarStarfieldSequence::AtlasPresetCount - 1u);
    const std::size_t ExpectedSize = static_cast<std::size_t>(W) * H * GeometricRaster::LunarStarfieldSequence::AtlasPresetCount * 4u;
    if (Atlas.size() < ExpectedSize)
    {
        OutR = 200.0f; OutG = 196.0f; OutB = 188.0f;
        return;
    }

    const float WrappedU = U - std::floor(U);
    const float ClampedV = std::clamp(V, 0.0f, 1.0f);
    const float FX = WrappedU * static_cast<float>(W) - 0.5f;
    const float FY = ClampedV * static_cast<float>(H) - 0.5f;
    const int32_t X0 = ((static_cast<int32_t>(std::floor(FX)) % static_cast<int32_t>(W)) + static_cast<int32_t>(W)) % static_cast<int32_t>(W);
    const int32_t X1 = (X0 + 1) % static_cast<int32_t>(W);
    const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(FY)), 0, static_cast<int32_t>(H) - 1);
    const int32_t Y1 = std::min(Y0 + 1, static_cast<int32_t>(H) - 1);
    const float TX = std::clamp(FX - std::floor(FX), 0.0f, 1.0f);
    const float TY = std::clamp(FY - std::floor(FY), 0.0f, 1.0f);

    const std::size_t TileRowOff = static_cast<std::size_t>(SafePreset) * H;
    const auto Fetch = [&](int32_t X, int32_t Y, uint32_t C) noexcept -> float
    {
        return static_cast<float>(Atlas[((TileRowOff + static_cast<std::size_t>(Y)) * W + static_cast<std::size_t>(X)) * 4u + C]);
    };

    const auto Bilerp = [&](uint32_t C) noexcept -> float
    {
        const float Top = Fetch(X0, Y0, C) + (Fetch(X1, Y0, C) - Fetch(X0, Y0, C)) * TX;
        const float Bot = Fetch(X0, Y1, C) + (Fetch(X1, Y1, C) - Fetch(X0, Y1, C)) * TX;
        return Top + (Bot - Top) * TY;
    };

    OutR = Bilerp(0u);
    OutG = Bilerp(1u);
    OutB = Bilerp(2u);
}

} // namespace

void LunarInspector::RasterizeLunarGlobe(
    InspectorPreviewSurface& Target, const GeometricRaster::LunarBodyConfiguration& Moon,
    uint32_t WidthPx, uint32_t HeightPx, float DiscRadiusPx,
    bool ApplyPhaseShadow, bool IncludeNightBackdrop, uint8_t OverridePresetIndex)
{
    const uint8_t Preset = (OverridePresetIndex < 6u) ? OverridePresetIndex : static_cast<uint8_t>(Moon.SurfacePreset);
    const uint32_t BakeFlags = static_cast<uint32_t>(Preset & 7u) |
                               (ApplyPhaseShadow ? 8u : 0u) |
                               (IncludeNightBackdrop ? 16u : 0u);
    uint64_t Key = 0x104A0002ull ^ (static_cast<uint64_t>(BakeFlags) << 8u);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Moon.SynodicPhaseFraction);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Moon.BrightnessMultiplier);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Moon.HaloGlowMultiplier);
    Key = InspectorPrimitives::MixRevisionFloat(Key, DiscRadiusPx);

    const float NormDiscR = DiscRadiusPx / std::max(1.0f, static_cast<float>(std::min(WidthPx, HeightPx)) * 0.5f);
    const auto PresetTint = GeometricRaster::LunarStarfieldSequence::QueryPresetTint(static_cast<GeometricRaster::LunarSurfacePreset>(Preset));
    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::LunarPhase,
        BakeFlags,
        { Moon.SynodicPhaseFraction, Moon.BrightnessMultiplier, Moon.HaloGlowMultiplier, NormDiscR },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { PresetTint.R, PresetTint.G, PresetTint.B, 1.0f },
        { 0.04f, 0.06f, 0.12f, 1.0f }
    };
    Target.EnsureUpdatedGpu(Key, WidthPx, HeightPx, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const auto& Atlas = GeometricRaster::LunarStarfieldSequence::QueryLunarSurfaceAtlasRgba8();
        const float Cx = static_cast<float>(WidthPx) * 0.5f;
        const float Cy = static_cast<float>(HeightPx) * 0.5f;
        const float PhaseRad = (Moon.SynodicPhaseFraction - 0.5f) * 6.2831853f;
        const float Lx = std::sin(PhaseRad), Ly = -0.14f, Lz = std::cos(PhaseRad);

        for (uint32_t Y = 0u; Y < HeightPx; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(HeightPx);
            for (uint32_t X = 0u; X < WidthPx; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);

                float R = 32.0f, G = 32.0f, B = 32.0f, A = 0.0f;
                if (IncludeNightBackdrop)
                {
                    // Deep navy starry night sky + radial lunar halo glow matching moon__1
                    R = 11.0f + V * 14.0f;
                    G = 16.0f + V * 19.0f;
                    B = 30.0f + V * 28.0f;
                    const float Halo = std::exp(-D / (DiscRadiusPx * 1.45f)) * 46.0f * std::clamp(Moon.HaloGlowMultiplier, 0.0f, 2.5f);
                    R += Halo * 0.90f; G += Halo * 0.94f; B += Halo * 1.05f;
                    if (D > DiscRadiusPx + 2.0f)
                    {
                        const uint32_t Hs = static_cast<uint32_t>(X * 73856093u ^ Y * 19349663u);
                        if ((Hs & 2047u) < 3u)
                        {
                            const float Bri = 110.0f + static_cast<float>(Hs & 95u);
                            R = std::max(R, Bri); G = std::max(G, Bri); B = std::max(B, Bri + 16.0f);
                        }
                    }
                    A = 255.0f;
                }

                if (D <= DiscRadiusPx + 1.0f)
                {
                    const float EdgeCov = std::clamp(DiscRadiusPx + 0.5f - D, 0.0f, 1.0f);
                    const float Nx = std::clamp(DX / std::max(1.0f, DiscRadiusPx), -1.0f, 1.0f);
                    const float Ny = std::clamp(DY / std::max(1.0f, DiscRadiusPx), -1.0f, 1.0f);
                    const float Nz = std::sqrt(std::max(0.0f, 1.0f - Nx * Nx - Ny * Ny));

                    // Sample real 2K Lunar Surface Atlas texture from EngineContent/Skies/LunarSurfaceAtlas.ImageSpace
                    const float LonU = std::atan2(Nx, Nz) * (0.5f / 3.14159265f) + 0.5f;
                    const float LatV = std::acos(std::clamp(-Ny, -1.0f, 1.0f)) / 3.14159265f;
                    float TexR = 200.0f, TexG = 196.0f, TexB = 188.0f;
                    SampleLunarAtlasBilinear(Atlas, Preset, LonU, LatV, TexR, TexG, TexB);

                    float Light = 0.96f * (0.58f + 0.42f * std::pow(Nz, 0.30f));
                    if (ApplyPhaseShadow)
                    {
                        const float NdotL = Nx * Lx + Ny * Ly + Nz * Lz;
                        const float Lit   = std::clamp((NdotL + 0.10f) / 0.28f, 0.0f, 1.0f);
                        Light = (0.09f + 0.96f * Lit) * (0.55f + 0.45f * std::pow(Nz, 0.32f));
                    }

                    const float GR = TexR * Light;
                    const float GG = TexG * Light;
                    const float GB = TexB * Light;
                    if (IncludeNightBackdrop)
                    {
                        R = R * (1.0f - EdgeCov) + GR * EdgeCov;
                        G = G * (1.0f - EdgeCov) + GG * EdgeCov;
                        B = B * (1.0f - EdgeCov) + GB * EdgeCov;
                        A = 255.0f;
                    }
                    else
                    {
                        R = GR; G = GG; B = GB; A = EdgeCov * 255.0f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * WidthPx + X) * 4u;
                Pixels[Off + 0u] = ClampByteLu(R);
                Pixels[Off + 1u] = ClampByteLu(G);
                Pixels[Off + 2u] = ClampByteLu(B);
                Pixels[Off + 3u] = ClampByteLu(A);
            }
        }
    });
}

float LunarInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LunarInstanceData& Instance, uint32_t DisplayOrdinal,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Appearance", "Position" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Moon    = Instance.Moon;
    const float PadX  = std::clamp(InnerWidth * 0.065f, 14.0f, 22.0f);
    const float BodyX = LeftX + PadX;
    const float BodyW = std::max(100.0f, InnerWidth - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Card 1 Moon slot (h = 138), Card 2 Lunar phase (h = 444.5), Card 3 Quick controls
        const Rectangle2D SlotCard { LeftX, CursorY, InnerWidth, 138.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SlotCard, "Moon slot");

        const Rectangle2D SlotPill { BodyX + BodyW - 96.0f, SlotCard.Y + 18.0f, 96.0f, 28.0f };
        Surface.FillRectangle(SlotPill, ColorRgba{ 16u, 16u, 16u, 255u }, 14.0f);
        Surface.StrokeRectangle(SlotPill, ColorRgba{ 255u, 255u, 255u, 16u }, 14.0f, 1.0f);
        char SlotBuf[24]{};
        std::snprintf(SlotBuf, sizeof(SlotBuf), "Moon %u", std::max(1u, DisplayOrdinal));
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SlotPill.X + 14.0f, SlotPill.Y + 8.0f }, 11.4f, InspectorMetrics::HeadingInk, SlotBuf);
        const Point2D Ch { SlotPill.Right() - 14.0f, SlotPill.Center().Y };
        Surface.StrokeLine({ Ch.X - 3.5f, Ch.Y - 1.5f }, { Ch.X, Ch.Y + 2.0f }, InspectorMetrics::HeadingInk, 1.4f);
        Surface.StrokeLine({ Ch.X, Ch.Y + 2.0f }, { Ch.X + 3.5f, Ch.Y - 1.5f }, InspectorMetrics::HeadingInk, 1.4f);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, SlotCard.Y + 68.0f, BodyW, 11.6f, 16.5f, InspectorMetrics::MutedInk,
            "Six moons can be up at once; every card edits the slot chosen here.");
        CursorY = SlotCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Lunar phase (h = 444.5)
        RasterizeLunarGlobe(PhaseGlobe, Moon, 304u, 304u, 146.0f, true, false, 255u);
        const Rectangle2D PhaseCard { LeftX, CursorY, InnerWidth, 444.5f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PhaseCard, "Lunar phase");

        const float SynodicDays = Moon.SynodicPhaseFraction * 29.53f;
        const int IllumPct = static_cast<int>(std::lround(50.0f * (1.0f - std::cos(Moon.SynodicPhaseFraction * 6.2831853f))));
        char DayBuf[24]{};
        std::snprintf(DayBuf, sizeof(DayBuf), "%.1f", SynodicDays);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, PhaseCard.Y + 62.0f, DayBuf, "days", nullptr, 36.0f);

        char IllumBuf[32]{};
        std::snprintf(IllumBuf, sizeof(IllumBuf), "%d%% illuminated", IllumPct);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PhaseCard.Y + 116.5f }, 11.4f, InspectorMetrics::MutedInk, IllumBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PhaseCard.Y + 147.0f }, 11.4f, InspectorMetrics::MutedInk, "Position in the 29.53-day synodic cycle");

        const float GlobeD = std::clamp(BodyW - 12.0f, 96.0f, 152.0f);
        const Rectangle2D GlobeBox { PhaseCard.Center().X - GlobeD * 0.5f, PhaseCard.Y + 178.0f + (152.0f - GlobeD) * 0.5f, GlobeD, GlobeD };
        PhaseGlobe.Present(Surface, GlobeBox, 0.0f, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, ColorRgba{ 0u, 0u, 0u, 0u });

        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, PhaseCard.Y + 350.0f, BodyW, 7801u, "Phase", "", "%.2f", 0.0f, 1.0f, Moon.SynodicPhaseFraction, ActiveDragId, Input, OutMutated);
        CursorY = PhaseCard.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",            InspectorSwitchSymbol::Orbit, Entry.VisibilityEnabled,    false },
            { "Opposite the sun",   InspectorSwitchSymbol::Orbit, Moon.FollowSkyOppositeSun,  false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Moon.Visible = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Moon.FollowSkyOppositeSun = !Moon.FollowSkyOppositeSun; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: APPEARANCE — Card 1 Moon catalogue (h = 231: Luna + Ember), Card 2 Moonlight, size and atmosphere (h = 460)
        RasterizeLunarGlobe(LunaPreset,  Moon, 176u, 176u, 82.0f, false, false, 0u);
        RasterizeLunarGlobe(EmberPreset, Moon, 176u, 176u, 82.0f, false, false, 1u);

        const Rectangle2D CatCard { LeftX, CursorY, InnerWidth, 231.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, CatCard, "Moon catalogue");

        const float Gap   = 12.0f;
        const float TileW = std::floor((BodyW - Gap) * 0.5f);
        const float TileH = 128.0f;
        static constexpr std::array<const char*, 2u> Names = {{ "Luna", "Ember" }};
        for (uint32_t I = 0u; I < 2u; ++I)
        {
            const Rectangle2D Tile { BodyX + static_cast<float>(I) * (TileW + Gap), CatCard.Y + 74.0f, TileW, TileH };
            const bool Sel = (static_cast<uint32_t>(Moon.SurfacePreset) == I);
            if (Sel)
            {
                Surface.FillRectangle(Tile, ColorRgba{ 36u, 38u, 42u, 255u }, InspectorMetrics::QuickTileRadius);
                Surface.StrokeRectangle(Tile, ColorRgba{ 138u, 152u, 176u, 200u }, InspectorMetrics::QuickTileRadius, 1.2f);
            }
            const Rectangle2D Img { Tile.Center().X - 42.0f, Tile.Y + 8.0f, 84.0f, 84.0f };
            (I == 0u ? LunaPreset : EmberPreset).Present(Surface, Img, 0.0f, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Contain, ColorRgba{ 0u, 0u, 0u, 0u });
            const float NW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.4f, Names[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.Center().X - NW * 0.5f, Tile.Bottom() - 22.0f }, 11.4f, InspectorMetrics::HeadingInk, Names[I]);
            if (Tile.Contains(Cursor) && Input.PrimaryPointer.Pressed)
            {
                Moon.SurfacePreset = static_cast<GeometricRaster::LunarSurfacePreset>(I);
                OutMutated = true;
            }
        }
        CursorY = CatCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Moonlight, size and atmosphere
        RasterizeLunarGlobe(NightPreview, Moon, 444u, 312u, 48.0f, true, true, 255u);
        const Rectangle2D LookCard { LeftX, CursorY, InnerWidth, 460.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LookCard, "Moonlight, size and atmosphere");

        char BriBuf[20]{}, DiaBuf[20]{};
        std::snprintf(BriBuf, sizeof(BriBuf), "%.2f\xC3\x97", Moon.BrightnessMultiplier);
        std::snprintf(DiaBuf, sizeof(DiaBuf), "%.2f\xC2\xB0", Moon.AngularDiameterDegrees);
        const char* AtmoLabel = Moon.HasAtmosphere ? "Yes" : "None";

        const float ColW = BodyW / 3.0f;
        const float KpiY = LookCard.Y + 62.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, KpiY }, 16.5f, InspectorMetrics::HeadingInk, BriBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, KpiY + 22.0f }, 10.6f, InspectorMetrics::MutedInk, "moonlight");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + ColW, KpiY }, 16.5f, InspectorMetrics::HeadingInk, DiaBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + ColW, KpiY + 22.0f }, 10.6f, InspectorMetrics::MutedInk, "across");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + ColW * 2.0f, KpiY }, 16.5f, InspectorMetrics::HeadingInk, AtmoLabel);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX + ColW * 2.0f, KpiY + 22.0f }, 10.6f, InspectorMetrics::MutedInk, "atmosphere");

        const Rectangle2D NightBox { BodyX, LookCard.Y + 114.0f, BodyW, 156.0f };
        NightPreview.Present(Surface, NightBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, NightBox.Bottom() + 18.0f }, 9.6f, InspectorMetrics::DimInk, "MOONLIGHT");
        float FY = NightBox.Bottom() + 42.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7810u, "Bright", "\xC3\x97", "%.2g", 0.0f, 6.0f, Moon.BrightnessMultiplier, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7811u, "Halo", "\xC3\x97", "%.2g", 0.0f, 3.0f, Moon.HaloGlowMultiplier, ActiveDragId, Input, OutMutated);

        CursorY = LookCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 3: POSITION — Card 1 Lunar position (h = 444: spherical sky dial + Azimuth + Elevation), Card 2 Moon rotation
        const Rectangle2D PosCard { LeftX, CursorY, InnerWidth, 444.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PosCard, "Lunar position");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, PosCard.Y + 68.0f }, 11.4f, InspectorMetrics::MutedInk, "Position in the sky \xC2\xB7 drag the spherical dial");

        // Centered spherical sky hemisphere dial matching moon__2 (responsive radius so narrow panes never clip)
        const Point2D DialC { PosCard.Center().X, PosCard.Y + 168.0f };
        const float R = std::clamp(BodyW * 0.25f, 34.0f, 54.0f);
        Surface.FillCircle(DialC, R, ColorRgba{ 28u, 34u, 44u, 255u });
        Surface.StrokeCircle(DialC, R, ColorRgba{ 118u, 134u, 156u, 140u }, 1.2f);
        Surface.StrokeCircle(DialC, R + 12.0f, ColorRgba{ 255u, 255u, 255u, 22u }, 1.0f);
        // Horizon ellipse + meridian line + center dot
        for (int Seg = 0; Seg < 36; ++Seg)
        {
            const float A0 = static_cast<float>(Seg) / 36.0f * 6.2831853f;
            const float A1 = static_cast<float>(Seg + 1) / 36.0f * 6.2831853f;
            Surface.StrokeLine({ DialC.X + std::cos(A0) * R, DialC.Y + std::sin(A0) * R * 0.34f },
                               { DialC.X + std::cos(A1) * R, DialC.Y + std::sin(A1) * R * 0.34f },
                               ColorRgba{ 148u, 164u, 184u, 125u }, 1.0f);
        }
        Surface.FillCircle(DialC, 2.0f, ColorRgba{ 225u, 232u, 240u, 210u });
        // Cardinal N / E / S / W
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialC.X - 3.5f, DialC.Y - R - 24.0f }, 9.2f, InspectorMetrics::MutedInk, "N");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialC.X + R + 17.0f, DialC.Y - 4.0f }, 9.2f, InspectorMetrics::MutedInk, "E");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialC.X - 3.0f, DialC.Y + R + 14.0f }, 9.2f, InspectorMetrics::MutedInk, "S");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialC.X - R - 26.0f, DialC.Y - 4.0f }, 9.2f, InspectorMetrics::MutedInk, "W");

        // Moon position handle dot
        const float AzRad = Moon.AzimuthDegrees * 0.01745329f;
        const float ElRad = std::clamp(Moon.ElevationDegrees, -85.0f, 85.0f) * 0.01745329f;
        const Point2D Handle { DialC.X + std::sin(AzRad) * std::cos(ElRad) * R * 0.92f, DialC.Y - std::sin(ElRad) * R * 0.85f };
        InspectorPrimitives::ConstructDashedSegment(Surface, DialC, Handle, ColorRgba{ 165u, 186u, 215u, 135u }, 2.0f, 3.0f, 1.0f);
        Surface.FillCircle(Handle, 5.5f, ColorRgba{ 168u, 200u, 238u, 255u });
        Surface.StrokeCircle(Handle, 5.5f, ColorRgba{ 242u, 248u, 255u, 220u }, 1.0f);

        const float CapW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, "Near side \xC2\xB7 drag to orbit");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DialC.X - CapW * 0.5f, DialC.Y + R + 28.0f }, 8.8f, InspectorMetrics::DimInk, "Near side \xC2\xB7 drag to orbit");

        float FY = PosCard.Y + 277.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7820u, "Azimuth",   "deg", "%.0f",   0.0f, 360.0f, Moon.AzimuthDegrees,   ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 7821u, "Elevation", "deg", "%.0f", -90.0f,  90.0f, Moon.ElevationDegrees, ActiveDragId, Input, OutMutated);
        CursorY = PosCard.Bottom() + InspectorMetrics::SectionGap;

        // Card 2: Moon rotation
        RasterizeLunarGlobe(TurnGlobe, Moon, 352u, 352u, 168.0f, false, false, 255u);
        const Rectangle2D RotCard { LeftX, CursorY, InnerWidth, 340.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, RotCard, "Moon rotation");
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, RotCard.Y + 68.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Surface orientation \xC2\xB7 independent of sky position");
        const Rectangle2D RotBox { RotCard.Center().X - 88.0f, RotCard.Y + 115.0f, 176.0f, 176.0f };
        TurnGlobe.Present(Surface, RotBox, 0.0f);
        CursorY = RotCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
