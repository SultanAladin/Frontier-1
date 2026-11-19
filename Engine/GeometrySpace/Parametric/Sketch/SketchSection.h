/*====================================================================================================================================
                                                         SKETCHSECTION.H
====================================================================================================================================*/
// 🧩 A solved sketch as one exact NURBS section curve (lines, polylines, arcs, slots, circles, splines) for Loft

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <optional>
#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketchSectionOutcome
{
    std::optional<NurbsCurve> Curve           = {};                                          // [-]   - The section (world)
    bool                      ClosedCondition = false;                                       // [-]   - Closed loop (first pole = last pole)
    int                       Pieces          = 0;                                           // [-]   - Sketch curves joined
    std::string               Refusal         = {};                                          // [-]   - Why no section (empty: fine)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The drawn (non-construction) curves of a solved sketch must form ONE chain: a closed loop (rectangle, polygon,
//    slot, circle, closed polyline / spline) or an open run (line, polyline, arc, open spline). Each piece is converted
//    exactly (line → degree 1, arc / circle → rational quadratic, spline → its B-spline), raised to one degree and
//    joined with parameter spans proportional to piece length (JoinNurbsCurves), lifted onto Plane.
//    Closed loops run counter-clockwise about Plane.ZAxis and start where the ray from the loop centroid along
//    +Plane.XAxis meets the loop (split exactly there), so similar sections line up without a twist.
[[nodiscard]] SketchSectionOutcome ComposeSketchSection(const SketchModel& Sketch, const OrthonormalBasis& Plane);

} // namespace Frontier::GeometrySpace::Parametric
