/*============================================================================================================================================
                                                          IESLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance IesLightInspector with own retained C0/C90 polar candela diagram

#include "Engine/Editor/Inspectors/IesLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{
constexpr float PiF = 3.14159265359f;

// Analytical photometric lobe matching ProjectZeroEditor IesProfile.js (Section 0 = C0 plane, Section 1 = C90 plane)
[[nodiscard]] float EvaluatePresetLobe(uint32_t ProfileIdx, float SignedAngleRad, float ConeDeg, bool PlaneC90) noexcept
{
    const float Scaled = SignedAngleRad * (60.0f / std::max(5.0f, ConeDeg));
    if (std::fabs(Scaled) > PiF * 0.5f) return 0.0f;
    const float Axis = std::max(0.0f, std::cos(Scaled));
    if (ProfileIdx == 2u) // Batwing
    {
        return std::pow(std::fabs(std::sin(Scaled * 2.0f)), 1.4f) * Axis;
    }
    if (ProfileIdx == 1u) // Wall wash
    {
        return std::pow(std::max(0.0f, std::cos(Scaled - 0.55f)), PlaneC90 ? 3.0f : 5.0f);
    }
    float Power = 130.0f / 60.0f;
    if (ProfileIdx == 5u) Power = 240.0f / 60.0f; // High Beam
    else if (ProfileIdx == 6u) Power = 0.65f;     // Fog Lamp
    else if (ProfileIdx == 7u) Power = 0.40f;     // Parking Lamp
    float Lobe = std::pow(Axis, std::max(0.4f, Power * (PlaneC90 ? 0.65f : 1.0f)));
    if (ProfileIdx == 3u || ProfileIdx == 4u) // ECE / SAE Low Beam asymmetric cutoff
    {
        if (std::sin(Scaled) > 0.0f) Lobe *= (ProfileIdx == 4u) ? 0.50f : 0.65f;
    }
    return Lobe;
}
} // namespace



float IesLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, IesLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Distribution", "Placement", "Shadows" }};
    static constexpr std::array<const char*, 8u> PresetLabels = {{
        "Downlight", "Wall wash", "Batwing", "ECE Low", "SAE Low", "High Beam", "Fog Lamp", "Parking"
    }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const char* PresetName = PresetLabels[std::min<std::size_t>(Instance.ProfilePresetIndex, PresetLabels.size() - 1u)];
    const float ScaledLumens = Instance.LuminousFlux * Instance.ProfileMultiplier;

    if (ActivePage == 0u)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const Mathematics::LinearRGB EffTint { Instance.RadiantTint.R * K.R, Instance.RadiantTint.G * K.G, Instance.RadiantTint.B * K.B };
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 286.0f };
        InspectorVector::ConstructSectionSurfaceWithIcon(
            Surface, PrevSec, InspectorVector::HtmlGlyph::IesLight, InspectorPrimitives::EncodeLinearRgbToSrgb8(EffTint),
            "Photometric distribution", PresetName);
        float HY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char WBuf[20]{}, CapBuf[64]{};
        std::snprintf(WBuf, sizeof(WBuf), "%.0f", ScaledLumens);
        std::snprintf(CapBuf, sizeof(CapBuf), "%s · %.0f° field angle", PresetName, Instance.FieldAngleDegrees);
        HY = InspectorVector::ConstructSplitMetricReadout(Surface, LeftX + PadX, HY, WBuf, nullptr, "lm", CapBuf, 30.0f);
        InspectorVector::LightBlueprintParams Bp{};
        Bp.Kind = InspectorVector::LightBlueprintKind::Ies;
        Bp.TintRgb = EffTint;
        Bp.CountOrPreset = Instance.ProfilePresetIndex % 3u;
        InspectorVector::ConstructLightBlueprint(Surface, { LeftX + PadX, HY + 2.0f, BodyW, 158.0f }, Bp);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, FieldBuf[20]{}, MultBuf[20]{}, RangeBuf[20]{};
        std::snprintf(LmBuf,    sizeof(LmBuf),    "%.0f", ScaledLumens);
        std::snprintf(FieldBuf, sizeof(FieldBuf), "%.0f", Instance.FieldAngleDegrees);
        std::snprintf(MultBuf,  sizeof(MultBuf),  "%.2f", Instance.ProfileMultiplier);
        std::snprintf(RangeBuf, sizeof(RangeBuf), "%.0f", Instance.RangeMeters);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,    "lm",  "SCALED FLUX" },
            { FieldBuf, "deg", "FIELD ANGLE" },
            { MultBuf,  "x",   "MULTIPLIER"  },
            { RangeBuf, "m",   "RANGE"       },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled,     false },
            { "Cast shadows", InspectorSwitchSymbol::Shadow,    Entry.CastShadows,           false },
            { "Distribution", InspectorSwitchSymbol::Orbit,     Instance.ShowDistribution,   false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowDistribution = !Instance.ShowDistribution; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "ies-lm63" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Source & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char LmBuf[20]{}; std::snprintf(LmBuf, sizeof(LmBuf), "%.0f", ScaledLumens);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, LmBuf, "lm", "Scaled photometric flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8310u, "Source flux", "lm", "%.0f", 0.0f, 8000.0f, Instance.LuminousFlux, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8311u, "Profile multiplier", "x", "%.2f", 0.0f, 4.0f, Instance.ProfileMultiplier, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8312u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8313u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 456.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Distribution profile", PresetName);
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 32u> LobeCurve {};
        for (std::size_t I = 0u; I < LobeCurve.size(); ++I)
        {
            const float Gamma = (static_cast<float>(I) / 31.0f) * (PiF * 0.5f);
            LobeCurve[I] = std::clamp(EvaluatePresetLobe(Instance.ProfilePresetIndex, Gamma, Instance.FieldAngleDegrees, false), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(
            Surface, { LeftX + PadX, Y, BodyW, 70.0f }, LobeCurve,
            ColorRgba{ 168u, 212u, 200u, 255u }, ColorRgba{ 168u, 212u, 200u, 32u }, "0 deg", "90 deg");
        Y += 82.0f;
        int32_t PickPreset = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Photometric preset", PresetLabels, Instance.ProfilePresetIndex, 2u, Input, PickPreset);
        if (PickPreset >= 0 && !Entry.Locked) { Instance.ProfilePresetIndex = static_cast<uint32_t>(PickPreset); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8320u, "Field angle", "deg", "%.1f", 5.0f, 100.0f, Instance.FieldAngleDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8321u, "Cut-off pitch", "deg", "%.1f", -5.0f, 5.0f, Instance.CutoffPitchDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8322u, "Photometric range", "m", "%.0f", 1.0f, 250.0f, Instance.RangeMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8340u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, true, "Draw distribution", Instance.ShowDistribution, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
