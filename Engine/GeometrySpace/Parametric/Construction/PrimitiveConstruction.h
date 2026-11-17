/*====================================================================================================================================
                                                    PRIMITIVECONSTRUCTION.H
====================================================================================================================================*/
// 🧩 Solid primitives assembled purely from Euler edits, then given exact analytic geometry and SP-curves

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/EulerEdits.h"

#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Faces of a translational sweep, for callers that keep building on it (holes, cavities).
struct PrismAssembly
{
    BodyToken              Body       = {};                                                  // [-] - Swept body
    FaceToken              StartFace  = {};                                                  // [-] - Cap at the profile
    FaceToken              EndFace    = {};                                                  // [-] - Cap at the displaced profile
    std::vector<FaceToken> SideFaces  = {};                                                  // [-] - One per profile edge
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Prism over a planar polygon (counter-clockwise about Displacement) swept by Displacement. Lines and planes throughout.
[[nodiscard]] EditOutcome<PrismAssembly> ConstructPrism(ModelPartition& Partition, const std::vector<Vector3>& Profile, Vector3 Displacement);

[[nodiscard]] EditOutcome<BodyToken> ConstructBlock(ModelPartition& Partition, Vector3 Corner, Vector3 Extent);

// Cylinder on Frame.ZAxis: base circle centred at Frame.Origin, seam through Origin + Radius·XAxis.
[[nodiscard]] EditOutcome<BodyToken> ConstructCylinder(ModelPartition& Partition, const OrthonormalFrame& Frame, double Radius, double Height);

// Pointed cone: base circle at Frame.Origin, apex at Origin + Height·ZAxis.
[[nodiscard]] EditOutcome<BodyToken> ConstructCone(ModelPartition& Partition, const OrthonormalFrame& Frame, double Radius, double Height);

// Truncated cone: circle of OriginRadius at Frame.Origin, top circle of TopRadius at Origin + Height·ZAxis, seam through +XAxis.
[[nodiscard]] EditOutcome<BodyToken> ConstructFrustum(ModelPartition& Partition, const OrthonormalFrame& Frame, double OriginRadius, double TopRadius, double Height);

// Sphere: one face, one seam edge from the south pole to the north pole, singular rows at the poles.
[[nodiscard]] EditOutcome<BodyToken> ConstructSphere(ModelPartition& Partition, const OrthonormalFrame& Frame, double Radius);

// Torus: one face, one vertex, two seam circles (genus 1).
[[nodiscard]] EditOutcome<BodyToken> ConstructTorus(ModelPartition& Partition, const OrthonormalFrame& Frame, double MajorRadius, double MinorRadius);

// Block with a rectangular through hole along Z (genus 1, made with KEMR + KFMRH).
[[nodiscard]] EditOutcome<BodyToken> ConstructPerforatedBlock(ModelPartition& Partition, Vector3 Extent, Vector3 HoleExtent);

// Block with an internal block cavity (one solid region bounded by two shells).
[[nodiscard]] EditOutcome<BodyToken> ConstructHollowBlock(ModelPartition& Partition, Vector3 Extent, Vector3 CavityExtent);

} // namespace Frontier::GeometrySpace::Parametric
