/*====================================================================================================================================
                                                      MINIMUMDISTANCE.CPP
====================================================================================================================================*/
// 🧩 Minimum distance between B-rep entities (vertex, edge, face) with the two closest points

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Measurement/MinimumDistance.h"
#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int GridSamples     = 24;                                                      // [-] - Face chart grid per side
    constexpr int CurveSamples    = 48;                                                      // [-] - Samples per edge / boundary curve
    constexpr int SeedLimit       = 8;                                                       // [-] - Refined sample pairs
    constexpr int RoundLimit      = 400;                                                     // [-] - Projection rounds per seed

    struct LocusSample
    {
        Vector3 Position  = {};                                                              // [len] - Point
        Vector2 Parameter = {};                                                              // [-]   - Chart point (faces)
    };

    // 📝 One target, ready for closest-point queries.
    struct Locus
    {
        std::optional<Vector3>           Point    = {};                                      // [len] - A vertex
        std::optional<DomainBoundaryCurve> Curve  = {};                                      // [-]   - An edge
        std::optional<FaceDomain>        Domain   = {};                                      // [-]   - A face
        std::vector<LocusSample>         Samples  = {};                                      // [-]   - Seeds
    };

    [[nodiscard]] LocusSample ClosestOnCurve(const DomainBoundaryCurve& Curve, Vector3 Position)
    {
        LocusSample Best;
        double      BestDistance = std::numeric_limits<double>::infinity();
        const auto Consider = [&](double τ)
        {
            const Vector3 Point = EvaluateCurve(Curve.Curve, τ).Position;
            const double  Gap   = Distance(Point, Position);
            if (Gap < BestDistance) { BestDistance = Gap; Best.Position = Point; Best.Parameter = { τ, 0.0 }; }
        };
        const double τ = ProjectOntoCurve(Curve.Curve, Position, Curve.Low);
        if (std::isfinite(τ) && τ >= Curve.Low && τ <= Curve.High) Consider(τ);
        Consider(Curve.Low);
        Consider(Curve.High);
        return Best;
    }

    // Periodic images of a chart point that may fall inside the window (a projection folds 𝑢 into its own range).
    [[nodiscard]] std::vector<Vector2> ChartImages(const FaceDomain& Domain, Vector2 Parameter)
    {
        std::vector<Vector2> Images = { Parameter };
        const SurfacePeriodicity Period = ResolvePeriodicity(Domain.Surface);
        for (int ShiftU = -1; ShiftU <= 1; ++ShiftU)
        {
            for (int ShiftV = -1; ShiftV <= 1; ++ShiftV)
            {
                if ((ShiftU == 0 && ShiftV == 0) || (ShiftU != 0 && !(Period.PeriodU > 0.0)) || (ShiftV != 0 && !(Period.PeriodV > 0.0))) continue;
                Images.push_back({ Parameter.X + ShiftU * Period.PeriodU, Parameter.Y + ShiftV * Period.PeriodV });
            }
        }
        return Images;
    }

    [[nodiscard]] LocusSample ClosestOnFace(const FaceDomain& Domain, Vector3 Position, Vector2 Hint)
    {
        const Vector2 Foot = ProjectOntoSurface(Domain.Surface, Position, Hint);
        for (Vector2 Image : ChartImages(Domain, Foot))
        {
            if (ClassifyParameterPoint(Domain, Image) != PointMembership::Outside) return { EvaluateSurface(Domain.Surface, Image).Position, Image };
        }
        // ① The surface foot lies outside the trimmed face: the nearest point of its boundary.
        LocusSample Best;
        double      BestDistance = std::numeric_limits<double>::infinity();
        for (const DomainBoundaryCurve& Curve : Domain.BoundaryCurves)
        {
            const LocusSample Candidate = ClosestOnCurve(Curve, Position);
            const double      Gap       = Distance(Candidate.Position, Position);
            if (Gap < BestDistance) { BestDistance = Gap; Best = Candidate; }
        }
        // ② A chart hint for the next round: the boundary point's own foot.
        Best.Parameter = ProjectOntoSurface(Domain.Surface, Best.Position, Hint);
        return Best;
    }

    [[nodiscard]] LocusSample ClosestOn(const Locus& Target, Vector3 Position, Vector2 Hint)
    {
        if (Target.Point) return { *Target.Point, {} };
        if (Target.Curve) return ClosestOnCurve(*Target.Curve, Position);
        return ClosestOnFace(*Target.Domain, Position, Hint);
    }

    void SampleCurve(const DomainBoundaryCurve& Curve, std::vector<LocusSample>& Samples)
    {
        for (int Index = 0; Index <= CurveSamples; ++Index)
        {
            const double τ = Curve.Low + (Curve.High - Curve.Low) * Index / CurveSamples;
            Samples.push_back({ EvaluateCurve(Curve.Curve, τ).Position, { τ, 0.0 } });
        }
    }

    [[nodiscard]] std::optional<Locus> DescribeLocus(const ModelPartition& Partition, const MeasureTarget& Target, std::string& Refusal)
    {
        Locus Result;
        if (const auto* Vertex = std::get_if<VertexToken>(&Target))
        {
            Result.Point = Partition.Vertices.Resolve(*Vertex).Position;
            Result.Samples.push_back({ *Result.Point, {} });
            return Result;
        }
        if (const auto* Edge = std::get_if<EdgeToken>(&Target))
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(*Edge);
            if (!Profile.Curve.PresentCondition()) { Refusal = "Distance: the edge has no curve"; return std::nullopt; }
            Result.Curve = DomainBoundaryCurve{ Partition.Curves.Resolve(Profile.Curve), Profile.IntervalLow, Profile.IntervalHigh };
            SampleCurve(*Result.Curve, Result.Samples);
            return Result;
        }
        const FaceToken Face = std::get<FaceToken>(Target);
        if (!Partition.Faces.Resolve(Face).Surface.PresentCondition()) { Refusal = "Distance: the face has no surface"; return std::nullopt; }
        Result.Domain = DescribeFaceDomain(Partition, Face);
        const ParameterWindow& Window = Result.Domain->Window;
        for (int Row = 0; Row <= GridSamples; ++Row)
        {
            for (int Column = 0; Column <= GridSamples; ++Column)
            {
                const Vector2 Parameter = { Window.Low.X + (Window.High.X - Window.Low.X) * (Column + 0.5) / (GridSamples + 1),
                                            Window.Low.Y + (Window.High.Y - Window.Low.Y) * (Row + 0.5) / (GridSamples + 1) };
                if (ClassifyParameterPoint(*Result.Domain, Parameter) == PointMembership::Inside) Result.Samples.push_back({ EvaluateSurface(Result.Domain->Surface, Parameter).Position, Parameter });
            }
        }
        for (const DomainBoundaryCurve& Curve : Result.Domain->BoundaryCurves)
        {
            const size_t First = Result.Samples.size();
            SampleCurve(Curve, Result.Samples);
            for (size_t Index = First; Index < Result.Samples.size(); ++Index) Result.Samples[Index].Parameter = ProjectOntoSurface(Result.Domain->Surface, Result.Samples[Index].Position, { 0.5 * (Window.Low.X + Window.High.X), 0.5 * (Window.Low.Y + Window.High.Y) });
        }
        if (Result.Samples.empty()) { Refusal = "Distance: the face has no samples"; return std::nullopt; }
        return Result;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

DistanceMeasure MeasureMinimumDistance(const ModelPartition& Partition, const MeasureTarget& First, const MeasureTarget& Second)
{
    DistanceMeasure Measure;
    const std::optional<Locus> FirstLocus  = DescribeLocus(Partition, First, Measure.Refusal);
    const std::optional<Locus> SecondLocus = DescribeLocus(Partition, Second, Measure.Refusal);
    if (!FirstLocus || !SecondLocus) return Measure;

    // ---- ① sample pairs, closest first
    struct SeedPair
    {
        double Gap    = 0.0;                                                                 // [len] - Sample distance
        size_t First  = 0;                                                                   // [idx] - First sample
        size_t Second = 0;                                                                   // [idx] - Second sample
    };
    std::vector<SeedPair> Pairs;
    Pairs.reserve(FirstLocus->Samples.size() * SecondLocus->Samples.size());
    Vector3 Low = FirstLocus->Samples.front().Position, High = Low;
    for (size_t FirstIndex = 0; FirstIndex < FirstLocus->Samples.size(); ++FirstIndex)
    {
        const Vector3 Point = FirstLocus->Samples[FirstIndex].Position;
        Low = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
        High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
        for (size_t SecondIndex = 0; SecondIndex < SecondLocus->Samples.size(); ++SecondIndex) Pairs.push_back({ Distance(Point, SecondLocus->Samples[SecondIndex].Position), FirstIndex, SecondIndex });
    }
    for (const LocusSample& Sample : SecondLocus->Samples)
    {
        Low = { std::min(Low.X, Sample.Position.X), std::min(Low.Y, Sample.Position.Y), std::min(Low.Z, Sample.Position.Z) };
        High = { std::max(High.X, Sample.Position.X), std::max(High.Y, Sample.Position.Y), std::max(High.Z, Sample.Position.Z) };
    }
    const double Scale = std::max(1.0e-12, Length(High - Low));                              // [len] - Joint extent
    std::sort(Pairs.begin(), Pairs.end(), [](const SeedPair& Left, const SeedPair& Right) { return Left.Gap < Right.Gap; });

    // ---- ② spread seeds: skip pairs whose first point lies within 2 % of the extent of an earlier seed
    std::vector<SeedPair> Seeds;
    for (const SeedPair& Pair : Pairs)
    {
        if (static_cast<int>(Seeds.size()) >= SeedLimit) break;
        bool Crowded = false;
        for (const SeedPair& Seed : Seeds) Crowded = Crowded || (Distance(FirstLocus->Samples[Seed.First].Position, FirstLocus->Samples[Pair.First].Position) < 0.02 * Scale
                                                               && Distance(SecondLocus->Samples[Seed.Second].Position, SecondLocus->Samples[Pair.Second].Position) < 0.02 * Scale);
        if (!Crowded) Seeds.push_back(Pair);
    }

    // ---- ③ alternating projections from every seed
    Measure.Distance = std::numeric_limits<double>::infinity();
    for (const SeedPair& Seed : Seeds)
    {
        LocusSample FirstPoint  = FirstLocus->Samples[Seed.First];
        LocusSample SecondPoint = SecondLocus->Samples[Seed.Second];
        int Round = 0;
        double Previous = Distance(FirstPoint.Position, SecondPoint.Position);
        for (; Round < RoundLimit; ++Round)
        {
            SecondPoint = ClosestOn(*SecondLocus, FirstPoint.Position, SecondPoint.Parameter);
            const LocusSample Next = ClosestOn(*FirstLocus, SecondPoint.Position, FirstPoint.Parameter);
            const double Moved = Distance(Next.Position, FirstPoint.Position);
            FirstPoint = Next;
            const double Gap = Distance(FirstPoint.Position, SecondPoint.Position);
            if (Moved <= 1.0e-15 * Scale || (Previous - Gap <= 1.0e-17 * Scale && Moved <= 1.0e-9 * Scale)) break;
            Previous = Gap;
        }
        SecondPoint = ClosestOn(*SecondLocus, FirstPoint.Position, SecondPoint.Parameter);
        const double Gap = Distance(FirstPoint.Position, SecondPoint.Position);
        ++Measure.Seeds;
        if (Gap < Measure.Distance)
        {
            Measure.Distance = Gap;
            Measure.First    = FirstPoint.Position;
            Measure.Second   = SecondPoint.Position;
            Measure.Rounds   = Round;
        }
    }
    return Measure;
}

} // namespace Frontier::GeometrySpace::Parametric
