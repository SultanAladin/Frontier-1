/*============================================================================================================================================
                                                         AREALIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Standalone AreaLightInspector — 1:1 pure-vector port of FollowUp55_Light-Overview.png (zero CPU pixel loops, zero vkQueueWaitIdle)

#include "Engine/Editor/Inspectors/AreaLightInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

void FormatSplitNumber(float Value, char* OutWhole, std::size_t WholeCap, char* OutFrac, std::size_t FracCap) noexcept
{
    const float Clamped = std::max(0.0f, Value);
    const int64_t Whole = static_cast<int64_t>(std::floor(Clamped));
    const int32_t Tenths = static_cast<int32_t>(std::lround((Clamped - static_cast<float>(Whole)) * 10.0f)) % 10;
    if (Whole >= 1000)
        std::snprintf(OutWhole, WholeCap, "%lld,%03lld", static_cast<long long>(Whole / 1000), static_cast<long long>(Whole % 1000));
    else
        std::snprintf(OutWhole, WholeCap, "%lld", static_cast<long long>(Whole));
    std::snprintf(OutFrac, FracCap, ".%d", std::abs(Tenths));
}
} // namespace

float AreaLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AreaLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Aperture", "Placement", "Shadows" }};
    static constexpr std::array<const char*, 2u> ApertureLabels = {{ "Rectangle", "Disk" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const bool  DiskAperture = (Instance.ApertureFormIndex == 1u);
    const float AreaM2  = std::max(0.01f, Instance.WidthMeters * Instance.HeightMeters * (DiskAperture ? (PiF * 0.25f) : 1.0f));
    const float Density = Instance.LuminousFlux / AreaM2;

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffectiveTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };

        // Exact FollowUp55_Light-Overview.png Hero Card: Luminous surface + split "2,400.0 lm" + APERTURE / FRONT ELEVATION blueprint
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 302.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::LuminousSurface, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffectiveTint),
            "Luminous surface", DiskAperture ? "Disk" : "Quad LTC");

        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WholeBuf[24]{}, FracBuf[8]{}, SubBuf[64]{};
        FormatSplitNumber(Instance.LuminousFlux, WholeBuf, sizeof(WholeBuf), FracBuf, sizeof(FracBuf));
        std::snprintf(SubBuf, sizeof(SubBuf), "%.1f × %.1f m · %.0f K · %.0f lm/m²", Instance.WidthMeters, Instance.HeightMeters, Instance.KelvinTemperature, Density);
        Y = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, Y, WholeBuf, FracBuf, "lm", SubBuf, 32.0f);

        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind          = InspectorVector::LightBlueprintKind::Area;
        Bp.TintRgb       = EffectiveTint;
        Bp.WidthMeters   = Instance.WidthMeters;
        Bp.HeightMeters  = Instance.HeightMeters;
        Bp.SpreadDegrees = Instance.SpreadDegrees;
        Bp.TwoSided      = Instance.TwoSidedRadiator;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, Y + 4.0f, BodyW, 174.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, AreaBuf[20]{}, DensBuf[20]{}, SprBuf[20]{};
        std::snprintf(LmBuf,   sizeof(LmBuf),   "%.0f", Instance.LuminousFlux);
        std::snprintf(AreaBuf, sizeof(AreaBuf), "%.2f", AreaM2);
        std::snprintf(DensBuf, sizeof(DensBuf), "%.0f", Density);
        std::snprintf(SprBuf,  sizeof(SprBuf),  "%.0f", Instance.SpreadDegrees);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,   "lm",    "FLUX"     },
            { AreaBuf, "m²",    "APERTURE" },
            { DensBuf, "lm/m²", "DENSITY"  },
            { SprBuf,  "°",     "SPREAD"   },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",    InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled,   false },
            { "Two-sided",  InspectorSwitchSymbol::Orbit,     Instance.TwoSidedRadiator, false },
            { "Show shape", InspectorSwitchSymbol::Cube,      Instance.ShowFixture,      false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.TwoSidedRadiator = !Instance.TwoSidedRadiator; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "area-ltc" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 294.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Source & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", Instance.LuminousFlux);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Authored luminous flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8410u, "Luminous flux", "lm", "%.0f", 0.0f, 20000.0f, Instance.LuminousFlux, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8411u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8412u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 384.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Emitter dimensions");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        int32_t PickShape = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Aperture shape", ApertureLabels, Instance.ApertureFormIndex, 2u, Input, PickShape);
        if (PickShape >= 0 && !Entry.Locked) { Instance.ApertureFormIndex = static_cast<uint32_t>(PickShape); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8420u, "Width", "m", "%.1f", 0.1f, 20.0f, Instance.WidthMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8421u, "Height", "m", "%.1f", 0.1f, 20.0f, Instance.HeightMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8422u, "Beam spread", "°", "%.0f", 1.0f, 180.0f, Instance.SpreadDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Two-sided emission", Instance.TwoSidedRadiator, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8440u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
