//============================================================================================================================================
//                                                 WORLDSKETCHDIMENSIONGEOMETRY.H
//============================================================================================================================================
// 🧩 What a dimension LOOKS like — the witness lines, the dimension line, the arrows and where the figure
//    sits — recomputed from the live geometry every single time it is asked.
//
// 🔴 A DIMENSION STORES NO COORDINATES, AND THAT IS THE WHOLE DESIGN. It holds a reference to what it
//    measures and an offset saying how far to the side to draw. Everything else on screen is derived
//    here, from scratch, from the geometry as it stands right now. So a dimension CANNOT go stale and
//    CANNOT drift off the thing it measures: resize the shape and the dimension follows, because there
//    was never a stored position to become wrong. The alternative -- caching the endpoints when the
//    dimension is placed -- is the classic annotation bug, and it is unfixable once the cache exists.
//
// 🔴 THE OFFSET'S SIGN IS THE SIDE IT DRAWS ON. It is a projection onto the edge's perpendicular, so
//    dragging across the edge makes it negative and the whole dimension flips over. Nothing branches on
//    which side the pointer is; the arithmetic already knows.
//
// 📝 Pure geometry, in millimetres, in world space. No camera, no pixels, no text formatting -- the
//    caller projects and draws. That is what makes every claim below provable without a device.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS DRAWN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How a dimension is drawn, which follows from what it measures.
/// note  📝 Three kinds, not six. The six `WorldDimensionSubject` values collapse to three shapes on
///        screen: everything linear draws the same way whatever axis it measures along.
enum class DimensionDrawing : std::uint32_t
{
    Linear   = 0u,   // [-] - two witness lines, a dimension line between them, arrows turned inward
    Diameter = 1u,   // [-] - a full chord through the centre, prefixed ⌀
    Radial   = 2u,   // [-] - a leader from the centre to the rim, prefixed R
    Angular  = 3u    // [-] - an arc between two edges, with the angle at its middle
};

/// 🧩 Everything needed to draw one dimension, all of it derived.
struct DimensionGeometry
{
    DimensionDrawing Drawing = DimensionDrawing::Linear;

    /// 🧩 The measured span itself, on the geometry.
    SpatialPoint MeasuredStart = {};
    SpatialPoint MeasuredEnd   = {};

    /// 🧩 The dimension line, offset to the side by `Offset`.
    SpatialPoint LineStart = {};
    SpatialPoint LineEnd   = {};

    /// 🧩 Where the figure is written, and which way is "along" for laying it out.
    SpatialPoint     TextAt    = {};
    SpatialDirection TextAlong = {};

    /// 🧩 The direction each arrowhead points, already turned to face inward.
    SpatialDirection ArrowStart = {};
    SpatialDirection ArrowEnd   = {};

    /// 🧩 Whether the arrows had to be turned outward because the span is too short to hold them.
    /// note  📝 A dimension narrower than its own arrowheads is common and must still read clearly, so
    ///        the arrows flip to the outside rather than overlapping into an unreadable blob.
    bool ArrowsOutward = false;

    /// 🧩 The value the figure shows, in millimetres, measured live.
    double Measured = 0.0;

    /// 🧩 The plane the whole thing is drawn in.
    WorldPlacementFrame Frame = {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How long an arrowhead is, in millimetres.
constexpr double DimensionArrowReach = 3.0;

/// 🧩 How far past the dimension line a witness line runs on.
constexpr double DimensionWitnessOvershoot = 1.5;

/// 🧩 Derives everything drawable about a dimension from the geometry it names.
/// out   Resolved  [-] the witness lines, dimension line, arrows and figure position
/// note  🔴 NOTHING HERE IS READ FROM THE DIMENSION EXCEPT ITS REFERENCE AND ITS OFFSET. If this function
///        ever reads a stored coordinate, the guarantee that dimensions track their geometry is gone.
/// note  ⚠️ Refuses when the referenced geometry is absent -- a dimension whose subject was deleted has
///        nothing to measure, and drawing it at the origin would be worse than not drawing it.
/// cost  🚩
/// tag   api, nonthrowing
Deliver<DimensionGeometry> ResolveDimensionGeometry(const WorldSketchStructure& Declared,
                                                    WorldDimensionName Subject);

/// 🧩 The offset a pointer at `Probe` is asking for, as a signed projection.
/// 🔴 THIS IS WHY DIMENSIONS FLIP SIDES BY THEMSELVES. The offset is the pointer's distance from the
///    measured span along that span's perpendicular, WITH SIGN. Cross the edge and it goes negative; the
///    dimension draws on the other side because the number says so, not because anything checked.
/// tag   api, nonthrowing
Deliver<double> ResolveDimensionOffsetFor(const WorldSketchStructure& Declared,
                                          WorldDimensionName Subject,
                                          const SpatialPoint& Probe);

/// 🧩 The angle around a circle a pointer is asking for, for a radial or diameter dimension.
/// note  📝 Round dimensions are placed in polar terms -- an angle around the centre, and a stand-off
///        from the rim -- because that is how they are dragged. Storing a cartesian offset would make
///        the drag lossy the moment the circle was resized.
/// tag   api, nonthrowing
Deliver<double> ResolveDimensionAngleFor(const WorldSketchStructure& Declared,
                                         WorldDimensionName Subject,
                                         const SpatialPoint& Probe);

} // namespace Slate
