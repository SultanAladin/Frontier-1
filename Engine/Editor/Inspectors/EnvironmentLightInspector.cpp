/*============================================================================================================================================
                                                      ENVIRONMENTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance EnvironmentLightInspector with own retained dome-lit hemisphere study

#include "Engine/Editor/Inspectors/EnvironmentLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

struct EnvironmentPresetSpec
{
    const char*            Title;
    Mathematics::LinearRGB Overhead;
    Mathematics::LinearRGB Skyline;
    Mathematics::LinearRGB Ground;
    float                  ReferenceLux;
    float                  PatchAzimuthDeg;
    float                  PatchElevationDeg;
    Mathematics::LinearRGB PatchTint;
    float                  PatchShare;
};

constexpr std::array<EnvironmentPresetSpec, 5u> EnvironmentPresets = {{
    { "Sky atmosphere", { 0.25f, 0.50f, 0.88f }, { 0.74f, 0.84f, 0.95f }, { 0.35f, 0.32f, 0.28f }, 12000.0f, 140.0f, 38.0f, { 1.00f, 0.95f, 0.78f }, 0.55f },
    { "HDRI · studio",  { 0.17f, 0.17f, 0.17f }, { 0.44f, 0.44f, 0.44f }, { 0.12f, 0.12f, 0.12f },   900.0f,  60.0f, 34.0f, { 1.00f, 1.00f, 1.00f }, 0.45f },
    { "HDRI · overcast",{ 0.64f, 0.68f, 0.71f }, { 0.83f, 0.85f, 0.86f }, { 0.29f, 0.29f, 0.27f },  2500.0f,   0.0f, 70.0f, { 0.92f, 0.94f, 0.96f }, 0.10f },
    { "HDRI · sunset",  { 0.16f, 0.25f, 0.48f }, { 1.00f, 0.60f, 0.35f }, { 0.23f, 0.17f, 0.14f },  1400.0f, 250.0f,  6.0f, { 1.00f, 0.77f, 0.42f }, 0.60f },
    { "Uniform colour", { 0.62f, 0.71f, 0.80f }, { 0.62f, 0.71f, 0.80f }, { 0.32f, 0.36f, 0.40f },  1000.0f,   0.0f, 90.0f, { 1.00f, 1.00f, 1.00f }, 0.00f },
}};

inline Mathematics::LinearRGB ApplySat(Mathematics::LinearRGB C, float Sat) noexcept
{
    const float L = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
    return {
        std::max(0.0f, L + (C.R - L) * Sat),
        std::max(0.0f, L + (C.G - L) * Sat),
        std::max(0.0f, L + (C.B - L) * Sat),
    };
}
} // namespace



float EnvironmentLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, EnvironmentLightInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Source", "Response" }};
    static constexpr std::array<const char*, 5u> SourceLabels = {{
        "Sky atmosphere", "HDRI · studio", "HDRI · overcast", "HDRI · sunset", "Uniform colour"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const auto& Preset = EnvironmentPresets[std::min<std::size_t>(Instance.SourcePresetIndex, EnvironmentPresets.size() - 1u)];
    const float EffectiveLux = Preset.ReferenceLux * Instance.GainScale * std::pow(2.0f, Instance.ExposureEv);

    if (ActivePage == 0u)
    {
        const Mathematics::LinearRGB EffTint = Preset.PatchTint;
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::Environment, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Environment hemisphere", Preset.Title);
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WBuf[20]{}, FBuf[8]{}, CapBuf[64]{};
        const float Klx = EffectiveLux * 0.001f;
        std::snprintf(WBuf, sizeof(WBuf), "%.0f", std::floor(std::max(0.0f, Klx)));
        std::snprintf(FBuf, sizeof(FBuf), ".%d", static_cast<int>(std::lround((Klx - std::floor(Klx)) * 10.0f)) % 10);
        std::snprintf(CapBuf, sizeof(CapBuf), "%s · %.0f° azimuth · %+.1f EV", Preset.Title, Instance.RotationDegrees, Instance.ExposureEv);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WBuf, FBuf, "klx", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Environment;
        Bp.TintRgb = EffTint;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LuxBuf[20]{}, EvBuf[20]{}, RotBuf[20]{}, BncBuf[20]{};
        std::snprintf(LuxBuf, sizeof(LuxBuf), "%.1f", EffectiveLux * 0.001f);
        std::snprintf(EvBuf,  sizeof(EvBuf),  "%+.1f", Instance.ExposureEv);
        std::snprintf(RotBuf, sizeof(RotBuf), "%.0f", Instance.RotationDegrees);
        std::snprintf(BncBuf, sizeof(BncBuf), "%.0f", Instance.GroundBounce * 100.0f);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LuxBuf, "klx", "ILLUMINANCE" },
            { EvBuf,  "EV",  "EXPOSURE"    },
            { RotBuf, "deg", "ROTATION"    },
            { BncBuf, "%",   "GROUND BNC"  },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",     InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Indirect GI", InspectorSwitchSymbol::Orbit,     Instance.IndirectGi,     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.IndirectGi = !Instance.IndirectGi; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "sky-light", "ibl" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 370.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Environment source", Preset.Title);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        int32_t PickSrc = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Source preset", SourceLabels, Instance.SourcePresetIndex, 2u, Input, PickSrc);
        if (PickSrc >= 0) { Instance.SourcePresetIndex = static_cast<uint32_t>(PickSrc); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7181u, "Rotation", "deg", "%.0f", 0.0f, 360.0f, Instance.RotationDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 7180u, "Sky colour", Instance.SkyRadiance, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 376.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Response & indirect GI");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7190u, "Intensity", "x", "%.2f", 0.0f, 4.0f, Instance.GainScale, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7191u, "Exposure", "EV", "%+.1f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7192u, "Saturation", "", "%.2f", 0.0f, 2.0f, Instance.Saturation, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7193u, "Ground bounce", "", "%.2f", 0.0f, 1.0f, Instance.GroundBounce, ActiveDragId, Input, OutMutated);
        if (DisplaySettings)
        {
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "XeGTAO occlusion", DisplaySettings->XeGtaoEnabled, Input, OutMutated);
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Reflections", DisplaySettings->ReflectionsEnabled, Input, OutMutated);
        }
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
