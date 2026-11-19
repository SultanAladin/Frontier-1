/*============================================================================================================================================
                                                           STREAMTRANSPORTFIELD.CPP
============================================================================================================================================*/
// 🧩 Directed FlowField ForceModel laws: FlowStream (Current, ThermalUpdraft, SlipstreamJet, NavigationField, VesselWake), SplineForce, Adhesion

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/StreamTransportField.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"
#include "Engine/SimulationSpace/ForceFields/VectorField.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

namespace
{

[[nodiscard]] SpatialForceSample EvaluateFlowStream(
    const ForceSourceRecordEntry& Entry,
    const BasisVectors3&          Basis,
    const SpatialVector3&         Delta,
    float                         Scale,
    double                        TimeSeconds) noexcept
{
    SpatialForceSample Output{};
    const float SafeR = std::max(0.25f, Entry.RadiusMeters);

    switch (Entry.StreamProfile)
    {
        case StreamTransportProfile::ThermalUpdraft:
        {
            const float          Axial  = DotProduct(Delta, Basis.Up);
            const SpatialVector3 Rad    = Delta - Basis.Up * Axial;
            const float          RadLen = VectorLength(Rad);
            const float          NormR  = std::clamp(RadLen / SafeR, 0.0f, 1.0f);
            const float          Bell   = (1.0f - NormR * NormR);
            const SpatialVector3 Tang   = NormalizeSafe(CrossProduct(Basis.Up, Rad), Basis.Left);
            const SpatialVector3 InPull = (RadLen > 1.0e-4f) ? (Rad * (-0.25f * Entry.SwirlRatio / SafeR)) : SpatialVector3{};
            Output.AdvectionVelocity    = Basis.Up * (Scale * std::max(0.15f, Bell)) + Tang * (Scale * Entry.SwirlRatio * 0.45f) + InPull * Scale;
            Output.LinearAcceleration   = Basis.Up * (Scale * 0.35f * std::max(0.15f, Bell));
            break;
        }

        case StreamTransportProfile::SlipstreamJet:
        {
            const float          FwdDist = std::max(0.0f, DotProduct(Delta, Basis.Forward));
            const SpatialVector3 Lat     = Delta - Basis.Forward * FwdDist;
            const float          JetCone = Entry.CoreRadiusMeters + FwdDist * 0.32f;
            const float          LatDist = VectorLength(Lat);
            const float          Profile = std::exp(-(LatDist * LatDist) / std::max(0.25f, JetCone * JetCone));
            Output.AdvectionVelocity     = Basis.Forward * (Scale * (1.0f + 0.5f * Profile));
            Output.LinearAcceleration    = Basis.Forward * (Scale * Profile * 0.4f);
            break;
        }

        case StreamTransportProfile::NavigationField:
        {
            const float          Axial     = DotProduct(Delta, Basis.Up);
            const SpatialVector3 Flat      = Delta - Basis.Up * Axial;
            const float          LateralY  = DotProduct(Flat, Basis.Left);
            const float          SteerGain = std::clamp(-LateralY / SafeR, -1.0f, 1.0f) * 0.55f;
            const SpatialVector3 Guided    = NormalizeSafe(Basis.Forward + Basis.Left * SteerGain, Basis.Forward);
            Output.AdvectionVelocity       = Guided * Scale;
            break;
        }

        case StreamTransportProfile::VesselWake:
        {
            const float          FwdDist  = DotProduct(Delta, Basis.Forward);
            const float          LatDist  = DotProduct(Delta, Basis.Left);
            const float          KelvinSign = (LatDist >= 0.0f) ? 1.0f : -1.0f;
            const float          WaveArg  = (FwdDist - std::fabs(LatDist) * 1.45f) * 0.25f - static_cast<float>(TimeSeconds) * 2.0f;
            const float          Ripple   = 0.5f + 0.5f * std::cos(WaveArg);
            const SpatialVector3 WakeDir  = NormalizeSafe(Basis.Forward * -0.7f + Basis.Left * (KelvinSign * 0.72f), Basis.Left);
            Output.AdvectionVelocity      = WakeDir * (Scale * (0.45f + 0.55f * Ripple));
            break;
        }

        case StreamTransportProfile::Current:
        default:
        {
            const float Meander = 0.28f * std::sin(DotProduct(Delta, Basis.Forward) / std::max(1.0f, Entry.TurbulenceScaleMeters) +
                                                   static_cast<float>(TimeSeconds) * Entry.FrequencyHertz);
            const SpatialVector3 StreamDir = NormalizeSafe(Basis.Forward + Basis.Left * Meander, Basis.Forward);
            Output.AdvectionVelocity       = StreamDir * Scale;
            break;
        }
    }

    return Output;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             FLOW FIELD LAW EVALUATION
//------------------------------------------------------------------------------------------------------------------------

SpatialForceSample EvaluateStreamTransportModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept
{
    (void)BodyVelocity;

    SpatialForceSample Output{};
    if (!Entry.Enabled)
    {
        return Output;
    }

    const BasisVectors3 Basis = ConstructOrthonormalBasisFromEuler(Entry.EulerDegrees);
    const float         Atten = EvaluateSpatialAttenuation(Entry, Basis, WorldPoint);
    if (Atten <= 1.0e-6f)
    {
        return Output;
    }

    float UnusedWave          = 0.0f;
    const float Temporal      = EvaluateTemporalModulation(Entry, TimeSeconds, UnusedWave);
    const float Scale         = Entry.Strength * Atten * Temporal;
    const SpatialVector3 Anchor = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;
    const SpatialVector3 Delta  = WorldPoint - Anchor;

    switch (Entry.Model)
    {
        case ForceModel::FlowStream:
            return EvaluateFlowStream(Entry, Basis, Delta, Scale, TimeSeconds);

        case ForceModel::SplineForce:
        {
            if (Entry.Motion.WaypointCount < 2u || Entry.Motion.Topology == MotionPathGeometry::Static)
            {
                Output.AdvectionVelocity = Basis.Forward * Scale;
                return Output;
            }

            // Find nearest phase along spline waypoints for local tangent + radial tube centering
            constexpr uint32_t SearchSteps = 16u;
            float              BestDistSq  = 1.0e30f;
            MotionSamplePoint  BestSample{};

            for (uint32_t Step = 0u; Step <= SearchSteps; ++Step)
            {
                const float             U      = static_cast<float>(Step) / static_cast<float>(SearchSteps);
                const MotionSamplePoint Sample = SampleForceMotionAtNormalizedPhase(Entry, U);
                const float             DistSq = VectorLengthSquared(WorldPoint - Sample.PositionMeters);
                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    BestSample = Sample;
                }
            }

            const SpatialVector3 RadialOffset = BestSample.PositionMeters - WorldPoint;
            const float          SafeCore     = std::max(0.5f, Entry.CoreRadiusMeters);
            const SpatialVector3 CenterPull   = RadialOffset / SafeCore;
            Output.AdvectionVelocity          = BestSample.TangentVector * Scale + CenterPull * (Scale * 0.45f);
            Output.LinearAcceleration         = CenterPull * (Scale * 0.35f);
            return Output;
        }

        case ForceModel::BoundaryAdhesion:
        {
            // Adhesion traction toward local reference boundary plane (dot(Delta, Basis.Up)) + tangential drag
            const float          SignedDist = DotProduct(Delta, Basis.Up);
            const SpatialVector3 PullNormal = Basis.Up * (-SignedDist / std::max(0.5f, Entry.CoreRadiusMeters));
            Output.LinearAcceleration       = PullNormal * Scale;
            Output.DragDecayRate            = std::max(0.0f, Entry.DragCoefficient) * Atten;
            return Output;
        }

        default:
            return Output;
    }
}

} // namespace Frontier::SimulationSpace::ForceFields
