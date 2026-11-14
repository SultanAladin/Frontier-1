/*============================================================================================================================================
                                                            PARTICLEDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleDocument helpers and Studio Default scene seeding

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleDocument.h"

namespace Frontier::ParticleSpace
{

namespace
{

using SimulationSpace::ForceFields::ForceDomainCategory;
using SimulationSpace::ForceFields::ForceDurationMode;
using SimulationSpace::ForceFields::ForceModel;
using SimulationSpace::ForceFields::ForceSourceRecordEntry;
using SimulationSpace::ForceFields::ReachEnvelopeCategory;

ForceSourceRecordEntry CreateForceSource(
    uint32_t            Token,
    const char*         Label,
    ForceModel          Model,
    ForceDomainCategory Domain,
    float               Strength,
    float               Radius,
    float               Cx,
    float               Cy,
    float               Cz,
    ForceDurationMode   Duration = ForceDurationMode::Continuous)
{
    ForceSourceRecordEntry F {};
    F.UniqueToken      = Token;
    F.Label            = Label;
    F.Model            = Model;
    F.Domain           = Domain;
    F.Envelope              = ReachEnvelopeCategory::Sphere;
    F.DurationMode          = Duration;
    F.Strength              = Strength;
    F.RadiusMeters          = Radius;
    F.CentreMeters          = { Cx, Cy, Cz };
    F.EvaluatedCentreMeters = { Cx, Cy, Cz };
    F.Enabled               = true;
    return F;
}

} // namespace

uint32_t ParticleDocument::ComputeTotalSlotCapacity() const noexcept
{
    uint32_t Total = 0u;
    for (const auto& R : Rigs)
    {
        Total += R.SlotCapacity;
    }
    return Total;
}

uint32_t ParticleDocument::CountVisibleRigs() const noexcept
{
    uint32_t VisibleCount = 0u;
    for (const auto& R : Rigs)
    {
        if (R.Visible) ++VisibleCount;
    }
    return VisibleCount;
}

ParticleDocument ConstructStudioDefaultDocument()
{
    ParticleDocument Doc {};
    Doc.DocumentTitle  = "StudioDefault";
    Doc.ActivePresetId = "object_surface_pour";
    Doc.Seed           = 1337u;

    Doc.Rigs.push_back(ConstructDefaultStudioRig());

    Doc.ForceSources = {
        CreateForceSource(101u, "Wind Generator East", ForceModel::LinearWind,       ForceDomainCategory::FlowField,   8.5f,  4.2f, -3.2f, 1.8f,  0.0f),
        CreateForceSource(102u, "Swirl Column",        ForceModel::RotationalColumn, ForceDomainCategory::VectorField, 14.5f, 3.6f,  2.1f, 2.0f, -1.5f),
        CreateForceSource(103u, "Jet Corridor",        ForceModel::FlowStream,       ForceDomainCategory::FlowField,   11.5f, 2.2f, -1.0f, 1.4f,  2.4f),
        CreateForceSource(104u, "Impulse Shell",       ForceModel::RadialBurst,      ForceDomainCategory::VectorField, 18.0f, 3.4f,  0.0f, 1.6f,  0.0f, ForceDurationMode::Impulse),
        CreateForceSource(105u, "Attractor",           ForceModel::Attractor,        ForceDomainCategory::VectorField,  9.5f, 2.8f,  2.8f, 2.2f,  1.2f),
        CreateForceSource(106u, "Repulsor",            ForceModel::Repulsor,         ForceDomainCategory::VectorField, 11.5f, 2.2f, -2.5f, 1.5f, -1.8f),
    };

    return Doc;
}

} // namespace Frontier::ParticleSpace
