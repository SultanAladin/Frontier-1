/*==========================================================================================================================================
                                                MINIMAL LINEAR ALGEBRA FOR THE VIEWPORT ERA
                                     ------------------------------------------------------------------
                                          Vectors, column-major matrices, Vulkan clip conventions.
==========================================================================================================================================*/

// 🧩 Minimal linear algebra for the viewport era: vectors, column-major matrices, Vulkan clip conventions.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        VECTORS
//------------------------------------------------------------------------------------------------------------------------

struct Vec2
{
    float X = 0;
    float Y = 0;
};

struct Vec3
{
    float X = 0;
    float Y = 0;
    float Z = 0;
};

struct Vec4
{
    float X = 0;
    float Y = 0;
    float Z = 0;
    float W = 0;
};

[[nodiscard]] inline Vec3 operator+(Vec3 A, Vec3 B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
[[nodiscard]] inline Vec3 operator-(Vec3 A, Vec3 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
[[nodiscard]] inline Vec3 operator*(Vec3 A, float S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
[[nodiscard]] inline Vec3 operator-(Vec3 A) noexcept { return {-A.X, -A.Y, -A.Z}; }
[[nodiscard]] inline Vec4 operator+(Vec4 A, Vec4 B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W}; }
[[nodiscard]] inline Vec4 operator-(Vec4 A, Vec4 B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W}; }
[[nodiscard]] inline Vec4 operator*(Vec4 A, float S) noexcept { return {A.X * S, A.Y * S, A.Z * S, A.W * S}; }

[[nodiscard]] inline float Dot(Vec3 A, Vec3 B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }

[[nodiscard]] inline Vec3 Cross(Vec3 A, Vec3 B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

[[nodiscard]] inline float Length(Vec3 A) noexcept { return std::sqrt(Dot(A, A)); }

[[nodiscard]] inline Vec3 Normalise(Vec3 A) noexcept
{
    const float Magnitude = Length(A);
    return Magnitude > 0 ? A * (1.0f / Magnitude) : Vec3{};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        MATRICES
//------------------------------------------------------------------------------------------------------------------------

// Column-major 4x4, matching Vulkan/GLSL mat4 memory layout: Columns[c][r], so
// a point transforms as M * p with basis vectors down the columns.
struct Mat4
{
    float Columns[4][4] = {};

    [[nodiscard]] static Mat4 Identity() noexcept
    {
        Mat4 M;
        M.Columns[0][0] = M.Columns[1][1] = M.Columns[2][2] = M.Columns[3][3] = 1.0f;
        return M;
    }
};

[[nodiscard]] inline Mat4 operator*(const Mat4& A, const Mat4& B) noexcept
{
    Mat4 Out;
    for (uint32_t Column = 0; Column < 4; ++Column)
    {
        for (uint32_t Row = 0; Row < 4; ++Row)
        {
            float Sum = 0;
            for (uint32_t K = 0; K < 4; ++K)
            {
                Sum += A.Columns[K][Row] * B.Columns[Column][K];
            }
            Out.Columns[Column][Row] = Sum;
        }
    }
    return Out;
}

[[nodiscard]] inline Vec4 Transform(const Mat4& M, Vec4 P) noexcept
{
    return {
        M.Columns[0][0] * P.X + M.Columns[1][0] * P.Y + M.Columns[2][0] * P.Z + M.Columns[3][0] * P.W,
        M.Columns[0][1] * P.X + M.Columns[1][1] * P.Y + M.Columns[2][1] * P.Z + M.Columns[3][1] * P.W,
        M.Columns[0][2] * P.X + M.Columns[1][2] * P.Y + M.Columns[2][2] * P.Z + M.Columns[3][2] * P.W,
        M.Columns[0][3] * P.X + M.Columns[1][3] * P.Y + M.Columns[2][3] * P.Z + M.Columns[3][3] * P.W,
    };
}

[[nodiscard]] inline Vec3 TransformPoint(const Mat4& M, Vec3 P) noexcept
{
    const Vec4 Homogeneous = Transform(M, {P.X, P.Y, P.Z, 1.0f});
    const float InverseW   = Homogeneous.W != 0 ? 1.0f / Homogeneous.W : 0;
    return {Homogeneous.X * InverseW, Homogeneous.Y * InverseW, Homogeneous.Z * InverseW};
}

[[nodiscard]] inline Vec3 TransformDirection(const Mat4& M, Vec3 D) noexcept
{
    const Vec4 Homogeneous = Transform(M, {D.X, D.Y, D.Z, 0});
    return {Homogeneous.X, Homogeneous.Y, Homogeneous.Z};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATRIX BUILDERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mat4 TranslationMatrix(Vec3 Offset) noexcept
{
    Mat4 M = Mat4::Identity();
    M.Columns[3][0] = Offset.X;
    M.Columns[3][1] = Offset.Y;
    M.Columns[3][2] = Offset.Z;
    return M;
}

// Right-handed rotation about a unit axis, angle θ in radians.
[[nodiscard]] inline Mat4 RotationMatrix(Vec3 Axis, float θ) noexcept
{
    const Vec3  A = Normalise(Axis);
    const float C = std::cos(θ);
    const float S = std::sin(θ);
    const float T = 1.0f - C;

    Mat4 M = Mat4::Identity();
    M.Columns[0][0] = T * A.X * A.X + C;
    M.Columns[0][1] = T * A.X * A.Y + S * A.Z;
    M.Columns[0][2] = T * A.X * A.Z - S * A.Y;
    M.Columns[1][0] = T * A.X * A.Y - S * A.Z;
    M.Columns[1][1] = T * A.Y * A.Y + C;
    M.Columns[1][2] = T * A.Y * A.Z + S * A.X;
    M.Columns[2][0] = T * A.X * A.Z + S * A.Y;
    M.Columns[2][1] = T * A.Y * A.Z - S * A.X;
    M.Columns[2][2] = T * A.Z * A.Z + C;
    return M;
}

// Right-handed view matrix: camera looks down its local -Z.
[[nodiscard]] inline Mat4 LookAtMatrix(Vec3 Eye, Vec3 Target, Vec3 Up) noexcept
{
    const Vec3 Forward = Normalise(Target - Eye);
    const Vec3 Right   = Normalise(Cross(Forward, Up));
    const Vec3 TrueUp  = Cross(Right, Forward);

    Mat4 M = Mat4::Identity();
    M.Columns[0][0] = Right.X;
    M.Columns[1][0] = Right.Y;
    M.Columns[2][0] = Right.Z;
    M.Columns[0][1] = TrueUp.X;
    M.Columns[1][1] = TrueUp.Y;
    M.Columns[2][1] = TrueUp.Z;
    M.Columns[0][2] = -Forward.X;
    M.Columns[1][2] = -Forward.Y;
    M.Columns[2][2] = -Forward.Z;
    M.Columns[3][0] = -Dot(Right, Eye);
    M.Columns[3][1] = -Dot(TrueUp, Eye);
    M.Columns[3][2] = Dot(Forward, Eye);
    return M;
}

// Vulkan clip space: depth in [0, 1], Y pointing down (the [1][1] sign bakes the
// framebuffer Y-flip so shaders need no gl_Position.y negation). θ = vertical fov.
[[nodiscard]] inline Mat4 PerspectiveMatrix(float θ, float Aspect, float Near, float Far) noexcept
{
    const float Tangent = std::tan(θ * 0.5f);

    Mat4 M;
    M.Columns[0][0] = 1.0f / (Aspect * Tangent);
    M.Columns[1][1] = -1.0f / Tangent;
    M.Columns[2][2] = Far / (Near - Far);
    M.Columns[2][3] = -1.0f;
    M.Columns[3][2] = (Far * Near) / (Near - Far);
    return M;
}

} // namespace Frontier
