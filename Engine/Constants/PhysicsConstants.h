/*============================================================================================================================================
                                                            PHYSICSCONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time physical constants, reference medium densities, and contact friction limits

#pragma once

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double StandardGravity         = 9.80665;  // [m/s^2]  - Standard terrestrial gravitational acceleration
inline constexpr double DryAirDensity           = 1.225;    // [kg/m^3] - Sea-level ISA dry air reference density
inline constexpr double FreshWaterDensity       = 998.2;    // [kg/m^3] - Liquid water density at 293.15 K
inline constexpr double StandardAtmospherePa    = 101325.0; // [Pa]     - Standard sea-level barometric pressure
inline constexpr double UniversalGasConstant    = 8.314462; // [J/molK] - Molar ideal gas constant
inline constexpr float  MinimumFrictionLimit    = 0.0f;     // [-]      - Lower bound for Coulomb contact μ
inline constexpr float  MaximumFrictionLimit    = 4.0f;     // [-]      - Upper safety clamp for Coulomb contact μ
inline constexpr float  MaximumRestitutionLimit = 1.0f;     // [0-1]    - Elastic coefficient upper clamp

} // namespace Frontier::Constants
