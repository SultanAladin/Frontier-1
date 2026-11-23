//============================================================================================================================================
//                                                        OVERFLOWSCROLL.H
//============================================================================================================================================
// 🧩 Shared wheel scrolling for any bounded interface region whose content can exceed its viewport.

#pragma once

#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

#include <algorithm>

namespace Slate
{

/// 🧩 Retains one region's eased vertical offset and derives a theme-neutral scrollbar thumb.
/// note  Panels own one instance per independently scrolling page; the scrolling arithmetic is shared.
struct OverflowScroll
{
    float Shown = 0.0f;
    float Wanted = 0.0f;

    float Advance(const PointerCondition& Pointer, const PlaneExtent& Viewport,
                  float ContentHeight, float WheelStep = 72.0f)
    {
        const float Limit = std::max(ContentHeight - Viewport.Height(), 0.0f);
        if (Viewport.Encloses(Pointer.PositionX, Pointer.PositionY) && Pointer.WheelY != 0.0f)
            Wanted = std::clamp(Wanted - Pointer.WheelY * WheelStep, 0.0f, Limit);
        else
            Wanted = std::clamp(Wanted, 0.0f, Limit);

        Shown += (Wanted - Shown) * 0.22f;
        if (Shown < 0.05f && Wanted == 0.0f) Shown = 0.0f;
        if (Shown > Limit) Shown = Limit;
        return Shown;
    }

    PlaneExtent Thumb(const PlaneExtent& Viewport, float ContentHeight, float Width = 3.0f) const
    {
        if (ContentHeight <= Viewport.Height() || Viewport.Height() <= 0.0f)
            return {};
        const float Height = std::max(28.0f, Viewport.Height() * Viewport.Height() / ContentHeight);
        const float Limit = std::max(ContentHeight - Viewport.Height(), 1.0f);
        const float Travel = Viewport.Height() - Height;
        return Spanning(Viewport.MaximumX - Width, Viewport.MinimumY + Travel * (Shown / Limit), Width, Height);
    }

    void Reset() { Shown = Wanted = 0.0f; }
};

} // namespace Slate
