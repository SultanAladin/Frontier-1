//============================================================================================================================================
//                                                        WORLDSKETCHCORNER.H
//============================================================================================================================================
// 🧩 Rounds or cuts the corner where two curves of a world sketch meet — the 2D fillet and chamfer.
//
// 🔴 A CORNER IS NOT A CURVE, AND IT IS NOT A POINT EITHER. It is the JUNCTION of two curves that share an
//    endpoint, and it only exists while both of them do. That is why this unit names a corner by the two
//    curves that form it rather than by an index into something: an index into a loop's traversal is
//    invalidated by the very operation that fillets it, and an index into a point list cannot say which
//    of the several curves meeting there the artist meant.
//
// 🔴 IT DOES NOT REQUIRE A LOOP. The retired `ProfileCorner` could only round a corner of a RESOLVED
//    PROFILE LOOP, so two loose lines sharing an end -- an L, the most obvious thing an artist would try
//    to fillet -- had no corner at all as far as it was concerned. A junction is a junction. Whether the
//    curves happen to close into a loop decides whether the result can be filled, and nothing else.
//
// 🔴 THE CLAMP IS PART OF THE OPERATION, NOT A GUARD AROUND IT. A fillet eats `Radius / tan(θ/2)` off each
//    leg, so a radius large enough to eat more than a leg is long does not "fail" -- it asks for
//    something no geometry can express. `ResolveCornerLimit` answers the largest radius the corner can
//    take, so a drag can be held at the limit and the artist sees the fillet stop growing rather than
//    the tool refusing them with nothing on screen.
//
// 📝 Straight legs only, for now. Both curves at the junction must be lines; a line meeting an arc is a
//    tangent problem with a different construction, and answering `UnsupportedGeometry` for it is honest
//    where quietly rounding it against the chord would not be.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHAT A CORNER IS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The junction of two curves that share one endpoint.
/// note  📝 `Position` is where they actually meet, so a caller that has already resolved the corner does
///        not have to resolve it a second time to draw a handle on it.
struct WorldCornerTarget
{
    WorldCurveName First    = {};        // [-] - one of the two curves meeting at the junction
    WorldCurveName Second   = {};        // [-] - the other
    SpatialPoint   Position = {};        // [-] - where they meet
    double         Radians  = 0.0;       // [rad] - the interior angle between the two legs
    double         Limit    = 0.0;       // [-] - the largest radius this corner can take

    bool Declared() const { return First.Assigned() && Second.Assigned() && Limit > 0.0; }
};

/// 🧩 How a corner operation ended.
/// note  🔴 Separate refusals rather than one `false`. An artist dragging a fillet past the limit and an
///        artist clicking a corner made of two arcs need different answers, and a caller that cannot
///        tell them apart cannot say anything useful.
enum class CornerVerdict : std::uint32_t
{
    Produced            = 0u,   // [-] - the corner was rounded or cut
    NoSharedEndpoint    = 1u,   // [-] - the two curves do not meet
    Collinear           = 2u,   // [-] - the legs run straight through; there is no corner to round
    RadiusNotPositive   = 3u,   // [-] - zero or negative was asked for
    RadiusBeyondLimit   = 4u,   // [-] - larger than the legs can give up
    UnsupportedGeometry = 5u    // [-] - at least one leg is not a line
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      FINDING A CORNER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The corner nearest a probe, among every junction in the sketch.
/// in    Declared  [-] the sketch to search
/// in    Probe     [-] where the artist clicked
/// in    Reach     [-] how far from a junction the probe may be and still name it, in world units
/// out   Result    [-] refuses when no two curves share an endpoint within `Reach` of the probe
/// note  🔴 SEARCHES THE WHOLE SKETCH, not one selected curve's loops. The artist points at a corner; they
///        should not have to select an edge first and then point at one of its ends.
/// cost  🚩
/// tag   api, nonthrowing
Deliver<WorldCornerTarget> ResolveWorldCornerNear(const WorldSketchStructure& Declared,
                                                  const SpatialPoint& Probe,
                                                  double Reach);

/// 🧩 Every corner in the sketch, for drawing handles on all of them at once.
/// note  📝 A junction of three or more curves yields one entry per PAIR, because each pair is a corner
///        that can be rounded independently.
/// cost  🚩🚩
/// tag   api, nonthrowing
void CollectWorldCorners(const WorldSketchStructure& Declared,
                         std::vector<WorldCornerTarget>& Corners);

/// 🧩 The largest radius a corner can take before it would eat past the end of a leg.
/// out   Limit  [-] zero when the corner cannot be rounded at all
/// note  🔴 THIS IS THE CLAMP THE DRAG NEEDS. Without it a drag either refuses silently past some
///        threshold nobody can see, or produces geometry that has eaten its own neighbour.
/// note  📝 Half the shorter leg is the true bound: two adjacent corners of the same short edge must both
///        fit, and each may claim up to half of it.
/// tag   api, nonthrowing
double ResolveCornerLimit(const WorldSketchStructure& Declared,
                          WorldCurveName First,
                          WorldCurveName Second);

//------------------------------------------------------------------------------------------------------------------------
//                                                    ROUNDING AND CUTTING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether a corner operation would succeed, without changing anything.
/// note  📝 The dry run the preview uses. Same arguments, same answer, no mutation -- so what the artist
///        sees while dragging cannot disagree with what lands when they release.
/// tag   api, nonthrowing
CornerVerdict EvaluateWorldCorner(const WorldSketchStructure& Declared,
                                  WorldCurveName First,
                                  WorldCurveName Second,
                                  double Radius);

/// 🧩 The shape a corner operation would produce, without changing anything.
/// in    Chamfer     [-] true describes the straight chord; false the rounded arc
/// out   EnterPoint  [-] where the new curve meets the first leg
/// out   Through     [-] a point on the new curve between its ends -- the arc's far point, or the
///                       chord's midpoint, so both manners answer in the same shape
/// out   ExitPoint   [-] where it meets the second leg
/// note  🔴 THE PREVIEW THE ARTIST DRAGS AGAINST. Until this existed there was no way to see a fillet
///        without committing it, so the artist dragged a slider and watched a sharp corner stay sharp
///        with no way to tell a working tool from a broken one.
/// note  🔴 `ApplyWorldCorner` CALLS THIS RATHER THAN REPEATING IT, so what is drawn and what is written
///        are one derivation. A preview that computes the same shape a second way is a preview that will
///        eventually disagree with the commit.
/// tag   api, nonthrowing
CornerVerdict EvaluateWorldCornerShape(const WorldSketchStructure& Declared,
                                       WorldCurveName First,
                                       WorldCurveName Second,
                                       double Radius,
                                       bool Chamfer,
                                       SpatialPoint& EnterPoint,
                                       SpatialPoint& Through,
                                       SpatialPoint& ExitPoint);

/// 🧩 Rounds a corner with an arc, or cuts it with a straight chamfer.
/// in    Declared  [-] the sketch, mutated in place
/// in    First     [-] one leg of the junction
/// in    Second    [-] the other
/// in    Radius    [-] the fillet radius, or the chamfer's setback along each leg
/// in    Chamfer   [-] true cuts the corner straight; false rounds it
/// out   Verdict   [-] `Produced`, or why not; nothing is changed unless it is `Produced`
/// note  🔴 THE TWO LEGS ARE SHORTENED, NOT REPLACED. Their names survive, so a loop that traverses them,
///        a constraint that names them and a selection that holds them all remain valid. Replacing them
///        would silently break every one of those, which is the sort of damage that shows up three
///        operations later as a loop that will not close.
/// note  🔴 ALL OR NOTHING. The legs are only written once the new curve has been declared, so a refusal
///        cannot leave a sketch with two shortened legs and no arc between them.
/// note  📝 A chamfer is the same construction with a line instead of an arc, which is why one function
///        answers both rather than two that can disagree about where the tangent points are.
/// cost  🚩
/// tag   api, nonthrowing
CornerVerdict ApplyWorldCorner(WorldSketchStructure& Declared,
                               WorldCurveName First,
                               WorldCurveName Second,
                               double Radius,
                               bool Chamfer,
                               WorldCurveName& Produced);

} // namespace Slate
