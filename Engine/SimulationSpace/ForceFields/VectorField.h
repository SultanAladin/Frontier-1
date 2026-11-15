/*============================================================================================================================================
                                                                VECTORFIELD.H
============================================================================================================================================*/
// 🧩 Shared spatial reach envelope attenuation helpers and analytical VectorField ForceModel laws (Right-Hand +Z Up)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

namespace Frontier::SimulationSpace::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float EvaluateReachEnvelopeNormalizedDistance(
    const ForceSourceRecordEntry& Entry,
    const BasisVectors3&          Basis,
    const SpatialVector3&         WorldPoint) noexcept;

[[nodiscard]] float EvaluateSpatialAttenuation(
    const ForceSourceRecordEntry& Entry,
    const BasisVectors3&          Basis,
    const SpatialVector3&         WorldPoint) noexcept;

[[nodiscard]] float EvaluateTemporalModulation(
    const ForceSourceRecordEntry& Entry,
    double                        TimeSeconds,
    float&                        OutImpulseWaveRadiusFactor) noexcept;

[[nodiscard]] SpatialForceSample EvaluateVectorFieldModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
