/*============================================================================================================================================
                                                          RAINBOWINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Rainbow Inspector (3 tabs: Overview, Bow, Baking)

#include "Engine/Editor/Inspectors/RainbowInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

namespace
{

[[nodiscard]] uint8_t ClampByteRb(float V) noexcept
{
    return static_cast<uint8_t>(std::clamp(std::lround(V), 0l, 255l));
}

} // namespace

void RainbowInspector::UpdateRainbowSurface(const RainbowInstanceData& Instance)
{
    uint64_t Key = 0x5A1AB0E1ull;
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.PrimaryIntensity);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SupernumeraryWidth);
    Key = InspectorPrimitives::MixRevisionFloat(Key, Instance.SecondaryIntensity);
    Key ^= AlexanderBandEnabled ? 0x1111ull : 0x2222ull;

    // Exact rainbow__0 square optical preview (222 x 208 -> render 2x crisp 444 x 416)
    const uint32_t W = 444u, H = 416u;
    InspectorGpuBakeSpec GpuSpec { InspectorGpuBakeMode::RainbowArc, AlexanderBandEnabled ? 1u : 0u, { Instance.PrimaryIntensity, Instance.SecondaryIntensity, Instance.SupernumeraryWidth, 0.0f } };
    RainbowPreview.EnsureUpdatedGpu(Key, W, H, GpuSpec, [&](std::vector<uint8_t>& Pixels)
    {
        const float Gain  = std::clamp(Instance.PrimaryIntensity, 0.0f, 2.5f);
        const float Width = std::clamp(Instance.SupernumeraryWidth, 0.25f, 3.0f);
        const float Cx    = static_cast<float>(W) * 0.50f;
        const float Cy    = static_cast<float>(H) * 0.90f;
        const float R0    = static_cast<float>(W) * 0.72f;
        const float BandW = static_cast<float>(W) * 0.062f * std::max(0.5f, Width);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float V = static_cast<float>(Y) / static_cast<float>(H);
            // Exact muted blue-grey to pale mist vertical sky gradient matching rainbow__0
            const float SkyR = 72.0f + (130.0f - 72.0f) * V;
            const float SkyG = 93.0f + (146.0f - 93.0f) * V;
            const float SkyB = 112.0f + (152.0f - 112.0f) * V;

            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) - Cx;
                const float DY = static_cast<float>(Y) - Cy;
                const float D  = std::sqrt(DX * DX + DY * DY);
                const float U  = (D - R0) / BandW; // -1 inner violet .. +1 outer red

                float R = SkyR, G = SkyG, B = SkyB;
                // Alexander's dark band outside primary bow
                if (AlexanderBandEnabled && U > 0.8f && U < 3.5f)
                {
                    const float Dark = 0.08f * std::sin(std::clamp((U - 0.8f) / 2.7f, 0.0f, 1.0f) * 3.14159f);
                    R *= (1.0f - Dark); G *= (1.0f - Dark); B *= (1.0f - Dark);
                }
                // Primary 42-deg spectral rainbow arc (violet inner -> blue -> green -> yellow -> red outer)
                if (std::fabs(U) < 1.35f)
                {
                    const float Env = std::exp(-U * U * 2.2f) * 0.28f * Gain;
                    const float Red = std::exp(-std::pow((U - 0.48f) * 2.6f, 2.0f));
                    const float Grn = std::exp(-std::pow((U - 0.05f) * 2.6f, 2.0f));
                    const float Blu = std::exp(-std::pow((U + 0.42f) * 2.6f, 2.0f));
                    R += (Red * 180.0f + Grn * 70.0f + Blu * 45.0f) * Env;
                    G += (Red * 55.0f  + Grn * 165.0f + Blu * 55.0f) * Env;
                    B += (Blu * 190.0f + Grn * 45.0f) * Env;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ClampByteRb(R);
                Pixels[Off + 1u] = ClampByteRb(G);
                Pixels[Off + 2u] = ClampByteRb(B);
                Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float RainbowInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, RainbowInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Bow", "Baking" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = 25.0f;
    const float BodyX = LeftX + PadX;
    const float BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Optical preview (h = 362), Quick controls (Enabled, Alexander's Band), Tags, Notes
        UpdateRainbowSurface(Instance);
        const Rectangle2D PrevCard { LeftX, CursorY, InnerWidth, 362.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevCard, "Optical preview");

        const Rectangle2D Box { BodyX, PrevCard.Y + 66.0f, BodyW, 208.0f };
        RainbowPreview.Present(Surface, Box, InspectorMetrics::PreviewRadius, { 255u, 255u, 255u, 255u }, InspectorPreviewFitMode::Cover);

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, PrevCard.Y + 292.0f, BodyW, 11.8f, 16.5f, InspectorMetrics::MutedInk,
            "Liquid-water optics \xC2\xB7 illustrative HTML preview, not the spectral kernel.");

        CursorY = PrevCard.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",          InspectorSwitchSymbol::PostFx, Entry.VisibilityEnabled, false },
            { "Alexander's Band", InspectorSwitchSymbol::PostFx, AlexanderBandEnabled,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { AlexanderBandEnabled = !AlexanderBandEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "environment", "optics" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        // TAB 2: BOW — Bow response (h = 379: Intensity, Width, Secondary, Minimum Path)
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 379.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Bow response");
        float FY = S.Y + 70.0f;
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6901u, "Intensity",    "\xC3\x97", "%.2g",  0.0f,    2.5f, Instance.PrimaryIntensity,   ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6902u, "Width",        "\xC3\x97", "%.2g",  0.25f,   3.0f, Instance.SupernumeraryWidth, ActiveDragId, Input, OutMutated);
        FY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6903u, "Secondary",    "",         "%.2g",  0.0f,    1.0f, Instance.SecondaryIntensity, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, FY, BodyW, 6904u, "Minimum Path", "m",        "%.0f", 20.0f, 1500.0f, MinimumPathMeters,           ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        // TAB 3: BAKING — Bake / image (h = 240.5)
        bool BakeClicked = false, LoadClicked = false;
        CursorY = InspectorPrimitives::ConstructBakeImageCard(
            Surface, LeftX, CursorY, InnerWidth, false, "UNAVAILABLE",
            "No image source connected.",
            "Renderer / imported-image binding unavailable",
            Input, BakeClicked, LoadClicked);
    }

    return CursorY;
}

} // namespace Frontier
