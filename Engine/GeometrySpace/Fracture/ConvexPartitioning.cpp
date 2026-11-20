/*============================================================================================================================================
                                                         CONVEXPARTITIONING.CPP
============================================================================================================================================*/
// 🧩 Exact 3D convex polyhedron half-space clipping, PolygonNetwork interchange, 6-solver fracture dispatch, bonds, and SDF sections

#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/GeometrySpace/Fracture/EngineeringBakedFracture.h"
#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"
#include "Engine/GeometrySpace/Primitives/WatertightSewing.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::GeometrySpace::Fracture
{

namespace
{

constexpr double PiConst = 3.14159265358979323846;

Mathematics::Axis3D PerpendicularVector(Mathematics::Axis3D N) noexcept
{
    const Mathematics::Axis3D Ref = std::abs(N.Z) < 0.85f
        ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f }
        : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
    return Mathematics::NormalizeAxis(Mathematics::EvaluateCross(N, Ref), { 1.0f, 0.0f, 0.0f });
}

struct VertexKey
{
    int64_t X = 0;
    int64_t Y = 0;
    int64_t Z = 0;
    bool operator==(const VertexKey&) const noexcept = default;
};

struct VertexKeyHasher
{
    size_t operator()(const VertexKey& K) const noexcept
    {
        uint64_t H = static_cast<uint64_t>(K.X) * 73856093ull;
        H ^= static_cast<uint64_t>(K.Y) * 19349663ull;
        H ^= static_cast<uint64_t>(K.Z) * 83492791ull;
        return static_cast<size_t>(H);
    }
};

VertexKey HashVertex(Mathematics::Axis3D V) noexcept
{
    return {
        static_cast<int64_t>(std::llround(static_cast<double>(V.X) * 1.0e6)),
        static_cast<int64_t>(std::llround(static_cast<double>(V.Y) * 1.0e6)),
        static_cast<int64_t>(std::llround(static_cast<double>(V.Z) * 1.0e6))
    };
}

std::vector<uint32_t> DeduplicateCycle(const std::vector<uint32_t>& Indices)
{
    std::vector<uint32_t> Out;
    Out.reserve(Indices.size());
    for (size_t I = 0u; I < Indices.size(); ++I)
    {
        const uint32_t Curr = Indices[I];
        const uint32_t Prev = Out.empty() ? Indices.back() : Out.back();
        if (Curr != Prev)
        {
            Out.push_back(Curr);
        }
    }
    return Out;
}

Mathematics::Axis3D SampleStressCrackNormal(
    const FractureMaterialSpec& Material,
    Mathematics::Axis3D         Centroid,
    Mathematics::Axis3D         ImpactPoint,
    Mathematics::Axis3D         ImpactNormal,
    double                      SolidRadius,
    double                      GrainAngleRad,
    DeterministicRandomStream&  Rand) noexcept
{
    const Mathematics::Axis3D Rel   = Centroid - ImpactPoint;
    const Mathematics::Axis3D Axial = ImpactNormal * Mathematics::EvaluateDot(Rel, ImpactNormal);
    Mathematics::Axis3D       Rad   = Rel - Axial;
    const double              R     = static_cast<double>(Mathematics::EvaluateLength(Rad));
    Rad = R > 1.0e-5
        ? (Rad * static_cast<float>(1.0 / R))
        : PerpendicularVector(ImpactNormal);
    const Mathematics::Axis3D Hoop = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(ImpactNormal, Rad));

    const float Cg = static_cast<float>(std::cos(GrainAngleRad));
    const float Sg = static_cast<float>(std::sin(GrainAngleRad));
    const Mathematics::Axis3D RotGrain = Mathematics::NormalizeAxis({
        Cg * Material.GrainAxis.X - Sg * Material.GrainAxis.Y,
        Sg * Material.GrainAxis.X + Cg * Material.GrainAxis.Y,
        Material.GrainAxis.Z
    });

    Mathematics::Axis3D N {};
    if (Material.Category == FractureMaterialCategory::PlateBrittle)
    {
        if (Material.StoredEnergyJPerM3 > 0.0f)
        {
            N = Mathematics::NormalizeAxis({
                Rand.NextFloat() * 2.0f - 1.0f,
                Rand.NextFloat() * 2.0f - 1.0f,
                Rand.NextFloat() * 2.0f - 1.0f
            });
            N = Mathematics::NormalizeAxis(N - ImpactNormal * (Mathematics::EvaluateDot(N, ImpactNormal) * 0.86f));
        }
        else
        {
            const double RingProb = std::min(1.0, std::pow(R / std::max(0.15, SolidRadius * 0.48), 1.35))
                                  * (0.32 + static_cast<double>(Material.RingBias));
            N = Rand.NextDouble() < RingProb ? Rad : Hoop;
            N = Mathematics::NormalizeAxis(N - ImpactNormal * (Mathematics::EvaluateDot(N, ImpactNormal) * 0.88f));
        }
    }
    else if (Material.Category == FractureMaterialCategory::Fibrous)
    {
        const Mathematics::Axis3D RandVec {
            Rand.NextFloat() * 2.0f - 1.0f,
            Rand.NextFloat() * 2.0f - 1.0f,
            Rand.NextFloat() * 2.0f - 1.0f
        };
        const Mathematics::Axis3D CrossV = Mathematics::EvaluateCross(RotGrain, RandVec);
        N = Mathematics::EvaluateLengthSquared(CrossV) > 1.0e-4f
            ? Mathematics::NormalizeAxis(CrossV)
            : PerpendicularVector(RotGrain);
        const double NearImpact = std::max(0.0, 1.0 - R / std::max(0.2, SolidRadius * 0.55));
        if (Rand.NextDouble() < 0.12 + 0.24 * NearImpact)
        {
            N = Mathematics::NormalizeAxis(RotGrain + Hoop * 0.28f);
        }
    }
    else if (Material.Category == FractureMaterialCategory::Ductile)
    {
        N = Mathematics::NormalizeAxis(Mathematics::InterpolateLinear(Hoop, Rad, Rand.NextFloat() * 0.65f));
    }
    else
    {
        const double RingProb = std::min(1.0, std::pow(R / std::max(0.2, SolidRadius * 0.62), 1.45))
                              * static_cast<double>(Material.RingBias);
        N = Rand.NextDouble() < RingProb ? Rad : Hoop;
        if (Rand.NextDouble() < 0.30)
        {
            const Mathematics::Axis3D Iso = Mathematics::NormalizeAxis({
                Rand.NextFloat() * 2.0f - 1.0f,
                Rand.NextFloat() * 2.0f - 1.0f,
                Rand.NextFloat() * 2.0f - 1.0f
            });
            N = Mathematics::NormalizeAxis(Mathematics::InterpolateLinear(N, Iso, 0.72f));
        }
        if (Material.BeddingWeakness > 0.0f && Rand.NextDouble() < 0.32 + static_cast<double>(Material.BeddingWeakness) * 0.4)
        {
            N = Mathematics::NormalizeAxis(Mathematics::InterpolateLinear(N, RotGrain, 0.82f));
        }
    }

    const float J = Material.CrackJitter;
    N = Mathematics::NormalizeAxis({
        N.X + (Rand.NextFloat() - 0.5f) * J,
        N.Y + (Rand.NextFloat() - 0.5f) * J,
        N.Z + (Rand.NextFloat() - 0.5f) * J
    });
    return N;
}

float MinSpanComponent(const PolyhedronMetrics& Metrics) noexcept
{
    return std::min({ Metrics.Span.X, Metrics.Span.Y, Metrics.Span.Z });
}

float MaxSpanComponent(const PolyhedronMetrics& Metrics) noexcept
{
    return std::max({ Metrics.Span.X, Metrics.Span.Y, Metrics.Span.Z });
}

struct GriffithCandidate
{
    ConvexPolyhedron  Polyhedron {};
    PolyhedronMetrics Metrics    {};
    double            Energy     = 0.0;
};

struct SolverOutput
{
    std::vector<ConvexPolyhedron>      Pieces         {};
    double                             SpentEnergy    = 0.0;
    std::vector<FractureWavefrontPath> WavefrontPaths {};
};

SolverOutput SolveGriffithStressBudget(
    const ConvexPolyhedron&     SourcePolyhedron,
    const PolyhedronMetrics&    SourceMetrics,
    const FractureMaterialSpec& Material,
    Mathematics::Axis3D         ImpactPoint,
    Mathematics::Axis3D         ImpactNormal,
    double                      TotalEnergyBudget,
    uint32_t                    Ceiling,
    double                      MinSpan,
    double                      GrainAngleRad,
    DeterministicRandomStream&  Rand,
    uint32_t&                   TokenCounter)
{
    std::vector<GriffithCandidate> Queue;
    Queue.push_back({ SourcePolyhedron, SourceMetrics, TotalEnergyBudget });
    double SpentEnergy = 0.0;

    if (Material.HertzConeRad > 0.1f && Queue.size() < Ceiling)
    {
        constexpr uint32_t ConePlanes = 4u;
        const Mathematics::Axis3D PerpU = PerpendicularVector(ImpactNormal);
        const Mathematics::Axis3D PerpV = Mathematics::EvaluateCross(ImpactNormal, PerpU);

        for (uint32_t K = 0u; K < ConePlanes && Queue.size() < Ceiling; ++K)
        {
            const double Azimuth = (2.0 * PiConst * static_cast<double>(K)) / static_cast<double>(ConePlanes)
                                 + Rand.NextDouble() * 0.35;
            const Mathematics::Axis3D Tang = PerpU * static_cast<float>(std::cos(Azimuth))
                                           + PerpV * static_cast<float>(std::sin(Azimuth));
            const Mathematics::Axis3D ConeNormal = Mathematics::NormalizeAxis(
                Tang * std::sin(Material.HertzConeRad) - ImpactNormal * std::cos(Material.HertzConeRad));
            const Mathematics::Axis3D Anchor = ImpactPoint + ImpactNormal * static_cast<float>(0.14 * SourceMetrics.RadiusM);
            const double Offset = static_cast<double>(Mathematics::EvaluateDot(ConeNormal, Anchor))
                                + (Rand.NextDouble() - 0.5) * 0.06 * SourceMetrics.RadiusM;

            std::sort(Queue.begin(), Queue.end(), [&](const GriffithCandidate& A, const GriffithCandidate& B)
            {
                return Mathematics::EvaluateDistance(A.Metrics.Centroid, ImpactPoint)
                     < Mathematics::EvaluateDistance(B.Metrics.Centroid, ImpactPoint);
            });

            const auto& Target = Queue.front();
            const auto  Clipped = ConvexPartitioning::ClipConvexPolyhedron(Target.Polyhedron, ConeNormal, Offset, ++TokenCounter, true);
            if (Clipped.HasFront && Clipped.HasBack)
            {
                const auto Mf = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Front);
                const auto Mb = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Back);
                if (MinSpanComponent(Mf) >= MinSpan * 0.6 && MinSpanComponent(Mb) >= MinSpan * 0.6 &&
                    Mf.VolumeM3 > 1.0e-6 && Mb.VolumeM3 > 1.0e-6)
                {
                    const double Cost  = 2.0 * static_cast<double>(Material.FractureToughnessGc) * Clipped.CutAreaM2;
                    SpentEnergy += Cost;
                    const double Rem   = std::max(0.0, Target.Energy - Cost);
                    const double Ratio = Mf.VolumeM3 / (Mf.VolumeM3 + Mb.VolumeM3);
                    Queue.erase(Queue.begin());
                    Queue.push_back({ Clipped.Front, Mf, Rem * Ratio });
                    Queue.push_back({ Clipped.Back, Mb, Rem * (1.0 - Ratio) });
                }
            }
        }
    }

    uint32_t Guard = 0u;
    while (Queue.size() < Ceiling && Guard++ < Ceiling * 10u)
    {
        int32_t BestIdx      = -1;
        double  BestPriority = -1.0;
        for (size_t I = 0u; I < Queue.size(); ++I)
        {
            const auto& Entry = Queue[I];
            if (MaxSpanComponent(Entry.Metrics) < MinSpan * 2.1) continue;
            const double D = static_cast<double>(Mathematics::EvaluateDistance(Entry.Metrics.Centroid, ImpactPoint));
            const double ProximityWeight = Material.StoredEnergyJPerM3 > 0.0f
                ? 1.0
                : 1.0 / (0.14 + std::pow(D / std::max(0.25, SourceMetrics.RadiusM), 1.35));
            const double Priority = (Entry.Energy + 18.0) * std::pow(Entry.Metrics.VolumeM3, 0.55) * ProximityWeight;
            if (Priority > BestPriority)
            {
                BestPriority = Priority;
                BestIdx      = static_cast<int32_t>(I);
            }
        }
        if (BestIdx < 0) break;

        GriffithCandidate Target = Queue[static_cast<size_t>(BestIdx)];
        bool                 HasChosen = false;
        PolyhedronClipOutput BestClipped {};
        PolyhedronMetrics    BestMf {}, BestMb {};
        double               BestCost  = 0.0;
        double               BestScore = -1.0;

        for (uint32_t Candidate = 0u; Candidate < 7u; ++Candidate)
        {
            const auto   N       = SampleStressCrackNormal(Material, Target.Metrics.Centroid, ImpactPoint, ImpactNormal, SourceMetrics.RadiusM, GrainAngleRad, Rand);
            const double Bias    = (Rand.NextDouble() - 0.5) * 0.28 * Target.Metrics.RadiusM;
            const double D       = static_cast<double>(Mathematics::EvaluateDot(N, Target.Metrics.Centroid)) + Bias;
            const auto   Clipped = ConvexPartitioning::ClipConvexPolyhedron(Target.Polyhedron, N, D, ++TokenCounter, true);
            if (!Clipped.HasFront || !Clipped.HasBack) continue;

            const auto   Mf      = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Front);
            const auto   Mb      = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Back);
            const double MinSplit = std::min(MinSpanComponent(Mf), MinSpanComponent(Mb));
            const double Balance  = std::min(Mf.VolumeM3, Mb.VolumeM3) / std::max(Mf.VolumeM3, Mb.VolumeM3);
            if (MinSplit < MinSpan * 0.45 || Balance < 0.12) continue;

            const double LocalGc = ConvexPartitioning::ComputeDirectionalToughness(Material, N, GrainAngleRad, &Rand);
            const double Cost    = 2.0 * LocalGc * Clipped.CutAreaM2;
            const double Score   = (Balance * 1.4) / (1.0 + Cost / std::max(60.0, TotalEnergyBudget * 0.15));
            if (!HasChosen || Score > BestScore)
            {
                HasChosen   = true;
                BestClipped = Clipped;
                BestMf      = Mf;
                BestMb      = Mb;
                BestCost    = Cost;
                BestScore   = Score;
            }
        }

        if (!HasChosen)
        {
            Queue[static_cast<size_t>(BestIdx)].Energy *= 0.15;
            continue;
        }

        if (SpentEnergy + BestCost > TotalEnergyBudget * 1.35 && Queue.size() >= 4u)
        {
            break;
        }

        SpentEnergy += BestCost;
        const double Remaining = std::max(0.0, Target.Energy - BestCost);
        const double ShareF    = BestMf.VolumeM3 / (BestMf.VolumeM3 + BestMb.VolumeM3);
        Queue.erase(Queue.begin() + BestIdx);
        Queue.push_back({ BestClipped.Front, BestMf, Remaining * ShareF });
        Queue.push_back({ BestClipped.Back,  BestMb, Remaining * (1.0 - ShareF) });
    }

    SolverOutput Out {};
    Out.SpentEnergy = SpentEnergy;
    Out.Pieces.reserve(Queue.size());
    for (auto& Entry : Queue) Out.Pieces.push_back(std::move(Entry.Polyhedron));
    return Out;
}

SolverOutput SolveLefmWavefront(
    const ConvexPolyhedron&     SourcePolyhedron,
    const PolyhedronMetrics&    SourceMetrics,
    const FractureMaterialSpec& Material,
    Mathematics::Axis3D         ImpactPoint,
    Mathematics::Axis3D         ImpactNormal,
    double                      TotalEnergyBudget,
    uint32_t                    Ceiling,
    double                      MinSpan,
    double                      GrainAngleRad,
    DeterministicRandomStream&  Rand,
    uint32_t&                   TokenCounter)
{
    const Mathematics::Axis3D TangentU = PerpendicularVector(ImpactNormal);
    const Mathematics::Axis3D TangentV = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(ImpactNormal, TangentU));
    const uint32_t RadialSpokes = static_cast<uint32_t>(std::clamp(
        static_cast<int>(std::llround(3.0 + std::sqrt(TotalEnergyBudget / std::max(12.0, static_cast<double>(Material.FractureToughnessGc) * 8.0)))),
        3, 12));

    std::vector<ConvexPolyhedron>      Pieces { SourcePolyhedron };
    std::vector<FractureWavefrontPath> Paths;
    double                             SpentEnergy = 0.0;

    for (uint32_t S = 0u; S < RadialSpokes && Pieces.size() < Ceiling; ++S)
    {
        const double Theta = (2.0 * PiConst * static_cast<double>(S)) / static_cast<double>(RadialSpokes)
                           + (Rand.NextDouble() - 0.5) * (0.38 / static_cast<double>(RadialSpokes)) * PiConst;
        const Mathematics::Axis3D RayDir    = TangentU * static_cast<float>(std::cos(Theta)) + TangentV * static_cast<float>(std::sin(Theta));
        const Mathematics::Axis3D PlaneNorm = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(ImpactNormal, RayDir));
        const double              Offset    = static_cast<double>(Mathematics::EvaluateDot(PlaneNorm, ImpactPoint));

        Paths.push_back({
            { ImpactPoint, ImpactPoint + RayDir * static_cast<float>(SourceMetrics.RadiusM * 1.4) },
            false
        });

        std::vector<ConvexPolyhedron> NextPieces;
        for (const auto& Piece : Pieces)
        {
            if (Pieces.size() + NextPieces.size() >= Ceiling) { NextPieces.push_back(Piece); continue; }
            const auto Pm = ConvexPartitioning::ComputePolyhedronMetrics(Piece);
            if (Mathematics::EvaluateDot(Pm.Centroid - ImpactPoint, RayDir) < -0.15f * static_cast<float>(SourceMetrics.RadiusM) &&
                NextPieces.size() + Pieces.size() > 2u)
            {
                NextPieces.push_back(Piece);
                continue;
            }
            const auto Clipped = ConvexPartitioning::ClipConvexPolyhedron(Piece, PlaneNorm, Offset, ++TokenCounter, true);
            if (Clipped.HasFront && Clipped.HasBack)
            {
                const auto Mf = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Front);
                const auto Mb = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Back);
                if (Mf.VolumeM3 > 1.0e-5 && Mb.VolumeM3 > 1.0e-5)
                {
                    SpentEnergy += 2.0 * static_cast<double>(Material.FractureToughnessGc) * Clipped.CutAreaM2;
                    NextPieces.push_back(Clipped.Front);
                    NextPieces.push_back(Clipped.Back);
                    continue;
                }
            }
            NextPieces.push_back(Piece);
        }
        Pieces = std::move(NextPieces);
    }

    constexpr double Rings[] = { 0.24, 0.52, 0.84, 1.18 };
    for (double RingFrac : Rings)
    {
        if (Pieces.size() >= Ceiling) break;
        const double RingRadius = RingFrac * SourceMetrics.RadiusM;
        std::vector<ConvexPolyhedron> NextPieces;
        for (const auto& Piece : Pieces)
        {
            if (Pieces.size() + NextPieces.size() >= Ceiling) { NextPieces.push_back(Piece); continue; }
            const auto                Pm      = ConvexPartitioning::ComputePolyhedronMetrics(Piece);
            const Mathematics::Axis3D Rel     = Pm.Centroid - ImpactPoint;
            const Mathematics::Axis3D InPlane = Rel - ImpactNormal * Mathematics::EvaluateDot(Rel, ImpactNormal);
            const double              R       = static_cast<double>(Mathematics::EvaluateLength(InPlane));
            if (std::abs(R - RingRadius) < Pm.RadiusM * 0.95 && MaxSpanComponent(Pm) > MinSpan * 2.0)
            {
                const Mathematics::Axis3D RadialDir = R > 1.0e-5 ? (InPlane * static_cast<float>(1.0 / R)) : TangentU;
                const Mathematics::Axis3D RingNorm  = Mathematics::NormalizeAxis(RadialDir + ImpactNormal * ((Rand.NextFloat() - 0.5f) * 0.22f));
                const Mathematics::Axis3D Anchor    = ImpactPoint + RingNorm * static_cast<float>(RingRadius * (0.9 + Rand.NextDouble() * 0.2));
                const auto Clipped = ConvexPartitioning::ClipConvexPolyhedron(Piece, RingNorm, static_cast<double>(Mathematics::EvaluateDot(RingNorm, Anchor)), ++TokenCounter, true);
                if (Clipped.HasFront && Clipped.HasBack)
                {
                    const auto Mf = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Front);
                    const auto Mb = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Back);
                    if (Mf.VolumeM3 > 1.0e-5 && Mb.VolumeM3 > 1.0e-5)
                    {
                        SpentEnergy += 2.0 * ConvexPartitioning::ComputeDirectionalToughness(Material, RingNorm, GrainAngleRad, &Rand) * Clipped.CutAreaM2;
                        NextPieces.push_back(Clipped.Front);
                        NextPieces.push_back(Clipped.Back);
                        continue;
                    }
                }
            }
            NextPieces.push_back(Piece);
        }
        Pieces = std::move(NextPieces);
    }

    return { std::move(Pieces), SpentEnergy, std::move(Paths) };
}

std::vector<ConvexPolyhedron> SolveClusteredVoronoi(
    const ConvexPolyhedron&    SourcePolyhedron,
    const PolyhedronMetrics&   SourceMetrics,
    Mathematics::Axis3D        ImpactPoint,
    uint32_t                   Ceiling,
    double                     MinSpan,
    DeterministicRandomStream& Rand,
    uint32_t&                  TokenCounter)
{
    std::vector<Mathematics::Axis3D> Seeds;
    const uint32_t MaxTries = Ceiling * 18u;
    for (uint32_t T = 0u; T < MaxTries && Seeds.size() < Ceiling; ++T)
    {
        Mathematics::Axis3D Candidate {};
        if (Rand.NextDouble() < 0.62)
        {
            const double U     = std::pow(Rand.NextDouble(), 1.65);
            const double Theta = Rand.NextDouble() * PiConst * 2.0;
            const double Phi   = std::acos(2.0 * Rand.NextDouble() - 1.0);
            const double R     = U * SourceMetrics.RadiusM * 1.15;
            Candidate = {
                std::clamp(ImpactPoint.X + static_cast<float>(R * std::sin(Phi) * std::cos(Theta)), SourceMetrics.BoundsMin.X + 0.02f, SourceMetrics.BoundsMax.X - 0.02f),
                std::clamp(ImpactPoint.Y + static_cast<float>(R * std::sin(Phi) * std::sin(Theta)), SourceMetrics.BoundsMin.Y + 0.02f, SourceMetrics.BoundsMax.Y - 0.02f),
                std::clamp(ImpactPoint.Z + static_cast<float>(R * std::cos(Phi)),                   SourceMetrics.BoundsMin.Z + 0.02f, SourceMetrics.BoundsMax.Z - 0.02f)
            };
        }
        else
        {
            Candidate = {
                SourceMetrics.BoundsMin.X + (0.06f + 0.88f * Rand.NextFloat()) * SourceMetrics.Span.X,
                SourceMetrics.BoundsMin.Y + (0.06f + 0.88f * Rand.NextFloat()) * SourceMetrics.Span.Y,
                SourceMetrics.BoundsMin.Z + (0.06f + 0.88f * Rand.NextFloat()) * SourceMetrics.Span.Z
            };
        }

        bool TooClose = false;
        for (const auto& S : Seeds)
        {
            if (Mathematics::EvaluateDistance(S, Candidate) < MinSpan * 0.75) { TooClose = true; break; }
        }
        if (!TooClose) Seeds.push_back(Candidate);
    }

    if (Seeds.size() < 2u)
    {
        Seeds.push_back(SourceMetrics.Centroid);
        Seeds.push_back(SourceMetrics.Centroid + Mathematics::Axis3D { 0.1f, 0.05f, 0.0f });
    }

    std::unordered_map<uint64_t, uint32_t> PairTokens;
    auto GetPairToken = [&](uint32_t I, uint32_t J) -> uint32_t
    {
        const uint32_t Lo  = std::min(I, J);
        const uint32_t Hi  = std::max(I, J);
        const uint64_t Key = (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
        auto [It, Inserted] = PairTokens.try_emplace(Key, 0u);
        if (Inserted) It->second = ++TokenCounter;
        return It->second;
    };

    std::vector<ConvexPolyhedron> Cells;
    for (uint32_t I = 0u; I < Seeds.size(); ++I)
    {
        ConvexPolyhedron Cell = SourcePolyhedron;
        bool             Alive = true;
        const auto       Si    = Seeds[I];

        std::vector<uint32_t> NeighborOrder(Seeds.size());
        std::iota(NeighborOrder.begin(), NeighborOrder.end(), 0u);
        std::sort(NeighborOrder.begin(), NeighborOrder.end(), [&](uint32_t A, uint32_t B)
        {
            return Mathematics::EvaluateDistance(Si, Seeds[A]) < Mathematics::EvaluateDistance(Si, Seeds[B]);
        });

        for (uint32_t J : NeighborOrder)
        {
            if (J == I) continue;
            const auto   Sj        = Seeds[J];
            const auto   Mid       = (Si + Sj) * 0.5f;
            const auto   NormalOut = Mathematics::NormalizeAxis(Sj - Si);
            const double Offset    = static_cast<double>(Mathematics::EvaluateDot(NormalOut, Mid));
            const auto   Clipped   = ConvexPartitioning::ClipConvexPolyhedron(Cell, NormalOut, Offset, GetPairToken(I, J), true);
            if (!Clipped.HasBack)
            {
                Alive = false;
                break;
            }
            Cell = Clipped.Back;
        }

        if (Alive && ConvexPartitioning::ComputePolyhedronMetrics(Cell).VolumeM3 > 1.0e-6)
        {
            Cells.push_back(std::move(Cell));
        }
    }

    if (Cells.empty()) Cells.push_back(SourcePolyhedron);
    return Cells;
}

SolverOutput SolveDendriticSpallHull(
    const ConvexPolyhedron&     SourcePolyhedron,
    const PolyhedronMetrics&    SourceMetrics,
    const FractureMaterialSpec& Material,
    Mathematics::Axis3D         ImpactPoint,
    Mathematics::Axis3D         ImpactNormal,
    uint32_t                    Ceiling,
    double                      MinSpan,
    double                      GrainAngleRad,
    DeterministicRandomStream&  Rand,
    uint32_t&                   TokenCounter)
{
    std::vector<ConvexPolyhedron> Pieces { SourcePolyhedron };
    double                        SpentEnergy = 0.0;
    std::vector<FractureWavefrontPath> Polylines;

    const Mathematics::Axis3D SpallDirs[] = {
        Mathematics::NormalizeAxis(ImpactNormal * -1.0f),
        Mathematics::NormalizeAxis(ImpactNormal * -0.6f + PerpendicularVector(ImpactNormal)),
        Mathematics::NormalizeAxis(ImpactNormal * -0.6f - PerpendicularVector(ImpactNormal)),
        { 0.0f, 0.0f, 1.0f }
    };

    for (const auto& Dir : SpallDirs)
    {
        if (Pieces.size() >= std::min(Ceiling, 7u)) break;
        const double ShellDepth = SourceMetrics.RadiusM * (0.48 + Rand.NextDouble() * 0.18);
        const double Offset     = static_cast<double>(Mathematics::EvaluateDot(
            Dir, SourceMetrics.Centroid + Dir * static_cast<float>(ShellDepth)));
        std::vector<ConvexPolyhedron> Next;
        for (const auto& P : Pieces)
        {
            const auto Clipped = ConvexPartitioning::ClipConvexPolyhedron(P, Dir, Offset, ++TokenCounter, true);
            if (Clipped.HasFront && Clipped.HasBack)
            {
                SpentEnergy += 2.0 * static_cast<double>(Material.FractureToughnessGc) * Clipped.CutAreaM2;
                Next.push_back(Clipped.Front);
                Next.push_back(Clipped.Back);
            }
            else
            {
                Next.push_back(P);
            }
        }
        Pieces = std::move(Next);
    }

    const uint32_t BranchCount = static_cast<uint32_t>(std::clamp(static_cast<int>(std::llround(static_cast<double>(Ceiling) / 7.0)), 3, 8));
    const auto     ExitTarget  = SourceMetrics.Centroid + ImpactNormal * static_cast<float>(SourceMetrics.RadiusM * 0.85);
    std::vector<Mathematics::Axis3D> Trunk { ImpactPoint };
    constexpr uint32_t Steps = 5u;
    for (uint32_t S = 1u; S <= Steps; ++S)
    {
        const float T = static_cast<float>(S) / static_cast<float>(Steps);
        const Mathematics::Axis3D Jitter = S < Steps ? Mathematics::Axis3D {
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 0.45f,
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 0.45f,
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 0.45f
        } : Mathematics::Axis3D { 0.0f, 0.0f, 0.0f };
        Trunk.push_back(Mathematics::InterpolateLinear(ImpactPoint, ExitTarget, T) + Jitter);
    }
    Polylines.push_back({ Trunk, true });

    for (uint32_t B = 0u; B < BranchCount; ++B)
    {
        const auto& Anchor    = Trunk[std::min<size_t>(Trunk.size() - 1u, 1u + (B % (Trunk.size() - 1u)))];
        const auto  BranchEnd = Anchor + Mathematics::Axis3D {
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 1.3f,
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 1.3f,
            (Rand.NextFloat() - 0.5f) * static_cast<float>(SourceMetrics.RadiusM) * 1.3f
        };
        Polylines.push_back({ { Anchor, Mathematics::InterpolateLinear(Anchor, BranchEnd, 0.52f), BranchEnd }, false });
    }

    uint32_t Guard = 0u;
    while (Pieces.size() < Ceiling && Guard++ < Ceiling * 6u)
    {
        std::sort(Pieces.begin(), Pieces.end(), [](const ConvexPolyhedron& A, const ConvexPolyhedron& B)
        {
            return ConvexPartitioning::ComputePolyhedronMetrics(A).VolumeM3
                 > ConvexPartitioning::ComputePolyhedronMetrics(B).VolumeM3;
        });
        const auto& Target = Pieces.front();
        const auto  Tm     = ConvexPartitioning::ComputePolyhedronMetrics(Target);
        if (MaxSpanComponent(Tm) < MinSpan * 1.8) break;

        const auto& Polyline  = Polylines[Guard % Polylines.size()];
        const auto  SegDir    = Mathematics::NormalizeAxis(Polyline.Points.back() - Polyline.Points.front());
        const auto  RandSide  = Mathematics::NormalizeAxis({
            Rand.NextFloat() * 2.0f - 1.0f,
            Rand.NextFloat() * 2.0f - 1.0f,
            Rand.NextFloat() * 2.0f - 1.0f
        });
        const auto  CutNormal = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(SegDir, RandSide));
        const auto  Anchor    = Mathematics::InterpolateLinear(Tm.Centroid, Polyline.Points.front(), 0.35f);
        const auto  Clipped   = ConvexPartitioning::ClipConvexPolyhedron(
            Target, CutNormal, static_cast<double>(Mathematics::EvaluateDot(CutNormal, Anchor)), ++TokenCounter, true);
        if (Clipped.HasFront && Clipped.HasBack)
        {
            const auto Mf = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Front);
            const auto Mb = ConvexPartitioning::ComputePolyhedronMetrics(Clipped.Back);
            if (Mf.VolumeM3 > 1.0e-5 && Mb.VolumeM3 > 1.0e-5)
            {
                SpentEnergy += 2.0 * ConvexPartitioning::ComputeDirectionalToughness(Material, CutNormal, GrainAngleRad, &Rand) * Clipped.CutAreaM2;
                Pieces.erase(Pieces.begin());
                Pieces.push_back(Clipped.Front);
                Pieces.push_back(Clipped.Back);
            }
        }
    }

    return { std::move(Pieces), SpentEnergy, std::move(Polylines) };
}

} // namespace

Mathematics::Axis3D ConvexPartitioning::ComputeFaceNormal(
    const ConvexPolyhedron& Polyhedron,
    const ConvexFace&       Face) noexcept
{
    double Nx = 0.0, Ny = 0.0, Nz = 0.0;
    const size_t Count = Face.Indices.size();
    for (size_t I = 0u; I < Count; ++I)
    {
        const auto& A = Polyhedron.Vertices[Face.Indices[I]];
        const auto& B = Polyhedron.Vertices[Face.Indices[(I + 1u) % Count]];
        Nx += static_cast<double>(A.Y - B.Y) * static_cast<double>(A.Z + B.Z);
        Ny += static_cast<double>(A.Z - B.Z) * static_cast<double>(A.X + B.X);
        Nz += static_cast<double>(A.X - B.X) * static_cast<double>(A.Y + B.Y);
    }
    const double Len = std::hypot(Nx, Ny, Nz);
    if (Len <= 1.0e-12) return { 0.0f, 0.0f, 1.0f };
    return { static_cast<float>(Nx / Len), static_cast<float>(Ny / Len), static_cast<float>(Nz / Len) };
}

double ConvexPartitioning::ComputeFaceAreaM2(
    const ConvexPolyhedron& Polyhedron,
    const ConvexFace&       Face) noexcept
{
    if (Face.Indices.size() < 3u) return 0.0;
    const auto& O    = Polyhedron.Vertices[Face.Indices[0]];
    double      Area = 0.0;
    for (size_t I = 1u; I + 1u < Face.Indices.size(); ++I)
    {
        const auto Ab = Polyhedron.Vertices[Face.Indices[I]] - O;
        const auto Ac = Polyhedron.Vertices[Face.Indices[I + 1u]] - O;
        Area += 0.5 * static_cast<double>(Mathematics::EvaluateLength(Mathematics::EvaluateCross(Ab, Ac)));
    }
    return Area;
}

PolyhedronMetrics ConvexPartitioning::ComputePolyhedronMetrics(const ConvexPolyhedron& Polyhedron) noexcept
{
    PolyhedronMetrics Out {};
    if (Polyhedron.Vertices.empty()) return Out;

    const auto& Origin   = Polyhedron.Vertices[0];
    double      Vol6     = 0.0;
    double      Cx = 0.0, Cy = 0.0, Cz = 0.0;
    double      CrackArea = 0.0;

    for (const auto& Face : Polyhedron.Faces)
    {
        if (Face.Interior && !Face.Seam) CrackArea += ComputeFaceAreaM2(Polyhedron, Face);
        for (size_t I = 1u; I + 1u < Face.Indices.size(); ++I)
        {
            const auto& P1 = Polyhedron.Vertices[Face.Indices[0]];
            const auto& P2 = Polyhedron.Vertices[Face.Indices[I]];
            const auto& P3 = Polyhedron.Vertices[Face.Indices[I + 1u]];
            const double Tet6 = static_cast<double>(Mathematics::EvaluateDot(
                P1 - Origin, Mathematics::EvaluateCross(P2 - Origin, P3 - Origin)));
            Vol6 += Tet6;
            Cx   += static_cast<double>(Origin.X + P1.X + P2.X + P3.X) * 0.25 * Tet6;
            Cy   += static_cast<double>(Origin.Y + P1.Y + P2.Y + P3.Y) * 0.25 * Tet6;
            Cz   += static_cast<double>(Origin.Z + P1.Z + P2.Z + P3.Z) * 0.25 * Tet6;
        }
    }

    Out.VolumeM3    = std::abs(Vol6) / 6.0;
    Out.CrackAreaM2 = CrackArea;
    Out.Centroid    = std::abs(Vol6) > 1.0e-12
        ? Mathematics::Axis3D { static_cast<float>(Cx / Vol6), static_cast<float>(Cy / Vol6), static_cast<float>(Cz / Vol6) }
        : Origin;

    Mathematics::Axis3D BMin { std::numeric_limits<float>::infinity(),  std::numeric_limits<float>::infinity(),  std::numeric_limits<float>::infinity() };
    Mathematics::Axis3D BMax { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    double MaxRadius = 0.0;
    for (const auto& P : Polyhedron.Vertices)
    {
        BMin.X = std::min(BMin.X, P.X); BMin.Y = std::min(BMin.Y, P.Y); BMin.Z = std::min(BMin.Z, P.Z);
        BMax.X = std::max(BMax.X, P.X); BMax.Y = std::max(BMax.Y, P.Y); BMax.Z = std::max(BMax.Z, P.Z);
        MaxRadius = std::max(MaxRadius, static_cast<double>(Mathematics::EvaluateDistance(P, Out.Centroid)));
    }
    Out.BoundsMin = BMin;
    Out.BoundsMax = BMax;
    Out.Span      = BMax - BMin;
    Out.RadiusM   = MaxRadius;
    return Out;
}

void ConvexPartitioning::OrientFacesOutward(ConvexPolyhedron& Polyhedron) noexcept
{
    if (Polyhedron.Vertices.empty()) return;
    Mathematics::Axis3D Sum { 0.0f, 0.0f, 0.0f };
    for (const auto& V : Polyhedron.Vertices) Sum = Sum + V;
    const auto Centre = Sum * (1.0f / static_cast<float>(Polyhedron.Vertices.size()));

    for (auto& Face : Polyhedron.Faces)
    {
        if (Face.Indices.empty()) continue;
        Mathematics::Axis3D FSum { 0.0f, 0.0f, 0.0f };
        for (uint32_t Idx : Face.Indices) FSum = FSum + Polyhedron.Vertices[Idx];
        const auto Fc = FSum * (1.0f / static_cast<float>(Face.Indices.size()));
        const auto N  = ComputeFaceNormal(Polyhedron, Face);
        if (Mathematics::EvaluateDot(N, Fc - Centre) < 0.0f)
        {
            std::reverse(Face.Indices.begin(), Face.Indices.end());
        }
    }
}

ConvexPolyhedron ConvexPartitioning::ConstructBoxPolyhedron(float ExtentX, float ExtentY, float ExtentZ)
{
    const float Hx = 0.5f * ExtentX;
    const float Hy = 0.5f * ExtentY;
    const float Hz = 0.5f * ExtentZ;

    ConvexPolyhedron Polyhedron {};
    Polyhedron.Vertices = {
        { -Hx, -Hy, -Hz }, {  Hx, -Hy, -Hz }, {  Hx,  Hy, -Hz }, { -Hx,  Hy, -Hz },
        { -Hx, -Hy,  Hz }, {  Hx, -Hy,  Hz }, {  Hx,  Hy,  Hz }, { -Hx,  Hy,  Hz }
    };
    const uint32_t Loops[6][4] = {
        { 4u, 5u, 6u, 7u }, { 3u, 2u, 1u, 0u },
        { 1u, 2u, 6u, 5u }, { 3u, 0u, 4u, 7u },
        { 2u, 3u, 7u, 6u }, { 0u, 1u, 5u, 4u }
    };
    for (const auto& Loop : Loops)
    {
        Polyhedron.Faces.push_back({ { Loop[0], Loop[1], Loop[2], Loop[3] }, false, 0u, { 0.0f, 0.0f, 1.0f } });
    }
    OrientFacesOutward(Polyhedron);
    return Polyhedron;
}

ConvexPolyhedron ConvexPartitioning::ConstructPrismPolyhedron(float RadiusBottom, float RadiusTop, float HeightZ, uint32_t Sides)
{
    const float Hz = 0.5f * HeightZ;
    ConvexPolyhedron Polyhedron {};

    if (RadiusTop <= 1.0e-5f)
    {
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const double Theta = (2.0 * PiConst * static_cast<double>(S)) / static_cast<double>(Sides);
            Polyhedron.Vertices.push_back({
                static_cast<float>(RadiusBottom * std::cos(Theta)),
                static_cast<float>(RadiusBottom * std::sin(Theta)),
                -Hz
            });
        }
        const uint32_t ApexSlot = static_cast<uint32_t>(Polyhedron.Vertices.size());
        Polyhedron.Vertices.push_back({ 0.0f, 0.0f, Hz });

        std::vector<uint32_t> Bottom(Sides);
        for (uint32_t S = 0u; S < Sides; ++S) Bottom[S] = Sides - 1u - S;
        Polyhedron.Faces.push_back({ std::move(Bottom), false, 0u, { 0.0f, 0.0f, -1.0f } });

        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const uint32_t Next = (S + 1u) % Sides;
            Polyhedron.Faces.push_back({ { S, Next, ApexSlot }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }
    else
    {
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const double Theta = (2.0 * PiConst * static_cast<double>(S)) / static_cast<double>(Sides);
            Polyhedron.Vertices.push_back({
                static_cast<float>(RadiusBottom * std::cos(Theta)),
                static_cast<float>(RadiusBottom * std::sin(Theta)),
                -Hz
            });
        }
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const double Theta = (2.0 * PiConst * static_cast<double>(S)) / static_cast<double>(Sides);
            Polyhedron.Vertices.push_back({
                static_cast<float>(RadiusTop * std::cos(Theta)),
                static_cast<float>(RadiusTop * std::sin(Theta)),
                Hz
            });
        }
        std::vector<uint32_t> Bottom(Sides), Top(Sides);
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            Bottom[S] = Sides - 1u - S;
            Top[S]    = Sides + S;
        }
        Polyhedron.Faces.push_back({ std::move(Bottom), false, 0u, { 0.0f, 0.0f, -1.0f } });
        Polyhedron.Faces.push_back({ std::move(Top),    false, 0u, { 0.0f, 0.0f,  1.0f } });
        for (uint32_t S = 0u; S < Sides; ++S)
        {
            const uint32_t Next = (S + 1u) % Sides;
            Polyhedron.Faces.push_back({ { S, Next, Sides + Next, Sides + S }, false, 0u, { 0.0f, 0.0f, 1.0f } });
        }
    }

    OrientFacesOutward(Polyhedron);
    return Polyhedron;
}

ConvexPolyhedron ConvexPartitioning::ConstructSolidPolyhedron(
    FractureSolidCategory Category,
    Mathematics::Scalar3D Scale,
    uint32_t              Seed)
{
    ConvexPolyhedron Polyhedron {};
    switch (Category)
    {
        case FractureSolidCategory::Monolith:
            Polyhedron = ConstructBoxPolyhedron(0.96f, 0.96f, 1.92f);
            break;
        case FractureSolidCategory::CurtainPane:
            Polyhedron = ConstructBoxPolyhedron(1.90f, 0.065f, 1.38f);
            break;
        case FractureSolidCategory::TimberBeam:
            Polyhedron = ConstructBoxPolyhedron(2.15f, 0.28f, 0.36f);
            break;
        case FractureSolidCategory::Cylinder:
            Polyhedron = ConstructPrismPolyhedron(0.62f, 0.62f, 1.75f, 22u);
            break;
        case FractureSolidCategory::Cone:
            Polyhedron = ConstructPrismPolyhedron(0.76f, 0.0f, 1.72f, 22u);
            break;
        case FractureSolidCategory::Boulder:
        case FractureSolidCategory::HollowCavity:
        {
            Polyhedron = ConstructBoxPolyhedron(1.70f, 1.56f, 1.48f);
            DeterministicRandomStream Rand(Seed + 109u);
            const uint32_t Cuts = Category == FractureSolidCategory::HollowCavity ? 18u : 14u;
            for (uint32_t I = 0u; I < Cuts; ++I)
            {
                const double Theta = (2.0 * PiConst * static_cast<double>(I)) / static_cast<double>(Cuts) + (Rand.NextDouble() - 0.5) * 0.35;
                const double Phi   = std::acos(1.0 - 2.0 * ((static_cast<double>(I) + 0.5) / static_cast<double>(Cuts))) + (Rand.NextDouble() - 0.5) * 0.25;
                const auto   N     = Mathematics::NormalizeAxis({
                    static_cast<float>(std::sin(Phi) * std::cos(Theta)),
                    static_cast<float>(std::sin(Phi) * std::sin(Theta)),
                    static_cast<float>(std::cos(Phi) * 1.08)
                });
                const double D = 0.64 + Rand.NextDouble() * 0.16;
                const auto Clipped = ClipConvexPolyhedron(Polyhedron, N, D, 0u, false);
                if (Clipped.HasBack) Polyhedron = Clipped.Back;
            }
            break;
        }
        case FractureSolidCategory::Cube:
        default:
            Polyhedron = ConstructBoxPolyhedron(1.32f, 1.32f, 1.32f);
            break;
    }

    for (auto& V : Polyhedron.Vertices)
    {
        V = Mathematics::HadamardMultiply(V, Scale);
    }
    OrientFacesOutward(Polyhedron);
    return Polyhedron;
}

ConvexPolyhedron ConvexPartitioning::ConvertPolygonNetworkToPolyhedron(const PolygonNetwork& Network)
{
    ConvexPolyhedron Polyhedron {};
    Polyhedron.Vertices.reserve(Network.Vertices.size());
    for (const auto& V : Network.Vertices)
    {
        Polyhedron.Vertices.push_back(V.Axes);
    }
    Polyhedron.Faces.reserve(Network.Faces.size());
    for (const auto& Face : Network.Faces)
    {
        if (Face.BoundaryLoopEdgeIndex == InvalidNetworkIndex) continue;
        ConvexFace OutFace {};
        OutFace.Interior    = Face.PriorityLayerOrdinal > 0u;
        OutFace.CutToken    = Face.PriorityLayerOrdinal;
        OutFace.CrackNormal = Face.GeometricNormal;

        uint32_t EdgeIdx = Face.BoundaryLoopEdgeIndex;
        uint32_t Guard   = 0u;
        do
        {
            const auto& Edge = Network.Edges[EdgeIdx];
            OutFace.Indices.push_back(Edge.OriginVertexIndex);
            EdgeIdx = Edge.NextEdgeIndex;
        }
        while (EdgeIdx != Face.BoundaryLoopEdgeIndex && EdgeIdx != InvalidNetworkIndex && ++Guard < 256u);

        if (OutFace.Indices.size() >= 3u)
        {
            Polyhedron.Faces.push_back(std::move(OutFace));
        }
    }
    OrientFacesOutward(Polyhedron);
    return Polyhedron;
}

PolygonNetwork ConvexPartitioning::ConvertPolyhedronToPolygonNetwork(
    const ConvexPolyhedron&      Polyhedron,
    const FaceShadingAssignment& ExteriorShading,
    const FaceShadingAssignment& InteriorShading)
{
    PolygonNetwork Network {};
    Network.ReserveStorage(
        static_cast<uint32_t>(Polyhedron.Vertices.size()),
        static_cast<uint32_t>(Polyhedron.Faces.size() * 4u),
        static_cast<uint32_t>(Polyhedron.Faces.size()));

    for (const auto& V : Polyhedron.Vertices)
    {
        (void)Network.AppendVertex(V);
    }
    for (const auto& Face : Polyhedron.Faces)
    {
        if (Face.Indices.size() < 3u) continue;
        const auto& Shading = Face.Interior ? InteriorShading : ExteriorShading;
        (void)Network.AppendFace(
            Face.Indices, {}, Shading,
            Face.Interior ? static_cast<uint16_t>(Face.CutToken) : 0u, 0u);
    }
    Network.LinkOppositeEdges(false);
    Network.RecomputeNormals();
    return Network;
}

PolyhedronClipOutput ConvexPartitioning::ClipConvexPolyhedron(
    const ConvexPolyhedron& Polyhedron,
    Mathematics::Axis3D     PlaneNormal,
    double                  PlaneOffset,
    uint32_t                CutToken,
    bool                    MarkInterior)
{
    constexpr double Eps = 1.0e-6;
    const auto N = Mathematics::NormalizeAxis(PlaneNormal);
    const auto& V = Polyhedron.Vertices;

    std::vector<double> Dist(V.size(), 0.0);
    uint32_t PosCount = 0u, NegCount = 0u;
    for (size_t I = 0u; I < V.size(); ++I)
    {
        const double S = static_cast<double>(Mathematics::EvaluateDot(N, V[I])) - PlaneOffset;
        Dist[I] = S;
        if (S > Eps) ++PosCount;
        else if (S < -Eps) ++NegCount;
    }

    if (NegCount == 0u) return { Polyhedron, {}, true, false, 0.0 };
    if (PosCount == 0u) return { {}, Polyhedron, false, true, 0.0 };

    std::vector<Mathematics::Axis3D>             FrontVerts, BackVerts;
    std::unordered_map<VertexKey, uint32_t, VertexKeyHasher> FrontLookup, BackLookup;

    auto InsertVertex = [](std::vector<Mathematics::Axis3D>& Arr,
                           std::unordered_map<VertexKey, uint32_t, VertexKeyHasher>& Lookup,
                           Mathematics::Axis3D Pt) -> uint32_t
    {
        const VertexKey Key = HashVertex(Pt);
        if (auto It = Lookup.find(Key); It != Lookup.end()) return It->second;
        const uint32_t Slot = static_cast<uint32_t>(Arr.size());
        Arr.push_back(Pt);
        Lookup.emplace(Key, Slot);
        return Slot;
    };

    std::vector<ConvexFace>                    FrontFaces, BackFaces;
    std::vector<Mathematics::Axis3D>           CutPoints;
    std::unordered_set<VertexKey, VertexKeyHasher> CutSeen;

    for (const auto& Face : Polyhedron.Faces)
    {
        std::vector<uint32_t> Fp, Bp;
        const size_t Count = Face.Indices.size();
        for (size_t I = 0u; I < Count; ++I)
        {
            const uint32_t Ia = Face.Indices[I];
            const uint32_t Ib = Face.Indices[(I + 1u) % Count];
            const double   Da = Dist[Ia], Db = Dist[Ib];
            const auto     A  = V[Ia],    B  = V[Ib];

            if (Da >= -Eps) Fp.push_back(InsertVertex(FrontVerts, FrontLookup, A));
            if (Da <=  Eps) Bp.push_back(InsertVertex(BackVerts, BackLookup, A));
            if (std::abs(Da) <= Eps)
            {
                const VertexKey Key = HashVertex(A);
                if (CutSeen.insert(Key).second) CutPoints.push_back(A);
            }

            if ((Da > Eps && Db < -Eps) || (Da < -Eps && Db > Eps))
            {
                const float T = static_cast<float>(Da / (Da - Db));
                const auto  X = Mathematics::InterpolateLinear(A, B, T);
                Fp.push_back(InsertVertex(FrontVerts, FrontLookup, X));
                Bp.push_back(InsertVertex(BackVerts, BackLookup, X));
                const VertexKey Key = HashVertex(X);
                if (CutSeen.insert(Key).second) CutPoints.push_back(X);
            }
        }

        const auto CleanFp = DeduplicateCycle(Fp);
        const auto CleanBp = DeduplicateCycle(Bp);
        if (CleanFp.size() >= 3u) FrontFaces.push_back({ CleanFp, Face.Interior, Face.CutToken, Face.CrackNormal, Face.Seam, Face.SeamId });
        if (CleanBp.size() >= 3u) BackFaces.push_back({ CleanBp, Face.Interior, Face.CutToken, Face.CrackNormal, Face.Seam, Face.SeamId });
    }

    double CutArea = 0.0;
    if (CutPoints.size() >= 3u)
    {
        Mathematics::Axis3D Sum { 0.0f, 0.0f, 0.0f };
        for (const auto& P : CutPoints) Sum = Sum + P;
        const auto Centre = Sum * (1.0f / static_cast<float>(CutPoints.size()));
        const auto AxisX  = Mathematics::NormalizeAxis(CutPoints[0] - Centre);
        const auto AxisY  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(N, AxisX));

        struct CapPointAngle
        {
            Mathematics::Axis3D Pt  {};
            double              Ang = 0.0;
        };
        std::vector<CapPointAngle> Ordered;
        Ordered.reserve(CutPoints.size());
        for (const auto& P : CutPoints)
        {
            const auto Rel = P - Centre;
            Ordered.push_back({
                P,
                std::atan2(
                    static_cast<double>(Mathematics::EvaluateDot(Rel, AxisY)),
                    static_cast<double>(Mathematics::EvaluateDot(Rel, AxisX)))
            });
        }
        std::sort(Ordered.begin(), Ordered.end(), [](const CapPointAngle& A, const CapPointAngle& B)
        {
            return A.Ang < B.Ang;
        });

        for (size_t I = 1u; I + 1u < Ordered.size(); ++I)
        {
            CutArea += 0.5 * static_cast<double>(Mathematics::EvaluateLength(
                Mathematics::EvaluateCross(Ordered[I].Pt - Ordered[0].Pt, Ordered[I + 1u].Pt - Ordered[0].Pt)));
        }

        std::vector<uint32_t> FrontCapRaw, BackCapRaw;
        FrontCapRaw.reserve(Ordered.size());
        BackCapRaw.reserve(Ordered.size());
        for (size_t I = 0u; I < Ordered.size(); ++I)
        {
            BackCapRaw.push_back(InsertVertex(BackVerts, BackLookup, Ordered[I].Pt));
        }
        for (int64_t I = static_cast<int64_t>(Ordered.size()) - 1; I >= 0; --I)
        {
            FrontCapRaw.push_back(InsertVertex(FrontVerts, FrontLookup, Ordered[static_cast<size_t>(I)].Pt));
        }

        const auto FrontCap = DeduplicateCycle(FrontCapRaw);
        const auto BackCap  = DeduplicateCycle(BackCapRaw);
        if (FrontCap.size() >= 3u) FrontFaces.push_back({ FrontCap, MarkInterior, CutToken, N * -1.0f });
        if (BackCap.size()  >= 3u) BackFaces.push_back({ BackCap,  MarkInterior, CutToken, N });
    }

    if (FrontFaces.size() < 4u || BackFaces.size() < 4u)
    {
        return { Polyhedron, {}, true, false, 0.0 };
    }

    return {
        { std::move(FrontVerts), std::move(FrontFaces) },
        { std::move(BackVerts),  std::move(BackFaces) },
        true, true, CutArea
    };
}

double ConvexPartitioning::ComputeDirectionalToughness(
    const FractureMaterialSpec& Material,
    Mathematics::Axis3D         PlaneNormal,
    double                      GrainAngleRad,
    DeterministicRandomStream*  Rand) noexcept
{
    double G = static_cast<double>(Material.FractureToughnessGc);
    const float Cg = static_cast<float>(std::cos(GrainAngleRad));
    const float Sg = static_cast<float>(std::sin(GrainAngleRad));
    const auto RotGrain = Mathematics::NormalizeAxis({
        Cg * Material.GrainAxis.X - Sg * Material.GrainAxis.Y,
        Sg * Material.GrainAxis.X + Cg * Material.GrainAxis.Y,
        Material.GrainAxis.Z
    });

    if (Material.Category == FractureMaterialCategory::Fibrous)
    {
        const double Across = std::abs(static_cast<double>(Mathematics::EvaluateDot(PlaneNormal, RotGrain)));
        G *= 1.0 + (static_cast<double>(Material.AnisotropyRatio) - 1.0) * Across * Across;
    }
    else if (Material.BeddingWeakness > 0.0f)
    {
        const double Align = std::abs(static_cast<double>(Mathematics::EvaluateDot(PlaneNormal, RotGrain)));
        G *= 1.0 - static_cast<double>(Material.BeddingWeakness) * std::pow(Align, 4.0);
    }

    if (Rand != nullptr)
    {
        const double U             = std::clamp(Rand->NextDouble(), 1.0e-4, 0.9999);
        const double WeibullFactor = std::pow(-std::log(1.0 - U), 1.0 / static_cast<double>(Material.WeibullModulus));
        G *= std::clamp(WeibullFactor, 0.35, 2.2);
    }
    return G;
}

std::vector<FractureBondEntry> ConvexPartitioning::ConstructSharedCutBonds(
    const std::vector<FracturePieceEntry>& Fragments,
    const FractureMaterialSpec&            Material,
    Mathematics::Axis3D                    ImpactPoint,
    double                                 SolidRadiusM,
    double                                 BondStrengthScale)
{
    struct TokenEntry
    {
        uint32_t FragIdx = 0u;
        double   Area    = 0.0;
    };

    std::unordered_map<uint32_t, std::vector<TokenEntry>> TokenGroups;
    for (uint32_t FragIdx = 0u; FragIdx < Fragments.size(); ++FragIdx)
    {
        const auto& Frag = Fragments[FragIdx];
        for (const auto& Face : Frag.Polyhedron.Faces)
        {
            if (Face.Interior && Face.CutToken > 0u)
            {
                TokenGroups[Face.CutToken].push_back({ FragIdx, ComputeFaceAreaM2(Frag.Polyhedron, Face) });
            }
        }
    }

    std::vector<FractureBondEntry> Bonds;
    std::unordered_set<uint64_t>   SeenPairs;

    auto AddBond = [&](uint32_t IndexA, uint32_t IndexB, double SharedArea)
    {
        if (IndexA == IndexB) return;
        const uint32_t Lo  = std::min(IndexA, IndexB);
        const uint32_t Hi  = std::max(IndexA, IndexB);
        const uint64_t Key = (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
        if (!SeenPairs.insert(Key).second) return;

        const auto&  A   = Fragments[Lo];
        const auto&  B   = Fragments[Hi];
        const auto   Mid = (A.Centroid + B.Centroid) * 0.5f;
        const double DImp = static_cast<double>(Mathematics::EvaluateDistance(Mid, ImpactPoint));
        const double ImpactStress = 1.0 / (0.16 + std::pow(DImp / std::max(0.25, SolidRadiusM), 1.65));
        const double Capacity = (0.55 + 0.45 * (static_cast<double>(Material.TensileStrengthMPa) / 60.0))
                              * BondStrengthScale
                              * (0.65 + std::min(1.4, SharedArea * 4.0));
        const double StrainRatio = std::min(1.6, ImpactStress / std::max(0.15, Capacity * 2.4));

        Bonds.push_back({
            Lo, Hi,
            A.Centroid, B.Centroid,
            SharedArea,
            SharedArea * static_cast<double>(Material.TensileStrengthMPa) * 1.0e3 * BondStrengthScale,
            StrainRatio,
            Material.StoredEnergyJPerM3 > 0.0f ? true : (StrainRatio >= 1.0)
        });
    };

    for (const auto& [Tok, Entries] : TokenGroups)
    {
        (void)Tok;
        for (size_t I = 0u; I < Entries.size(); ++I)
        {
            for (size_t J = I + 1u; J < Entries.size(); ++J)
            {
                const auto& Fa = Fragments[Entries[I].FragIdx];
                const auto& Fb = Fragments[Entries[J].FragIdx];
                if (Mathematics::EvaluateDistance(Fa.Centroid, Fb.Centroid) <= (Fa.RadiusM + Fb.RadiusM) * 1.12)
                {
                    AddBond(Entries[I].FragIdx, Entries[J].FragIdx, std::min(Entries[I].Area, Entries[J].Area));
                }
            }
        }
    }

    for (uint32_t I = 0u; I < Fragments.size(); ++I)
    {
        for (uint32_t J = I + 1u; J < Fragments.size(); ++J)
        {
            const double D = static_cast<double>(Mathematics::EvaluateDistance(Fragments[I].Centroid, Fragments[J].Centroid));
            if (D < (Fragments[I].RadiusM + Fragments[J].RadiusM) * 0.78)
            {
                AddBond(I, J, 0.04);
            }
        }
    }

    return Bonds;
}

FracturePartitionResult ConvexPartitioning::ExecuteFracturePartition(
    const ConvexPolyhedron&      SourcePolyhedron,
    const FractureSpecification& Specification)
{
    if (Specification.Solver == FractureSolverMethod::FastRuntimeBoussinesq)
    {
        return FastRuntimeFracture::Execute(SourcePolyhedron, Specification);
    }
    if (Specification.Solver == FractureSolverMethod::EngineeringBakedSolid)
    {
        return EngineeringBakedFracture::Execute(SourcePolyhedron, Specification);
    }

    const auto  StartClock = std::chrono::steady_clock::now();
    const auto& Material   = FractureMaterialRepository::LookupMaterial(Specification.MaterialId);
    DeterministicRandomStream Rand(Specification.Seed != 0u ? Specification.Seed : 42u);

    const auto     SourceMetrics     = ComputePolyhedronMetrics(SourcePolyhedron);
    const auto     ImpactPoint       = Specification.ImpactPoint;
    const auto     ImpactNormal      = Mathematics::NormalizeAxis(Specification.ImpactNormal, { 0.0f, -1.0f, 0.0f });
    const uint32_t Ceiling           = std::clamp(Specification.Ceiling, 2u, 160u);
    const double   MinSpan           = std::max(0.015, Specification.MinimumSizeM);
    const double   GrainAngleRad     = Specification.GrainAngleDeg * (PiConst / 180.0);
    const double   StoredTotalEnergy = static_cast<double>(Material.StoredEnergyJPerM3) * SourceMetrics.VolumeM3;
    const double   TotalEnergyBudget = std::max(40.0, Specification.EnergyJoules) + StoredTotalEnergy;

    uint32_t TokenCounter = 1u;
    SolverOutput Solved {};

    if (Specification.Solver == FractureSolverMethod::ClusteredVoronoi)
    {
        Solved.Pieces = SolveClusteredVoronoi(
            SourcePolyhedron, SourceMetrics, ImpactPoint, Ceiling, MinSpan, Rand, TokenCounter);
        for (const auto& P : Solved.Pieces)
        {
            Solved.SpentEnergy += ComputePolyhedronMetrics(P).CrackAreaM2 * static_cast<double>(Material.FractureToughnessGc);
        }
    }
    else if (Specification.Solver == FractureSolverMethod::LefmWavefront)
    {
        Solved = SolveLefmWavefront(
            SourcePolyhedron, SourceMetrics, Material, ImpactPoint, ImpactNormal,
            TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, TokenCounter);
    }
    else if (Specification.Solver == FractureSolverMethod::DendriticSpallHull)
    {
        Solved = SolveDendriticSpallHull(
            SourcePolyhedron, SourceMetrics, Material, ImpactPoint, ImpactNormal,
            Ceiling, MinSpan, GrainAngleRad, Rand, TokenCounter);
    }
    else
    {
        Solved = SolveGriffithStressBudget(
            SourcePolyhedron, SourceMetrics, Material, ImpactPoint, ImpactNormal,
            TotalEnergyBudget, Ceiling, MinSpan, GrainAngleRad, Rand, TokenCounter);
    }

    const double EjectionBase = std::sqrt(
        (2.0 * std::max(15.0, TotalEnergyBudget * 0.08))
        / std::max(0.8, SourceMetrics.VolumeM3 * static_cast<double>(Material.DensityKgPerM3)));

    std::vector<FracturePieceEntry> Fragments;
    Fragments.reserve(Solved.Pieces.size());
    double TotalFragmentVol  = 0.0;
    double TotalCrackAreaSum = 0.0;

    for (uint32_t Idx = 0u; Idx < Solved.Pieces.size(); ++Idx)
    {
        const auto& Poly    = Solved.Pieces[Idx];
        const auto  Metrics = ComputePolyhedronMetrics(Poly);
        const double DistImpact = static_cast<double>(Mathematics::EvaluateDistance(Metrics.Centroid, ImpactPoint));
        const double Proximity  = std::max(0.0, 1.0 - DistImpact / std::max(0.25, SourceMetrics.RadiusM * 1.65));
        const double Mass       = std::max(0.01, Metrics.VolumeM3 * static_cast<double>(Material.DensityKgPerM3));
        const auto   RadialOut  = Mathematics::NormalizeAxis((Metrics.Centroid - ImpactPoint) + ImpactNormal * 0.35f);
        const double EjectSpeed = std::min(18.0, EjectionBase * (0.28 + 1.45 * Proximity * Proximity));

        TotalFragmentVol  += Metrics.VolumeM3;
        TotalCrackAreaSum += Metrics.CrackAreaM2;

        char TokenBuf[24] {};
        std::snprintf(TokenBuf, sizeof(TokenBuf), "FRAG-%03u", Idx + 1u);

        FracturePieceEntry Entry {};
        Entry.Index             = Idx;
        Entry.Token             = TokenBuf;
        Entry.Polyhedron        = Poly;
        Entry.VolumeM3          = Metrics.VolumeM3;
        Entry.ConvexVolumeM3    = Metrics.VolumeM3;
        Entry.MassKg            = Mass;
        Entry.Centroid          = Metrics.Centroid;
        Entry.BoundsMin         = Metrics.BoundsMin;
        Entry.BoundsMax         = Metrics.BoundsMax;
        Entry.Span              = Metrics.Span;
        Entry.RadiusM           = Metrics.RadiusM;
        Entry.CrackAreaM2       = Metrics.CrackAreaM2;
        Entry.Proximity         = static_cast<float>(Proximity);
        Entry.StressRatio       = static_cast<float>(std::min(1.0, 0.18 + Proximity * 0.92));
        Entry.InitialVelocity   = RadialOut * static_cast<float>(EjectSpeed);
        Entry.InitialAngular    = {
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Proximity)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Proximity)),
            static_cast<float>((Rand.NextDouble() - 0.5) * 9.0 * (0.3 + Proximity))
        };
        Entry.VisibilityEnabled = true;
        Fragments.push_back(std::move(Entry));
    }

    auto Bonds = ConstructSharedCutBonds(
        Fragments, Material, ImpactPoint, SourceMetrics.RadiusM,
        Specification.BondStrengthScale > 0.0 ? Specification.BondStrengthScale : 1.0);

    const double TotalCrackArea   = TotalCrackAreaSum * 0.5;
    const double VolumeDriftRatio = SourceMetrics.VolumeM3 > 1.0e-9
        ? std::abs(TotalFragmentVol - SourceMetrics.VolumeM3) / SourceMetrics.VolumeM3
        : 0.0;
    const auto   EndClock         = std::chrono::steady_clock::now();
    const double ElapsedMs        = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    uint32_t IntactCount = 0u;
    for (const auto& Bond : Bonds)
    {
        if (!Bond.BrokenOnImpact) ++IntactCount;
    }

    FracturePartitionResult Result {};
    Result.Solver         = Specification.Solver;
    Result.MaterialSpec   = Material;
    Result.SourceMetrics  = SourceMetrics;
    Result.Fragments      = std::move(Fragments);
    Result.Bonds          = std::move(Bonds);
    Result.WavefrontPaths = std::move(Solved.WavefrontPaths);
    Result.Receipt        = {
        static_cast<uint32_t>(Result.Fragments.size()),
        static_cast<uint32_t>(Result.Bonds.size()),
        IntactCount,
        SourceMetrics.VolumeM3,
        TotalFragmentVol,
        VolumeDriftRatio,
        TotalCrackArea,
        TotalEnergyBudget,
        std::min(TotalEnergyBudget, Solved.SpentEnergy > 0.0 ? Solved.SpentEnergy : 2.0 * static_cast<double>(Material.FractureToughnessGc) * TotalCrackArea),
        FractureMaterialRepository::ComputeRayleighWaveSpeedMps(Material),
        ElapsedMs,
        VolumeDriftRatio < 1.0e-4
    };
    return Result;
}

PieceSdfSectionOutput ConvexPartitioning::ComputePieceSdfSection(
    const std::vector<FracturePieceEntry>& Fragments,
    float                                  SectionZ,
    uint32_t                               GridResolution,
    float                                  BoundsRadius)
{
    struct PlaneEq
    {
        Mathematics::Axis3D Normal {};
        float               Offset = 0.0f;
    };
    struct PiecePlanes
    {
        std::vector<PlaneEq> Planes   {};
        Mathematics::Axis3D  Centroid {};
        double               RadiusM  = 0.0;
    };

    std::vector<PiecePlanes> Precomputed;
    for (const auto& Frag : Fragments)
    {
        if (!Frag.VisibilityEnabled) continue;
        if (!Frag.SubConvexHulls.empty())
        {
            for (const auto& Sub : Frag.SubConvexHulls)
            {
                const auto Sm = ComputePolyhedronMetrics(Sub);
                PiecePlanes Item {};
                Item.Centroid = Sm.Centroid;
                Item.RadiusM  = Sm.RadiusM;
                for (const auto& Face : Sub.Faces)
                {
                    if (Face.Indices.empty()) continue;
                    const auto N  = ComputeFaceNormal(Sub, Face);
                    const auto V0 = Sub.Vertices[Face.Indices[0]];
                    Item.Planes.push_back({ N, Mathematics::EvaluateDot(N, V0) });
                }
                Precomputed.push_back(std::move(Item));
            }
        }
        else
        {
            PiecePlanes Item {};
            Item.Centroid = Frag.Centroid;
            Item.RadiusM  = Frag.RadiusM;
            for (const auto& Face : Frag.Polyhedron.Faces)
            {
                if (Face.Indices.empty()) continue;
                const auto N  = ComputeFaceNormal(Frag.Polyhedron, Face);
                const auto V0 = Frag.Polyhedron.Vertices[Face.Indices[0]];
                Item.Planes.push_back({ N, Mathematics::EvaluateDot(N, V0) });
            }
            Precomputed.push_back(std::move(Item));
        }
    }

    PieceSdfSectionOutput Output {};
    Output.Resolution = GridResolution;
    Output.Field.resize(static_cast<size_t>(GridResolution) * static_cast<size_t>(GridResolution), BoundsRadius);

    uint32_t InsideSamples = 0u;
    for (uint32_t Row = 0u; Row < GridResolution; ++Row)
    {
        const float Y = ((static_cast<float>(Row) / static_cast<float>(GridResolution - 1u)) * 2.0f - 1.0f) * BoundsRadius;
        for (uint32_t Col = 0u; Col < GridResolution; ++Col)
        {
            const float X = ((static_cast<float>(Col) / static_cast<float>(GridResolution - 1u)) * 2.0f - 1.0f) * BoundsRadius;
            const Mathematics::Axis3D P { X, Y, SectionZ };
            float MinSignedDist = std::numeric_limits<float>::infinity();

            for (const auto& Item : Precomputed)
            {
                const float QuickDist = Mathematics::EvaluateDistance(P, Item.Centroid) - static_cast<float>(Item.RadiusM);
                if (QuickDist > MinSignedDist) continue;
                float PieceDist = -std::numeric_limits<float>::infinity();
                for (const auto& Pl : Item.Planes)
                {
                    const float Sd = Mathematics::EvaluateDot(Pl.Normal, P) - Pl.Offset;
                    if (Sd > PieceDist) PieceDist = Sd;
                }
                if (PieceDist < MinSignedDist) MinSignedDist = PieceDist;
            }

            Output.Field[static_cast<size_t>(Row) * GridResolution + Col] = MinSignedDist;
            if (MinSignedDist <= 0.0f) ++InsideSamples;
        }
    }

    Output.OccupancyRatio = static_cast<double>(InsideSamples) / static_cast<double>(GridResolution * GridResolution);
    return Output;
}

SdfVolumeFootprint ConvexPartitioning::ComputeSdfVolumeFootprint(
    uint32_t FragmentCount,
    uint32_t SdfResolution,
    bool     Enabled) noexcept
{
    if (!Enabled)
    {
        return {};
    }
    const uint64_t VoxelsPerPiece = static_cast<uint64_t>(SdfResolution) * SdfResolution * SdfResolution;
    const uint64_t TotalVoxels    = static_cast<uint64_t>(FragmentCount) * VoxelsPerPiece;
    const uint64_t Bytes          = TotalVoxels * 2ull;
    return {
        Bytes,
        static_cast<double>(Bytes) / (1024.0 * 1024.0),
        TotalVoxels,
        std::min(96u, std::max(1u, FragmentCount))
    };
}

} // namespace Frontier::GeometrySpace::Fracture
