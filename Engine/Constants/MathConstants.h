/*============================================================================================================================================
                                                              MATHCONSTANTS.H
============================================================================================================================================*/
// 🧩 Shared compile-time mathematical constants, angular conversion factors, and IEEE-754 comparison tolerances

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <numbers>

namespace Frontier::Constants
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr double Pi                = std::numbers::pi_v<double>;         // [rad]   - Half-turn circle ratio
inline constexpr double Tau               = 2.0 * std::numbers::pi_v<double>;   // [rad]   - Full-turn circle ratio
inline constexpr double HalfPi            = 0.5 * std::numbers::pi_v<double>;   // [rad]   - Quarter-turn right angle
inline constexpr double InversePi         = std::numbers::inv_pi_v<double>;     // [1/rad] - Reciprocal of Pi
inline constexpr double SquareRootTwo     = std::numbers::sqrt2_v<double>;      // [-]     - Principal root of two
inline constexpr double GoldenRatio       = std::numbers::phi_v<double>;        // [-]     - Algebraic golden ratio
inline constexpr double DegToRad          = Pi / 180.0;                         // [rad/°] - Degrees to radians factor
inline constexpr double RadToDeg          = 180.0 / Pi;                         // [°/rad] - Radians to degrees factor
inline constexpr double Epsilon           = 1.0e-9;                             // [-]     - Double-precision geometric tolerance
inline constexpr float  SingleEpsilon     = 1.0e-5f;                            // [-]     - Single-precision geometric tolerance
inline constexpr float  PiFloat           = std::numbers::pi_v<float>;          // [rad]   - Single-precision Pi
inline constexpr float  TauFloat          = 2.0f * std::numbers::pi_v<float>;   // [rad]   - Single-precision Tau
inline constexpr float  DegToRadFloat     = PiFloat / 180.0f;                   // [rad/°] - Single-precision degree factor
inline constexpr float  RadToDegFloat     = 180.0f / PiFloat;                   // [°/rad] - Single-precision radian factor

} // namespace Frontier::Constants
