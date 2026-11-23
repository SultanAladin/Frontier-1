//============================================================================================================================================
//                                                              CURVESOLVER.H
//============================================================================================================================================
// 🧩 Planar path evaluation, flattening to a tolerance, and stroke offsetting — the mechanism `52` resolves with.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PLANAR POSITIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One position in a planar path's own space.
/// note  Held at 64-bit because `PlanarClassifier` classifies the flattened result exactly, and an exact
///        predicate over narrowed positions is exact about the wrong positions.
/// tag   nonallocating, nonthrowing
struct PlanarPosition
{
    double  PositionX = 0.0;   // [-] - in the path's own space
    double  PositionY = 0.0;   // [-] - in the path's own space
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE SEGMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a path segment's geometry is, which fixes which control positions below are read.
/// note  ⚠️ `52` §2's accepted subset and nothing beyond it. A construct outside this enumeration is rejected at
///        intake with its position named, never approximated by the nearest member.
/// tag   guarantee
enum class SegmentSubject : std::uint32_t
{
    Line         = 0u,   // [-] - straight to the terminus
    Quadratic    = 1u,   // [-] - one control position
    Cubic        = 2u,   // [-] - two control positions
    Arc          = 3u,   // [-] - elliptical, by endpoint parameterisation
    SegmentCount = 4u    // [-] - the closed count, never a segment
};

/// 🧩 One segment of a planar path, continuing from wherever the preceding segment ended.
/// note  📝 Held as a continuation rather than as an independent curve, so a path cannot express a gap between
///        two segments that the fill rule would then have to guess how to close.
/// tag   nonallocating, nonthrowing
struct PathSegment
{
    SegmentSubject  Subject         = SegmentSubject::Line;   // [-]   - which controls below are read
    PlanarPosition  Terminus        = {};                     // [-]   - where the segment ends
    PlanarPosition  FirstControl    = {};                     // [-]   - Quadratic and Cubic
    PlanarPosition  SecondControl   = {};                     // [-]   - Cubic
    double          RadiusX     = 0.0;                    // [-]   - Arc; along the arc's own abscissa
    double          RadiusY    = 0.0;                    // [-]   - Arc; along its coordinate
    double          Rotation        = 0.0;                    // [deg] - Arc; rotation of its own axes
    bool            LargeArcEnabled = false;                  // [-]   - Arc; take the greater sweep
    bool            SweepEnabled    = false;                  // [-]   - Arc; sweep in increasing angle
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     FLATTENING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Appends the flattened polyline of one segment, excluding its origin and including its terminus.
/// in    Origin     [-]  where the segment begins — the preceding terminus
/// in    Segment    [-]  the segment to flatten
/// in    Tolerance  [-]  greatest permitted deviation, in the path's own space
/// out   Appending  [-]  positions appended in traversal order
/// note  🔴 Tolerance is **resolution-relative** and is supplied by the caller, never fixed here. `70` resolves
///        an outline at whatever reduction level a tile was promoted to, so a fixed tolerance is either wasteful
///        at coarse levels or visibly polygonal at fine ones — `52` §4.
/// note  📐 Subdivision is adaptive: a segment is halved while its control positions deviate from the chord by
///        more than the tolerance. Uniform subdivision at a fixed count is either wrong on a tight curve or
///        wasteful on a slack one, and a path carries both.
/// cost  🚩
/// tag   api, nonthrowing
void Flatten(PlanarPosition                Origin,
             const PathSegment&            Segment,
             double                        Tolerance,
             std::vector<PlanarPosition>&  Appending);

/// 🧩 Flattens a whole ordered run of segments into one polyline.
/// in    Origin    [-]  where the run begins
/// in    Segments  [-]  the run, in traversal order
/// out   Flattened [-]  the origin followed by every appended position
/// cost  🚩
/// tag   api, nonthrowing
std::vector<PlanarPosition> Flatten(PlanarPosition                  Origin,
                                    const std::vector<PathSegment>& Segments,
                                    double                          Tolerance);

//------------------------------------------------------------------------------------------------------------------------
//                                                     OFFSETTING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Converts a flattened polyline and a half-width into the closed outline of its stroke.
/// in    Traversed   [-]  the flattened polyline, in traversal order
/// in    HalfWidth   [-]  half the stroke width, in the path's own space
/// in    ClosedRun   [-]  whether the polyline closes back on its origin
/// out   Result     [-]  refuses with ContentUnsupported for a non-positive half-width, and with
///                        ExtentExhausted for a polyline of fewer than two positions
/// note  🔴 `52` §2 converts strokes at **intake** rather than storing a width. A stroke width is a distance in
///        the source's own space and a placement scales that space, so a stored width thins when the placement
///        shrinks — correct for a drawing program and wrong for content placed onto a surface at a chosen size.
/// note  🚧 Joins are bevelled and both terminals are butt. `52` §6 carries no row for cap and join declarations,
///        so declaring one here would be inventing an authored property the artist cannot see.
/// cost  🚩
/// tag   api, nonthrowing
Deliver<std::vector<PlanarPosition>> OffsetOutline(const std::vector<PlanarPosition>& Traversed,
                                                   double                             HalfWidth,
                                                   bool                               ClosedRun);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
