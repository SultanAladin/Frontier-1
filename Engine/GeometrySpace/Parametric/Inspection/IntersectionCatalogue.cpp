/*====================================================================================================================================
                                                  INTERSECTIONCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Intersection scenes: analytic sections, classic singular curves (Steinmetz, Viviani, Villarceau), small loops

#include "Engine/GeometrySpace/Parametric/Inspection/IntersectionCatalogue.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double π        = std::numbers::pi;                                            // [rad] - π
    constexpr double FullTurn = 2.0 * π;                                                     // [rad] - 2π

    const ParameterBox WholeSphere{ { 0.0, -0.5 * π }, { FullTurn, 0.5 * π } };              // [-] - Full sphere chart
    const ParameterBox WholeTorus{ { 0.0, 0.0 }, { FullTurn, FullTurn } };                   // [-] - Full torus chart
    const ParameterBox WidePlane{ { -10.0, -10.0 }, { 10.0, 10.0 } };                         // [-] - Large plane patch

    [[nodiscard]] ParameterBox CylinderBox(double Half) { return { { 0.0, -Half }, { FullTurn, Half } }; }

    // 📝 Ellipse perimeter by quadrature of √(a² sin² τ + b² cos² τ).
    [[nodiscard]] double EllipsePerimeter(double First, double Second)
    {
        return IntegrateArcLength([&](double τ) { return std::hypot(First * std::sin(τ), Second * std::cos(τ)); }, 0.0, FullTurn);
    }

    using SceneBuilder = std::function<void(IntersectionScene&, const SpecimenParameters&)>;

    struct SceneEntry
    {
        const char*  Name    = "";                                                           // [-] - Catalogue name
        SceneBuilder Builder = {};                                                           // [-] - Fills the scene
    };

    [[nodiscard]] double Use(IntersectionScene& Scene, const SpecimenParameters& Parameters, const char* Key, double Default)
    {
        const double Value = Parameters.Resolve(Key, Default);
        Scene.Parameters[Key] = Value;
        return Value;
    }

    [[nodiscard]] const std::vector<SceneEntry>& SceneTable()
    {
        static const std::vector<SceneEntry> Table =
        {
            { "PlaneSphereSection", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double r = Use(Scene, Parameters, "Radius", 2.0), h = Use(Scene, Parameters, "Offset", 1.0), θ = Use(Scene, Parameters, "Tilt", 0.3);
                const Vector3 Normal{ 0.0, std::sin(θ), std::cos(θ) };
                Scene.Description = "Plane at distance h from the centre: one exact circle of radius √(r² − h²); h = r touches";
                Scene.First = PlaneSurface{ ConstructFrame(Normal * h, Normal, { 1.0, 0.0, 0.0 }) };  Scene.FirstBox = WidePlane;
                Scene.Second = SphereSurface{ {}, r };                                       Scene.SecondBox = WholeSphere;
                if (std::fabs(h) < r) Scene.Expectation = { 1, 1, 0, FullTurn * std::sqrt(r * r - h * h) };
                else Scene.Expectation = { 0, 0, std::fabs(std::fabs(h) - r) <= SessionPrecision ? 1 : 0, 0.0 };
            } },
            { "SphereSphereLens", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double R1 = Use(Scene, Parameters, "FirstRadius", 2.0), R2 = Use(Scene, Parameters, "SecondRadius", 1.5), d = Use(Scene, Parameters, "Separation", 2.5);
                Scene.Description = "Two spheres: the meridian circles meet in one point — one exact circle (or a touch)";
                Scene.First = SphereSurface{ {}, R1 };                                       Scene.FirstBox = WholeSphere;
                Scene.Second = SphereSurface{ { { d, 0.0, 0.0 } }, R2 };                     Scene.SecondBox = WholeSphere;
                const double Along = (d * d + R1 * R1 - R2 * R2) / (2.0 * d);                // [len] - Circle plane offset
                if (d < R1 + R2 && d > std::fabs(R1 - R2)) Scene.Expectation = { 1, 1, 0, FullTurn * std::sqrt(R1 * R1 - Along * Along) };
                else Scene.Expectation = { 0, 0, std::fabs(d - R1 - R2) <= SessionPrecision || std::fabs(d - std::fabs(R1 - R2)) <= SessionPrecision ? 1 : 0, 0.0 };
            } },
            { "TorusPlaneSection", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double R = Use(Scene, Parameters, "MajorRadius", 3.0), r = Use(Scene, Parameters, "MinorRadius", 1.0), h = Use(Scene, Parameters, "Height", 0.5);
                Scene.Description = "Plane ⟂ the torus axis: two exact circles of radii R ± √(r² − h²) (total 4πR)";
                Scene.First = TorusSurface{ {}, R, r };                                      Scene.FirstBox = WholeTorus;
                Scene.Second = PlaneSurface{ { { 0.0, 0.0, h } } };                          Scene.SecondBox = WidePlane;
                Scene.Expectation = std::fabs(h) < r ? IntersectionExpectation{ 2, 2, 0, 4.0 * π * R } : IntersectionExpectation{ 0, 0, -1, 0.0 };
            } },
            { "VillarceauCircles", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double R = Use(Scene, Parameters, "MajorRadius", 3.0), r = Use(Scene, Parameters, "MinorRadius", 1.0);
                const double θ = std::asin(r / R);                                           // [rad] - Bitangent plane tilt
                Scene.Description = "Bitangent plane through the centre: two Villarceau circles of radius R crossing at two tangency points (marched)";
                Scene.First = TorusSurface{ {}, R, r };                                      Scene.FirstBox = WholeTorus;
                Scene.Second = PlaneSurface{ ConstructFrame({}, { 0.0, -std::sin(θ), std::cos(θ) }, { 1.0, 0.0, 0.0 }) };  Scene.SecondBox = WidePlane;
                Scene.Expectation = { 4, 0, 2, 2.0 * FullTurn * R };
            } },
            { "ObliqueCylinderSection", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double r = Use(Scene, Parameters, "Radius", 1.0), θ = Use(Scene, Parameters, "Tilt", 0.6);
                Scene.Description = "Oblique plane through a cylinder: an ellipse with semi-axes r and r/cos θ (marched; no ellipse type yet)";
                Scene.First = CylinderSurface{ {}, r };                                      Scene.FirstBox = CylinderBox(4.0 * r);
                Scene.Second = PlaneSurface{ ConstructFrame({}, { 0.0, std::sin(θ), std::cos(θ) }, { 1.0, 0.0, 0.0 }) };  Scene.SecondBox = WidePlane;
                Scene.Expectation = { 1, 1, 0, EllipsePerimeter(r, r / std::cos(θ)) };
            } },
            { "Bicylinder", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double a = Use(Scene, Parameters, "RadiusA", 1.0), b = Use(Scene, Parameters, "RadiusB", 0.6);
                Scene.Description = "Perpendicular cylinders, b < a: two closed loops (quadrature length reference)";
                Scene.First = CylinderSurface{ {}, a };                                      Scene.FirstBox = CylinderBox(3.0 * a);
                Scene.Second = CylinderSurface{ ConstructFrame({}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), b };  Scene.SecondBox = CylinderBox(3.0 * a);
                // 📝 Loop: y = b cos τ, z = b sin τ, x = √(a² − y²).
                const double Loop = IntegrateArcLength([&](double τ)
                {
                    const double y = b * std::cos(τ), x = std::sqrt(a * a - y * y);
                    const double dy = -b * std::sin(τ), dz = b * std::cos(τ), dx = -y * dy / x;
                    return std::sqrt(dx * dx + dy * dy + dz * dz);
                }, 0.0, FullTurn);
                Scene.Expectation = b < a ? IntersectionExpectation{ 2, 2, 0, 2.0 * Loop } : IntersectionExpectation{};
            } },
            { "SteinmetzCrossing", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double r = Use(Scene, Parameters, "Radius", 1.0);
                Scene.Description = "Equal perpendicular cylinders: two ellipses crossing at two singular points (split into 4 arcs)";
                Scene.First = CylinderSurface{ {}, r };                                      Scene.FirstBox = CylinderBox(3.0 * r);
                Scene.Second = CylinderSurface{ ConstructFrame({}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), r };  Scene.SecondBox = CylinderBox(3.0 * r);
                Scene.Expectation = { 4, 0, 2, 2.0 * EllipsePerimeter(r, r * std::numbers::sqrt2) };
            } },
            { "VivianiCurve", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double a = Use(Scene, Parameters, "Radius", 1.0);
                Scene.Description = "Sphere of radius 2a and a cylinder of radius a through its centre: figure-eight with one double point";
                Scene.First = SphereSurface{ {}, 2.0 * a };                                  Scene.FirstBox = WholeSphere;
                Scene.Second = CylinderSurface{ { { a, 0.0, 0.0 } }, a };                    Scene.SecondBox = CylinderBox(3.0 * a);
                // 📝 x = a(1 + cos τ), y = a sin τ, z = 2a sin(τ/2), τ ∈ [0, 4π): |r′| = a√(1 + cos²(τ/2)).
                Scene.Expectation = { 2, 0, 1, IntegrateArcLength([&](double τ) { return a * std::sqrt(1.0 + std::pow(std::cos(0.5 * τ), 2)); }, 0.0, 2.0 * FullTurn) };
            } },
            { "ConeTorusRing", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double Lean = Use(Scene, Parameters, "Lean", 0.15);
                Scene.Description = "Leaning cone through the torus tube: two closed loops, no closed form (lattice reference only)";
                Scene.First = TorusSurface{ {}, 3.0, 1.0 };                                  Scene.FirstBox = WholeTorus;
                Scene.Second = ConeSurface{ ConstructFrame({ 0.2, 0.1, 0.0 }, { Lean, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }), 3.0, -0.4 };  Scene.SecondBox = { { 0.0, -3.0 }, { FullTurn, 3.0 } };
                Scene.Expectation = { 2, 2, 0, std::numeric_limits<double>::quiet_NaN() };
            } },
            { "SmallLoop", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double δ = Use(Scene, Parameters, "Depth", 1.0e-3);
                Scene.Description = "Huge cylinder dipping δ into a unit sphere between lattice lines: found only by normal-cone loop detection";
                const Vector3 Direction{ std::cos(0.2) * std::cos(0.39), std::cos(0.2) * std::sin(0.39), std::sin(0.2) };
                constexpr double Huge = 50.0;                                                // [len] - Cylinder radius
                Scene.First = SphereSurface{ {}, 1.0 };                                      Scene.FirstBox = WholeSphere;
                Scene.Second = CylinderSurface{ ConstructFrame(Direction * (1.0 - δ + Huge), Cross(Direction, { 0.0, 0.0, 1.0 }), Direction), Huge };
                Scene.SecondBox = { { π - 0.3, -1.0 }, { π + 0.3, 1.0 } };
                Scene.Expectation = { 1, 1, 0, std::numeric_limits<double>::quiet_NaN() };
            } },
            { "TangentSpheres", [](IntersectionScene& Scene, const SpecimenParameters& Parameters)
            {
                const double R1 = Use(Scene, Parameters, "FirstRadius", 1.5), R2 = Use(Scene, Parameters, "SecondRadius", 1.0);
                Scene.Description = "Externally tangent spheres: no curve, one isolated touch point (try MarchingOnly = 1)";
                Scene.First = SphereSurface{ {}, R1 };                                       Scene.FirstBox = WholeSphere;
                // 📝 Centre direction off-axis, distance exactly R1 + R2.
                Scene.Second = SphereSurface{ { Normalize(Vector3{ R1 + R2, 0.3, 0.0 }) * (R1 + R2) }, R2 };
                Scene.SecondBox = WholeSphere;
                Scene.Expectation = { 0, 0, 1, 0.0 };
            } },
        };
        return Table;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> EnumerateIntersectionScenes()
{
    std::vector<std::string> Names;
    for (const SceneEntry& Entry : SceneTable()) Names.push_back(Entry.Name);
    return Names;
}

bool IntersectionSceneCondition(const std::string& Name)
{
    const auto& Table = SceneTable();
    return std::any_of(Table.begin(), Table.end(), [&](const SceneEntry& Entry) { return Name == Entry.Name; });
}

IntersectionScene ConstructIntersectionScene(const std::string& Name, const SpecimenParameters& Parameters)
{
    IntersectionScene Scene;
    for (const SceneEntry& Entry : SceneTable())
    {
        if (Name != Entry.Name) continue;
        Entry.Builder(Scene, Parameters);
        Scene.Settings.Policy               = Use(Scene, Parameters, "MarchingOnly", 0.0) != 0.0 ? IntersectionPolicy::MarchingOnly : IntersectionPolicy::Hybrid;
        Scene.Settings.LoopDetectionEnabled = Use(Scene, Parameters, "LoopDetection", 1.0) != 0.0;
        Scene.Settings.ChordTolerance       = std::max(1.0e-6, Use(Scene, Parameters, "ChordTolerance", 1.0e-3));
        Scene.Settings.LatticeDivisions     = std::clamp(static_cast<int>(Use(Scene, Parameters, "Lattice", 8.0)), 1, 64);
        return Scene;
    }
    Scene.Refusal = "unknown intersection scene '" + Name + "'";
    return Scene;
}

} // namespace Frontier::GeometrySpace::Parametric
