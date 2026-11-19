/*====================================================================================================================================
                                                        VECTORALGEBRA.H
====================================================================================================================================*/
// 🧩 Double-precision 2D/3D vector algebra and orthonormal frames for exact geometry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cmath>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct Vector2
{
    double X = 0.0;                                                                          // [-] - First component
    double Y = 0.0;                                                                          // [-] - Second component

    constexpr Vector2  operator+(Vector2 Other) const noexcept { return { X + Other.X, Y + Other.Y }; }
    constexpr Vector2  operator-(Vector2 Other) const noexcept { return { X - Other.X, Y - Other.Y }; }
    constexpr Vector2  operator*(double Scale)  const noexcept { return { X * Scale, Y * Scale }; }
    constexpr Vector2  operator-()              const noexcept { return { -X, -Y }; }
};

struct Vector3
{
    double X = 0.0;                                                                          // [-] - First component
    double Y = 0.0;                                                                          // [-] - Second component
    double Z = 0.0;                                                                          // [-] - Third component

    constexpr Vector3  operator+(Vector3 Other) const noexcept { return { X + Other.X, Y + Other.Y, Z + Other.Z }; }
    constexpr Vector3  operator-(Vector3 Other) const noexcept { return { X - Other.X, Y - Other.Y, Z - Other.Z }; }
    constexpr Vector3  operator*(double Scale)  const noexcept { return { X * Scale, Y * Scale, Z * Scale }; }
    constexpr Vector3  operator-()              const noexcept { return { -X, -Y, -Z }; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] constexpr double  Dot(Vector2 First, Vector2 Second)   noexcept { return First.X * Second.X + First.Y * Second.Y; }
[[nodiscard]] constexpr double  Cross(Vector2 First, Vector2 Second) noexcept { return First.X * Second.Y - First.Y * Second.X; }
[[nodiscard]] constexpr double  Dot(Vector3 First, Vector3 Second)   noexcept { return First.X * Second.X + First.Y * Second.Y + First.Z * Second.Z; }
[[nodiscard]] constexpr Vector3 Cross(Vector3 First, Vector3 Second) noexcept
{
    return { First.Y * Second.Z - First.Z * Second.Y, First.Z * Second.X - First.X * Second.Z, First.X * Second.Y - First.Y * Second.X };
}

[[nodiscard]] inline double  Length(Vector2 Vector) noexcept { return std::hypot(Vector.X, Vector.Y); }
[[nodiscard]] inline double  Length(Vector3 Vector) noexcept { return std::sqrt(Dot(Vector, Vector)); }
[[nodiscard]] inline double  Distance(Vector3 First, Vector3 Second) noexcept { return Length(First - Second); }
[[nodiscard]] inline double  Distance(Vector2 First, Vector2 Second) noexcept { return Length(First - Second); }

[[nodiscard]] inline Vector3 Normalize(Vector3 Vector) noexcept
{
    const double Magnitude = Length(Vector);                                                 // [-] - Euclidean norm
    return Magnitude > 0.0 ? Vector * (1.0 / Magnitude) : Vector3{};
}

[[nodiscard]] inline Vector2 Normalize(Vector2 Vector) noexcept
{
    const double Magnitude = Length(Vector);                                                 // [-] - Euclidean norm
    return Magnitude > 0.0 ? Vector * (1.0 / Magnitude) : Vector2{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAMES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Right-handed orthonormal frame: XAxis × YAxis = ZAxis. Every analytic curve and surface is placed by one.
struct OrthonormalBasis
{
    Vector3 Origin = { 0.0, 0.0, 0.0 };                                                      // [len] - Frame origin
    Vector3 XAxis  = { 1.0, 0.0, 0.0 };                                                      // [-]   - Unit reference direction
    Vector3 YAxis  = { 0.0, 1.0, 0.0 };                                                      // [-]   - Unit second direction
    Vector3 ZAxis  = { 0.0, 0.0, 1.0 };                                                      // [-]   - Unit axis / normal direction
};

// Construct a right-handed frame from an axis and a reference direction (re-orthogonalised against the axis).
[[nodiscard]] inline OrthonormalBasis ConstructFrame(Vector3 Origin, Vector3 Axis, Vector3 Reference) noexcept
{
    const Vector3 ZAxis = Normalize(Axis);                                                   // [-] - Unit axis
    const Vector3 XAxis = Normalize(Reference - ZAxis * Dot(Reference, ZAxis));               // [-] - Reference projected off the axis
    return { Origin, XAxis, Cross(ZAxis, XAxis), ZAxis };
}

} // namespace Frontier::GeometrySpace::Parametric
