/*============================================================================================================================================
                                                         LEDSTRIPINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LedStripInspector with own retained SMD ribbon / opal diffuser preview

#include "Engine/Editor/Inspectors/LedStripInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
} // namespace



float LedStripInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LedStripData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Layout", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float TotalLumens = std::max(0.0f, Instance.LumensPerMetre * Instance.LengthMeters * Instance.Dimmer);
    const float TotalWatts  = std::max(0.0f, Instance.WattsPerMetre  * Instance.LengthMeters);
    const float TotalLeds   = std::max(1.0f, std::round(Instance.LengthMeters * Instance.DiodesPerMetre));
    const float LoadAmps    = TotalWatts / std::max(1.0f, Instance.SupplyVolts);

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::LedStrip, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Ribbon light", Instance.OpalDiffuser ? "Diffused" : "SMD Strip");
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WholeStr[20]{}, CapBuf[64]{};
        std::snprintf(WholeStr, sizeof(WholeStr), "%.0f", TotalLumens);
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f m run · %.0f V DC · %s", Instance.LengthMeters, Instance.SupplyVolts, Instance.OpalDiffuser ? "Opal diffuser" : "Exposed SMD");
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WholeStr, nullptr, "lm", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::LedStrip;
        Bp.TintRgb = EffTint;
        Bp.CountOrPreset = static_cast<uint32_t>(std::clamp(std::lround(TotalLeds / 10.0f), 6l, 14l));
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, WBuf[20]{}, LedBuf[20]{}, AmpBuf[20]{};
        std::snprintf(LmBuf,  sizeof(LmBuf),  "%.0f", TotalLumens);
        std::snprintf(WBuf,   sizeof(WBuf),   "%.1f", TotalWatts);
        std::snprintf(LedBuf, sizeof(LedBuf), "%.0f", TotalLeds);
        std::snprintf(AmpBuf, sizeof(AmpBuf), "%.2f", LoadAmps);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,  "lm",   "TOTAL FLUX"   },
            { WBuf,   "W",    "LOAD POWER"   },
            { LedBuf, "LEDs", "EMITTERS"     },
            { AmpBuf, "A",    "LOAD CURRENT" },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",       InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Opal diffuser", InspectorSwitchSymbol::Orbit,     Instance.OpalDiffuser,   false },
            { "Show ribbon",   InspectorSwitchSymbol::Cube,      Instance.ShowFixture,    false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.OpalDiffuser = !Instance.OpalDiffuser; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "led-strip" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 392.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Output per metre");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", TotalLumens);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Estimated ribbon flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8710u, "Flux per metre", "lm/m", "%.0f", 10.0f, 4000.0f, Instance.LumensPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8711u, "Load per metre", "W/m", "%.1f", 1.0f, 50.0f, Instance.WattsPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8712u, "Dimmer", "", "%.2f", 0.0f, 1.0f, Instance.Dimmer, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8713u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8714u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 306.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Layout & electrical budget");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char AmpBuf[20]{}; std::snprintf(AmpBuf, sizeof(AmpBuf), "%.2f", LoadAmps);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, AmpBuf, "A", "Ideal full-load current");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8720u, "Strip length", "m", "%.1f", 0.1f, 20.0f, Instance.LengthMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8721u, "Emitter density", "/m", "%.0f", 10.0f, 240.0f, Instance.DiodesPerMetre, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8722u, "Supply voltage", "V", "%.0f", 5.0f, 48.0f, Instance.SupplyVolts, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Opal diffuser", Instance.OpalDiffuser, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8740u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
