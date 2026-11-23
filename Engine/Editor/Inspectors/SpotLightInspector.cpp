/*============================================================================================================================================
                                                         SPOTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance SpotLightInspector with own retained beam-envelope diagram

#include "Engine/Editor/Inspectors/SpotLightInspector.h"

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

void SpotLightInspector::UpdateEnvelopeSurface(const SpotLightData& Instance)
{
    uint64_t Sig = 0x1B010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.IntensityCandela);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.FullConeDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Penumbra);

    const uint32_t W = 240u, H = 152u;
    EnvelopePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float ApexY = 30.0f;
        const float ConeH = 108.0f;
        const float HalfBottom = std::clamp(std::tan(Instance.FullConeDegrees * PiF / 360.0f) * ConeH * 1.45f, 12.0f, 96.0f);
        const float InnerScale = std::clamp(1.0f - Instance.Penumbra, 0.05f, 1.0f);
        const float Gain = std::clamp(Instance.IntensityCandela / 90.0f, 0.25f, 2.0f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                const float AX = std::fabs(DX);
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Fixture housing head at top
                if (FY >= 14.0f && FY <= 30.0f && AX <= 14.0f)
                {
                    R = 0.18f; G = 0.19f; B = 0.21f;
                    if (FY < 15.2f || FY > 28.8f || AX > 12.8f) { R = 0.42f; G = 0.44f; B = 0.48f; }
                }

                // Beam envelope cone
                if (FY > ApexY && FY <= ApexY + ConeH)
                {
                    const float T = (FY - ApexY) / ConeH;
                    const float OuterW = std::max(2.0f, HalfBottom * T);
                    const float InnerW = OuterW * InnerScale;
                    if (AX <= OuterW)
                    {
                        float Soft = 1.0f;
                        if (AX > InnerW && OuterW > InnerW)
                        {
                            const float U = (AX - InnerW) / (OuterW - InnerW);
                            Soft = (1.0f - U) * (1.0f - U);
                        }
                        const float DepthFade = (0.28f + 0.52f * T) * Gain * 0.55f;
                        R += TR * DepthFade * Soft;
                        G += TG * DepthFade * Soft;
                        B += TB * DepthFade * Soft;

                        // Outer solid cone edge
                        if (std::fabs(AX - OuterW) < 1.05f)
                        {
                            R += TR * 0.45f; G += TG * 0.45f; B += TB * 0.45f;
                        }
                        // Inner dashed penumbra edge
                        const bool Dash = (static_cast<int>(FY) % 7) < 4;
                        if (Dash && std::fabs(AX - InnerW) < 1.0f && Instance.Penumbra > 0.02f)
                        {
                            R += TR * 0.55f; G += TG * 0.55f; B += TB * 0.55f;
                        }
                        // Depth cross-section guide bars at 1/3, 2/3, 3/3
                        for (int Step = 1; Step <= 3; ++Step)
                        {
                            const float GuideY = ApexY + ConeH * (static_cast<float>(Step) / 3.0f);
                            if (std::fabs(FY - GuideY) < 0.7f && (static_cast<int>(X) % 6) < 3)
                            {
                                R += 0.12f; G += 0.12f; B += 0.13f;
                            }
                        }
                    }
                    // Center optical axis dashed line
                    if (AX < 0.7f && (static_cast<int>(FY) % 8) < 4)
                    {
                        R += 0.15f; G += 0.15f; B += 0.16f;
                    }
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float SpotLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, SpotLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Beam", "Placement", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const float BeamDiam5m = 2.0f * 5.0f * std::tan(Instance.FullConeDegrees * PiF / 360.0f);

    if (ActivePage == 0u)
    {
        UpdateEnvelopeSurface(Instance);
        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.0f deg cone · %.0f%% soft edge", Instance.FullConeDegrees, Instance.Penumbra * 100.0f);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Beam envelope", "Spot");
        EnvelopePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char CdBuf[20]{}, ConeBuf[20]{}, PenBuf[20]{}, DiamBuf[20]{};
        std::snprintf(CdBuf,   sizeof(CdBuf),   "%.1f", Instance.IntensityCandela);
        std::snprintf(ConeBuf, sizeof(ConeBuf), "%.1f", Instance.FullConeDegrees);
        std::snprintf(PenBuf,  sizeof(PenBuf),  "%.0f", Instance.Penumbra * 100.0f);
        std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", BeamDiam5m);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { CdBuf,   "cd",  "INTENSITY" },
            { ConeBuf, "deg", "FULL CONE" },
            { PenBuf,  "%",   "SOFT EDGE" },
            { DiamBuf, "m",   "AT 5 M"    },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Cast shadows", InspectorSwitchSymbol::Shadow,    Entry.CastShadows,       false },
            { "Draw cone",    InspectorSwitchSymbol::Sun,       Instance.ShowCone,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowCone = !Instance.ShowCone; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "spot" }};
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
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8210u, "Intensity", "cd", "%.1f", 0.0f, 200.0f, Instance.IntensityCandela, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8211u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8212u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 314.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Beam shaping");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char DiamBuf[20]{}; std::snprintf(DiamBuf, sizeof(DiamBuf), "%.2f", BeamDiam5m);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, DiamBuf, "m", "Beam diameter at 5 m");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8220u, "Full cone angle", "deg", "%.1f", 2.0f, 80.0f, Instance.FullConeDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8221u, "Penumbra", "", "%.2f", 0.0f, 1.0f, Instance.Penumbra, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8222u, "Reach", "m", "%.0f", 1.0f, 150.0f, Instance.ReachMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8223u, "Lens radius", "m", "%.2f", 0.02f, 0.8f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8240u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, true, "Draw cone", Instance.ShowCone, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
