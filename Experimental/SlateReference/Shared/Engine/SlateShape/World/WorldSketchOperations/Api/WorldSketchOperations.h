//============================================================================================================================================
//                                                      WORLDSKETCHOPERATIONS.H
//============================================================================================================================================
// 🧩 The four editing operations that reshape curves already drawn: Cut, Trim, Extend and Offset.
//
// 🔴 CUT AND TRIM ARE OPPOSITES AND ARE EASILY CONFUSED, so they are stated together here rather than in
//    two units that could drift. CUT DIVIDES AND REMOVES NOTHING: one curve becomes two curves meeting at
//    the point clicked, and if they were part of a closed loop the loop is still closed, so its fill
//    stays. TRIM REMOVES: the piece between two divisions is deleted, and whatever it was part of is now
//    open. Cut first, then trim what the cut created, is the ordinary order.
//
// 🔴 EVERY OPERATION IS MEASURED IN THE LOOP'S OWN PLANE, never in world axes. Two curves cross in the
//    plane they share; asking whether two world-space segments intersect is a different and usually
//    wrong question, because two lines on a wall have no world-Y separation to speak of and would appear
//    to cross when they merely pass.
//
// 🔴 NOTHING HERE MOVES AN ENDPOINT THAT WAS NOT ASKED FOR. Extend grows one named end along its own
//    direction and leaves the far end and the direction alone. That is what makes it predictable, and it
//    is why the tool takes an END rather than a curve.
//
// 📝 Straight curves only, as with the corner operations. An arc trimmed at an intersection is a
//    different construction and is refused honestly rather than approximated against its chord.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       HOW THEY REFUSE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How an editing operation ended.
/// note  🔴 Discriminating refusals, not one `false`. "There was nothing to hit" and "that curve is an
///        arc" call for different things to be said to the artist, and a caller given one boolean can
///        say neither.
enum class OperationVerdict : std::uint32_t
{
    Produced            = 0u,   // [-] - the sketch was changed
    SubjectMissing      = 1u,   // [-] - the named curve does not resolve
    UnsupportedGeometry = 2u,   // [-] - the curve is not a line
    PointNotOnCurve     = 3u,   // [-] - the cut point is not on the curve being cut
    PointAtEnd          = 4u,   // [-] - cutting at an endpoint would produce a zero-length piece
    NoIntersection      = 5u,   // [-] - extend found nothing to reach, or trim found no bounds
    DistanceNotPositive = 6u,   // [-] - an offset of zero, which is a copy of the original
    WouldCollapse       = 7u    // [-] - the offset is large enough to invert the shape
};

//------------------------------------------------------------------------------------------------------------------------
//                                                           CUT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Divides one curve at a point, producing two curves that meet there.
/// in    Subject   [-] the curve to divide
/// in    Position  [-] where to divide it; must lie on the curve
/// out   Leading   [-] the piece from the original origin to the point -- this is `Subject`, shortened
/// out   Trailing  [-] the newly declared piece, from the point to the original terminus
/// note  🔴 `Subject` SURVIVES AS THE LEADING PIECE rather than being retired and replaced by two new
///        curves. Every loop traversal, constraint and selection that names it keeps working, and a
///        caller that wants the loop to stay closed only has to insert `Trailing` after it.
/// note  🔴 NOTHING IS REMOVED. This is the whole difference from Trim. A closed loop cut anywhere is
///        still a closed loop -- more edges, same boundary -- so its fill is unaffected. That follows
///        from the geometry and needs no special case anywhere.
/// note  ⚠️ Refuses at either endpoint. A cut there would declare a zero-length curve, which every
///        downstream measure would then have to defend itself against.
/// cost  🚩
/// tag   api, nonthrowing
OperationVerdict CutWorldCurve(WorldSketchStructure& Declared,
                               WorldCurveName Subject,
                               const SpatialPoint& Position,
                               WorldCurveName& Leading,
                               WorldCurveName& Trailing);

/// 🧩 Divides a curve wherever another curve crosses it, in one action.
/// out   Produced  [-] every new curve declared, in order along the subject
/// note  📝 The bulk form of Cut, and what makes Trim pleasant: an artist should not have to place cut
///        points by hand before trimming an intersection they can already see.
/// cost  🚩🚩
/// tag   api, nonthrowing
OperationVerdict CutWorldCurveAtCrossings(WorldSketchStructure& Declared,
                                          WorldCurveName Subject,
                                          std::vector<WorldCurveName>& Produced);

//------------------------------------------------------------------------------------------------------------------------
//                                                          TRIM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Removes the piece of a curve the probe sits in -- bounded by the crossings either side of it, or,
///    when nothing crosses, the whole edge.
/// in    Probe   [-] a point on the piece the artist wants gone
/// note  🔴 THE ARTIST POINTS AT WHAT THEY WANT REMOVED, not at the bounds. Trim's whole appeal is that
///        the bounds are found for you: the nearest crossing on each side of the probe. Asking for them
///        would make it a two-click operation and no better than cutting twice and deleting.
/// note  📝 With a crossing on only one side, the piece from the probe to the free END is removed --
///        which is what an artist means by trimming an overhang.
/// note  🔴 WITH NOTHING CROSSING IT, THE WHOLE EDGE IS THE PIECE, AND IT GOES. An ordinary shape's side
///        meets its neighbours only at its own ends -- junctions, not interior crossings -- so the one
///        piece the probe can sit in is the entire edge. This once refused, calling a whole-edge removal
///        a deletion rather than a trim; that refusal is exactly why Trim did nothing on a drawn shape,
///        where every edge is uncrossed. The edge is retired in place: its geometry is cleared and its
///        1-based index kept, so every loop, constraint, dimension and mapping that names a later curve
///        is undisturbed, and a loop that used the edge simply reports itself open and stops filling.
/// note  📝 The whole-edge removal is the one trim that is not line geometry -- an arc or a circle is
///        taken out the same way, where the crossing-bounded partial trim below is straight-line only.
/// note  🔴 A TRIM THROUGH THE MIDDLE LEAVES TWO PIECES. `Remaining` carries both, because a curve
///        trimmed in its interior is not one shortened curve -- assuming it is loses the far piece. A
///        whole-edge removal leaves `Remaining` empty: nothing of the edge is kept.
/// cost  🚩🚩
/// tag   api, nonthrowing
OperationVerdict TrimWorldCurve(WorldSketchStructure& Declared,
                                WorldCurveName Subject,
                                const SpatialPoint& Probe,
                                std::vector<WorldCurveName>& Remaining);

/// 🧩 Which span of a curve a trim would remove, without changing anything.
/// out   DepartingFrom  [-] one end of the piece that would go
/// out   DepartingTo    [-] the other end of it
/// note  🔴 THE PREVIEW THE ARTIST NEEDS BEFORE COMMITTING. Trim finds its own bounds -- the nearest
///        crossing either side of the probe -- so until the piece is drawn the artist is guessing which
///        of several segments a click will delete, and a wrong guess is destructive. It answers with the
///        SAME crossing search `TrimWorldCurve` uses, so the highlight is the geometry that will go
///        rather than a second opinion about it.
/// note  📝 Refuses for exactly the reasons the trim itself refuses, so a span is only reported when a
///        trim would actually succeed.
/// tag   api, nonthrowing
OperationVerdict EvaluateWorldTrim(const WorldSketchStructure& Declared,
                                   WorldCurveName Subject,
                                   const SpatialPoint& Probe,
                                   SpatialPoint& DepartingFrom,
                                   SpatialPoint& DepartingTo);

/// 🧩 Where a cut would divide a curve, without changing anything.
/// out   Division  [-] the point on the curve the artist's click snaps to
/// note  🔴 SNAPPED ONTO THE CURVE, exactly as the cut snaps it. Drawing the raw probe would put the
///        marker beside the line it claims to be cutting, and at the ends it would promise a cut the
///        operation refuses.
/// tag   api, nonthrowing
OperationVerdict EvaluateWorldCut(const WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialPoint& Probe,
                                  SpatialPoint& Division);

//------------------------------------------------------------------------------------------------------------------------
//                                                         EXTEND
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Grows one end of a curve along its own direction until it meets another curve.
/// in    Probe   [-] a point near the end to grow; the nearer end is the one that moves
/// note  🔴 THE NEAREST CROSSING, NOT THE FURTHEST. When the extended ray would cross several curves it
///        stops at the first, which is what "extend to meet" means and what every CAD package does.
/// note  🔴 REFUSES WHEN THERE IS NOTHING TO MEET, rather than extending by some default length. A tool
///        that invents a distance produces geometry the artist did not ask for and cannot predict.
/// note  📝 The far end and the direction are untouched. Only the named end travels.
/// cost  🚩🚩
/// tag   api, nonthrowing
OperationVerdict ExtendWorldCurve(WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialPoint& Probe);

/// 🧩 Whether an extend would succeed, and where the end would land, without changing anything.
/// note  📝 The dry run the preview draws, so the dashed line the artist sees is the geometry that will
///        be committed rather than an approximation of it.
/// tag   api, nonthrowing
OperationVerdict EvaluateWorldExtend(const WorldSketchStructure& Declared,
                                     WorldCurveName Subject,
                                     const SpatialPoint& Probe,
                                     SpatialPoint& Landing);

//------------------------------------------------------------------------------------------------------------------------
//                                                         OFFSET
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A parallel copy of a chain of curves, at a fixed distance, in the plane they lie in.
/// in    Chain     [-] the curves to copy, in order; they must meet end to end
/// in    Frame     [-] the plane to offset within; its normal decides which way is positive
/// in    Distance  [-] signed -- positive is towards the frame's `Across`-left side, negative the other
/// out   Produced  [-] one new curve per input curve, in the same order
/// note  🔴 THE COPY'S CORNERS ARE WHERE THE OFFSET LEGS INTERSECT, not where the original corner was
///        pushed sideways. Pushing each corner along its own normal leaves gaps on the outside of a bend
///        and overlaps on the inside, and the distance stops being constant -- which is the one property
///        an offset has.
/// note  ⚠️ Refuses rather than producing a self-intersecting result when an inward offset is large
///        enough to collapse an edge. Cleaning up a self-intersecting offset is a much larger piece of
///        work; refusing at the point of collapse is honest, and the caller clamps the drag there.
/// note  📝 The original is never modified. An offset is a new curve, always.
/// cost  🚩🚩
/// tag   api, nonthrowing
OperationVerdict OffsetWorldChain(WorldSketchStructure& Declared,
                                  const std::vector<WorldCurveName>& Chain,
                                  const WorldPlacementFrame& Frame,
                                  double Distance,
                                  std::vector<WorldCurveName>& Produced);

/// 🧩 The largest inward offset a chain accepts before an edge collapses.
/// note  🔴 THE CLAMP THE DRAG NEEDS, exactly as `ResolveCornerLimit` is for the fillet. Without it the
///        drag refuses somewhere the artist cannot see rather than visibly stopping.
/// tag   api, nonthrowing
double ResolveOffsetLimit(const WorldSketchStructure& Declared,
                          const std::vector<WorldCurveName>& Chain,
                          const WorldPlacementFrame& Frame);

} // namespace Slate
