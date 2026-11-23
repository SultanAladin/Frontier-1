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
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}
} // namespace

void LedStripInspector::UpdateRibbonSurface(const LedStripData& Instance)
{
    uint64_t Sig = 0x20010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.OpalDiffuser ? 1u : 0u);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LumensPerMetre);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.Dimmer);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LengthMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.DiodesPerMetre);

    const uint32_t W = 240u, H = 152u;
    RibbonPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CY = static_cast<float>(H) * 0.5f;
        const float LeftEnd = 22.0f, RightEnd = 218.0f;
        const float RunW = RightEnd - LeftEnd;
        const float Dim = std::clamp(Instance.Dimmer, 0.0f, 1.0f);
        const int   Diodes = std::clamp(static_cast<int>(std::lround(Instance.LengthMeters * Instance.DiodesPerMetre / 4.0f)), 8, 32);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            const float DY = std::fabs(FY - CY);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                float R = 0.024f, G = 0.026f, B = 0.030f;

                const bool AlongRun = (FX >= LeftEnd && FX <= RightEnd);

                // Soft radiant spill above & below ribbon
                if (AlongRun && DY < 38.0f)
                {
                    const float Spill = std::exp(-DY * 0.09f) * (Instance.OpalDiffuser ? 0.32f : 0.20f) * Dim;
                    R += TR * Spill; G += TG * Spill; B += TB * Spill;
                }

                // Extruded aluminium profile channel
                if (AlongRun && DY <= 10.5f)
                {
                    R = 0.19f; G = 0.19f; B = 0.17f;
                    if (DY <= 6.8f) { R = 0.10f; G = 0.11f; B = 0.10f; }
                }

                if (AlongRun && Instance.OpalDiffuser && DY <= 5.2f)
                {
                    // Smooth opal polycarbonate diffuser bar
                    const float Core = 1.0f - (DY / 5.2f) * 0.35f;
                    const float Lum  = (0.28f + 1.05f * Dim) * Core;
                    R = 0.08f + TR * Lum; G = 0.08f + TG * Lum; B = 0.08f + TB * Lum;
                }
                else if (AlongRun && !Instance.OpalDiffuser && DY <= 5.5f)
                {
                    // Discrete exposed SMD LED packages & copper cut pads
                    const float U = (FX - LeftEnd) / RunW * static_cast<float>(Diodes - 1);
                    const float Nearest = std::round(U);
                    const float DistX = std::fabs(FX - (LeftEnd + (Nearest / static_cast<float>(Diodes - 1)) * RunW));
                    if (DistX < 2.2f && DY < 2.3f)
                    {
                        const float Lum = 0.25f + 1.25f * Dim;
                        R = 0.05f + TR * Lum; G = 0.05f + TG * Lum; B = 0.05f + TB * Lum;
                    }
                    else if ((static_cast<int>(Nearest) % 6) == 0 && DistX < 3.6f && DY < 4.5f && (DistX > 2.8f || DY > 3.6f))
                    {
                        R = 0.56f; G = 0.48f; B = 0.24f; // Copper solder/cut pad outline
                    }
                }

                // Bottom dimension line
                if (std::fabs(FY - 128.0f) < 0.7f && AlongRun)
                {
                    R += 0.22f; G += 0.24f; B += 0.26f;
                }
                if (std::fabs(FY - 128.0f) < 3.5f && (std::fabs(FX - LeftEnd) < 0.8f || std::fabs(FX - RightEnd) < 0.8f))
                {
                    R += 0.28f; G += 0.30f; B += 0.32f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        UpdateRibbonSurface(Instance);
        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f m run · %.0f V DC · %s",
                      Instance.LengthMeters, Instance.SupplyVolts, Instance.OpalDiffuser ? "Opal diffuser" : "Exposed SMD");
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Ribbon light", Instance.OpalDiffuser ? "Diffused" : "SMD Strip");
        RibbonPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
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
