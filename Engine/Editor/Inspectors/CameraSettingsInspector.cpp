/*============================================================================================================================================
                                                       CAMERASETTINGSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Standalone CameraSettingsInspector — 1:1 pure-vector port of ProjectZeroEditor F78 (RenderTypeCard, clickable page rows, BudgetBar, Tracer)

#include "Engine/Editor/Inspectors/CameraSettingsInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{
struct BudgetBreakdown
{
    uint32_t ShadowSamples     = 0u;
    uint32_t GiSamples         = 0u;
    uint32_t ReflectionSamples = 0u;
    uint32_t TotalSamples      = 1u;
};

[[nodiscard]] BudgetBreakdown ComputeBudgetBreakdown(const CameraSettingsData& S) noexcept
{
    BudgetBreakdown B{};
    switch (S.RendererIndex)
    {
    case 0u: // PBR
        B.ShadowSamples     = (S.PbrShadowIndex == 2u) ? 8u : ((S.PbrShadowIndex == 1u) ? 4u : 2u);
        B.GiSamples         = S.PbrAoEnabled ? 2u : 0u;
        B.ReflectionSamples = 1u;
        break;
    case 1u: // SDF
        B.ShadowSamples     = (S.SdfShadowIndex == 2u) ? 6u : ((S.SdfShadowIndex == 1u) ? 4u : 2u);
        B.GiSamples         = S.SdfConeGiEnabled ? static_cast<uint32_t>(std::max(1.0f, std::round(S.SdfConeCount))) : 0u;
        B.ReflectionSamples = S.SdfAoEnabled ? 2u : 0u;
        break;
    case 2u: // Ray traced
        B.ShadowSamples     = static_cast<uint32_t>(std::max(1.0f, std::round(S.RayShadowRays)));
        B.GiSamples         = S.RayGiEnabled ? static_cast<uint32_t>(std::max(1.0f, std::round(S.RayGiSamples))) : 0u;
        B.ReflectionSamples = S.RayReflectionsEnabled ? static_cast<uint32_t>(std::max(1.0f, std::round(S.RayReflectionBounces * 2.0f))) : 0u;
        break;
    default: // MatCap
        B.ShadowSamples     = (S.MatcapShadowIndex > 0u) ? 2u : 0u;
        B.GiSamples         = 0u;
        B.ReflectionSamples = 0u;
        break;
    }
    B.TotalSamples = std::max(1u, B.ShadowSamples + B.GiSamples + B.ReflectionSamples);
    return B;
}

void ApplyQualityTierPreset(CameraSettingsData& S, uint32_t Tier) noexcept
{
    S.QualityTierIndex = std::min(Tier, 4u);
    static constexpr std::array<float, 5u> kSdfCones    = {{ 4.0f,  6.0f,  8.0f, 12.0f, 16.0f }};
    static constexpr std::array<float, 5u> kSdfSteps    = {{ 24.0f, 36.0f, 48.0f, 72.0f, 96.0f }};
    static constexpr std::array<float, 5u> kRayGi       = {{ 2.0f,  4.0f,  8.0f, 16.0f, 32.0f }};
    static constexpr std::array<float, 5u> kRayShadow   = {{ 1.0f,  2.0f,  4.0f,  8.0f, 12.0f }};
    static constexpr std::array<float, 5u> kShadowRes   = {{ 1024.0f, 2048.0f, 2048.0f, 4096.0f, 4096.0f }};
    const std::size_t I = static_cast<std::size_t>(S.QualityTierIndex);
    S.SdfConeCount        = kSdfCones[I];
    S.SdfTraceSteps       = kSdfSteps[I];
    S.RayGiSamples        = kRayGi[I];
    S.RayShadowRays       = kRayShadow[I];
    S.PbrShadowCascadeRes = kShadowRes[I];
}
} // namespace

float CameraSettingsInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraSettingsData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> RendererCodes  = {{ "A", "B", "C", "D" }};
    static constexpr std::array<const char*, 4u> RendererShort  = {{ "PBR", "SDF", "Ray traced", "MatCap" }};
    static constexpr std::array<const char*, 4u> RendererTitles = {{ "A · PBR", "B · SDF", "C · Ray traced", "D · MatCap" }};
    static constexpr std::array<const char*, 4u> RendererLines  = {{
        "Raster with shadow maps · sky fill",
        "Distance-field shadows · real-time cone GI",
        "BVH / path-traced light, shadows & bounce",
        "Clay studio shading to read shape",
    }};
    static constexpr std::array<std::array<const char*, 3u>, 4u> RendererPills = {{
        {{ "Shadow maps", "LTC lights", "XeGTAO" }},
        {{ "SDF shadows", "Cone GI",    "Field AO" }},
        {{ "BVH4 trace",  "Bounce GI",  "Reflections" }},
        {{ "Clay matcap", "Rim light",  "Floor shadow" }},
    }};
    static constexpr std::array<const char*, 5u> QualityLabels = {{ "Low", "Medium", "High", "Ultra", "Reference" }};

    // Per-renderer dynamic page carousel (exact F77/F78)
    std::vector<const char*> DynamicPages {};
    switch (Instance.RendererIndex)
    {
        case 0u: DynamicPages = { "Overview", "Shadows", "Lighting", "AO", "Output" }; break;
        case 1u: DynamicPages = { "Overview", "Shadows", "GI", "AO", "Output" }; break;
        case 2u: DynamicPages = { "Overview", "Tracer", "GI", "Shadows", "AO", "Reflections", "Output" }; break;
        default: DynamicPages = { "Overview", "Shadows", "Lighting", "MatCap", "Output" }; break;
    }
    if (ActivePage >= DynamicPages.size()) ActivePage = 0u;

    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, DynamicPages, ActivePage, Input, OutPageSwitched);
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Clicked = Input.PrimaryPointer.Pressed;

    const uint32_t RIdx          = std::min<uint32_t>(Instance.RendererIndex, 3u);
    const auto     Budget        = ComputeBudgetBreakdown(Instance);
    const uint32_t OutputPageIdx = static_cast<uint32_t>(DynamicPages.size() - 1u);

    if (ActivePage == 0u)
    {
        //----------------------------------------------------------------------------------------------------------------
        // CARD 1: RENDER TYPE HERO + 4-COLUMN A/B/C/D SELECTOR (Exact FollowUp68_RenderTypeCard.png)
        //----------------------------------------------------------------------------------------------------------------
        const float HeroBoxH  = 112.0f;
        const float StripBoxH = 74.0f;
        const float CardH     = InspectorMetrics::SectionPadTop + 28.0f + HeroBoxH + 10.0f + StripBoxH + InspectorMetrics::SectionPadBottom;
        const Rectangle2D HeroSec { LeftX, CursorY, InnerWidth, CardH };
        InspectorPrimitives::ConstructSectionSurface(Surface, HeroSec, "Render type", RendererShort[RIdx]);

        // Radial-styled hero banner (.rc-type-hero)
        const Rectangle2D HeroBanner { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, HeroBoxH };
        Surface.FillRectangle(HeroBanner, ColorRgba{ 22u, 24u, 29u, 255u }, 14.0f);
        Surface.FillCircle({ HeroBanner.X + 54.0f, HeroBanner.Center().Y }, 48.0f, ColorRgba{ 58u, 66u, 82u, 36u });
        Surface.StrokeRectangle(HeroBanner, ColorRgba{ 255u, 255u, 255u, 20u }, 14.0f, 1.0f);

        const Rectangle2D HeroSphereRect { HeroBanner.X + 8.0f, HeroBanner.Y + 10.0f, 92.0f, 92.0f };
        InspectorVector::ConstructRenderTypeSphere(Surface, HeroSphereRect, static_cast<InspectorVector::RenderTypeThumbKind>(RIdx), true);

        const float MetaX = HeroSphereRect.Right() + 10.0f;
        const float MetaW = HeroBanner.Right() - MetaX - 12.0f;
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { MetaX, HeroBanner.Y + 16.0f }, MetaW, 14.0f, ColorRgba{ 244u, 244u, 248u, 255u }, RendererTitles[RIdx]);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { MetaX, HeroBanner.Y + 38.0f }, MetaW, 9.8f, InspectorMetrics::MutedInk, RendererLines[RIdx]);

        // Feature pills inside hero banner
        float PillX = MetaX;
        const float PillY = HeroBanner.Bottom() - 30.0f;
        for (const char* PillTxt : RendererPills[RIdx])
        {
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, PillTxt);
            const float PW = TW + 14.0f;
            if (PillX + PW > HeroBanner.Right() - 8.0f) break;
            const Rectangle2D Pill { PillX, PillY, PW, 20.0f };
            Surface.FillRectangle(Pill, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f);
            Surface.StrokeRectangle(Pill, ColorRgba{ 255u, 255u, 255u, 18u }, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Pill.X + 7.0f, Pill.Y + 4.5f }, 8.6f, ColorRgba{ 216u, 220u, 228u, 255u }, PillTxt);
            PillX += PW + 5.0f;
        }

        // 4-column A/B/C/D selector strip (.rc-types) with red #ff3b30 active corner letter
        const float StripY = HeroBanner.Bottom() + 10.0f;
        const float TileGap = 6.0f;
        const float TileW   = (BodyW - TileGap * 3.0f) * 0.25f;
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D Tile { LeftX + PadX + static_cast<float>(I) * (TileW + TileGap), StripY, TileW, StripBoxH };
            const bool Active = (RIdx == I);
            const bool Hov    = Tile.Contains(Cur);
            Surface.FillRectangle(Tile, Active ? ColorRgba{ 30u, 32u, 38u, 255u } : (Hov ? ColorRgba{ 26u, 27u, 30u, 255u } : ColorRgba{ 18u, 19u, 22u, 255u }), 12.0f);
            Surface.StrokeRectangle(Tile, Active ? ColorRgba{ 242u, 242u, 242u, 160u } : ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 36u : 16u) }, 12.0f, Active ? 1.4f : 1.0f);

            // Corner letter A/B/C/D — exact F78 red #ff3b30 when active!
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { Tile.X + 7.0f, Tile.Y + 5.0f }, 9.2f,
                Active ? ColorRgba{ 255u, 59u, 48u, 255u } : InspectorMetrics::DimInk, RendererCodes[I]);

            InspectorVector::ConstructRenderTypeSphere(
                Surface, { Tile.X + 6.0f, Tile.Y + 11.0f, Tile.Width - 12.0f, 40.0f },
                static_cast<InspectorVector::RenderTypeThumbKind>(I), false);

            const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, RendererShort[I]);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Active ? FontWeightClass::Medium : FontWeightClass::Regular,
                { std::max(Tile.X + 4.0f, Tile.Center().X - LW * 0.5f), Tile.Bottom() - 17.0f },
                Tile.Width - 8.0f, 9.0f, Active ? ColorRgba{ 242u, 242u, 244u, 255u } : InspectorMetrics::MutedInk, RendererShort[I]);

            if (Hov && Clicked && Instance.RendererIndex != I)
            {
                Instance.RendererIndex = I;
                OutMutated = true;
            }
        }
        CursorY = HeroSec.Bottom() + InspectorMetrics::SectionGap;

        //----------------------------------------------------------------------------------------------------------------
        // CARD 2: CLICKABLE RENDERER PAGE LIST (Exact FollowUp68_RenderTypeCard.png: "Each line opens its page")
        //----------------------------------------------------------------------------------------------------------------
        struct PageRowSpec { const char* Name; char Value[40]; uint32_t TargetPage; };
        std::vector<PageRowSpec> Rows{};
        if (RIdx == 0u)
        {
            static constexpr std::array<const char*, 3u> Sh = {{ "PCF", "PCSS", "SMRT" }};
            PageRowSpec R0{ "Shadows", "", 1u }; std::snprintf(R0.Value, sizeof(R0.Value), "%s · %.0f px", Sh[std::min(Instance.PbrShadowIndex, 2u)], Instance.PbrShadowCascadeRes);
            PageRowSpec R1{ "Lighting", "", 2u }; std::snprintf(R1.Value, sizeof(R1.Value), "%s · %.2f×", Instance.PbrAreaLights ? "LTC area" : "Analytic", Instance.PbrFillGain);
            PageRowSpec R2{ "AO", "", 3u }; std::snprintf(R2.Value, sizeof(R2.Value), "%s", Instance.PbrAoEnabled ? "XeGTAO" : "Off");
            PageRowSpec R3{ "Output", "", 4u }; std::snprintf(R3.Value, sizeof(R3.Value), "%.0f%% · %.2f×", Instance.RenderScalePercent, Instance.ExposureGain);
            Rows = { R0, R1, R2, R3 };
        }
        else if (RIdx == 1u)
        {
            static constexpr std::array<const char*, 3u> SdfSh = {{ "Hard SDF", "Soft Cone", "Hybrid" }};
            PageRowSpec R0{ "Shadows", "", 1u }; std::snprintf(R0.Value, sizeof(R0.Value), "%s · %.0f steps", SdfSh[std::min(Instance.SdfShadowIndex, 2u)], Instance.SdfTraceSteps);
            PageRowSpec R1{ "GI", "", 2u }; std::snprintf(R1.Value, sizeof(R1.Value), "%s · %.0f cones", Instance.SdfConeGiEnabled ? "Real-time" : "Off", Instance.SdfConeCount);
            PageRowSpec R2{ "AO", "", 3u }; std::snprintf(R2.Value, sizeof(R2.Value), "%s", Instance.SdfAoEnabled ? "Distance-field" : "Off");
            PageRowSpec R3{ "Output", "", 4u }; std::snprintf(R3.Value, sizeof(R3.Value), "%.0f%% · %.2f×", Instance.RenderScalePercent, Instance.ExposureGain);
            Rows = { R0, R1, R2, R3 };
        }
        else if (RIdx == 2u)
        {
            static constexpr std::array<const char*, 3u> Tr = {{ "BVH4 GI", "PT game", "PT final" }};
            PageRowSpec R0{ "Tracer", "", 1u }; std::snprintf(R0.Value, sizeof(R0.Value), "%s", Tr[std::min(Instance.RayTracerIndex, 2u)]);
            PageRowSpec R1{ "GI", "", 2u }; std::snprintf(R1.Value, sizeof(R1.Value), "%s · %.0f spp", Instance.RayGiEnabled ? "Traced" : "Off", Instance.RayGiSamples);
            PageRowSpec R2{ "Shadows", "", 3u }; std::snprintf(R2.Value, sizeof(R2.Value), "%.0f rays", Instance.RayShadowRays);
            PageRowSpec R3{ "Reflections", "", 5u }; std::snprintf(R3.Value, sizeof(R3.Value), "%s · %.0f bounces", Instance.RayReflectionsEnabled ? "Traced" : "Sky", Instance.RayReflectionBounces);
            PageRowSpec R4{ "Output", "", 6u }; std::snprintf(R4.Value, sizeof(R4.Value), "%.0f%% · %.2f×", Instance.RenderScalePercent, Instance.ExposureGain);
            Rows = { R0, R1, R2, R3, R4 };
        }
        else
        {
            static constexpr std::array<const char*, 4u> Ch = {{ "Clay", "Ceramic", "Metallic", "Normal" }};
            PageRowSpec R0{ "Shadows", "", 1u }; std::snprintf(R0.Value, sizeof(R0.Value), "%s", Instance.MatcapShadowIndex == 0u ? "None" : (Instance.MatcapShadowIndex == 1u ? "Contact" : "Soft"));
            PageRowSpec R1{ "Lighting", "", 2u }; std::snprintf(R1.Value, sizeof(R1.Value), "Key %.0f° · rim %.2f", Instance.MatcapLightAngleDeg, Instance.MatcapRimGain);
            PageRowSpec R2{ "MatCap", "", 3u }; std::snprintf(R2.Value, sizeof(R2.Value), "%s", Ch[std::min(Instance.MatcapChannelIndex, 3u)]);
            PageRowSpec R3{ "Output", "", 4u }; std::snprintf(R3.Value, sizeof(R3.Value), "%.0f%% · %.2f×", Instance.RenderScalePercent, Instance.ExposureGain);
            Rows = { R0, R1, R2, R3 };
        }

        const float RowH = 34.0f;
        const float ListH = InspectorMetrics::SectionPadTop + 28.0f + static_cast<float>(Rows.size()) * (RowH + 4.0f) + InspectorMetrics::SectionPadBottom - 4.0f;
        const Rectangle2D ListSec { LeftX, CursorY, InnerWidth, ListH };
        InspectorPrimitives::ConstructSectionSurface(Surface, ListSec, RendererShort[RIdx], "Each line opens its page");
        float RY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        for (const auto& Row : Rows)
        {
            const Rectangle2D Btn { LeftX + PadX, RY, BodyW, RowH };
            const bool Hov = Btn.Contains(Cur);
            Surface.FillRectangle(Btn, Hov ? ColorRgba{ 30u, 32u, 37u, 255u } : ColorRgba{ 20u, 21u, 24u, 255u }, 10.0f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 32u : 12u) }, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Btn.X + 12.0f, Btn.Y + 10.0f }, 11.0f, ColorRgba{ 236u, 236u, 240u, 255u }, Row.Name);
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Row.Value);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Btn.Right() - 26.0f - VW, Btn.Y + 10.5f }, 10.0f, InspectorMetrics::MutedInk, Row.Value);
            InspectorVector::ConstructHtmlGlyph(Surface, InspectorVector::HtmlGlyph::ChevronRight, { Btn.Right() - 13.0f, Btn.Center().Y }, 12.0f, InspectorMetrics::DimInk);
            if (Hov && Clicked)
            {
                ActivePage = Row.TargetPage;
                OutPageSwitched = true;
            }
            RY += RowH + 4.0f;
        }
        CursorY = ListSec.Bottom() + InspectorMetrics::SectionGap;

        //----------------------------------------------------------------------------------------------------------------
        // CARD 3: QUALITY + BUDGET BAR + 5-TIER SELECTOR + RESET (Exact FollowUp68_RenderTypeCard.png)
        //----------------------------------------------------------------------------------------------------------------
        const uint32_t TierIdx = std::min<uint32_t>(Instance.QualityTierIndex, 4u);
        const Rectangle2D QualSec { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, QualSec, "Quality", QualityLabels[TierIdx]);
        float QY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;

        char SppBuf[16]{};
        std::snprintf(SppBuf, sizeof(SppBuf), "%u", Budget.TotalSamples);
        QY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, QY, SppBuf, nullptr, "samples / pixel", nullptr, 28.0f);
        QY = InspectorVector::ConstructRenderBudgetBar(Surface, LeftX + PadX, QY + 2.0f, BodyW, Budget.ShadowSamples, Budget.GiSamples, Budget.ReflectionSamples);

        // 5-tier Low | Medium | High | Ultra | Reference segmented bar
        const Rectangle2D TierRail { LeftX + PadX, QY, BodyW, 30.0f };
        Surface.FillRectangle(TierRail, ColorRgba{ 16u, 17u, 19u, 255u }, 10.0f);
        Surface.StrokeRectangle(TierRail, ColorRgba{ 255u, 255u, 255u, 14u }, 10.0f, 1.0f);
        const float SegW = (TierRail.Width - 6.0f) / 5.0f;
        for (uint32_t T = 0u; T < 5u; ++T)
        {
            const Rectangle2D Seg { TierRail.X + 3.0f + static_cast<float>(T) * SegW, TierRail.Y + 3.0f, SegW, TierRail.Height - 6.0f };
            const bool Sel = (TierIdx == T);
            const bool Hov = Seg.Contains(Cur);
            if (Sel) Surface.FillRectangle(Seg, ColorRgba{ 54u, 56u, 64u, 255u }, 7.0f);
            else if (Hov) Surface.FillRectangle(Seg, ColorRgba{ 32u, 34u, 38u, 255u }, 7.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, QualityLabels[T]);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Sel ? FontWeightClass::Medium : FontWeightClass::Regular,
                { std::max(Seg.X + 2.0f, Seg.Center().X - TW * 0.5f), Seg.Y + 6.5f },
                Seg.Width - 4.0f, 8.8f, Sel ? ColorRgba{ 244u, 244u, 248u, 255u } : InspectorMetrics::MutedInk, QualityLabels[T]);
            if (Hov && Clicked)
            {
                ApplyQualityTierPreset(Instance, T);
                OutMutated = true;
            }
        }
        QY = TierRail.Bottom() + 12.0f;

        // Footer: "Source defaults · this camera only" + Reset button
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + PadX, QY + 5.0f }, 9.2f, InspectorMetrics::DimInk, "Source defaults · this camera only");
        const Rectangle2D ResetBtn { LeftX + InnerWidth - PadX - 54.0f, QY, 54.0f, 24.0f };
        const bool ResetHov = ResetBtn.Contains(Cur);
        Surface.FillRectangle(ResetBtn, ResetHov ? ColorRgba{ 40u, 42u, 46u, 255u } : ColorRgba{ 26u, 27u, 30u, 255u }, 12.0f);
        Surface.StrokeRectangle(ResetBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(ResetHov ? 36u : 16u) }, 12.0f, 1.0f);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, "Reset");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ResetBtn.Center().X - RW * 0.5f, ResetBtn.Y + 6.0f }, 9.5f, InspectorMetrics::MutedInk, "Reset");
        if (ResetHov && Clicked)
        {
            Instance = CameraSettingsData{};
            OutMutated = true;
        }
        CursorY = QualSec.Bottom() + InspectorMetrics::SectionGap;

        static constexpr std::array<const char*, 2u> Tags = {{ "camera-settings", "renderer" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == OutputPageIdx)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 260.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Camera output");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8810u, "Render scale", "%", "%.0f", 50.0f, 200.0f, Instance.RenderScalePercent, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8811u, "Exposure gain", "×", "%.2f", 0.1f, 4.0f, Instance.ExposureGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Temporal anti-aliasing", Instance.TemporalAntiAliasing, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (Instance.RendererIndex == 0u) // PBR pages: Shadows(1), Lighting(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> ShLabels = {{ "PCF", "PCSS", "SMRT" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 378.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "PBR shadow maps");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructPenumbraStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f },
                Instance.PbrShadowIndex == 2u ? 0.72f : (Instance.PbrShadowIndex == 1u ? 0.48f : 0.22f),
                static_cast<uint32_t>(Instance.PbrShadowCascadeRes), Budget.ShadowSamples, true);
            Y += 144.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow kernel", ShLabels, Instance.PbrShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.PbrShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8820u, "Cascade resolution", "px", "%.0f", 512.0f, 4096.0f, Instance.PbrShadowCascadeRes, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 186.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "PBR direct lighting");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "LTC polygonal area lights", Instance.PbrAreaLights, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8821u, "Sky fill gain", "×", "%.2f", 0.0f, 3.0f, Instance.PbrFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ambient occlusion");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "XeGTAO screen-space AO", Instance.PbrAoEnabled, Input, OutMutated);
            if (DisplaySettings) DisplaySettings->XeGtaoEnabled = Instance.PbrAoEnabled;
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (Instance.RendererIndex == 1u) // SDF pages: Shadows(1), GI(2), AO(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> SdfSh = {{ "Hard SDF", "Soft Cone", "Hybrid" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 378.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructPenumbraStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f },
                Instance.SdfShadowIndex == 0u ? 0.14f : (Instance.SdfShadowIndex == 1u ? 0.58f : 0.42f),
                2048u, Budget.ShadowSamples, false);
            Y += 144.0f;
            int32_t Pick = -1;
            Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "SDF penumbra mode", SdfSh, Instance.SdfShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.SdfShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8830u, "Trace steps", "", "%.0f", 16.0f, 128.0f, Instance.SdfTraceSteps, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 382.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF cone GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructHemisphereStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f },
                Instance.SdfConeGiEnabled ? 1u : 0u, static_cast<uint32_t>(Instance.SdfConeCount), Instance.SdfFillGain);
            Y += 144.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Real-time cone GI", Instance.SdfConeGiEnabled, Input, OutMutated);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8831u, "Diffuse cones", "", "%.0f", 2.0f, 16.0f, Instance.SdfConeCount, ActiveDragId, Input, OutMutated, !Instance.SdfConeGiEnabled);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8832u, "Ambient fill", "×", "%.2f", 0.0f, 3.0f, Instance.SdfFillGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Distance-field AO");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable DFAO", Instance.SdfAoEnabled, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else if (Instance.RendererIndex == 2u) // Ray traced pages: Tracer(1), GI(2), Shadows(3), AO(4), Reflections(5)
    {
        if (ActivePage == 1u)
        {
            // Exact FollowUp68_RayTracerPage.png: Big "N rays / pixel" + 3 descriptive Tracer cards (BVH4 GI, PT game, PT final)
            struct TracerCardSpec { const char* Title; const char* Badge; const char* Desc; };
            static constexpr std::array<TracerCardSpec, 3u> Cards = {{
                { "BVH4 GI",  "4-wide BVH",  "Fast interactive shadows + hemisphere bounce" },
                { "PT game",  "1-bounce PT", "Low-noise real-time path trace with temporal reuse" },
                { "PT final", "Unbiased PT", "Multi-bounce reference integrator for look-dev" },
            }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 326.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Tracer", Cards[std::min(Instance.RayTracerIndex, 2u)].Title);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char RaysBuf[16]{};
            std::snprintf(RaysBuf, sizeof(RaysBuf), "%u", Budget.TotalSamples);
            Y = InspectorVector::ConstructSplitMetricReadout(
                Surface, LeftX + PadX, Y, RaysBuf, nullptr, "rays / pixel",
                "Total rays across shadows, GI, and reflections", 28.0f);
            Y += 4.0f;

            for (uint32_t I = 0u; I < 3u; ++I)
            {
                const Rectangle2D Card { LeftX + PadX, Y, BodyW, 60.0f };
                const bool Sel = (Instance.RayTracerIndex == I);
                const bool Hov = Card.Contains(Cur);
                Surface.FillRectangle(Card, Sel ? ColorRgba{ 32u, 30u, 42u, 255u } : (Hov ? ColorRgba{ 26u, 27u, 31u, 255u } : ColorRgba{ 19u, 20u, 23u, 255u }), 12.0f);
                Surface.StrokeRectangle(Card, Sel ? ColorRgba{ 197u, 163u, 255u, 180u } : ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(Hov ? 36u : 14u) }, 12.0f, Sel ? 1.3f : 1.0f);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Card.X + 12.0f, Card.Y + 10.0f }, 11.5f, Sel ? ColorRgba{ 244u, 240u, 255u, 255u } : ColorRgba{ 230u, 232u, 238u, 255u }, Cards[I].Title);
                const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Cards[I].Badge);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Card.Right() - 12.0f - BW, Card.Y + 11.5f }, 9.0f, Sel ? ColorRgba{ 197u, 163u, 255u, 255u } : InspectorMetrics::DimInk, Cards[I].Badge);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Card.X + 12.0f, Card.Y + 34.0f }, Card.Width - 24.0f, 9.5f, InspectorMetrics::MutedInk, Cards[I].Desc);
                if (Hov && Clicked)
                {
                    Instance.RayTracerIndex = I;
                    OutMutated = true;
                }
                Y += 68.0f;
            }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 332.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructHemisphereStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f },
                Instance.RayGiEnabled ? 2u : 0u, static_cast<uint32_t>(Instance.RayGiSamples), 1.0f);
            Y += 144.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable traced GI", Instance.RayGiEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8840u, "GI samples", "spp", "%.0f", 1.0f, 32.0f, Instance.RayGiSamples, ActiveDragId, Input, OutMutated, !Instance.RayGiEnabled);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 298.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "BVH shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructPenumbraStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f }, 0.55f, 2048u, static_cast<uint32_t>(Instance.RayShadowRays), false);
            Y += 144.0f;
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8841u, "Shadow rays", "spp", "%.0f", 1.0f, 16.0f, Instance.RayShadowRays, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 4u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 150.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Short-ray cavity AO");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Ray-traced AO", Instance.RayAoEnabled, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 334.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Traced reflections");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            InspectorVector::ConstructBounceStudy(
                Surface, { LeftX + PadX, Y, BodyW, 132.0f },
                Instance.RayReflectionsEnabled, static_cast<uint32_t>(Instance.RayReflectionBounces), Budget.ReflectionSamples);
            Y += 144.0f;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable reflections", Instance.RayReflectionsEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8842u, "Max bounces", "", "%.0f", 1.0f, 6.0f, Instance.RayReflectionBounces, ActiveDragId, Input, OutMutated, !Instance.RayReflectionsEnabled);
            if (DisplaySettings) DisplaySettings->ReflectionsEnabled = Instance.RayReflectionsEnabled;
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }
    else // MatCap pages: Shadows(1), Lighting(2), MatCap(3)
    {
        if (ActivePage == 1u)
        {
            static constexpr std::array<const char*, 3u> MSh = {{ "None", "Contact", "Soft" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 164.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap floor shadow");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shadow mode", MSh, Instance.MatcapShadowIndex, 3u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapShadowIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 192.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Clay key & rim");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8850u, "Key angle", "°", "%.0f", 0.0f, 360.0f, Instance.MatcapLightAngleDeg, ActiveDragId, Input, OutMutated);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8851u, "Rim highlight", "", "%.2f", 0.0f, 1.0f, Instance.MatcapRimGain, ActiveDragId, Input, OutMutated);
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            static constexpr std::array<const char*, 4u> Channels = {{ "Clay", "Ceramic", "Metallic", "Normal" }};
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 186.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap channel");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            int32_t Pick = -1;
            (void)InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Shading sphere", Channels, Instance.MatcapChannelIndex, 2u, Input, Pick);
            if (Pick >= 0) { Instance.MatcapChannelIndex = static_cast<uint32_t>(Pick); OutMutated = true; }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
    }

    return CursorY;
}

} // namespace Frontier
