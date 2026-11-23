/*==========================================================================================================================================
                                                       ANISOTROPICREFLECTION.H
==========================================================================================================================================*/
// 🧩 Scalar references for tangent-aligned GGX reflection and exact unpolarized dielectric Fresnel, including total internal reflection.

#pragma once
#include "ScatteringSequence.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     DIRECTIONAL MICROFACET OPTICS
//------------------------------------------------------------------------------------------------------------------------

inline std::array<double,2> DeriveAnisotropicSpread(double Roughness,double Anisotropy)
{
    const double α = Roughness*Roughness;
    const double Aspect = std::sqrt(1-.9*std::abs(Anisotropy));
    return Anisotropy >= 0 ? std::array<double,2>{α/Aspect,α*Aspect} : std::array<double,2>{α*Aspect,α/Aspect};
}

inline double EvaluateAnisotropicDistribution(const std::array<double,3> &Half,const std::array<double,2> &Spread)
{
    if (Half[2] <= 0) return 0;
    const double Denominator = (Half[0]/Spread[0])*(Half[0]/Spread[0]) +
                               (Half[1]/Spread[1])*(Half[1]/Spread[1]) + Half[2]*Half[2];
    return 1/(std::numbers::pi*Spread[0]*Spread[1]*Denominator*Denominator);
}

inline double EvaluateAnisotropicVisibility(
    const std::array<double,3> &View,
    const std::array<double,3> &Light,
    const std::array<double,2> &Spread)
{
    if (View[2] <= 0 || Light[2] <= 0) return 0;
    const auto ProjectedLength = [&](const auto &Direction)
    {
        return std::sqrt(Spread[0]*Spread[0]*Direction[0]*Direction[0] +
                         Spread[1]*Spread[1]*Direction[1]*Direction[1] + Direction[2]*Direction[2]);
    };
    return .5/std::max(1e-12,Light[2]*ProjectedLength(View)+View[2]*ProjectedLength(Light));
}

inline double EvaluateExactDielectricReflectance(double Cosine,double IncidentIor,double TransmittedIor)
{
    if (IncidentIor == TransmittedIor) return 0;
    Cosine = std::clamp(std::abs(Cosine),0.0,1.0);
    const double Ratio = IncidentIor/TransmittedIor;
    const double SineSquared = Ratio*Ratio*(1-Cosine*Cosine);
    if (SineSquared >= 1) return 1;
    const double TransmittedCosine = std::sqrt(1-SineSquared);
    const double Perpendicular = (IncidentIor*Cosine-TransmittedIor*TransmittedCosine)/
                                 (IncidentIor*Cosine+TransmittedIor*TransmittedCosine);
    const double Parallel = (TransmittedIor*Cosine-IncidentIor*TransmittedCosine)/
                            (TransmittedIor*Cosine+IncidentIor*TransmittedCosine);
    return .5*(Perpendicular*Perpendicular+Parallel*Parallel);
}
} // namespace Frontier
