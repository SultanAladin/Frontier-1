/*====================================================================================================================================
                                                         SKETCHEDITING.H
====================================================================================================================================*/
// 🧩 Sketch editing (Plasticity O offset-curve, J join-curves): exact offsets of line / arc / circle chains, sketch merging

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The offset copy: a fresh sketch (no constraints) whose chains meet at shared points, so it profiles as it is.
struct SketchOffsetOutcome
{
    SketchModel Sketch  = {};                                                                // [-] - Offset curves only
    int         Chains  = 0;                                                                 // [-] - Chains offset (circles count as one each)
    int         Closed  = 0;                                                                 // [-] - Of which closed
    std::string Refusal = {};                                                                // [-] - "Offset: …" when refused (Sketch empty)
};

// 📝 Where a sketch lands in another sketch's plane coordinates: the images of its origin and unit axes. A placement with
//    XAxis × YAxis < 0 mirrors (the two planes face opposite ways).
struct SketchPlacement
{
    Vector2 Origin = { 0.0, 0.0 };                                                           // [len] - Image of (0, 0)
    Vector2 XAxis  = { 1.0, 0.0 };                                                           // [-]   - Image of x̂ (unit)
    Vector2 YAxis  = { 0.0, 1.0 };                                                           // [-]   - Image of ŷ (unit, ⟂ XAxis)
};

struct SketchJoinOutcome
{
    SketchModel Sketch    = {};                                                              // [-] - Base with the joined curves appended
    int         Welded    = 0;                                                               // [-] - Joined points merged into base points
    int         Dropped   = 0;                                                               // [-] - Orientation constraints left out (moved frame)
    std::string Refusal   = {};                                                              // [-] - "Join: …" when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Plasticity O on a solved sketch: every non-construction chain (curves chained end to end within 10⁻⁹·L) is offset
//    by Distance — closed chains grow for Distance > 0 and shrink for Distance < 0; open chains move to the left of their
//    first curve's drawn direction for Distance > 0. Lines shift along their normal, arcs and circles keep their centre
//    with radius r ± d; neighbouring offset pieces are re-joined at the intersection of their carriers nearest the
//    original corner (sharp, mitred corners — Plasticity's default), tangent joins stay tangent. Exact: every offset
//    point lies at |Distance| from its source curve. Refused (named) for splines (no exact offset 🚧), branching chains,
//    a circle or arc whose radius would vanish, and a piece the offset turns inside out (🚧 no loop clean-up).
[[nodiscard]] SketchOffsetOutcome OffsetSketchCurves(const SketchModel& Sketch, double Distance);

// 💡 Plasticity J: the curves of Joined (placed into Base coordinates by Placement) are appended to Base. A joined point
//    within Join of a base point (or an earlier joined point) is welded into it, so chains crossing the two sketches
//    profile as one; joined curve identifiers continue after Base's largest; a mirroring placement swaps arc ends (arcs
//    run counter-clockwise). Constraints come along with their references renumbered; when the placement moves the
//    frame, the frame-bound ones (Fixed, Horizontal, Vertical, Horizontal / Vertical distance, and Angle under a mirror)
//    are left out and counted in Dropped. Refused when the placement axes are not orthonormal.
[[nodiscard]] SketchJoinOutcome JoinSketchModels(const SketchModel& Base, const SketchModel& Joined, const SketchPlacement& Placement, double Join);

// Joined's points placed into Base coordinates (the same map JoinSketchModels applies).
[[nodiscard]] SketchModel PlaceSketchModel(const SketchModel& Sketch, const SketchPlacement& Placement);

// 💡 Two sketches (Second already in First's coordinates) touch when a non-construction curve of one crosses or meets one
//    of the other (IntersectProfileSegments within Join, ends included).
[[nodiscard]] bool SketchContactCondition(const SketchModel& First, const SketchModel& Second, double Join);

// Characteristic length of a sketch: its points' bounding diagonal, at least every circle's diameter (1 when empty).
[[nodiscard]] double MeasureSketchScale(const SketchModel& Sketch);

} // namespace Frontier::GeometrySpace::Parametric
