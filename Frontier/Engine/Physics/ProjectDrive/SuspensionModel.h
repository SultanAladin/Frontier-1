//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/SuspensionModel.h — strut spring + damper (header-only)
//============================================================================================================================================
//
//    Bit-faithful port of GRIT's suspension force model (FSuspensionSpecifications::GetSpringForce_N +
//    ComputeProgressiveCoefficients in Components/SuspensionSpecifications.h). Three spring laws — Linear, Progressive
//    (stiffening), Digressive (softening, GRIT default) — expressed as the cubic k1·x + k2·x² + k3·x³ with x the
//    compression in metres. The coefficients are derived exactly as GRIT derives them from (SpringRate, StaticPreload,
//    HardeningFactor, travel limits). Damper is linear in strut velocity; total strut force is clamped ≥ 0 (a strut
//    cannot pull the wheel back up) and applied along the ground normal by the vehicle layer.
//
//    Pure C++/scalar; no Unreal, no Jolt. Units: N/m, metres, m/s, Newtons.

#pragma once

#include <algorithm>
#include <cmath>

namespace Frontier::Vehicle {

enum class SpringLaw
{
    Linear,
    Progressive,   // stiffens toward bump stop
    Digressive,    // softens with travel (GRIT default)
};

struct SuspensionParameters
{
    float SpringRate_Npm   = 250000.0f;  // [N/m] base linear rate
    float DampingRate_Nspm = 6000.0f;    // [N·s/m] linear damper
    float HardeningFactor  = 1.5f;       // [-] force multiplier at full travel
    float StaticPreload_cm = 5.0f;       // [cm] preload compression (GRIT default)
    float MinRaise_cm      = 6.0f;       // [cm] travel above static
    float MaxDrop_cm       = 6.0f;       // [cm] travel below static
    SpringLaw Law          = SpringLaw::Digressive;
};

// Precomputed cubic coefficients (mirrors GRIT ComputeProgressiveCoefficients).
struct SuspensionCoefficients
{
    float k1 = 0.0f, k2 = 0.0f, k3 = 0.0f;   // spring cubic in x [m]
    float x_max = 0.0f;                        // [m] total usable travel
};

inline SuspensionCoefficients ComputeSuspensionCoefficients(const SuspensionParameters& P) noexcept
{
    SuspensionCoefficients C;
    C.x_max = (P.MinRaise_cm + P.MaxDrop_cm) * 0.01f;                 // [m]
    const float F0    = P.SpringRate_Npm * (P.StaticPreload_cm * 0.01f);
    const float F_max = F0 * P.HardeningFactor;

    switch (P.Law)
    {
    case SpringLaw::Progressive:
        C.k1 = P.SpringRate_Npm;
        C.k2 = 0.0f;
        C.k3 = (C.x_max > 1e-6f) ? (F_max - P.SpringRate_Npm * C.x_max) / (C.x_max * C.x_max * C.x_max) : 0.0f;
        break;
    case SpringLaw::Digressive:
        C.k1 = P.SpringRate_Npm * P.HardeningFactor;
        C.k2 = (C.x_max > 1e-6f) ? -P.SpringRate_Npm * (P.HardeningFactor - 1.0f) / C.x_max : 0.0f;
        C.k3 = 0.0f;
        break;
    case SpringLaw::Linear:
    default:
        C.k1 = P.SpringRate_Npm;
        C.k2 = 0.0f;
        C.k3 = 0.0f;
        break;
    }
    return C;
}

// Spring force from compression [cm], per GRIT GetSpringForce_N(compCm).
inline float SpringForce_N(const SuspensionParameters& P, const SuspensionCoefficients& C, float compression_cm) noexcept
{
    const float x = compression_cm * 0.01f;   // [m]
    if (P.Law == SpringLaw::Linear)
        return P.SpringRate_Npm * x;
    return C.k1 * x + C.k2 * x * x + C.k3 * x * x * x;
}

// Full strut force: spring + damper, clamped to ≥ 0. compression_cm [cm], strutVelocity [m/s] (+ = compressing).
inline float StrutForce_N(const SuspensionParameters& P, const SuspensionCoefficients& C,
                          float compression_cm, float strutVelocity_mps) noexcept
{
    const float spring = SpringForce_N(P, C, compression_cm);
    const float damper = strutVelocity_mps * P.DampingRate_Nspm;
    return std::max(spring + damper, 0.0f);
}

} // namespace Frontier::Vehicle
