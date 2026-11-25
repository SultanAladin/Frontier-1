/*============================================================================================================================================
                                                         FORCEFIELDGPUUPLOADER.CPP
============================================================================================================================================*/
// 🧩 100% GPU gizmo line-list builder (all 20 force models) and triple-buffered Vulkan SSBO / vertex uploader
// Reference: Experimental/FluidEditor/Engine/VolumeRenderer.js L575-L994, FluidSolver.js L419-L438

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldGpuUploader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

using Vec3 = std::array<float, 3>;
using Rgba = std::array<float, 4>;

[[nodiscard]] Vec3 Add(const Vec3& a, const Vec3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] Vec3 Sub(const Vec3& a, const Vec3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] Vec3 Scale(const Vec3& a, const float s) noexcept { return { a[0] * s, a[1] * s, a[2] * s }; }
[[nodiscard]] float Dot(const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] Vec3 Normalize(const Vec3& v) noexcept
{
    const float len = std::sqrt(Dot(v, v));
    return len > 1.0e-6f ? Scale(v, 1.0f / len) : Vec3 { 0.0f, 1.0f, 0.0f };
}
[[nodiscard]] Vec3 AnyPerpendicular(const Vec3& n) noexcept
{
    const Vec3 ref = (std::fabs(n[1]) < 0.9f) ? Vec3 { 0.0f, 1.0f, 0.0f } : Vec3 { 1.0f, 0.0f, 0.0f };
    return Normalize(Cross(ref, n));
}
[[nodiscard]] float PositiveMod(const float x, const float m) noexcept
{
    const float r = std::fmod(x, m);
    return r < 0.0f ? r + m : r;
}

[[nodiscard]] std::array<float, 3> ParseHexRgb(const std::string_view hex) noexcept
{
    auto nibble = [](const char c) noexcept -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return 0;
    };
    if (hex.size() < 7u || hex[0] != '#') return { 0.7f, 0.8f, 1.0f };
    const int r = (nibble(hex[1]) << 4) | nibble(hex[2]);
    const int g = (nibble(hex[3]) << 4) | nibble(hex[4]);
    const int b = (nibble(hex[5]) << 4) | nibble(hex[6]);
    return { static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f };
}

class GizmoLineWriter
{
public:
    explicit GizmoLineWriter(std::vector<ForceGizmoVertex>& target) noexcept : Out(target) {}

    void Line(const Vec3& a, const Vec3& b, const Rgba& colour)
    {
        Out.push_back(ForceGizmoVertex { { a[0], a[1], a[2] }, 0.0f, { colour[0], colour[1], colour[2], colour[3] } });
        Out.push_back(ForceGizmoVertex { { b[0], b[1], b[2] }, 0.0f, { colour[0], colour[1], colour[2], colour[3] } });
    }

    void Polyline(std::span<const Vec3> pts, const Rgba& colour)
    {
        for (size_t i = 1u; i < pts.size(); ++i)
        {
            Line(pts[i - 1u], pts[i], colour);
        }
    }

    void Circle(const Vec3& centre, const Vec3& normal, const float radius, const Rgba& colour, const int segments)
    {
        const Vec3 n = Normalize(normal);
        const Vec3 u = AnyPerpendicular(n);
        const Vec3 v = Cross(n, u);
        Vec3 prev = Add(centre, Scale(u, radius));
        for (int i = 1; i <= segments; ++i)
        {
            const float a = (static_cast<float>(i) / static_cast<float>(segments)) * kTwoPi;
            const Vec3 cur = Add(centre, Add(Scale(u, std::cos(a) * radius), Scale(v, std::sin(a) * radius)));
            Line(prev, cur, colour);
            prev = cur;
        }
    }

    void DashedCircle(const Vec3& centre, const Vec3& normal, const float radius, const Rgba& colour, const int segments)
    {
        const Vec3 n = Normalize(normal);
        const Vec3 u = AnyPerpendicular(n);
        const Vec3 v = Cross(n, u);
        for (int i = 0; i < segments; i += 2)
        {
            const float a0 = (static_cast<float>(i) / static_cast<float>(segments)) * kTwoPi;
            const float a1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * kTwoPi;
            const Vec3 p0 = Add(centre, Add(Scale(u, std::cos(a0) * radius), Scale(v, std::sin(a0) * radius)));
            const Vec3 p1 = Add(centre, Add(Scale(u, std::cos(a1) * radius), Scale(v, std::sin(a1) * radius)));
            Line(p0, p1, colour);
        }
    }

    void SphereWire(const Vec3& centre, const float radius, const Rgba& colour)
    {
        Circle(centre, { 0.0f, 1.0f, 0.0f }, radius, colour, 32);
        Circle(centre, { 1.0f, 0.0f, 0.0f }, radius, colour, 32);
        Circle(centre, { 0.0f, 0.0f, 1.0f }, radius, colour, 32);
    }

    void Chevron(const Vec3& tip, const Vec3& direction, const float size, const Rgba& colour)
    {
        const Vec3 d = Normalize(direction);
        const Vec3 u = AnyPerpendicular(d);
        const Vec3 base = Sub(tip, Scale(d, size));
        Line(tip, Add(base, Scale(u, size * 0.5f)), colour);
        Line(tip, Sub(base, Scale(u, size * 0.5f)), colour);
    }

    void Arrow(const Vec3& start, const Vec3& direction, const float length, const Rgba& colour)
    {
        const Vec3 d = Normalize(direction);
        const Vec3 tip = Add(start, Scale(d, length));
        Line(start, tip, colour);
        Chevron(tip, d, std::min(0.18f, length * 0.2f), colour);
    }

    void RotationMarks(const Vec3& centre, const Vec3& axis, const float radius, const Rgba& colour, const int count, const float sign)
    {
        const Vec3 n = Normalize(axis);
        const Vec3 u = AnyPerpendicular(n);
        const Vec3 v = Cross(n, u);
        for (int i = 0; i < count; ++i)
        {
            const float a = (static_cast<float>(i) / static_cast<float>(count)) * kTwoPi;
            const Vec3 radial = Add(Scale(u, std::cos(a)), Scale(v, std::sin(a)));
            const Vec3 point = Add(centre, Scale(radial, radius));
            const Vec3 tangent = Scale(Cross(n, radial), sign);
            Chevron(point, tangent, std::max(0.05f, radius * 0.12f), colour);
        }
    }

    std::vector<Vec3> Helix(
        const Vec3& centre,
        const Vec3& axis,
        const float radius,
        const float height,
        const float turns,
        const Rgba& colour,
        const float phase,
        const int steps)
    {
        const Vec3 n = Normalize(axis);
        const Vec3 u = AnyPerpendicular(n);
        const Vec3 v = Cross(n, u);
        std::vector<Vec3> pts;
        pts.reserve(static_cast<size_t>(steps) + 1u);
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float a = t * kTwoPi * turns + phase;
            const Vec3 p = Add(centre, Add(Scale(n, (t - 0.5f) * height), Add(Scale(u, std::cos(a) * radius), Scale(v, std::sin(a) * radius))));
            pts.push_back(p);
        }
        Polyline(pts, colour);
        return pts;
    }

private:
    std::vector<ForceGizmoVertex>& Out;
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             1:1 GPU GIZMO GEOMETRY BUILDER
//------------------------------------------------------------------------------------------------------------------------

void AppendForceGizmoLineVertices(
    const ForceModelNode& node,
    const bool selected,
    const float opacity,
    const float simulationSeconds,
    const std::array<float, 3>& regionHalfExtent,
    std::vector<ForceGizmoVertex>& outVertices)
{
    // Reference: Experimental/FluidEditor/Engine/VolumeRenderer.js L575-L994
    const ForceModelDescriptor* desc = FindForceModelByOrdinal(node.ModelCode);
    const std::array<float, 3> tint = ParseHexRgb(desc != nullptr ? desc->TintHex : "#88bbff");
    const float alpha = (node.Enabled ? 1.0f : 0.35f) * opacity;
    const Rgba colour = selected
        ? Rgba { 1.0f, 1.0f, 1.0f, alpha }
        : Rgba { tint[0], tint[1], tint[2], alpha * 0.9f };
    const Rgba faint { colour[0], colour[1], colour[2], alpha * 0.32f };

    const Vec3 c = node.PositionMeters;
    const float r = std::max(0.05f, node.RadiusMeters);
    const Vec3 axis = Normalize(node.Direction);
    const Vec3 u = AnyPerpendicular(axis);
    const Vec3 v = Cross(axis, u);
    const bool infinite = (node.Falloff == ForceFalloffOrdinal::Constant);
    const float sign = (node.Strength >= 0.0f) ? 1.0f : -1.0f;
    const float time = simulationSeconds;

    GizmoLineWriter w(outVertices);

    switch (node.ModelCode)
    {
        case ForceModelOrdinal::DirectionalWind:
        {
            const float maxHalf = std::max({ regionHalfExtent[0], regionHalfExtent[1], regionHalfExtent[2] });
            const float length = infinite ? maxHalf * 1.2f : r * 1.6f;
            const float spacing = infinite ? 0.45f : r * 0.35f;
            const float slide = PositiveMod(time * 0.9f, 1.0f);
            for (int i = -1; i <= 1; ++i)
            {
                for (int j = -1; j <= 1; ++j)
                {
                    const Vec3 offset = Add(Scale(u, static_cast<float>(i) * spacing), Scale(v, static_cast<float>(j) * spacing));
                    const Vec3 start = Add(Sub(c, Scale(axis, length * 0.5f)), offset);
                    const Vec3 end = Add(start, Scale(axis, length * sign));
                    const Rgba streak = (i == 0 && j == 0) ? colour : faint;
                    w.Line(start, end, streak);
                    for (int k = 0; k < 3; ++k)
                    {
                        const float t = (static_cast<float>(k) + slide) / 3.0f;
                        w.Chevron(Add(start, Scale(axis, length * sign * t)), Scale(axis, sign), std::min(0.14f, length * 0.06f), streak);
                    }
                }
            }
            if (!infinite) w.DashedCircle(c, axis, r, faint, 32);
            else w.Circle(c, axis, spacing * 1.6f, faint, 24);
            break;
        }
        case ForceModelOrdinal::PointAttractor:
        case ForceModelOrdinal::PointRepulsor:
        {
            const bool inward = (node.ModelCode == ForceModelOrdinal::PointAttractor) == (sign > 0.0f);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 36);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 36);
            w.DashedCircle(c, { 0.0f, 0.0f, 1.0f }, r, faint, 36);
            w.SphereWire(c, r * 0.1f, colour);
            const float phase = PositiveMod(time * 0.6f, 1.0f);
            for (int k = 0; k < 3; ++k)
            {
                const float t = PositiveMod(static_cast<float>(k) / 3.0f + (inward ? (1.0f - phase) : phase), 1.0f);
                const float ringRadius = r * (0.15f + 0.85f * t);
                const Rgba fade { colour[0], colour[1], colour[2], alpha * (0.15f + 0.5f * (1.0f - t)) };
                w.Circle(c, { 0.0f, 1.0f, 0.0f }, ringRadius, fade, 36);
            }
            std::vector<Vec3> dirs = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            for (float sx : { -1.0f, 1.0f })
                for (float sy : { -1.0f, 1.0f })
                    for (float sz : { -1.0f, 1.0f })
                        dirs.push_back(Normalize({ sx, sy, sz }));
            for (const Vec3& d : dirs)
            {
                const Vec3 outer = Add(c, Scale(d, r * 0.92f));
                const Vec3 inner = Add(c, Scale(d, r * 0.22f));
                w.Line(inner, outer, faint);
                w.Chevron(inward ? inner : outer, Scale(d, inward ? -1.0f : 1.0f), r * 0.1f, colour);
            }
            break;
        }
        case ForceModelOrdinal::RotationalColumn:
        {
            const float height = r * 2.0f;
            const Vec3 top = Add(c, Scale(axis, height * 0.5f));
            const Vec3 bottom = Sub(c, Scale(axis, height * 0.5f));
            w.Circle(top, axis, r, faint, 40);
            w.Circle(bottom, axis, r, faint, 40);
            w.DashedCircle(c, axis, r, faint, 32);
            w.Line(bottom, top, faint);
            const float spin = time * 1.2f * sign;
            for (float phase : { 0.0f, kPi })
            {
                std::vector<Vec3> pts;
                pts.reserve(73u);
                for (int i = 0; i <= 72; ++i)
                {
                    const float t = static_cast<float>(i) / 72.0f;
                    const float radius = r * (0.35f + 0.6f * t);
                    const float angle = t * kPi * 5.0f * sign + phase + spin;
                    pts.push_back(Add(bottom, Add(Scale(axis, t * height), Add(Scale(u, std::cos(angle) * radius), Scale(v, std::sin(angle) * radius)))));
                }
                w.Polyline(pts, colour);
                w.Chevron(pts.back(), Sub(pts.back(), pts[pts.size() - 2u]), r * 0.12f, colour);
            }
            w.RotationMarks(top, axis, r, colour, 4, sign);
            if (node.Secondary > 0.01f) w.Arrow(Add(c, Scale(axis, -height * 0.15f)), axis, height * 0.4f, colour);
            break;
        }
        case ForceModelOrdinal::CurlNoiseTurbulence:
        {
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 40);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 40);
            for (int k = 0; k < 6; ++k)
            {
                const float seed = static_cast<float>(k) * 2.39996f;
                const Vec3 centre = Add(c, { std::sin(seed) * r * 0.5f, std::cos(seed * 1.7f) * r * 0.45f, std::sin(seed * 0.9f + 1.0f) * r * 0.5f });
                const Vec3 swirlAxis = Normalize({ std::cos(seed * 1.3f), std::sin(seed * 0.7f) + 0.4f, std::cos(seed * 2.1f) });
                const auto pts = w.Helix(centre, swirlAxis, r * 0.16f, r * 0.18f, 1.6f, colour, time * 1.5f + seed, 28);
                w.Chevron(pts.back(), Sub(pts.back(), pts[pts.size() - 2u]), r * 0.06f, colour);
            }
            break;
        }
        case ForceModelOrdinal::DragBaffle:
        {
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 36);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 36);
            w.DashedCircle(c, { 0.0f, 0.0f, 1.0f }, r, faint, 36);
            for (int i = -2; i <= 2; ++i)
            {
                const float y = static_cast<float>(i) * r * 0.3f;
                for (float z : { -r * 0.35f, 0.0f, r * 0.35f })
                {
                    w.Line(Add(c, { -r * 0.45f, y, z }), Add(c, { r * 0.45f, y, z }), faint);
                }
            }
            for (int i = -1; i <= 1; ++i)
            {
                const float y = static_cast<float>(i) * r * 0.45f;
                w.Arrow(Add(c, { -r * 1.0f, y, 0.0f }), { 1.0f, 0.0f, 0.0f }, r * 0.5f, colour);
                w.Arrow(Add(c, { r * 0.55f, y, 0.0f }), { 1.0f, 0.0f, 0.0f }, r * 0.22f, colour);
                w.Chevron(Add(c, { r * 0.2f, y, 0.0f }), { 1.0f, 0.0f, 0.0f }, r * 0.07f, faint);
                w.Chevron(Add(c, { r * 0.34f, y, 0.0f }), { 1.0f, 0.0f, 0.0f }, r * 0.06f, faint);
            }
            break;
        }
        case ForceModelOrdinal::RadialBurst:
        {
            const float period = std::max(node.Frequency, 0.05f);
            const float phase = PositiveMod(time / period, 1.0f);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 40);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 40);
            for (int k = 0; k < 2; ++k)
            {
                const float t = PositiveMod(phase + static_cast<float>(k) * 0.5f, 1.0f);
                const float shell = r * (0.08f + 0.92f * t);
                const Rgba fade { colour[0], colour[1], colour[2], alpha * (1.0f - t) * 0.9f };
                w.Circle(c, { 0.0f, 1.0f, 0.0f }, shell, fade, 48);
                w.Circle(c, { 1.0f, 0.0f, 0.0f }, shell, fade, 48);
                w.Circle(c, { 0.0f, 0.0f, 1.0f }, shell, fade, 48);
            }
            for (int i = 0; i < 12; ++i)
            {
                const float theta = (static_cast<float>(i) / 12.0f) * kTwoPi;
                const Vec3 d = Normalize({ std::cos(theta), std::sin(theta * 2.0f) * 0.6f, std::sin(theta) });
                const float spike = r * (0.25f + 0.2f * (static_cast<float>(i % 3) / 2.0f));
                w.Line(Add(c, Scale(d, r * 0.05f)), Add(c, Scale(d, spike)), colour);
                w.Chevron(Add(c, Scale(d, spike)), d, r * 0.06f, colour);
            }
            break;
        }
        case ForceModelOrdinal::ThermalUpdraft:
        {
            const float height = r * 2.6f;
            const Vec3 bottom = Sub(c, Scale(axis, height * 0.5f));
            for (float k : { 0.0f, 0.5f, 1.0f })
            {
                w.Circle(Add(bottom, Scale(axis, k * height)), axis, r, (k == 0.5f) ? faint : colour, 36);
            }
            for (int i = 0; i < 4; ++i)
            {
                const float theta = (static_cast<float>(i) / 4.0f) * kTwoPi;
                const Vec3 foot = Add(bottom, Add(Scale(u, std::cos(theta) * r), Scale(v, std::sin(theta) * r)));
                w.Line(foot, Add(foot, Scale(axis, height)), faint);
            }
            const float rise = PositiveMod(time * 0.7f, 1.0f);
            for (int i = 0; i < 5; ++i)
            {
                const float theta = (static_cast<float>(i) / 5.0f) * kTwoPi + 0.4f;
                const Vec3 base = Add(bottom, Add(Scale(u, std::cos(theta) * r * 0.55f), Scale(v, std::sin(theta) * r * 0.55f)));
                std::vector<Vec3> pts;
                pts.reserve(25u);
                for (int k = 0; k <= 24; ++k)
                {
                    const float t = static_cast<float>(k) / 24.0f;
                    const float wobble = std::sin(t * kPi * 3.0f + rise * kTwoPi + static_cast<float>(i)) * r * 0.12f;
                    const float axial = t * height * sign + (sign > 0.0f ? 0.0f : height);
                    pts.push_back(Add(base, Add(Scale(axis, axial), Scale(u, wobble))));
                }
                w.Polyline(pts, colour);
                w.Chevron(pts.back(), Scale(axis, sign), r * 0.12f, colour);
            }
            break;
        }
        case ForceModelOrdinal::AxisAttractor:
        {
            const float length = r * 3.0f;
            const Vec3 bottom = Sub(c, Scale(axis, length * 0.5f));
            w.Line(bottom, Add(bottom, Scale(axis, length)), colour);
            w.Chevron(Add(bottom, Scale(axis, length)), axis, r * 0.1f, faint);
            w.Chevron(bottom, Scale(axis, -1.0f), r * 0.1f, faint);
            const float pull = PositiveMod(time * 0.6f, 1.0f);
            for (float k : { 0.15f, 0.5f, 0.85f })
            {
                const Vec3 ringCentre = Add(bottom, Scale(axis, k * length));
                w.DashedCircle(ringCentre, axis, r, faint, 32);
                const float animR = r * (sign > 0.0f ? (1.0f - pull * 0.85f) : (0.15f + pull * 0.85f));
                w.Circle(ringCentre, axis, animR, { colour[0], colour[1], colour[2], alpha * 0.4f }, 32);
                for (int i = 0; i < 6; ++i)
                {
                    const float theta = (static_cast<float>(i) / 6.0f) * kTwoPi;
                    const Vec3 d = Add(Scale(u, std::cos(theta)), Scale(v, std::sin(theta)));
                    const Vec3 outer = Add(ringCentre, Scale(d, r * 0.95f));
                    const Vec3 inner = Add(ringCentre, Scale(d, r * 0.1f));
                    w.Line(inner, outer, faint);
                    w.Chevron(sign > 0.0f ? inner : outer, Scale(d, sign > 0.0f ? -1.0f : 1.0f), r * 0.09f, colour);
                }
            }
            break;
        }
        case ForceModelOrdinal::RotationalRing:
        {
            const float tube = std::max(0.05f, node.Frequency);
            w.Circle(c, axis, r, colour, 64);
            w.Circle(c, axis, r + tube, faint, 64);
            w.Circle(c, axis, std::max(0.01f, r - tube), faint, 64);
            for (int i = 0; i < 8; ++i)
            {
                const float theta = (static_cast<float>(i) / 8.0f) * kTwoPi;
                const Vec3 radialDir = Add(Scale(u, std::cos(theta)), Scale(v, std::sin(theta)));
                const Vec3 ringPoint = Add(c, Scale(radialDir, r));
                const Vec3 ringTangent = Cross(axis, radialDir);
                w.Circle(ringPoint, ringTangent, tube, faint, 20);
                w.RotationMarks(ringPoint, ringTangent, tube, colour, 2, sign);
            }
            w.Arrow(c, Scale(axis, sign), r * 1.1f, colour);
            break;
        }
        case ForceModelOrdinal::Orbit:
        {
            w.Circle(c, axis, r, colour, 56);
            w.Circle(c, axis, r * 0.6f, faint, 40);
            w.RotationMarks(c, axis, r, colour, 6, sign);
            w.RotationMarks(c, axis, r * 0.6f, colour, 3, sign);
            w.SphereWire(c, r * 0.08f, colour);
            const float orbitPhase = time * 1.4f * sign;
            const Vec3 satellite = Add(c, Add(Scale(u, std::cos(orbitPhase) * r * 0.8f), Scale(v, std::sin(orbitPhase) * r * 0.8f)));
            w.SphereWire(satellite, r * 0.05f, colour);
            if (node.Secondary > 0.01f) w.Arrow(satellite, Sub(c, satellite), r * 0.3f, faint);
            break;
        }
        case ForceModelOrdinal::Containment:
        {
            w.SphereWire(c, r, colour);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r * (1.0f - node.Secondary), faint, 36);
            const float breath = r * (1.08f + 0.06f * std::sin(time * 2.5f));
            for (float sx : { -1.0f, 1.0f })
                for (float sy : { -1.0f, 1.0f })
                    for (float sz : { -1.0f, 1.0f })
                    {
                        const Vec3 d = Normalize({ sx, sy, sz });
                        w.Line(Add(c, Scale(d, breath + r * 0.25f)), Add(c, Scale(d, breath)), faint);
                        w.Chevron(Add(c, Scale(d, breath)), Scale(d, -1.0f), r * 0.09f, colour);
                    }
            break;
        }
        case ForceModelOrdinal::ShockwaveShell:
        {
            const float period = std::max(node.Frequency, 0.05f);
            const float phase = PositiveMod((time - node.Tertiary) / period, 1.0f);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 48);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 48);
            const float shell = r * std::max(0.03f, phase);
            const float thick = std::max(0.02f, node.Secondary) * r;
            for (int k : { -1, 0, 1 })
            {
                const float radius = std::max(0.01f, shell + static_cast<float>(k) * thick * 0.5f);
                const Rgba fade { colour[0], colour[1], colour[2], alpha * (k == 0 ? 0.95f : 0.35f) * (1.0f - phase * 0.6f) };
                w.Circle(c, { 0.0f, 1.0f, 0.0f }, radius, fade, 56);
                w.Circle(c, { 1.0f, 0.0f, 0.0f }, radius, fade, 56);
                w.Circle(c, { 0.0f, 0.0f, 1.0f }, radius, fade, 56);
            }
            w.SphereWire(c, r * 0.05f, colour);
            break;
        }
        case ForceModelOrdinal::Implosion:
        {
            const float period = std::max(node.Frequency, 0.05f);
            const float phase = PositiveMod((time - node.Tertiary) / period, 1.0f);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 40);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 40);
            for (int k = 0; k < 3; ++k)
            {
                const float t = PositiveMod(phase + static_cast<float>(k) / 3.0f, 1.0f);
                const float radius = r * (1.0f - t) + r * 0.05f;
                const Rgba fade { colour[0], colour[1], colour[2], alpha * (0.2f + 0.7f * t) };
                w.Circle(c, { 0.0f, 1.0f, 0.0f }, radius, fade, 48);
                w.Circle(c, { 0.0f, 0.0f, 1.0f }, radius, fade, 48);
            }
            const Vec3 axes[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            for (const Vec3& d : axes)
            {
                w.Line(Add(c, Scale(d, r)), Add(c, Scale(d, r * 0.15f)), faint);
                w.Chevron(Add(c, Scale(d, r * 0.15f)), Scale(d, -1.0f), r * 0.09f, colour);
            }
            break;
        }
        case ForceModelOrdinal::DirectionalBurst:
        {
            const float period = std::max(node.Frequency, 0.05f);
            const float phase = PositiveMod((time - node.Tertiary) / period, 1.0f);
            const float pulse = std::exp(-phase / std::max(node.Secondary, 0.01f));
            w.DashedCircle(c, axis, r, faint, 36);
            w.Circle(c, axis, r * 0.35f, faint, 24);
            w.Line(Sub(c, Scale(axis, r * 0.6f)), Add(c, Scale(axis, r * 0.6f)), faint);
            const float reach = r * (0.5f + 1.1f * (1.0f - std::exp(-phase * 4.0f)));
            for (int i = 0; i < 5; ++i)
            {
                const float theta = (static_cast<float>(i) / 5.0f) * kTwoPi;
                const Vec3 offset = (i == 0)
                    ? Vec3 { 0.0f, 0.0f, 0.0f }
                    : Add(Scale(u, std::cos(theta) * r * 0.3f), Scale(v, std::sin(theta) * r * 0.3f));
                w.Arrow(Add(c, offset), Scale(axis, sign), reach, { colour[0], colour[1], colour[2], alpha * (0.25f + 0.75f * pulse) });
            }
            break;
        }
        case ForceModelOrdinal::BrownianJitter:
        {
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 36);
            w.DashedCircle(c, { 1.0f, 0.0f, 0.0f }, r, faint, 36);
            const float roll = std::floor(time * std::max(node.Secondary, 0.1f));
            for (int k = 0; k < 10; ++k)
            {
                Vec3 point = Add(c, { std::sin(static_cast<float>(k) * 2.1f) * r * 0.5f, std::cos(static_cast<float>(k) * 1.3f) * r * 0.5f, std::sin(static_cast<float>(k) * 0.7f + 2.0f) * r * 0.5f });
                std::vector<Vec3> pts { point };
                for (int step = 0; step < 5; ++step)
                {
                    const float seed = static_cast<float>(k * 31 + step * 7) + roll * 13.0f;
                    const Vec3 kick { std::sin(seed * 12.9898f), std::sin(seed * 78.233f), std::sin(seed * 37.719f) };
                    point = Add(point, Scale(kick, r * 0.08f));
                    pts.push_back(point);
                }
                w.Polyline(pts, colour);
            }
            break;
        }
        case ForceModelOrdinal::DirectedStream:
        {
            const float length = r * 1.8f;
            w.Circle(Sub(c, Scale(axis, length * 0.5f)), axis, r, faint, 36);
            w.Circle(Add(c, Scale(axis, length * 0.5f)), axis, r, colour, 36);
            const float slide = PositiveMod(time * std::max(node.Frequency, 0.3f) * 0.3f, 1.0f);
            for (int i = 0; i < 7; ++i)
            {
                const float theta = (static_cast<float>(i) / 7.0f) * kTwoPi;
                const float radial = (i == 6) ? 0.0f : r * 0.65f;
                const Vec3 start = Add(Sub(c, Scale(axis, length * 0.5f)), Add(Scale(u, std::cos(theta) * radial), Scale(v, std::sin(theta) * radial)));
                const Rgba lineCol = (i == 6) ? colour : faint;
                w.Line(start, Add(start, Scale(axis, length)), lineCol);
                for (int k = 0; k < 4; ++k)
                {
                    w.Chevron(Add(start, Scale(axis, length * ((static_cast<float>(k) + slide) / 4.0f))), axis, r * 0.09f, lineCol);
                }
            }
            break;
        }
        case ForceModelOrdinal::WindShear:
        {
            const float height = r * 2.0f;
            const Vec3 bottom = Sub(c, Scale(axis, height * 0.5f));
            w.Line(bottom, Add(bottom, Scale(axis, height)), faint);
            constexpr int kLayers = 7;
            for (int i = 0; i < kLayers; ++i)
            {
                const float h = static_cast<float>(i) / static_cast<float>(kLayers - 1) * 2.0f - 1.0f;
                const float angle = h * node.Secondary * kPi;
                const Vec3 heading = Add(Scale(u, std::cos(angle)), Scale(v, std::sin(angle)));
                const Vec3 centre = Add(bottom, Scale(axis, (h * 0.5f + 0.5f) * height));
                w.Arrow(Sub(centre, Scale(heading, r * 0.6f)), Scale(heading, sign), r * 1.2f, (i == kLayers / 2) ? colour : faint);
                w.DashedCircle(centre, axis, r * 0.25f, faint, 12);
            }
            if (!infinite) w.DashedCircle(c, axis, r, faint, 40);
            break;
        }
        case ForceModelOrdinal::Spin:
        {
            const float spinPhase = time * 1.0f * sign;
            for (float k : { 0.3f, 0.65f, 1.0f })
            {
                w.Circle(c, axis, r * k, (k == 1.0f) ? colour : faint, 48);
                w.RotationMarks(c, axis, r * k, colour, static_cast<int>(std::lround(2.0f + 4.0f * k)), sign);
            }
            for (int i = 0; i < 4; ++i)
            {
                const float theta = (static_cast<float>(i) / 4.0f) * kTwoPi + spinPhase;
                const Vec3 d = Add(Scale(u, std::cos(theta)), Scale(v, std::sin(theta)));
                w.Line(c, Add(c, Scale(d, r)), faint);
            }
            w.Line(Sub(c, Scale(axis, r * 0.5f)), Add(c, Scale(axis, r * 0.5f)), colour);
            break;
        }
        case ForceModelOrdinal::MagneticDipole:
        {
            w.Arrow(Sub(c, Scale(axis, r * 0.35f)), axis, r * 0.7f, colour);
            w.DashedCircle(c, { 0.0f, 1.0f, 0.0f }, r, faint, 40);
            for (int plane = 0; plane < 3; ++plane)
            {
                const float theta0 = (static_cast<float>(plane) / 3.0f) * kPi;
                const Vec3 wDir = Add(Scale(u, std::cos(theta0)), Scale(v, std::sin(theta0)));
                for (float shellL : { r * 0.45f, r * 0.8f, r * 1.15f })
                {
                    for (float flip : { 1.0f, -1.0f })
                    {
                        std::vector<Vec3> pts;
                        pts.reserve(37u);
                        for (int k = 0; k <= 36; ++k)
                        {
                            const float th = 0.12f + (static_cast<float>(k) / 36.0f) * (kPi - 0.24f);
                            const float sinTh = std::sin(th);
                            const float rad = shellL * sinTh * sinTh;
                            if (rad > r) continue;
                            pts.push_back(Add(c, Add(Scale(axis, rad * std::cos(th)), Scale(wDir, rad * sinTh * flip))));
                        }
                        const Rgba col = (std::fabs(shellL - r * 0.8f) < 1.0e-4f) ? colour : faint;
                        w.Polyline(pts, col);
                        if (pts.size() > 2u)
                        {
                            const size_t mid = pts.size() / 2u;
                            w.Chevron(pts[mid], Scale(Sub(pts[mid - 1u], pts[mid]), sign), r * 0.07f, colour);
                        }
                    }
                }
            }
            break;
        }
    }
}

void BuildVisibleForceGizmoVertices(
    const ForceFieldRegister& forceRegister,
    const std::string_view selectedNodeId,
    const float opacity,
    const double simulationSeconds,
    const std::array<float, 3>& regionHalfExtent,
    std::vector<ForceGizmoVertex>& outVertices)
{
    outVertices.clear();
    for (const ManagedForceEntry& entry : forceRegister.QueryEntries())
    {
        if (!entry.Node.Visible) continue;
        const ForceModelNode keyed = EvaluateKeyedForceNode(entry, simulationSeconds);
        const bool isSelected = (!selectedNodeId.empty() && keyed.NodeId == selectedNodeId);
        AppendForceGizmoLineVertices(
            keyed,
            isSelected,
            opacity,
            static_cast<float>(simulationSeconds),
            regionHalfExtent,
            outVertices);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FORCEFIELDGPUUPLOADER
//------------------------------------------------------------------------------------------------------------------------

ForceFieldGpuUploader::~ForceFieldGpuUploader() noexcept
{
#if FRONTIER_FORCEFIELD_HAS_VULKAN
    TeardownUploader();
#endif
}

#if FRONTIER_FORCEFIELD_HAS_VULKAN

bool ForceFieldGpuUploader::InitializeUploader(GeometricRaster::VulkanInterchange* const boundDevice) noexcept
{
    TeardownUploader();
    if (boundDevice == nullptr || !boundDevice->Ready())
    {
        return false;
    }
    BoundDevice = boundDevice;
    const uint64_t forceBytes = static_cast<uint64_t>(kMaximumGpuForceModels) * sizeof(ForceModelGpuRecord);
    const uint64_t gizmoBytes = static_cast<uint64_t>(kMaxGizmoLineVertices) * sizeof(ForceGizmoVertex);

    for (uint32_t i = 0u; i < kRingSlotCount; ++i)
    {
        ForceSsboRing[i] = BoundDevice->AllocateBuffer(
            forceBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        GizmoVertexRing[i] = BoundDevice->AllocateBuffer(
            gizmoBytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ForceSsboRing[i].BufferHandle == VK_NULL_HANDLE || GizmoVertexRing[i].BufferHandle == VK_NULL_HANDLE)
        {
            TeardownUploader();
            return false;
        }
    }
    return true;
}

void ForceFieldGpuUploader::TeardownUploader() noexcept
{
    if (BoundDevice != nullptr)
    {
        for (uint32_t i = 0u; i < kRingSlotCount; ++i)
        {
            BoundDevice->ReleaseBuffer(ForceSsboRing[i]);
            BoundDevice->ReleaseBuffer(GizmoVertexRing[i]);
        }
    }
    BoundDevice              = nullptr;
    ActiveSlotIndex          = 0u;
    UploadedForceCount       = 0u;
    UploadedGizmoVertexCount = 0u;
    LastUploadedGeneration   = 0ull;
}

uint32_t ForceFieldGpuUploader::UploadLatestForceBuffer(const ForceFieldRegister& forceRegister) noexcept
{
    auto lease = forceRegister.AcquirePublishedReading();
    if (!lease.Valid()) return 0u;
    const auto& reading = lease.InspectPayload();
    const uint64_t generation = lease.InspectHeader().GenerationSequence;

    auto& slot = ForceSsboRing[ActiveSlotIndex];
    if (slot.HostAddress != nullptr && reading.ActiveCount > 0u)
    {
        const size_t copyBytes = static_cast<size_t>(reading.ActiveCount) * sizeof(ForceModelGpuRecord);
        std::memcpy(slot.HostAddress, reading.Records.data(), std::min(copyBytes, static_cast<size_t>(slot.ByteCapacity)));
    }
    UploadedForceCount     = reading.ActiveCount;
    LastUploadedGeneration = generation;
    return UploadedForceCount;
}

uint32_t ForceFieldGpuUploader::UploadGizmoVertices(const std::span<const ForceGizmoVertex> vertices) noexcept
{
    const uint32_t clampedCount = static_cast<uint32_t>(std::min<size_t>(vertices.size(), kMaxGizmoLineVertices));
    auto& slot = GizmoVertexRing[ActiveSlotIndex];
    if (slot.HostAddress != nullptr && clampedCount > 0u)
    {
        const size_t copyBytes = static_cast<size_t>(clampedCount) * sizeof(ForceGizmoVertex);
        std::memcpy(slot.HostAddress, vertices.data(), std::min(copyBytes, static_cast<size_t>(slot.ByteCapacity)));
    }
    UploadedGizmoVertexCount = clampedCount;
    return UploadedGizmoVertexCount;
}

#endif

} // namespace Frontier::SimulationSpace::ForceFields
