/*============================================================================================================================================
                                                            LIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance LightInspector with own retained photometric beam preview

#include "Engine/Editor/Inspectors/LightInspector.h"

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

void LightInspector::UpdateBeamSurface(const LightInstanceData& Instance)
{
    uint64_t Sig = 0x11010001ull;
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Instance.LuminaireFormIndex);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LuminousPower);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.ConeDegrees);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LightTint.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LightTint.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.LightTint.B);

    const uint32_t W = 224u, H = 142u;
    BeamPreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f;
        const float Spread = std::clamp(Instance.ConeDegrees / 90.0f, 0.15f, 1.25f);
        const float Gain   = std::clamp(Instance.LuminousPower / 1200.0f, 0.25f, 2.4f);
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            const float FY = static_cast<float>(Y) + 0.5f;
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX;
                float R = 0.035f, G = 0.038f, B = 0.045f;
                const float BeamY = FY - 26.0f;
                if (BeamY > 0.0f)
                {
                    const float HalfW = 10.0f + BeamY * (0.22f + Spread * 0.72f);
                    const float Lat   = std::fabs(DX) / std::max(1.0f, HalfW);
                    if (Lat < 1.0f)
                    {
                        const float Atten = std::exp(-BeamY * 0.015f) * (1.0f - Lat * Lat) * Gain;
                        R += Instance.LightTint.R * Atten * 1.15f;
                        G += Instance.LightTint.G * Atten * 1.15f;
                        B += Instance.LightTint.B * Atten * 1.15f;
                    }
                }
                const float BulbD = std::sqrt(DX * DX + (FY - 26.0f) * (FY - 26.0f));
                if (BulbD < 10.0f)
                {
                    const float Core = 1.0f - BulbD / 10.0f;
                    R += Instance.LightTint.R * (1.2f + Core * 2.5f);
                    G += Instance.LightTint.G * (1.2f + Core * 2.5f);
                    B += Instance.LightTint.B * (1.2f + Core * 2.5f);
                }
                const std::size_t Off = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Pixels[Off + 0u] = ToneSrgb8(R); Pixels[Off + 1u] = ToneSrgb8(G); Pixels[Off + 2u] = ToneSrgb8(B); Pixels[Off + 3u] = 255u;
            }
        }
    });
}

float LightInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, LightInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 4u> Pages = {{ "Overview", "Intensity", "Beam", "Shadows" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateBeamSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 224.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Photometric beam", "Retained");
        BeamPreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 142.0f }, 12.0f);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",      InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Cast shadows", InspectorSwitchSymbol::Shadow,    Entry.CastShadows,       false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Entry.CastShadows = !Entry.CastShadows; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "lighting", "photometric" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 290.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Luminous output");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char Lm[20]{}; std::snprintf(Lm, sizeof(Lm), "%.0f", Instance.LuminousPower);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, Lm, "lm", "Radiant luminous flux");
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7110u, "Power", "lm", "%.0f", 0.0f, 12000.0f, Instance.LuminousPower, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7111u, "Temperature", "K", "%.0f", 1800.0f, 12000.0f, Instance.KelvinTemperature, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 7120u, "Light tint", Instance.LightTint, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Luminaire shape & beam");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        static constexpr std::array<const char*, 5u> Shapes = {{ "Point", "Spot", "Area", "Rect", "Tube" }};
        int32_t Chosen = -1;
        Y = InspectorPrimitives::ConstructChoiceRow(Surface, LeftX + PadX, Y, BodyW, "Luminaire form", Shapes, Instance.LuminaireFormIndex, 3u, Input, Chosen);
        if (Chosen >= 0 && Chosen < 5) { Instance.LuminaireFormIndex = static_cast<uint32_t>(Chosen); OutMutated = true; }
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7130u, "Cone spread", "deg", "%.0f", 5.0f, 160.0f, Instance.ConeDegrees, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7131u, "Source radius", "m", "%.2f", 0.01f, 3.0f, Instance.SourceRadiusMeters, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 164.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Shadow casting");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows", Entry.CastShadows, Input, OutMutated);
        (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock luminaire", Entry.Locked, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
