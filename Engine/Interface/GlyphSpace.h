//============================================================================================================================================
//                                                         GLYPHSPACE.H
//============================================================================================================================================
// 🧩 SVG path data (lucide 24 × 24 stroke glyphs) → flattened polylines in display pixels, drawn as strokes on a PixelSpace.
//
//    Supports the full SVG path command set the glyph tables use: M m L l H h V v C c S s Q q T t A a Z z.
//    Arcs are converted centre-parameterised (SVG F.6.5) and flattened with a fixed angular step; cubics and quadratics
//    are flattened with uniform parameter steps. Precision is more than adequate for 16–24 px stroke icons.
//
//    Coordinate convention: PixelSpace pixels, origin top-left, +Y down — the same as the SVG viewBox, so no flip.

#pragma once

#include "PixelSpace.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLYPH PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

struct GlyphPlacement
{
    float X            = 0.0f;   // [px] top-left of the glyph box on the display
    float Y            = 0.0f;   // [px]
    float Size         = 24.0f;  // [px] rendered box edge (the 24-unit viewBox is scaled to this)
    float StrokeWidth  = 2.0f;   // [viewBox units] lucide default 2; the Notch UI uses 1.5 for idle tiles
    ColorQuad Colour   = ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      GLYPH SPACE
//------------------------------------------------------------------------------------------------------------------------

class GlyphSpace
{
public:
    // Flatten an SVG path (viewBox units) into sub-path polylines. Each sub-path carries a Closed flag so the
    //    stroke joins its ends (Z) or leaves them open (lucide caps are round; ImGui renders butt caps — see .cpp).
    struct Contour
    {
        std::vector<PlanePoint> Points;   // [viewBox units]
        bool                    Closed = false;
    };

    [[nodiscard]] static std::vector<Contour> Flatten(std::string_view SvgPath) noexcept;

    // Stroke an SVG path onto the surface, scaled from a 24 × 24 viewBox into the placement box.
    static void Stroke(PixelSpace& Surface, std::string_view SvgPath, const GlyphPlacement& Placement, float ViewBox = 24.0f) noexcept;

    // Fill every closed sub-path (used for the "fill-current" active-tile treatment Notch applies to the icon).
    static void Fill(PixelSpace& Surface, std::string_view SvgPath, const GlyphPlacement& Placement, float ViewBox = 24.0f) noexcept;

private:
    static constexpr int   CurveSegments = 12;          // per cubic / quadratic
    static constexpr float ArcStepRadians = 0.2618f;    // 15° per arc segment
};

} // namespace Frontier
