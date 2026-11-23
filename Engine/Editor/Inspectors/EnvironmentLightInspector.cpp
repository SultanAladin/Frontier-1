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

inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}

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

void EnvironmentLightInspector::UpdateHemisphereSurface(const EnvironmentLightInstanceData& Instance)
{
    uint64_t Sig = 0xE1010002ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.SourcePresetIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RotationDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.GainScale);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ExposureEv);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Saturation);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.GroundBounce);

    const uint32_t W = 240u, H = 152u;
    HemispherePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto& Preset = EnvironmentPresets[std::min<std::size_t>(Instance.SourcePresetIndex, EnvironmentPresets.size() - 1u)];
        const bool Uniform = (Instance.SourcePresetIndex == 4u);
        const Mathematics::LinearRGB Overhead = ApplySat(Uniform ? Instance.SkyRadiance : Preset.Overhead, Instance.Saturation);
        const Mathematics::LinearRGB Skyline  = ApplySat(Uniform ? Instance.SkyRadiance : Preset.Skyline,  Instance.Saturation);
        const Mathematics::LinearRGB Ground   = ApplySat(Preset.Ground, Instance.Saturation);
        const float Gain = Instance.GainScale * std::pow(2.0f, Instance.ExposureEv * 0.5f);

        const float AzRad = (Preset.PatchAzimuthDeg + Instance.RotationDegrees) * (PiF / 180.0f);
        const float ElRad = Preset.PatchElevationDeg * (PiF / 180.0f);
        const float SunX  = std::cos(ElRad) * std::sin(AzRad);
        const float SunY  = std::sin(ElRad);
        const float SunZ  = std::cos(ElRad) * std::cos(AzRad);

        const float SphereCX = static_cast<float>(W) * 0.34f, SphereCY = static_cast<float>(H) * 0.54f, SphereR = 36.0f;
        const float CompassCX = static_cast<float>(W) * 0.76f, CompassCY = static_cast<float>(H) * 0.54f, CompassR = 32.0f;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            const float V  = FY / static_cast<float>(H);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                // Left backdrop: subtle sky dome gradient
                float R = 0.03f, G = 0.032f, B = 0.038f;
                if (V < 0.56f)
                {
                    const float T = V / 0.56f;
                    R = (Overhead.R * (1.0f - T) + Skyline.R * T) * 0.32f * Gain;
                    G = (Overhead.G * (1.0f - T) + Skyline.G * T) * 0.32f * Gain;
                    B = (Overhead.B * (1.0f - T) + Skyline.B * T) * 0.32f * Gain;
                }
                else
                {
                    const float T = (V - 0.56f) / 0.44f;
                    R = (Skyline.R * (1.0f - T) * 0.2f + Ground.R * T * 0.25f) * Gain;
                    G = (Skyline.G * (1.0f - T) * 0.2f + Ground.G * T * 0.25f) * Gain;
                    B = (Skyline.B * (1.0f - T) * 0.2f + Ground.B * T * 0.25f) * Gain;
                }

                // Left specimen sphere illuminated by sky dome + ground bounce + key patch
                const float SX = (FX - SphereCX) / SphereR;
                const float SY = (SphereCY - FY) / SphereR;
                const float S2 = SX * SX + SY * SY;
                if (S2 <= 1.0f)
                {
                    const float SZ = std::sqrt(std::max(0.0f, 1.0f - S2));
                    const float Up = std::clamp(SY, 0.0f, 1.0f);
                    const float Dn = std::clamp(-SY, 0.0f, 1.0f);
                    const float Hor = 1.0f - std::fabs(SY);
                    float LR = Overhead.R * Up + Skyline.R * Hor * 0.75f + Ground.R * Dn * Instance.GroundBounce;
                    float LG = Overhead.G * Up + Skyline.G * Hor * 0.75f + Ground.G * Dn * Instance.GroundBounce;
                    float LB = Overhead.B * Up + Skyline.B * Hor * 0.75f + Ground.B * Dn * Instance.GroundBounce;
                    const float NdotS = std::max(0.0f, SX * SunX + SY * SunY + SZ * SunZ);
                    const float Spec  = std::pow(NdotS, 22.0f) * Preset.PatchShare * 1.4f;
                    LR += Preset.PatchTint.R * (NdotS * Preset.PatchShare * 0.9f + Spec);
                    LG += Preset.PatchTint.G * (NdotS * Preset.PatchShare * 0.9f + Spec);
                    LB += Preset.PatchTint.B * (NdotS * Preset.PatchShare * 0.9f + Spec);
                    R = LR * Gain * 0.75f; G = LG * Gain * 0.75f; B = LB * Gain * 0.75f;
                }

                // Right top-down hemisphere compass showing rotated bright patch direction
                const float CDX = FX - CompassCX, CDY = FY - CompassCY;
                const float CD  = std::sqrt(CDX * CDX + CDY * CDY);
                if (CD <= CompassR + 1.0f)
                {
                    if (CD <= CompassR)
                    {
                        const float Fade = CD / CompassR;
                        R = (Overhead.R * (1.0f - Fade) + Skyline.R * Fade) * 0.45f * Gain;
                        G = (Overhead.G * (1.0f - Fade) + Skyline.G * Fade) * 0.45f * Gain;
                        B = (Overhead.B * (1.0f - Fade) + Skyline.B * Fade) * 0.45f * Gain;
                        const float PX = CompassCX + std::sin(AzRad) * std::cos(ElRad) * (CompassR * 0.82f);
                        const float PY = CompassCY - std::cos(AzRad) * std::cos(ElRad) * (CompassR * 0.82f);
                        const float PD = std::sqrt((FX - PX) * (FX - PX) + (FY - PY) * (FY - PY));
                        if (Preset.PatchShare > 0.01f && PD < 12.0f)
                        {
                            const float Glow = std::exp(-PD * PD / 34.0f) * Preset.PatchShare * 2.2f * Gain;
                            R += Preset.PatchTint.R * Glow; G += Preset.PatchTint.G * Glow; B += Preset.PatchTint.B * Glow;
                        }
                    }
                    if (std::fabs(CD - CompassR) < 1.0f || std::fabs(CD - CompassR * 0.5f) < 0.65f)
                    {
                        R += 0.18f; G += 0.20f; B += 0.22f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        UpdateHemisphereSurface(Instance);
        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%s · %.0f deg azimuth", Preset.Title, Instance.RotationDegrees);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Environment hemisphere", Preset.Title);
        HemispherePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
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
