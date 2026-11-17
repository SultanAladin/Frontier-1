/*====================================================================================================================================
                                                       SKETCHCONSTRAINING.H
====================================================================================================================================*/
// 🧩 Interactive sketch constraining: SolveSpace constraint verbs on picked points / curves, dimension measuring, point drag

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

enum class SketchPickKind : uint8_t
{
    Point = 0,                                                                               // Index into SketchModel::Points
    Curve = 1,                                                                               // Index into SketchModel::Curves
};

// 📝 The SolveSpace "Constrain" menu (Plasticity has no constraint commands); the key in brackets is SolveSpace's.
enum class SketchConstraintVerb : uint8_t
{
    Dimension     = 0,                                                                       // [D] line → length · 2 points → distance · circle / arc → radius
    Angle         = 1,                                                                       // [N] 2 lines → angle from the first to the second (CCW)
    Horizontal    = 2,                                                                       // [H] line → horizontal · 2 points → level (same y) · spline + its end → end tangent horizontal
    Vertical      = 3,                                                                       // [V] line → vertical · 2 points → plumb (same x) · spline + its end → end tangent vertical
    On            = 4,                                                                       // [O] 2 points → coincident · point + line / circle / spline → on it · 2 circles → concentric
    Equal         = 5,                                                                       // [Q] 2 lines → equal length · 2 circles / arcs → equal radius
    Midpoint      = 6,                                                                       // [M] point + line → at its midpoint
    Symmetric     = 7,                                                                       // [Y] 2 points + line → mirror images across it
    Parallel      = 8,                                                                       // [L] 2 lines → parallel · line + circle or 2 circles → tangent · spline + curve meeting at an end → tangent (G1)
    Perpendicular = 9,                                                                       // [[] 2 lines → perpendicular
    Lock          = 10,                                                                      // []] point → fixed where it is
    Curvature     = 11,                                                                      // [C] spline + curve meeting at an end → tangent and equal curvature (G2) · not in SolveSpace
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct SketchPick
{
    SketchPickKind Kind  = SketchPickKind::Point;                                            // [-] - Point or curve
    int            Index = -1;                                                               // [-] - Its index in the sketch
};

// 📝 A proposed constraint: dimensions carry the value measured on the sketch as it stands (the caller may replace it
//    with a typed value), so a fresh constraint is always met and only the solver diagnosis decides redundancy.
struct SketchConstraintProposal
{
    SketchConstraint Constraint = {};                                                        // [-] - Ready to append
    std::string      Refusal    = {};                                                        // [-] - "Distance: …" when the picks do not fit the verb
};

struct SketchDragOutcome
{
    SketchModel        Sketch          = {};                                                 // [-]   - Solved sketch after the drag
    SketchSolveOutcome Solve           = {};                                                 // [-]   - Its diagnosis (without the drag pin)
    bool               PinnedCondition = false;                                              // [-]   - The point landed on the target
    double             Travel          = 0.0;                                                // [len] - How far the point moved
    std::string        Refusal         = {};                                                 // [-]   - "Drag: …" when no solution was found
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Maps a SolveSpace verb and the picks (any order) to one constraint of the kernel's kinds, or a refusal naming what
//    the verb takes. Splines take O (point on it), L / C (tangent / curvature where it meets another curve at an end:
//    shared or coincident end points, or the end on a circle) and H / V with one of its ends (end direction); a pick out
//    of range, a curve picked twice or the same point twice is refused.
[[nodiscard]] SketchConstraintProposal ProposeSketchConstraint(const SketchModel& Sketch, const std::vector<SketchPick>& Picks, SketchConstraintVerb Verb);

// 💡 Shift+D in SolveSpace: a reference (driven) dimension — what D would add for these picks (or the angle of two lines),
//    with DrivenCondition set. It never constrains, so it is never redundant; the solver keeps its Value measured.
[[nodiscard]] SketchConstraintProposal ProposeSketchReference(const SketchModel& Sketch, const std::vector<SketchPick>& Picks);

// 💡 The value a dimension has on the sketch as it stands: distance / length / radius [len], horizontal or vertical
//    distance (signed, second − first) [len], angle from the first line to the second (CCW, (−π, π]) [rad]. 0 for kinds
//    without a dimension.
[[nodiscard]] double MeasureSketchConstraint(const SketchModel& Sketch, const SketchConstraint& Constraint);

// Dimension-carrying kinds: Distance, HorizontalDistance, VerticalDistance, Length, Angle, Radius.
[[nodiscard]] bool SketchDimensionCondition(SketchConstraintKind Kind) noexcept;

// "Line2", "Line2.Start", "Arc3.Centre", "Spline4.Point2", "Point0" (a point no curve uses).
[[nodiscard]] std::string NameSketchPick(const SketchModel& Sketch, SketchPick Pick);

// "Horizontal · Line1", "Length 2 · Line3", "Coincident · Line1.End · Line2.Start", "Angle 90° · Line1 · Line2".
[[nodiscard]] std::string DescribeSketchConstraint(const SketchModel& Sketch, const SketchConstraint& Constraint);

// "Distance", "Horizontal distance", "Equal length", …
[[nodiscard]] const char* SketchConstraintTitle(SketchConstraintKind Kind) noexcept;

// [D] [N] [H] … the SolveSpace key of a verb, and its menu name.
[[nodiscard]] char        SketchConstraintVerbKey(SketchConstraintVerb Verb) noexcept;
[[nodiscard]] const char* SketchConstraintVerbLabel(SketchConstraintVerb Verb) noexcept;

// 💡 Drags one point to Target the way SolveSpace does: ① the point is pinned at the target and the rest of the sketch
//    follows (minimum-norm solve); ② when the pin conflicts (the point has fewer free directions than 2), the sketch is
//    solved from the dragged configuration without the pin, so the point lands as near as its constraints allow.
[[nodiscard]] SketchDragOutcome DragSketchPoint(const SketchModel& Sketch, int Point, Vector2 Target, const SketchSolveSettings& Settings = {});

} // namespace Frontier::GeometrySpace::Parametric
