/*============================================================================================================================================
                                                       SIMULATIONTIMELINEFOOTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable SimulationTimelineFooter (transport controls, clock, scrubbable kinetic curve, speed strip & loop toggle)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SimulationTimelineFooter.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Frontier
{

namespace
{

ColorRgba ScaleAlpha(ColorRgba Base, float Opacity) noexcept
{
    return Base.ScaledAlpha(Opacity);
}

void FormatEnergyJoules(float Joules, char* OutBuf, std::size_t BufSize) noexcept
{
    if (std::fabs(Joules) >= 1000.0f)
    {
        std::snprintf(OutBuf, BufSize, "%.2f kJ", Joules / 1000.0f);
    }
    else if (Joules < 10.0f)
    {
        std::snprintf(OutBuf, BufSize, "%.2f J", Joules);
    }
    else
    {
        std::snprintf(OutBuf, BufSize, "%.0f J", Joules);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC INTERFACE
//------------------------------------------------------------------------------------------------------------------------

SimulationTimelineFooterResult SimulationTimelineFooter::Construct(
    PixelSpace&                            Surface,
    Rectangle2D                            Bounds,
    const SimulationTimelineFooterReading& Reading,
    SimulationTimelineFooterSession&       Session,
    const ControlPointer&                  Pointer,
    float                                  Opacity) noexcept
{
    SimulationTimelineFooterResult Result {};
    if (Bounds.Width < 260.0f || Bounds.Height < 36.0f)
    {
        return Result;
    }

    const Point2D Cursor{ Pointer.X, Pointer.Y };

    Surface.FillRectangle(Bounds, ScaleAlpha(ColorRgba{ 19, 19, 21, 248 }, Opacity), 12.0f);
    Surface.StrokeRectangle(Bounds, ScaleAlpha(ColorRgba{ 255, 255, 255, 20 }, Opacity), 12.0f, 1.0f);

    const float CenterY = Bounds.Center().Y;
    float       LeftX   = Bounds.X + 10.0f;

    // 1. Transport cluster: To-Start (26x26), Play/Pause (32x32), Step (26x26)
    const Rectangle2D StartBtn{ LeftX, CenterY - 13.0f, 26.0f, 26.0f };
    const bool        StartHover = StartBtn.Contains(Cursor);
    Surface.FillCircle(StartBtn.Center(), 13.0f, ScaleAlpha(StartHover ? ColorRgba{ 48, 48, 52, 255 } : ColorRgba{ 32, 32, 35, 255 }, Opacity));
    Surface.StrokeCircle(StartBtn.Center(), 13.0f, ScaleAlpha(ColorRgba{ 255, 255, 255, 24 }, Opacity), 1.0f);
    {
        const ColorRgba Ink = ScaleAlpha(ColorRgba{ 218, 222, 226, 255 }, Opacity);
        Surface.StrokeLine({ StartBtn.X + 8.5f, StartBtn.Y + 8.0f }, { StartBtn.X + 8.5f, StartBtn.Bottom() - 8.0f }, Ink, 1.6f);
        const std::array<Point2D, 3> Tri = {{
            { StartBtn.X + 10.5f, StartBtn.Center().Y },
            { StartBtn.X + 17.5f, StartBtn.Y + 8.0f },
            { StartBtn.X + 17.5f, StartBtn.Bottom() - 8.0f },
        }};
        Surface.FillPolygon(Tri, Ink);
    }
    if (StartHover && Pointer.Pressed)
    {
        Result.ToStartPressed = true;
    }

    const Rectangle2D PlayBtn{ StartBtn.Right() + 6.0f, CenterY - 16.0f, 32.0f, 32.0f };
    const bool        PlayHover = PlayBtn.Contains(Cursor);
    const ColorRgba   PlayBg    = Reading.Playing
                                    ? ColorRgba{ 185, 130, 48, 255 }
                                    : (PlayHover ? ColorRgba{ 62, 62, 66, 255 } : ColorRgba{ 44, 44, 48, 255 });
    Surface.FillCircle(PlayBtn.Center(), 16.0f, ScaleAlpha(PlayBg, Opacity));
    Surface.StrokeCircle(PlayBtn.Center(), 16.0f, ScaleAlpha(ColorRgba{ 255, 255, 255, 34 }, Opacity), 1.0f);
    {
        const ColorRgba Ink = ScaleAlpha(Reading.Playing ? ColorRgba{ 16, 16, 18, 255 } : ColorRgba{ 240, 240, 242, 255 }, Opacity);
        if (Reading.Playing)
        {
            Surface.FillRectangle({ PlayBtn.Center().X - 4.5f, PlayBtn.Center().Y - 5.5f, 3.2f, 11.0f }, Ink, 1.0f);
            Surface.FillRectangle({ PlayBtn.Center().X + 1.3f, PlayBtn.Center().Y - 5.5f, 3.2f, 11.0f }, Ink, 1.0f);
        }
        else
        {
            const std::array<Point2D, 3> Tri = {{
                { PlayBtn.Center().X - 3.5f, PlayBtn.Center().Y - 6.0f },
                { PlayBtn.Center().X - 3.5f, PlayBtn.Center().Y + 6.0f },
                { PlayBtn.Center().X + 6.5f, PlayBtn.Center().Y },
            }};
            Surface.FillPolygon(Tri, Ink);
        }
    }
    if (PlayHover && Pointer.Pressed)
    {
        Result.TogglePlayPressed = true;
    }

    const Rectangle2D StepBtn{ PlayBtn.Right() + 6.0f, CenterY - 13.0f, 26.0f, 26.0f };
    const bool        StepHover = StepBtn.Contains(Cursor);
    Surface.FillCircle(StepBtn.Center(), 13.0f, ScaleAlpha(StepHover ? ColorRgba{ 48, 48, 52, 255 } : ColorRgba{ 32, 32, 35, 255 }, Opacity));
    Surface.StrokeCircle(StepBtn.Center(), 13.0f, ScaleAlpha(ColorRgba{ 255, 255, 255, 24 }, Opacity), 1.0f);
    {
        const ColorRgba Ink = ScaleAlpha(ColorRgba{ 218, 222, 226, 255 }, Opacity);
        const std::array<Point2D, 3> Tri = {{
            { StepBtn.X + 8.5f, StepBtn.Y + 8.0f },
            { StepBtn.X + 8.5f, StepBtn.Bottom() - 8.0f },
            { StepBtn.X + 15.5f, StepBtn.Center().Y },
        }};
        Surface.FillPolygon(Tri, Ink);
        Surface.StrokeLine({ StepBtn.X + 17.5f, StepBtn.Y + 8.0f }, { StepBtn.X + 17.5f, StepBtn.Bottom() - 8.0f }, Ink, 1.6f);
    }
    if (StepHover && Pointer.Pressed)
    {
        Result.SingleStepPressed = true;
    }

    LeftX = StepBtn.Right() + 12.0f;

    // 2. Clock readout + Phase caption
    const double ClampedTime     = std::max(0.0, std::min(Reading.ElapsedSeconds, std::max(0.01, Reading.DurationSeconds)));
    const double ClampedDuration = std::max(0.01, Reading.DurationSeconds);
    char         NowBuf[24]{};
    char         TotalBuf[24]{};
    std::snprintf(NowBuf, sizeof(NowBuf), "%.2f", ClampedTime);
    std::snprintf(TotalBuf, sizeof(TotalBuf), " / %.2f s", ClampedDuration);

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { LeftX, Bounds.Y + 11.0f },
        14.0f,
        ScaleAlpha(ColorRgba{ 238, 240, 242, 255 }, Opacity),
        NowBuf);
    const float NowW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 14.0f, NowBuf);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + NowW, Bounds.Y + 13.0f },
        10.5f,
        ScaleAlpha(ColorRgba{ 146, 150, 156, 255 }, Opacity),
        TotalBuf);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { LeftX, Bounds.Y + 32.0f },
        8.5f,
        ScaleAlpha(ColorRgba{ 158, 162, 168, 255 }, Opacity),
        Reading.PhaseCaption != nullptr ? Reading.PhaseCaption : "EDIT");

    const float ClockBlockRight = LeftX + 104.0f;

    // 3. Right cluster: Loop toggle (26x26), Speed strip (122x26), Kinetic energy readout (88px)
    const Rectangle2D LoopBtn{ Bounds.Right() - 36.0f, CenterY - 13.0f, 26.0f, 26.0f };
    const bool        LoopHover = LoopBtn.Contains(Cursor);
    Surface.FillCircle(
        LoopBtn.Center(),
        13.0f,
        ScaleAlpha(
            Reading.LoopEnabled ? ColorRgba{ 46, 56, 44, 255 }
                                : (LoopHover ? ColorRgba{ 48, 48, 52, 255 } : ColorRgba{ 32, 32, 35, 255 }),
            Opacity));
    Surface.StrokeCircle(
        LoopBtn.Center(),
        13.0f,
        ScaleAlpha(Reading.LoopEnabled ? ColorRgba{ 142, 198, 152, 140 } : ColorRgba{ 255, 255, 255, 22 }, Opacity),
        1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { LoopBtn.Center().X - 3.5f, LoopBtn.Center().Y - 5.5f },
        10.0f,
        ScaleAlpha(Reading.LoopEnabled ? ColorRgba{ 154, 214, 166, 255 } : ColorRgba{ 150, 154, 160, 255 }, Opacity),
        "L");
    if (LoopHover && Pointer.Pressed)
    {
        Result.LoopToggled = true;
    }

    constexpr float                       SpeedStripW = 124.0f;
    const Rectangle2D                     SpeedStrip{ LoopBtn.X - 8.0f - SpeedStripW, CenterY - 13.0f, SpeedStripW, 26.0f };
    const std::array<float, 4>            SpeedValues = {{ 0.25f, 0.5f, 1.0f, 2.0f }};
    const std::array<const char*, 4>      SpeedLabels = {{ "0.25x", "0.5x", "1x", "2x" }};
    Surface.FillRectangle(SpeedStrip, ScaleAlpha(ColorRgba{ 12, 12, 14, 255 }, Opacity), 13.0f);
    Surface.StrokeRectangle(SpeedStrip, ScaleAlpha(ColorRgba{ 255, 255, 255, 18 }, Opacity), 13.0f, 1.0f);
    const float SegW = (SpeedStrip.Width - 6.0f) / 4.0f;
    for (std::size_t Idx = 0u; Idx < SpeedValues.size(); ++Idx)
    {
        const Rectangle2D SegRect{ SpeedStrip.X + 3.0f + static_cast<float>(Idx) * SegW, SpeedStrip.Y + 3.0f, SegW, 20.0f };
        const bool        Active = std::fabs(Reading.TimeScale - SpeedValues[Idx]) < 0.05f;
        const bool        Hov    = SegRect.Contains(Cursor);
        if (Active)
        {
            Surface.FillRectangle(SegRect, ScaleAlpha(ColorRgba{ 54, 54, 58, 255 }, Opacity), 10.0f);
        }
        else if (Hov)
        {
            Surface.FillRectangle(SegRect, ScaleAlpha(ColorRgba{ 36, 36, 40, 255 }, Opacity), 10.0f);
        }
        const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, SpeedLabels[Idx]);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { SegRect.Center().X - LW * 0.5f, SegRect.Y + 5.0f },
            8.5f,
            ScaleAlpha(Active ? ColorRgba{ 238, 240, 242, 255 } : ColorRgba{ 156, 160, 166, 255 }, Opacity),
            SpeedLabels[Idx]);
        if (Hov && Pointer.Pressed)
        {
            Result.TimeScaleChanged = true;
            Result.NewTimeScale     = SpeedValues[Idx];
        }
    }

    const Rectangle2D EnergyBox{ SpeedStrip.X - 90.0f, Bounds.Y + 7.0f, 82.0f, Bounds.Height - 14.0f };
    char              EnergyStr[32]{};
    char              MovingStr[32]{};
    FormatEnergyJoules(Reading.CurrentKineticJoules, EnergyStr, sizeof(EnergyStr));
    std::snprintf(MovingStr, sizeof(MovingStr), "%u moving", Reading.MovingBodyCount);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { EnergyBox.X, EnergyBox.Y + 2.0f },
        8.5f,
        ScaleAlpha(ColorRgba{ 138, 142, 148, 255 }, Opacity),
        "Kinetic");
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { EnergyBox.X, EnergyBox.Y + 13.0f },
        11.5f,
        ScaleAlpha(ColorRgba{ 242, 168, 92, 255 }, Opacity),
        EnergyStr);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { EnergyBox.X, EnergyBox.Y + 28.0f },
        8.5f,
        ScaleAlpha(ColorRgba{ 138, 142, 148, 255 }, Opacity),
        MovingStr);

    // 4. Center scrubbable timeline track
    const float TrackLeft  = ClockBlockRight + 10.0f;
    const float TrackRight = EnergyBox.X - 12.0f;
    if (TrackRight > TrackLeft + 48.0f)
    {
        const Rectangle2D TrackRect{ TrackLeft, Bounds.Y + 9.0f, TrackRight - TrackLeft, Bounds.Height - 18.0f };
        Session.LastTrackBounds = TrackRect;

        Surface.FillRectangle(TrackRect, ScaleAlpha(ColorRgba{ 12, 12, 14, 255 }, Opacity), 8.0f);
        Surface.StrokeRectangle(TrackRect, ScaleAlpha(ColorRgba{ 255, 255, 255, 20 }, Opacity), 8.0f, 1.0f);

        Surface.PushClip(TrackRect);

        if (!Reading.HasBakedTimeline || Reading.EnergyCurveSamples.empty())
        {
            const char* Msg = Reading.EmptyPromptText != nullptr
                                  ? Reading.EmptyPromptText
                                  : "Timeline bakes when you simulate";
            const float MsgW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Msg);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TrackRect.Center().X - MsgW * 0.5f, TrackRect.Center().Y - 5.0f },
                9.5f,
                ScaleAlpha(ColorRgba{ 124, 128, 134, 255 }, Opacity),
                Msg);
        }
        else
        {
            float PeakJ = 1.0e-3f;
            for (const float E : Reading.EnergyCurveSamples)
            {
                PeakJ = std::max(PeakJ, E);
            }

            const std::size_t SampleCount = Reading.EnergyCurveSamples.size();
            std::vector<Point2D> CurvePts(SampleCount);
            for (std::size_t Idx = 0u; Idx < SampleCount; ++Idx)
            {
                const float U = SampleCount > 1u
                                    ? static_cast<float>(Idx) / static_cast<float>(SampleCount - 1u)
                                    : 0.0f;
                const float V = std::clamp(Reading.EnergyCurveSamples[Idx] / PeakJ, 0.0f, 1.0f);
                const float X = TrackRect.X + 4.0f + U * (TrackRect.Width - 8.0f);
                const float Y = TrackRect.Bottom() - 4.0f - V * (TrackRect.Height - 10.0f);
                CurvePts[Idx] = { X, Y };
            }

            // Subtle area fill bars under curve
            for (std::size_t Idx = 1u; Idx < SampleCount; ++Idx)
            {
                const float X0 = CurvePts[Idx - 1u].X;
                const float X1 = CurvePts[Idx].X;
                const float TopY = 0.5f * (CurvePts[Idx - 1u].Y + CurvePts[Idx].Y);
                Surface.FillRectangle(
                    { X0, TopY, std::max(1.0f, X1 - X0 + 0.5f), TrackRect.Bottom() - 4.0f - TopY },
                    ScaleAlpha(ColorRgba{ 255, 138, 62, 56 }, Opacity),
                    0.0f);
            }
            Surface.StrokePolyline(CurvePts, ScaleAlpha(ColorRgba{ 246, 246, 248, 220 }, Opacity), 1.5f, false);
        }

        // Event markers
        for (const auto& Marker : Reading.EventMarkers)
        {
            const float U = std::clamp(Marker.NormalizedFraction, 0.0f, 1.0f);
            const float MX = TrackRect.X + 4.0f + U * (TrackRect.Width - 8.0f);
            Surface.StrokeLine({ MX, TrackRect.Y + 3.0f }, { MX, TrackRect.Bottom() - 3.0f }, ScaleAlpha(Marker.Tint, Opacity), 1.3f);
            if (Marker.Caption != nullptr && Marker.Caption[0] != '\0')
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Medium,
                    { MX + 3.0f, TrackRect.Y + 3.0f },
                    8.0f,
                    ScaleAlpha(Marker.Tint, Opacity),
                    Marker.Caption);
            }
        }

        // Playhead scrubbing & needle
        if (Pointer.Pressed && TrackRect.Contains(Cursor))
        {
            Session.Scrubbing = true;
        }
        if (!Pointer.Down)
        {
            Session.Scrubbing = false;
        }

        if (Session.Scrubbing && Pointer.Down)
        {
            const float U = std::clamp((Cursor.X - (TrackRect.X + 4.0f)) / std::max(1.0f, TrackRect.Width - 8.0f), 0.0f, 1.0f);
            Result.Scrubbed            = true;
            Result.ScrubbedTimeSeconds = static_cast<double>(U) * ClampedDuration;
        }

        const float PlayU = static_cast<float>(std::clamp(ClampedTime / ClampedDuration, 0.0, 1.0));
        const float HeadX = TrackRect.X + 4.0f + PlayU * (TrackRect.Width - 8.0f);
        Surface.StrokeLine({ HeadX, TrackRect.Y + 1.0f }, { HeadX, TrackRect.Bottom() - 1.0f }, ScaleAlpha(ColorRgba{ 255, 166, 82, 255 }, Opacity), 1.8f);
        Surface.FillCircle({ HeadX, TrackRect.Y + 4.0f }, 3.5f, ScaleAlpha(ColorRgba{ 255, 166, 82, 255 }, Opacity));

        Surface.PopClip();
    }

    return Result;
}

} // namespace Frontier
