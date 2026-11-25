/*============================================================================================================================================
                                                       CARRIERSOLIDREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 procedural 3D carrier solids (18 built-in + CustomObject import), OBJ/STL/FCT1 loaders, RGBA32F texel packing & anchor sampling
// Reference: Experimental/FluidEditor/Engine/ObjectMeshCatalog.js L1-L1167

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/CarrierSolidRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

using Vec3d = std::array<double, 3>;

constexpr std::array<std::string_view, 19u> kAllSolidNames {{
    "Cube", "Sphere", "Coin", "CrystalShard", "HexPrism", "StarPoly", "UtahTeapot", "SportsCar", "ObsidianHand",
    "SculptedBust", "TorusKnot", "PrecisionGear", "CrystalCluster", "Archway", "FacetedSpire",
    "WingedButterfly", "WingedBird", "FishBody", "CustomObject"
}};

constexpr std::array<std::string_view, 18u> kBuiltInSolidNames {{
    "Cube", "Sphere", "Coin", "CrystalShard", "HexPrism", "StarPoly", "UtahTeapot", "SportsCar", "ObsidianHand",
    "SculptedBust", "TorusKnot", "PrecisionGear", "CrystalCluster", "Archway", "FacetedSpire",
    "WingedButterfly", "WingedBird", "FishBody"
}};

constexpr std::array<std::string_view, 19u> kSolidLabels {{
    "Cube", "Sphere", "Coin", "Crystal Shard", "Hex Prism", "Star Polyhedron", "Utah Teapot", "Sports Car", "Obsidian Hand",
    "Sculpted Bust", "Torus Knot", "Precision Gear", "Crystal Cluster", "Archway", "Faceted Spire",
    "Winged Butterfly", "Winged Bird", "Fish Body", "Custom Object..."
}};

[[nodiscard]] Vec3d Add3(const Vec3d& a, const Vec3d& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
[[nodiscard]] Vec3d Sub3(const Vec3d& a, const Vec3d& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
[[nodiscard]] Vec3d Scale3(const Vec3d& a, const double k) noexcept { return { a[0] * k, a[1] * k, a[2] * k }; }
[[nodiscard]] Vec3d Cross3(const Vec3d& a, const Vec3d& b) noexcept
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
[[nodiscard]] double Length3(const Vec3d& a) noexcept
{
    return std::hypot(a[0], a[1], a[2]);
}
[[nodiscard]] Vec3d Unit3(const Vec3d& a) noexcept
{
    const double len = Length3(a);
    return len > 1.0e-9 ? Vec3d { a[0] / len, a[1] / len, a[2] / len } : Vec3d { 0.0, 1.0, 0.0 };
}

struct RawTriangle
{
    Vec3d A {};
    Vec3d B {};
    Vec3d C {};
};

class CarrierSolidBuilder
{
public:
    explicit CarrierSolidBuilder(std::string_view name) : Name(name) {}

    void Triangle(const Vec3d& a, const Vec3d& b, const Vec3d& c)
    {
        const Vec3d normal = Cross3(Sub3(b, a), Sub3(c, a));
        if (Length3(normal) < 1.0e-12) return;
        Raw.push_back(RawTriangle { a, b, c });
    }

    void Quad(const Vec3d& a, const Vec3d& b, const Vec3d& c, const Vec3d& d)
    {
        Triangle(a, b, c);
        Triangle(a, c, d);
    }

    void Box(const double cx, const double cy, const double cz, const double hx, const double hy, const double hz)
    {
        const double x0 = cx - hx, x1 = cx + hx;
        const double y0 = cy - hy, y1 = cy + hy;
        const double z0 = cz - hz, z1 = cz + hz;
        Quad({ x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 });
        Quad({ x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 });
        Quad({ x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 });
        Quad({ x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 });
        Quad({ x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 });
        Quad({ x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 });
    }

    void Sphere(const Vec3d& centre, const double radius, const int rings = 8, const int sides = 12)
    {
        std::vector<std::vector<Vec3d>> grid;
        grid.reserve(static_cast<size_t>(rings) + 1u);
        for (int r = 0; r <= rings; ++r)
        {
            const double v = static_cast<double>(r) / static_cast<double>(rings);
            const double y = std::cos(v * kPi);
            const double ringRadius = std::sin(v * kPi);
            std::vector<Vec3d> row;
            row.reserve(static_cast<size_t>(sides));
            for (int s = 0; s < sides; ++s)
            {
                const double a = (static_cast<double>(s) / static_cast<double>(sides)) * kTwoPi;
                row.push_back({ centre[0] + std::cos(a) * ringRadius * radius, centre[1] + y * radius, centre[2] + std::sin(a) * ringRadius * radius });
            }
            grid.push_back(std::move(row));
        }
        for (int r = 0; r < rings; ++r)
        {
            for (int s = 0; s < sides; ++s)
            {
                const int sn = (s + 1) % sides;
                Quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
            }
        }
    }

    void Lathe(std::span<const std::array<double, 2>> profile, const int segments = 24)
    {
        for (int s = 0; s < segments; ++s)
        {
            const double a0 = (static_cast<double>(s) / static_cast<double>(segments)) * kTwoPi;
            const double a1 = (static_cast<double>(s + 1) / static_cast<double>(segments)) * kTwoPi;
            for (size_t p = 0u; p + 1u < profile.size(); ++p)
            {
                const double r0 = profile[p][0], y0 = profile[p][1];
                const double r1 = profile[p + 1u][0], y1 = profile[p + 1u][1];
                if (r0 < 1.0e-6 && r1 < 1.0e-6) continue;
                const Vec3d p00 { std::cos(a0) * r0, y0, std::sin(a0) * r0 };
                const Vec3d p01 { std::cos(a0) * r1, y1, std::sin(a0) * r1 };
                const Vec3d p10 { std::cos(a1) * r0, y0, std::sin(a1) * r0 };
                const Vec3d p11 { std::cos(a1) * r1, y1, std::sin(a1) * r1 };
                if (r0 < 1.0e-6) Triangle(p00, p01, p11);
                else if (r1 < 1.0e-6) Triangle(p00, p01, p10);
                else Quad(p00, p01, p11, p10);
            }
        }
    }

    void Tube(
        const std::function<Vec3d(double)>& curve,
        const int steps,
        const int sides,
        const std::function<double(double)>& radiusFn)
    {
        std::vector<Vec3d> centres;
        centres.reserve(static_cast<size_t>(steps) + 1u);
        for (int i = 0; i <= steps; ++i)
        {
            centres.push_back(curve(static_cast<double>(i) / static_cast<double>(steps)));
        }
        std::vector<std::vector<Vec3d>> rings(centres.size());
        for (size_t i = 0u; i < centres.size(); ++i)
        {
            // Reference ObjectMeshCatalog.js L135-L149 overwrites rings[i] in-place before iteration i+1,
            // so i > 0 evaluates unit(sub(next, prev)) -> [0, 1, 0] fallback tangent; preserve 1:1 parity.
            const Vec3d tangent = (i == 0u && centres.size() > 1u)
                ? Unit3(Sub3(centres[1u], centres[0u]))
                : Vec3d { 0.0, 1.0, 0.0 };
            const Vec3d helper = (std::fabs(tangent[1]) < 0.95) ? Vec3d { 0.0, 1.0, 0.0 } : Vec3d { 1.0, 0.0, 0.0 };
            const Vec3d normal = Unit3(Cross3(helper, tangent));
            const Vec3d binormal = Cross3(tangent, normal);
            const double r = radiusFn(static_cast<double>(i) / static_cast<double>(steps));
            rings[i].reserve(static_cast<size_t>(sides));
            for (int s = 0; s < sides; ++s)
            {
                const double a = (static_cast<double>(s) / static_cast<double>(sides)) * kTwoPi;
                rings[i].push_back(Add3(centres[i], Add3(Scale3(normal, std::cos(a) * r), Scale3(binormal, std::sin(a) * r))));
            }
        }
        for (size_t i = 0u; i + 1u < rings.size(); ++i)
        {
            const auto& a = rings[i];
            const auto& b = rings[i + 1u];
            for (int s = 0; s < sides; ++s)
            {
                const int sn = (s + 1) % sides;
                Quad(a[s], a[sn], b[sn], b[s]);
            }
        }
    }

    void Extrude(std::span<const std::array<double, 2>> polygon, const double halfHeight)
    {
        std::vector<Vec3d> top;
        std::vector<Vec3d> bottom;
        top.reserve(polygon.size());
        bottom.reserve(polygon.size());
        for (const auto& pt : polygon)
        {
            top.push_back({ pt[0], halfHeight, pt[1] });
            bottom.push_back({ pt[0], -halfHeight, pt[1] });
        }
        const Vec3d centreTop { 0.0, halfHeight, 0.0 };
        const Vec3d centreBottom { 0.0, -halfHeight, 0.0 };
        for (size_t i = 0u; i < polygon.size(); ++i)
        {
            const size_t n = (i + 1u) % polygon.size();
            Quad(bottom[i], bottom[n], top[n], top[i]);
            Triangle(centreTop, top[i], top[n]);
            Triangle(centreBottom, bottom[n], bottom[i]);
        }
    }

    std::string              Name {};
    std::vector<RawTriangle> Raw  {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILT-IN SOLIDS
//------------------------------------------------------------------------------------------------------------------------

CarrierSolidBuilder BuildCube()
{
    CarrierSolidBuilder b("Cube");
    b.Box(0.0, 0.0, 0.0, 0.72, 0.72, 0.72);
    return b;
}

CarrierSolidBuilder BuildSphere()
{
    CarrierSolidBuilder b("Sphere");
    b.Sphere({ 0.0, 0.0, 0.0 }, 1.0, 18, 26);
    return b;
}

CarrierSolidBuilder BuildCoin()
{
    CarrierSolidBuilder b("Coin");
    constexpr int kSegments = 40;
    constexpr double kThickness = 0.10;
    std::vector<std::array<double, 2>> rim;
    rim.reserve(kSegments);
    for (int s = 0; s < kSegments; ++s)
    {
        const double a = (static_cast<double>(s) / static_cast<double>(kSegments)) * kTwoPi;
        rim.push_back({ std::cos(a), std::sin(a) });
    }
    for (int s = 0; s < kSegments; ++s)
    {
        const int n = (s + 1) % kSegments;
        const double x0 = rim[s][0], z0 = rim[s][1];
        const double x1 = rim[n][0], z1 = rim[n][1];
        b.Quad({ x0, -kThickness, z0 }, { x1, -kThickness, z1 }, { x1, kThickness, z1 }, { x0, kThickness, z0 });
        for (const double side : { 1.0, -1.0 })
        {
            const double y = side * kThickness;
            b.Quad({ x0 * 0.86, y, z0 * 0.86 }, { x1 * 0.86, y, z1 * 0.86 }, { x1, y, z1 }, { x0, y, z0 });
            b.Triangle({ 0.0, y, 0.0 }, { x0 * 0.86, y, z0 * 0.86 }, { x1 * 0.86, y, z1 * 0.86 });
        }
    }
    constexpr int kBossSegments = 18;
    for (const double side : { 1.0, -1.0 })
    {
        const double y = side * (kThickness + 0.03);
        for (int s = 0; s < kBossSegments; ++s)
        {
            const double a0 = (static_cast<double>(s) / static_cast<double>(kBossSegments)) * kTwoPi;
            const double a1 = (static_cast<double>(s + 1) / static_cast<double>(kBossSegments)) * kTwoPi;
            constexpr double r = 0.34;
            b.Quad({ 0.0, y, 0.0 }, { std::cos(a0) * r, y, std::sin(a0) * r }, { std::cos(a1) * r, y, std::sin(a1) * r }, { 0.0, y, 0.0 });
            b.Quad({ std::cos(a0) * r, y, std::sin(a0) * r }, { std::cos(a0) * r, side * kThickness, std::sin(a0) * r },
                   { std::cos(a1) * r, side * kThickness, std::sin(a1) * r }, { std::cos(a1) * r, y, std::sin(a1) * r });
        }
    }
    return b;
}

CarrierSolidBuilder BuildCrystalShard()
{
    CarrierSolidBuilder b("CrystalShard");
    constexpr int kSides = 6;
    constexpr double kWaist = 0.34, kHipY = 0.05;
    std::array<Vec3d, kSides> hip {};
    for (int s = 0; s < kSides; ++s)
    {
        const double a = (static_cast<double>(s) / static_cast<double>(kSides)) * kTwoPi;
        hip[s] = { std::cos(a) * kWaist, kHipY, std::sin(a) * kWaist };
    }
    const Vec3d tip { 0.0, 1.0, 0.0 };
    const Vec3d base { 0.0, -1.0, 0.0 };
    for (int s = 0; s < kSides; ++s)
    {
        const int n = (s + 1) % kSides;
        b.Triangle(hip[s], hip[n], tip);
        b.Triangle(hip[n], hip[s], base);
    }
    return b;
}

CarrierSolidBuilder BuildHexPrism()
{
    CarrierSolidBuilder b("HexPrism");
    std::array<std::array<double, 2>, 6u> poly {};
    for (int s = 0; s < 6; ++s)
    {
        const double a = (static_cast<double>(s) / 6.0) * kTwoPi + kPi / 6.0;
        poly[s] = { std::cos(a) * 0.78, std::sin(a) * 0.78 };
    }
    b.Extrude(poly, 0.62);
    return b;
}

CarrierSolidBuilder BuildStarPoly()
{
    CarrierSolidBuilder b("StarPoly");
    const std::array<Vec3d, 6u> core {{
        Scale3({ 1, 0, 0 }, 0.52), Scale3({ -1, 0, 0 }, 0.52), Scale3({ 0, 1, 0 }, 0.52),
        Scale3({ 0, -1, 0 }, 0.52), Scale3({ 0, 0, 1 }, 0.52), Scale3({ 0, 0, -1 }, 0.52)
    }};
    constexpr std::array<std::array<int, 3>, 8u> faces {{
        { 0, 2, 4 }, { 4, 2, 1 }, { 1, 2, 5 }, { 5, 2, 0 },
        { 4, 3, 0 }, { 1, 3, 4 }, { 5, 3, 1 }, { 0, 3, 5 }
    }};
    for (const auto& f : faces)
    {
        const Vec3d& va = core[f[0]], & vb = core[f[1]], & vc = core[f[2]];
        const Vec3d mid = Scale3(Add3(Add3(va, vb), vc), 1.0 / 3.0);
        const Vec3d normal = Unit3(Cross3(Sub3(vb, va), Sub3(vc, va)));
        const Vec3d apex = Add3(mid, Scale3(normal, 0.88));
        b.Triangle(va, vb, apex);
        b.Triangle(vb, vc, apex);
        b.Triangle(vc, va, apex);
    }
    return b;
}

CarrierSolidBuilder BuildUtahTeapot()
{
    CarrierSolidBuilder b("UtahTeapot");
    constexpr std::array<std::array<double, 2>, 15u> body {{
        { 0.0, -0.58 }, { 0.46, -0.58 }, { 0.58, -0.45 }, { 0.76, -0.18 }, { 0.78, 0.08 },
        { 0.64, 0.34 }, { 0.46, 0.48 }, { 0.50, 0.54 }, { 0.38, 0.55 }, { 0.36, 0.62 },
        { 0.14, 0.69 }, { 0.06, 0.72 }, { 0.11, 0.80 }, { 0.08, 0.87 }, { 0.0, 0.89 }
    }};
    b.Lathe(body, 26);
    b.Tube(
        [](const double u) -> Vec3d {
            return { 0.64 + u * 0.58 + std::sin(u * kPi) * 0.08, -0.22 + u * 0.78 - std::sin(u * kPi * 1.15) * 0.14, 0.0 };
        },
        14, 10,
        [](const double u) -> double { return 0.16 * (1.0 - u * 0.56); });
    b.Tube(
        [](const double u) -> Vec3d {
            const double a = -kPi * 0.52 + u * kPi * 1.18;
            return { -0.64 - std::cos(a) * 0.44, 0.06 + std::sin(a) * 0.42, 0.0 };
        },
        16, 10,
        [](const double u) -> double { return 0.072 - u * 0.02; });
    return b;
}

CarrierSolidBuilder BuildSportsCar()
{
    CarrierSolidBuilder b("SportsCar");
    struct Station { double x, yBot, yTop, yCab, wBot, wTop, wCab; };
    constexpr std::array<Station, 7u> stations {{
        { -1.04, -0.20, 0.16, 0.18, 0.44, 0.40, 0.34 },
        { -0.82, -0.22, 0.20, 0.28, 0.49, 0.44, 0.34 },
        { -0.45, -0.22, 0.21, 0.44, 0.48, 0.44, 0.32 },
        {  0.02, -0.22, 0.20, 0.45, 0.47, 0.43, 0.31 },
        {  0.42, -0.22, 0.18, 0.22, 0.48, 0.43, 0.34 },
        {  0.80, -0.21, 0.12, 0.13, 0.47, 0.41, 0.34 },
        {  1.06, -0.18, 0.02, 0.03, 0.40, 0.35, 0.28 },
    }};
    std::array<std::array<Vec3d, 6u>, 7u> rings {};
    for (size_t i = 0u; i < 7u; ++i)
    {
        const Station& st = stations[i];
        rings[i] = {{
            { st.x, st.yBot, -st.wBot }, { st.x, st.yTop, -st.wTop }, { st.x, st.yCab, -st.wCab },
            { st.x, st.yCab,  st.wCab }, { st.x, st.yTop,  st.wTop }, { st.x, st.yBot,  st.wBot }
        }};
    }
    for (size_t i = 0u; i < 6u; ++i)
    {
        for (size_t k = 0u; k < 6u; ++k)
        {
            const size_t kn = (k + 1u) % 6u;
            b.Quad(rings[i][k], rings[i][kn], rings[i + 1u][kn], rings[i + 1u][k]);
        }
    }
    const std::array<std::pair<size_t, bool>, 2u> caps {{ { 0u, true }, { 6u, false } }};
    for (const auto& [ringIdx, flip] : caps)
    {
        const auto& ring = rings[ringIdx];
        const Vec3d centre { ring[0][0], (ring[0][1] + ring[2][1]) * 0.5, 0.0 };
        for (size_t k = 0u; k < 6u; ++k)
        {
            const size_t kn = (k + 1u) % 6u;
            if (flip) b.Triangle(centre, ring[kn], ring[k]);
            else b.Triangle(centre, ring[k], ring[kn]);
        }
    }
    b.Box(-0.96, 0.30, 0.0, 0.11, 0.025, 0.44);
    b.Box(-0.94, 0.22, -0.24, 0.04, 0.07, 0.02);
    b.Box(-0.94, 0.22,  0.24, 0.04, 0.07, 0.02);
    constexpr std::array<std::array<double, 2>, 4u> wheels {{ { -0.64, -0.46 }, { -0.64, 0.46 }, { 0.64, -0.46 }, { 0.64, 0.46 } }};
    for (const auto& w : wheels)
    {
        const double wx = w[0], wz = w[1];
        constexpr int kSegs = 14;
        constexpr double kWr = 0.20, kHw = 0.08;
        for (int s = 0; s < kSegs; ++s)
        {
            const double a0 = (static_cast<double>(s) / static_cast<double>(kSegs)) * kTwoPi;
            const double a1 = (static_cast<double>(s + 1) / static_cast<double>(kSegs)) * kTwoPi;
            const Vec3d p0i { wx + std::cos(a0) * kWr, -0.18 + std::sin(a0) * kWr, wz - kHw };
            const Vec3d p1i { wx + std::cos(a1) * kWr, -0.18 + std::sin(a1) * kWr, wz - kHw };
            const Vec3d p0o { wx + std::cos(a0) * kWr, -0.18 + std::sin(a0) * kWr, wz + kHw };
            const Vec3d p1o { wx + std::cos(a1) * kWr, -0.18 + std::sin(a1) * kWr, wz + kHw };
            b.Quad(p0i, p0o, p1o, p1i);
            b.Triangle({ wx, -0.18, wz + kHw }, p0o, p1o);
            b.Triangle({ wx, -0.18, wz - kHw }, p1i, p0i);
        }
    }
    return b;
}

CarrierSolidBuilder BuildObsidianHand()
{
    CarrierSolidBuilder b("ObsidianHand");
    b.Box(0.0, -0.72, 0.0, 0.24, 0.26, 0.18);
    b.Box(0.0, -0.32, 0.0, 0.36, 0.20, 0.16);
    struct Finger { Vec3d base; Vec3d dir; double length; double radius; };
    const std::array<Finger, 5u> fingers {{
        { { -0.30, -0.16, 0.02 }, { -0.14, 0.34, 0.02 }, 0.42, 0.075 },
        { { -0.10, -0.12, 0.02 }, { -0.04, 0.40, 0.02 }, 0.50, 0.078 },
        { {  0.10, -0.12, 0.02 }, {  0.05, 0.42, 0.02 }, 0.52, 0.078 },
        { {  0.29, -0.15, 0.02 }, {  0.16, 0.36, 0.02 }, 0.42, 0.068 },
        { {  0.34, -0.36, 0.10 }, {  0.34, 0.10, 0.10 }, 0.36, 0.078 },
    }};
    for (const Finger& finger : fingers)
    {
        Vec3d point = finger.base;
        Vec3d direction = Unit3(finger.dir);
        for (int joint = 0; joint < 3; ++joint)
        {
            const Vec3d segment = Scale3(direction, finger.length / 3.0);
            const Vec3d next = Add3(point, segment);
            const double radius = finger.radius * (1.0 - static_cast<double>(joint) * 0.16);
            b.Tube(
                [&](const double u) -> Vec3d { return Add3(point, Scale3(segment, u)); },
                3, 8,
                [radius](const double u) -> double { return radius * (1.0 - u * 0.14); });
            b.Sphere(next, radius * 0.94, 5, 8);
            point = next;
            direction = Unit3({ direction[0], direction[1] - 0.16, direction[2] });
        }
    }
    return b;
}

CarrierSolidBuilder BuildSculptedBust()
{
    CarrierSolidBuilder b("SculptedBust");
    constexpr int kRings = 22, kSegments = 26;
    std::vector<std::vector<Vec3d>> grid;
    grid.reserve(kRings + 1);
    for (int r = 0; r <= kRings; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(kRings);
        const double y = -0.95 + v * 1.92;
        std::vector<Vec3d> row;
        row.reserve(kSegments);
        for (int s = 0; s < kSegments; ++s)
        {
            const double phi = (static_cast<double>(s) / static_cast<double>(kSegments)) * kTwoPi;
            const double cosPhi = std::cos(phi), sinPhi = std::sin(phi);
            double rx = 0.42, rz = 0.42;
            if (v < 0.24)
            {
                const double t = v / 0.24;
                rx = 0.78 - t * 0.46;
                rz = 0.38 - t * 0.12;
            }
            else if (v < 0.38)
            {
                const double t = (v - 0.24) / 0.14;
                rx = 0.25 + t * 0.04;
                rz = 0.24 + t * 0.05;
            }
            else
            {
                const double h = (v - 0.38) / 0.62;
                const double profile = std::sin(std::pow(h, 0.85) * kPi);
                rx = 0.46 * profile * (h < 0.32 ? (0.78 + h * 0.7) : 1.0);
                rz = 0.54 * profile;
                if (sinPhi > 0.65 && h > 0.26 && h < 0.56)
                {
                    rz += 0.12 * std::pow(std::max(0.0, (sinPhi - 0.65) / 0.35), 2.2) * std::sin(((h - 0.26) / 0.30) * kPi);
                }
                if (sinPhi > 0.35 && h > 0.46 && h < 0.62)
                {
                    rz += 0.045 * std::sin(((h - 0.46) / 0.16) * kPi) * std::max(0.0, sinPhi - 0.35);
                }
                if (sinPhi > 0.4 && h < 0.18)
                {
                    rz += 0.06 * (1.0 - h / 0.18) * sinPhi;
                }
            }
            row.push_back({ cosPhi * rx, y, sinPhi * rz });
        }
        grid.push_back(std::move(row));
    }
    for (int r = 0; r < kRings; ++r)
    {
        for (int s = 0; s < kSegments; ++s)
        {
            const int sn = (s + 1) % kSegments;
            b.Quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    }
    const Vec3d baseCentre { 0.0, -0.95, 0.0 };
    for (int s = 0; s < kSegments; ++s) b.Triangle(baseCentre, grid[0][(s + 1) % kSegments], grid[0][s]);
    return b;
}

CarrierSolidBuilder BuildTorusKnot()
{
    CarrierSolidBuilder b("TorusKnot");
    b.Tube(
        [](const double u) -> Vec3d {
            const double theta = u * kTwoPi;
            const double radius = 0.62 + 0.26 * std::cos(3.0 * theta);
            return { radius * std::cos(2.0 * theta), 0.32 * std::sin(3.0 * theta), radius * std::sin(2.0 * theta) };
        },
        64, 12,
        [](double) -> double { return 0.165; });
    return b;
}

CarrierSolidBuilder BuildPrecisionGear()
{
    CarrierSolidBuilder b("PrecisionGear");
    constexpr int kTeeth = 12;
    std::vector<std::array<double, 2>> polygon;
    polygon.reserve(kTeeth * 4);
    for (int i = 0; i < kTeeth; ++i)
    {
        const double base = (static_cast<double>(i) / static_cast<double>(kTeeth)) * kTwoPi;
        const double step = kTwoPi / static_cast<double>(kTeeth);
        const std::array<std::array<double, 2>, 4u> pts {{
            { 0.68, base + step * 0.06 }, { 0.96, base + step * 0.18 },
            { 0.96, base + step * 0.42 }, { 0.68, base + step * 0.54 }
        }};
        for (const auto& pt : pts) polygon.push_back({ std::cos(pt[1]) * pt[0], std::sin(pt[1]) * pt[0] });
    }
    b.Extrude(polygon, 0.20);
    std::array<std::array<double, 2>, 16u> hub {};
    for (int i = 0; i < 16; ++i)
    {
        const double a = (static_cast<double>(i) / 16.0) * kTwoPi;
        hub[i] = { std::cos(a) * 0.34, std::sin(a) * 0.34 };
    }
    b.Extrude(hub, 0.32);
    return b;
}

CarrierSolidBuilder BuildCrystalCluster()
{
    CarrierSolidBuilder b("CrystalCluster");
    struct ShardSpec { double ox, oz, tiltX, tiltZ, r, h; };
    constexpr std::array<ShardSpec, 5u> shards {{
        {  0.00,  0.00,  0.04, -0.02, 0.30, 1.55 },
        { -0.34,  0.16, -0.34,  0.16, 0.22, 1.15 },
        {  0.36, -0.14,  0.32, -0.18, 0.24, 1.22 },
        {  0.12,  0.36,  0.12,  0.36, 0.19, 0.96 },
        { -0.22, -0.32, -0.22, -0.32, 0.18, 0.92 },
    }};
    for (const ShardSpec& shard : shards)
    {
        constexpr int kSides = 6;
        constexpr double kBaseY = -0.65;
        const double topY = kBaseY + shard.h * 0.72;
        const double tipY = kBaseY + shard.h;
        std::array<Vec3d, kSides> bottom {}, top {};
        for (int s = 0; s < kSides; ++s)
        {
            const double a = (static_cast<double>(s) / static_cast<double>(kSides)) * kTwoPi;
            const double lx = std::cos(a) * shard.r, lz = std::sin(a) * shard.r;
            bottom[s] = { shard.ox + lx, kBaseY, shard.oz + lz };
            top[s] = { shard.ox + lx * 0.84 + shard.tiltX * shard.h * 0.72, topY, shard.oz + lz * 0.84 + shard.tiltZ * shard.h * 0.72 };
        }
        const Vec3d tip { shard.ox + shard.tiltX * shard.h, tipY, shard.oz + shard.tiltZ * shard.h };
        const Vec3d baseCentre { shard.ox, kBaseY, shard.oz };
        for (int s = 0; s < kSides; ++s)
        {
            const int sn = (s + 1) % kSides;
            b.Quad(bottom[s], top[s], top[sn], bottom[sn]);
            b.Triangle(top[s], tip, top[sn]);
            b.Triangle(baseCentre, bottom[sn], bottom[s]);
        }
    }
    return b;
}

CarrierSolidBuilder BuildArchway()
{
    CarrierSolidBuilder b("Archway");
    b.Box(-0.62, -0.35, 0.0, 0.22, 0.65, 0.28);
    b.Box( 0.62, -0.35, 0.0, 0.22, 0.65, 0.28);
    b.Box(-0.62, -0.94, 0.0, 0.28, 0.08, 0.34);
    b.Box( 0.62, -0.94, 0.0, 0.28, 0.08, 0.34);
    constexpr int kSegs = 16;
    constexpr double kInner = 0.40, kOuter = 0.84, kHalfZ = 0.28;
    for (int i = 0; i < kSegs; ++i)
    {
        const double a0 = (static_cast<double>(i) / static_cast<double>(kSegs)) * kPi;
        const double a1 = (static_cast<double>(i + 1) / static_cast<double>(kSegs)) * kPi;
        const Vec3d pi0f { std::cos(a0) * kInner, 0.30 + std::sin(a0) * kInner, kHalfZ };
        const Vec3d pi1f { std::cos(a1) * kInner, 0.30 + std::sin(a1) * kInner, kHalfZ };
        const Vec3d po0f { std::cos(a0) * kOuter, 0.30 + std::sin(a0) * kOuter, kHalfZ };
        const Vec3d po1f { std::cos(a1) * kOuter, 0.30 + std::sin(a1) * kOuter, kHalfZ };
        const Vec3d pi0b { pi0f[0], pi0f[1], -kHalfZ }, pi1b { pi1f[0], pi1f[1], -kHalfZ };
        const Vec3d po0b { po0f[0], po0f[1], -kHalfZ }, po1b { po1f[0], po1f[1], -kHalfZ };
        b.Quad(pi0f, po0f, po1f, pi1f);
        b.Quad(po0b, pi0b, pi1b, po1b);
        b.Quad(po0f, po0b, po1b, po1f);
        b.Quad(pi0b, pi0f, pi1f, pi1b);
    }
    return b;
}

CarrierSolidBuilder BuildFacetedSpire()
{
    CarrierSolidBuilder b("FacetedSpire");
    constexpr int kSegs = 20, kRings = 6;
    std::vector<std::vector<Vec3d>> grid;
    grid.reserve(kRings + 1);
    for (int r = 0; r <= kRings; ++r)
    {
        const double t = static_cast<double>(r) / static_cast<double>(kRings);
        const double radius = (1.0 - t) * 0.85;
        const double y = -0.85 + t * 1.85;
        std::vector<Vec3d> row;
        row.reserve(kSegs);
        for (int s = 0; s < kSegs; ++s)
        {
            const double a = (static_cast<double>(s) / static_cast<double>(kSegs)) * kTwoPi;
            row.push_back({ std::cos(a) * radius, y, std::sin(a) * radius });
        }
        grid.push_back(std::move(row));
    }
    for (int r = 0; r < kRings - 1; ++r)
    {
        for (int s = 0; s < kSegs; ++s)
        {
            const int sn = (s + 1) % kSegs;
            b.Quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    }
    const Vec3d tip { 0.0, 1.0, 0.0 }, baseCentre { 0.0, -0.85, 0.0 };
    for (int s = 0; s < kSegs; ++s)
    {
        const int sn = (s + 1) % kSegs;
        b.Triangle(grid[kRings - 1][s], tip, grid[kRings - 1][sn]);
        b.Triangle(baseCentre, grid[0][sn], grid[0][s]);
    }
    return b;
}

CarrierSolidBuilder BuildWingedButterfly()
{
    CarrierSolidBuilder b("WingedButterfly");
    constexpr std::array<std::array<double, 2>, 6u> body {{
        { 0.0, -0.42 }, { 0.07, -0.30 }, { 0.10, 0.0 }, { 0.08, 0.26 }, { 0.04, 0.44 }, { 0.0, 0.50 }
    }};
    b.Lathe(body, 10);
    struct WingSpec { double spanX, spanY, tipX, tipY, cc; };
    constexpr std::array<WingSpec, 2u> wings {{
        { 0.62,  0.30, 0.86,  0.10,  0.22 },
        { 0.50, -0.20, 0.66, -0.46, -0.16 }
    }};
    for (const double side : { 1.0, -1.0 })
    {
        for (const WingSpec& w : wings)
        {
            const Vec3d rootA { side * 0.04, w.spanY + 0.12,  0.06 };
            const Vec3d rootB { side * 0.04, w.spanY - 0.12, -0.06 };
            const Vec3d midA  { side * w.spanX * 0.72, w.spanY + w.cc * 0.5 + 0.10,  0.04 };
            const Vec3d midB  { side * w.spanX * 0.72, w.spanY + w.cc * 0.5 - 0.10, -0.02 };
            const Vec3d tip   { side * w.tipX, w.tipY, 0.0 };
            b.Quad(rootA, midA, midB, rootB);
            b.Triangle(midA, tip, midB);
        }
    }
    return b;
}

CarrierSolidBuilder BuildWingedBird()
{
    CarrierSolidBuilder b("WingedBird");
    constexpr std::array<std::array<double, 2>, 7u> body {{
        { 0.0, -0.46 }, { 0.05, -0.34 }, { 0.16, -0.05 }, { 0.19, 0.12 },
        { 0.13, 0.30 }, { 0.07, 0.40 }, { 0.0, 0.46 }
    }};
    b.Lathe(body, 14);
    b.Triangle({ 0.0, 0.46, 0.0 }, { 0.05, 0.58, 0.0 }, { -0.05, 0.58, 0.0 });
    for (const double side : { 1.0, -1.0 })
    {
        const Vec3d shoulder { side * 0.12, 0.20,  0.02 };
        const Vec3d wrist    { side * 0.52, 0.28, -0.02 };
        const Vec3d trailing { side * 0.34, 0.06, -0.06 };
        const Vec3d tip      { side * 0.94, 0.10, -0.14 };
        b.Quad(shoulder, wrist, trailing, { side * 0.16, 0.02, 0.0 });
        b.Triangle(wrist, tip, trailing);
        b.Quad(trailing, tip, { side * 0.42, -0.10, -0.16 }, { side * 0.14, -0.14, -0.04 });
    }
    for (const double offset : { -0.05, 0.0, 0.05 })
    {
        b.Quad({ 0.0, -0.40, 0.0 }, { offset, -0.76, 0.0 }, { offset * 1.6, -0.80, 0.0 }, { offset * 0.6, -0.40, 0.0 });
    }
    return b;
}

CarrierSolidBuilder BuildFishBody()
{
    CarrierSolidBuilder b("FishBody");
    constexpr int kRings = 12, kSides = 16;
    std::vector<std::vector<Vec3d>> grid;
    grid.reserve(kRings + 1);
    for (int r = 0; r <= kRings; ++r)
    {
        const double v = static_cast<double>(r) / static_cast<double>(kRings);
        const double x = -0.72 + v * 1.44;
        const double profile = std::sin(std::pow(v, 0.82) * kPi);
        std::vector<Vec3d> row;
        row.reserve(kSides);
        for (int s = 0; s < kSides; ++s)
        {
            const double a = (static_cast<double>(s) / static_cast<double>(kSides)) * kTwoPi;
            row.push_back({ x, std::cos(a) * profile * 0.30, std::sin(a) * profile * 0.13 });
        }
        grid.push_back(std::move(row));
    }
    for (int r = 0; r < kRings; ++r)
    {
        for (int s = 0; s < kSides; ++s)
        {
            const int sn = (s + 1) % kSides;
            b.Quad(grid[r][s], grid[r + 1][s], grid[r + 1][sn], grid[r][sn]);
        }
    }
    b.Triangle({ -0.70, 0.0, 0.0 }, { -1.02,  0.34, 0.0 }, { -0.92, 0.0, 0.0 });
    b.Triangle({ -0.70, 0.0, 0.0 }, { -0.92,  0.0,  0.0 }, { -1.02, -0.34, 0.0 });
    b.Triangle({  0.10, 0.28, 0.0 }, { -0.18, 0.62, 0.0 }, { -0.34,  0.20, 0.0 });
    b.Triangle({  0.02, -0.26, 0.0 }, { -0.24, -0.52, 0.0 }, { -0.34, -0.16, 0.0 });
    for (const double side : { 1.0, -1.0 })
    {
        b.Triangle({ 0.26, 0.02, side * 0.10 }, { -0.06, 0.16, side * 0.34 }, { -0.02, -0.10, side * 0.30 });
    }
    return b;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FINALIZE SOLID
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string FormatVertexKey(const float x, const float y, const float z)
{
    // Exact ECMA-262 Number.prototype.toFixed(4): -0.0 formats as "0.0000", strictly negative tiny values format as "-0.0000"
    auto jsToFixedArg = [](const float v) noexcept -> double { return (v == 0.0f) ? 0.0 : static_cast<double>(v); };
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.4f|%.4f|%.4f",
        jsToFixedArg(x),
        jsToFixedArg(y),
        jsToFixedArg(z));
    return std::string(buf);
}

[[nodiscard]] std::optional<CarrierSolidRecord> FinalizeSolid(const CarrierSolidBuilder& builder)
{
    // Reference: ObjectMeshCatalog.js L685-L788
    const size_t triCount = std::min<size_t>(builder.Raw.size(), kMaximumSolidTriangles);
    if (triCount == 0u) return std::nullopt;

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double minZ = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();

    for (size_t i = 0u; i < triCount; ++i)
    {
        for (const Vec3d& pt : { builder.Raw[i].A, builder.Raw[i].B, builder.Raw[i].C })
        {
            minX = std::min(minX, pt[0]); maxX = std::max(maxX, pt[0]);
            minY = std::min(minY, pt[1]); maxY = std::max(maxY, pt[1]);
            minZ = std::min(minZ, pt[2]); maxZ = std::max(maxZ, pt[2]);
        }
    }

    const Vec3d centre { (minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5 };
    const double extent = std::max({ maxX - minX, maxY - minY, maxZ - minZ }) * 0.5;
    const double scaleFactor = (extent > 1.0e-6) ? (1.0 / extent) : 1.0;

    CarrierSolidRecord solid {};
    solid.Name        = builder.Name;
    solid.Triangles   = static_cast<uint32_t>(triCount);
    solid.VertexCount = static_cast<uint32_t>(triCount * 3u);
    solid.Radius      = 1.0f;
    solid.Bounds      = SolidBounds { { minX, minY, minZ }, { maxX, maxY, maxZ }, centre, extent };
    solid.Corners.resize(triCount * 9u);
    solid.Normals.resize(triCount * 9u);
    solid.Areas.resize(triCount);

    std::vector<Vec3d> faceNormals;
    faceNormals.reserve(triCount);
    double totalArea = 0.0;

    for (size_t i = 0u; i < triCount; ++i)
    {
        const Vec3d pa = Scale3(Sub3(builder.Raw[i].A, centre), scaleFactor);
        const Vec3d pb = Scale3(Sub3(builder.Raw[i].B, centre), scaleFactor);
        const Vec3d pc = Scale3(Sub3(builder.Raw[i].C, centre), scaleFactor);
        const size_t o = i * 9u;
        solid.Corners[o + 0u] = static_cast<float>(pa[0]);
        solid.Corners[o + 1u] = static_cast<float>(pa[1]);
        solid.Corners[o + 2u] = static_cast<float>(pa[2]);
        solid.Corners[o + 3u] = static_cast<float>(pb[0]);
        solid.Corners[o + 4u] = static_cast<float>(pb[1]);
        solid.Corners[o + 5u] = static_cast<float>(pb[2]);
        solid.Corners[o + 6u] = static_cast<float>(pc[0]);
        solid.Corners[o + 7u] = static_cast<float>(pc[1]);
        solid.Corners[o + 8u] = static_cast<float>(pc[2]);

        const Vec3d crossVec = Cross3(Sub3(pb, pa), Sub3(pc, pa));
        const Vec3d normal = Unit3(crossVec);
        faceNormals.push_back(normal);
        for (size_t c = 0u; c < 3u; ++c)
        {
            solid.Normals[o + c * 3u + 0u] = static_cast<float>(normal[0]);
            solid.Normals[o + c * 3u + 1u] = static_cast<float>(normal[1]);
            solid.Normals[o + c * 3u + 2u] = static_cast<float>(normal[2]);
        }
        const double area = Length3(crossVec) * 0.5;
        solid.Areas[i] = static_cast<float>(area);
        totalArea += area;
    }
    solid.TotalArea = totalArea;

    // Preserve exact JS Map insertion order for crease edge extraction (ObjectMeshCatalog.js L724-L756)
    struct EdgeBucket
    {
        std::string           KeyA;
        std::string           KeyB;
        std::vector<uint32_t> Faces;
    };
    std::vector<EdgeBucket> edgeBuckets;
    std::unordered_map<std::string, size_t> edgeLookup;
    std::unordered_map<std::string, std::array<float, 3>> pointsByKey;

    for (size_t idx = 0u; idx < triCount; ++idx)
    {
        std::array<std::string, 3u> ring;
        for (size_t c = 0u; c < 3u; ++c)
        {
            const float x = solid.Corners[idx * 9u + c * 3u + 0u];
            const float y = solid.Corners[idx * 9u + c * 3u + 1u];
            const float z = solid.Corners[idx * 9u + c * 3u + 2u];
            ring[c] = FormatVertexKey(x, y, z);
            if (pointsByKey.find(ring[c]) == pointsByKey.end())
            {
                pointsByKey.emplace(ring[c], std::array<float, 3> { x, y, z });
            }
        }
        for (size_t c = 0u; c < 3u; ++c)
        {
            const std::string& ka = ring[c];
            const std::string& kb = ring[(c + 1u) % 3u];
            const bool aFirst = (ka < kb);
            const std::string& lo = aFirst ? ka : kb;
            const std::string& hi = aFirst ? kb : ka;
            const std::string edgeKey = lo + "~" + hi;
            const auto it = edgeLookup.find(edgeKey);
            if (it != edgeLookup.end())
            {
                edgeBuckets[it->second].Faces.push_back(static_cast<uint32_t>(idx));
            }
            else
            {
                edgeLookup.emplace(edgeKey, edgeBuckets.size());
                EdgeBucket bucket { lo, hi, { static_cast<uint32_t>(idx) } };
                edgeBuckets.push_back(std::move(bucket));
            }
        }
    }

    std::vector<float> creaseCount(triCount, 0.0f);
    for (const EdgeBucket& bucket : edgeBuckets)
    {
        if (bucket.Faces.size() != 2u) continue;
        const uint32_t f0 = bucket.Faces[0];
        const uint32_t f1 = bucket.Faces[1];
        const double d = faceNormals[f0][0] * faceNormals[f1][0]
                       + faceNormals[f0][1] * faceNormals[f1][1]
                       + faceNormals[f0][2] * faceNormals[f1][2];
        if (std::fabs(d) > 0.84) continue;
        creaseCount[f0] += 1.0f;
        creaseCount[f1] += 1.0f;
        const auto itA = pointsByKey.find(bucket.KeyA);
        const auto itB = pointsByKey.find(bucket.KeyB);
        solid.Creases.push_back(SolidCreaseSegment {
            itA != pointsByKey.end() ? itA->second : std::array<float, 3> { 0, 0, 0 },
            itB != pointsByKey.end() ? itB->second : std::array<float, 3> { 0, 0, 0 }
        });
        if (solid.Creases.size() > 4096u) break;
    }

    if (solid.Creases.empty())
    {
        for (size_t idx = 0u; idx < triCount; idx += 7u)
        {
            if (solid.Corners[idx * 9u + 1u] < 0.05f) continue;
            solid.Creases.push_back(SolidCreaseSegment {
                { solid.Corners[idx * 9u + 0u], solid.Corners[idx * 9u + 1u], solid.Corners[idx * 9u + 2u] },
                { solid.Corners[idx * 9u + 3u], solid.Corners[idx * 9u + 4u], solid.Corners[idx * 9u + 5u] }
            });
            if (solid.Creases.size() > 512u) break;
        }
    }

    solid.RidgeWeights.resize(triCount);
    solid.CreaseFactors.resize(triCount);
    for (size_t idx = 0u; idx < triCount; ++idx)
    {
        solid.RidgeWeights[idx]  = solid.Areas[idx] * (1.0f + creaseCount[idx] * 1.6f);
        solid.CreaseFactors[idx] = std::min(1.0f, creaseCount[idx] * 0.5f);
    }

    return solid;
}

[[nodiscard]] CarrierSolidBuilder BuildNamedSolid(const std::string_view name)
{
    if (name == "Cube")            return BuildCube();
    if (name == "Sphere")          return BuildSphere();
    if (name == "Coin")            return BuildCoin();
    if (name == "CrystalShard")    return BuildCrystalShard();
    if (name == "HexPrism")        return BuildHexPrism();
    if (name == "StarPoly")        return BuildStarPoly();
    if (name == "UtahTeapot")      return BuildUtahTeapot();
    if (name == "SportsCar")       return BuildSportsCar();
    if (name == "ObsidianHand")    return BuildObsidianHand();
    if (name == "SculptedBust")    return BuildSculptedBust();
    if (name == "TorusKnot")       return BuildTorusKnot();
    if (name == "PrecisionGear")   return BuildPrecisionGear();
    if (name == "CrystalCluster")  return BuildCrystalCluster();
    if (name == "Archway")         return BuildArchway();
    if (name == "FacetedSpire")    return BuildFacetedSpire();
    if (name == "WingedButterfly") return BuildWingedButterfly();
    if (name == "WingedBird")      return BuildWingedBird();
    if (name == "FishBody")        return BuildFishBody();
    return BuildUtahTeapot();
}

[[nodiscard]] double Hash01(const uint32_t index, const uint32_t salt) noexcept
{
    // Exact 32-bit integer hash matching ObjectMeshCatalog.js L1020-L1025
    uint32_t x = (static_cast<uint32_t>(index + 1u) * 0x9e3779b1u) ^ (static_cast<uint32_t>(salt + 7u) * 0x85ebca6bu);
    x ^= (x >> 15u);
    x *= 0x2545f491u;
    x ^= (x >> 13u);
    return static_cast<double>(x) / 4294967296.0;
}

[[nodiscard]] Vec3d RotateY(const Vec3d& pt, const double radians) noexcept
{
    const double c = std::cos(radians), s = std::sin(radians);
    return { pt[0] * c + pt[2] * s, pt[1], -pt[0] * s + pt[2] * c };
}

[[nodiscard]] std::vector<double> BuildCumulativeDistribution(std::span<const double> weights)
{
    std::vector<double> cdf(weights.size(), 0.0);
    double total = 0.0;
    for (size_t i = 0u; i < weights.size(); ++i)
    {
        total += weights[i];
        cdf[i] = total;
    }
    const double inv = (total > 1.0e-9) ? (1.0 / total) : 1.0;
    for (double& v : cdf) v *= inv;
    return cdf;
}

[[nodiscard]] size_t PickIndex(std::span<const double> cdf, const double u) noexcept
{
    if (cdf.empty()) return 0u;
    size_t lo = 0u;
    size_t hi = cdf.size() - 1u;
    while (lo < hi)
    {
        const size_t mid = (lo + hi) >> 1u;
        if (cdf[mid] < u) lo = mid + 1u;
        else hi = mid;
    }
    return lo;
}

[[nodiscard]] uint32_t ReadLeUInt32(const uint8_t* p) noexcept
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8u)
         | (static_cast<uint32_t>(p[2]) << 16u)
         | (static_cast<uint32_t>(p[3]) << 24u);
}

[[nodiscard]] float ReadLeFloat32(const uint8_t* p) noexcept
{
    const uint32_t bits = ReadLeUInt32(p);
    float val = 0.0f;
    std::memcpy(&val, &bits, sizeof(float));
    return val;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               BUILT-IN SOLID QUERIES
//------------------------------------------------------------------------------------------------------------------------

std::span<const std::string_view> QueryAllSolidNames() noexcept
{
    return kAllSolidNames;
}

std::span<const std::string_view> QueryBuiltInSolidNames() noexcept
{
    return kBuiltInSolidNames;
}

std::string_view QuerySolidDisplayLabel(const std::string_view solidName) noexcept
{
    for (size_t i = 0u; i < kAllSolidNames.size(); ++i)
    {
        if (kAllSolidNames[i] == solidName) return kSolidLabels[i];
    }
    return solidName;
}

const CarrierSolidRecord& ResolveBuiltInSolid(const std::string_view solidName)
{
    static std::array<std::optional<CarrierSolidRecord>, 18u> builtInTable {};
    size_t idx = 6u; // UtahTeapot fallback
    for (size_t i = 0u; i < kBuiltInSolidNames.size(); ++i)
    {
        if (kBuiltInSolidNames[i] == solidName)
        {
            idx = i;
            break;
        }
    }
    if (!builtInTable[idx].has_value())
    {
        builtInTable[idx] = *FinalizeSolid(BuildNamedSolid(kBuiltInSolidNames[idx]));
    }
    return *builtInTable[idx];
}

//------------------------------------------------------------------------------------------------------------------------
//                                           IMPORT, PACKING & SAMPLING
//------------------------------------------------------------------------------------------------------------------------

std::optional<CarrierSolidRecord> ParseObjSolid(const std::string_view objText, const std::string_view name)
{
    // Reference: ObjectMeshCatalog.js L825-L858
    CarrierSolidBuilder builder(name);
    std::vector<Vec3d> vertices;

    size_t pos = 0u;
    while (pos < objText.size())
    {
        size_t nl = objText.find('\n', pos);
        if (nl == std::string_view::npos) nl = objText.size();
        std::string_view line = objText.substr(pos, nl - pos);
        pos = nl + 1u;

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.remove_prefix(1u);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.remove_suffix(1u);
        if (line.empty() || line.front() == '#') continue;

        if (line.size() >= 2u && line[0] == 'v' && std::isspace(static_cast<unsigned char>(line[1])))
        {
            double x = 0.0, y = 0.0, z = 0.0;
            std::string tmp(line.substr(2u));
            if (std::sscanf(tmp.c_str(), "%lf %lf %lf", &x, &y, &z) == 3 && std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
            {
                vertices.push_back({ x, y, z });
            }
        }
        else if (line.size() >= 2u && line[0] == 'f' && std::isspace(static_cast<unsigned char>(line[1])))
        {
            std::vector<size_t> indices;
            size_t i = 2u;
            while (i < line.size())
            {
                while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
                if (i >= line.size()) break;
                size_t j = i;
                while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
                const std::string tok(line.substr(i, j - i));
                char* endPtr = nullptr;
                const long raw = std::strtol(tok.c_str(), &endPtr, 10);
                if (endPtr != tok.c_str())
                {
                    const long resolved = (raw < 0) ? (static_cast<long>(vertices.size()) + raw) : (raw - 1);
                    if (resolved >= 0 && static_cast<size_t>(resolved) < vertices.size())
                    {
                        indices.push_back(static_cast<size_t>(resolved));
                    }
                }
                i = j;
            }
            for (size_t k = 1u; k + 1u < indices.size(); ++k)
            {
                builder.Triangle(vertices[indices[0]], vertices[indices[k]], vertices[indices[k + 1u]]);
            }
        }
    }
    return FinalizeSolid(builder);
}

std::optional<CarrierSolidRecord> ParseStlSolid(const std::span<const uint8_t> rawBytes, const std::string_view name)
{
    // Reference: ObjectMeshCatalog.js L861-L893
    CarrierSolidBuilder builder(name);
    if (rawBytes.size() >= 84u)
    {
        const uint32_t count = ReadLeUInt32(rawBytes.data() + 80u);
        if (count > 0u && count < kMaximumSolidTriangles && 84ull + static_cast<uint64_t>(count) * 50ull == rawBytes.size())
        {
            size_t offset = 84u;
            for (uint32_t i = 0u; i < count; ++i)
            {
                offset += 12u; // skip facet normal
                const Vec3d a { ReadLeFloat32(&rawBytes[offset]), ReadLeFloat32(&rawBytes[offset + 4u]), ReadLeFloat32(&rawBytes[offset + 8u]) };
                offset += 12u;
                const Vec3d b { ReadLeFloat32(&rawBytes[offset]), ReadLeFloat32(&rawBytes[offset + 4u]), ReadLeFloat32(&rawBytes[offset + 8u]) };
                offset += 12u;
                const Vec3d c { ReadLeFloat32(&rawBytes[offset]), ReadLeFloat32(&rawBytes[offset + 4u]), ReadLeFloat32(&rawBytes[offset + 8u]) };
                offset += 14u; // 12 + 2 attribute bytes
                builder.Triangle(a, b, c);
            }
            return FinalizeSolid(builder);
        }
    }

    // ASCII STL fallback
    const std::string_view text(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
    std::vector<Vec3d> verts;
    size_t pos = 0u;
    while (pos < text.size())
    {
        const size_t hit = text.find("vertex", pos);
        if (hit == std::string_view::npos) break;
        pos = hit + 6u;
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        const std::string chunk(text.substr(pos, nl - pos));
        double x = 0.0, y = 0.0, z = 0.0;
        if (std::sscanf(chunk.c_str(), "%lf %lf %lf", &x, &y, &z) == 3)
        {
            verts.push_back({ x, y, z });
        }
        pos = nl + 1u;
    }
    for (size_t i = 0u; i + 2u < verts.size(); i += 3u)
    {
        builder.Triangle(verts[i], verts[i + 1u], verts[i + 2u]);
    }
    return FinalizeSolid(builder);
}

std::optional<CarrierSolidRecord> ParseBinaryFct1Solid(const std::span<const uint8_t> rawBytes, const std::string_view name)
{
    // Reference: ObjectMeshCatalog.js L926-L961
    if (rawBytes.size() < 48u) return std::nullopt;
    if (rawBytes[0] != 'F' || rawBytes[1] != 'C' || rawBytes[2] != 'T' || rawBytes[3] != '1') return std::nullopt;

    const uint32_t vertexCount = ReadLeUInt32(&rawBytes[4]);
    const uint32_t triCount    = ReadLeUInt32(&rawBytes[8]);
    const uint64_t posEnd      = 48ull + static_cast<uint64_t>(vertexCount) * 12ull;
    const uint64_t idxEnd      = posEnd + static_cast<uint64_t>(triCount) * 12ull;
    if (vertexCount == 0u || triCount == 0u || idxEnd > rawBytes.size()) return std::nullopt;

    const uint32_t stride = std::max(1u, (triCount + kMaximumSolidTriangles - 1u) / kMaximumSolidTriangles);
    CarrierSolidBuilder builder(name);
    for (uint32_t f = 0u; f < triCount; f += stride)
    {
        const size_t idxOff = static_cast<size_t>(posEnd) + static_cast<size_t>(f) * 12u;
        const uint32_t i0 = ReadLeUInt32(&rawBytes[idxOff + 0u]);
        const uint32_t i1 = ReadLeUInt32(&rawBytes[idxOff + 4u]);
        const uint32_t i2 = ReadLeUInt32(&rawBytes[idxOff + 8u]);
        if (i0 < vertexCount && i1 < vertexCount && i2 < vertexCount)
        {
            auto readPos = [&](const uint32_t vi) noexcept -> Vec3d {
                const size_t pOff = 48u + static_cast<size_t>(vi) * 12u;
                return { ReadLeFloat32(&rawBytes[pOff]), ReadLeFloat32(&rawBytes[pOff + 4u]), ReadLeFloat32(&rawBytes[pOff + 8u]) };
            };
            builder.Triangle(readPos(i0), readPos(i1), readPos(i2));
        }
    }
    return FinalizeSolid(builder);
}

SolidTexelTable PackSolidTexels(const CarrierSolidRecord& solid)
{
    // Reference: ObjectMeshCatalog.js L986-L1014
    const uint32_t triangles = solid.Triangles;
    const uint32_t texelCount = std::max(1u, triangles * 6u);
    const uint32_t width = std::min(1024u, std::max(1u, texelCount));
    const uint32_t height = (texelCount + width - 1u) / width;

    SolidTexelTable table {};
    table.Width         = width;
    table.Height        = height;
    table.TriangleCount = triangles;
    table.VertexCount   = triangles * 3u;
    table.Data.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0.0f);

    for (uint32_t tri = 0u; tri < triangles; ++tri)
    {
        const float ridge = (tri < solid.CreaseFactors.size()) ? solid.CreaseFactors[tri] : 0.0f;
        for (uint32_t corner = 0u; corner < 3u; ++corner)
        {
            const size_t src = static_cast<size_t>(tri) * 9u + static_cast<size_t>(corner) * 3u;
            const size_t dst = (static_cast<size_t>(tri) * 6u + static_cast<size_t>(corner) * 2u) * 4u;
            table.Data[dst + 0u] = solid.Corners[src + 0u];
            table.Data[dst + 1u] = solid.Corners[src + 1u];
            table.Data[dst + 2u] = solid.Corners[src + 2u];
            table.Data[dst + 3u] = solid.Normals[src + 0u];
            table.Data[dst + 4u] = solid.Normals[src + 1u];
            table.Data[dst + 5u] = solid.Normals[src + 2u];
            table.Data[dst + 6u] = 0.0f;
            table.Data[dst + 7u] = ridge;
        }
    }
    return table;
}

SolidAnchorSet SampleSolidAnchors(
    const CarrierSolidRecord& solid,
    const uint32_t count,
    const SolidSampleModeOrdinal mode,
    const double yawDegrees)
{
    // Reference: ObjectMeshCatalog.js L1058-L1118
    const uint32_t total = std::clamp(count, 1u, kMaximumAnchorCount);
    SolidAnchorSet out {};
    out.Count = total;
    out.Positions.assign(static_cast<size_t>(total) * 3u, 0.0f);
    out.Normals.assign(static_cast<size_t>(total) * 3u, 0.0f);
    out.Ridges.assign(total, 0.0f);

    if (solid.Triangles == 0u)
    {
        for (uint32_t i = 0u; i < total; ++i) out.Normals[i * 3u + 1u] = 1.0f;
        return out;
    }

    std::vector<double> areaWeights(solid.Areas.begin(), solid.Areas.end());
    std::vector<double> ridgeWeights(solid.RidgeWeights.begin(), solid.RidgeWeights.end());
    const auto cdf      = BuildCumulativeDistribution(areaWeights);
    const auto ridgeCdf = BuildCumulativeDistribution(ridgeWeights);

    std::vector<double> creaseLengths;
    creaseLengths.reserve(solid.Creases.size());
    double totalCreaseLength = 0.0;
    for (const SolidCreaseSegment& seg : solid.Creases)
    {
        const Vec3d a { seg.A[0], seg.A[1], seg.A[2] };
        const Vec3d b { seg.B[0], seg.B[1], seg.B[2] };
        const double len = Length3(Sub3(b, a));
        creaseLengths.push_back(len);
        totalCreaseLength += len;
    }
    const auto creaseCdf = BuildCumulativeDistribution(creaseLengths);

    const double yaw = yawDegrees * kPi / 180.0;
    const uint32_t modeIndex = static_cast<uint32_t>(mode);
    const double ridgeFraction = (modeIndex == 2u && !solid.Creases.empty() && totalCreaseLength > 1.0e-6) ? 0.72 : 0.0;

    for (uint32_t i = 0u; i < total; ++i)
    {
        Vec3d position { 0.0, 0.0, 0.0 };
        Vec3d normal   { 0.0, 1.0, 0.0 };
        float ridge    = 0.0f;

        if (ridgeFraction > 0.0 && Hash01(i, 13u) < ridgeFraction)
        {
            const SolidCreaseSegment& crease = solid.Creases[PickIndex(creaseCdf, Hash01(i, 17u))];
            const double t = Hash01(i, 19u);
            position = {
                static_cast<double>(crease.A[0]) + (static_cast<double>(crease.B[0]) - static_cast<double>(crease.A[0])) * t,
                static_cast<double>(crease.A[1]) + (static_cast<double>(crease.B[1]) - static_cast<double>(crease.A[1])) * t,
                static_cast<double>(crease.A[2]) + (static_cast<double>(crease.B[2]) - static_cast<double>(crease.A[2])) * t
            };
            normal = Unit3(position);
            ridge  = 1.0f;
        }
        else
        {
            const double u = (static_cast<double>(i) + Hash01(i, 11u) * 0.92) / static_cast<double>(total);
            const bool useRidge = (ridgeFraction > 0.0 && Hash01(i, 23u) < 0.5);
            const size_t tri = PickIndex(useRidge ? std::span<const double>(ridgeCdf) : std::span<const double>(cdf), std::min(0.999999, u));
            const size_t src = tri * 9u;
            const double r1 = std::sqrt(Hash01(i, 29u));
            const double r2 = Hash01(i, 31u);
            const double wa = 1.0 - r1;
            const double wb = r1 * (1.0 - r2);
            const double wc = r1 * r2;

            for (size_t axis = 0u; axis < 3u; ++axis)
            {
                position[axis] = static_cast<double>(solid.Corners[src + axis]) * wa
                               + static_cast<double>(solid.Corners[src + 3u + axis]) * wb
                               + static_cast<double>(solid.Corners[src + 6u + axis]) * wc;
                normal[axis] = static_cast<double>(solid.Normals[src + axis]);
            }
            if (modeIndex == 1u && (i % 3u == 0u))
            {
                const double depth = 0.25 + 0.68 * std::cbrt(Hash01(i, 37u));
                position = Scale3(position, depth);
            }
            normal = Unit3(normal);
        }

        position = RotateY(position, yaw);
        normal   = RotateY(normal, yaw);
        out.Positions[i * 3u + 0u] = static_cast<float>(position[0]);
        out.Positions[i * 3u + 1u] = static_cast<float>(position[1]);
        out.Positions[i * 3u + 2u] = static_cast<float>(position[2]);
        out.Normals[i * 3u + 0u]   = static_cast<float>(normal[0]);
        out.Normals[i * 3u + 1u]   = static_cast<float>(normal[1]);
        out.Normals[i * 3u + 2u]   = static_cast<float>(normal[2]);
        out.Ridges[i]              = ridge;
    }
    return out;
}

AnchorTexelTable PackAnchorTexels(const SolidAnchorSet& sampled)
{
    // Reference: ObjectMeshCatalog.js L1121-L1139
    const uint32_t count = sampled.Count;
    const uint32_t texelCount = std::max(2u, count * 2u);
    const uint32_t height = (texelCount + kAnchorTexelWidth - 1u) / kAnchorTexelWidth;

    AnchorTexelTable out {};
    out.Width  = kAnchorTexelWidth;
    out.Height = height;
    out.Count  = count;
    out.Data.assign(static_cast<size_t>(kAnchorTexelWidth) * static_cast<size_t>(height) * 4u, 0.0f);

    for (uint32_t i = 0u; i < count; ++i)
    {
        const size_t first  = static_cast<size_t>(i) * 8u;
        const size_t second = first + 4u;
        out.Data[first + 0u]  = sampled.Positions[i * 3u + 0u];
        out.Data[first + 1u]  = sampled.Positions[i * 3u + 1u];
        out.Data[first + 2u]  = sampled.Positions[i * 3u + 2u];
        out.Data[first + 3u]  = sampled.Positions[i * 3u + 1u];
        out.Data[second + 0u] = sampled.Normals[i * 3u + 0u];
        out.Data[second + 1u] = sampled.Normals[i * 3u + 1u];
        out.Data[second + 2u] = sampled.Normals[i * 3u + 2u];
        out.Data[second + 3u] = sampled.Ridges[i];
    }
    return out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 CARRIERSOLIDREGISTRY
//------------------------------------------------------------------------------------------------------------------------

void CarrierSolidRegistry::ClearCustomSolids() noexcept
{
    CustomEntries.clear();
    NextSequence = 1u;
}

std::string CarrierSolidRegistry::RegisterCustomSolid(const std::string_view name, CarrierSolidRecord solid)
{
    if (solid.Triangles == 0u) return {};
    const std::string id = "solid-" + std::to_string(NextSequence++);
    const uint32_t triCount = solid.Triangles;
    CustomEntries.push_back(CustomSolidEntry {
        id,
        name.empty() ? ("Custom Object " + std::to_string(NextSequence - 1u)) : std::string(name),
        std::move(solid),
        triCount
    });
    return id;
}

std::optional<std::string> CarrierSolidRegistry::ImportCustomSolid(
    const std::string_view fileName,
    const std::span<const uint8_t> rawBytes)
{
    std::string lower(fileName);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string plain(fileName);
    if (const size_t dot = plain.rfind('.'); dot != std::string::npos) plain.resize(dot);
    if (plain.empty()) plain = "Custom Object";

    std::optional<CarrierSolidRecord> solid;
    if (lower.size() >= 4u && lower.substr(lower.size() - 4u) == ".stl")
    {
        solid = ParseStlSolid(rawBytes, plain);
    }
    else if (lower.size() >= 4u && lower.substr(lower.size() - 4u) == ".obj")
    {
        solid = ParseObjSolid(std::string_view(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size()), plain);
    }
    else
    {
        solid = ParseBinaryFct1Solid(rawBytes, plain);
        if (!solid) solid = ParseStlSolid(rawBytes, plain);
        if (!solid) solid = ParseObjSolid(std::string_view(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size()), plain);
    }
    if (!solid.has_value()) return std::nullopt;
    return RegisterCustomSolid(plain, std::move(*solid));
}

const CustomSolidEntry* CarrierSolidRegistry::FindCustomSolid(const std::string_view customId) const noexcept
{
    for (const CustomSolidEntry& e : CustomEntries)
    {
        if (e.Id == customId) return &e;
    }
    return nullptr;
}

const CarrierSolidRecord& CarrierSolidRegistry::ResolveSolid(
    const std::string_view solidName,
    const std::string_view customId) const
{
    if (solidName == "CustomObject")
    {
        if (const CustomSolidEntry* entry = FindCustomSolid(customId))
        {
            return entry->Solid;
        }
        return ResolveBuiltInSolid("UtahTeapot");
    }
    return ResolveBuiltInSolid(solidName);
}

} // namespace Frontier::ParticleSpace
