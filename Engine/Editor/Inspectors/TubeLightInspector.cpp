/*============================================================================================================================================
                                                         TUBELIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance TubeLightInspector with own retained linear cylindrical radiance study

#include "Engine/Editor/Inspectors/TubeLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
} // namespace



float TubeLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, TubeLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Emitter", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float LinearLmPerM = Instance.LuminousFlux / std::max(0.1f, Instance.LengthMeters);

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::TubeLight, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Linear radiance", "Tube");
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WBuf[20]{}, CapBuf[64]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.0f", Instance.LuminousFlux);
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f m linear emitter · %.0f mm tube", Instance.LengthMeters, Instance.TubeRadiusMeters * 1000.0f);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WBuf, nullptr, "lm", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Tube;
        Bp.TintRgb = EffTint;
        Bp.WidthMeters = Instance.LengthMeters;
        Bp.HeightMeters = Instance.TubeRadiusMeters * 2.0f;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, LinBuf[20]{}, RadBuf[20]{}, LenBuf[20]{};
        std::snprintf(LmBuf,  sizeof(LmBuf),  "%.0f", Instance.LuminousFlux);
        std::snprintf(LinBuf, sizeof(LinBuf), "%.0f", LinearLmPerM);
        std::snprintf(RadBuf, sizeof(RadBuf), "%.0f", Instance.TubeRadiusMeters * 1000.0f);
        std::snprintf(LenBuf, sizeof(LenBuf), "%.1f", Instance.LengthMeters);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,  "lm",   "TOTAL FLUX"    },
            { LinBuf, "lm/m", "LINEAR OUTPUT" },
            { RadBuf, "mm",   "TUBE RADIUS"   },
            { LenBuf, "m",    "LENGTH"        },
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

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "tube" }};
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
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8510u, "Luminous flux", "lm", "%.0f", 0.0f, 12000.0f, Instance.LuminousFlux, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8511u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8512u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 268.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Emitter dimensions");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LinBuf[20]{}; std::snprintf(LinBuf, sizeof(LinBuf), "%.0f", LinearLmPerM);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LinBuf, "lm/m", "Authored flux per metre");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8520u, "Length", "m", "%.1f", 0.1f, 20.0f, Instance.LengthMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8521u, "Tube radius", "m", "%.2f", 0.01f, 1.0f, Instance.TubeRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8522u, "Reach", "m", "%.0f", 1.0f, 120.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8540u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
