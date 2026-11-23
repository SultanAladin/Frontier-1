//============================================================================================================================================
//                                                       DRAWABLESCALEPROOF.CPP
//============================================================================================================================================
// 🧩 Proves the conversion between logical points and physical pixels, and pins the shipped defect that
//    made the parametric viewport draw geometry away from the cursor that placed it.

#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cmath>
#include <cstdio>

using namespace Slate;

namespace
{

std::uint32_t Claims = 0u;
std::uint32_t Failures = 0u;

void Claim(bool Held, const char* Sentence)
{
    ++Claims;
    if (!Held)
    {
        ++Failures;
        std::printf("  FAILED  %s\n", Sentence);
    }
}

bool Near(double Left, double Right, double Tolerance = 1.0e-9)
{
    return std::fabs(Left - Right) <= Tolerance;
}

//========================================================================================================
// 1. READING THE SCALE
//========================================================================================================

void ProveReading()
{
    std::printf("\n1. Reading the scale from the two measured extents\n");

    Claim(Near(DrawableScale::Between(1600.0, 1600.0).Factor, 1.0), "equal extents read as unscaled");
    Claim(DrawableScale::Between(1600.0, 1600.0).Unscaled(), "and report themselves unscaled");
    Claim(Near(DrawableScale::Between(1600.0, 2400.0).Factor, 1.5), "1600 logical to 2400 physical is 150%");
    Claim(Near(DrawableScale::Between(1600.0, 3200.0).Factor, 2.0), "1600 logical to 3200 physical is 200%");
    Claim(!DrawableScale::Between(1600.0, 2400.0).Unscaled(), "150% does not report itself unscaled");

    // ⚠️ A zero extent must not produce a zero or infinite factor.
    Claim(Near(DrawableScale::Between(0.0, 1600.0).Factor, 1.0), "a zero logical extent falls back to unscaled");
    Claim(Near(DrawableScale::Between(1600.0, 0.0).Factor, 1.0), "a zero physical extent falls back to unscaled");
    Claim(Near(DrawableScale::Between(-1.0, 100.0).Factor, 1.0), "a negative extent falls back to unscaled");
    Claim(Near(DrawableScale{}.Factor, 1.0), "a default-constructed scale is unscaled, not zero");
}

//========================================================================================================
// 2. THE CONVERSION
//========================================================================================================

void ProveConversion()
{
    std::printf("\n2. Converting a measurement between the two units\n");

    const DrawableScale Half = DrawableScale::Between(1600.0, 2400.0);

    Claim(Near(Half.ToPhysical(100.0), 150.0), "100 logical points is 150 physical pixels at 150%");
    Claim(Near(Half.ToLogical(150.0), 100.0), "and 150 physical pixels is 100 logical points");
    Claim(Near(Half.ToLogical(Half.ToPhysical(437.25)), 437.25), "the conversion round-trips");
    Claim(Near(Half.ToPhysical(0.0), 0.0), "zero converts to zero");

    const DrawableScale One;
    Claim(Near(One.ToPhysical(812.5), 812.5), "an unscaled display changes nothing");
    Claim(Near(One.ToLogical(812.5), 812.5), "in either direction");

    // 🔴 Both the position and the size scale.
    const PlaneExtent Logical  = { 800.0f, 100.0f, 1600.0f, 900.0f };
    const PlaneExtent Physical = Half.ToPhysical(Logical);

    Claim(Near(Physical.MinimumX, 1200.0), "the leading edge scales");
    Claim(Near(Physical.MinimumY, 150.0), "the upper edge scales");
    Claim(Near(Physical.MaximumX, 2400.0), "the trailing edge scales");
    Claim(Near(Physical.MaximumY, 1350.0), "the lower edge scales");
    Claim(Near(Physical.Width(), Half.ToPhysical(Logical.Width())), "the width scales with the rest");
    Claim(Near(Physical.Height(), Half.ToPhysical(Logical.Height())), "and the height too");

    // ⚠️ THE CLAIM THAT CATCHES THE SCISSOR BUG. A panel on the right of the display must not keep its
    //    logical origin while taking a physical size, or it is clipped to the wrong region entirely.
    Claim(!Near(Physical.MinimumX, Logical.MinimumX),
          "a panel away from the origin does NOT keep its logical position when measured physically");
}

//========================================================================================================
// 3. THE SHIPPED DEFECT
//========================================================================================================

/// 🧩 Replays what ResolveCadProjection's orthographic arm computes, then follows the number through the
///    shader the way the GPU does, and reports where the geometry actually lands in logical points.
double DrawnAtLogicalX(const SpatialBasis& Basis,
                       const ViewportStanding& View,
                       const PlaneExtent& Leaf,
                       double Along,
                       double Across,
                       double PhysicalDisplayWidth,
                       double Scale)
{
    const ViewFrame Frame = ResolveViewportFrame(Basis, View, false);
    const double CentreX = Leaf.MinimumX + Leaf.Width() * 0.5;

    const SpatialPoint Subject = Added(Basis.Origin, Added(Scaled(Basis.Along, Along),
                                                           Scaled(Basis.Across, Across)));
    const SpatialDirection FocusToSubject = Difference(View.Focus, Subject);

    // What the host packs into Projection0 — built entirely from the LOGICAL leaf extent.
    const double PackedX = CentreX + Dot(FocusToSubject, Frame.Right) * View.OrthoScale;

    // What the shader does with it: divide by the DisplayWidth it was handed to reach clip space.
    const double Ndc = PackedX / PhysicalDisplayWidth * 2.0 - 1.0;

    // The Vulkan viewport covers the whole physical swapchain, so clip space maps back across it.
    const double DrawnPhysical = (Ndc * 0.5 + 0.5) * PhysicalDisplayWidth;

    // And the artist sees it in logical points.
    return DrawnPhysical / Scale;
}

void ProveShippedDefect()
{
    std::printf("\n3. The defect: geometry drawn away from the cursor that placed it\n");

    SpatialBasis Basis;
    Basis.Origin = { 0.0, 0.0, 0.0 };
    Basis.Normal = { 0.0, 1.0, 0.0 };
    Basis.Along  = { 1.0, 0.0, 0.0 };
    Basis.Across = { 0.0, 0.0, 1.0 };

    ViewportStanding View;
    View.Focus      = { 0.0, 0.0, 0.0 };
    View.OrthoScale = 4.0;
    View.Distance   = 240.0;

    const PlaneExtent Leaf = { 0.0f, 0.0f, 1600.0f, 900.0f };
    const double Along = 100.0;
    const double Across = 50.0;

    // Where the picker says that plane point is. This is the space io.MousePos lives in, so this is where
    // the artist's cursor was when they placed it.
    float PickerX = 0.0f;
    float PickerY = 0.0f;
    const bool Projected = ProjectViewportPoint(Basis, View, false, Leaf, Along, Across, PickerX, PickerY);
    Claim(Projected, "the point projects to a screen position");

    // 📝 UNSCALED: the two units are the same number, so the mismatch cannot be seen.
    {
        const double Drawn = DrawnAtLogicalX(Basis, View, Leaf, Along, Across, 1600.0, 1.0);
        Claim(Near(Drawn, static_cast<double>(PickerX), 1.0e-6),
              "on an unscaled display the geometry is drawn exactly where the picker says");
    }

    // 🔴 SCALED: the leaf is still 1600 logical points wide, but the swapchain is 2400 physical pixels.
    //    Handing the shader the physical width while the packed coordinate is logical divides by the
    //    wrong number.
    {
        const double Drawn = DrawnAtLogicalX(Basis, View, Leaf, Along, Across, 2400.0, 1.5);
        Claim(!Near(Drawn, static_cast<double>(PickerX), 1.0),
              "at 150% scaling the SHIPPED arithmetic draws the point somewhere else entirely");
        Claim(Near(Drawn, static_cast<double>(PickerX) / 1.5, 1.0e-6),
              "and it is out by exactly the scale factor - 1200 becomes 800");
        Claim(std::fabs(Drawn - static_cast<double>(PickerX)) > 300.0,
              "which is a 400 logical point error, not a rounding difference");
    }

    // 🔴 THE FIX. Convert the leaf extent to physical pixels before building the projection, and the two
    //    agree again at every scale.
    for (double Scale : { 1.0, 1.25, 1.5, 2.0, 3.0 })
    {
        const DrawableScale Drawable = DrawableScale::Between(1600.0, 1600.0 * Scale);
        const PlaneExtent   Physical = Drawable.ToPhysical(Leaf);

        // Rebuilt from the PHYSICAL leaf, but the plane point and the ortho scale must scale too, so the
        // image is the same size on the display rather than shrinking as the scale rises.
        ViewportStanding Scaled = View;
        Scaled.OrthoScale = View.OrthoScale * Scale;

        const double Drawn = DrawnAtLogicalX(Basis, Scaled, Physical, Along, Across,
                                             1600.0 * Scale, Scale);
        Claim(Near(Drawn, static_cast<double>(PickerX), 1.0e-6),
              "converting the extent to physical pixels first puts the geometry back under the cursor");
    }
}

//========================================================================================================
// 4. THE SCISSOR
//========================================================================================================

void ProveScissor()
{
    std::printf("\n4. Clipping a viewport leaf\n");

    // A leaf occupying the right half of a 1600-point display, at 150%.
    const PlaneExtent   Leaf     = { 800.0f, 0.0f, 1600.0f, 900.0f };
    const DrawableScale Drawable = DrawableScale::Between(1600.0, 2400.0);
    const double        PhysicalWidth = 2400.0;

    // 🔴 THE SHIPPED BEHAVIOUR: a logical rectangle clamped against a physical width. The clamp does not
    //    fire, so it silently keeps logical numbers and clips the wrong region of the image.
    {
        const double ClippedRight = Leaf.MaximumX > PhysicalWidth ? PhysicalWidth : Leaf.MaximumX;
        Claim(Near(ClippedRight, 1600.0),
              "the shipped clamp leaves the trailing edge at 1600 - a logical number in a physical field");
        Claim(!Near(ClippedRight, 2400.0),
              "so the right third of the leaf is scissored away");
    }

    // The fix: convert, then clamp.
    {
        const PlaneExtent Physical = Drawable.ToPhysical(Leaf);
        const double ClippedRight = Physical.MaximumX > PhysicalWidth ? PhysicalWidth : Physical.MaximumX;
        Claim(Near(ClippedRight, 2400.0), "converted first, the trailing edge reaches the display edge");
        Claim(Near(Physical.MinimumX, 1200.0), "and the leading edge is at the true half-way point");
    }

    // ⚠️ The clamp still has to work, for a leaf that genuinely runs past the display.
    {
        const PlaneExtent Overhanging = { 800.0f, 0.0f, 2000.0f, 900.0f };
        const PlaneExtent Physical    = Drawable.ToPhysical(Overhanging);
        const double ClippedRight = Physical.MaximumX > PhysicalWidth ? PhysicalWidth : Physical.MaximumX;
        Claim(Near(ClippedRight, PhysicalWidth), "a leaf wider than the display is still clamped to it");
    }
}

} // namespace

int main()
{
    std::printf("=========================================================================\n");
    std::printf("DRAWABLE SCALE PROOF\n");
    std::printf("=========================================================================\n");

    ProveReading();
    ProveConversion();
    ProveShippedDefect();
    ProveScissor();

    std::printf("\n=========================================================================\n");
    std::printf("%u claims, %u failures -> %s\n", Claims, Failures,
                Failures == 0u ? "PROVEN" : "REFUTED");
    std::printf("=========================================================================\n");
    return Failures == 0u ? 0 : 1;
}
