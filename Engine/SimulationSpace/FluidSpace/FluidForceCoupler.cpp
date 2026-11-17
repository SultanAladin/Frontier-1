/*============================================================================================================================================
                                                          FLUIDFORCECOUPLER.CPP
============================================================================================================================================*/
// 🧩 Samples ForceFieldAccumulator & FieldLatticeCascade into Fluid velocity and emits RadialBurst impulses on detonation

#include "Engine/SimulationSpace/FluidSpace/FluidForceCoupler.h"

#include <algorithm>
#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

Mathematics::Axis3D FluidForceCoupler::SampleExternalForceVelocity(
    const FluidCombustionSettings&            Combustion,
    const Mathematics::Axis3D&                WorldPoint,
    float                                     SimulationSeconds,
    const ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const ForceFields::FieldLatticeCascade*   OptionalLattice,
    float*                                    OutCurlScalar) noexcept
{
    Mathematics::Axis3D Out { Combustion.WindVelocityXY.X, Combustion.WindVelocityXY.Y, 0.0f };
    float               Curl = 0.0f;

    if (!Combustion.CoupleForceFields)
    {
        if (OutCurlScalar != nullptr) *OutCurlScalar = 0.0f;
        return Out;
    }

    const ForceFields::SpatialVector3 Pt { WorldPoint.X, WorldPoint.Y, WorldPoint.Z };

    if (OptionalForceFields != nullptr && OptionalForceFields->QueryActiveSourceCount() > 0u)
    {
        const auto Sample = OptionalForceFields->QueryForceFieldSample(Pt, {}, static_cast<double>(SimulationSeconds));
        Out.X += Sample.AdvectionVelocity.X + Sample.LinearAcceleration.X * 0.5f;
        Out.Y += Sample.AdvectionVelocity.Y + Sample.LinearAcceleration.Y * 0.5f;
        Out.Z += Sample.AdvectionVelocity.Z + Sample.LinearAcceleration.Z * 0.5f;
        Curl  += std::abs(Sample.AngularTorque.X) + std::abs(Sample.AngularTorque.Y) + std::abs(Sample.AngularTorque.Z);
    }

    if (OptionalLattice != nullptr)
    {
        const auto V0 = OptionalLattice->SampleTierTrilinear(0u, Pt);
        Out.X += V0.VelocityX;
        Out.Y += V0.VelocityY;
        Out.Z += V0.VelocityZ;
        Curl  += V0.CurlScalar;
    }

    if (OutCurlScalar != nullptr) *OutCurlScalar = Curl;
    return Out;
}

void FluidForceCoupler::EmitBlastForceImpulse(
    const FluidBlastSpecification&      Blast,
    double                              SimulationSeconds,
    ForceFields::ForceFieldAccumulator& InOutForceFields) noexcept
{
    if (!Blast.TriggerForceImpulse)
    {
        return;
    }

    constexpr uint32_t BlastForceToken = 0xF101D001u;
    auto               Existing        = InOutForceFields.QuerySources();
    std::vector<ForceFields::ForceSourceRecordEntry> Updated(Existing.begin(), Existing.end());

    ForceFields::ForceSourceRecordEntry* Target = nullptr;
    for (auto& Src : Updated)
    {
        if (Src.UniqueToken == BlastForceToken)
        {
            Target = &Src;
            break;
        }
    }

    if (Target == nullptr)
    {
        ForceFields::ForceSourceRecordEntry Burst {};
        Burst.UniqueToken = BlastForceToken;
        Burst.Label       = "Fluid Detonation Shockwave";
        Updated.push_back(Burst);
        Target = &Updated.back();
    }

    Target->Model                 = ForceFields::ForceModel::RadialBurst;
    Target->Domain                = ForceFields::ForceDomainCategory::TurbulenceField;
    Target->DurationMode          = ForceFields::ForceDurationMode::Impulse;
    Target->Envelope              = ForceFields::ReachEnvelopeCategory::Sphere;
    Target->Attenuation           = ForceFields::ForceAttenuationMode::InverseSquare;
    Target->CentreMeters          = { Blast.CenterWorld.X, Blast.CenterWorld.Y, Blast.CenterWorld.Z };
    Target->EvaluatedCentreMeters = Target->CentreMeters;
    Target->RadiusMeters          = std::max(2.5f, Blast.RadiusMeters * 18.0f);
    Target->CoreRadiusMeters      = std::max(0.25f, Blast.RadiusMeters * 1.5f);
    Target->Strength              = Blast.StrengthMps * 2.2f;
    Target->ImpulseTriggerTimeSec = static_cast<float>(SimulationSeconds);
    Target->ImpulseDurationSec    = 0.85f;
    Target->Enabled               = true;

    InOutForceFields.AssignSources(Updated);
    InOutForceFields.TriggerImpulse(BlastForceToken, SimulationSeconds);
}

} // namespace Frontier::SimulationSpace::FluidSpace
