/*============================================================================================================================================
                                                        LEDLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LedLightInspector with own retained PCB die-array & optic preview

#include "Engine/Editor/Inspectors/LedLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

} // namespace



float LedLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Driver", "Optic", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float EstLumens = std::max(0.0f, Instance.DriverWatts * Instance.EfficacyLmPerW * Instance.Dimmer);

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::LedLight, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "LED emitter", "COB Array");
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WholeStr[20]{}, CapBuf[64]{};
        std::snprintf(WholeStr, sizeof(WholeStr), "%.0f", EstLumens);
        std::snprintf(CapBuf, sizeof(CapBuf), "%.0f mm package · %.0f K · %.0f° optic", Instance.PackageDiameterMm, Instance.KelvinTemperature, Instance.OpticAngleDeg);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WholeStr, nullptr, "lm", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Led;
        Bp.TintRgb = EffTint;
        Bp.SpreadDegrees = Instance.OpticAngleDeg;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, WBuf[20]{}, EffBuf[20]{}, DimBuf[20]{};
        std::snprintf(LmBuf,  sizeof(LmBuf),  "%.0f", EstLumens);
        std::snprintf(WBuf,   sizeof(WBuf),   "%.1f", Instance.DriverWatts);
        std::snprintf(EffBuf, sizeof(EffBuf), "%.0f", Instance.EfficacyLmPerW);
        std::snprintf(DimBuf, sizeof(DimBuf), "%.0f", Instance.Dimmer * 100.0f);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,  "lm",   "EST. FLUX"    },
            { WBuf,   "W",    "DRIVER POWER" },
            { EffBuf, "lm/W", "EFFICACY"     },
            { DimBuf, "%",    "DIMMER"       },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Show emitter", InspectorSwitchSymbol::Cube,      Instance.ShowFixture,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "led-cob" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 392.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Driver & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", EstLumens);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Estimated luminous flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8610u, "Driver power", "W", "%.1f", 0.1f, 100.0f, Instance.DriverWatts, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8611u, "Efficacy target", "lm/W", "%.0f", 10.0f, 250.0f, Instance.EfficacyLmPerW, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8612u, "Dimmer", "", "%.2f", 0.0f, 1.0f, Instance.Dimmer, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8613u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8614u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 218.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Package & optic");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DimBuf[20]{}; std::snprintf(DimBuf, sizeof(DimBuf), "%.0f", Instance.Dimmer * 100.0f);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DimBuf, "%", "Conversion dimmer output");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8620u, "Package diameter", "mm", "%.0f", 5.0f, 120.0f, Instance.PackageDiameterMm, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8621u, "Emission angle", "deg", "%.0f", 10.0f, 180.0f, Instance.OpticAngleDeg, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8640u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
