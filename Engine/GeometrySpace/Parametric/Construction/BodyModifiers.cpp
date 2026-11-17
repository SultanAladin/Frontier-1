/*====================================================================================================================================
                                                       BODYMODIFIERS.CPP
====================================================================================================================================*/
// 🧩 Non-destructive CAD modifier kernels: multi-instance assembly, linear/circular/curve/surface arrays, and certified 3D curve bend

#include "Engine/GeometrySpace/Parametric/Construction/BodyModifiers.h"

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyAssembly.h"
#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    struct BoxBounds
    {
        Vector3 Low  = {  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max() };
        Vector3 High = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };

        void Include(Vector3 Point) noexcept
        {
            Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
            High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
        }

        [[nodiscard]] bool ValidCondition() const noexcept { return Low.X <= High.X && Low.Y <= High.Y && Low.Z <= High.Z; }

        [[nodiscard]] bool Overlaps(const BoxBounds& Other, double Margin) const noexcept
        {
            return Low.X <= Other.High.X + Margin && Other.Low.X <= High.X + Margin &&
                   Low.Y <= Other.High.Y + Margin && Other.Low.Y <= High.Y + Margin &&
                   Low.Z <= Other.High.Z + Margin && Other.Low.Z <= High.Z + Margin;
        }
    };

    [[nodiscard]] BoxBounds MeasureBodyBounds(const ModelPartition& Partition, BodyToken Body)
    {
        BoxBounds           Bounds;
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        for (FaceToken Face : Inventory.Faces)
        {
            const FaceDomain Domain = DescribeFaceDomain(Partition, Face);
            Bounds.Include(Domain.BoundsLow);
            Bounds.Include(Domain.BoundsHigh);
        }
        for (VertexToken Vertex : Inventory.Vertices) Bounds.Include(Partition.Vertices.Resolve(Vertex).Position);
        return Bounds;
    }

    [[nodiscard]] BodyNaming CopyInstanceNaming(const ModelPartition& Partition, const TransformOutcome& Transform,
                                                const BodyNaming& Source, const std::string& InstancePrefix)
    {
        BodyNaming Naming;
        for (size_t Index = 0; Index < Transform.SourceFaces.size() && Index < Transform.Faces.size(); ++Index)
        {
            const auto Found = Source.Faces.find(Transform.SourceFaces[Index].Slot);
            const std::string Base = Found != Source.Faces.end() ? Found->second : ("Face" + std::to_string(Index + 1));
            Naming.Faces[Transform.Faces[Index].Slot] = InstancePrefix.empty() ? Base : (InstancePrefix + "." + Base);
        }
        CompleteNaming(Partition, Transform.Body, Naming);
        return Naming;
    }

    // Rotation-minimising rail table (Wang et al. 2008 double reflection + 3-point Gauss–Legendre arc length).
    struct RailStation
    {
        double  Parameter = 0.0;                                                             // [-]   - Curve τ
        double  ArcLength = 0.0;                                                             // [len] - Cumulative arc length s
        Vector3 Position  = {};                                                              // [len] - C(τ)
        Vector3 Tangent   = { 1.0, 0.0, 0.0 };                                               // [-]   - Unit T
        Vector3 Normal    = { 0.0, 1.0, 0.0 };                                               // [-]   - Unit RMF N
        Vector3 Binormal  = { 0.0, 0.0, 1.0 };                                               // [-]   - Unit RMF B = T × N
    };

    struct RailTable
    {
        const NurbsCurveDefinition* Definition    = nullptr;                                 // [-]     - Underlying NURBS definition
        std::vector<RailStation>    Stations      = {};                                      // [-]     - Dense monotonic table
        double                      TotalLength   = 0.0;                                     // [len]   - Arc length over domain
        double                      MaxCurvature  = 0.0;                                     // [1/len] - Peak |κ|
    };

    struct EvaluatedStation
    {
        Vector3 Position  = {};                                                              // [len]   - C(s)
        Vector3 Tangent   = { 1.0, 0.0, 0.0 };                                               // [-]     - Unit T(s)
        Vector3 Normal    = { 0.0, 1.0, 0.0 };                                               // [-]     - Unit N(s)
        Vector3 Binormal  = { 0.0, 0.0, 1.0 };                                               // [-]     - Unit B(s)
        Vector3 Curvature = {};                                                              // [1/len] - κ(s) = dT/ds
    };

    [[nodiscard]] Vector3 ChoosePerpendicular(Vector3 Axis) noexcept
    {
        const Vector3 Reference = std::fabs(Axis.Z) < 0.9 ? Vector3{ 0.0, 0.0, 1.0 } : Vector3{ 1.0, 0.0, 0.0 };
        return Normalize(Cross(Reference, Axis));
    }

    [[nodiscard]] RailTable BuildRailTable(const NurbsCurve& Rail, int SampleCount = 2048)
    {
        RailTable Table;
        Table.Definition = Rail.Definition.get();
        const NurbsDomain Domain = MeasureNurbsCurveDomain(*Table.Definition);
        Table.Stations.resize(static_cast<size_t>(SampleCount + 1));

        static constexpr double GaussNodes[3]   = { -0.77459666924148338, 0.0, 0.77459666924148338 };
        static constexpr double GaussWeights[3] = {  0.55555555555555556, 0.88888888888888889, 0.55555555555555556 };

        double Cumulative = 0.0;
        for (int Index = 0; Index <= SampleCount; ++Index)
        {
            const double τ = Domain.Low + (Domain.High - Domain.Low) * static_cast<double>(Index) / static_cast<double>(SampleCount);
            if (Index > 0)
            {
                const double Prev = Table.Stations[static_cast<size_t>(Index - 1)].Parameter;
                const double Mid  = 0.5 * (Prev + τ), Half = 0.5 * (τ - Prev);
                double Panel = 0.0;
                for (int Quad = 0; Quad < 3; ++Quad)
                    Panel += GaussWeights[Quad] * Length(EvaluateNurbsCurve(*Table.Definition, Mid + Half * GaussNodes[Quad]).Derivative);
                Cumulative += Half * Panel;
            }
            const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(*Table.Definition, τ);
            const double Speed = Length(Derivatives.First);
            if (Speed > 1.0e-14)
            {
                const double Curvature = Length(Cross(Derivatives.First, Derivatives.Second)) / (Speed * Speed * Speed);
                Table.MaxCurvature = std::max(Table.MaxCurvature, Curvature);
            }
            RailStation& Station = Table.Stations[static_cast<size_t>(Index)];
            Station.Parameter = τ;
            Station.ArcLength = Cumulative;
            Station.Position  = Derivatives.Position;
            Station.Tangent   = Speed > 1.0e-14 ? Derivatives.First * (1.0 / Speed)
                              : (Index > 0 ? Table.Stations[static_cast<size_t>(Index - 1)].Tangent : Vector3{ 1.0, 0.0, 0.0 });
        }
        Table.TotalLength = Cumulative;

        // Initial frame at τ₀ and Wang et al. (2008) double-reflection propagation.
        Table.Stations[0].Normal   = ChoosePerpendicular(Table.Stations[0].Tangent);
        Table.Stations[0].Binormal = Normalize(Cross(Table.Stations[0].Tangent, Table.Stations[0].Normal));
        for (size_t Index = 0; Index + 1 < Table.Stations.size(); ++Index)
        {
            const RailStation& Current = Table.Stations[Index];
            RailStation&       Next    = Table.Stations[Index + 1];
            const Vector3      Chord   = Next.Position - Current.Position;
            const double       Square  = Dot(Chord, Chord);
            if (Square <= 1.0e-28)
            {
                Next.Normal   = Normalize(Current.Normal - Next.Tangent * Dot(Current.Normal, Next.Tangent));
                Next.Binormal = Normalize(Cross(Next.Tangent, Next.Normal));
                continue;
            }
            const Vector3 ReflectedNormal  = Current.Normal - Chord * (2.0 * Dot(Chord, Current.Normal) / Square);
            const Vector3 ReflectedTangent = Current.Tangent - Chord * (2.0 * Dot(Chord, Current.Tangent) / Square);
            const Vector3 SecondAxis       = Next.Tangent - ReflectedTangent;
            const double  SecondSquare     = Dot(SecondAxis, SecondAxis);
            Vector3 CarriedNormal = SecondSquare > 1.0e-28
                                  ? ReflectedNormal - SecondAxis * (2.0 * Dot(SecondAxis, ReflectedNormal) / SecondSquare)
                                  : ReflectedNormal;
            CarriedNormal = CarriedNormal - Next.Tangent * Dot(CarriedNormal, Next.Tangent);
            Next.Normal   = Length(CarriedNormal) > 1.0e-14 ? Normalize(CarriedNormal) : ChoosePerpendicular(Next.Tangent);
            Next.Binormal = Normalize(Cross(Next.Tangent, Next.Normal));
        }
        return Table;
    }

    [[nodiscard]] EvaluatedStation EvaluateRailAt(const RailTable& Table, double ArcLength, bool ClosedCondition)
    {
        if (Table.Stations.empty() || Table.TotalLength <= 1.0e-14) return {};
        if (ClosedCondition)
        {
            ArcLength = std::fmod(ArcLength, Table.TotalLength);
            if (ArcLength < 0.0) ArcLength += Table.TotalLength;
        }
        if (ArcLength <= 0.0)
        {
            const RailStation& First = Table.Stations.front();
            const NurbsCurveDerivatives EndD = EvaluateNurbsCurveDerivatives(*Table.Definition, First.Parameter);
            const double Speed = Length(EndD.First);
            const Vector3 Curv = Speed > 1.0e-14 ? (EndD.Second - First.Tangent * Dot(EndD.Second, First.Tangent)) * (1.0 / (Speed * Speed)) : Vector3{};
            // C² Taylor/osculating continuation for tiny chart-margin overhangs at s ≤ 0.
            const Vector3 Pos  = First.Position + First.Tangent * ArcLength + Curv * (0.5 * ArcLength * ArcLength);
            const Vector3 Tan  = Normalize(First.Tangent + Curv * ArcLength);
            Vector3 Norm       = First.Normal - Tan * Dot(First.Normal, Tan);
            Norm               = Length(Norm) > 1.0e-14 ? Normalize(Norm) : ChoosePerpendicular(Tan);
            return { Pos, Tan, Norm, Normalize(Cross(Tan, Norm)), Curv };
        }
        if (ArcLength >= Table.TotalLength)
        {
            const RailStation& Last = Table.Stations.back();
            const double Delta = ArcLength - Table.TotalLength;
            const NurbsCurveDerivatives EndD = EvaluateNurbsCurveDerivatives(*Table.Definition, Last.Parameter);
            const double Speed = Length(EndD.First);
            const Vector3 Curv = Speed > 1.0e-14 ? (EndD.Second - Last.Tangent * Dot(EndD.Second, Last.Tangent)) * (1.0 / (Speed * Speed)) : Vector3{};
            const Vector3 Pos  = Last.Position + Last.Tangent * Delta + Curv * (0.5 * Delta * Delta);
            const Vector3 Tan  = Normalize(Last.Tangent + Curv * Delta);
            Vector3 Norm       = Last.Normal - Tan * Dot(Last.Normal, Tan);
            Norm               = Length(Norm) > 1.0e-14 ? Normalize(Norm) : ChoosePerpendicular(Tan);
            return { Pos, Tan, Norm, Normalize(Cross(Tan, Norm)), Curv };
        }
        const auto Upper = std::lower_bound(Table.Stations.begin(), Table.Stations.end(), ArcLength,
                                            [](const RailStation& Entry, double Target) { return Entry.ArcLength < Target; });
        const size_t Index = Upper == Table.Stations.begin() ? 0 : static_cast<size_t>(std::distance(Table.Stations.begin(), Upper) - 1);
        const RailStation& Left  = Table.Stations[Index];
        const RailStation& Right = Table.Stations[std::min(Index + 1, Table.Stations.size() - 1)];
        const double       Span  = Right.ArcLength - Left.ArcLength;
        const double       Blend = Span > 1.0e-15 ? std::clamp((ArcLength - Left.ArcLength) / Span, 0.0, 1.0) : 0.0;
        double             τ     = Left.Parameter + (Right.Parameter - Left.Parameter) * Blend;

        // Two Simpson-refined Newton steps polish τ to ~10⁻¹⁴ arc-length precision so τ(s) is C²-smooth.
        for (int Step = 0; Step < 2 && Span > 1.0e-14; ++Step)
        {
            const NurbsCurveDerivatives Eval = EvaluateNurbsCurveDerivatives(*Table.Definition, τ);
            const double Speed = Length(Eval.First);
            if (Speed <= 1.0e-14) break;
            const double LeftSpeed = Length(EvaluateNurbsCurve(*Table.Definition, Left.Parameter).Derivative);
            const double MidSpeed  = Length(EvaluateNurbsCurve(*Table.Definition, 0.5 * (Left.Parameter + τ)).Derivative);
            const double ApproxS   = Left.ArcLength + (τ - Left.Parameter) * (LeftSpeed + 4.0 * MidSpeed + Speed) / 6.0;
            τ = std::clamp(τ + (ArcLength - ApproxS) / Speed, Left.Parameter, Right.Parameter);
        }
        const NurbsCurveDerivatives Derivatives = EvaluateNurbsCurveDerivatives(*Table.Definition, τ);
        const double  Speed   = Length(Derivatives.First);
        const Vector3 Tangent = Speed > 1.0e-14 ? Derivatives.First * (1.0 / Speed) : Left.Tangent;

        // Exact double-reflection transport from Left to τ keeps Normal C²-smooth across station boundaries.
        Vector3 Normal = Left.Normal;
        const Vector3 Chord  = Derivatives.Position - Left.Position;
        const double  Square = Dot(Chord, Chord);
        if (Square > 1.0e-28)
        {
            const Vector3 RefN = Left.Normal - Chord * (2.0 * Dot(Chord, Left.Normal) / Square);
            const Vector3 RefT = Left.Tangent - Chord * (2.0 * Dot(Chord, Left.Tangent) / Square);
            const Vector3 Sec  = Tangent - RefT;
            const double  Sec2 = Dot(Sec, Sec);
            Normal = Sec2 > 1.0e-28 ? (RefN - Sec * (2.0 * Dot(Sec, RefN) / Sec2)) : RefN;
        }
        Normal = Normal - Tangent * Dot(Normal, Tangent);
        Normal = Length(Normal) > 1.0e-14 ? Normalize(Normal) : ChoosePerpendicular(Tangent);
        const Vector3 Binormal  = Normalize(Cross(Tangent, Normal));
        const Vector3 Curvature = Speed > 1.0e-14
                                ? (Derivatives.Second - Tangent * Dot(Derivatives.Second, Tangent)) * (1.0 / (Speed * Speed))
                                : Vector3{};
        return { Derivatives.Position, Tangent, Normal, Binormal, Curvature };
    }

    // Rigid frame-to-frame map carrying SourceOrigin + (SourceX, SourceY, SourceZ) onto TargetOrigin + (TargetX, TargetY, TargetZ).
    [[nodiscard]] AffineMap RigidFrameMap(Vector3 SourceOrigin, Vector3 SourceX, Vector3 SourceY, Vector3 SourceZ,
                                          Vector3 TargetOrigin, Vector3 TargetX, Vector3 TargetY, Vector3 TargetZ) noexcept
    {
        // R = [TargetX TargetY TargetZ] · [SourceX SourceY SourceZ]ᵀ.
        const auto Column = [&](Vector3 Unit)
        {
            return TargetX * Dot(SourceX, Unit) + TargetY * Dot(SourceY, Unit) + TargetZ * Dot(SourceZ, Unit);
        };
        const Vector3 ColX = Column({ 1.0, 0.0, 0.0 });
        const Vector3 ColY = Column({ 0.0, 1.0, 0.0 });
        const Vector3 ColZ = Column({ 0.0, 0.0, 1.0 });
        const Vector3 MappedOrigin = ColX * SourceOrigin.X + ColY * SourceOrigin.Y + ColZ * SourceOrigin.Z;
        return { ColX, ColY, ColZ, TargetOrigin - MappedOrigin };
    }

    // Face chart box sampled densely and padded safely within carrier limits so deformed B-splines cover every SP-curve point.
    [[nodiscard]] std::pair<Vector2, Vector2> MeasurePaddedChartBox(const ModelPartition& Partition, FaceToken Face)
    {
        Vector2 Low{ INFINITY, INFINITY }, High{ -INFINITY, -INFINITY };
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
                const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
                if (!Use.SurfaceCurve) return;
                for (int Sample = 0; Sample <= 256; ++Sample)
                {
                    const Vector2 Point = EvaluateParameterCurve(*Use.SurfaceCurve, Edge.IntervalLow + (Edge.IntervalHigh - Edge.IntervalLow) * Sample / 256.0).Position;
                    Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
                    High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
                }
            });
        }
        const SurfaceGeometry& Carrier = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
        const Vector2 Pad = (High - Low) * 1.0e-4 + Vector2{ 1.0e-7, 1.0e-7 };
        Low  = Low - Pad;
        High = High + Pad;
        if (std::holds_alternative<SphereSurface>(Carrier))
        {
            Low.Y  = std::max(Low.Y, -0.5 * std::numbers::pi);
            High.Y = std::min(High.Y,  0.5 * std::numbers::pi);
        }
        if (const NurbsSurface* Spline = ResolveNurbsChart(Carrier); Spline && Spline->Definition)
        {
            const NurbsDomain DomU = MeasureNurbsSurfaceDomain(*Spline->Definition, true);
            const NurbsDomain DomV = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
            if (Spline->Definition->PeriodU == 0.0) { Low.X = std::max(Low.X, DomU.Low); High.X = std::min(High.X, DomU.High); }
            if (Spline->Definition->PeriodV == 0.0) { Low.Y = std::max(Low.Y, DomV.Low); High.Y = std::min(High.Y, DomV.High); }
        }
        return { Low, High };
    }
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ModifierBodyOutcome CombineModifierInstances(ModelPartition& Partition, const std::vector<BodyToken>& Instances,
                                             const std::vector<BodyNaming>& Namings, const std::string& Label,
                                             bool MergeCondition)
{
    ModifierBodyOutcome Outcome;
    if (Instances.empty()) { Outcome.Refusal = Label + ": pattern produced no instances"; return Outcome; }
    Outcome.Instances = static_cast<int>(Instances.size());
    if (Instances.size() == 1)
    {
        Outcome.Body   = Instances.front();
        Outcome.Naming = Namings.front();
        return Outcome;
    }

    // Check whether every instance is a solid and whether any pair's bounding boxes overlap.
    bool AllSolid = true;
    std::vector<BoxBounds> Boxes(Instances.size());
    for (size_t Index = 0; Index < Instances.size(); ++Index)
    {
        if (Partition.Bodies.Resolve(Instances[Index]).Category != BodyCategory::Solid) AllSolid = false;
        Boxes[Index] = MeasureBodyBounds(Partition, Instances[Index]);
    }
    bool AnyOverlap = false;
    for (size_t First = 0; First < Boxes.size() && !AnyOverlap; ++First)
        for (size_t Second = First + 1; Second < Boxes.size() && !AnyOverlap; ++Second)
            if (Boxes[First].Overlaps(Boxes[Second], 1.0e-5)) AnyOverlap = true;

    // Fast path: non-merged solid instances (or disjoint merged instances) combine into one multi-region BodyToken in O(regions).
    if (AllSolid && (!MergeCondition || !AnyOverlap))
    {
        const BodyToken Combined = Instances.front();
        BodyNaming      Naming   = Namings.front();
        for (size_t Index = 1; Index < Instances.size(); ++Index)
        {
            for (RegionToken Region : Partition.Bodies.Resolve(Instances[Index]).Regions)
            {
                if (Partition.Regions.Resolve(Region).Material != RegionMaterial::Solid) continue;
                Partition.Regions.Resolve(Region).Body = Combined;
                Partition.Bodies.Resolve(Combined).Regions.push_back(Region);
            }
            for (const auto& [Slot, Name] : Namings[Index].Faces) Naming.Faces[Slot] = Name;
        }
        CompleteNaming(Partition, Combined, Naming);
        Outcome.Body   = Combined;
        Outcome.Naming = std::move(Naming);
        return Outcome;
    }

    // Boolean union path when MergeCondition is requested on touching / overlapping solids.
    if (AllSolid && MergeCondition)
    {
        BodyToken  Current = Instances.front();
        BodyNaming Naming  = Namings.front();
        for (size_t Index = 1; Index < Instances.size(); ++Index)
        {
            const BoxBounds CurrentBox = MeasureBodyBounds(Partition, Current);
            if (!CurrentBox.Overlaps(Boxes[Index], 1.0e-5))
            {
                for (RegionToken Region : Partition.Bodies.Resolve(Instances[Index]).Regions)
                {
                    if (Partition.Regions.Resolve(Region).Material != RegionMaterial::Solid) continue;
                    Partition.Regions.Resolve(Region).Body = Current;
                    Partition.Bodies.Resolve(Current).Regions.push_back(Region);
                }
                for (const auto& [Slot, Name] : Namings[Index].Faces) Naming.Faces[Slot] = Name;
                CompleteNaming(Partition, Current, Naming);
                continue;
            }
            const BooleanOutcome Fused = ComputeBoolean(Partition, Current, Instances[Index], BooleanOperation::Union);
            if (!Fused.Refusal.empty()) { Outcome.Refusal = Label + ": " + Fused.Refusal; return Outcome; }
            Naming  = NameBooleanBody(Partition, Fused.Body, Fused, Naming, Namings[Index]);
            Current = Fused.Body;
        }
        Outcome.Body   = Current;
        Outcome.Naming = std::move(Naming);
        return Outcome;
    }

    // Fallback (overlapping non-merged solids or sheet arrays): fuse overlapping solids, or assemble sheet blueprint.
    if (AllSolid)
    {
        BodyToken  Current = Instances.front();
        BodyNaming Naming  = Namings.front();
        for (size_t Index = 1; Index < Instances.size(); ++Index)
        {
            const BooleanOutcome Fused = ComputeBoolean(Partition, Current, Instances[Index], BooleanOperation::Union);
            if (!Fused.Refusal.empty()) { Outcome.Refusal = Label + ": " + Fused.Refusal; return Outcome; }
            Naming  = NameBooleanBody(Partition, Fused.Body, Fused, Naming, Namings[Index]);
            Current = Fused.Body;
        }
        Outcome.Body   = Current;
        Outcome.Naming = std::move(Naming);
        return Outcome;
    }

    Outcome.Refusal = Label + ": array modifiers require a closed solid body";
    return Outcome;
}

ModifierBodyOutcome ArrayBodyGrid(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                  const std::string& Label, Vector3 StepX, int CountX,
                                  Vector3 StepY, int CountY, bool MergeCondition)
{
    FRONTIER_TRACE_SCOPE("Modifier", Label + " · LinearArray " + std::to_string(CountX) + "×" + std::to_string(CountY));
    ModifierBodyOutcome Outcome;
    if (CountX < 1 || CountY < 1) { Outcome.Refusal = Label + ": array count must be at least 1"; return Outcome; }
    if (static_cast<long long>(CountX) * CountY > 512) { Outcome.Refusal = Label + ": array count exceeds 512 instances"; return Outcome; }
    if (CountX > 1 && Length(StepX) <= 1.0e-12) { Outcome.Refusal = Label + ": primary array step is zero"; return Outcome; }
    if (CountY > 1 && Length(StepY) <= 1.0e-12) { Outcome.Refusal = Label + ": secondary array step is zero"; return Outcome; }

    std::vector<BodyToken>  Instances;
    std::vector<BodyNaming> Namings;
    for (int J = 0; J < CountY; ++J)
    {
        for (int I = 0; I < CountX; ++I)
        {
            const Vector3 Shift  = StepX * static_cast<double>(I) + StepY * static_cast<double>(J);
            const std::string Prefix = (I == 0 && J == 0) ? std::string{} : (Label + "[" + std::to_string(J * CountX + I) + "]");
            const TransformOutcome Copy = TransformBody(Partition, Source, TranslationMap(Shift));
            if (!Copy.Refusal.empty()) { Outcome.Refusal = Label + ": " + Copy.Refusal; return Outcome; }
            Namings.push_back(CopyInstanceNaming(Partition, Copy, SourceNaming, Prefix));
            Instances.push_back(Copy.Body);
        }
    }
    return CombineModifierInstances(Partition, Instances, Namings, Label, MergeCondition);
}

ModifierBodyOutcome ArrayBodyRadial(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                    const std::string& Label, Vector3 AxisOrigin, Vector3 AxisDirection,
                                    int Count, double SweepAngle, double AxialOffset, bool MergeCondition)
{
    FRONTIER_TRACE_SCOPE("Modifier", Label + " · CircularArray ×" + std::to_string(Count));
    ModifierBodyOutcome Outcome;
    if (Count < 1) { Outcome.Refusal = Label + ": circular array count must be at least 1"; return Outcome; }
    if (Count > 512) { Outcome.Refusal = Label + ": circular array count exceeds 512 instances"; return Outcome; }
    if (Length(AxisDirection) <= 1.0e-12) { Outcome.Refusal = Label + ": circular array axis is zero"; return Outcome; }
    if (Count > 1 && std::fabs(SweepAngle) <= 1.0e-12 && std::fabs(AxialOffset) <= 1.0e-12)
    {
        Outcome.Refusal = Label + ": circular sweep angle is zero";
        return Outcome;
    }

    const Vector3 UnitAxis = Normalize(AxisDirection);
    const bool    FullTurn = std::fabs(std::fabs(SweepAngle) - 2.0 * std::numbers::pi) <= 1.0e-7;
    const double  Pitch    = Count > 1 ? (FullTurn ? SweepAngle / static_cast<double>(Count)
                                                   : SweepAngle / static_cast<double>(Count - 1)) : 0.0;
    const double  RiseStep = Count > 1 ? AxialOffset / static_cast<double>(FullTurn ? Count : (Count - 1)) : 0.0;

    std::vector<BodyToken>  Instances;
    std::vector<BodyNaming> Namings;
    for (int Index = 0; Index < Count; ++Index)
    {
        AffineMap Map = RotationMap(AxisOrigin, UnitAxis, Pitch * static_cast<double>(Index));
        Map.Shift = Map.Shift + UnitAxis * (RiseStep * static_cast<double>(Index));
        const std::string Prefix = Index == 0 ? std::string{} : (Label + "[" + std::to_string(Index) + "]");
        const TransformOutcome Copy = TransformBody(Partition, Source, Map);
        if (!Copy.Refusal.empty()) { Outcome.Refusal = Label + ": " + Copy.Refusal; return Outcome; }
        Namings.push_back(CopyInstanceNaming(Partition, Copy, SourceNaming, Prefix));
        Instances.push_back(Copy.Body);
    }
    return CombineModifierInstances(Partition, Instances, Namings, Label, MergeCondition);
}

ModifierBodyOutcome ArrayBodyAlongCurve(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                        const std::string& Label, const NurbsCurve& Rail, bool ClosedRailCondition,
                                        int Count, double Spacing, bool AlignCondition, bool MergeCondition)
{
    FRONTIER_TRACE_SCOPE("Modifier", Label + " · CurveArray ×" + std::to_string(Count));
    ModifierBodyOutcome Outcome;
    if (Count < 1) { Outcome.Refusal = Label + ": curve array count must be at least 1"; return Outcome; }
    if (Count > 512) { Outcome.Refusal = Label + ": curve array count exceeds 512 instances"; return Outcome; }
    if (!Rail.Definition) { Outcome.Refusal = Label + ": rail curve is empty"; return Outcome; }

    const RailTable Table = BuildRailTable(Rail);
    if (!(Table.TotalLength > 1.0e-9)) { Outcome.Refusal = Label + ": rail curve has zero length"; return Outcome; }

    const BoxBounds SourceBox = MeasureBodyBounds(Partition, Source);
    if (!SourceBox.ValidCondition()) { Outcome.Refusal = Label + ": source body has no geometry"; return Outcome; }
    const Vector3 SourceCentre = (SourceBox.Low + SourceBox.High) * 0.5;
    const EvaluatedStation StartStation = EvaluateRailAt(Table, 0.0, ClosedRailCondition);

    const double Step = Spacing > 1.0e-12 ? Spacing
                      : (Count > 1 ? Table.TotalLength / static_cast<double>(ClosedRailCondition ? Count : (Count - 1)) : 0.0);

    std::vector<BodyToken>  Instances;
    std::vector<BodyNaming> Namings;
    for (int Index = 0; Index < Count; ++Index)
    {
        const EvaluatedStation Station = EvaluateRailAt(Table, Step * static_cast<double>(Index), ClosedRailCondition);
        const AffineMap Map = AlignCondition
                            ? RigidFrameMap(SourceCentre, StartStation.Tangent, StartStation.Normal, StartStation.Binormal,
                                            Station.Position, Station.Tangent, Station.Normal, Station.Binormal)
                            : TranslationMap(Station.Position - SourceCentre);
        const std::string Prefix = Index == 0 ? std::string{} : (Label + "[" + std::to_string(Index) + "]");
        const TransformOutcome Copy = TransformBody(Partition, Source, Map);
        if (!Copy.Refusal.empty()) { Outcome.Refusal = Label + ": " + Copy.Refusal; return Outcome; }
        Namings.push_back(CopyInstanceNaming(Partition, Copy, SourceNaming, Prefix));
        Instances.push_back(Copy.Body);
    }
    return CombineModifierInstances(Partition, Instances, Namings, Label, MergeCondition);
}

ModifierBodyOutcome ArrayBodyOnSurface(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                       const std::string& Label, const FaceDomain& Domain,
                                       int CountU, int CountV, double Margin, double NormalOffset,
                                       bool AlignCondition, bool MergeCondition)
{
    FRONTIER_TRACE_SCOPE("Modifier", Label + " · SurfaceArray " + std::to_string(CountU) + "×" + std::to_string(CountV));
    ModifierBodyOutcome Outcome;
    if (CountU < 1 || CountV < 1) { Outcome.Refusal = Label + ": surface array counts must be at least 1"; return Outcome; }
    if (static_cast<long long>(CountU) * CountV > 512) { Outcome.Refusal = Label + ": surface array exceeds 512 stations"; return Outcome; }

    const BoxBounds SourceBox = MeasureBodyBounds(Partition, Source);
    if (!SourceBox.ValidCondition()) { Outcome.Refusal = Label + ": source body has no geometry"; return Outcome; }
    // Anchor at the bottom-z centre of the source body so features sit cleanly on the target surface.
    const Vector3 Anchor = { 0.5 * (SourceBox.Low.X + SourceBox.High.X), 0.5 * (SourceBox.Low.Y + SourceBox.High.Y), SourceBox.Low.Z };

    const SurfacePeriodicity Period = ResolvePeriodicity(Domain.Surface);
    const double SpanU = Domain.Window.High.X - Domain.Window.Low.X;
    const double SpanV = Domain.Window.High.Y - Domain.Window.Low.Y;
    if (!(SpanU > 1.0e-12 && SpanV > 1.0e-12)) { Outcome.Refusal = Label + ": target face has degenerate UV window"; return Outcome; }

    const double ClampedMargin = std::clamp(Margin, 0.0, 0.45);
    const bool   WrapU = Period.PeriodU > 0.0 && std::fabs(SpanU - Period.PeriodU) <= 1.0e-5 * Period.PeriodU;
    const bool   WrapV = Period.PeriodV > 0.0 && std::fabs(SpanV - Period.PeriodV) <= 1.0e-5 * Period.PeriodV;

    const auto SampleAxis = [](double Low, double High, int Count, double Inset, bool Periodic)
    {
        std::vector<double> Coordinates;
        if (Count == 1) { Coordinates.push_back(0.5 * (Low + High)); return Coordinates; }
        if (Periodic)
        {
            for (int K = 0; K < Count; ++K) Coordinates.push_back(Low + (High - Low) * static_cast<double>(K) / static_cast<double>(Count));
            return Coordinates;
        }
        const double Start = Low + (High - Low) * Inset;
        const double End   = High - (High - Low) * Inset;
        for (int K = 0; K < Count; ++K) Coordinates.push_back(Start + (End - Start) * static_cast<double>(K) / static_cast<double>(Count - 1));
        return Coordinates;
    };

    const std::vector<double> GridU = SampleAxis(Domain.Window.Low.X, Domain.Window.High.X, CountU, ClampedMargin, WrapU);
    const std::vector<double> GridV = SampleAxis(Domain.Window.Low.Y, Domain.Window.High.Y, CountV, ClampedMargin, WrapV);
    const double SenseSign = Domain.Sense == Orientation::Forward ? 1.0 : -1.0;

    std::vector<BodyToken>  Instances;
    std::vector<BodyNaming> Namings;
    int Placed = 0;
    for (double U : GridU)
    {
        for (double V : GridV)
        {
            if (ClassifyParameterPoint(Domain, { U, V }) == PointMembership::Outside) continue;
            const SurfaceSample Sample  = EvaluateSurface(Domain.Surface, { U, V });
            const Vector3       Outward = EvaluateSurfaceNormal(Domain.Surface, { U, V }) * SenseSign;
            Vector3 TangentU = Sample.DerivativeU - Outward * Dot(Sample.DerivativeU, Outward);
            if (Length(TangentU) <= 1.0e-12) TangentU = ChoosePerpendicular(Outward);
            else TangentU = Normalize(TangentU);
            const Vector3 TangentV = Normalize(Cross(Outward, TangentU));
            const Vector3 TargetPoint = Sample.Position + Outward * NormalOffset;

            const AffineMap Map = AlignCondition
                                ? RigidFrameMap(Anchor, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 },
                                                TargetPoint, TangentU, TangentV, Outward)
                                : TranslationMap(TargetPoint - Anchor);
            const std::string Prefix = Placed == 0 ? std::string{} : (Label + "[" + std::to_string(Placed) + "]");
            const TransformOutcome Copy = TransformBody(Partition, Source, Map);
            if (!Copy.Refusal.empty()) { Outcome.Refusal = Label + ": " + Copy.Refusal; return Outcome; }
            Namings.push_back(CopyInstanceNaming(Partition, Copy, SourceNaming, Prefix));
            Instances.push_back(Copy.Body);
            ++Placed;
        }
    }
    if (Instances.empty()) { Outcome.Refusal = Label + ": no UV sample fell inside the trimmed target face (reduce margin or count)"; return Outcome; }
    return CombineModifierInstances(Partition, Instances, Namings, Label, MergeCondition);
}

ModifierBodyOutcome DeformBodyAlongCurve(ModelPartition& Partition, BodyToken Source, const BodyNaming& SourceNaming,
                                         const std::string& Label, const NurbsCurve& Rail,
                                         int DeformAxis, bool StretchCondition, const ApproximationSettings& Settings)
{
    FRONTIER_TRACE_SCOPE("Modifier", Label + " · CurveBend");
    ModifierBodyOutcome Outcome;
    if (!Rail.Definition) { Outcome.Refusal = Label + ": bend curve is empty"; return Outcome; }

    const BoxBounds Bounds = MeasureBodyBounds(Partition, Source);
    if (!Bounds.ValidCondition()) { Outcome.Refusal = Label + ": source body has no geometry"; return Outcome; }
    const Vector3 Extents = Bounds.High - Bounds.Low;
    const double  Spans[3] = { Extents.X, Extents.Y, Extents.Z };

    int Axis = DeformAxis;
    if (Axis < 0 || Axis > 2)
        Axis = (Spans[1] > Spans[0] && Spans[1] >= Spans[2]) ? 1 : (Spans[2] > Spans[0] && Spans[2] > Spans[1] ? 2 : 0);
    if (!(Spans[Axis] > 1.0e-9)) { Outcome.Refusal = Label + ": source body has zero extent along the bend axis"; return Outcome; }

    static constexpr Vector3 UnitAxes[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const Vector3 AlongAxis  = UnitAxes[Axis];
    const Vector3 LateralOne = UnitAxes[(Axis + 1) % 3];
    const Vector3 LateralTwo = UnitAxes[(Axis + 2) % 3];
    const Vector3 Centre     = (Bounds.Low + Bounds.High) * 0.5;
    const Vector3 Reference  = Centre - AlongAxis * (0.5 * Spans[Axis]);                     // [len] - Start face centre on bend axis

    const RailTable Table = BuildRailTable(Rail);
    if (!(Table.TotalLength > 1.0e-9)) { Outcome.Refusal = Label + ": bend curve has zero length"; return Outcome; }

    const double LateralReach = 0.5 * std::hypot(Spans[(Axis + 1) % 3], Spans[(Axis + 2) % 3]);
    if (Table.MaxCurvature * LateralReach >= 0.95)
    {
        Outcome.Refusal = Label + ": body thickness exceeds the bend curve's tightest radius of curvature (would self-intersect)";
        return Outcome;
    }

    const double LengthScale = StretchCondition ? (Table.TotalLength / Spans[Axis]) : 1.0;

    // Smooth 3D RMF spatial map Φ(P) = C(s) + y₁·N(s) + y₂·B(s) and its exact Bishop-frame Jacobian DΦ(P).
    const auto MapPoint = [&](Vector3 Point) -> Vector3
    {
        const Vector3 Relative = Point - Reference;
        const double  Along    = Dot(Relative, AlongAxis);
        const double  Coord1   = Dot(Relative, LateralOne);
        const double  Coord2   = Dot(Relative, LateralTwo);
        const EvaluatedStation Station = EvaluateRailAt(Table, Along * LengthScale, false);
        return Station.Position + Station.Normal * Coord1 + Station.Binormal * Coord2;
    };

    const auto MapSurfaceSample = [&](const SurfaceSample& Sample) -> SurfaceSample
    {
        const Vector3 Relative = Sample.Position - Reference;
        const double  Along    = Dot(Relative, AlongAxis);
        const double  Coord1   = Dot(Relative, LateralOne);
        const double  Coord2   = Dot(Relative, LateralTwo);
        const EvaluatedStation Station = EvaluateRailAt(Table, Along * LengthScale, false);
        // Bishop / RMF exact derivatives: N′(s) = −(κ·N) T, B′(s) = −(κ·B) T.
        const double  ScaleFactor = LengthScale * (1.0 - Coord1 * Dot(Station.Curvature, Station.Normal) - Coord2 * Dot(Station.Curvature, Station.Binormal));
        const Vector3 ImageAlong  = Station.Tangent * ScaleFactor;
        const auto PushVector = [&](Vector3 Tangent) -> Vector3
        {
            return ImageAlong * Dot(Tangent, AlongAxis) + Station.Normal * Dot(Tangent, LateralOne) + Station.Binormal * Dot(Tangent, LateralTwo);
        };
        return { Station.Position + Station.Normal * Coord1 + Station.Binormal * Coord2,
                 PushVector(Sample.DerivativeU), PushVector(Sample.DerivativeV) };
    };

    const BodyInventory Inventory = Partition.CollectInventory(Source);
    BodyBlueprint       Blueprint;
    Blueprint.Category = Partition.Bodies.Resolve(Source).Category;

    std::map<uint32_t, int> VertexIndex, EdgeIndex;
    for (VertexToken Vertex : Inventory.Vertices)
    {
        VertexIndex[Vertex.Slot] = static_cast<int>(Blueprint.Vertices.size());
        Blueprint.Vertices.push_back({ MapPoint(Partition.Vertices.Resolve(Vertex).Position) });
    }

    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Carrier = Partition.Curves.Resolve(Profile.Curve);
        CurveApproximation   Stand   = ApproximateDeformedCurve(Carrier, MapPoint, Profile.IntervalLow, Profile.IntervalHigh, Settings);
        if (!Stand.Refusal.empty()) { Outcome.Refusal = Label + ": edge " + std::to_string(Edge.Slot) + " — " + Stand.Refusal; return Outcome; }
        // Pin first and last poles bit-for-bit onto the mapped vertices so G1 endpoint error is identically zero.
        NurbsCurveDefinition Pinned = *Stand.Curve->Definition;
        if (!Pinned.Poles.empty())
        {
            Pinned.Poles.front() = Blueprint.Vertices[VertexIndex.at(Profile.Start.Slot)].Position;
            Pinned.Poles.back()  = Blueprint.Vertices[VertexIndex.at(Profile.End.Slot)].Position;
        }
        const std::optional<NurbsCurve> BuiltCurve = ConstructNurbsCurve(Pinned.Degree, std::move(Pinned.Knots), std::move(Pinned.Poles), std::move(Pinned.Weights));
        if (!BuiltCurve) { Outcome.Refusal = Label + ": deformed edge spline invalid"; return Outcome; }
        Outcome.Deviation    = std::max(Outcome.Deviation, Stand.Deviation);
        EdgeIndex[Edge.Slot] = static_cast<int>(Blueprint.Edges.size());
        Blueprint.Edges.push_back({ *BuiltCurve, Profile.IntervalLow, Profile.IntervalHigh, VertexIndex.at(Profile.Start.Slot), VertexIndex.at(Profile.End.Slot) });
    }

    for (FaceToken Face : Inventory.Faces)
    {
        const FaceProfile&     Profile = Partition.Faces.Resolve(Face);
        const SurfaceGeometry& Carrier = Partition.Surfaces.Resolve(Profile.Surface);
        const auto [Low, High]         = MeasurePaddedChartBox(Partition, Face);
        const SurfaceApproximation Stand = ApproximateDeformedSurface(Carrier, MapSurfaceSample, Low, High, Settings);
        if (!Stand.Refusal.empty()) { Outcome.Refusal = Label + ": face — " + Stand.Refusal; return Outcome; }
        Outcome.Deviation = std::max(Outcome.Deviation, Stand.Deviation);

        BlueprintFace Image;
        Image.Surface = *Stand.Surface;
        Image.Sense   = Profile.Sense;                                                       // det DΦ > 0 preserves orientation everywhere
        for (LoopToken Loop : Profile.Loops)
        {
            std::vector<BlueprintUse> Uses;
            bool Valid = true;
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile& Use = Partition.Fins.Resolve(Fin);
                if (!Use.SurfaceCurve) { Valid = false; return; }
                Uses.push_back({ EdgeIndex.at(Use.Edge.Slot), Use.Sense, *Use.SurfaceCurve });
            });
            if (!Valid) { Outcome.Refusal = Label + ": source fin lacks SP-curve"; return Outcome; }
            Image.Loops.push_back(std::move(Uses));
        }
        Blueprint.Faces.push_back(std::move(Image));
    }

    // Parasolid-style local precision tube (3 × Tolerance covers surface + curve stand-in deviations; vertex ≥ edge).
    const double Tube = std::fmax(SessionPrecision, 3.0 * Settings.Tolerance);
    for (BlueprintEdge& Edge : Blueprint.Edges)
    {
        Edge.Precision = Tube;
        Blueprint.Vertices[Edge.Start].Precision = std::fmax(Blueprint.Vertices[Edge.Start].Precision, Tube);
        Blueprint.Vertices[Edge.End].Precision   = std::fmax(Blueprint.Vertices[Edge.End].Precision, Tube);
    }

    const AssemblyOutcome Assembly = AssembleBody(Partition, Blueprint, Label);
    if (!Assembly.Refusal.empty()) { Outcome.Refusal = Assembly.Refusal; return Outcome; }

    BodyNaming Naming;
    for (size_t Index = 0; Index < Inventory.Faces.size() && Index < Assembly.Faces.size(); ++Index)
    {
        const auto Found = SourceNaming.Faces.find(Inventory.Faces[Index].Slot);
        if (Found != SourceNaming.Faces.end()) Naming.Faces[Assembly.Faces[Index].Slot] = Found->second;
    }
    CompleteNaming(Partition, Assembly.Body, Naming);
    Outcome.Body      = Assembly.Body;
    Outcome.Naming    = std::move(Naming);
    Outcome.Instances = 1;
    return Outcome;
}

} // namespace Frontier::GeometrySpace::Parametric
