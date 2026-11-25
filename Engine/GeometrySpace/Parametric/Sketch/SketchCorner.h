/*====================================================================================================================================
                                                         SKETCHCORNER.H
====================================================================================================================================*/
// 🧩 Sketch corner fillet / chamfer (Plasticity fillet-curve): two lines meeting at a point get a tangent arc or a cut line

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketchCornerShape : uint8_t
{
    Fillet  = 0,                                                                             // Tangent arc of radius Size
    Chamfer = 1,                                                                             // Straight cut, Size back along both lines
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The sketch after the corners were cut. Corners lists the corner points that were rounded (in the order done);
//    Setback is the largest distance a corner moved back along its lines.
struct SketchCornerOutcome
{
    SketchModel      Sketch   = {};                                                          // [-]   - After the corners
    std::vector<int> Corners  = {};                                                          // [-]   - Corner points cut
    double           Setback  = 0.0;                                                         // [len] - Largest setback t
    std::string      Refusal  = {};                                                          // [-]   - Why nothing changed (empty: cut)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Corner points of a solved sketch: points where exactly two profile lines (not construction) end, at an angle away
//    from 0 and π (a straight joint is no corner). Ascending point order.
[[nodiscard]] std::vector<int> FindSketchCorners(const SketchModel& Sketch);

// 💡 Cut the corners Points (empty: every corner) of a solved sketch. For two lines from the corner P along unit
//    directions û₁, û₂ with interior angle θ:
//      fillet  r  → setback t = r / tan(θ/2), tangent points P + t·ûᵢ, centre P + (û₁ + û₂)/|û₁ + û₂| · r / sin(θ/2),
//                   an arc (counter-clockwise, as sketch arcs are) with Tangent to both lines and Radius r;
//      chamfer d  → points P + d·ûᵢ joined by a new line.
//    The lines end at the new points; P stays as the virtual sharp, pinned to both lines (PointOnLine), so every
//    dimension on the corner keeps its meaning. A Length on a cut line becomes the Distance from the virtual sharp to
//    the line's other end; EqualLength and Midpoint on a cut line drop. Refused (nothing changes) when a point is no
//    corner, the size is not positive, or a setback reaches the far end of a line (corners on one line share it).
[[nodiscard]] SketchCornerOutcome CutSketchCorners(const SketchModel& Sketch, const std::vector<int>& Points, double Size, SketchCornerShape Shape);

} // namespace Frontier::GeometrySpace::Parametric
