/*============================================================================================================================================
                                                             LOOPBACKTRANSPORT.H
============================================================================================================================================*/
// 🧩 Deterministic intra-application packet switch with SplitMix64 latency, jitter, reordering, and loss simulation for offline/CI runs

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/TransportInterface.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct LoopbackImpairmentProfile
{
    double   NominalOneWayLatencySeconds = 0.0;            // [s]      - Constant propagation delay (e.g. 0.056 s Frankfurt->Cairo)
    double   MaxJitterSeconds         = 0.0;               // [s]      - Uniform [0, MaxJitterSeconds] random delay
    uint32_t LossPermille             = 0u;                // [0..1000]- Unreliable packet drop rate in parts per thousand
    uint64_t DeterministicSeed        = 0x9E3779B97F4A7C15ull; // [-]  - Deterministic SplitMix64 PRNG seed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class LoopbackPacketSwitch final
{
public:
    LoopbackPacketSwitch() = default;

    void               ConfigureImpairment(const LoopbackImpairmentProfile& Profile) noexcept;
    void               AdvanceSwitchClock(double Δt) noexcept;
    void               ResetSwitch() noexcept;

    [[nodiscard]] bool EnqueueFlightDatagram(
        uint32_t                 SourceEndpointIdentifier,
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);

    [[nodiscard]] bool PopArrivedDatagram(uint32_t ReceiverEndpointIdentifier, PacketDatagram& OutDatagram);

    [[nodiscard]] double   QuerySwitchClockSeconds() const noexcept { return CurrentClockSeconds; }
    [[nodiscard]] uint64_t QueryDroppedDatagramCount() const noexcept { return DroppedDatagramCount; }
    [[nodiscard]] uint64_t QueryRoutedDatagramCount() const noexcept { return RoutedDatagramCount; }

private:
    struct InFlightEnvelope
    {
        uint32_t             SourceEndpointIdentifier = 0u;
        uint32_t             TargetEndpointIdentifier = 0u;
        DeliveryChannel      Channel                  = DeliveryChannel::UnreliableSequenced;
        double               DeliveryTimestampSeconds = 0.0;
        std::vector<uint8_t> WireBytes                {};
    };

    [[nodiscard]] uint64_t SampleDeterministicRandom64() noexcept;

    LoopbackImpairmentProfile       Impairment           {};
    uint64_t                        RandomGeneratorCursor= 0x9E3779B97F4A7C15ull;
    double                          CurrentClockSeconds  = 0.0;
    uint64_t                        DroppedDatagramCount = 0u;
    uint64_t                        RoutedDatagramCount  = 0u;
    std::vector<InFlightEnvelope>   InFlightQueue        {};
};

class LoopbackTransport final
{
public:
    LoopbackTransport() = default;
    LoopbackTransport(LoopbackPacketSwitch* SharedSwitch, uint32_t LocalEndpointIdentifier) noexcept;

    void               AttachPacketSwitch(LoopbackPacketSwitch* SharedSwitch, uint32_t LocalEndpointIdentifier) noexcept;

    [[nodiscard]] bool OpenChannel() noexcept;
    void               CloseChannel() noexcept;
    void               AdvanceChannel(double Δt) noexcept;

    [[nodiscard]] bool TransmitDatagram(
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);

    [[nodiscard]] bool ReceiveDatagram(PacketDatagram& OutDatagram);

    [[nodiscard]] TransportChannelCategory QueryChannelCategory() const noexcept { return TransportChannelCategory::Loopback; }
    [[nodiscard]] bool                     QueryChannelOperational() const noexcept { return ChannelOpen && (PacketSwitch != nullptr); }
    [[nodiscard]] bool                     QueryPacketSwitchAttached() const noexcept { return PacketSwitch != nullptr; }
    [[nodiscard]] uint32_t                 QueryLocalEndpointIdentifier() const noexcept { return LocalEndpoint; }

private:
    LoopbackPacketSwitch* PacketSwitch  = nullptr;
    uint32_t              LocalEndpoint = 1u;
    bool                  ChannelOpen   = false;
};

static_assert(PacketTransportChannel<LoopbackTransport>, "LoopbackTransport must satisfy PacketTransportChannel");

} // namespace Frontier::NetworkingSpace
