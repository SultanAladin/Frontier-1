/*============================================================================================================================================
                                                         AREALIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance AreaLightInspector with own retained orthographic aperture elevation

#include "Engine/Editor/Inspectors/AreaLightInspector.h"

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

void AreaLightInspector::UpdateApertureSurface(const AreaLightData& Instance)
{
    uint64_t Sig = 0x1D010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.ApertureFormIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.TwoSidedRadiator ? 1u : 0u);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LuminousFlux);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.KelvinTemperature);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.RadiantTint.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.WidthMeters);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.HeightMeters);

    const uint32_t W = 240u, H = 152u;
    AperturePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const auto K = InspectorPrimitives::ComputeKelvinRgb(Instance.KelvinTemperature);
        const float TR = Instance.RadiantTint.R * K.R;
        const float TG = Instance.RadiantTint.G * K.G;
        const float TB = Instance.RadiantTint.B * K.B;
        const float CX = static_cast<float>(W) * 0.5f;
        const float CY = static_cast<float>(H) * 0.5f;
        const float Scale = std::min(152.0f / std::max(0.2f, Instance.WidthMeters), 96.0f / std::max(0.2f, Instance.HeightMeters));
        const float HW = std::clamp(Instance.WidthMeters  * Scale * 0.5f, 16.0f, 84.0f);
        const float HH = std::clamp(Instance.HeightMeters * Scale * 0.5f, 14.0f, 52.0f);
        const bool  Disk = (Instance.ApertureFormIndex == 1u);
        const float GlowGain = 0.22f + std::clamp(Instance.LuminousFlux / 8000.0f, 0.0f, 0.55f);

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float DY = static_cast<float>(Y) + 0.5f - CY;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                float R = 0.024f, G = 0.026f, B = 0.030f;

                // Orthographic dashed crosshairs
                if ((std::fabs(DX) < 0.7f && (static_cast<int>(Y) % 7) < 3) ||
                    (std::fabs(DY) < 0.7f && (static_cast<int>(X) % 7) < 3))
                {
                    R += 0.06f; G += 0.065f; B += 0.072f;
                }

                float EdgeDist = 100.0f;
                bool Inside = false;
                if (Disk)
                {
                    const float NX = DX / HW, NY = DY / HH;
                    const float E = std::sqrt(NX * NX + NY * NY);
                    Inside = (E <= 1.0f);
                    EdgeDist = std::fabs(E - 1.0f) * std::min(HW, HH);
                }
                else
                {
                    const float AX = std::fabs(DX), AY = std::fabs(DY);
                    Inside = (AX <= HW && AY <= HH);
                    EdgeDist = Inside ? std::min(HW - AX, HH - AY)
                                      : std::sqrt(std::max(0.0f, AX - HW) * std::max(0.0f, AX - HW) +
                                                  std::max(0.0f, AY - HH) * std::max(0.0f, AY - HH));
                }

                // Soft outside halo
                if (!Inside && EdgeDist < 16.0f)
                {
                    const float Halo = (1.0f - EdgeDist / 16.0f) * (1.0f - EdgeDist / 16.0f) * 0.16f;
                    R += TR * Halo; G += TG * Halo; B += TB * Halo;
                }

                // Illuminated emitter interior + diffuser micro-dot grid
                if (Inside)
                {
                    const float Diffuser = ((static_cast<int>(X) % 9) < 2 && (static_cast<int>(Y) % 9) < 2) ? 0.35f : 0.0f;
                    const float Fill = GlowGain + Diffuser;
                    R += TR * Fill; G += TG * Fill; B += TB * Fill;
                }
                if (EdgeDist < 1.3f)
                {
                    R += TR * 0.82f; G += TG * 0.82f; B += TB * 0.82f;
                }

                // Bottom dimension tick bar
                const float DimY = CY + HH + 14.0f;
                if (std::fabs(static_cast<float>(Y) - DimY) < 0.7f && std::fabs(DX) <= HW)
                {
                    R += 0.22f; G += 0.24f; B += 0.26f;
                }
                if (std::fabs(static_cast<float>(Y) - DimY) < 3.5f && std::fabs(std::fabs(DX) - HW) < 0.8f)
                {
                    R += 0.28f; G += 0.30f; B += 0.32f;
                }

                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float AreaLightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, AreaLightData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 5u> Pages = {{ "Overview", "Output", "Aperture", "Placement", "Shadows" }};
    static constexpr std::array<const char*, 2u> ApertureLabels = {{ "Rectangle", "Disk" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    const bool  DiskAperture = (Instance.ApertureFormIndex == 1u);
    const float AreaM2   = std::max(0.01f, Instance.WidthMeters * Instance.HeightMeters * (DiskAperture ? (PiF * 0.25f) : 1.0f));
    const float Density  = Instance.LuminousFlux / AreaM2;

    if (ActivePage == 0u)
    {
        UpdateApertureSurface(Instance);
        char CapBuf[64]{};
        std::snprintf(CapBuf, sizeof(CapBuf), "%s · %.1f x %.1f m aperture", DiskAperture ? "Disk" : "Rectangle", Instance.WidthMeters, Instance.HeightMeters);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 250.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Luminous surface", DiskAperture ? "Disk" : "Quad LTC");
        AperturePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 152.0f }, 12.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, PrevSec.Bottom() - 28.0f }, BodyW, 10.5f, InspectorMetrics::MutedInk, CapBuf);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        char LmBuf[20]{}, AreaBuf[20]{}, DensBuf[20]{}, SprBuf[20]{};
        std::snprintf(LmBuf,   sizeof(LmBuf),   "%.0f", Instance.LuminousFlux);
        std::snprintf(AreaBuf, sizeof(AreaBuf), "%.2f", AreaM2);
        std::snprintf(DensBuf, sizeof(DensBuf), "%.0f", Density);
        std::snprintf(SprBuf,  sizeof(SprBuf),  "%.0f", Instance.SpreadDegrees);
        const std::array<InspectorKpiMetricSpec, 4u> Tiles = {{
            { LmBuf,   "lm",    "FLUX"     },
            { AreaBuf, "m2",    "APERTURE" },
            { DensBuf, "lm/m2", "DENSITY"  },
            { SprBuf,  "deg",   "SPREAD"   },
        }};
        CursorY = InspectorPrimitives::ConstructKpiMetricGrid(Surface, LeftX, CursorY, InnerWidth, Tiles);

        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",     InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled,   false },
            { "Two-sided",   InspectorSwitchSymbol::Orbit,     Instance.TwoSidedRadiator, false },
            { "Show shape",  InspectorSwitchSymbol::Cube,      Instance.ShowFixture,      false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.TwoSidedRadiator = !Instance.TwoSidedRadiator; OutMutated = true; }
        else if (Clicked == 2) { Instance.ShowFixture = !Instance.ShowFixture; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "area-ltc" }};
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
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8410u, "Luminous flux", "lm", "%.0f", 0.0f, 20000.0f, Instance.LuminousFlux, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8411u, "Colour temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 8412u, "Emission tint", Instance.RadiantTint, ActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 384.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Emitter dimensions");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        int32_t PickShape = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Aperture shape", ApertureLabels, Instance.ApertureFormIndex, 2u, Input, PickShape);
        if (PickShape >= 0 && !Entry.Locked) { Instance.ApertureFormIndex = static_cast<uint32_t>(PickShape); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8420u, "Width", "m", "%.1f", 0.1f, 20.0f, Instance.WidthMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8421u, "Height", "m", "%.1f", 0.1f, 20.0f, Instance.HeightMeters, ActiveDragId, Input, OutMutated, Entry.Locked);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8422u, "Beam spread", "deg", "%.0f", 1.0f, 180.0f, Instance.SpreadDegrees, ActiveDragId, Input, OutMutated, Entry.Locked);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Two-sided emission", Instance.TwoSidedRadiator, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 3u)
    {
        CursorY = InspectorPrimitives::ConstructTransformSections(Surface, LeftX, CursorY, InnerWidth, Entry, 8440u, ActiveDragId, Input, OutMutated);
    }
    else
    {
        CursorY = InspectorPrimitives::ConstructLuminaireShadowsSection(Surface, LeftX, CursorY, InnerWidth, Entry, false, "Draw emitter", Instance.ShowFixture, Input, OutMutated);
    }

    return CursorY;
}

} // namespace Frontier
