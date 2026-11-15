/*============================================================================================================================================
                                                               VECTORFIELD.CPP
============================================================================================================================================*/
// 🧩 Shared spatial reach envelope attenuation helpers and analytical VectorField ForceModel laws (Right-Hand +Z Up)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/VectorField.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                        ENVELOPE, ATTENUATION & TEMPORAL MODULATION
//------------------------------------------------------------------------------------------------------------------------

float EvaluateReachEnvelopeNormalizedDistance(
    const ForceSourceRecordEntry& Entry,
    const BasisVectors3&          Basis,
    const SpatialVector3&         WorldPoint) noexcept
{
    const SpatialVector3 Anchor = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;
    const SpatialVector3 Delta  = WorldPoint - Anchor;
    const float          SafeR  = std::max(0.25f, Entry.RadiusMeters);

    switch (Entry.Envelope)
    {
        case ReachEnvelopeCategory::Column:
        {
            const float Axial       = std::fabs(DotProduct(Delta, Basis.Up));
            const SpatialVector3 Rad = Delta - Basis.Up * Axial;
            const float RadialRatio = VectorLength(Rad) / SafeR;
            const float AxialRatio  = Axial / std::max(0.25f, Entry.HalfExtentsMeters.Z);
            return std::max(RadialRatio, AxialRatio);
        }

        case ReachEnvelopeCategory::OrientedBox:
        case ReachEnvelopeCategory::PlanarBox:
        {
            const float Lx = std::fabs(DotProduct(Delta, Basis.Forward)) / std::max(0.25f, Entry.HalfExtentsMeters.X);
            const float Ly = std::fabs(DotProduct(Delta, Basis.Left))    / std::max(0.25f, Entry.HalfExtentsMeters.Y);
            const float Lz = std::fabs(DotProduct(Delta, Basis.Up))      / std::max(0.25f, Entry.HalfExtentsMeters.Z);
            return std::max({ Lx, Ly, Lz });
        }

        case ReachEnvelopeCategory::Sphere:
        default:
            return VectorLength(Delta) / SafeR;
    }
}

float EvaluateSpatialAttenuation(
    const ForceSourceRecordEntry& Entry,
    const BasisVectors3&          Basis,
    const SpatialVector3&         WorldPoint) noexcept
{
    const float U = EvaluateReachEnvelopeNormalizedDistance(Entry, Basis, WorldPoint);
    if (U >= 1.0f)
    {
        return 0.0f;
    }

    const float Taper = std::clamp(1.0f - U, 0.0f, 1.0f);
    const float Power = std::max(0.1f, Entry.FalloffExponent);

    switch (Entry.Attenuation)
    {
        case ForceAttenuationMode::None:
            return 1.0f;

        case ForceAttenuationMode::Linear:
            return std::pow(Taper, Power);

        case ForceAttenuationMode::InverseSquare:
        {
            const float Dist     = U * std::max(0.25f, Entry.RadiusMeters);
            const float Core     = std::max(0.1f, Entry.CoreRadiusMeters);
            const float RatioSq  = (Dist / Core) * (Dist / Core);
            const float EdgeFade = std::clamp(1.0f - U * U * U * U, 0.0f, 1.0f);
            return EdgeFade / (1.0f + RatioSq);
        }

        case ForceAttenuationMode::SmoothStep:
        default:
        {
            const float Hermite = Taper * Taper * (3.0f - 2.0f * Taper);
            return std::pow(Hermite, 0.25f + 0.75f * Power);
        }
    }
}

float EvaluateTemporalModulation(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds,
    float&                        OutImpulseWaveRadiusFactor) noexcept
{
    OutImpulseWaveRadiusFactor = 0.0f;

    switch (Entry.DurationMode)
    {
        case ForceDurationMode::Modulated:
        {
            const float Phase = Constants::TauFloat * std::max(0.01f, Entry.FrequencyHertz) * static_cast<float>(TimeSeconds);
            return 0.5f + 0.5f * std::sin(Phase);
        }

        case ForceDurationMode::Impulse:
        {
            const float Elapsed = static_cast<float>(TimeSeconds) - Entry.ImpulseTriggerTimeSec;
            if (Elapsed < 0.0f || Entry.ImpulseDurationSec <= 1.0e-4f)
            {
                return 0.0f;
            }
            const float Norm = Elapsed / Entry.ImpulseDurationSec;
            if (Norm > 2.0f)
            {
                return 0.0f;
            }
            OutImpulseWaveRadiusFactor = std::clamp(Norm, 0.0f, 1.25f);
            return std::exp(-2.8f * Norm);
        }

        case ForceDurationMode::Continuous:
        default:
            return 1.0f;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           ANALYTICAL VECTOR FIELD LAWS
//------------------------------------------------------------------------------------------------------------------------

SpatialForceSample EvaluateVectorFieldModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept
{
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

    float ImpulseWaveNorm = 0.0f;
    const float Temporal  = EvaluateTemporalModulation(Entry, TimeSeconds, ImpulseWaveNorm);
    const float Scale     = Entry.Strength * Atten * Temporal;
    if (std::fabs(Scale) <= 1.0e-6f && Entry.Model != ForceModel::Drag)
    {
        return Output;
    }

    const SpatialVector3 Anchor = Entry.DynamicMotion ? Entry.EvaluatedCentreMeters : Entry.CentreMeters;
    const SpatialVector3 Delta  = WorldPoint - Anchor;
    const float          SafeR  = std::max(0.25f, Entry.RadiusMeters);
    const float          CoreR  = std::max(0.1f, Entry.CoreRadiusMeters);

    switch (Entry.Model)
    {
        case ForceModel::LinearWind:
        {
            Output.AdvectionVelocity = Basis.Forward * Scale;
            break;
        }

        case ForceModel::Buoyancy:
        {
            // +Z Up buoyant acceleration & upward convection
            const float Uplift           = Scale * std::max(0.1f, Entry.BuoyancyDensityRatio);
            Output.AdvectionVelocity     = { 0.0f, 0.0f, Uplift * 0.45f, 0.0f };
            Output.LinearAcceleration    = { 0.0f, 0.0f, Uplift, 0.0f };
            break;
        }

        case ForceModel::RadialBurst:
        {
            const float          Dist     = VectorLength(Delta);
            const SpatialVector3 Radial   = (Dist > 1.0e-4f) ? (Delta / Dist) : Basis.Up;
            float                ShellMod = 1.0f;
            if (Entry.DurationMode == ForceDurationMode::Impulse && ImpulseWaveNorm > 0.0f)
            {
                const float FrontDist = ImpulseWaveNorm * SafeR;
                const float Sigma     = std::max(1.5f, SafeR * 0.22f);
                const float Diff      = Dist - FrontDist;
                ShellMod              = std::exp(-(Diff * Diff) / (2.0f * Sigma * Sigma));
            }
            const float SmoothCore       = Dist / std::sqrt(Dist * Dist + CoreR * CoreR);
            Output.LinearAcceleration    = Radial * (Scale * SmoothCore * ShellMod);
            Output.AdvectionVelocity     = Radial * (Scale * SmoothCore * ShellMod * 0.35f);
            break;
        }

        case ForceModel::Attractor:
        {
            const float          Dist       = VectorLength(Delta);
            const SpatialVector3 Inward     = (Dist > 1.0e-4f) ? (Delta * (-1.0f / Dist)) : SpatialVector3{};
            const float          SoftFactor = Dist / std::sqrt(Dist * Dist + CoreR * CoreR);
            Output.LinearAcceleration       = Inward * (Scale * SoftFactor);
            Output.AdvectionVelocity        = Inward * (Scale * SoftFactor * 0.4f);
            break;
        }

        case ForceModel::Repulsor:
        {
            const float          Dist       = VectorLength(Delta);
            const SpatialVector3 Outward    = (Dist > 1.0e-4f) ? (Delta / Dist) : Basis.Up;
            const float          SoftFactor = Dist / std::sqrt(Dist * Dist + CoreR * CoreR);
            Output.LinearAcceleration       = Outward * (Scale * SoftFactor);
            Output.AdvectionVelocity        = Outward * (Scale * SoftFactor * 0.4f);
            break;
        }

        case ForceModel::AxisAttractor:
        {
            const float          Axial    = DotProduct(Delta, Basis.Up);
            const SpatialVector3 RadVec   = Delta - Basis.Up * Axial;
            const float          RadDist  = VectorLength(RadVec);
            const SpatialVector3 Inward   = (RadDist > 1.0e-4f) ? (RadVec * (-1.0f / RadDist)) : SpatialVector3{};
            const float          SoftRad  = RadDist / std::sqrt(RadDist * RadDist + CoreR * CoreR);
            Output.LinearAcceleration     = Inward * (Scale * SoftRad);
            Output.AdvectionVelocity      = (Inward * SoftRad + Basis.Up * (Entry.SwirlRatio * 0.25f)) * Scale * 0.5f;
            break;
        }

        case ForceModel::Containment:
        {
            const float Dist = VectorLength(Delta);
            const float Norm = Dist / SafeR;
            if (Norm > 0.35f)
            {
                const float          EdgeRatio = std::clamp((Norm - 0.35f) / 0.65f, 0.0f, 1.0f);
                const SpatialVector3 Inward    = (Dist > 1.0e-4f) ? (Delta * (-1.0f / Dist)) : SpatialVector3{};
                Output.LinearAcceleration      = Inward * (Entry.Strength * Temporal * EdgeRatio * EdgeRatio);
                Output.AdvectionVelocity       = Inward * (Entry.Strength * Temporal * EdgeRatio * 0.5f);
            }
            break;
        }

        case ForceModel::RotationalColumn:
        {
            const float          Axial     = DotProduct(Delta, Basis.Up);
            const SpatialVector3 RadVec    = Delta - Basis.Up * Axial;
            const float          RadDist   = VectorLength(RadVec);
            const SpatialVector3 RadialDir = (RadDist > 1.0e-4f) ? (RadVec / RadDist) : Basis.Forward;
            const SpatialVector3 Tangent   = NormalizeSafe(CrossProduct(Basis.Up, RadialDir), Basis.Left);
            const float          SoftProfile = (RadDist / CoreR) / (1.0f + (RadDist / CoreR) * (RadDist / CoreR));
            const SpatialVector3 SwirlVel    = Tangent * (Scale * (0.65f + 1.35f * SoftProfile));
            const SpatialVector3 InwardPull  = RadialDir * (-Scale * std::fabs(Entry.SwirlRatio) * 0.32f * SoftProfile);
            const SpatialVector3 HelicalLift = Basis.Up * (Scale * Entry.SwirlRatio * 0.35f);
            Output.AdvectionVelocity         = SwirlVel + InwardPull + HelicalLift;
            Output.AngularTorque             = Basis.Up * (Scale / SafeR);
            break;
        }

        case ForceModel::Torque:
        {
            const float          Axial   = DotProduct(Delta, Basis.Up);
            const SpatialVector3 RadVec  = Delta - Basis.Up * Axial;
            const SpatialVector3 Tangent = CrossProduct(Basis.Up, RadVec / SafeR);
            Output.AngularTorque         = Basis.Up * Scale;
            Output.LinearAcceleration    = Tangent * (Scale * 0.5f);
            break;
        }

        case ForceModel::Drag:
        {
            const float DragRate    = std::max(0.0f, Entry.DragCoefficient) * Atten * Temporal;
            Output.DragDecayRate    = DragRate;
            Output.LinearAcceleration = BodyVelocity * (-DragRate);
            break;
        }

        case ForceModel::LorentzField:
        {
            // q (v x B) with B along local Basis.Up
            const SpatialVector3 VelCrossB = CrossProduct(BodyVelocity, Basis.Up);
            const SpatialVector3 RefDir    = (VectorLengthSquared(VelCrossB) > 1.0e-6f)
                                                 ? VelCrossB
                                                 : CrossProduct(Basis.Forward, Basis.Up);
            Output.LinearAcceleration      = RefDir * Scale;
            Output.AdvectionVelocity       = CrossProduct(Basis.Forward, Basis.Up) * (Scale * 0.35f);
            break;
        }

        case ForceModel::MagneticDipole:
        {
            const float          Dist    = std::max(CoreR, VectorLength(Delta));
            const SpatialVector3 RHat    = (Dist > 1.0e-4f) ? (Delta / VectorLength(Delta)) : Basis.Up;
            const float          MDotR   = DotProduct(Basis.Up, RHat);
            const SpatialVector3 DipoleB = RHat * (3.0f * MDotR) - Basis.Up;
            const float          Falloff = (CoreR * CoreR) / (Dist * Dist);
            Output.AdvectionVelocity     = DipoleB * (Scale * Falloff);
            Output.LinearAcceleration    = DipoleB * (Scale * Falloff * 0.65f);
            break;
        }

        default:
            break;
    }

    return Output;
}

} // namespace Frontier::SimulationSpace::ForceFields
