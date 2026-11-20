/*====================================================================================================================================
                                                           SKETCHTRIM.H
====================================================================================================================================*/
// 🧩 Curve trim (Plasticity T): the piece of a sketch curve under the pointer, between its neighbouring crossings, is removed;
//    curve cut: the curve breaks in two at the pointer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One crossing of two oriented segments: fractions on both (EvaluateProfileSegment), and the meeting point.
struct SegmentCrossing
{
    double  TargetFraction = 0.0;                                                            // [-]   - On the trimmed segment
    double  CutterFraction = 0.0;                                                            // [-]   - On the cutting segment
    Vector2 Position       = {};                                                             // [len] - Where they meet
};

// 📝 The trimmed sketch and what came off. Pieces counts what is left of the curve: 0 (erased whole: nothing crossed it,
//    or a circle crossed once), 1 (an end piece came off, or a circle became an arc) or 2 (a middle piece came off, the
//    curve keeps the first part and a new curve carries the second). Removed samples the piece that came off.
struct SketchTrimOutcome
{
    SketchModel          Sketch        = {};                                                 // [-]   - After the trim
    int                  Pieces        = 0;                                                  // [-]   - Left of the curve (0, 1, 2)
    int                  Crossings     = 0;                                                  // [-]   - Distinct crossings found on the curve
    double               FromFraction  = 0.0;                                                // [-]   - Removed piece start (target fraction)
    double               ToFraction    = 1.0;                                                // [-]   - Removed piece end (circle: may exceed 1)
    std::vector<Vector2> Removed       = {};                                                 // [len] - Removed piece polyline
    std::vector<Vector2> SplitPositions = {};                                                // [len] - Split: where the curve now breaks
    std::string          Refusal       = {};                                                 // [-]   - Why nothing changed (empty: trimmed)
    int                  InsertedPoint = -1;                                                 // [-]   - Insert point: the new defining point
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Every crossing of Target with Cutter: polyline seeds (knot spans / ≤ 45° arc pieces, 24 samples each) refined by
//    Newton on Target(s) − Cutter(t) = 0 over the exact curves, kept within Join; touching ends count. Sorted by
//    TargetFraction, duplicates (≤ 10⁻⁹) merged. Overlapping collinear pieces have no isolated crossing and are skipped.
[[nodiscard]] std::vector<SegmentCrossing> IntersectProfileSegments(const ProfileSegment& Target, const ProfileSegment& Cutter, double Join);

// Fraction of the point of Segment nearest to Point (dense samples, then Newton on the tangent).
[[nodiscard]] double LocateProfileFraction(const ProfileSegment& Segment, Vector2 Point);

// 💡 Plasticity T on curve CurveIndex of a solved sketch: the crossings come from the sketch's other curves (all, also
//    construction) and from Foreign (curves of coplanar sketches, already in this sketch's plane coordinates). The piece
//    holding Pick between neighbouring crossings comes off:
//      line / arc → shortened, or split in two (the second part is a new curve with the next identifier);
//      circle     → the complementary arc (needs ≥ 2 crossings);
//      no crossing on an open curve, or < 2 on a circle → the curve is erased (Plasticity behaviour);
//      spline with crossings → refused (the point-defined forms have no exact split).
//    Constraints: those on an erased curve drop and later curve references shift; a shortened line drops Length,
//    EqualLength, Midpoint and Symmetric; a split line copies Horizontal, Vertical, Parallel, Perpendicular and Angle to
//    its new part, a split arc copies Radius and EqualRadius. A new end made by a line, circle or arc of the same sketch
//    is pinned to it (PointOnLine / PointOnCircle) so later solves keep the trim.
[[nodiscard]] SketchTrimOutcome TrimSketchCurve(const SketchModel& Sketch, int CurveIndex, Vector2 Pick, const std::vector<ProfileSegment>& Foreign = {});

// 💡 Plasticity Cut on curve CurveIndex at the point nearest to Pick: nothing comes off, the curve breaks there.
//      line / arc → two curves meeting at one shared new point (the second part gets the next identifier);
//      circle     → two arcs, from the pick to Partner and back (same centre and radius); without Partner (or with one
//                   at the pick) two half arcs, the second break at the opposite point;
//      spline, or a pick on an open curve's end → refused.
//    A split line drops Length, EqualLength, Midpoint and Symmetric and copies Horizontal, Vertical, Parallel,
//    Perpendicular and Angle to its second part; a split arc / circle copies Radius and EqualRadius.
[[nodiscard]] SketchTrimOutcome SplitSketchCurve(const SketchModel& Sketch, int CurveIndex, Vector2 Pick, std::optional<Vector2> Partner = std::nullopt);

// 💡 Insert point: a new defining point on curve CurveIndex nearest to Pick, the shape unchanged (to be dragged next).
//      line / arc / circle → split there as Cut does (a circle also at the opposite point); a split line drops its
//                            direction constraints (H, V, parallel, perpendicular, angle) so the new point moves freely;
//      cubic spline        → a through-point at its parameter τ*: the curve keeps one τ per point (SketchCurve::Knots), and
//                            the natural C² interpolant on the refined parameters is the same cubic pieces (exact);
//      Hermite / Bezier / NURBS → the curve becomes a NURBS on its own knot vector with τ* inserted once (Boehm): one more
//                            control point, the neighbours moved so the curve stays the same (exact; Hermite through-points
//                            stay as control points on the curve, their handles become control points too).
//    Refused: the pick on an open end, on an existing point, or where a NURBS already has a corner knot.
[[nodiscard]] SketchTrimOutcome InsertSketchCurvePoint(const SketchModel& Sketch, int CurveIndex, Vector2 Pick);

// Curve CurveIndex erased: constraints on it dropped, later curve references shifted down. Points stay.
void EraseSketchCurve(SketchModel& Sketch, int CurveIndex);

// 💡 Delete on sketch points (mode 1 X): every curve through one of Points goes (EraseSketchCurve), then every point no
//    remaining curve uses is dropped with the constraints naming it (Coincident, Fixed, distances, point-on, midpoint,
//    symmetric); later point references shift down. Returns the number of curves erased.
int EraseSketchPoints(SketchModel& Sketch, const std::vector<int>& Points);

} // namespace Frontier::GeometrySpace::Parametric
