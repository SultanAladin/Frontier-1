/*============================================================================================================================================
                                                            SMOOTHSCROLLVIEW.CPP
============================================================================================================================================*/
// 🧩 Reusable animated vertical scroll view & studio pill scrollbar shared by Outliner, Inspector, InspectorRegistry, and Dropdown Pills

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/SmoothScrollView.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{
constexpr float TrackWidthPx         = 5.0f;
constexpr float TrackRightMarginPx   = 3.0f;
constexpr float TrackVerticalInsetPx = 4.0f;
constexpr float MinimumThumbHeightPx = 24.0f;
constexpr float ScrollEaseRateHz     = 16.0f;
constexpr float ScrollSnapEpsilonPx  = 0.25f;
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCROLL VIEW GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

bool SmoothScrollView::QueryScrollable(float ViewportHeight, float ContentHeight) const noexcept
{
    return ContentHeight > ViewportHeight + 1.0f && ViewportHeight > 24.0f;
}

Rectangle2D SmoothScrollView::QueryTrackBounds(Rectangle2D ViewportBounds) noexcept
{
    return {
        ViewportBounds.Right() - TrackWidthPx - TrackRightMarginPx,
        ViewportBounds.Y + TrackVerticalInsetPx,
        TrackWidthPx,
        std::max(1.0f, ViewportBounds.Height - TrackVerticalInsetPx * 2.0f),
    };
}

Rectangle2D SmoothScrollView::QueryThumbBounds(
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    float       ScrollOffset) noexcept
{
    const Rectangle2D Track        = QueryTrackBounds(ViewportBounds);
    const float       VisibleRatio = std::clamp(ViewportBounds.Height / std::max(1.0f, ContentHeight), 0.08f, 1.0f);
    const float       ThumbHeight  = std::min(Track.Height, std::max(MinimumThumbHeightPx, Track.Height * VisibleRatio));
    const float       MaxScroll    = std::max(1.0f, ContentHeight - ViewportBounds.Height);
    const float       Normalized   = std::clamp(ScrollOffset / MaxScroll, 0.0f, 1.0f);
    const float       ThumbY       = Track.Y + (Track.Height - ThumbHeight) * Normalized;
    return { Track.X, ThumbY, Track.Width, ThumbHeight };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 STATE & ANIMATION UPDATE
//------------------------------------------------------------------------------------------------------------------------

void SmoothScrollView::Reset() noexcept
{
    TargetOffsetY       = 0.0f;
    DisplayOffsetY      = 0.0f;
    CachedContentHeight = 0.0f;
    HasCachedHeight     = false;
    DraggingThumb       = false;
    Animating           = false;
    ThumbGrabOffset     = 0.0f;
    LastTickTime        = {};
}

void SmoothScrollView::ConfigureImmediateOffset(float OffsetY) noexcept
{
    const float Clamped = std::max(0.0f, OffsetY);
    TargetOffsetY       = Clamped;
    DisplayOffsetY      = Clamped;
    Animating           = false;
}

void SmoothScrollView::ConfigureTargetOffset(float OffsetY) noexcept
{
    TargetOffsetY = std::max(0.0f, OffsetY);
    Animating     = std::fabs(TargetOffsetY - DisplayOffsetY) >= ScrollSnapEpsilonPx;
}

void SmoothScrollView::EnsureRangeVisible(
    float RangeTopY,
    float RangeBottomY,
    float ViewportHeight,
    bool  Immediate) noexcept
{
    if (ViewportHeight <= 1.0f)
    {
        return;
    }
    float Desired = TargetOffsetY;
    if (RangeTopY < Desired)
    {
        Desired = std::max(0.0f, RangeTopY);
    }
    else if (RangeBottomY > Desired + ViewportHeight)
    {
        Desired = std::max(0.0f, RangeBottomY - ViewportHeight);
    }
    TargetOffsetY = Desired;
    if (Immediate)
    {
        DisplayOffsetY = Desired;
        Animating      = false;
    }
    else
    {
        Animating = std::fabs(TargetOffsetY - DisplayOffsetY) >= ScrollSnapEpsilonPx;
    }
}

SmoothScrollUpdateResult SmoothScrollView::Advance(
    Rectangle2D           ViewportBounds,
    float                 KnownContentHeight,
    const ControlPointer& Pointer,
    float                 WheelDeltaY,
    float                 WheelStepPx,
    double                ExplicitDeltaSeconds) noexcept
{
    SmoothScrollUpdateResult Result {};

    if (KnownContentHeight >= 0.0f)
    {
        CachedContentHeight = KnownContentHeight;
        HasCachedHeight     = true;
    }

    float DtSeconds = 1.0f / 60.0f;
    if (ExplicitDeltaSeconds > 0.0)
    {
        DtSeconds = std::clamp(static_cast<float>(ExplicitDeltaSeconds), 1.0f / 240.0f, 1.0f / 20.0f);
    }
    else
    {
        const auto Now = std::chrono::steady_clock::now();
        if (LastTickTime.time_since_epoch().count() != 0)
        {
            const float RawDt = std::chrono::duration<float>(Now - LastTickTime).count();
            DtSeconds         = std::clamp(RawDt, 1.0f / 240.0f, 1.0f / 20.0f);
        }
        LastTickTime = Now;
    }

    const float EffectiveContentH = HasCachedHeight ? CachedContentHeight : 0.0f;
    const float MaxScroll         = HasCachedHeight ? std::max(0.0f, EffectiveContentH - ViewportBounds.Height) : 0.0f;
    const bool  CanDragThumb      = HasCachedHeight && MaxScroll > 0.5f;

    if (HasCachedHeight && MaxScroll <= 0.5f)
    {
        TargetOffsetY  = 0.0f;
        DisplayOffsetY = 0.0f;
        DraggingThumb  = false;
        Animating      = false;
        return Result;
    }

    const Point2D Cursor { Pointer.X, Pointer.Y };

    if (!Pointer.Down || !CanDragThumb)
    {
        DraggingThumb = false;
    }

    if (CanDragThumb)
    {
        const Rectangle2D Track    = QueryTrackBounds(ViewportBounds);
        const Rectangle2D Thumb    = QueryThumbBounds(ViewportBounds, EffectiveContentH, DisplayOffsetY);
        const Rectangle2D HitTrack { Track.X - 4.0f, Track.Y, Track.Width + 7.0f, Track.Height };

        if (Pointer.Enabled && Pointer.Pressed && HitTrack.Contains(Cursor))
        {
            Result.ConsumedPointer = true;
            DraggingThumb          = true;
            if (Cursor.Y >= Thumb.Y && Cursor.Y <= Thumb.Bottom())
            {
                ThumbGrabOffset = Cursor.Y - Thumb.Y;
            }
            else
            {
                ThumbGrabOffset      = Thumb.Height * 0.5f;
                const float Travel   = std::max(1.0f, Track.Height - Thumb.Height);
                const float NextNorm = std::clamp((Cursor.Y - ThumbGrabOffset - Track.Y) / Travel, 0.0f, 1.0f);
                TargetOffsetY        = NextNorm * MaxScroll;
            }
        }

        if (DraggingThumb && Pointer.Down)
        {
            Result.ConsumedPointer = true;
            const float Travel     = std::max(1.0f, Track.Height - Thumb.Height);
            const float NextNorm   = std::clamp((Cursor.Y - ThumbGrabOffset - Track.Y) / Travel, 0.0f, 1.0f);
            TargetOffsetY          = NextNorm * MaxScroll;
            DisplayOffsetY         = TargetOffsetY;
        }
    }

    if (!DraggingThumb && Pointer.Enabled && ViewportBounds.Contains(Cursor) && std::fabs(WheelDeltaY) > 1.0e-4f)
    {
        TargetOffsetY       -= WheelDeltaY * WheelStepPx;
        Result.ConsumedWheel = true;
    }

    if (HasCachedHeight)
    {
        TargetOffsetY = std::clamp(TargetOffsetY, 0.0f, MaxScroll);
    }
    else
    {
        TargetOffsetY = std::max(0.0f, TargetOffsetY);
    }

    // Critically-damped smooth scroll micro-animation (zero overshoot, zero snap-back)
    const float Alpha = 1.0f - std::exp(-ScrollEaseRateHz * DtSeconds);
    DisplayOffsetY += (TargetOffsetY - DisplayOffsetY) * Alpha;
    if (std::fabs(TargetOffsetY - DisplayOffsetY) < ScrollSnapEpsilonPx)
    {
        DisplayOffsetY = TargetOffsetY;
        Animating      = false;
    }
    else
    {
        Animating = true;
    }

    if (HasCachedHeight)
    {
        DisplayOffsetY = std::clamp(DisplayOffsetY, 0.0f, MaxScroll);
    }
    else
    {
        DisplayOffsetY = std::max(0.0f, DisplayOffsetY);
    }

    Result.DisplayOffsetY = DisplayOffsetY;
    Result.Animating      = Animating;
    return Result;
}

void SmoothScrollView::FinalizeContentHeight(float ViewportHeight, float MeasuredContentHeight) noexcept
{
    CachedContentHeight   = std::max(0.0f, MeasuredContentHeight);
    HasCachedHeight       = true;
    const float MaxScroll = std::max(0.0f, CachedContentHeight - ViewportHeight);
    TargetOffsetY         = std::clamp(TargetOffsetY, 0.0f, MaxScroll);
    DisplayOffsetY        = std::clamp(DisplayOffsetY, 0.0f, MaxScroll);
    if (std::fabs(TargetOffsetY - DisplayOffsetY) < ScrollSnapEpsilonPx)
    {
        DisplayOffsetY = TargetOffsetY;
        Animating      = false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                STUDIO PILL SCROLLBAR
//------------------------------------------------------------------------------------------------------------------------

void SmoothScrollView::RenderIndicator(
    PixelSpace& Surface,
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    Point2D     Cursor,
    float       Opacity) const noexcept
{
    if (!QueryScrollable(ViewportBounds.Height, ContentHeight))
    {
        return;
    }

    const Rectangle2D Track   = QueryTrackBounds(ViewportBounds);
    const Rectangle2D Thumb   = QueryThumbBounds(ViewportBounds, ContentHeight, DisplayOffsetY);
    const Rectangle2D HitArea { Track.X - 4.0f, Track.Y, Track.Width + 7.0f, Track.Height };
    const bool        Hot     = DraggingThumb || HitArea.Contains(Cursor);

    const uint8_t TrackAlpha = static_cast<uint8_t>(std::clamp(std::lround(14.0f * Opacity), 0l, 255l));
    const uint8_t ThumbAlpha = static_cast<uint8_t>(std::clamp(std::lround((Hot ? 210.0f : 150.0f) * Opacity), 0l, 255l));
    const uint8_t ThumbLuma  = DraggingThumb ? 172u : (Hot ? 146u : 104u);

    Surface.FillRectangle(Track, ColorRgba{ 255u, 255u, 255u, TrackAlpha }, 2.5f);
    Surface.FillRectangle(Thumb, ColorRgba{ ThumbLuma, ThumbLuma, ThumbLuma, ThumbAlpha }, 2.5f);
}

} // namespace Frontier
