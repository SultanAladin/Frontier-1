/*============================================================================================================================================
                                                             FORCEMOTIONPATH.CPP
============================================================================================================================================*/
// 🧩 Deterministic 3D LineAB, Catmull-Rom spline, and helical orbit trajectory evaluator for ForceSourceRecordEntry

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] SpatialVector3 EvaluateCatmullRomSegment(
    const SpatialVector3& P0,
    const SpatialVector3& P1,
    const SpatialVector3& P2,
    const SpatialVector3& P3,
    float                 μ) noexcept
{
    const float μ2 = μ * μ;
    const float μ3 = μ2 * μ;

    return {
        0.5f * ((2.0f * P1.X) + (-P0.X + P2.X) * μ + (2.0f * P0.X - 5.0f * P1.X + 4.0f * P2.X - P3.X) * μ2 + (-P0.X + 3.0f * P1.X - 3.0f * P2.X + P3.X) * μ3),
        0.5f * ((2.0f * P1.Y) + (-P0.Y + P2.Y) * μ + (2.0f * P0.Y - 5.0f * P1.Y + 4.0f * P2.Y - P3.Y) * μ2 + (-P0.Y + 3.0f * P1.Y - 3.0f * P2.Y + P3.Y) * μ3),
        0.5f * ((2.0f * P1.Z) + (-P0.Z + P2.Z) * μ + (2.0f * P0.Z - 5.0f * P1.Z + 4.0f * P2.Z - P3.Z) * μ2 + (-P0.Z + 3.0f * P1.Z - 3.0f * P2.Z + P3.Z) * μ3),
        0.0f,
    };
}

[[nodiscard]] SpatialVector3 SampleRawCurvePosition(const ForceSourceRecordEntry& Entry, float NormalizedU) noexcept
{
    if (Entry.Motion.Topology == MotionPathTopology::Static)
    {
        return Entry.CentreMeters;
    }

    const uint32_t Count = std::min<uint32_t>(Entry.Motion.WaypointCount, MaxForceMotionWaypoints);
    if (Entry.Motion.Topology == MotionPathTopology::HelicalOrbit)
    {
        const SpatialVector3 Anchor = (Count > 0u) ? Entry.Motion.Waypoints[0] : Entry.CentreMeters;
        const float          θ      = NormalizedU * Constants::TauFloat * std::max(0.25f, Entry.Motion.OrbitTurns);
        const float          Radius = std::max(0.25f, Entry.Motion.OrbitRadiusMeters);
        const float          ZLift  = std::sin(θ) * Entry.Motion.OrbitVerticalAmplitudeMeters;
        return {
            Anchor.X + Radius * std::cos(θ),
            Anchor.Y + Radius * std::sin(θ),
            Anchor.Z + ZLift,
            0.0f,
        };
    }

    if (Count == 0u)
    {
        return Entry.CentreMeters;
    }

    if (Count == 1u)
    {
        return Entry.Motion.Waypoints[0];
    }

    const float ClampedU = std::clamp(NormalizedU, 0.0f, 1.0f);
    if (Entry.Motion.Topology == MotionPathTopology::LinearSpan || Count == 2u)
    {
        const float          Scaled = ClampedU * static_cast<float>(Count - 1u);
        const uint32_t       SegIdx = std::min(Count - 2u, static_cast<uint32_t>(std::floor(Scaled)));
        const float          LocalT = Scaled - static_cast<float>(SegIdx);
        const SpatialVector3& A     = Entry.Motion.Waypoints[SegIdx];
        const SpatialVector3& B     = Entry.Motion.Waypoints[SegIdx + 1u];
        return A + (B - A) * LocalT;
    }

    // Smooth 3D Catmull-Rom spline with phantom reflected endpoint knots
    const float    Scaled  = ClampedU * static_cast<float>(Count - 1u);
    const uint32_t SegIdx  = std::min(Count - 2u, static_cast<uint32_t>(std::floor(Scaled)));
    const float    LocalU  = Scaled - static_cast<float>(SegIdx);

    const SpatialVector3& P1 = Entry.Motion.Waypoints[SegIdx];
    const SpatialVector3& P2 = Entry.Motion.Waypoints[SegIdx + 1u];
    const SpatialVector3  P0 = (SegIdx > 0u) ? Entry.Motion.Waypoints[SegIdx - 1u] : (P1 + (P1 - P2));
    const SpatialVector3  P3 = (SegIdx + 2u < Count) ? Entry.Motion.Waypoints[SegIdx + 2u] : (P2 + (P2 - P1));

    return EvaluateCatmullRomSegment(P0, P1, P2, P3, LocalU);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            PATH INITIALIZATION & EVALUATION
//------------------------------------------------------------------------------------------------------------------------

void AssignPresetMotionPath(
    ForceSourceRecordEntry& Entry,
    MotionPathTopology      PresetTopology) noexcept
{
    const SpatialVector3 Base = Entry.CentreMeters;
    Entry.Motion.Topology     = PresetTopology;

    switch (PresetTopology)
    {
        case MotionPathTopology::Static:
        {
            Entry.DynamicMotion          = false;
            Entry.Motion.WaypointCount   = 0u;
            Entry.EvaluatedCentreMeters  = Base;
            Entry.Motion.NormalizedProgress = 0.0f;
            break;
        }

        case MotionPathTopology::LinearSpan:
        {
            Entry.DynamicMotion        = true;
            Entry.Motion.WaypointCount = 2u;
            Entry.Motion.Waypoints[0]  = { Base.X - 24.0f, Base.Y, Base.Z, 0.0f };
            Entry.Motion.Waypoints[1]  = { Base.X + 24.0f, Base.Y, Base.Z, 0.0f };
            break;
        }

        case MotionPathTopology::CatmullSpline:
        {
            Entry.DynamicMotion        = true;
            Entry.Motion.WaypointCount = 4u;
            Entry.Motion.Waypoints[0]  = { Base.X - 26.0f, Base.Y - 12.0f, Base.Z, 0.0f };
            Entry.Motion.Waypoints[1]  = { Base.X - 8.0f,  Base.Y + 18.0f, Base.Z + 6.0f, 0.0f };
            Entry.Motion.Waypoints[2]  = { Base.X + 10.0f, Base.Y - 16.0f, Base.Z + 8.0f, 0.0f };
            Entry.Motion.Waypoints[3]  = { Base.X + 28.0f, Base.Y + 10.0f, Base.Z, 0.0f };
            break;
        }

        case MotionPathTopology::HelicalOrbit:
        {
            Entry.DynamicMotion                      = true;
            Entry.Motion.WaypointCount               = 1u;
            Entry.Motion.Waypoints[0]                = Base;
            Entry.Motion.OrbitRadiusMeters           = std::max(8.0f, Entry.RadiusMeters * 0.75f);
            Entry.Motion.OrbitVerticalAmplitudeMeters = 5.0f;
            Entry.Motion.OrbitTurns                  = 1.0f;
            break;
        }
    }

    Entry.Motion.EvaluatedArcLengthMeters = ComputeMotionPathArcLengthMeters(Entry);
    if (Entry.DynamicMotion)
    {
        const MotionSamplePoint Initial = SampleForceMotionAtNormalizedPhase(Entry, Entry.Motion.PhaseOffset);
        Entry.EvaluatedCentreMeters     = Initial.PositionMeters;
        Entry.Motion.NormalizedProgress = Initial.NormalizedPhase;
    }
}

float ComputeMotionPathArcLengthMeters(const ForceSourceRecordEntry& Entry) noexcept
{
    if (Entry.Motion.Topology == MotionPathTopology::Static)
    {
        return 0.0f;
    }
    if (Entry.Motion.Topology == MotionPathTopology::HelicalOrbit)
    {
        const float Circumference = Constants::TauFloat * std::max(0.25f, Entry.Motion.OrbitRadiusMeters) *
                                    std::max(0.25f, Entry.Motion.OrbitTurns);
        return std::sqrt(Circumference * Circumference +
                         4.0f * Entry.Motion.OrbitVerticalAmplitudeMeters * Entry.Motion.OrbitVerticalAmplitudeMeters);
    }
    if (Entry.Motion.WaypointCount < 2u)
    {
        return 0.0f;
    }

    constexpr uint32_t SampleSegments = 32u;
    SpatialVector3     Previous       = SampleRawCurvePosition(Entry, 0.0f);
    float              Accumulated    = 0.0f;
    for (uint32_t Step = 1u; Step <= SampleSegments; ++Step)
    {
        const float          U       = static_cast<float>(Step) / static_cast<float>(SampleSegments);
        const SpatialVector3 Current = SampleRawCurvePosition(Entry, U);
        Accumulated += VectorLength(Current - Previous);
        Previous     = Current;
    }
    return Accumulated;
}

float ResolveCycleNormalizedPhase(
    double              TimeSeconds,
    float               ArcLengthMeters,
    float               SpeedMetresPerSecond,
    float               PhaseOffset,
    MotionCycleBehavior CycleBehavior) noexcept
{
    if (ArcLengthMeters <= 1.0e-4f || SpeedMetresPerSecond <= 1.0e-5f)
    {
        return std::clamp(PhaseOffset, 0.0f, 1.0f);
    }

    const double Cycles = (std::max(0.0, TimeSeconds) * static_cast<double>(SpeedMetresPerSecond)) /
                              static_cast<double>(ArcLengthMeters) +
                          static_cast<double>(PhaseOffset);

    switch (CycleBehavior)
    {
        case MotionCycleBehavior::PlayOnce:
            return static_cast<float>(std::clamp(Cycles, 0.0, 1.0));

        case MotionCycleBehavior::WrapLoop:
        {
            const double Wrapped = Cycles - std::floor(Cycles);
            return static_cast<float>(std::clamp(Wrapped, 0.0, 1.0));
        }

        case MotionCycleBehavior::PingPong:
        default:
        {
            const double CycleMod2 = std::fmod(std::max(0.0, Cycles), 2.0);
            const double Tri       = (CycleMod2 <= 1.0) ? CycleMod2 : (2.0 - CycleMod2);
            return static_cast<float>(std::clamp(Tri, 0.0, 1.0));
        }
    }
}

MotionSamplePoint SampleForceMotionAtNormalizedPhase(
    const ForceSourceRecordEntry& Entry,
    float                         NormalizedPhase) noexcept
{
    const float U = std::clamp(NormalizedPhase, 0.0f, 1.0f);

    MotionSamplePoint Result{};
    Result.NormalizedPhase = U;
    Result.PositionMeters  = SampleRawCurvePosition(Entry, U);

    constexpr float DeltaU = 0.004f;
    const float     U0     = std::max(0.0f, U - DeltaU);
    const float     U1     = std::min(1.0f, U + DeltaU);
    const SpatialVector3 Ahead  = SampleRawCurvePosition(Entry, U1);
    const SpatialVector3 Behind = SampleRawCurvePosition(Entry, U0);

    const BasisVectors3 Fallback = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
    Result.TangentVector         = NormalizeSafe(Ahead - Behind, Fallback.Forward);
    return Result;
}

MotionSamplePoint EvaluateForceMotionAtTime(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds) noexcept
{
    if (!Entry.DynamicMotion || Entry.Motion.Topology == MotionPathTopology::Static)
    {
        const BasisVectors3 Basis = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
        return { Entry.CentreMeters, Basis.Forward, 0.0f };
    }

    const float ArcLen = (Entry.Motion.EvaluatedArcLengthMeters > 1.0e-4f)
                             ? Entry.Motion.EvaluatedArcLengthMeters
                             : ComputeMotionPathArcLengthMeters(Entry);

    const float Normalized = ResolveCycleNormalizedPhase(
        TimeSeconds,
        ArcLen,
        Entry.Motion.SpeedMetresPerSecond,
        Entry.Motion.PhaseOffset,
        Entry.Motion.CycleBehavior);

    return SampleForceMotionAtNormalizedPhase(Entry, Normalized);
}

void StepAnimatedForceSources(
    std::span<ForceSourceRecordEntry> Sources,
    double                            TimeSeconds,
    bool                              AdvanceMotion) noexcept
{
    for (ForceSourceRecordEntry& Entry : Sources)
    {
        if (!Entry.DynamicMotion || Entry.Motion.Topology == MotionPathTopology::Static)
        {
            Entry.EvaluatedCentreMeters = Entry.CentreMeters;
            continue;
        }

        Entry.Motion.EvaluatedArcLengthMeters = ComputeMotionPathArcLengthMeters(Entry);
        if (!AdvanceMotion)
        {
            const MotionSamplePoint Scrubbed = SampleForceMotionAtNormalizedPhase(Entry, Entry.Motion.NormalizedProgress);
            Entry.EvaluatedCentreMeters      = Scrubbed.PositionMeters;
            continue;
        }

        const MotionSamplePoint Sample  = EvaluateForceMotionAtTime(Entry, TimeSeconds);
        Entry.EvaluatedCentreMeters     = Sample.PositionMeters;
        Entry.Motion.NormalizedProgress = Sample.NormalizedPhase;

        if (Entry.Motion.OrientAlongPathTangent && Entry.Motion.Topology != MotionPathTopology::Static)
        {
            Entry.EulerDegrees = ComputeEulerDegreesFromDirection(Sample.TangentVector);
        }
    }
}

} // namespace Frontier::SimulationSpace::ForceFields
