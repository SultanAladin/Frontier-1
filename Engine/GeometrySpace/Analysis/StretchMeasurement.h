/*============================================================================================================================================
                                                             STRETCHMEASUREMENT.H
============================================================================================================================================*/
// 🧩 Exact 2x2 UV-Jacobian singular value decomposition, Sander L2/L-infinity stretch, and Symmetric Dirichlet distortion metrics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FaceStretchMetrics
{
    float SingularMaxSigma         = 1.0f;  // [m/uv] - Largest singular value sigma_1 of dP/dUV Jacobian
    float SingularMinSigma         = 1.0f;  // [m/uv] - Smallest singular value sigma_2 of dP/dUV Jacobian
    float SanderL2Stretch          = 1.0f;  // [-]    - Normalized Sander L2 stretch (>= 1.0; 1.0 = isometric)
    float SymmetricDirichletEnergy = 2.0f;  // [-]    - 0.5 * (s1^2 + s1^-2 + s2^2 + s2^-2) (>= 2.0; 2.0 = isometric)
    float QuasiConformalRatio      = 1.0f;  // [-]    - sigma_1 / sigma_2 (>= 1.0; 1.0 = zero angle distortion)
    float AuthalicAreaRatio        = 1.0f;  // [-]    - Normalized UV-to-3D area ratio (1.0 = equal texel density)
    bool  InvertedUVOrientation    = false; // [-]    - True iff signed 2D UV area <= 0
};

struct StretchMeasurementSummary
{
    std::vector<FaceStretchMetrics> PerFaceMetrics               {};    // [-]      - One distortion entry per PolygonNetwork face
    float                           AreaWeightedL2Stretch        = 1.0f; // [-]     - Sander surface-area-weighted global L2 stretch
    float                           WorstFaceLInfinityStretch    = 1.0f; // [-]     - Maximum normalized singular value across faces
    float                           MeanSymmetricDirichletEnergy = 2.0f; // [-]     - Area-weighted Symmetric Dirichlet energy
    float                           MeanQuasiConformalRatio      = 1.0f; // [-]     - Area-weighted angle distortion ratio
    float                           TotalSurfaceAreaSquareMeters = 0.0f; // [m^2]   - Sum of 3D polygon areas
    float                           TotalPositiveUVArea          = 0.0f; // [uv^2]  - Sum of positive parameterized UV areas
    uint32_t                        InvertedUVFaceCount          = 0u;   // [count] - Faces with negative UV orientation
    uint32_t                        DegenerateUVFaceCount        = 0u;   // [count] - Faces with collapsed zero-area UV triangles
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] StretchMeasurementSummary EvaluateNetworkStretch(
    const PolygonNetwork&     Network,
    std::span<const uint32_t> OptionalSubsetFaces = {});

} // namespace Frontier::GeometrySpace
