/*============================================================================================================================================
                                                           SIMULATIONTIMERULER.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable SimulationTimeRuler (graduated ticks, shaded active track strip & scrubbable playhead needle)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SimulationTimeRuler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

SimulationTimeRulerResult SimulationTimeRuler::Construct(
    PixelSpace&                       Surface,
    Rectangle2D                       Bounds,
    const SimulationTimeRulerReading& Reading,
    SimulationTimeRulerSession&       Session,
    const ControlPointer&             Pointer,
    float                             Opacity) noexcept
{
    SimulationTimeRulerResult Result {};
    Result.ScrubbedTimeSec = Reading.ElapsedSeconds;
    if (Bounds.Width < 180.0f || Bounds.Height < 28.0f || Opacity <= 0.001f)
    {
        return Result;
    }

    const ColorRgba TextSoft   { 168u, 168u, 168u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba TextMuted  { 118u, 118u, 118u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba Hairline   { 255u, 255u, 255u, static_cast<uint8_t>(20.0f * Opacity) };
    const ColorRgba TrackBg    { 18u,  18u,  18u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba ActiveSpan { 232u, 194u, 122u, static_cast<uint8_t>(42.0f * Opacity) };
    const ColorRgba ActiveEdge { 232u, 194u, 122u, static_cast<uint8_t>(118.0f * Opacity) };
    const ColorRgba Playhead   { 239u, 239u, 239u, static_cast<uint8_t>(245.0f * Opacity) };

    // Top separator hairline
    Surface.StrokeLine({ Bounds.X + 10.0f, Bounds.Y }, { Bounds.Right() - 10.0f, Bounds.Y }, Hairline, 1.0f);

    // Left stacked label block (SIMULATION / STEP 0004)
    const float LabelWidth = 98.0f;
    Surface.Text({ Bounds.X + 14.0f, Bounds.Y + 8.0f }, 8.8f, TextMuted, Reading.HeaderLabel != nullptr ? Reading.HeaderLabel : "SIMULATION");
    char StepText[32];
    std::snprintf(StepText, sizeof(StepText), "%s %04u", Reading.StepPrefix != nullptr ? Reading.StepPrefix : "STEP", Reading.StepCounter);
    Surface.Text({ Bounds.X + 14.0f, Bounds.Y + 21.0f }, 10.5f, TextSoft, StepText);

    // Right ruler area
    const Rectangle2D RulerArea { Bounds.X + LabelWidth + 8.0f, Bounds.Y + 4.0f, Bounds.Width - LabelWidth - 20.0f, Bounds.Height - 8.0f };
    const Rectangle2D TrackRect { RulerArea.X, RulerArea.Y + 16.0f, RulerArea.Width, 16.0f };
    Session.LastTrackBounds = TrackRect;

    const double LoopDuration = std::max(1.0, Reading.LoopDurationSec);
    constexpr int TickCount   = 6; // 0s, 5s, 10s, 15s, 20s, 25s, 30s
    for (int I = 0; I <= TickCount; ++I)
    {
        const float Frac = static_cast<float>(I) / static_cast<float>(TickCount);
        const float XPos = RulerArea.X + RulerArea.Width * Frac;
        const int   Sec  = static_cast<int>(std::round(LoopDuration * static_cast<double>(Frac)));
        char TickBuf[12];
        std::snprintf(TickBuf, sizeof(TickBuf), "%ds", Sec);
        const float TextW = Surface.MeasureTextWidth(9.0f, TickBuf);
        const float DrawX = (I == 0) ? XPos : ((I == TickCount) ? (XPos - TextW) : (XPos - TextW * 0.5f));
        Surface.Text({ DrawX, RulerArea.Y + 2.0f }, 9.0f, TextMuted, TickBuf);
    }

    // Track base strip
    Surface.FillRectangle(TrackRect, TrackBg, 4.0f);
    Surface.StrokeRectangle(TrackRect, Hairline, 4.0f, 1.0f);

    // Shaded active emission & force field sub-span
    const float ActiveW = std::clamp(Reading.ActiveTrackEndFrac, 0.08f, 1.0f) * TrackRect.Width;
    const Rectangle2D ActiveRect { TrackRect.X, TrackRect.Y, ActiveW, TrackRect.Height };
    Surface.FillRectangle(ActiveRect, ActiveSpan, 4.0f);
    Surface.StrokeRectangle(ActiveRect, ActiveEdge, 4.0f, 0.9f);
    Surface.PushClip(ActiveRect);
    Surface.Text({ ActiveRect.X + 7.0f, ActiveRect.Y + 3.0f }, 9.0f, TextSoft,
        Reading.ActiveTrackCaption != nullptr ? Reading.ActiveTrackCaption : "Active release & force field");
    Surface.PopClip();

    // Pointer scrubbing interaction
    const bool TrackHover = ControlAppearance::QueryPointerOver(RulerArea, Pointer);
    if (TrackHover && Pointer.Pressed)
    {
        Session.ScrubbingActive = true;
    }
    if (!Pointer.Down)
    {
        Session.ScrubbingActive = false;
    }
    if (Session.ScrubbingActive && TrackRect.Width > 1.0f)
    {
        const float Frac       = std::clamp((Pointer.X - TrackRect.X) / TrackRect.Width, 0.0f, 1.0f);
        Result.Scrubbed        = true;
        Result.ScrubbedTimeSec = LoopDuration * static_cast<double>(Frac);
    }

    // Playhead needle
    const double ActiveSec   = Result.Scrubbed ? Result.ScrubbedTimeSec : Reading.ElapsedSeconds;
    const float  WrappedFrac = static_cast<float>(std::fmod(std::max(0.0, ActiveSec), LoopDuration) / LoopDuration);
    const float  HeadX       = TrackRect.X + TrackRect.Width * std::clamp(WrappedFrac, 0.0f, 1.0f);
    Surface.StrokeLine({ HeadX, RulerArea.Y + 12.0f }, { HeadX, TrackRect.Bottom() + 2.0f }, Playhead, 1.6f);
    Surface.FillCircle({ HeadX, RulerArea.Y + 13.0f }, 2.6f, Playhead);

    return Result;
}

} // namespace Frontier
