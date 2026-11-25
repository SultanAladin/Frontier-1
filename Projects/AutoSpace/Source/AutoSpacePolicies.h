/*============================================================================================================================================
                                                           AUTOSPACEPOLICIES.H
============================================================================================================================================*/
// 🧩 Declarative policy bundle for project AutoSpace (Execution, GameplayRule, DeterministicDriving, and NetworkingSpace policies)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Lifecycle/ProjectExecutionPolicy.h"
#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"
#include "Engine/SimulationSpace/Policies/DeterministicDrivingPolicy.h"
#include "Engine/SimulationSpace/Policies/GameplayRulePolicy.h"

#include <cstdint>

namespace AutoSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AutoSpacePolicyBundle
{
    Frontier::Lifecycle::ProjectExecutionPolicy                   ExecutionPolicy   {}; // [-] - Cadence, opt-in domains & worker lanes
    Frontier::SimulationSpace::Policies::GameplayRulePolicy       GameplayPolicy    {}; // [-] - Fixed Hz, alignment hash & bounds rules
    Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy DrivingPolicy   {}; // [-] - 16->20->3 deterministic driver policy
    Frontier::NetworkingSpace::ReplicationScheduleSpec            VehicleContract   {}; // [-] - Kinematic replication schedule
    Frontier::NetworkingSpace::TransportSelectionInput            TransportInput    {}; // [-] - Transport routing & failover rule
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Frontier::Lifecycle::ProjectExecutionPolicy                   BuildAutoSpaceExecutionPolicy() noexcept;
[[nodiscard]] Frontier::SimulationSpace::Policies::GameplayRulePolicy       BuildAutoSpaceGameplayPolicy() noexcept;
[[nodiscard]] Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy BuildAutoSpaceDrivingPolicy(uint64_t Seed = 0x4155544Full) noexcept;
[[nodiscard]] AutoSpacePolicyBundle                                         BuildAutoSpacePolicyBundle(uint64_t DrivingSeed = 0x4155544Full) noexcept;

} // namespace AutoSpace
