/*============================================================================================================================================
                                                            AXISCOORDINATES.H
============================================================================================================================================*/
// 🧩 Axis3D, Axis2D, zero-overhead Scalar3D/Scalar2D aliases, dedicated UVAxes, SpatialTransform, and 16B-aligned .slang GPU layout structs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Frontier::Mathematics
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct Axis3D
{
    float X = 0.0f; // [m | rad] - Primary X-axis coordinate or angular component
    float Y = 0.0f; // [m | rad] - Primary Y-axis coordinate or angular component
    float Z = 0.0f; // [m | rad] - Primary Z-axis coordinate or angular component

    [[nodiscard]] constexpr bool operator==(const Axis3D&) const noexcept = default;
};

struct Axis2D
{
    float X = 0.0f; // [m | px]  - Primary horizontal planar coordinate
    float Y = 0.0f; // [m | px]  - Primary vertical planar coordinate

    [[nodiscard]] constexpr bool operator==(const Axis2D&) const noexcept = default;
};

// 📝 Zero-overhead C++20 aliases sharing all operators while expressing non-positional scale, extent, or linear RGB triplets
using Scalar3D = Axis3D;
using Scalar2D = Axis2D;

// 📝 Dedicated separate UV parameter struct preserving explicit .U and .V fields across UDIM tiles
struct UVAxes
{
    float U = 0.0f; // [uv] - Horizontal texture parameter coordinate (TileU + local [0,1])
    float V = 0.0f; // [uv] - Vertical texture parameter coordinate   (TileV + local [0,1])

    [[nodiscard]] constexpr bool operator==(const UVAxes&) const noexcept = default;
};

struct SpatialTransform
{
    Axis3D   Position { 0.0f, 0.0f, 0.0f }; // [m]   - Translation along X, Y, Z axes
    Axis3D   Rotation { 0.0f, 0.0f, 0.0f }; // [rad] - Euler orientation around X, Y, Z axes
    Scalar3D Scale    { 1.0f, 1.0f, 1.0f }; // [-]   - Scale multiplier along X, Y, Z axes

    [[nodiscard]] constexpr bool operator==(const SpatialTransform&) const noexcept = default;
};

struct alignas(16) GPUAxis4
{
    float X = 0.0f; // [-] - First 32-bit float lane
    float Y = 0.0f; // [-] - Second 32-bit float lane
    float Z = 0.0f; // [-] - Third 32-bit float lane
    float W = 0.0f; // [-] - Fourth 32-bit float lane
};

static_assert(sizeof(Axis3D)   == 12u, "Axis3D must occupy 12 contiguous bytes.");
static_assert(sizeof(Axis2D)   == 8u,  "Axis2D must occupy 8 contiguous bytes.");
static_assert(sizeof(UVAxes)   == 8u,  "UVAxes must occupy 8 contiguous bytes.");
static_assert(sizeof(GPUAxis4) == 16u, "GPUAxis4 must occupy 16 aligned bytes for .slang parity.");
static_assert(offsetof(GPUAxis4, W) == 12u, "GPUAxis4::W must sit at byte offset 12.");

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr Axis3D operator+(Axis3D Left, Axis3D Right) noexcept;
[[nodiscard]] constexpr Axis3D operator-(Axis3D Left, Axis3D Right) noexcept;
[[nodiscard]] constexpr Axis3D operator-(Axis3D Operand) noexcept;
[[nodiscard]] constexpr Axis3D operator*(Axis3D Vector, float Factor) noexcept;
[[nodiscard]] constexpr Axis3D operator*(float Factor, Axis3D Vector) noexcept;
[[nodiscard]] constexpr Axis3D HadamardMultiply(Axis3D Left, Axis3D Right) noexcept;

[[nodiscard]] constexpr Axis2D operator+(Axis2D Left, Axis2D Right) noexcept;
[[nodiscard]] constexpr Axis2D operator-(Axis2D Left, Axis2D Right) noexcept;
[[nodiscard]] constexpr Axis2D operator*(Axis2D Vector, float Factor) noexcept;

[[nodiscard]] constexpr UVAxes operator+(UVAxes Left, UVAxes Right) noexcept;
[[nodiscard]] constexpr UVAxes operator-(UVAxes Left, UVAxes Right) noexcept;
[[nodiscard]] constexpr UVAxes operator*(UVAxes Coordinate, float Factor) noexcept;

[[nodiscard]] constexpr float  EvaluateDot(Axis3D Left, Axis3D Right) noexcept;
[[nodiscard]] constexpr float  EvaluateDot(Axis2D Left, Axis2D Right) noexcept;
[[nodiscard]] constexpr float  EvaluateDot(UVAxes Left, UVAxes Right) noexcept;
[[nodiscard]] constexpr Axis3D EvaluateCross(Axis3D Left, Axis3D Right) noexcept;
[[nodiscard]] constexpr float  EvaluateCross2D(UVAxes Left, UVAxes Right) noexcept;

[[nodiscard]] constexpr float  EvaluateLengthSquared(Axis3D Vector) noexcept;
[[nodiscard]] inline    float  EvaluateLength(Axis3D Vector) noexcept;
[[nodiscard]] inline    Axis3D NormalizeAxis(Axis3D Vector, Axis3D Fallback = { 0.0f, 1.0f, 0.0f }) noexcept;
[[nodiscard]] inline    float  EvaluateDistance(Axis3D First, Axis3D Second) noexcept;
[[nodiscard]] inline    float  EvaluateUVDistance(UVAxes First, UVAxes Second) noexcept;

[[nodiscard]] constexpr Axis3D InterpolateLinear(Axis3D Start, Axis3D Finish, float Parameter) noexcept;
[[nodiscard]] constexpr UVAxes InterpolateLinear(UVAxes Start, UVAxes Finish, float Parameter) noexcept;
[[nodiscard]] inline    Axis3D ApplySpatialTransform(const SpatialTransform& Transform, Axis3D LocalAxes) noexcept;
[[nodiscard]] constexpr GPUAxis4 PackGPUAxis4(Axis3D XYZ, float W = 1.0f) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

constexpr Axis3D operator+(Axis3D Left, Axis3D Right) noexcept
{
    return { Left.X + Right.X, Left.Y + Right.Y, Left.Z + Right.Z };
}

constexpr Axis3D operator-(Axis3D Left, Axis3D Right) noexcept
{
    return { Left.X - Right.X, Left.Y - Right.Y, Left.Z - Right.Z };
}

constexpr Axis3D operator-(Axis3D Operand) noexcept
{
    return { -Operand.X, -Operand.Y, -Operand.Z };
}

constexpr Axis3D operator*(Axis3D Vector, float Factor) noexcept
{
    return { Vector.X * Factor, Vector.Y * Factor, Vector.Z * Factor };
}

constexpr Axis3D operator*(float Factor, Axis3D Vector) noexcept
{
    return { Vector.X * Factor, Vector.Y * Factor, Vector.Z * Factor };
}

constexpr Axis3D HadamardMultiply(Axis3D Left, Axis3D Right) noexcept
{
    return { Left.X * Right.X, Left.Y * Right.Y, Left.Z * Right.Z };
}

constexpr Axis2D operator+(Axis2D Left, Axis2D Right) noexcept
{
    return { Left.X + Right.X, Left.Y + Right.Y };
}

constexpr Axis2D operator-(Axis2D Left, Axis2D Right) noexcept
{
    return { Left.X - Right.X, Left.Y - Right.Y };
}

constexpr Axis2D operator*(Axis2D Vector, float Factor) noexcept
{
    return { Vector.X * Factor, Vector.Y * Factor };
}

constexpr UVAxes operator+(UVAxes Left, UVAxes Right) noexcept
{
    return { Left.U + Right.U, Left.V + Right.V };
}

constexpr UVAxes operator-(UVAxes Left, UVAxes Right) noexcept
{
    return { Left.U - Right.U, Left.V - Right.V };
}

constexpr UVAxes operator*(UVAxes Coordinate, float Factor) noexcept
{
    return { Coordinate.U * Factor, Coordinate.V * Factor };
}

constexpr float EvaluateDot(Axis3D Left, Axis3D Right) noexcept
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

constexpr float EvaluateDot(Axis2D Left, Axis2D Right) noexcept
{
    return Left.X * Right.X + Left.Y * Right.Y;
}

constexpr float EvaluateDot(UVAxes Left, UVAxes Right) noexcept
{
    return Left.U * Right.U + Left.V * Right.V;
}

constexpr Axis3D EvaluateCross(Axis3D Left, Axis3D Right) noexcept
{
    return {
        Left.Y * Right.Z - Left.Z * Right.Y,
        Left.Z * Right.X - Left.X * Right.Z,
        Left.X * Right.Y - Left.Y * Right.X,
    };
}

constexpr float EvaluateCross2D(UVAxes Left, UVAxes Right) noexcept
{
    return Left.U * Right.V - Left.V * Right.U;
}

constexpr float EvaluateLengthSquared(Axis3D Vector) noexcept
{
    return EvaluateDot(Vector, Vector);
}

inline float EvaluateLength(Axis3D Vector) noexcept
{
    return std::sqrt(EvaluateLengthSquared(Vector));
}

inline Axis3D NormalizeAxis(Axis3D Vector, Axis3D Fallback) noexcept
{
    const float LengthSq = EvaluateLengthSquared(Vector);
    if (LengthSq <= Constants::SingleEpsilon * Constants::SingleEpsilon)
    {
        return Fallback;
    }
    const float InvLength = 1.0f / std::sqrt(LengthSq);
    return Vector * InvLength;
}

inline float EvaluateDistance(Axis3D First, Axis3D Second) noexcept
{
    return EvaluateLength(Second - First);
}

inline float EvaluateUVDistance(UVAxes First, UVAxes Second) noexcept
{
    const UVAxes Delta = Second - First;
    return std::sqrt(EvaluateDot(Delta, Delta));
}

constexpr Axis3D InterpolateLinear(Axis3D Start, Axis3D Finish, float Parameter) noexcept
{
    return Start + (Finish - Start) * Parameter;
}

constexpr UVAxes InterpolateLinear(UVAxes Start, UVAxes Finish, float Parameter) noexcept
{
    return Start + (Finish - Start) * Parameter;
}

inline Axis3D ApplySpatialTransform(const SpatialTransform& Transform, Axis3D LocalAxes) noexcept
{
    Axis3D Scaled = HadamardMultiply(LocalAxes, Transform.Scale);

    const float CosX = std::cos(Transform.Rotation.X), SinX = std::sin(Transform.Rotation.X);
    const float CosY = std::cos(Transform.Rotation.Y), SinY = std::sin(Transform.Rotation.Y);
    const float CosZ = std::cos(Transform.Rotation.Z), SinZ = std::sin(Transform.Rotation.Z);

    // ① Rotate around X-axis
    const Axis3D RotX {
        Scaled.X,
        Scaled.Y * CosX - Scaled.Z * SinX,
        Scaled.Y * SinX + Scaled.Z * CosX,
    };
    // ② Rotate around Y-axis
    const Axis3D RotXY {
        RotX.X * CosY + RotX.Z * SinY,
        RotX.Y,
        -RotX.X * SinY + RotX.Z * CosY,
    };
    // ③ Rotate around Z-axis + translate
    const Axis3D RotXYZ {
        RotXY.X * CosZ - RotXY.Y * SinZ,
        RotXY.X * SinZ + RotXY.Y * CosZ,
        RotXY.Z,
    };
    return RotXYZ + Transform.Position;
}

constexpr GPUAxis4 PackGPUAxis4(Axis3D XYZ, float W) noexcept
{
    return { XYZ.X, XYZ.Y, XYZ.Z, W };
}

} // namespace Frontier::Mathematics
