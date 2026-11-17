/*====================================================================================================================================
                                                    ASSEMBLYANALYSIS.CPP
====================================================================================================================================*/
// 🧩 Assembly checks (priority 11, A1): mass properties of several bodies with densities, clearance and interference

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Assembly/AssemblyAnalysis.h"

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.h"

#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double ContactDistance = 1.0e-7;                                               // [len] - Faces closer than this meet

    // Whether a vertex of Inner lies strictly inside Outer.
    [[nodiscard]] bool InsideCondition(const ModelPartition& Partition, BodyToken Inner, BodyToken Outer)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Inner);
        Vector3 Probe;
        if (!Inventory.Vertices.empty()) Probe = Partition.Vertices.Resolve(Inventory.Vertices.front()).Position;
        else if (!Inventory.Faces.empty()) Probe = MeasureFaceArea(Partition, Inventory.Faces.front()).Centroid;    // A sphere has no vertex
        else return false;
        const BodyContainment Containment = PrepareBodyContainment(Partition, Outer);
        return ClassifySolidPointScreened(Containment, Probe).Membership == PointMembership::Inside;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

AssemblyMassProperties MeasureAssemblyMass(const ModelPartition& Partition, const std::vector<AssemblyMember>& Members)
{
    AssemblyMassProperties Totals;
    if (Members.empty()) { Totals.Refusal = "Assembly mass: no bodies"; return Totals; }
    std::vector<BodyMassProperties> Parts;
    Vector3 Moment;
    for (const AssemblyMember& Member : Members)
    {
        if (!(Member.Density > 0.0)) { Totals.Refusal = "Assembly mass: densities must be positive"; return Totals; }
        BodyMassProperties Part = MeasureBodyMass(Partition, Member.Body);
        if (!Part.Refusal.empty()) { Totals.Refusal = "Assembly mass: " + Part.Refusal; return Totals; }
        const double Mass = Member.Density * Part.Volume;
        Totals.Mass        += Mass;
        Totals.Volume      += Part.Volume;
        Totals.SurfaceArea += Part.SurfaceArea;
        Moment = Moment + Part.Centroid * Mass;
        Parts.push_back(std::move(Part));
    }
    Totals.CentreOfMass = Moment * (1.0 / Totals.Mass);
    for (size_t Index = 0; Index < Members.size(); ++Index)
    {
        const double  ρ    = Members[Index].Density;
        const double  Mass = ρ * Parts[Index].Volume;
        const Vector3 d    = Parts[Index].Centroid - Totals.CentreOfMass;
        const std::array<double, 6>& I = Parts[Index].Inertia;
        Totals.Inertia[0] += ρ * I[0] + Mass * (d.Y * d.Y + d.Z * d.Z);
        Totals.Inertia[1] += ρ * I[1] + Mass * (d.Z * d.Z + d.X * d.X);
        Totals.Inertia[2] += ρ * I[2] + Mass * (d.X * d.X + d.Y * d.Y);
        Totals.Inertia[3] += ρ * I[3] - Mass * d.X * d.Y;
        Totals.Inertia[4] += ρ * I[4] - Mass * d.Y * d.Z;
        Totals.Inertia[5] += ρ * I[5] - Mass * d.Z * d.X;
    }
    return Totals;
}

BodyClearance MeasureBodyClearance(const ModelPartition& Partition, BodyToken First, BodyToken Second)
{
    BodyClearance Clearance;
    Clearance.Distance = std::numeric_limits<double>::infinity();
    const std::vector<FaceToken> FirstFaces  = Partition.CollectInventory(First).Faces;
    const std::vector<FaceToken> SecondFaces = Partition.CollectInventory(Second).Faces;
    if (FirstFaces.empty() || SecondFaces.empty()) { Clearance.Refusal = "Clearance: a body has no faces"; return Clearance; }
    for (FaceToken A : FirstFaces)
    {
        for (FaceToken B : SecondFaces)
        {
            const DistanceMeasure Measure = MeasureMinimumDistance(Partition, A, B);
            ++Clearance.Pairs;
            if (!Measure.Refusal.empty()) { Clearance.Refusal = "Clearance: " + Measure.Refusal; return Clearance; }
            if (Measure.Distance < Clearance.Distance)
            {
                Clearance.Distance = Measure.Distance;
                Clearance.First    = Measure.First;
                Clearance.Second   = Measure.Second;
            }
            if (Clearance.Distance <= ContactDistance) return Clearance;
        }
    }
    return Clearance;
}

InterferenceMeasure DetectBodyInterference(ModelPartition& Partition, BodyToken First, BodyToken Second)
{
    InterferenceMeasure Measure;
    if (First == Second) { Measure.Refusal = "Interference: a body cannot interfere with itself"; return Measure; }
    for (BodyToken Body : { First, Second })
    {
        if (Partition.Bodies.Resolve(Body).Category != BodyCategory::Solid) { Measure.Refusal = "Interference: solids only (a sheet has no volume)"; return Measure; }
    }
    const BodyClearance Clearance = MeasureBodyClearance(Partition, First, Second);
    if (!Clearance.Refusal.empty()) { Measure.Refusal = "Interference: " + Clearance.Refusal; return Measure; }
    Measure.First  = Clearance.First;
    Measure.Second = Clearance.Second;
    if (Clearance.Distance > ContactDistance)
    {
        for (const auto& [Inner, Outer] : { std::pair{ First, Second }, std::pair{ Second, First } })
        {
            if (!InsideCondition(Partition, Inner, Outer)) continue;
            Measure.Condition = InterferenceCondition::Contained;
            Measure.Body      = Inner;
            Measure.Volume    = IntegrateBodyVolume(Partition, Inner);
            return Measure;
        }
        Measure.Condition = InterferenceCondition::Clear;
        Measure.Clearance = Clearance.Distance;
        return Measure;
    }
    const BooleanOutcome Common = ComputeBoolean(Partition, First, Second, BooleanOperation::Intersection);
    if (Common.Refusal == "Boolean: empty result" || (Common.Refusal.empty() && Common.Solids == 0))
    {
        Measure.Condition = InterferenceCondition::Contact;
        return Measure;
    }
    if (!Common.Refusal.empty()) { Measure.Refusal = "Interference: " + Common.Refusal; return Measure; }
    Measure.Body   = Common.Body;
    Measure.Volume = IntegrateBodyVolume(Partition, Common.Body);
    Measure.Condition = Measure.Volume > 0.0 ? InterferenceCondition::Overlap : InterferenceCondition::Contact;
    return Measure;
}

const char* InterferenceConditionLabel(InterferenceCondition Condition) noexcept
{
    switch (Condition)
    {
    case InterferenceCondition::Clear:     return "Clear";
    case InterferenceCondition::Contact:   return "Contact";
    case InterferenceCondition::Overlap:   return "Overlap";
    case InterferenceCondition::Contained: return "Contained";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric
