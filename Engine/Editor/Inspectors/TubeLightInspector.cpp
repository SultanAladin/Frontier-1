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
inline uint8_t ToneSrgb8(float Hdr) noexcept
{
    const float M = std::clamp(Hdr / (1.0f + std::max(0.0f, Hdr)), 0.0f, 1.0f);
    const float S = (M <= 0.0031308f) ? (M * 12.92f) : (1.055f * std::pow(M, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
}
} // namespace

void TubeLightInspector::UpdateTubeSurface(const TubeLightData& Instance)
{
    uint64_t Sig = 0x1E010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LuminousFlux);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LengthMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.TubeRadiusMeters);

    const uint32_t W = 240u, H = 152u;
    TubePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = 60.0f;
        const float HalfLen = std::clamp(58.0f + Instance.LengthMeters * 14.0f, 64.0f, 100.0f);
        const float TubeR   = std::clamp(5.0f + Instance.TubeRadiusMeters * 65.0f, 5.5f, 16.0f);
        const float Gain    = 0.35f + std::clamp(Instance.LuminousFlux / 4500.0f, 0.0f, 0.85f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            const float DY = FY - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float FX = static_cast<float>(X) + 0.5f;
                const float DX = FX - CX;
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Capsule SDF for cylindrical glass tube
                const float ClampedX = std::clamp(DX, -(HalfLen - TubeR), (HalfLen - TubeR));
                const float Dist = std::sqrt((DX - ClampedX) * (DX - ClampedX) + DY * DY) - TubeR;

                // Soft radiant halo around tube
                if (Dist > 0.0f && Dist < 34.0f)
                {
                    const float Halo = std::exp(-Dist * 0.11f) * 0.34f * Gain;
                    R += TR * Halo; G += TG * Halo; B += TB * Halo;
                }

                // Glowing cylindrical tube body
                if (Dist <= 0.0f)
                {
                    const float CapZone = (std::fabs(DX) > HalfLen - 8.0f);
                    if (CapZone)
                    {
                        const float Shade = 0.24f + 0.14f * (1.0f - std::fabs(DY) / TubeR);
                        R = Shade; G = Shade * 1.03f; B = Shade * 1.06f;
                    }
                    else
                    {
                        const float CylCore = 1.0f - std::clamp(std::fabs(DY) / TubeR, 0.0f, 1.0f);
                        const float Lum = Gain * (0.45f + 1.15f * std::pow(CylCore, 0.65f));
                        R += TR * Lum; G += TG * Lum; B += TB * Lum;
                    }
                }
                else if (Dist < 1.1f)
                {
                    R += TR * 0.65f; G += TG * 0.65f; B += TB * 0.65f;
                }

                // 9 downward dashed radiance rays beneath tube
                if (FY > CY + TubeR + 8.0f && FY < 126.0f && std::fabs(DX) <= HalfLen)
                {
                    for (int Ray = 0; Ray < 9; ++Ray)
                    {
                        const float RayX = (CX - HalfLen) + (2.0f * HalfLen * static_cast<float>(Ray) / 8.0f);
                        if (std::fabs(FX - RayX) < 0.7f && (static_cast<int>(Y) % 7) < 4)
                        {
                            R += TR * 0.22f; G += TG * 0.22f; B += TB * 0.22f;
                        }
                    }
                }

                // Floor dimension line
                if (std::fabs(FY - 134.0f) < 0.7f && std::fabs(DX) <= HalfLen)
                {
                    R += 0.22f; G += 0.24f; B += 0.26f;
                }
                if (std::fabs(FY - 134.0f) < 3.5f && std::fabs(std::fabs(DX) - HalfLen) < 0.8f)
                {
                    R += 0.28f; G += 0.30f; B += 0.32f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

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
        UpdateTubeSurface(Instance);
        char CapBuf[56]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f m linear emitter · %.0f mm tube", Instance.LengthMeters, Instance.TubeRadiusMeters * 1000.0f);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Linear radiance", "Tube");
        TubePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
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
