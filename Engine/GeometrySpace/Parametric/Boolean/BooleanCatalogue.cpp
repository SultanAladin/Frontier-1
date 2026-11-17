/*====================================================================================================================================
                                                      BOOLEANCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Boolean scenes: argument pairs with closed-form volumes, shared by BooleanValidation and the inspection viewer

#include "Engine/GeometrySpace/Parametric/Boolean/BooleanCatalogue.h"

#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"

#include <functional>
#include <map>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr double π = std::numbers::pi;                                                   // [rad] - π

    [[nodiscard]] double SphereVolume(double Radius) { return 4.0 / 3.0 * π * Radius * Radius * Radius; }
    [[nodiscard]] double CapVolume(double Radius, double Height) { return π * Height * Height * (3.0 * Radius - Height) / 3.0; }

    // Circular segment beyond a chord at distance Offset from the centre.
    [[nodiscard]] double SegmentArea(double Radius, double Offset)
    {
        return Radius * Radius * std::acos(Offset / Radius) - Offset * std::sqrt(Radius * Radius - Offset * Offset);
    }

    // 📝 Union / intersection / differences from V(A), V(B), V(A ∩ B).
    [[nodiscard]] std::array<BooleanExpectation, 4> FromOverlap(double First, double Second, double Overlap, int UnionShells = 1, int OverlapShells = 1, int DifferenceShells = 1, int ReversedShells = 1)
    {
        return { { { First + Second - Overlap, UnionShells, UnionShells }, { Overlap, OverlapShells, OverlapShells },
                   { First - Overlap, DifferenceShells, DifferenceShells }, { Second - Overlap, ReversedShells, ReversedShells } } };
    }

    // Frame with ZAxis along Axis (cylinder axes of the step-8 scenes).
    [[nodiscard]] OrthonormalFrame AxisFrame(Vector3 Origin, Vector3 Axis)
    {
        return ConstructFrame(Origin, Axis, std::fabs(Axis.Z) > 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 0.0, 1.0 });
    }

    using SceneBuilder = std::function<BooleanScene(ModelPartition&)>;

    [[nodiscard]] const std::vector<std::pair<std::string, SceneBuilder>>& Scenes()
    {
        static const std::vector<std::pair<std::string, SceneBuilder>> Table = {
            { "BlockCorner", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockCorner", "Two blocks overlapping at a corner: 6 section lines, L-shaped split faces" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.FirstVolume = Scene.SecondVolume = 8.0;
                Scene.Expected = FromOverlap(8.0, 8.0, 1.0);
                return Scene;
            } },
            { "BlockStacked", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockStacked", "Coplanar contact: B sits on A's top face, offset (ON faces, imprints both ways)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 1.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 1.0, 0.5, 1.0 }, { 2.0, 1.0, 1.0 }).Value;
                Scene.FirstVolume = 4.0;  Scene.SecondVolume = 2.0;
                Scene.Expected = { { { 6.0, 1, 1 }, { 0.0, 0, 0 }, { 4.0, 1, 1 }, { 2.0, 1, 1 } } };
                return Scene;
            } },
            { "BlockRotated", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockRotated", "Block against the same block turned 30° about its vertical axis (octagonal prism)" };
                Scene.First = ConstructBlock(Partition, { -1.0, -1.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                const double Angle = π / 6.0;                                                // [rad] - Turn
                std::vector<Vector3> Profile;
                for (const auto& Corner : { Vector3{ -1.0, -1.0, -0.5 }, Vector3{ 1.0, -1.0, -0.5 }, Vector3{ 1.0, 1.0, -0.5 }, Vector3{ -1.0, 1.0, -0.5 } })
                {
                    Profile.push_back({ Corner.X * std::cos(Angle) - Corner.Y * std::sin(Angle), Corner.X * std::sin(Angle) + Corner.Y * std::cos(Angle), Corner.Z });
                }
                Scene.Second = ConstructPrism(Partition, Profile, { 0.0, 0.0, 2.0 }).Value.Body;
                // 📝 Overlap of two concentric squares (half-side a = 1) turned by θ: each corner of the first loses one
                //    right triangle cut by one edge line x cos θ + y sin θ = a of the second, with legs a(1 − tan θ/2) on
                //    x = a and a(cos θ + sin θ − 1)/cos θ on y = a. Shared height: z ∈ [0, 1.5].
                const double LegFirst  = 1.0 - std::tan(Angle * 0.5);                        // [len] - Along x = a
                const double LegSecond = (std::cos(Angle) + std::sin(Angle) - 1.0) / std::cos(Angle);  // [len] - Along y = a
                const double Area      = 4.0 - 4.0 * 0.5 * LegFirst * LegSecond;              // [len²] - Octagon
                Scene.FirstVolume = 8.0;  Scene.SecondVolume = 8.0;
                Scene.Expected = FromOverlap(8.0, 8.0, Area * 1.5);
                return Scene;
            } },
            { "DrilledBlock", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "DrilledBlock", "Block − through cylinder: genus 1, seam split by both caps" };
                Scene.First = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 4.0, 2.0 }).Value;
                Scene.Second = ConstructCylinder(Partition, { { 2.0, 2.0, -1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 1.0, 4.0).Value;
                Scene.FirstVolume = 32.0;  Scene.SecondVolume = 4.0 * π;
                Scene.Expected = FromOverlap(32.0, 4.0 * π, 2.0 * π, 1, 1, 1, 2);
                return Scene;
            } },
            { "SphereCrater", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "SphereCrater", "Block ∩ sphere breaking the top face (latitude circle, iso-line SP-curves)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.Second = ConstructSphere(Partition, { { 1.0, 1.0, 1.8 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.5).Value;
                const double Cap = CapVolume(0.5, 0.3);                                       // [len³] - Above z = 2
                Scene.FirstVolume = 8.0;  Scene.SecondVolume = SphereVolume(0.5);
                Scene.Expected = FromOverlap(8.0, SphereVolume(0.5), SphereVolume(0.5) - Cap);
                return Scene;
            } },
            { "SphereCraterTilted", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "SphereCraterTilted", "As SphereCrater with the sphere chart tilted: the section circle is not an iso-line (projected SP-curves)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.Second = ConstructSphere(Partition, ConstructFrame({ 1.0, 1.0, 1.8 }, { 0.5, 0.3, 1.0 }, { 1.0, 0.0, 0.0 }), 0.5).Value;
                const double Cap = CapVolume(0.5, 0.3);                                       // [len³] - Above z = 2
                Scene.FirstVolume = 8.0;  Scene.SecondVolume = SphereVolume(0.5);
                Scene.Expected = FromOverlap(8.0, SphereVolume(0.5), SphereVolume(0.5) - Cap);
                return Scene;
            } },
            { "SphereLens", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "SphereLens", "Two unit spheres one radius apart: lens = π(4r + d)(2r − d)²/12" };
                Scene.First  = ConstructSphere(Partition, {}, 1.0).Value;
                Scene.Second = ConstructSphere(Partition, { { 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 1.0).Value;
                const double Lens = π * (4.0 + 1.0) * 1.0 / 12.0;                            // [len³] - r = 1, d = 1
                Scene.FirstVolume = Scene.SecondVolume = SphereVolume(1.0);
                Scene.Expected = FromOverlap(SphereVolume(1.0), SphereVolume(1.0), Lens);
                return Scene;
            } },
            { "SteppedShaft", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "SteppedShaft", "Coaxial cylinders r 1 / 0.5 overlapping by 1: section circle on a cap" };
                Scene.First  = ConstructCylinder(Partition, {}, 1.0, 2.0).Value;
                Scene.Second = ConstructCylinder(Partition, { { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.5, 2.0).Value;
                Scene.FirstVolume = 2.0 * π;  Scene.SecondVolume = 0.5 * π;
                Scene.Expected = FromOverlap(2.0 * π, 0.5 * π, 0.25 * π, 1, 1, 1, 1);
                return Scene;
            } },
            { "ConeInCylinder", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "ConeInCylinder", "Coaxial cone (r 1, h 2) and cylinder (r 0.5): the apex singular row lies inside B" };
                Scene.First  = ConstructCone(Partition, {}, 1.0, 2.0).Value;
                Scene.Second = ConstructCylinder(Partition, { { 0.0, 0.0, -0.5 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.5, 3.0).Value;
                Scene.FirstVolume = 2.0 * π / 3.0;  Scene.SecondVolume = 0.75 * π;
                Scene.Expected = FromOverlap(2.0 * π / 3.0, 0.75 * π, π / 3.0, 1, 1, 1, 2);
                return Scene;
            } },
            { "TorusSlab", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "TorusSlab", "Torus ∩ slab z ∈ [0.2, 1]: Pappus V = 2πR·segment" };
                Scene.First  = ConstructTorus(Partition, {}, 2.0, 0.6).Value;
                Scene.Second = ConstructBlock(Partition, { -3.0, -3.0, 0.2 }, { 6.0, 6.0, 0.8 }).Value;
                const double Slice = 2.0 * π * 2.0 * SegmentArea(0.6, 0.2);                  // [len³] - Pappus
                const double Torus = 2.0 * π * π * 2.0 * 0.36;                                // [len³] - 2π²Rr²
                Scene.FirstVolume = Torus;  Scene.SecondVolume = 28.8;
                Scene.Expected = FromOverlap(Torus, 28.8, Slice, 1, 1, 1, 1);
                return Scene;
            } },
            { "TorusEquator", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "TorusEquator", "Torus ∩ slab z ∈ [0, 1]: a section circle coincides with the torus seam (common block)" };
                Scene.First  = ConstructTorus(Partition, {}, 2.0, 0.6).Value;
                Scene.Second = ConstructBlock(Partition, { -3.0, -3.0, 0.0 }, { 6.0, 6.0, 1.0 }).Value;
                const double Torus = 2.0 * π * π * 2.0 * 0.36;                                // [len³] - 2π²Rr²
                Scene.FirstVolume = Torus;  Scene.SecondVolume = 36.0;
                Scene.Expected = FromOverlap(Torus, 36.0, 0.5 * Torus, 1, 1, 1, 1);
                return Scene;
            } },
            { "DShaft", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "DShaft", "Cylinder − block beyond x = 0.6: the whole seam is removed, flat by two lines" };
                Scene.First  = ConstructCylinder(Partition, {}, 1.0, 3.0).Value;
                Scene.Second = ConstructBlock(Partition, { 0.6, -2.0, -1.0 }, { 1.4, 4.0, 5.0 }).Value;
                const double Cut = SegmentArea(1.0, 0.6) * 3.0;                               // [len³] - Removed segment prism
                Scene.FirstVolume = 3.0 * π;  Scene.SecondVolume = 28.0;
                Scene.Expected = FromOverlap(3.0 * π, 28.0, Cut);
                return Scene;
            } },
            { "BlockCavity", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockCavity", "B strictly inside A: no sections; A − B has a cavity shell, B − A is empty" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 4.0, 4.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.FirstVolume = 64.0;  Scene.SecondVolume = 8.0;
                Scene.Expected = { { { 64.0, 1, 1 }, { 8.0, 1, 1 }, { 56.0, 1, 2 }, { 0.0, 0, 0 } } };
                return Scene;
            } },
            { "BlockApart", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockApart", "Disjoint blocks: the union has two solid regions (lumps)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 2.0, 0.0, 0.0 }, { 1.0, 2.0, 1.0 }).Value;
                Scene.FirstVolume = 1.0;  Scene.SecondVolume = 2.0;
                Scene.Expected = { { { 3.0, 2, 2 }, { 0.0, 0, 0 }, { 1.0, 1, 1 }, { 2.0, 1, 1 } } };
                return Scene;
            } },
            { "NapkinRing", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "NapkinRing", "Sphere − coaxial cylinder r 0.4: both poles removed, V = πh³/6" };
                Scene.First  = ConstructSphere(Partition, {}, 1.0).Value;
                Scene.Second = ConstructCylinder(Partition, { { 0.0, 0.0, -2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.4, 4.0).Value;
                const double Height = 2.0 * std::sqrt(1.0 - 0.16);                            // [len] - Ring height
                const double Ring   = π * Height * Height * Height / 6.0;                     // [len³] - Napkin ring
                Scene.FirstVolume = SphereVolume(1.0);  Scene.SecondVolume = π * 0.16 * 4.0;
                Scene.Expected = FromOverlap(SphereVolume(1.0), π * 0.64, SphereVolume(1.0) - Ring, 1, 1, 1, 2);
                return Scene;
            } },
            // ---- Step 8 (G3 / G4 / G5): the corpus scenes the old kernel refused or fragmented.
            { "CrossUnequal", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "CrossUnequal", "Tube r 0.3 through rod r 0.5 at right angles: two quartic loops, traced section carriers (G3)" };
                Scene.First  = ConstructCylinder(Partition, AxisFrame({ -2.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }), 0.5, 4.0).Value;
                Scene.Second = ConstructCylinder(Partition, AxisFrame({ 0.0, -2.0, 0.0 }, { 0.0, 1.0, 0.0 }), 0.3, 4.0).Value;
                Scene.FirstVolume = π;  Scene.SecondVolume = 0.36 * π;
                Scene.Expected = FromOverlap(π, 0.36 * π, 1.36 * π - 4.003195681354056, 1, 1, 1, 2);
                return Scene;
            } },
            { "CrossOffset", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "CrossOffset", "As CrossUnequal with the rod lifted 0.1: asymmetric loops (old kernel 4.009803, true 4.009989)" };
                Scene.First  = ConstructCylinder(Partition, AxisFrame({ -2.0, 0.0, 0.1 }, { 1.0, 0.0, 0.0 }), 0.5, 4.0).Value;
                Scene.Second = ConstructCylinder(Partition, AxisFrame({ 0.0, -2.0, 0.0 }, { 0.0, 1.0, 0.0 }), 0.3, 4.0).Value;
                Scene.FirstVolume = π;  Scene.SecondVolume = 0.36 * π;
                Scene.Expected = FromOverlap(π, 0.36 * π, 1.36 * π - 4.009989052758214, 1, 1, 1, 2);
                return Scene;
            } },
            { "SteinmetzSolid", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "SteinmetzSolid", "Equal rods r 0.5 at right angles: two ellipses crossing at 2 singular points (G4), V(A ∩ B) = 16r³/3" };
                Scene.First  = ConstructCylinder(Partition, AxisFrame({ -2.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }), 0.5, 4.0).Value;
                Scene.Second = ConstructCylinder(Partition, AxisFrame({ 0.0, -2.0, 0.0 }, { 0.0, 1.0, 0.0 }), 0.5, 4.0).Value;
                Scene.FirstVolume = Scene.SecondVolume = π;
                Scene.Expected = FromOverlap(π, π, 16.0 / 3.0 * 0.125, 1, 1, 2, 2);
                return Scene;
            } },
            { "SteinmetzOblique", [](ModelPartition& Partition)
            {
                const Vector3 Along{ 0.5, std::sqrt(3.0) / 2.0, 0.0 };                        // [-] - 60° to the first axis
                BooleanScene Scene{ "SteinmetzOblique", "Equal rods r 0.5 at 60°: oblique ellipses through the singular points, V(A ∩ B) = 16r³/(3 sin θ)" };
                Scene.First  = ConstructCylinder(Partition, AxisFrame({ -2.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }), 0.5, 4.0).Value;
                Scene.Second = ConstructCylinder(Partition, AxisFrame(Along * -2.0, Along), 0.5, 4.0).Value;
                Scene.FirstVolume = Scene.SecondVolume = π;
                Scene.Expected = FromOverlap(π, π, 16.0 * 0.125 / (3.0 * Along.Y), 1, 1, 2, 2);
                return Scene;
            } },
            { "BlockFlushHalf", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockFlushHalf", "Corpus BoxA / BoxB: half-overlapping blocks with 4 coplanar face pairs — maximal faces give 6 faces (G5)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 2.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value;
                Scene.FirstVolume = Scene.SecondVolume = 24.0;
                Scene.Expected = FromOverlap(24.0, 24.0, 12.0);
                return Scene;
            } },
            { "BlockStep", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockStep", "Lower block flush with the back, sides and bottom of a taller one: an L-profile of maximal faces (G5)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 3.0, 2.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 2.0, 0.0, 0.0 }, { 4.0, 3.0, 1.0 }).Value;
                Scene.FirstVolume = 24.0;  Scene.SecondVolume = 12.0;
                Scene.Expected = FromOverlap(24.0, 12.0, 6.0);
                return Scene;
            } },
            { "BlockFlushSlot", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockFlushSlot", "Slot bar flush with the top, through both sides: A − B one U-solid, B − A two stubs (G5)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 3.0, 3.0, 2.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 1.0, -1.0, 1.0 }, { 1.0, 5.0, 1.0 }).Value;
                Scene.FirstVolume = 18.0;  Scene.SecondVolume = 5.0;
                Scene.Expected = FromOverlap(18.0, 5.0, 3.0, 1, 1, 1, 2);
                return Scene;
            } },
            { "BlockFlushCorner", [](ModelPartition& Partition)
            {
                BooleanScene Scene{ "BlockFlushCorner", "Corner overlap with flush top and bottom: L-shaped maximal caps (G5)" };
                Scene.First  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.Second = ConstructBlock(Partition, { 1.0, 1.0, 0.0 }, { 2.0, 2.0, 2.0 }).Value;
                Scene.FirstVolume = Scene.SecondVolume = 8.0;
                Scene.Expected = FromOverlap(8.0, 8.0, 2.0);
                return Scene;
            } },
        };
        return Table;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> EnumerateBooleanScenes()
{
    std::vector<std::string> Names;
    for (const auto& Entry : Scenes()) Names.push_back(Entry.first);
    return Names;
}

BooleanScene ConstructBooleanScene(ModelPartition& Partition, const std::string& Name)
{
    for (const auto& Entry : Scenes())
    {
        if (Entry.first == Name) return Entry.second(Partition);
    }
    return {};
}

} // namespace Frontier::GeometrySpace::Parametric
