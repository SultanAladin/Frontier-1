/*============================================================================================================================================
                                                         NETWORKENDPOINTREGISTER.CPP
============================================================================================================================================*/
// 🧩 Peer endpoint registration, RFC 6298 RTT/jitter EWMA smoothing, modular 32-bit ACK windowing, and synthetic filler isolation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/NetworkEndpointRegister.h"

#include <algorithm>
#include <cmath>

namespace Frontier::NetworkingSpace
{

namespace
{

[[nodiscard]] constexpr bool SequenceGreaterThan(uint32_t Candidate, uint32_t Reference) noexcept
{
    return (Candidate != Reference) && (static_cast<int32_t>(Candidate - Reference) > 0);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            REGISTRATION & ATTRIBUTE MUTATION
//------------------------------------------------------------------------------------------------------------------------

uint32_t NetworkEndpointRegister::RegisterRealEndpoint(
    uint64_t              ProductAccountHash,
    std::string_view      DisplayAlias,
    EndpointParticipation Participation,
    uint32_t              InitialAttributeMask,
    uint32_t              ExplicitEndpointIdentifier)
{
    if (ProductAccountHash == 0u)
    {
        return 0u;
    }

    const uint32_t AssignedId = (ExplicitEndpointIdentifier != 0u) ? ExplicitEndpointIdentifier : NextEndpointIdentifier++;
    if (AssignedId >= NextEndpointIdentifier)
    {
        NextEndpointIdentifier = AssignedId + 1u;
    }
    if (FindEndpoint(AssignedId) != nullptr)
    {
        return 0u;
    }

    // Clear synthetic bit if accidentally passed to real endpoint registration
    const uint32_t CleanMask = InitialAttributeMask &
                               ~static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly);

    EndpointTelemetryRecord Record{};
    Record.EndpointIdentifier = AssignedId;
    Record.ProductAccountHash = ProductAccountHash;
    Record.DisplayAlias       = std::string(DisplayAlias);
    Record.Participation      = Participation;
    Record.AttributeMask      = CleanMask;
    Records.push_back(std::move(Record));
    return Records.back().EndpointIdentifier;
}

uint32_t NetworkEndpointRegister::RegisterSyntheticReadinessEndpoint(std::string_view DisplayAlias)
{
    EndpointTelemetryRecord Record{};
    Record.EndpointIdentifier = NextEndpointIdentifier++;
    Record.ProductAccountHash = 0u;
    Record.DisplayAlias       = std::string(DisplayAlias);
    Record.Participation      = EndpointParticipation::ClientParticipant;
    Record.AttributeMask      = static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly) |
                                static_cast<uint32_t>(EndpointAttributeMask::EnableReadyStatus);
    Records.push_back(std::move(Record));
    return Records.back().EndpointIdentifier;
}

bool NetworkEndpointRegister::UnregisterEndpoint(uint32_t EndpointIdentifier) noexcept
{
    for (std::size_t Index = 0u; Index < Records.size(); ++Index)
    {
        if (Records[Index].EndpointIdentifier == EndpointIdentifier)
        {
            Records.erase(Records.begin() + static_cast<std::ptrdiff_t>(Index));
            return true;
        }
    }
    return false;
}

void NetworkEndpointRegister::ClearAllEndpoints() noexcept
{
    Records.clear();
    NextEndpointIdentifier = 1u;
}

bool NetworkEndpointRegister::AssignEndpointAttributes(
    uint32_t EndpointIdentifier,
    uint32_t MaskToEnable,
    uint32_t MaskToClear) noexcept
{
    EndpointTelemetryRecord* Target = FindMutableEndpoint(EndpointIdentifier);
    if (Target == nullptr)
    {
        return false;
    }

    // Preserve synthetic classification immutably after creation
    const uint32_t SyntheticBit = static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly);
    const uint32_t PreservedSyn = Target->AttributeMask & SyntheticBit;

    Target->AttributeMask |= (MaskToEnable & ~SyntheticBit);
    Target->AttributeMask &= ~(MaskToClear & ~SyntheticBit);
    Target->AttributeMask  = (Target->AttributeMask & ~SyntheticBit) | PreservedSyn;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SEQUENCE / ACK & RTT TELEMETRY
//------------------------------------------------------------------------------------------------------------------------

uint32_t NetworkEndpointRegister::IssueOutboundSequence(
    uint32_t  EndpointIdentifier,
    uint32_t& OutAckLatest,
    uint32_t& OutAckBitfield) noexcept
{
    EndpointTelemetryRecord* Target = FindMutableEndpoint(EndpointIdentifier);
    if (Target == nullptr)
    {
        OutAckLatest   = 0u;
        OutAckBitfield = 0u;
        return 0u;
    }

    ++Target->OutboundSequenceCounter;
    ++Target->PacketsSentCount;
    OutAckLatest   = Target->InboundHighestSequence;
    OutAckBitfield = Target->InboundAckBitfield;
    return Target->OutboundSequenceCounter;
}

bool NetworkEndpointRegister::RecordInboundSequence(
    uint32_t EndpointIdentifier,
    uint32_t InboundSequence,
    uint32_t RemoteAckLatest,
    uint32_t RemoteAckBitfield) noexcept
{
    EndpointTelemetryRecord* Target = FindMutableEndpoint(EndpointIdentifier);
    if (Target == nullptr || InboundSequence == 0u)
    {
        return false;
    }

    Target->SecondsSinceLastInbound = 0.0;

    if (!Target->InboundSequenceSeeded)
    {
        Target->InboundSequenceSeeded  = true;
        Target->InboundHighestSequence = InboundSequence;
        Target->InboundAckBitfield     = 0u;
        ++Target->PacketsReceivedCount;
        return true;
    }

    if (SequenceGreaterThan(InboundSequence, Target->InboundHighestSequence))
    {
        const uint32_t Shift = InboundSequence - Target->InboundHighestSequence;
        if (Shift >= 32u)
        {
            Target->InboundAckBitfield = (Shift == 32u) ? 0x80000000u : 0u;
        }
        else
        {
            Target->InboundAckBitfield = (Target->InboundAckBitfield << Shift) | (1u << (Shift - 1u));
        }
        Target->InboundHighestSequence = InboundSequence;
        ++Target->PacketsReceivedCount;
    }
    else
    {
        const uint32_t Lag = Target->InboundHighestSequence - InboundSequence;
        if (Lag == 0u || Lag > 32u)
        {
            return false; // Duplicate or outside 32-packet replay window
        }
        const uint32_t HistoryBit = (1u << (Lag - 1u));
        if ((Target->InboundAckBitfield & HistoryBit) != 0u)
        {
            return false; // Duplicate packet suppressed
        }
        Target->InboundAckBitfield |= HistoryBit;
        ++Target->PacketsReceivedCount;
    }

    // Count missing bits that just Aged past the remote 32-bit ACK window when RemoteAckLatest advances
    if (RemoteAckLatest > 33u && RemoteAckBitfield != 0xFFFFFFFFu)
    {
        const uint32_t MissingWindowBit = ((RemoteAckBitfield & 0x80000000u) == 0u) ? 1u : 0u;
        Target->PacketsLostCount += MissingWindowBit;
    }

    return true;
}

bool NetworkEndpointRegister::RecordRoundTripSample(
    uint32_t EndpointIdentifier,
    float    SampleRttMilliseconds,
    float    EstimatedClockOffsetMilliseconds) noexcept
{
    EndpointTelemetryRecord* Target = FindMutableEndpoint(EndpointIdentifier);
    if (Target == nullptr || SampleRttMilliseconds < 0.0f)
    {
        return false;
    }

    if (!Target->RttInitialized)
    {
        Target->RttInitialized          = true;
        Target->SmoothedRttMilliseconds = SampleRttMilliseconds;
        Target->RttJitterMilliseconds   = SampleRttMilliseconds * 0.25f;
        Target->ClockOffsetMilliseconds = EstimatedClockOffsetMilliseconds;
        return true;
    }

    // 💡 RFC 6298 EWMA alpha = 1/8 for smoothed RTT, beta = 1/4 for mean deviation (jitter)
    const float AbsDelta            = std::fabs(SampleRttMilliseconds - Target->SmoothedRttMilliseconds);
    Target->RttJitterMilliseconds   = 0.75f * Target->RttJitterMilliseconds + 0.25f * AbsDelta;
    Target->SmoothedRttMilliseconds = 0.875f * Target->SmoothedRttMilliseconds + 0.125f * SampleRttMilliseconds;
    Target->ClockOffsetMilliseconds = 0.875f * Target->ClockOffsetMilliseconds + 0.125f * EstimatedClockOffsetMilliseconds;
    return true;
}

void NetworkEndpointRegister::AdvanceSilenceTimers(double Δt) noexcept
{
    if (Δt <= 0.0)
    {
        return;
    }
    for (EndpointTelemetryRecord& Record : Records)
    {
        Record.SecondsSinceLastInbound += Δt;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ROSTER QUERIES
//------------------------------------------------------------------------------------------------------------------------

const EndpointTelemetryRecord* NetworkEndpointRegister::FindEndpoint(uint32_t EndpointIdentifier) const noexcept
{
    for (const EndpointTelemetryRecord& Record : Records)
    {
        if (Record.EndpointIdentifier == EndpointIdentifier)
        {
            return &Record;
        }
    }
    return nullptr;
}

EndpointTelemetryRecord* NetworkEndpointRegister::FindMutableEndpoint(uint32_t EndpointIdentifier) noexcept
{
    for (EndpointTelemetryRecord& Record : Records)
    {
        if (Record.EndpointIdentifier == EndpointIdentifier)
        {
            return &Record;
        }
    }
    return nullptr;
}

uint32_t NetworkEndpointRegister::QueryRealEndpointCount() const noexcept
{
    uint32_t Count = 0u;
    const uint32_t SyntheticBit = static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly);
    for (const EndpointTelemetryRecord& Record : Records)
    {
        if ((Record.AttributeMask & SyntheticBit) == 0u)
        {
            ++Count;
        }
    }
    return Count;
}

uint32_t NetworkEndpointRegister::QuerySyntheticEndpointCount() const noexcept
{
    uint32_t Count = 0u;
    const uint32_t SyntheticBit = static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly);
    for (const EndpointTelemetryRecord& Record : Records)
    {
        if ((Record.AttributeMask & SyntheticBit) != 0u)
        {
            ++Count;
        }
    }
    return Count;
}

std::vector<SessionParticipantCandidate> NetworkEndpointRegister::CollectSessionCandidates() const
{
    std::vector<SessionParticipantCandidate> Candidates;
    Candidates.reserve(Records.size());

    const uint32_t SyntheticBit = static_cast<uint32_t>(EndpointAttributeMask::EnableSyntheticReadinessOnly);
    const uint32_t ReadyBit     = static_cast<uint32_t>(EndpointAttributeMask::EnableReadyStatus);

    for (const EndpointTelemetryRecord& Record : Records)
    {
        SessionParticipantCandidate Candidate{};
        Candidate.EndpointIdentifier = Record.EndpointIdentifier;
        Candidate.ProductAccountHash = Record.ProductAccountHash;
        Candidate.Origin             = ((Record.AttributeMask & SyntheticBit) != 0u)
                                           ? SessionParticipantOrigin::SyntheticReadinessFiller
                                           : SessionParticipantOrigin::RealNetworkPeer;
        Candidate.MarkedReady        = ((Record.AttributeMask & ReadyBit) != 0u);
        Candidates.push_back(Candidate);
    }

    return Candidates;
}

} // namespace Frontier::NetworkingSpace
