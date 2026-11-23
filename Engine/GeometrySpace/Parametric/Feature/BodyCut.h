/*====================================================================================================================================
                                                           BODYCUT.H
====================================================================================================================================*/
// 🧩 Cut (Plasticity C): a solid or sheet split by a sketch profile or an open curve swept through it, into two bodies

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"

#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The two pieces of a cut. The tool is the cut profile swept along the plane normal from −2·Reach to +2·Reach, so
//    it passes through the whole body. Inside = body ∩ tool (closed profile: within it; open curve: on its left);
//    Outside = body − tool. Sheets keep only their own faces: no tool caps, so the pieces stay open sheets.
struct BodyCutOutcome
{
    SketchProfile  Profile        = {};                                                      // [-]   - Tool profile on the plane
    SweepOutcome   Tool           = {};                                                      // [-]   - Prism through the body
    BooleanOutcome Inside         = {};                                                      // [-]   - Piece within / left of the cut
    BooleanOutcome Outside        = {};                                                      // [-]   - The rest
    double         Reach          = 0.0;                                                     // [len] - Body radius about the plane origin
    bool           SheetCondition = false;                                                   // [-]   - The body is a sheet
    std::string    Refusal        = {};                                                      // [-]   - "Cut: …" when refused
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Radius about Plane.Origin that holds the whole body (coarse tessellation, padded for chordal sag).
[[nodiscard]] double MeasureCutReach(const ModelPartition& Partition, BodyToken Body, const OrthonormalBasis& Plane);

// 💡 Open chain (segments in traversal order, end to start joined) closed on its LEFT side: tangent extensions past
//    the reach, then a far box. The result is the profile of a half-plane bounded by the chain within the reach;
//    refused (Profile.Refusal) when the closing box crosses the chain.
[[nodiscard]] SketchProfile CloseCutChain(std::vector<ProfileSegment> Chain, double Reach);

// Cut profile of a solved sketch: its closed profile when it has one, otherwise its single open chain closed on the
// left (chained from the start of its first curve).
[[nodiscard]] SketchProfile BuildSketchCutProfile(const SketchModel& Sketch, double Reach);

// SpaceCurve projected along Plane.ZAxis onto the plane: the poles project exactly (parallel projection is affine), so
// a polynomial NURBS stays exact. Rational or periodic curves are refused (empty).
[[nodiscard]] std::optional<ProfileSegment> ProjectCutCurve(const NurbsCurve& Curve, const OrthonormalBasis& Plane, const std::string& Name);

// Cuts Body with Profile (plane coordinates of Plane). Reach ≤ 0 → MeasureCutReach.
[[nodiscard]] BodyCutOutcome CutBody(ModelPartition& Partition, BodyToken Body, const SketchProfile& Profile, const OrthonormalBasis& Plane, double Reach = 0.0);

} // namespace Frontier::GeometrySpace::Parametric
