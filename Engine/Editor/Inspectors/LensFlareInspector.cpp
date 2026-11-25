/*============================================================================================================================================
                                                         LENSFLAREINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Lens Flare Inspector (4 tabs: Overview, Optics, Halo, Baking)

#include "Engine/Editor/Inspectors/LensFlareInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteLf(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

} // namespace

void LensFlareInspector::UpdateFlareComposite(const GeometricRaster::LensFlareConfiguration& Flare)
{
    uint64_t Key = 0x1F1A0001ull ^ (AnamorphicEnabled ? 1ull : 0ull) ^ (StreaksEnabled ? 2ull : 0ull) ^ (HalosEnabled ? 4ull : 0ull);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.FlareIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.FlareSpread);
    Key = InspectorPrimitives::MixRevisionFloat(Key, HaloRadius);

    // Exact flare__0 black optical composite canvas (222 x 196 -> 2x crisp 444 x 392)
    const uint32_t W = 444u, H = 392u;
    const float FlagsMask = static_cast<float>((AnamorphicEnabled ? 1u : 0u) | (StreaksEnabled ? 2u : 0u) | (HalosEnabled ? 4u : 0u));
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::LensFlarePreview, 0u, { Flare.FlareIntensity, HaloRadius, Flare.FlareSpread, FlagsMask } };
    FlareComposite.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Gain = std::clamp(Flare.FlareIntensity, 0.0f, 3.0f);
        const float Sx   = static_cast<float>(W) * 0.27f;
        const float Sy   = static_cast<float>(H) * 0.43f;
        const float R0   = static_cast<float>(W) * std::clamp(HaloRadius * 0.92f, 0.12f, 0.42f);

        struct GhostSpec { float T; float Rad; float R; float G; float B; };
        const std::array<GhostSpec, 5u> Ghosts = {{
            { 0.34f, 24.0f, 0.42f, 0.56f, 0.38f },
            { 0.50f, 20.0f, 0.36f, 0.50f, 0.44f },
            { 0.65f, 16.0f, 0.52f, 0.32f, 0.40f },
            { 0.80f, 12.0f, 0.62f, 0.38f, 0.24f },
            { 0.98f, 24.0f, 0.42f, 0.54f, 0.36f },
        }};
        const float Gx1 = static_cast<float>(W) * 0.76f;
        const float Gy1 = static_cast<float>(H) * 0.57f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Sx;
                const float DY = static_cast<float>(Y) - Sy;
                const float D  = std::sqrt(DX * DX + DY * DY);
                const float Ang = std::atan2(DY, DX);

                float R = 2.0f, G = 2.0f, B = 2.0f;

                // Core solar glow + radial diffraction starburst rays
                const float Core = std::exp(-D / 14.0f) * 260.0f + std::exp(-D / 52.0f) * 62.0f;
                R += Core * Gain; G += Core * 0.94f * Gain; B += Core * 0.78f * Gain;

                if (StreaksEnabled)
                {
                    const float Star = std::pow(std::fabs(std::cos(Ang * RayPairs * 0.5f)), 24.0f) * std::exp(-D / 92.0f) * 195.0f * StreakGain;
                    R += Star; G += Star * 0.92f; B += Star * 0.75f;
                }
                if (AnamorphicEnabled)
                {
                    const float Ana = std::exp(-std::fabs(DY) / 2.6f) * std::exp(-std::fabs(DX) / 145.0f) * 72.0f;
                    R += Ana * 0.72f; G += Ana * 0.84f; B += Ana * 1.10f;
                }
                if (HalosEnabled)
                {
                    const float Dr = (D - R0) / 15.0f;
                    const float Red = std::exp(-std::pow(Dr - 0.35f, 2.0f) * 1.8f);
                    const float Grn = std::exp(-std::pow(Dr, 2.0f) * 1.8f);
                    const float Blu = std::exp(-std::pow(Dr + 0.35f, 2.0f) * 1.8f);
                    const float HGain = 78.0f * Flare.HaloIntensity;
                    R += Red * HGain; G += Grn * HGain * 0.88f; B += Blu * HGain * 0.85f;
                }

                // Multi-coated polygonal/circular ghosts along optical axis
                for (const auto& Gh : Ghosts)
                {
                    const float GX = Sx + (Gx1 - Sx) * Gh.T;
                    const float GY = Sy + (Gy1 - Sy) * Gh.T;
                    const float GD = std::hypot(static_cast<float>(X) - GX, static_cast<float>(Y) - GY);
                    if (GD < Gh.Rad + 3.0f)
                    {
                        const float Fill = (GD < Gh.Rad) ? (0.14f + 0.22f * std::pow(GD / Gh.Rad, 3.0f)) : 0.0f;
                        const float Rim  = std::exp(-std::pow((GD - Gh.Rad) / 1.8f, 2.0f)) * 0.42f;
                        const float Amp  = (Fill + Rim) * 165.0f * Flare.GhostIntensity;
                        R += Amp * Gh.R; G += Amp * Gh.G; B += Amp * Gh.B;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteLf(R);
                Pixels[Off + 1u] = ClampByteLf(G);
                Pixels[Off + 2u] = ClampByteLf(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

void LensFlareInspector::UpdateFlareAtlas(const GeometricRaster::LensFlareConfiguration& Flare)
{
    uint64_t Key = 0x1F1A4404ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Flare.FlareIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, RayPairs);

    // Exact 4x2 Lights/Ghosts optical atlas (188 x 94 -> 2x crisp 376 x 188) matching flare__3
    const uint32_t W = 376u, H = 188u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::LensFlarePreview, 1u, { Flare.FlareIntensity, RayPairs, 0.0f, 0.0f } };
    FlareAtlas.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float CellW = static_cast<float>(W) * 0.25f;
        const float CellH = static_cast<float>(H) * 0.50f;
        const std::array<std::array<float, 3u>, 4u> GhostCols = {{
            { 0.92f, 0.58f, 0.30f }, { 0.55f, 0.78f, 0.42f },
            { 0.58f, 0.64f, 0.72f }, { 0.84f, 0.44f, 0.56f },
        }};

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const uint32_t Row = (Y < H / 2u) ? 0u : 1u;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const uint32_t Col = std::min(3u, static_cast<uint32_t>(static_cast<float>(X) / CellW));
                const float LX = (static_cast<float>(X) - (static_cast<float>(Col) + 0.5f) * CellW) / (CellW * 0.5f);
                const float LY = (static_cast<float>(Y) - (static_cast<float>(Row) + 0.5f) * CellH) / (CellH * 0.5f);
                const float D  = std::sqrt(LX * LX + LY * LY);

                float R = 3.0f, G = 3.0f, B = 4.0f;
                if (Row == 0u)
                {
                    if (Col == 0u) // Soft warm glow disc
                    {
                        const float Glow = std::exp(-D * D * 4.5f) * 235.0f;
                        R += Glow; G += Glow * 0.85f; B += Glow * 0.65f;
                    }
                    else if (Col == 1u) // Diffraction starburst
                    {
                        const float Ang = std::atan2(LY, LX);
                        const float Ray = std::pow(std::fabs(std::cos(Ang * 3.0f)), 22.0f) * std::exp(-D * 1.9f) * 240.0f;
                        const float Dot = std::exp(-D * D * 40.0f) * 220.0f;
                        R += Ray + Dot; G += Ray + Dot; B += Ray + Dot;
                    }
                    else if (Col == 2u) // Rainbow spectral halo ring
                    {
                        const float Dr = (D - 0.74f) / 0.11f;
                        R += std::exp(-std::pow(Dr - 0.4f, 2.0f)) * 220.0f;
                        G += std::exp(-std::pow(Dr, 2.0f)) * 190.0f;
                        B += std::exp(-std::pow(Dr + 0.4f, 2.0f)) * 210.0f;
                    }
                    else // Horizontal blue anamorphic streak
                    {
                        const float St = std::exp(-std::fabs(LY) * 14.0f) * std::exp(-std::fabs(LX) * 2.2f) * 230.0f;
                        R += St * 0.68f; G += St * 0.84f; B += St * 1.15f;
                    }
                }
                else
                {
                    // Bottom row: 4 tinted coated aperture ghost discs
                    const auto& C = GhostCols[Col];
                    const float Rad = 0.78f;
                    if (D < Rad + 0.08f)
                    {
                        const float Fill = (D < Rad) ? (0.34f + 0.18f * std::pow(D / Rad, 2.5f)) : 0.0f;
                        const float Rim  = std::exp(-std::pow((D - Rad) / 0.055f, 2.0f)) * 0.52f;
                        const float Amp  = (Fill + Rim) * 215.0f;
                        R += Amp * C[0]; G += Amp * C[1]; B += Amp * C[2];
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteLf(R);
                Pixels[Off + 1u] = ClampByteLf(G);
                Pixels[Off + 2u] = ClampByteLf(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LensFlareInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LensFlareInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Optics", "Halo", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    auto& Flare   = Instance.Flare;
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Flare composite (h = 425), Quick controls (4 switches: Enabled, Anamorphic, Streaks, Halos)
        UpdateFlareComposite(Flare);
        const Rectangle2D CompCard { LeftX, CursorY, InnerWidth, 425.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, CompCard, "Flare composite");

        char GainBuf[16]{};
        std::snprintf(GainBuf, sizeof(GainBuf), "%.2f", Flare.FlareIntensity);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, CompCard.Y + 62.0f, GainBuf, "\xC3\x97", nullptr, 36.0f);

        const Rectangle2D ImgBox { BodyX, CompCard.Y + 122.5f, BodyW, 196.0f };
        FlareComposite.Present(Surface, ImgBox, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, CompCard.Y + 338.5f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Drag the sun \xC2\xB7 drawn by the 3D viewport's flare code from the atlas below, over black. Only this preview moves.");
        CursorY = CompCard.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",    InspectorSwitchSymbol::Stars,  Entry.VisibilityEnabled, false },
            { "Anamorphic", InspectorSwitchSymbol::PostFx, AnamorphicEnabled,       false },
            { "Streaks",    InspectorSwitchSymbol::Sun,    StreaksEnabled,          false },
            { "Halos",      InspectorSwitchSymbol::Orbit,  HalosEnabled,            false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Flare.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { AnamorphicEnabled = !AnamorphicEnabled; OutMutated = true; }
        else if (Clicked == 2) { StreaksEnabled = !StreaksEnabled; OutMutated = true; }
        else if (Clicked == 3) { HalosEnabled = !HalosEnabled; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: OPTICS — Card 1 Flare layers (h = 432), Card 2 Lens ghosts (h = 220)
        const Rectangle2D LayCard { LeftX, CursorY, InnerWidth, 432.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LayCard, "Flare layers");
        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, LayCard.Y + 68.0f, BodyW, 11.4f, 16.5f, InspectorMetrics::MutedInk,
            "Combine layers instead of choosing just one. Editing a layer selects Custom layers.");

        float FY = LayCard.Y + 121.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8301u, "Spread",      "\xC3\x97", "%.2g", 0.2f,  2.0f, Flare.FlareSpread,           ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8302u, "Rotation",    "deg",      "%.0f", 0.0f, 360.0f, Flare.StreakRotationDegrees, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8303u, "Ray pairs",   "",         "%.0f", 1.0f,  12.0f, RayPairs,                    ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8304u, "Streak gain","\xC3\x97", "%.2g", 0.0f,   3.0f, StreakGain,                  ActiveDragId, Input, OutMutated);
        CursorY = LayCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D GhostCard { LeftX, CursorY, InnerWidth, 220.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, GhostCard, "Lens ghosts");
        char GhBuf[16]{};
        std::snprintf(GhBuf, sizeof(GhBuf), "%u", Flare.GhostCount);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, GhostCard.Y + 62.0f, GhBuf, "elements", nullptr, 36.0f);
        float GhF = static_cast<float>(Flare.GhostCount);
        bool GhCh = false;
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, GhostCard.Y + 122.0f, BodyW, 8305u, "Ghosts", "", "%.0f", 0.0f, 16.0f, GhF, ActiveDragId, Input, GhCh);
        if (GhCh) { Flare.GhostCount = static_cast<uint32_t>(std::clamp(std::lround(GhF), 0l, 16l)); OutMutated = true; }
        CursorY = GhostCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        // TAB 3: HALO — Halo & optical response (h = 379)
        const Rectangle2D HaloCard { LeftX, CursorY, InnerWidth, 379.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, HaloCard, "Halo & optical response");
        float FY = HaloCard.Y + 68.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8310u, "Halo Radius",     "",         "%.2g", 0.05f, 0.8f,  HaloRadius,          ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8311u, "Halo brightness", "\xC3\x97", "%.2g", 0.0f,  2.5f,  Flare.HaloIntensity, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8312u, "Halo width",      "",         "%.3g", 0.005f,0.15f, HaloWidth,           ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 8313u, "Chromatic",      "",         "%.2g", 0.0f,  1.0f,  ChromaticDisp,       ActiveDragId, Input, OutMutated);
        CursorY = HaloCard.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 4: BAKING — BAKING eyebrow + Lens flare image (h = 262)
        UpdateFlareAtlas(Flare);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, "BAKING");
        const char* SubRight = "Browser bake \xC2\xB7 the 3D viewport uses this";
        const float SRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, SubRight);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + InnerWidth - SRW, CursorY + 2.0f }, 9.6f, InspectorMetrics::DimInk, SubRight);
        CursorY += 34.0f;

        const Rectangle2D AtlasCard { LeftX, CursorY, InnerWidth, 262.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, AtlasCard, "Lens flare image");

        const Rectangle2D BakedPill { BodyX, AtlasCard.Y + 64.0f, 64.0f, 24.0f };
        Surface.FillRectangle(BakedPill, ColorRgba{ 26u, 46u, 34u, 255u }, 12.0f);
        Surface.StrokeRectangle(BakedPill, ColorRgba{ 76u, 158u, 102u, 155u }, 12.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakedPill.X + 14.0f, BakedPill.Y + 6.0f }, 10.4f, ColorRgba{ 126u, 228u, 156u, 255u }, "Baked");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BakedPill.Right() + 10.0f, BakedPill.Y + 6.0f }, 10.4f, InspectorMetrics::MutedInk, "1024 \xC3\x97 512");

        const Rectangle2D AtlasBox { BodyX + 34.0f, AtlasCard.Y + 102.0f, BodyW - 34.0f, 94.0f };
        FlareAtlas.Present(Surface, AtlasBox, 10.0f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, AtlasBox.Y + 18.0f }, 9.2f, InspectorMetrics::DimInk, "Lights");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, AtlasBox.Y + 64.0f }, 9.2f, InspectorMetrics::DimInk, "Ghosts");
        for (int I = 0; I < 4; ++I)
        {
            char N[4]{};
            std::snprintf(N, sizeof(N), "%d", I + 1);
            const float NX = AtlasBox.X + (AtlasBox.Width / 4.0f) * (static_cast<float>(I) + 0.45f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { NX, AtlasBox.Bottom() + 7.0f }, 9.4f, InspectorMetrics::DimInk, N);
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, AtlasBox.Bottom() + 26.0f }, 10.4f, InspectorMetrics::MutedInk, "Point at the image to read it");

        CursorY = AtlasCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
