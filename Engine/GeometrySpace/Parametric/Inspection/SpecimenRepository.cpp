/*====================================================================================================================================
                                                     SPECIMENCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Named specimen bodies with independently derived expectations (counts, genus, analytic volume) for the gates

#include "Engine/GeometrySpace/Parametric/Inspection/SpecimenRepository.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"
#include "Engine/GeometrySpace/Parametric/Feature/PathSweep.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSolver.h"

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
    constexpr double π = std::numbers::pi;                                                   // [-] - Circle constant

    using SpecimenAssembly = std::function<SpecimenOutcome(ModelPartition&, const SpecimenParameters&)>;

    // Record a parameter as used and return its value.
    double Use(SpecimenOutcome& Outcome, const SpecimenParameters& Parameters, const std::string& Key, double Default)
    {
        const double Resolved = Parameters.Resolve(Key, Default);                            // [-] - Override or default
        Outcome.Parameters[Key] = Resolved;
        return Resolved;
    }

    template <typename Result>
    void Adopt(SpecimenOutcome& Outcome, const Result& Edit)
    {
        if (Edit.SucceededCondition()) Outcome.Body = Edit.Value;
        else Outcome.Refusal = Edit.Refusal;
    }

    const std::vector<std::pair<std::string, SpecimenAssembly>>& Catalogue()
    {
        static const std::vector<std::pair<std::string, SpecimenAssembly>> Entries = {
            { "Block", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Prism sweep of a rectangle: MVFS, 3 MEV, MEF, then 4 MEV + 4 MEF" };
                const Vector3 Extent{ Use(Outcome, Parameters, "SizeX", 4.0), Use(Outcome, Parameters, "SizeY", 3.0), Use(Outcome, Parameters, "SizeZ", 2.0) };
                Adopt(Outcome, ConstructBlock(Partition, {}, Extent));
                Outcome.Expectation = { 8, 12, 6, 6, 1, 0, Extent.X * Extent.Y * Extent.Z };
                return Outcome;
            } },
            { "HexagonalPrism", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Generic translational sweep of a regular hexagon" };
                const double Radius = Use(Outcome, Parameters, "Radius", 2.0);
                const double Height = Use(Outcome, Parameters, "Height", 3.0);
                std::vector<Vector3> Profile;
                for (int CornerIndex = 0; CornerIndex < 6; ++CornerIndex)
                {
                    const double θ = π / 3.0 * CornerIndex;                                  // [rad] - Corner azimuth
                    Profile.push_back({ Radius * std::cos(θ), Radius * std::sin(θ), 0.0 });
                }
                const auto Edit = ConstructPrism(Partition, Profile, { 0.0, 0.0, Height });
                if (Edit.SucceededCondition()) Outcome.Body = Edit.Value.Body; else Outcome.Refusal = Edit.Refusal;
                Outcome.Expectation = { 12, 18, 8, 8, 1, 0, 1.5 * std::sqrt(3.0) * Radius * Radius * Height };
                return Outcome;
            } },
            { "Cylinder", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Seam edge on a periodic chart; closed circle edges via MEF on one corner" };
                const double Radius = Use(Outcome, Parameters, "Radius", 1.5);
                const double Height = Use(Outcome, Parameters, "Height", 3.0);
                Adopt(Outcome, ConstructCylinder(Partition, OrthonormalBasis{}, Radius, Height));
                Outcome.Expectation = { 2, 3, 3, 3, 1, 0, π * Radius * Radius * Height };
                return Outcome;
            } },
            { "Cone", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Apex vertex on a singular row of the cone chart" };
                const double Radius = Use(Outcome, Parameters, "Radius", 2.0);
                const double Height = Use(Outcome, Parameters, "Height", 3.0);
                Adopt(Outcome, ConstructCone(Partition, OrthonormalBasis{}, Radius, Height));
                Outcome.Expectation = { 2, 2, 2, 2, 1, 0, π * Radius * Radius * Height / 3.0 };
                return Outcome;
            } },
            { "Sphere", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "One face, one seam, two singular pole rows" };
                const double Radius = Use(Outcome, Parameters, "Radius", 2.0);
                Adopt(Outcome, ConstructSphere(Partition, OrthonormalBasis{}, Radius));
                Outcome.Expectation = { 2, 1, 1, 1, 1, 0, 4.0 / 3.0 * π * Radius * Radius * Radius };
                return Outcome;
            } },
            { "Torus", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Doubly periodic chart; genus 1 from KFMRH, closed by MEKR" };
                const double MajorRadius = Use(Outcome, Parameters, "MajorRadius", 3.0);
                const double MinorRadius = Use(Outcome, Parameters, "MinorRadius", 1.0);
                Adopt(Outcome, ConstructTorus(Partition, OrthonormalBasis{}, MajorRadius, MinorRadius));
                Outcome.Expectation = { 1, 2, 1, 1, 1, 1, 2.0 * π * π * MajorRadius * MinorRadius * MinorRadius };
                return Outcome;
            } },
            { "PerforatedBlock", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Through hole: ring by KEMR, inward sweep, KFMRH into the bottom face" };
                const Vector3 Extent{ Use(Outcome, Parameters, "SizeX", 6.0), Use(Outcome, Parameters, "SizeY", 4.0), Use(Outcome, Parameters, "SizeZ", 2.0) };
                const Vector3 Hole{ Use(Outcome, Parameters, "HoleX", 2.0), Use(Outcome, Parameters, "HoleY", 1.5), 0.0 };
                Adopt(Outcome, ConstructPerforatedBlock(Partition, Extent, Hole));
                Outcome.Expectation = { 16, 24, 10, 12, 1, 1, (Extent.X * Extent.Y - Hole.X * Hole.Y) * Extent.Z };
                return Outcome;
            } },
            { "HollowBlock", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Cavity: a reversed second shell bounding the same solid region" };
                const Vector3 Extent{ Use(Outcome, Parameters, "SizeX", 6.0), Use(Outcome, Parameters, "SizeY", 5.0), Use(Outcome, Parameters, "SizeZ", 4.0) };
                const Vector3 Cavity{ Use(Outcome, Parameters, "CavityX", 3.0), Use(Outcome, Parameters, "CavityY", 2.0), Use(Outcome, Parameters, "CavityZ", 1.0) };
                Adopt(Outcome, ConstructHollowBlock(Partition, Extent, Cavity));
                Outcome.Expectation = { 16, 24, 12, 12, 2, 0, Extent.X * Extent.Y * Extent.Z - Cavity.X * Cavity.Y * Cavity.Z };
                return Outcome;
            } },
            { "TolerantBlock", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "One vertex displaced off its exact edges and faces; local precision covers it (0 = exact)" };
                const Vector3 Extent{ Use(Outcome, Parameters, "SizeX", 4.0), Use(Outcome, Parameters, "SizeY", 3.0), Use(Outcome, Parameters, "SizeZ", 2.0) };
                const double  Displacement = Use(Outcome, Parameters, "Displacement", 4.0e-6);
                const double  Precision    = Use(Outcome, Parameters, "Precision", 1.0e-5);
                Adopt(Outcome, ConstructBlock(Partition, {}, Extent));
                Outcome.Expectation = { 8, 12, 6, 6, 1, 0, Extent.X * Extent.Y * Extent.Z };
                if (!Outcome.Refusal.empty()) return Outcome;
                // 📝 Displace the top corner nearest (SizeX, SizeY, SizeZ); its three edges and three faces stay exact.
                const BodyInventory Inventory = Partition.CollectInventory(Outcome.Body);
                VertexToken Corner = Inventory.Vertices.front();                             // [-] - Vertex to displace
                for (VertexToken Vertex : Inventory.Vertices)
                {
                    if (Distance(Partition.Vertices.Resolve(Vertex).Position, Extent) < Distance(Partition.Vertices.Resolve(Corner).Position, Extent)) Corner = Vertex;
                }
                Partition.Vertices.Resolve(Corner).Position = Partition.Vertices.Resolve(Corner).Position + Vector3{ Displacement, -Displacement, Displacement } * (1.0 / std::sqrt(3.0));
                if (Precision > 0.0) Partition.AssignVertexPrecision(Corner, Precision);
                return Outcome;
            } },
            // ---- Step 8 (G2): path and transform verbs.
            { "PipeBend", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Pipe along an arc = section circle revolved about the arc axis: exact partial torus, planar end caps (360°: closed torus)" };
                const double BendRadius = Use(Outcome, Parameters, "BendRadius", 2.0);
                const double TubeRadius = Use(Outcome, Parameters, "TubeRadius", 0.5);
                const double Turn       = std::clamp(Use(Outcome, Parameters, "Turn", 90.0), 1.0, 360.0) * π / 180.0;   // [rad] - Arc range
                const SweepOutcome Sweep = PipeAlongPath(Partition, CircularArc{ OrthonormalBasis{}, BendRadius }, 0.0, Turn, TubeRadius);
                Outcome.Body = Sweep.Body;
                Outcome.Refusal = Sweep.Refusal;
                const bool ClosedCondition = Turn >= 2.0 * π - 1.0e-12;
                Outcome.Expectation = ClosedCondition ? SpecimenExpectation{ 1, 2, 1, 1, 1, 1, 0.0 } : SpecimenExpectation{ 2, 3, 3, 3, 1, 0, 0.0 };
                Outcome.Expectation.Volume = Turn * π * TubeRadius * TubeRadius * BendRadius;
                return Outcome;
            } },
            { "LoftFrustum", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Ruled loft between coaxial circles: exact frustum (equal radii: cylinder)" };
                const double FootRadius = Use(Outcome, Parameters, "FootRadius", 1.5);
                const double HeadRadius = Use(Outcome, Parameters, "HeadRadius", 0.75);
                const double Height     = Use(Outcome, Parameters, "Height", 2.0);
                const SweepOutcome Sweep = LoftCircles(Partition, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, FootRadius, { 0.0, 0.0, Height }, { 0.0, 0.0, 1.0 }, HeadRadius);
                Outcome.Body = Sweep.Body;
                Outcome.Refusal = Sweep.Refusal;
                Outcome.Expectation = { 2, 3, 3, 3, 1, 0, π * Height / 3.0 * (FootRadius * FootRadius + FootRadius * HeadRadius + HeadRadius * HeadRadius) };
                return Outcome;
            } },
            { "SweptRing", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Annulus profile swept along its normal: two seams, holed caps, genus 1" };
                const double Outer  = Use(Outcome, Parameters, "OuterRadius", 1.5);
                const double Inner  = Use(Outcome, Parameters, "InnerRadius", 0.8);
                const double Length = Use(Outcome, Parameters, "Length", 2.0);
                SketchModel Sketch;
                const int Centre = AppendSketchPoint(Sketch, { 0.0, 0.0 });
                static_cast<void>(AppendSketchCircle(Sketch, Centre, Outer, 1));
                static_cast<void>(AppendSketchCircle(Sketch, Centre, Inner, 2));
                const SweepOutcome Sweep = SweepProfileAlongLine(Partition, ExtractSketchProfile(Sketch), OrthonormalBasis{}, { 0.0, 0.0, Length });
                Outcome.Body = Sweep.Body;
                Outcome.Refusal = Sweep.Refusal;
                Outcome.Expectation = { 4, 6, 4, 6, 1, 1, π * (Outer * Outer - Inner * Inner) * Length };
                return Outcome;
            } },
            { "MirroredCross", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "CrossUnequal union mirrored across an oblique plane: traced section edges mapped exactly (guide, surfaces, knots)" };
                static_cast<void>(Parameters);
                ModelPartition Scratch;                                                      // Arguments and the union stay here
                const BodyToken Rod  = ConstructCylinder(Scratch, ConstructFrame({ -2.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }), 0.5, 4.0).Value;
                const BodyToken Tube = ConstructCylinder(Scratch, ConstructFrame({ 0.0, -2.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }), 0.3, 4.0).Value;
                const BooleanOutcome Cross = ComputeBoolean(Scratch, Rod, Tube, BooleanOperation::Union);
                if (!Cross.Refusal.empty()) { Outcome.Refusal = Cross.Refusal; return Outcome; }
                const TransformOutcome Mirrored = TransformBodyAcross(Scratch, Cross.Body, Partition, ReflectionMap({ 1.0, 0.0, 0.0 }, { 1.0, 0.5, 0.0 }));
                Outcome.Body = Mirrored.Body;
                Outcome.Refusal = Mirrored.Refusal;
                Outcome.Expectation = { 6, 9, 7, 9, 1, 0, 4.003195681354056 };
                return Outcome;
            } },
            { "ArrayedPins", [](ModelPartition& Partition, const SpecimenParameters& Parameters)
            {
                SpecimenOutcome Outcome{ .Description = "Pin arrayed along a plate and united: pin bases flush with the plate bottom merge into one maximal face (G5)" };
                const int    Count     = std::clamp(static_cast<int>(std::lround(Use(Outcome, Parameters, "Count", 3.0))), 1, 8);
                const double Spacing   = Use(Outcome, Parameters, "Spacing", 2.0);
                const double PinRadius = std::min(Use(Outcome, Parameters, "PinRadius", 0.3), 0.45 * Spacing);
                ModelPartition Scratch;
                BodyToken Plate = ConstructBlock(Scratch, {}, { Spacing * Count, 2.0, 0.5 }).Value;
                const BodyToken Pin = ConstructCylinder(Scratch, OrthonormalBasis{ { 0.5 * Spacing, 1.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, PinRadius, 1.0).Value;
                std::vector<BodyToken> Pins = { Pin };
                for (const TransformOutcome& Copy : ArrayBody(Scratch, Pin, { Spacing, 0.0, 0.0 }, Count))
                {
                    if (!Copy.Refusal.empty()) { Outcome.Refusal = Copy.Refusal; return Outcome; }
                    Pins.push_back(Copy.Body);
                }
                for (BodyToken Each : Pins)
                {
                    const BooleanOutcome United = ComputeBoolean(Scratch, Plate, Each, BooleanOperation::Union);
                    if (!United.Refusal.empty()) { Outcome.Refusal = United.Refusal; return Outcome; }
                    Plate = United.Body;
                }
                const TransformOutcome Kept = TransformBodyAcross(Scratch, Plate, Partition, TranslationMap({}));
                Outcome.Body = Kept.Body;
                Outcome.Refusal = Kept.Refusal;
                Outcome.Expectation = { 8 + 2 * Count, 12 + 3 * Count, 6 + 2 * Count, 6 + 3 * Count, 1, 0, Spacing * Count + Count * π * PinRadius * PinRadius * 0.5 };
                return Outcome;
            } },
        };
        return Entries;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> EnumerateSpecimens()
{
    std::vector<std::string> Names;
    for (const auto& Entry : Catalogue()) Names.push_back(Entry.first);
    return Names;
}

SpecimenOutcome ConstructSpecimen(ModelPartition& Partition, const std::string& Name, const SpecimenParameters& Parameters)
{
    for (const auto& Entry : Catalogue())
    {
        if (Entry.first == Name) return Entry.second(Partition, Parameters);
    }
    SpecimenOutcome Unknown;
    Unknown.Refusal = "unknown specimen '" + Name + "'";
    return Unknown;
}

} // namespace Frontier::GeometrySpace::Parametric
