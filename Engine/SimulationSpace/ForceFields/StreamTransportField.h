/*============================================================================================================================================
                                                            STREAMTRANSPORTFIELD.H
============================================================================================================================================*/
// 🧩 Directed FlowField ForceModel laws: FlowStream (Current, ThermalUpdraft, SlipstreamJet, NavigationField, VesselWake), SplineForce, Adhesion

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

[[nodiscard]] SpatialForceSample EvaluateStreamTransportModel(
    const ForceSourceRecordEntry& Entry,
    const SpatialVector3&         WorldPoint,
    const SpatialVector3&         BodyVelocity,
    double                        TimeSeconds) noexcept;

} // namespace Frontier::SimulationSpace::ForceFields
