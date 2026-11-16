/*============================================================================================================================================
                                                            LOOPBACKTRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Deterministic SplitMix64 intra-application packet switch and zero-socket LoopbackTransport implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 LOOPBACK PACKET SWITCH
//------------------------------------------------------------------------------------------------------------------------

void LoopbackPacketSwitch::ConfigureImpairment(const LoopbackImpairmentProfile& Profile) noexcept
{
    Impairment            = Profile;
    RandomGeneratorCursor = (Profile.DeterministicSeed != 0u)
                                ? Profile.DeterministicSeed
                                : 0x9E3779B97F4A7C15ull;
}

void LoopbackPacketSwitch::AdvanceSwitchClock(double Δt) noexcept
{
    if (Δt > 0.0)
    {
        CurrentClockSeconds += Δt;
    }
}

void LoopbackPacketSwitch::ResetSwitch() noexcept
{
    InFlightQueue.clear();
    CurrentClockSeconds   = 0.0;
    DroppedDatagramCount  = 0u;
    RoutedDatagramCount   = 0u;
    RandomGeneratorCursor = (Impairment.DeterministicSeed != 0u)
                                ? Impairment.DeterministicSeed
                                : 0x9E3779B97F4A7C15ull;
}

uint64_t LoopbackPacketSwitch::SampleDeterministicRandom64() noexcept
{
    RandomGeneratorCursor += 0x9E3779B97F4A7C15ull;
    uint64_t Mixed         = RandomGeneratorCursor;
    Mixed                  = (Mixed ^ (Mixed >> 30u)) * 0xBF58476D1CE4E5B9ull;
    Mixed                  = (Mixed ^ (Mixed >> 27u)) * 0x94D049BB133111EBull;
    return Mixed ^ (Mixed >> 31u);
}

bool LoopbackPacketSwitch::EnqueueFlightDatagram(
    uint32_t                 SourceEndpointIdentifier,
    uint32_t                 TargetEndpointIdentifier,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    if (SourceEndpointIdentifier == 0u || TargetEndpointIdentifier == 0u || WireBytes.empty())
    {
        return false;
    }

    // Apply deterministic loss only to UnreliableSequenced datagrams; reliable channels simulate retransmission completion
    if (Channel == DeliveryChannel::UnreliableSequenced && Impairment.LossPermille > 0u)
    {
        const uint64_t RollPermille = SampleDeterministicRandom64() % 1000ull;
        if (RollPermille < static_cast<uint64_t>(Impairment.LossPermille))
        {
            ++DroppedDatagramCount;
            return true;
        }
    }

    double JitterOffsetSeconds = 0.0;
    if (Impairment.MaxJitterSeconds > 0.0)
    {
        const double UnitFraction = static_cast<double>(SampleDeterministicRandom64() & 0xFFFFFFull) / static_cast<double>(0x1000000ull);
        JitterOffsetSeconds       = UnitFraction * Impairment.MaxJitterSeconds;
    }

    InFlightEnvelope Envelope{};
    Envelope.SourceEndpointIdentifier = SourceEndpointIdentifier;
    Envelope.TargetEndpointIdentifier = TargetEndpointIdentifier;
    Envelope.Channel                  = Channel;
    Envelope.DeliveryTimestampSeconds = CurrentClockSeconds + Impairment.NominalOneWayLatencySeconds + JitterOffsetSeconds;
    Envelope.WireBytes.assign(WireBytes.begin(), WireBytes.end());

    InFlightQueue.push_back(std::move(Envelope));
    ++RoutedDatagramCount;
    return true;
}

bool LoopbackPacketSwitch::PopArrivedDatagram(uint32_t ReceiverEndpointIdentifier, PacketDatagram& OutDatagram)
{
    std::size_t BestIndex      = InFlightQueue.size();
    double      EarliestMoment = 0.0;

    for (std::size_t Index = 0u; Index < InFlightQueue.size(); ++Index)
    {
        const InFlightEnvelope& Candidate = InFlightQueue[Index];
        if (Candidate.TargetEndpointIdentifier == ReceiverEndpointIdentifier &&
            Candidate.DeliveryTimestampSeconds <= CurrentClockSeconds + 1.0e-9)
        {
            if (BestIndex == InFlightQueue.size() || Candidate.DeliveryTimestampSeconds < EarliestMoment)
            {
                BestIndex      = Index;
                EarliestMoment = Candidate.DeliveryTimestampSeconds;
            }
        }
    }

    if (BestIndex == InFlightQueue.size())
    {
        return false;
    }

    OutDatagram.PeerEndpointIdentifier  = InFlightQueue[BestIndex].SourceEndpointIdentifier;
    OutDatagram.Channel                 = InFlightQueue[BestIndex].Channel;
    OutDatagram.ArrivalTimestampSeconds = InFlightQueue[BestIndex].DeliveryTimestampSeconds;
    OutDatagram.WireBytes               = std::move(InFlightQueue[BestIndex].WireBytes);
    InFlightQueue.erase(InFlightQueue.begin() + static_cast<std::ptrdiff_t>(BestIndex));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LOOPBACK TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

LoopbackTransport::LoopbackTransport(LoopbackPacketSwitch* SharedSwitch, uint32_t LocalEndpointIdentifier) noexcept
    : PacketSwitch(SharedSwitch)
    , LocalEndpoint(LocalEndpointIdentifier != 0u ? LocalEndpointIdentifier : 1u)
{
}

void LoopbackTransport::AttachPacketSwitch(LoopbackPacketSwitch* SharedSwitch, uint32_t LocalEndpointIdentifier) noexcept
{
    PacketSwitch  = SharedSwitch;
    LocalEndpoint = (LocalEndpointIdentifier != 0u) ? LocalEndpointIdentifier : 1u;
}

bool LoopbackTransport::OpenChannel() noexcept
{
    if (PacketSwitch == nullptr)
    {
        return false;
    }
    ChannelOpen = true;
    return true;
}

void LoopbackTransport::CloseChannel() noexcept
{
    ChannelOpen = false;
}

void LoopbackTransport::AdvanceChannel([[maybe_unused]] double Δt) noexcept
{
    // Shared switch clock is advanced once per tick by the session host or test harness
}

bool LoopbackTransport::TransmitDatagram(
    uint32_t                 TargetEndpointIdentifier,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    if (!ChannelOpen || PacketSwitch == nullptr)
    {
        return false;
    }
    return PacketSwitch->EnqueueFlightDatagram(LocalEndpoint, TargetEndpointIdentifier, Channel, WireBytes);
}

bool LoopbackTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    if (!ChannelOpen || PacketSwitch == nullptr)
    {
        return false;
    }
    return PacketSwitch->PopArrivedDatagram(LocalEndpoint, OutDatagram);
}

} // namespace Frontier::NetworkingSpace
