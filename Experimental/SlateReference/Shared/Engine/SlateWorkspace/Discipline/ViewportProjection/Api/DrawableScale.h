//============================================================================================================================================
//                                                          DRAWABLESCALE.H
//============================================================================================================================================
// 🧩 Converting between the two pixel units a viewport lives in.
//
// 🔴 THERE ARE TWO KINDS OF PIXEL AND THEY ARE NOT THE SAME NUMBER.
//
//    LOGICAL POINTS  — what ImGui reports. `io.MousePos`, `GetCursorScreenPos`, every PlaneExtent that
//                      comes back from a panel. This is the space the artist points in.
//    PHYSICAL PIXELS — what the swapchain is made of. `glfwGetFramebufferSize`, the Vulkan viewport, the
//                      scissor rectangle. This is the space the image is drawn in.
//
//    On an unscaled display the two are equal and every confusion between them is invisible. At 150%
//    display scaling a logical point is 1.5 physical pixels, and mixing the units puts drawn geometry
//    hundreds of pixels away from the cursor that placed it.
//
// ⚠️ THIS IS NOT A HYPOTHETICAL. `ResolveCadProjection` built its screen mapping from a PlaneExtent in
//    logical points and handed the shader a `DisplayWidth` in physical pixels, and the shader divides one
//    by the other to reach clip space. The scissor had the same mismatch — a logical rectangle clamped
//    against a physical width. At 150% a point the picker placed at x=1200 was drawn at x=800.
//
// 📝 The type exists so the conversion cannot be forgotten. A bare `float` for a width carries no
//    indication of which pixel it is, which is exactly how the two got mixed; asking for
//    `Scale.ToPhysical(Extent)` states the intent and the unit at the call site.

#pragma once

#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

namespace Slate
{

/// 🧩 How many physical pixels one logical point covers.
struct DrawableScale
{
    /// ⚠️ One, not zero. An unset scale must behave as an unscaled display rather than collapsing every
    ///    extent to nothing — a wrong-but-sane image beats no image.
    double Factor = 1.0;

    /// 🧩 Reads the scale from the two extents actually measured, rather than trusting a reported value.
    /// in    LogicalWidth   [pt] the whole display extent as the interface reports it
    /// in    PhysicalWidth  [px] the swapchain extent
    /// note ⚠️ Derived from what both sides really are, so it cannot disagree with them. A scale read from
    ///       the window system separately can be stale for a frame after a monitor change, and a stale
    ///       scale is exactly the failure this type exists to prevent.
    static DrawableScale Between(double LogicalWidth, double PhysicalWidth)
    {
        if (LogicalWidth <= 0.0 || PhysicalWidth <= 0.0)
            return {};
        return { PhysicalWidth / LogicalWidth };
    }

    bool Unscaled() const { return Factor > 0.999 && Factor < 1.001; }

    double ToPhysical(double LogicalPoints) const { return LogicalPoints * Factor; }
    double ToLogical(double PhysicalPixels) const { return Factor > 0.0 ? PhysicalPixels / Factor : PhysicalPixels; }

    /// 🧩 The same rectangle measured in physical pixels.
    /// note 🔴 Both the position AND the size scale. Scaling only the size leaves a panel on the right of
    ///       the display clipped to a rectangle that starts too far left.
    PlaneExtent ToPhysical(const PlaneExtent& Logical) const
    {
        return { static_cast<float>(ToPhysical(Logical.MinimumX)),
                 static_cast<float>(ToPhysical(Logical.MinimumY)),
                 static_cast<float>(ToPhysical(Logical.MaximumX)),
                 static_cast<float>(ToPhysical(Logical.MaximumY)) };
    }
};

}   // namespace Slate
