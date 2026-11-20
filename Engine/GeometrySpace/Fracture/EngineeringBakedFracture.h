/*============================================================================================================================================
                                                        ENGINEERINGBAKEDFRACTURE.H
============================================================================================================================================*/
// 🧩 Offline engineering fracture solver: 3D Navier-Cauchy stress lattice, Smith 3x3 eigensolver, stress-shadow wake, and non-planar fractography

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"

namespace Frontier::GeometrySpace::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PrincipalStressEigen
{
    double Sigma1      = 0.0;
    double Sigma2      = 0.0;
    double Sigma3      = 0.0;
    double E1X         = 1.0, E1Y = 0.0, E1Z = 0.0;
    double E3X         = 0.0, E3Y = 0.0, E3Z = 1.0;
    double VonMisesMPa = 0.0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class EngineeringBakedFracture
{
public:
    static void SolveSymmetricEigen3x3(
        double                Sxx,
        double                Syy,
        double                Szz,
        double                Txy,
        double                Tyz,
        double                Txz,
        PrincipalStressEigen& OutEigen) noexcept;

    [[nodiscard]] static FracturePartitionResult Execute(
        const ConvexPolyhedron&      SourcePolyhedron,
        const FractureSpecification& Specification);
};

} // namespace Frontier::GeometrySpace::Fracture
