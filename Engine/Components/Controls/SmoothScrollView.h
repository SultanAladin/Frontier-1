/*============================================================================================================================================
                                                             SMOOTHSCROLLVIEW.H
============================================================================================================================================*/
// 🧩 Reusable animated vertical scroll view & studio pill scrollbar shared by Outliner, Inspector, InspectorRegistry, and Dropdown Pills

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <chrono>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SmoothScrollUpdateResult
{
    float DisplayOffsetY  = 0.0f;  // [px] - Critically damped visual scroll offset to apply to content
    bool  ConsumedWheel   = false; // [-]  - True when pointer hovered viewport and wheel scrolled
    bool  ConsumedPointer = false; // [-]  - True when scrollbar thumb captured pointer input
    bool  Animating       = false; // [-]  - True while DisplayOffsetY is easing toward TargetOffsetY
};

class SmoothScrollView
{
public:
    SmoothScrollView() noexcept = default;

    void Reset() noexcept;
    void ConfigureImmediateOffset(float OffsetY) noexcept;
    void ConfigureTargetOffset(float OffsetY) noexcept;
    void EnsureRangeVisible(float RangeTopY, float RangeBottomY, float ViewportHeight, bool Immediate = false) noexcept;

    // Advance smooth scroll using a known content height (or cached height when KnownContentHeight < 0)
    [[nodiscard]] SmoothScrollUpdateResult Advance(
        Rectangle2D           ViewportBounds,
        float                 KnownContentHeight,
        const ControlPointer& Pointer,
        float                 WheelDeltaY,
        float                 WheelStepPx = 42.0f,
        double                ExplicitDeltaSeconds = -1.0) noexcept;

    // Post-layout clamp for panels whose content height is measured during draw
    void FinalizeContentHeight(float ViewportHeight, float MeasuredContentHeight) noexcept;

    void RenderIndicator(
        PixelSpace& Surface,
        Rectangle2D ViewportBounds,
        float       ContentHeight,
        Point2D     Cursor,
        float       Opacity = 1.0f) const noexcept;

    [[nodiscard]] bool  QueryScrollable(float ViewportHeight, float ContentHeight) const noexcept;
    [[nodiscard]] bool  QueryDraggingThumb() const noexcept { return DraggingThumb; }
    [[nodiscard]] bool  QueryAnimating() const noexcept     { return Animating; }
    [[nodiscard]] float QueryDisplayOffset() const noexcept { return DisplayOffsetY; }
    [[nodiscard]] float QueryTargetOffset() const noexcept  { return TargetOffsetY; }
    [[nodiscard]] float QueryCachedContentHeight() const noexcept { return CachedContentHeight; }

    [[nodiscard]] static Rectangle2D QueryTrackBounds(Rectangle2D ViewportBounds) noexcept;
    [[nodiscard]] static Rectangle2D QueryThumbBounds(
        Rectangle2D ViewportBounds,
        float       ContentHeight,
        float       ScrollOffset) noexcept;

private:
    float                                 TargetOffsetY       = 0.0f;  // [px] - Desired clamped scroll target
    float                                 DisplayOffsetY      = 0.0f;  // [px] - Critically damped visual scroll offset
    float                                 CachedContentHeight = 0.0f;  // [px] - Last known/measured content height
    bool                                  HasCachedHeight     = false; // [-]  - True once content height is known
    bool                                  DraggingThumb       = false; // [-]  - True while pointer drags scrollbar pill thumb
    bool                                  Animating           = false; // [-]  - True while DisplayOffsetY is easing toward TargetOffsetY
    float                                 ThumbGrabOffset     = 0.0f;  // [px] - Pointer offset inside thumb during drag
    std::chrono::steady_clock::time_point LastTickTime        {};      // [tp] - Monotonic timestamp for smooth dt
};

} // namespace Frontier
