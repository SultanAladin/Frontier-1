/*============================================================================================================================================
                                                             REPLICATIONPOLICY.H
============================================================================================================================================*/
// 🧩 Authority ownership, transport delivery reliability channels, component publication masks, and rate-limited publish scheduling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string_view>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class AuthorityMode : uint8_t
{
    HostAuthoritative             = 0, // [-] - Host owns simulation truth; clients interpolate received snapshots
    ClientPredictedHostReconciled = 1, // [-] - Local client predicts ahead; host snapshots reconcile divergence
    PeerAuthoritative             = 2, // [-] - Owning peer publishes authoritative updates for collaborative workspaces
};

enum class DeliveryChannel : uint8_t
{
    UnreliableSequenced = 0, // [-] - Drop stale out-of-order datagrams; zero head-of-line blocking (spatial snapshots)
    ReliableUnordered   = 1, // [-] - Retransmit lost datagrams without ordering stalls (asset/chunk transfers)
    ReliableOrdered     = 2, // [-] - Strict sequence conservation (lifecycle transitions & workspace revisions)
};

enum class ReplicationComponentMask : uint32_t
{
    None                      = 0u,      // [mask] - Zero replicated components
    EnableTransform           = 1u << 0, // [mask] - Sector + quantized local XYZ + smallest-three quaternion
    EnableLinearVelocity      = 1u << 1, // [mask] - 16-bit half-range linear velocity vector
    EnableAngularVelocity     = 1u << 2, // [mask] - 16-bit half-range angular velocity vector
    EnableKinematicParameters = 1u << 3, // [mask] - Suspension / articulation / domain parameters
    EnableWorkspaceRevision   = 1u << 4, // [mask] - Collaborative .Space revision delta stream
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ReplicationScheduleContract
{
    uint32_t      ContractIdentifier        = 0u;                                  // [-]  - Stable replication contract identifier
    AuthorityMode Authority                 = AuthorityMode::HostAuthoritative;    // [-]  - Ownership authority rule
    DeliveryChannel Channel                 = DeliveryChannel::UnreliableSequenced;// [-]  - Wire reliability channel
    uint32_t      ComponentMask             = 0u;                                  // [mask] - Replicated component mask
    float         PublishFrequencyHz        = 30.0f;                               // [Hz] - Maximum publication rate
    float         SpatialDeadbandMillimeters= 1.5f;                                // [mm] - Minimum motion before dirtying transform
    uint32_t      LastPublishedRevision     = 0u;                                  // [-]  - Revision most recently sent on wire
    double        SecondsSinceLastPublish   = 0.0;                                 // [s]  - Accumulated elapsed time since publish
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ReplicationScheduleContract BuildRealtimeKinematicContract(uint32_t ContractIdentifier, float PublishFrequencyHz = 30.0f) noexcept;
[[nodiscard]] ReplicationScheduleContract BuildCollaborativeRevisionContract(uint32_t ContractIdentifier, float PublishFrequencyHz = 20.0f) noexcept;

void                                      AccumulateReplicationElapsed(ReplicationScheduleContract& Contract, double Δt) noexcept;
[[nodiscard]] bool                        QueryPublishDue(const ReplicationScheduleContract& Contract, uint32_t CurrentRevision) noexcept;
void                                      AcknowledgePublishedRevision(ReplicationScheduleContract& Contract, uint32_t PublishedRevision) noexcept;
[[nodiscard]] std::string_view            InspectDeliveryChannelTitle(DeliveryChannel Channel) noexcept;

} // namespace Frontier::NetworkingSpace
