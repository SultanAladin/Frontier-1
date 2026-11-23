//============================================================================================================================================
//                                                               VEHICLEMATH.H
//============================================================================================================================================
// 📦 The vehicle stack's own vector and quaternion: self-contained so the solver and its checks build headless.
//
// Frame conventions for everything in Engine/PhysicalDynamics/Vehicle: right-handed, +Z up, chassis-local +X forward,
//    +Y left; the wheel axle is hub-local +Y and positive spin rolls the tyre toward +X.

#pragma once

#include <cmath>

namespace Frontier::Vehicle {

struct Vec3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float X, float Y, float Z) noexcept : x(X), y(Y), z(Z) {}

    Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s)       const noexcept { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }

    [[nodiscard]] float Length()    const noexcept { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] float LengthSq()  const noexcept { return x * x + y * y + z * z; }
    [[nodiscard]] Vec3  Normalized() const noexcept
    {
        const float l = Length();
        return (l > 1e-12f) ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0};
    }
};

[[nodiscard]] inline float Dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] inline Vec3  Cross(const Vec3& a, const Vec3& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Quaternion (x,y,z,w).
struct Quat
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    [[nodiscard]] static Quat AxisAngle(const Vec3& axis, float angleRad) noexcept
    {
        const Vec3 a = axis.Normalized();
        const float h = 0.5f * angleRad, s = std::sin(h);
        return {a.x * s, a.y * s, a.z * s, std::cos(h)};
    }
    // Hamilton product. (a ∘ b).Rotate(v) == a.Rotate(b.Rotate(v)) — b is applied first, so a wheel's own spin
    //    composes on the RIGHT of the hub frame: it is a rotation of the material, inside the hub's axes.
    [[nodiscard]] Quat operator*(const Quat& o) const noexcept
    {
        return { w * o.x + x * o.w + y * o.z - z * o.y,
                 w * o.y - x * o.z + y * o.w + z * o.x,
                 w * o.z + x * o.y - y * o.x + z * o.w,
                 w * o.w - x * o.x - y * o.y - z * o.z };
    }
    [[nodiscard]] Quat Normalized() const noexcept
    {
        const float l = std::sqrt(x * x + y * y + z * z + w * w);
        return (l > 1e-12f) ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    [[nodiscard]] Quat Conjugate() const noexcept { return {-x, -y, -z, w}; }
    [[nodiscard]] Vec3 Rotate(const Vec3& v) const noexcept
    {
        const Vec3 u{x, y, z};
        const Vec3 t = Cross(u, v) * 2.0f;
        return v + (t * w) + Cross(u, t);
    }
};

} // namespace Frontier::Vehicle
