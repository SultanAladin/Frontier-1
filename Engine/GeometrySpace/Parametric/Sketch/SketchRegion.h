/*====================================================================================================================================
                                                          SKETCHREGION.H
====================================================================================================================================*/
// 🧩 Closed regions of overlapping sketch curves (Plasticity fill): the planar arrangement, the face under the pointer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One boundary piece of a region: the input segment it comes from, the fractions it spans on it and the exact piece in
//    traversal order (arcs and circle pieces become arcs, spline pieces are cut by knot insertion).
struct RegionPiece
{
    int            Source       = -1;                                                        // [-] - Index into the input segments
    double         FromFraction = 0.0;                                                       // [-] - Start on the source (≤ ToFraction)
    double         ToFraction   = 1.0;                                                       // [-] - End on the source
    ProfileSegment Segment      = {};                                                        // [-] - The piece, oriented along the loop
};

// 📝 Loops[0] is the outer boundary (counter-clockwise), the rest are holes (clockwise). Outlines sample each loop for
//    previews (closed polylines, first point not repeated).
struct SketchRegion
{
    std::vector<std::vector<RegionPiece>> Loops    = {};                                     // [-]    - Outer, then holes
    std::vector<std::vector<Vector2>>     Outlines = {};                                     // [len]  - Sampled loops
    double                                Area     = 0.0;                                    // [len²] - Net area (outer − holes)
    std::string                           Refusal  = {};                                     // [-]    - Why no region (empty: found)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The smallest closed region holding Point in the arrangement of Segments (one plane, any mix of lines, arcs, circles
//    and splines from any number of sketches): ① every segment is cut at its crossings with the others (Newton-refined,
//    IntersectProfileSegments); ② piece ends within 10⁻⁶·Scale merge into vertices and dangling pieces are pruned;
//    ③ faces are traced with the region on the left (the next piece at a vertex is the first one clockwise from the way
//    back); ④ the smallest counter-clockwise face whose outline winds around Point is the outer loop, and the outer
//    boundaries of separate curve groups lying directly inside it are its holes. Two overlapping circles give three
//    regions: the lens and each crescent. Refused (named) when Point lies in no closed region.
[[nodiscard]] SketchRegion LocateSketchRegion(const std::vector<ProfileSegment>& Segments, Vector2 Point, double Scale);

// 💡 A sketch holding exactly the region's boundary: lines, arcs (counter-clockwise from start to end), circles, and each
//    spline piece as Bézier curves, one per knot span (exact). Consecutive pieces share their end points.
[[nodiscard]] SketchModel ComposeRegionSketch(const SketchRegion& Region);

// 💡 The part of Spline over the knot interval [Low, High] as Bézier spans (Boehm knot insertion to multiplicity p at Low,
//    High and every interior knot between them): each entry holds the p + 1 control points of one span, in order.
[[nodiscard]] std::vector<std::vector<Vector2>> ExtractSplineBezierSpans(const SketchSplineCurve& Spline, double Low, double High);

} // namespace Frontier::GeometrySpace::Parametric
