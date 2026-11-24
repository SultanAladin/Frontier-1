/*============================================================================================================================================
                                                        MOTIONBLURINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Brand-new 1:1 HTML parity port of Motion Blur Inspector (2 tabs: Overview, Settings)

#include "Engine/Editor/Inspectors/MotionBlurInspector.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

float MotionBlurInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, MotionBlurInstanceData& Instance,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float WideX = LeftX;
    const float WideW = InnerWidth;
    static constexpr std::array<const char*, 2u> Pages = {{ "Overview", "Settings" }};
    float CursorY = InspectorPrimitives::ConstructPageStrip(Surface, WideX, TopY, WideW, Pages, ActivePage, Input, OutPageSwitched);
    const float PadX  = std::clamp(WideW * 0.065f, 14.0f, 22.0f);
    const float BodyX = WideX + PadX;
    const float BodyW = std::max(100.0f, WideW - PadX * 2.0f);
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    static constexpr std::array<float, 3u> FpsVals = {{ 24.0f, 30.0f, 60.0f }};
    static constexpr std::array<const char*, 3u> FpsNames = {{ "24 fps", "30 fps", "60 fps" }};
    const float Fps = FpsVals[std::min(FrameRateIdx, 2u)];
    const float Denom = std::max(1.0f, Fps * (360.0f / std::max(1.0f, Instance.ShutterAngleDegrees)));

    if (ActivePage == 0u)
    {
        // TAB 1: OVERVIEW — Shutter study (h = 372), Quick controls (3 switches: Enabled, Object motion, Camera motion)
        const Rectangle2D StudyCard { WideX, CursorY, WideW, 372.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, StudyCard, "Shutter study", "Authored values", InspectorMetrics::DimInk);

        char ExpBuf[24]{};
        std::snprintf(ExpBuf, sizeof(ExpBuf), "1/%.0f", Denom);
        (void)InspectorPrimitives::ConstructMetricReadout(Surface, BodyX, StudyCard.Y + 62.0f, ExpBuf, "s exposure", nullptr, 36.0f);

        // Dark shutter schematic well
        const Rectangle2D Well { BodyX, StudyCard.Y + 114.0f, BodyW, 134.0f };
        Surface.FillRectangle(Well, ColorRgba{ 24u, 24u, 26u, 255u }, 12.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);

        // Left rotary shutter disc (amber open sector)
        const Point2D DiscC { Well.X + 44.0f, Well.Y + 64.0f };
        const float DiscR = 29.0f;
        Surface.FillCircle(DiscC, DiscR, ColorRgba{ 16u, 16u, 18u, 255u });
        Surface.StrokeCircle(DiscC, DiscR, ColorRgba{ 255u, 255u, 255u, 28u }, 1.0f);
        const float OpenRad = std::clamp(Instance.ShutterAngleDegrees, 1.0f, 360.0f) * 0.01745329f;
        const int WedgeSteps = 24;
        for (int I = 0; I < WedgeSteps; ++I)
        {
            const float A0 = -1.570796f + OpenRad * (static_cast<float>(I) / static_cast<float>(WedgeSteps));
            const float A1 = -1.570796f + OpenRad * (static_cast<float>(I + 1) / static_cast<float>(WedgeSteps));
            Surface.StrokeLine(DiscC, { DiscC.X + std::cos((A0 + A1) * 0.5f) * (DiscR - 1.5f), DiscC.Y + std::sin((A0 + A1) * 0.5f) * (DiscR - 1.5f) }, ColorRgba{ 212u, 152u, 74u, 235u }, 3.2f);
        }
        Surface.FillCircle(DiscC, 2.5f, ColorRgba{ 245u, 245u, 245u, 255u });
        char OpenBuf[24]{};
        std::snprintf(OpenBuf, sizeof(OpenBuf), "%.0f\xC2\xB0 open", Instance.ShutterAngleDegrees);
        const float OW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.6f, OpenBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { DiscC.X - OW * 0.5f, Well.Y + 104.0f }, 8.6f, InspectorMetrics::MutedInk, OpenBuf);

        // Right top: FRAMES · 24 FPS timeline bars
        const float RX = Well.X + 94.0f;
        const float RW = Well.Width - 106.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RX, Well.Y + 14.0f }, 8.0f, InspectorMetrics::DimInk, "FRAMES \xC2\xB7 24 FPS");
        for (int F = 0; F < 3; ++F)
        {
            const Rectangle2D Slot { RX + static_cast<float>(F) * (RW / 3.0f), Well.Y + 28.0f, RW / 3.0f - 4.0f, 12.0f };
            Surface.StrokeRectangle(Slot, ColorRgba{ 255u, 255u, 255u, 22u }, 4.0f, 1.0f);
            const float FillW = std::clamp(Instance.ShutterAngleDegrees / 360.0f, 0.05f, 1.0f) * Slot.Width;
            Surface.FillRectangle({ Slot.X, Slot.Y, FillW, Slot.Height }, ColorRgba{ 212u, 152u, 74u, 240u }, 4.0f);
        }
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RX, Well.Y + 46.0f }, 7.8f, InspectorMetrics::DimInk, "shutter open 1/48 s of each 1/24 s frame");

        // Right bottom: Object crossing streak diagram
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RX, Well.Y + 68.0f }, 7.8f, InspectorMetrics::DimInk, "OBJECT CROSSING THE FRAME IN 0.25 s");
        const Rectangle2D StreakBox { RX, Well.Y + 80.0f, RW, 34.0f };
        Surface.FillRectangle(StreakBox, ColorRgba{ 16u, 16u, 18u, 255u }, 6.0f);
        Surface.StrokeRectangle(StreakBox, ColorRgba{ 255u, 255u, 255u, 16u }, 6.0f, 1.0f);
        const float StreakY = StreakBox.Center().Y;
        const float StreakStart = StreakBox.X + StreakBox.Width * 0.38f;
        const float StreakEnd   = StreakBox.X + StreakBox.Width * 0.68f;
        Surface.FillRectangle({ StreakStart, StreakY - 5.0f, StreakEnd - StreakStart, 10.0f }, ColorRgba{ 212u, 152u, 74u, 120u }, 5.0f);
        Surface.FillCircle({ StreakEnd, StreakY }, 5.5f, ColorRgba{ 246u, 246u, 246u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { RX - 8.0f, Well.Y + 118.0f }, 7.8f, InspectorMetrics::DimInk, "12 samples along the streak \xC2\xB7 capped by Max blur");

        // 3 KPI tiles: EXPOSURE, STREAK CAP, SAMPLES
        const float KpiY = Well.Bottom() + 12.0f;
        const float KpiGap = 8.0f;
        const float KpiW = std::floor((BodyW - KpiGap * 2.0f) / 3.0f);
        char ExpS[28]{}, CapBuf[16]{}, SmpBuf[16]{};
        std::snprintf(ExpS,   sizeof(ExpS),   "1/%.0f s", Denom);
        std::snprintf(CapBuf, sizeof(CapBuf), "%.1f%%", MaxBlurPercent);
        std::snprintf(SmpBuf, sizeof(SmpBuf), "%.0f", SampleCount);
        const std::array<std::pair<const char*, const char*>, 3u> Kpis = {{
            { "EXPOSURE", ExpS }, { "STREAK \xC2\xB7 CAP", CapBuf }, { "SAMPLES", SmpBuf }
        }};
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D Tile { BodyX + static_cast<float>(I) * (KpiW + KpiGap), KpiY, KpiW, 48.0f };
            Surface.FillRectangle(Tile, ColorRgba{ 26u, 26u, 26u, 255u }, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.X + 8.0f, Tile.Y + 8.0f }, 8.2f, InspectorMetrics::DimInk, Kpis[I].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Tile.X + 8.0f, Tile.Y + 24.0f }, 13.0f, InspectorMetrics::HeadingInk, Kpis[I].second);
        }

        (void)InspectorPrimitives::ConstructWrappedParagraph(
            Surface, BodyX, KpiY + 58.0f, BodyW, 10.2f, 14.5f, InspectorMetrics::DimInk,
            "180\xC2\xB0 shutter at 24 fps \xC2\xB7 drag to change the shutter angle. Diagram of the authored values; not the native renderer.");
        CursorY = StudyCard.Bottom() + InspectorMetrics::SectionGap;

        // Quick controls (3 switches: Enabled, Object motion, Camera motion)
        const std::array<InspectorQuickSwitchSpec, 3u> Quick = {{
            { "Enabled",       InspectorSwitchSymbol::Eye,        Entry.VisibilityEnabled, false },
            { "Object motion", InspectorSwitchSymbol::Physics,    ObjectMotion,            false },
            { "Camera motion", InspectorSwitchSymbol::CameraLens, CameraMotion,            false },
        }};
        int32_t Clicked = -1;
        CursorY = InspectorPrimitives::ConstructQuickSwitchSection(Surface, WideX, CursorY, WideW, Quick, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; Instance.Enabled = Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { ObjectMotion = !ObjectMotion; OutMutated = true; }
        else if (Clicked == 2) { CameraMotion = !CameraMotion; OutMutated = true; }
    }
    else
    {
        // TAB 2: SETTINGS — Card 1 Shutter (h=237), Card 2 Quality (h=235), Card 3 Preview (h=163)
        const Rectangle2D ShutCard { WideX, CursorY, WideW, 237.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, ShutCard, "Shutter");
        const float AfterAng = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, ShutCard.Y + 68.0f, BodyW, 8201u, "Shutter angle", "\xC2\xB0", "%.0f", 1.0f, 360.0f, Instance.ShutterAngleDegrees, ActiveDragId, Input, OutMutated);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { BodyX, AfterAng }, 11.4f, InspectorMetrics::MutedInk, "Reference rate");
        const Rectangle2D Drop { BodyX, AfterAng + 18.0f, BodyW, 34.0f };
        Surface.FillRectangle(Drop, ColorRgba{ 12u, 12u, 12u, 255u }, InspectorMetrics::ControlRadius);
        Surface.StrokeRectangle(Drop, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::ControlRadius, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Drop.X + 16.0f, Drop.Y + 10.5f }, 12.2f, InspectorMetrics::HeadingInk, FpsNames[std::min(FrameRateIdx, 2u)]);
        const Point2D Ch { Drop.Right() - 16.0f, Drop.Center().Y };
        Surface.StrokeLine({ Ch.X - 4.0f, Ch.Y - 2.0f }, { Ch.X, Ch.Y + 2.5f }, InspectorMetrics::HeadingInk, 1.5f);
        Surface.StrokeLine({ Ch.X, Ch.Y + 2.5f }, { Ch.X + 4.0f, Ch.Y - 2.0f }, InspectorMetrics::HeadingInk, 1.5f);
        if (Drop.Contains(Cursor) && Input.PrimaryPointer.Pressed) { FrameRateIdx = (FrameRateIdx + 1u) % 3u; OutMutated = true; }
        CursorY = ShutCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D QualCard { WideX, CursorY, WideW, 235.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, QualCard, "Quality");
        float QY = QualCard.Y + 68.0f;
        QY = InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, QY, BodyW, 8202u, "Max blur", "% width", "%.0f", 0.0f, 20.0f, MaxBlurPercent, ActiveDragId, Input, OutMutated);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, QY, BodyW, 8203u, "Samples", "", "%.0f", 4.0f, 32.0f, SampleCount, ActiveDragId, Input, OutMutated);
        Instance.MaxSampleRadiusPx = MaxBlurPercent * 4.0f;
        CursorY = QualCard.Bottom() + InspectorMetrics::SectionGap;

        const Rectangle2D PrevCard { WideX, CursorY, WideW, 163.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, PrevCard, "Preview");
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, BodyX, PrevCard.Y + 68.0f, BodyW, 8204u, "Preview speed", "", "%.2g", 0.1f, 4.0f, PreviewSpeed, ActiveDragId, Input, OutMutated);
        CursorY = PrevCard.Bottom() + InspectorMetrics::SectionGap;
    }

    return CursorY;
}

} // namespace Frontier
