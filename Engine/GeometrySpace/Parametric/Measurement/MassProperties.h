/*====================================================================================================================================
                                                        MASSPROPERTIES.H
====================================================================================================================================*/
// 🧩 Exact body and face measures: volume, surface area, centroid and inertia tensor by Green / divergence integrals

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <array>
#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Unit density. Inertia = ∭ (|r|² E − r rᵀ) dV about the centroid: Ixx = ∭ (𝑦² + 𝑧²) dV, Ixy = −∭ 𝑥𝑦 dV.
struct BodyMassProperties
{
    double                Volume      = 0.0;                                                 // [len³] - Solid regions only
    double                SurfaceArea = 0.0;                                                 // [len²] - Faces of the solid shells
    Vector3               Centroid    = {};                                                  // [len]  - Centre of mass
    std::array<double, 6> Inertia     = {};                                                  // [len⁵] - Ixx Iyy Izz Ixy Iyz Izx
    std::string           Refusal     = {};                                                  // [-]    - Empty on success
};

struct FaceAreaMeasure
{
    double  Area     = 0.0;                                                                  // [len²] - True surface area
    Vector3 Centroid = {};                                                                   // [len]  - Area centroid (may lie off the face)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 One divergence-theorem flux per moment over every solid shell, positions taken relative to the mean vertex (so a
//    body far from the origin keeps its digits): 1 volume, 3 first and 6 second moments, then the parallel-axis shift
//    to the centroid. Refused for a body without solid regions (a sheet) or a non-positive volume.
[[nodiscard]] BodyMassProperties MeasureBodyMass(const ModelPartition& Partition, BodyToken Body);

// Area and area centroid of one trimmed face (IntegrateFaceSurface with weights 1, 𝑥, 𝑦, 𝑧).
[[nodiscard]] FaceAreaMeasure MeasureFaceArea(const ModelPartition& Partition, FaceToken Face);

// Principal moments (ascending) of a symmetric inertia tensor Ixx Iyy Izz Ixy Iyz Izx (Jacobi rotations).
[[nodiscard]] std::array<double, 3> ResolvePrincipalMoments(const std::array<double, 6>& Inertia);

} // namespace Frontier::GeometrySpace::Parametric
