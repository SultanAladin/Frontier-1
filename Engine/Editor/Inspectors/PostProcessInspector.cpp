/*============================================================================================================================================
                                                         POSTPROCESSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of standalone multi-instance PostProcessInspector

#include "Engine/Editor/Inspectors/PostProcessInspector.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

float PostProcessInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, PostProcessInstanceData& Instance,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    static constexpr std::array<const char*, 3u> Pages = {{ "Overview", "Tone", "Colour" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX = InspectorMetrics::SectionPadX, BodyW = InnerWidth - PadX * 2.0f;

    if (ActivePage == 0u)
    {
        const bool AaOn   = DisplaySettings ? DisplaySettings->AntiAliasingEnabled     : true;
        const bool ReflOn = DisplaySettings ? DisplaySettings->ReflectionsEnabled      : true;
        const bool AoOn   = DisplaySettings ? DisplaySettings->XeGtaoEnabled           : false;
        const std::array<InspectorQuickSwitchSpec, 4u> Quick = {{
            { "Enabled",       InspectorSwitchSymbol::PostFx, Entry.VisibilityEnabled && Instance.Enabled, false },
            { "Anti-aliasing", InspectorSwitchSymbol::PostFx, AaOn,   false },
            { "Reflections",   InspectorSwitchSymbol::Stars,  ReflOn, false },
            { "XeGTAO",        InspectorSwitchSymbol::Orbit,  AoOn,   false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, LeftX, CursorY, InnerWidth, Quick, Input, Clicked);
        if (Clicked == 0) { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1 && DisplaySettings) { DisplaySettings->AntiAliasingEnabled = !DisplaySettings->AntiAliasingEnabled; OutMutated = true; }
        else if (Clicked == 2 && DisplaySettings) { DisplaySettings->ReflectionsEnabled = !DisplaySettings->ReflectionsEnabled; OutMutated = true; }
        else if (Clicked == 3 && DisplaySettings) { DisplaySettings->XeGtaoEnabled = !DisplaySettings->XeGtaoEnabled; OutMutated = true; }

        static constexpr std::array<const char*, 2u> Tags = {{ "post-process", "grading" }};
        CursorY = InspectorPrimitives::ConstructTagsSection(Surface, LeftX, CursorY, InnerWidth, Tags);
        CursorY = InspectorPrimitives::ConstructNotesSection(Surface, LeftX, CursorY, InnerWidth, Entry.AuthorNotes);
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 344.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Exposure & tone curve");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        std::array<float, 28u> Curve{};
        const float Gain = std::pow(2.0f, Instance.ExposureEv);
        for (std::size_t I = 0u; I < Curve.size(); ++I)
        {
            const float X = (static_cast<float>(I) / 27.0f) * 3.0f * Gain;
            Curve[I] = std::clamp(std::pow(X / (1.0f + X), Instance.ContrastGain), 0.0f, 1.0f);
        }
        InspectorPrimitives::ConstructCurvePlot(Surface, { LeftX + PadX, Y, BodyW, 74.0f }, Curve, ColorRgba{ 232u, 232u, 238u, 255u }, ColorRgba{ 188u, 198u, 218u, 36u }, "Toe", "Shoulder");
        Y += 86.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7201u, "Exposure", "EV", "%.2f", -4.0f, 4.0f, Instance.ExposureEv, ActiveDragId, Input, OutMutated);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7202u, "Vignette", "", "%.2f", 0.0f, 1.0f, Instance.VignetteStrength, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7203u, "Chromatic shift", "", "%.2f", 0.0f, 1.0f, Instance.ChromaticShift, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    else
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, S, "Colour grading");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7204u, "Contrast", "x", "%.2f", 0.5f, 1.8f, Instance.ContrastGain, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 7205u, "Saturation", "x", "%.2f", 0.0f, 2.0f, Instance.SaturationGain, ActiveDragId, Input, OutMutated);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

} // namespace Frontier
