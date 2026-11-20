/*============================================================================================================================================
                                                             TRANSPORTINTERFACE.H
============================================================================================================================================*/
// 🧩 Zero-vtable C++20 PacketTransportChannel concept and canonical wire datagram carrier across all four Frontier transports

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"

#include <concepts>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PacketDatagram
{
    uint32_t             PeerEndpointIdentifier  = 0u;                                  // [-] - Source or destination endpoint id
    DeliveryChannel      Channel                 = DeliveryChannel::UnreliableSequenced;// [-] - Reliability channel
    double               ArrivalTimestampSeconds = 0.0;                                 // [s] - Monotonic local arrival timestamp
    std::vector<uint8_t> WireBytes               {};                                    // [-] - Raw 'FP1' framed wire datagram
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template<typename ChannelCandidate>
concept PacketTransportChannel =
    requires(
        ChannelCandidate&          MutableChannel,
        const ChannelCandidate&    ConstChannel,
        uint32_t                   TargetEndpointIdentifier,
        DeliveryChannel            Channel,
        std::span<const uint8_t>   WireBytes,
        PacketDatagram&            OutDatagram,
        double                     Δt)
    {
        { MutableChannel.OpenChannel() } noexcept -> std::same_as<bool>;
        { MutableChannel.CloseChannel() } noexcept -> std::same_as<void>;
        { MutableChannel.AdvanceChannel(Δt) } noexcept -> std::same_as<void>;
        { MutableChannel.TransmitDatagram(TargetEndpointIdentifier, Channel, WireBytes) } -> std::same_as<bool>;
        { MutableChannel.ReceiveDatagram(OutDatagram) } -> std::same_as<bool>;
        { ConstChannel.QueryChannelCategory() } noexcept -> std::same_as<TransportChannelCategory>;
        { ConstChannel.QueryChannelOperational() } noexcept -> std::same_as<bool>;
    };

} // namespace Frontier::NetworkingSpace
