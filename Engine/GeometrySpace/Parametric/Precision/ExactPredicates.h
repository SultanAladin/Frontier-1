/*====================================================================================================================================
                                                        EXACTPREDICATES.H
====================================================================================================================================*/
// 🧩 Floating-point-filtered exact orientation predicates (Shewchuk expansion arithmetic) for combinatorial decisions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/VectorAlgebra.h"

#include <cstdint>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which branch produced the sign: the cheap filtered determinant, or the exact expansion fallback.
enum class SignResolution : uint8_t
{
    Filtered = 0,
    Expansion,
};

struct SignOutcome
{
    int                 Sign       = 0;                                                      // [-] - −1, 0, +1 exactly
    SignResolution Resolution = SignResolution::Filtered;                          // [-] - Branch that decided the sign
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Exact sign of the orientation determinant of A, B, C in the plane: +1 counter-clockwise, −1 clockwise, 0 collinear.
[[nodiscard]] SignOutcome Orient2D(Vector2 A, Vector2 B, Vector2 C) noexcept;

// Exact sign of the orientation determinant of D relative to the plane through A, B, C:
// +1 when D lies below the plane (A, B, C counter-clockwise seen from above), −1 above, 0 coplanar.
[[nodiscard]] SignOutcome Orient3D(Vector3 A, Vector3 B, Vector3 C, Vector3 D) noexcept;

// Exact sign of the lifted (in-circle) determinant: +1 when D lies strictly inside the circle through A, B, C taken
// counter-clockwise, −1 outside, 0 cocircular. Clockwise A, B, C flip the sign.
[[nodiscard]] SignOutcome InCircle(Vector2 A, Vector2 B, Vector2 C, Vector2 D) noexcept;

// Naive (unfiltered, inexact) determinants — retained only so the validation can measure how often they are wrong.
[[nodiscard]] double NaiveOrient2D(Vector2 A, Vector2 B, Vector2 C) noexcept;
[[nodiscard]] double NaiveOrient3D(Vector3 A, Vector3 B, Vector3 C, Vector3 D) noexcept;
[[nodiscard]] double NaiveInCircle(Vector2 A, Vector2 B, Vector2 C, Vector2 D) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
