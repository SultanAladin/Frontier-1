/*==========================================================================================================================================
                                                       DIELECTRICREFRACTION.H
==========================================================================================================================================*/
// 🧩 Unit-direction Snell refraction, exact unpolarized dielectric Fresnel and radiance transport scaling.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     DIELECTRIC BOUNDARY
//------------------------------------------------------------------------------------------------------------------------

struct DielectricRefraction
{
    std::array<double, 3> Direction{};
    double Reflectance = 1, RadianceScale = 0;
    bool TotalInternalReflection = false, Valid = false;
};
inline DielectricRefraction EvaluateDielectricRefraction(
    const std::array<double, 3>& Incident,
    const std::array<double, 3>& Normal,
    double                      IncidentIor,
    double                      TransmittedIor)
{
    DielectricRefraction R;
    if (!std::isfinite(IncidentIor) || !std::isfinite(TransmittedIor) || IncidentIor <= 0 || TransmittedIor <= 0) return R;
    double Dot = 0, IncidentLength = 0, NormalLength = 0;
    for (size_t I = 0; I < 3; ++I)
    {
        if (!std::isfinite(Incident[I]) || !std::isfinite(Normal[I])) return R;
        Dot += Incident[I] * Normal[I]; IncidentLength += Incident[I] * Incident[I]; NormalLength += Normal[I] * Normal[I];
    }
    if (std::abs(IncidentLength - 1) > 1e-4 || std::abs(NormalLength - 1) > 1e-4 || Dot > 1e-6) return R;
    double Cosine = std::clamp(-Dot, 0., 1.), η = IncidentIor / TransmittedIor;
    if (!std::isfinite(η * η)) return R;
    R.Valid = true;
    R.RadianceScale = η * η;
    double Squared = 1 - η * η * (1 - Cosine * Cosine);
    if (Squared < 0)
    {
        R.TotalInternalReflection = true;
        return R;
    }
    double TransmittedCosine = std::sqrt(Squared);
    if (IncidentIor == TransmittedIor) R.Reflectance = 0;
    else
    {
        double S = (IncidentIor * Cosine - TransmittedIor * TransmittedCosine) /
                   (IncidentIor * Cosine + TransmittedIor * TransmittedCosine);
        double P = (TransmittedIor * Cosine - IncidentIor * TransmittedCosine) /
                   (TransmittedIor * Cosine + IncidentIor * TransmittedCosine);
        R.Reflectance = .5 * (S * S + P * P);
    }
    double Length = 0;
    for (size_t I = 0; I < 3; ++I)
    {
        R.Direction[I] = η * Incident[I] + (η * Cosine - TransmittedCosine) * Normal[I];
        Length += R.Direction[I] * R.Direction[I];
    }
    for (double& C : R.Direction) C /= std::sqrt(Length);
    return R;
}
} // namespace Frontier
