/*============================================================================================================================================
                                                            REPLICATIONPOLICY.CPP
============================================================================================================================================*/
// 🧩 Standard kinematic and collaborative workspace replication contracts plus frequency-gated publication checks

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              CONTRACT BUILDERS & SCHEDULING
//------------------------------------------------------------------------------------------------------------------------

ReplicationScheduleContract BuildRealtimeKinematicContract(uint32_t ContractIdentifier, float PublishFrequencyHz) noexcept
{
    ReplicationScheduleContract Contract{};
    Contract.ContractIdentifier         = ContractIdentifier;
    Contract.Authority                  = AuthorityMode::ClientPredictedHostReconciled;
    Contract.Channel                    = DeliveryChannel::UnreliableSequenced;
    Contract.ComponentMask              = static_cast<uint32_t>(ReplicationComponentMask::EnableTransform) |
                                          static_cast<uint32_t>(ReplicationComponentMask::EnableLinearVelocity) |
                                          static_cast<uint32_t>(ReplicationComponentMask::EnableAngularVelocity);
    Contract.PublishFrequencyHz         = (PublishFrequencyHz > 1.0f) ? PublishFrequencyHz : 30.0f;
    Contract.SpatialDeadbandMillimeters = 1.5f;
    return Contract;
}

ReplicationScheduleContract BuildCollaborativeRevisionContract(uint32_t ContractIdentifier, float PublishFrequencyHz) noexcept
{
    ReplicationScheduleContract Contract{};
    Contract.ContractIdentifier         = ContractIdentifier;
    Contract.Authority                  = AuthorityMode::PeerAuthoritative;
    Contract.Channel                    = DeliveryChannel::ReliableOrdered;
    Contract.ComponentMask              = static_cast<uint32_t>(ReplicationComponentMask::EnableWorkspaceRevision);
    Contract.PublishFrequencyHz         = (PublishFrequencyHz > 1.0f) ? PublishFrequencyHz : 20.0f;
    Contract.SpatialDeadbandMillimeters = 0.0f;
    return Contract;
}

void AccumulateReplicationElapsed(ReplicationScheduleContract& Contract, double Δt) noexcept
{
    if (Δt > 0.0)
    {
        Contract.SecondsSinceLastPublish += Δt;
    }
}

bool QueryPublishDue(const ReplicationScheduleContract& Contract, uint32_t CurrentRevision) noexcept
{
    if (CurrentRevision <= Contract.LastPublishedRevision)
    {
        return false;
    }
    if (Contract.PublishFrequencyHz <= 0.0f)
    {
        return false;
    }
    const double MinimumPeriodSeconds = 1.0 / static_cast<double>(Contract.PublishFrequencyHz);
    return (Contract.LastPublishedRevision == 0u) || (Contract.SecondsSinceLastPublish + 1.0e-7 >= MinimumPeriodSeconds);
}

void AcknowledgePublishedRevision(ReplicationScheduleContract& Contract, uint32_t PublishedRevision) noexcept
{
    if (PublishedRevision > Contract.LastPublishedRevision)
    {
        Contract.LastPublishedRevision = PublishedRevision;
    }
    Contract.SecondsSinceLastPublish = 0.0;
}

std::string_view InspectDeliveryChannelTitle(DeliveryChannel Channel) noexcept
{
    switch (Channel)
    {
    case DeliveryChannel::UnreliableSequenced: return "UnreliableSequenced";
    case DeliveryChannel::ReliableUnordered:   return "ReliableUnordered";
    case DeliveryChannel::ReliableOrdered:     return "ReliableOrdered";
    }
    return "UnreliableSequenced";
}

} // namespace Frontier::NetworkingSpace
