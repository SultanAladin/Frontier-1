/*====================================================================================================================================
                                                         FACEDOMAIN.H
====================================================================================================================================*/
// 🧩 Trimmed face domains in (𝑢, 𝑣): SP-curve loops, exact 2D winding number, point-in-face, interior point location

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class PointMembership : uint8_t
{
    Outside  = 0,                                                                            // Strictly outside (beyond precision)
    Inside   = 1,                                                                            // Strictly inside (beyond precision)
    Boundary = 2,                                                                            // Within precision of the boundary
};

// 📝 One traversal of an SP-curve from τ = Start to τ = End (End < Start for a reversed fin). A segment without an edge
//    is a virtual closure along a singular row (sphere pole, cone apex): it maps to one point in 3D.
struct DomainSegment
{
    ParameterCurve Curve   = ParameterLine{};                                                // [-] - SP-curve
    double         Start   = 0.0;                                                            // [-] - τ at the segment start
    double         End     = 1.0;                                                            // [-] - τ at the segment end
    bool           Virtual = false;                                                          // [-] - Singular-row closure
};

// 📝 3D boundary used for the precision test (on-boundary is decided in space, not in the chart).
struct DomainBoundaryCurve
{
    CurveGeometry Curve = StraightLine{};                                                    // [-] - Edge carrier
    double        Low   = 0.0;                                                               // [-] - Edge interval
    double        High  = 1.0;                                                               // [-] - Edge interval
};

struct FaceDomain
{
    FaceToken                               Face           = {};                             // [-]   - Source face (absent for split faces)
    SurfaceGeometry                         Surface        = PlaneSurface{};                 // [-]   - Carrier
    Orientation                             Sense          = Orientation::Forward;           // [-]   - Outward vs surface normal
    std::vector<std::vector<DomainSegment>> Loops          = {};                             // [-]   - Closed chart loops
    std::vector<DomainBoundaryCurve>        BoundaryCurves = {};                             // [-]   - 3D edges
    std::vector<Vector3>                    BoundaryPoints = {};                             // [-]   - 3D vertices
    ParameterWindow                         Window         = {};                             // [-]   - Chart bounding box of the loops
    Vector3                                 BoundsLow      = {};                             // [len] - Conservative 3D box
    Vector3                                 BoundsHigh     = {};                             // [len] - Conservative 3D box
};

struct FacePositionClass
{
    PointMembership Membership      = PointMembership::Outside;                              // [-]   - Result
    Vector2         Parameter       = {};                                                    // [-]   - Foot on the chart
    double          SurfaceDistance = 0.0;                                                   // [len] - |S(foot) − P|
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Loops of a partition face (virtual closures inserted where consecutive SP-curves do not meet), plus window and bounds.
[[nodiscard]] FaceDomain DescribeFaceDomain(const ModelPartition& Partition, FaceToken Face);

// Window and 3D bounds from the loops (callers that assemble loops themselves, e.g. split faces).
void CompleteFaceDomain(FaceDomain& Domain);

// 💡 Σ swept angle / 2π of the loops about Parameter. Lines are exact; curves are bisected until the two halves agree
//    and each piece turns < 1 rad, so the result is an integer to rounding for any point off the loops.
[[nodiscard]] double MeasureParameterWinding(const FaceDomain& Domain, Vector2 Parameter);

// Inside / Outside in the chart, trying every periodic image of Parameter that falls in the window.
[[nodiscard]] PointMembership ClassifyParameterPoint(const FaceDomain& Domain, Vector2 Parameter);

// 3D point against a face: Boundary within Precision of an edge or vertex, Outside beyond Precision of the surface,
// otherwise the chart classification of the foot.
[[nodiscard]] FacePositionClass ClassifyFacePosition(const FaceDomain& Domain, Vector3 Position, double Precision);

// 💡 Interior point with the largest clearance: midpoints of scanline intervals in 𝑢 and 𝑣, scored by the smaller 3D
//    half-width. Empty when no interval is found.
[[nodiscard]] std::optional<Vector2> LocateFaceInterior(const FaceDomain& Domain);

// Sampled polyline of one loop (Samples per curved segment, 2 per line).
[[nodiscard]] std::vector<Vector2> SampleDomainLoop(const std::vector<DomainSegment>& Loop, int Samples);

// Signed chart area of one loop (shoelace of the sampled polyline).
[[nodiscard]] double MeasureLoopArea(const std::vector<DomainSegment>& Loop);

} // namespace Frontier::GeometrySpace::Parametric
