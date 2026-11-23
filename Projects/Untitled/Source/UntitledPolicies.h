/*============================================================================================================================================
                                                           UNTITLEDPOLICIES.H
============================================================================================================================================*/
// 🧩 Declarative policy bundle for reference project Untitled (Execution, GameplayRule, DeterministicDriving, and NetworkingSpace policies)

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

namespace Untitled
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UntitledPolicyBundle
{
    Frontier::Lifecycle::ProjectExecutionPolicy                   ExecutionPolicy   {}; // [-] - Cadence, opt-in domains & worker lanes
    Frontier::SimulationSpace::Policies::GameplayRulePolicy       GameplayPolicy    {}; // [-] - Fixed Hz, alignment hash & bounds rules
    Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy DrivingPolicy   {}; // [-] - 16->20->3 (1,612 B) deterministic driver
    Frontier::NetworkingSpace::ReplicationScheduleSpec            VehicleContract   {}; // [-] - Kinematic replication schedule
    Frontier::NetworkingSpace::TransportSelectionInput            TransportInput    {}; // [-] - Transport routing & failover rule
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Frontier::Lifecycle::ProjectExecutionPolicy             BuildUntitledExecutionPolicy() noexcept;
[[nodiscard]] Frontier::SimulationSpace::Policies::GameplayRulePolicy BuildUntitledGameplayPolicy() noexcept;
[[nodiscard]] Frontier::SimulationSpace::Policies::DeterministicDrivingPolicy BuildUntitledDrivingPolicy(uint64_t Seed = 0x554E5449ull) noexcept;
[[nodiscard]] UntitledPolicyBundle                                    BuildUntitledPolicyBundle(uint64_t DrivingSeed = 0x554E5449ull) noexcept;

} // namespace Untitled
