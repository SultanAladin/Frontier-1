/*============================================================================================================================================
                                                             PARTICLEVECTORMATH.H
============================================================================================================================================*/
// 🧩 Shared single-source 3D vector helpers for ParticleSpace dynamics & strand rendering backed by Frontier::Mathematics::Axis3D

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cmath>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      TYPE ALIASES
//------------------------------------------------------------------------------------------------------------------------

using Vector3f = Mathematics::Axis3D;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Vector3f MultiplyComponents(Vector3f A, Vector3f B) noexcept
{
    return { A.X * B.X, A.Y * B.Y, A.Z * B.Z };
}

[[nodiscard]] inline float Dot3(Vector3f A, Vector3f B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

[[nodiscard]] inline Vector3f Cross3(Vector3f A, Vector3f B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}

[[nodiscard]] inline float Len3(Vector3f V) noexcept
{
    return std::sqrt(Dot3(V, V));
}

[[nodiscard]] inline float Length3(Vector3f V) noexcept
{
    return Len3(V);
}

[[nodiscard]] inline Vector3f Norm3(Vector3f V) noexcept
{
    const float L = Len3(V);
    return (L > 1.0e-6f) ? Vector3f { V.X / L, V.Y / L, V.Z / L } : Vector3f { 0.0f, 1.0f, 0.0f };
}

[[nodiscard]] inline Vector3f Normalize3(Vector3f V) noexcept
{
    const float L = Len3(V);
    return (L > 1.0e-7f) ? Vector3f { V.X / L, V.Y / L, V.Z / L } : Vector3f { 0.0f, 1.0f, 0.0f };
}

[[nodiscard]] inline Vector3f Mix3(Vector3f A, Vector3f B, float T) noexcept
{
    return {
        A.X + (B.X - A.X) * T,
        A.Y + (B.Y - A.Y) * T,
        A.Z + (B.Z - A.Z) * T,
    };
}

} // namespace Frontier::ParticleSpace
