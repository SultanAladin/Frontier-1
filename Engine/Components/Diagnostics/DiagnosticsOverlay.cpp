/*============================================================================================================================================
                                                          DIAGNOSTICSOVERLAY.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable floating DiagnosticsOverlay (metric tabs, view modes, sparkline plot, stats rows & move/resize)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"

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

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC INTERFACE
//------------------------------------------------------------------------------------------------------------------------

DiagnosticsOverlayResult DiagnosticsOverlay::Construct(
    PixelSpace&                   Surface,
    Rectangle2D                   HostBounds,
    const DiagnosticsOverlaySpec& Spec,
    DiagnosticsOverlaySession&    Session,
    const ControlPointer&         Pointer,
    float                         Opacity) noexcept
{
    DiagnosticsOverlayResult Result {};
    if (!Session.Visible || HostBounds.Width < 180.0f || HostBounds.Height < 180.0f)
    {
        return Result;
    }

    const Point2D Cursor{ Pointer.X, Pointer.Y };

    // 1. Resolve panel bounds (default top-right or custom moved/resized bounds clamped to host)
    const float MinW = std::min(240.0f, HostBounds.Width - 20.0f);
    const float MinH = std::min(260.0f, HostBounds.Height - 20.0f);
    const float MaxW = std::max(MinW, HostBounds.Width - 24.0f);
    const float MaxH = std::max(MinH, HostBounds.Height - 24.0f);

    if (!Session.CustomLayoutPlaced || Session.PanelBounds.Width <= 0.0f || Session.PanelBounds.Height <= 0.0f)
    {
        const float W = std::clamp(Spec.DefaultWidthPx, MinW, MaxW);
        const float H = std::clamp(Spec.DefaultHeightPx, MinH, MaxH);
        Session.PanelBounds = Rectangle2D{
            HostBounds.Right() - W - 14.0f,
            HostBounds.Y + 14.0f,
            W,
            H
        };
    }

    if (Spec.AllowDragResize)
    {
        if (Session.DraggingMove && Pointer.Down)
        {
            const float DX = Cursor.X - Session.DragStartCursor.X;
            const float DY = Cursor.Y - Session.DragStartCursor.Y;
            Session.PanelBounds.X = Session.DragStartBounds.X + DX;
            Session.PanelBounds.Y = Session.DragStartBounds.Y + DY;
            Session.CustomLayoutPlaced = true;
            Result.Interacted          = true;
        }
        else if (Session.DraggingResize && Pointer.Down)
        {
            const float DX = Cursor.X - Session.DragStartCursor.X;
            const float DY = Cursor.Y - Session.DragStartCursor.Y;
            Session.PanelBounds.Width  = std::clamp(Session.DragStartBounds.Width + DX, MinW, MaxW);
            Session.PanelBounds.Height = std::clamp(Session.DragStartBounds.Height + DY, MinH, MaxH);
            Session.CustomLayoutPlaced = true;
            Result.Interacted          = true;
        }
        if (!Pointer.Down)
        {
            Session.DraggingMove   = false;
            Session.DraggingResize = false;
        }
    }

    Session.PanelBounds.Width  = std::clamp(Session.PanelBounds.Width, MinW, MaxW);
    Session.PanelBounds.Height = std::clamp(Session.PanelBounds.Height, MinH, MaxH);
    Session.PanelBounds.X      = std::clamp(Session.PanelBounds.X, HostBounds.X + 8.0f, HostBounds.Right() - Session.PanelBounds.Width - 8.0f);
    Session.PanelBounds.Y      = std::clamp(Session.PanelBounds.Y, HostBounds.Y + 8.0f, HostBounds.Bottom() - Session.PanelBounds.Height - 8.0f);

    const Rectangle2D Panel = Session.PanelBounds;
    if (Panel.Contains(Cursor) && (Pointer.Pressed || Pointer.Down))
    {
        Result.Interacted = true;
    }

    Surface.FillRectangle(Panel, ScaleAlpha(ColorRgba{ 32, 32, 32, 246 }, Opacity), 24.0f);
    Surface.StrokeRectangle(Panel, ScaleAlpha(ColorRgba{ 255, 255, 255, 28 }, Opacity), 24.0f, 1.0f);

    Surface.PushClip(Panel);

    // 2. Header: Close button, Icon, Title, Reset Layout (when movable), Shortcut Badge
    const Rectangle2D CloseBtn{ Panel.X + 14.0f, Panel.Y + 13.0f, 28.0f, 28.0f };
    const bool        CloseHover = CloseBtn.Contains(Cursor);
    Surface.FillCircle(CloseBtn.Center(), 14.0f, ScaleAlpha(CloseHover ? ColorRgba{ 54, 58, 49, 255 } : ColorRgba{ 43, 45, 40, 255 }, Opacity));
    VectorIconRepository::ConstructControl(
        Surface,
        ControlIcon::CloseCross,
        VectorIconPlacement{ CloseBtn.X + 7.5f, CloseBtn.Y + 7.5f, 13.0f, ScaleAlpha(ColorRgba{ 192, 197, 184, 255 }, Opacity), 1.85f });
    if (CloseHover && Pointer.Pressed)
    {
        Session.Visible     = false;
        Result.ClosePressed = true;
    }

    WorkspaceAppearance::ConstructDiagnosticsIcon(
        Surface,
        { CloseBtn.Right() + 10.0f, Panel.Y + 19.0f },
        15.0f,
        ScaleAlpha(ColorRgba{ 166, 185, 151, 255 }, Opacity));
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { CloseBtn.Right() + 31.0f, Panel.Y + 19.5f },
        12.0f,
        ScaleAlpha(ColorRgba{ 214, 216, 209, 255 }, Opacity),
        Spec.Title != nullptr ? Spec.Title : "Diagnostics");

    float RightCursor = Panel.Right() - 16.0f;
    if (Spec.ShortcutBadge != nullptr && Spec.ShortcutBadge[0] != '\0')
    {
        const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.0f, Spec.ShortcutBadge);
        RightCursor -= (BW + 6.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { RightCursor, Panel.Y + 22.0f },
            8.0f,
            ScaleAlpha(ColorRgba{ 129, 142, 119, 255 }, Opacity),
            Spec.ShortcutBadge);
        RightCursor -= 6.0f;
    }

    Rectangle2D ResetBtn{};
    if (Spec.AllowDragResize)
    {
        ResetBtn = Rectangle2D{ RightCursor - 24.0f, Panel.Y + 15.0f, 22.0f, 22.0f };
        const bool ResetHover = ResetBtn.Contains(Cursor);
        Surface.FillCircle(ResetBtn.Center(), 11.0f, ScaleAlpha(ResetHover ? ColorRgba{ 54, 58, 49, 255 } : ColorRgba{ 43, 45, 40, 255 }, Opacity));
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ResetBtn.X + 6.5f, ResetBtn.Y + 5.0f },
            9.0f,
            ScaleAlpha(ColorRgba{ 182, 188, 174, 255 }, Opacity),
            "R");
        if (ResetHover && Pointer.Pressed)
        {
            Session.CustomLayoutPlaced = false;
            Result.ResetLayoutPressed  = true;
        }
    }

    const Rectangle2D DragBar{ CloseBtn.Right() + 4.0f, Panel.Y + 8.0f, std::max(20.0f, (Spec.AllowDragResize ? ResetBtn.X : RightCursor) - CloseBtn.Right() - 8.0f), 34.0f };
    if (Spec.AllowDragResize && Pointer.Pressed && DragBar.Contains(Cursor))
    {
        Session.DraggingMove    = true;
        Session.DragStartCursor = Cursor;
        Session.DragStartBounds = Panel;
    }

    // 3. Channel tabs
    float BodyY = Panel.Y + 50.0f;
    if (!Spec.Channels.empty())
    {
        Session.ActiveChannelIndex = std::min(Session.ActiveChannelIndex, static_cast<uint32_t>(Spec.Channels.size() - 1u));
        const std::size_t Count    = Spec.Channels.size();
        const float       Gap      = 5.0f;
        const float       AvailW   = Panel.Width - 42.0f;
        const float       TabW     = (AvailW - Gap * static_cast<float>(Count - 1u)) / static_cast<float>(Count);

        for (std::size_t Idx = 0u; Idx < Count; ++Idx)
        {
            const Rectangle2D TabRect{ Panel.X + 21.0f + static_cast<float>(Idx) * (TabW + Gap), BodyY, TabW, 25.0f };
            const bool        Selected = (Session.ActiveChannelIndex == static_cast<uint32_t>(Idx));
            const bool        Hovered  = TabRect.Contains(Cursor);

            Surface.FillRectangle(
                TabRect,
                ScaleAlpha(Selected ? ColorRgba{ 62, 68, 56, 255 } : (Hovered ? ColorRgba{ 50, 53, 47, 255 } : ColorRgba{ 43, 45, 40, 255 }), Opacity),
                12.5f);
            Surface.StrokeRectangle(
                TabRect,
                ScaleAlpha(Selected ? ColorRgba{ 166, 185, 151, 115 } : ColorRgba{ 255, 255, 255, 14 }, Opacity),
                12.5f,
                1.0f);

            const std::string& Caption = Spec.Channels[Idx].TabCaption.empty() ? Spec.Channels[Idx].Title : Spec.Channels[Idx].TabCaption;
            const float        TW      = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, Caption.c_str());
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { TabRect.Center().X - TW * 0.5f, TabRect.Y + 6.5f },
                9.5f,
                ScaleAlpha(Selected ? ColorRgba{ 226, 232, 218, 255 } : ColorRgba{ 182, 188, 174, 255 }, Opacity),
                Caption.c_str());

            if (Hovered && Pointer.Pressed)
            {
                Session.ActiveChannelIndex = static_cast<uint32_t>(Idx);
                Result.ChannelChanged      = true;
            }
        }
        BodyY += 35.0f;
    }

    // Optional View & History Span pills
    if (Spec.ShowViewSpanControls)
    {
        const std::array<std::pair<const char*, DiagnosticsViewMode>, 3> ViewTabs = {{
            { "Plot+Stats", DiagnosticsViewMode::CombinedPlotStats },
            { "Plot",       DiagnosticsViewMode::PlotOnly     },
            { "Stats",      DiagnosticsViewMode::StatsOnly    },
        }};
        float VX = Panel.X + 21.0f;
        for (const auto& [Label, Mode] : ViewTabs)
        {
            const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, Label) + 14.0f;
            const Rectangle2D Pill{ VX, BodyY, W, 20.0f };
            const bool Act = (Session.ViewMode == Mode);
            Surface.FillRectangle(Pill, ScaleAlpha(Act ? ColorRgba{ 58, 63, 53, 255 } : ColorRgba{ 38, 40, 36, 255 }, Opacity), 10.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Pill.X + 7.0f, Pill.Y + 4.5f },
                8.5f,
                ScaleAlpha(Act ? ColorRgba{ 226, 232, 218, 255 } : ColorRgba{ 158, 165, 150, 255 }, Opacity),
                Label);
            if (Pill.Contains(Cursor) && Pointer.Pressed)
            {
                Session.ViewMode = Mode;
            }
            VX += W + 4.0f;
        }

        const std::array<uint32_t, 3> Spans = {{ 15u, 30u, 60u }};
        float SX = Panel.Right() - 21.0f;
        for (auto It = Spans.rbegin(); It != Spans.rend(); ++It)
        {
            char Buf[16]{};
            std::snprintf(Buf, sizeof(Buf), "%us", *It);
            const float W = 28.0f;
            SX -= W;
            const Rectangle2D Pill{ SX, BodyY, W, 20.0f };
            const bool Act = (Session.HistorySeconds == *It);
            Surface.FillRectangle(Pill, ScaleAlpha(Act ? ColorRgba{ 58, 63, 53, 255 } : ColorRgba{ 38, 40, 36, 255 }, Opacity), 10.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, Buf);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Pill.Center().X - TW * 0.5f, Pill.Y + 4.5f },
                8.5f,
                ScaleAlpha(Act ? ColorRgba{ 226, 232, 218, 255 } : ColorRgba{ 158, 165, 150, 255 }, Opacity),
                Buf);
            if (Pill.Contains(Cursor) && Pointer.Pressed)
            {
                Session.HistorySeconds = *It;
            }
            SX -= 4.0f;
        }
        BodyY += 28.0f;
    }

    Result.SelectedChannel = Session.ActiveChannelIndex;

    // 4. Active metric channel body
    const float FooterY = Panel.Bottom() - 36.0f;
    if (!Spec.Channels.empty())
    {
        const DiagnosticsMetricChannel& Ch = Spec.Channels[Session.ActiveChannelIndex];

        Surface.FillRectangle({ Panel.X + 21.0f, BodyY + 2.0f, 3.0f, 14.0f }, ScaleAlpha(ColorRgba{ 88, 153, 121, 255 }, Opacity), 1.5f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Panel.X + 31.0f, BodyY },
            12.5f,
            ScaleAlpha(ColorRgba{ 214, 216, 209, 255 }, Opacity),
            Ch.Title.c_str());

        float ReadoutX = Panel.X + 21.0f;
        const float BigY = BodyY + 22.0f;
        if (!Ch.PrimaryWhole.empty())
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Light,
                { ReadoutX, BigY },
                44.0f,
                ScaleAlpha(ColorRgba{ 214, 216, 209, 255 }, Opacity),
                Ch.PrimaryWhole.c_str());
            ReadoutX += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 44.0f, Ch.PrimaryWhole.c_str());
        }
        if (!Ch.PrimaryFraction.empty())
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Light,
                { ReadoutX, BigY + 12.0f },
                26.0f,
                ScaleAlpha(ColorRgba{ 148, 154, 140, 255 }, Opacity),
                Ch.PrimaryFraction.c_str());
            ReadoutX += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 26.0f, Ch.PrimaryFraction.c_str());
        }
        if (!Ch.PrimaryUnit.empty())
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Light,
                { ReadoutX + 6.0f, BigY + 26.0f },
                14.0f,
                ScaleAlpha(ColorRgba{ 138, 145, 130, 255 }, Opacity),
                Ch.PrimaryUnit.c_str());
        }

        float CursorY = BigY + 52.0f;
        if (!Ch.SourceCaption.empty())
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Panel.X + 21.0f, CursorY },
                10.5f,
                ScaleAlpha(ColorRgba{ 158, 168, 148, 255 }, Opacity),
                Ch.SourceCaption.c_str());
            CursorY += 20.0f;
        }
        else
        {
            CursorY += 4.0f;
        }

        const bool ShowPlot  = (Session.ViewMode != DiagnosticsViewMode::StatsOnly) && !Ch.Samples.empty();
        const bool ShowStats = (Session.ViewMode != DiagnosticsViewMode::PlotOnly);

        if (ShowPlot && CursorY + 88.0f < FooterY - 8.0f)
        {
            const Rectangle2D Plot{ Panel.X + 21.0f, CursorY, Panel.Width - 42.0f, 92.0f };
            Surface.FillRectangle(Plot, ScaleAlpha(ColorRgba{ 22, 24, 21, 255 }, Opacity), 10.0f);
            Surface.StrokeLine({ Plot.X, Plot.Center().Y }, { Plot.Right(), Plot.Center().Y }, ScaleAlpha(ColorRgba{ 255, 255, 255, 16 }, Opacity), 1.0f);

            float Lo = Ch.PlotMinBound;
            float Hi = Ch.PlotMaxBound;
            for (const float S : Ch.Samples)
            {
                Lo = std::min(Lo, S);
                Hi = std::max(Hi, S);
            }
            if (Ch.HasBudgetLine)
            {
                Hi = std::max(Hi, Ch.BudgetThreshold * 1.1f);
            }
            if (Hi - Lo < 0.5f)
            {
                Hi = Lo + 0.5f;
            }

            if (Ch.HasBudgetLine)
            {
                const float BNorm = std::clamp((Ch.BudgetThreshold - Lo) / (Hi - Lo), 0.0f, 1.0f);
                const float BY    = Plot.Bottom() - 10.0f - BNorm * (Plot.Height - 20.0f);
                Surface.StrokeLine({ Plot.X + 4.0f, BY }, { Plot.Right() - 4.0f, BY }, ScaleAlpha(ColorRgba{ 242, 168, 92, 175 }, Opacity), 1.2f);
            }

            std::vector<Point2D> Poly(Ch.Samples.size());
            for (std::size_t Step = 0u; Step < Ch.Samples.size(); ++Step)
            {
                const float NormX = Ch.Samples.size() > 1u
                                        ? static_cast<float>(Step) / static_cast<float>(Ch.Samples.size() - 1u)
                                        : 0.5f;
                const float NormY = std::clamp((Ch.Samples[Step] - Lo) / (Hi - Lo), 0.0f, 1.0f);
                Poly[Step]        = { Plot.X + 6.0f + NormX * (Plot.Width - 12.0f), Plot.Bottom() - 10.0f - NormY * (Plot.Height - 20.0f) };
            }
            if (Poly.size() >= 2u)
            {
                Surface.StrokePolyline(Poly, ScaleAlpha(Ch.AccentColor, Opacity), 1.6f, false);
            }

            CursorY = Plot.Bottom() + 12.0f;
        }

        if (ShowStats)
        {
            for (const auto& Row : Ch.Rows)
            {
                if (CursorY + 20.0f > FooterY - 4.0f)
                {
                    break;
                }
                Surface.StrokeLine({ Panel.X + 21.0f, CursorY }, { Panel.Right() - 21.0f, CursorY }, ScaleAlpha(ColorRgba{ 255, 255, 255, 15 }, Opacity), 1.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Regular,
                    { Panel.X + 21.0f, CursorY + 5.0f },
                    10.0f,
                    ScaleAlpha(ColorRgba{ 144, 153, 134, 255 }, Opacity),
                    Row.Label.c_str());
                const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, Row.ReadingText.c_str());
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Regular,
                    { Panel.Right() - 21.0f - VW, CursorY + 5.0f },
                    10.0f,
                    ScaleAlpha(ColorRgba{ 193, 202, 180, 255 }, Opacity),
                    Row.ReadingText.c_str());
                CursorY += 22.0f;
            }
        }

        if (!Ch.DebugSummary.empty() && CursorY + 18.0f <= FooterY - 6.0f)
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Panel.X + 21.0f, CursorY + 2.0f },
                9.0f,
                ScaleAlpha(ColorRgba{ 130, 140, 122, 255 }, Opacity),
                Ch.DebugSummary.c_str());
        }
    }

    // 5. Footer controls: Pause / Resume + Sample label + optional resize grip
    Surface.StrokeLine({ Panel.X + 16.0f, FooterY - 4.0f }, { Panel.Right() - 16.0f, FooterY - 4.0f }, ScaleAlpha(ColorRgba{ 255, 255, 255, 15 }, Opacity), 1.0f);

    const Rectangle2D PauseBtn{ Panel.X + 21.0f, FooterY, 60.0f, 24.0f };
    const bool        PauseHover = PauseBtn.Contains(Cursor);
    Surface.FillRectangle(PauseBtn, ScaleAlpha(PauseHover ? ColorRgba{ 54, 58, 49, 255 } : ColorRgba{ 43, 45, 40, 255 }, Opacity), 12.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { PauseBtn.X + 12.0f, PauseBtn.Y + 6.0f },
        9.5f,
        ScaleAlpha(ColorRgba{ 192, 197, 184, 255 }, Opacity),
        Session.Paused ? "Resume" : "Pause");
    if (PauseHover && Pointer.Pressed)
    {
        Session.Paused      = !Session.Paused;
        Result.PauseToggled = true;
    }

    const char* SampleCaption = Spec.FooterSampleLabel != nullptr ? Spec.FooterSampleLabel : "120 samples";
    const float SampleW       = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, SampleCaption);
    const float SampleRight   = Spec.AllowDragResize ? (Panel.Right() - 28.0f) : (Panel.Right() - 21.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { SampleRight - SampleW, FooterY + 7.0f },
        8.5f,
        ScaleAlpha(ColorRgba{ 120, 133, 108, 255 }, Opacity),
        SampleCaption);

    if (Spec.AllowDragResize)
    {
        const Rectangle2D ResizeGrip{ Panel.Right() - 22.0f, Panel.Bottom() - 22.0f, 18.0f, 18.0f };
        const ColorRgba   GripInk = ScaleAlpha(
            ResizeGrip.Contains(Cursor) || Session.DraggingResize ? ColorRgba{ 214, 216, 209, 255 } : ColorRgba{ 120, 133, 108, 255 },
            Opacity);
        Surface.StrokeLine({ ResizeGrip.X + 5.0f, ResizeGrip.Bottom() - 4.0f }, { ResizeGrip.Right() - 4.0f, ResizeGrip.Y + 5.0f }, GripInk, 1.3f);
        Surface.StrokeLine({ ResizeGrip.X + 10.0f, ResizeGrip.Bottom() - 4.0f }, { ResizeGrip.Right() - 4.0f, ResizeGrip.Y + 10.0f }, GripInk, 1.3f);
        if (Pointer.Pressed && ResizeGrip.Contains(Cursor))
        {
            Session.DraggingResize  = true;
            Session.DragStartCursor = Cursor;
            Session.DragStartBounds = Panel;
        }
    }

    Surface.PopClip();
    return Result;
}

} // namespace Frontier
