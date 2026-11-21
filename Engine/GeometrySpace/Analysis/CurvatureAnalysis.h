/*============================================================================================================================================
                                                             CURVATUREANALYSIS.H
============================================================================================================================================*/
// 🧩 Discrete Meyer cotangent mean & angle-deficit Gaussian curvature plus Mikkelsen/Duff continuous corner tangent bases

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VertexCurvatureEntry
{
    float               MixedVoronoiAreaSquareMeters = 0.0f; // [m^2]   - Meyer mixed Voronoi / obtuse-barycentric vertex area
    float               MeanCurvaturePerMeter        = 0.0f; // [1/m]   - Signed cotangent Laplace-Beltrami mean curvature H
    float               GaussianCurvaturePerSqMeter  = 0.0f; // [1/m^2] - Angle-deficit Gaussian curvature K
    float               PrincipalMaximumPerMeter     = 0.0f; // [1/m]   - First principal curvature kappa_1 = H + sqrt(H^2 - K)
    float               PrincipalMinimumPerMeter     = 0.0f; // [1/m]   - Second principal curvature kappa_2 = H - sqrt(H^2 - K)
    float               CavityConcavityFactor        = 0.5f; // [0-1]   - 0.0 = concave crease, 0.5 = flat, 1.0 = convex ridge
    Mathematics::Axis3D MeanCurvatureNormal          {};     // [1/m]   - Unnormalized Laplace-Beltrami curvature vector
};

struct CurvatureAnalysisSummary
{
    std::vector<VertexCurvatureEntry> PerVertexMetrics           {};   // [-]     - One differential entry per PolygonNetwork vertex
    float                             TotalMixedAreaSquareMeters = 0.0f; // [m^2] - Sum of mixed Voronoi areas across all vertices
    float                             AreaWeightedMeanCurvature  = 0.0f; // [1/m] - Area-weighted |H| average
    float                             MaximumAbsMeanCurvature    = 0.0f; // [1/m] - Peak |H| across surface
    float                             IntegratedGaussianRadians  = 0.0f; // [rad] - Integral of K dA (equals 2*pi*chi on closed surfaces)
    float                             GaussBonnetEulerEstimate   = 0.0f; // [-]   - IntegratedGaussianRadians / (2*pi)
};

struct CornerTangentBasis
{
    Mathematics::Axis3D TangentAxis    { 1.0f, 0.0f, 0.0f }; // [-] - Unit surface tangent aligned with +dP/dU
    Mathematics::Axis3D NormalAxis     { 0.0f, 1.0f, 0.0f }; // [-] - Crease-aware smooth unit corner normal
    float               HandednessSign = 1.0f;               // [-] - +1.0 or -1.0; Bitangent = HandednessSign * (Normal x Tangent)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Duff et al. (JCGT 2017) branchless orthonormal basis around a unit normal vector.
void ConstructDuffOrthonormalBasis(
    Mathematics::Axis3D  UnitNormal,
    Mathematics::Axis3D& OutTangent,
    Mathematics::Axis3D& OutBitangent) noexcept;

// Evaluates Meyer et al. (2003) discrete mean, Gaussian, and principal curvatures across Network.
[[nodiscard]] CurvatureAnalysisSummary EvaluateSurfaceCurvature(const PolygonNetwork& Network);

// Evaluates Mikkelsen-compatible UV-Jacobian corner tangent bases (one per DirectedEdge), respecting seams & sharp creases.
[[nodiscard]] std::vector<CornerTangentBasis> EvaluateContinuousCornerTangents(const PolygonNetwork& Network);

// Evaluates reconstructed unit bitangent from a CornerTangentBasis.
[[nodiscard]] Mathematics::Axis3D ReconstructCornerBitangent(const CornerTangentBasis& Basis) noexcept;

// Packs/unpacks a unit 3D direction into a 32-bit SNORM16x2 octahedral encoding for compact GPU cluster transport.
[[nodiscard]] uint32_t            PackOctahedralDirection32(Mathematics::Axis3D UnitDirection) noexcept;
[[nodiscard]] Mathematics::Axis3D UnpackOctahedralDirection32(uint32_t PackedWord) noexcept;

} // namespace Frontier::GeometrySpace
