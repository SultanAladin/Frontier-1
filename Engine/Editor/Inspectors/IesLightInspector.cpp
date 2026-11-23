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

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

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

void IesLightInspector::UpdatePolarSurface(const IesLightData& Instance)
{
    uint64_t Sig = 0x1C010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.ProfilePresetIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LuminousFlux);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ProfileMultiplier);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FieldAngleDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.CutoffPitchDegrees);

    const uint32_t W = 240u, H = 152u;
    PolarPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = static_cast<float>(H) * 0.48f;
        const float MaxR = 60.0f;
        const float PitchRad = Instance.CutoffPitchDegrees * (PiF / 180.0f);
        const float Gain = std::clamp((Instance.LuminousFlux * Instance.ProfileMultiplier) / 2400.0f, 0.25f, 1.8f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float DY = static_cast<float>(Y) + 0.5f - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float D  = std::sqrt(DX * DX + DY * DY);
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Polar concentric rings & 8 radial spokes
                for (int Ring = 1; Ring <= 4; ++Ring)
                {
                    if (std::fabs(D - MaxR * (static_cast<float>(Ring) * 0.25f)) < 0.7f)
                    {
                        R += 0.07f; G += 0.075f; B += 0.082f;
                    }
                }
                if (D < MaxR && D > 3.0f)
                {
                    const float Ang = std::atan2(DX, DY);
                    const float Mod = std::fmod(std::fabs(Ang), PiF * 0.25f);
                    const float SpokeDist = std::min(Mod, PiF * 0.25f - Mod) * D;
                    if (SpokeDist < 0.65f && (static_cast<int>(D) % 6) < 3)
                    {
                        R += 0.045f; G += 0.050f; B += 0.055f;
                    }

                    // C0 (solid warm-tinted) & C90 (dashed mint-tinted) polar lobes
                    const float SignedGamma = Ang - PitchRad;
                    const float R0 = MaxR * EvaluatePresetLobe(Instance.ProfilePresetIndex, SignedGamma, Instance.FieldAngleDegrees, false);
                    const float R9 = MaxR * EvaluatePresetLobe(Instance.ProfilePresetIndex, SignedGamma, Instance.FieldAngleDegrees, true);

                    if (D <= R9)
                    {
                        R += 0.035f; G += 0.055f; B += 0.052f;
                    }
                    if (D <= R0)
                    {
                        const float Fill = 0.16f + 0.14f * Gain;
                        R += TR * Fill; G += TG * Fill; B += TB * Fill;
                    }
                    if (std::fabs(D - R9) < 1.15f && (static_cast<int>(X + Y) % 7) < 4)
                    {
                        R += 0.34f; G += 0.52f; B += 0.48f;
                    }
                    if (std::fabs(D - R0) < 1.25f)
                    {
                        R += TR * 0.85f; G += TG * 0.85f; B += TB * 0.85f;
                    }
                }
                if (D < 3.0f)
                {
                    R += TR * 0.9f; G += TG * 0.9f; B += TB * 0.9f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        UpdatePolarSurface(Instance);
        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%s · C0 solid / C90 dashed", PresetName);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Photometric distribution", PresetName);
        PolarPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
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
