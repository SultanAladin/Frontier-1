/*============================================================================================================================================
                                                         POINTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PointLightInspector with own retained radial falloff diagram

#include "Engine/Editor/Inspectors/PointLightInspector.h"

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
} // namespace

void PointLightInspector::UpdateFalloffSurface(const PointLightData& Instance)
{
    uint64_t Sig = 0x1A010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.IntensityCandela);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ReachMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.DecayExponent);

    const uint32_t W = 240u, H = 152u;
    FalloffPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = static_cast<float>(H) * 0.5f;
        const float MaxR = 62.0f;
        const float Gain = std::clamp(Instance.IntensityCandela / 24.0f, 0.2f, 2.2f);
        const float Dec  = std::clamp(Instance.DecayExponent, 0.3f, 4.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float DY = static_cast<float>(Y) + 0.5f - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float D  = std::sqrt(DX * DX + DY * DY);
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Omnidirectional inverse-power falloff glow
                const float NormD = D / MaxR;
                const float Falloff = std::pow(1.0f / (1.0f + NormD * 2.4f), Dec * 0.85f) * Gain * 0.85f;
                R += TR * Falloff; G += TG * Falloff; B += TB * Falloff;

                // 4 concentric reach rings
                for (int Ring = 1; Ring <= 4; ++Ring)
                {
                    const float RR = MaxR * (static_cast<float>(Ring) * 0.25f);
                    if (std::fabs(D - RR) < 0.75f)
                    {
                        R += 0.075f; G += 0.080f; B += 0.088f;
                    }
                }

                // 12 dashed radial rays
                if (D > 16.0f && D < MaxR)
                {
                    const float Ang = std::atan2(DY, DX);
                    const float Sector = std::fmod(Ang + PiF, PiF / 6.0f);
                    const float AngDist = std::min(Sector, (PiF / 6.0f) - Sector) * D;
                    const bool  DashOn  = (static_cast<int>(D) % 8) < 4;
                    if (AngDist < 0.7f && DashOn)
                    {
                        R += TR * 0.18f; G += TG * 0.18f; B += TB * 0.18f;
                    }
                }

                // Central luminous bulb core
                if (D < 6.5f)
                {
                    const float Core = 1.0f - D / 6.5f;
                    R += TR * (1.2f + Core * 3.0f);
                    G += TG * (1.2f + Core * 3.0f);
                    B += TB * (1.2f + Core * 3.0f);
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float PointLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PointLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Attenuation", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float Lux5m = Instance.IntensityCandela / std::pow(5.0f, std::max(0.01f, Instance.DecayExponent));

    if (ActivePage == 0u)
    {
        UpdateFalloffSurface(Instance);
        char CapBuf[56]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "Omnidirectional · decay %.1f", Instance.DecayExponent);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Radial falloff", "Point");
        FalloffPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char CdBuf[20]{}, LuxBuf[20]{}, ReachBuf[20]{}, RadBuf[20]{};
        std::snprintf(CdBuf,    sizeof(CdBuf),    "%.1f", Instance.IntensityCandela);
        std::snprintf(LuxBuf,   sizeof(LuxBuf),   "%.2f", Lux5m);
        std::snprintf(ReachBuf, sizeof(ReachBuf), "%.0f", Instance.ReachMeters);
        std::snprintf(RadBuf,   sizeof(RadBuf),   "%.0f", Instance.SourceRadiusMeters * 1000.0f);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { CdBuf,    "cd", "INTENSITY" },
            { LuxBuf,   "lx", "AT 5 M"    },
            { ReachBuf, "m",  "REACH"     },
            { RadBuf,   "mm", "BULB RAD"  },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Show glow",    InspectorSwitchSymbol::Sun,       Instance.ShowGlow,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.ShowGlow = !Instance.ShowGlow; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "point" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 294.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Source & colour");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char CdBuf[20]{}; std::snprintf(CdBuf, sizeof(CdBuf), "%.1f", Instance.IntensityCandela);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, CdBuf, "cd", "Authored luminous intensity");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8110u, "Intensity", "cd", "%.1f", 0.0f, 60.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8111u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8112u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 356.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Attenuation & distance response");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 32u> Curve {};
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float Dist = 1.0f + (static_cast<float>(I) / 31.0f) * 9.0f;
            Curve[I] = std::clamp(1.0f / std::pow(Dist, std::max(0.05f, Instance.DecayExponent)), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(
            Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve,
            ColorRgba{ 246u, 206u, 128u, 255u }, ColorRgba{ 246u, 206u, 128u, 32u }, "1 m", "10 m");
        Y += 86.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8120u, "Reach", "m", "%.0f", 1.0f, 120.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8121u, "Decay exponent", "", "%.2f", 0.0f, 4.0f, Instance.DecayExponent, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8122u, "Source radius", "m", "%.2f", 0.01f, 1.0f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8140u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Show glow", Instance.ShowGlow, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
