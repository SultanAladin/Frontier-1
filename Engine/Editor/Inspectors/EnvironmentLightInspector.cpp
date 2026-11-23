/*============================================================================================================================================
                                                      ENVIRONMENTLIGHTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance EnvironmentLightInspector with own retained hemisphere preview

#include "Engine/Editor/Inspectors/EnvironmentLightInspector.h"

#include <algorithm>
#include <array>
#include <cmath>

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

void EnvironmentLightInspector::UpdateHemisphereSurface(const EnvironmentLightInstanceData& Instance)
{
    uint64_t Sig = 0xE1010001ull;
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.R);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.G);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.SkyRadiance.B);
    Sig = InspectorPrimitives::MixRevisionFloat(Sig, Instance.GainScale);

    const uint32_t W = 224u, H = 132u;
    HemispherePreview.EnsureUpdated(Sig, W, H, [&](std::vector<uint8_t>& Pixels)
    {
        const float CX = static_cast<float>(W) * 0.5f, CY = static_cast<float>(H) * 0.52f, Rad = 44.0f;
        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float DX = static_cast<float>(X) + 0.5f - CX, DY = static_cast<float>(Y) + 0.5f - CY;
                const float D2 = (DX * DX + DY * DY) / (Rad * Rad);
                float R = 0.04f, G = 0.042f, B = 0.05f;
                if (D2 <= 1.0f)
                {
                    const float NY = -DY / Rad;
                    const float Up = std::clamp(NY * 0.5f + 0.5f, 0.0f, 1.0f);
                    R = (0.08f + Up * Instance.SkyRadiance.R * 1.4f) * Instance.GainScale;
                    G = (0.08f + Up * Instance.SkyRadiance.G * 1.4f) * Instance.GainScale;
                    B = (0.09f + Up * Instance.SkyRadiance.B * 1.4f) * Instance.GainScale;
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
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Indirect" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        UpdateHemisphereSurface(Instance);
        const Rectangle2D PrevSec { LeftX, CursorY, InnerWidth, 214.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevSec, "Sky hemisphere", "Retained");
        HemispherePreview.Present(Surface, { LeftX + PadX, CursorY + InspectorMetrics::SectionPadTop + 28.0f, BodyW, 132.0f }, 12.0f);
        CursorY = PrevSec.Bottom() + InspectorMetrics::SectionGap;

        const std::array<InspectorQuickSwitchSpec, 2u> Quick = {{
            { "Enabled",     InspectorSwitchSymbol::LightBulb, Entry.VisibilityEnabled, false },
            { "Indirect GI", InspectorSwitchSymbol::Orbit,     Instance.IndirectGi,     false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Instance.IndirectGi = !Instance.IndirectGi; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "sky-light", "indirect" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ambient & indirect GI");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, 7180u, "Sky radiance", Instance.SkyRadiance, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7190u, "Radiance gain", "x", "%.2f", 0.0f, 4.0f, Instance.GainScale, ActiveDragId, Input, OutMutated);
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
