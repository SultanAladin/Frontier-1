/*====================================================================================================================================
                                                         PLANESECTION.H
====================================================================================================================================*/
// 🧩 Exact plane section of a solid: the cut region's area, perimeter, centroid, second moments and outline

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <array>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Plane coordinates: 𝑢 along Plane.XAxis, 𝑣 along Plane.YAxis. Second moments of area about the centroid:
//    Iuu = ∬ (𝑣 − 𝑣̄)² dA (bending about the 𝑢 axis), Ivv = ∬ (𝑢 − 𝑢̄)² dA, Iuv = ∬ (𝑢 − 𝑢̄)(𝑣 − 𝑣̄) dA.
struct PlaneSectionMeasure
{
    double                            Area          = 0.0;                                   // [len²] - Region area
    double                            Perimeter     = 0.0;                                   // [len]  - Σ boundary edge lengths
    Vector3                           Centroid      = {};                                    // [len]  - Area centroid (world)
    std::array<double, 3>             SecondMoments = {};                                    // [len⁴] - Iuu Ivv Iuv
    int                               Regions       = 0;                                     // [-]    - Cap faces (disjoint pieces)
    int                               Loops         = 0;                                     // [-]    - Outer loops + holes
    std::vector<std::vector<Vector3>> Outline       = {};                                    // [len]  - One polyline per boundary edge
    double                            Milliseconds  = 0.0;                                   // [ms]   - Cut + integrals
    std::string                       Refusal       = {};                                    // [-]    - Empty on success
};

// 📝 The kept piece of a half-space cut and its cap faces (planar, on the plane), in the partition that was cut.
struct HalfSpaceCut
{
    BodyToken              Body    = {};                                                     // [-]   - Piece on the +Plane.ZAxis side
    std::vector<FaceToken> Caps    = {};                                                     // [-]   - Its faces lying on the plane
    double                 Reach   = 0.0;                                                    // [len] - Body radius about the plane origin
    std::string            Refusal = {};                                                     // [-]   - "Section: …" when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 The body is cut on a scratch copy of the partition by the half-space on the +Plane.ZAxis side (CutBody with one
//    straight chain on the plane through Plane.XAxis, closed on its left), so every section curve is the exact
//    General Fuse intersection. The kept piece's planar faces lying on the plane are the section: their areas and
//    moments come from IntegrateFaceSurface, the perimeter from MeasureEdgeLength. Refused for sheets, a plane that
//    misses the body or touches it only along a face (no area), or when the cut is refused. OutlineStep bounds the
//    chord of the outline polylines (0 → 1 % of the body reach).
// 💡 Cuts Body in Partition (callers pass a scratch copy: the partition gains the tool and both pieces) by the half-space on
//    the +Plane.ZAxis side and finds the cap faces. Shared by SectionBody and the drawing section views.
[[nodiscard]] HalfSpaceCut CutBodyAtPlane(ModelPartition& Partition, BodyToken Body, const OrthonormalBasis& Plane);

[[nodiscard]] PlaneSectionMeasure SectionBody(const ModelPartition& Partition, BodyToken Body, const OrthonormalBasis& Plane, double OutlineStep = 0.0);

} // namespace Frontier::GeometrySpace::Parametric
