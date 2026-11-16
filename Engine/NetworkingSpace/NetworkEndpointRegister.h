/*============================================================================================================================================
                                                          NETWORKENDPOINTREGISTER.H
============================================================================================================================================*/
// 🧩 Peer endpoint roster, RFC 6298 EWMA RTT/jitter/clock-offset telemetry, sliding 32-bit ACK tracking, and synthetic slot isolation

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EndpointParticipation : uint8_t
{
    HostAuthority       = 0, // [-] - Session host / simulation authority
    ClientParticipant   = 1, // [-] - Connected remote or local client peer
    RelayWitness        = 2, // [-] - Non-simulating encrypted relay witness
    ObserverParticipant = 3, // [-] - Read-only spectator / telemetry viewer
};

enum class EndpointAttributeMask : uint32_t
{
    None                         = 0u,      // [mask] - Zero attributes active
    EnableReadyStatus            = 1u << 0, // [mask] - Peer marked ready in lobby
    EnableAuthenticatedIdentity  = 1u << 1, // [mask] - Peer verified via platform/JWT identity
    EnableEncryptedLink          = 1u << 2, // [mask] - ChaCha20-Poly1305 or DTLS link active
    EnableRelayedRoute           = 1u << 3, // [mask] - Routed via cloud/sovereign relay
    EnableSyntheticReadinessOnly = 1u << 4, // [mask] - Local synthetic readiness filler (never sent to backend)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EndpointTelemetryRecord
{
    uint32_t              EndpointIdentifier      = 0u;                                       // [-]  - Non-zero endpoint slot id
    uint64_t              ProductAccountHash      = 0u;                                       // [-]  - Non-secret account fingerprint
    std::string           DisplayAlias            {};                                         // [-]  - Sanitized display label
    EndpointParticipation Participation           = EndpointParticipation::ClientParticipant; // [-]  - Participation classification
    uint32_t              AttributeMask           = 0u;                                       // [mask] - EndpointAttributeMask bits
    float                 SmoothedRttMilliseconds = 0.0f;                                     // [ms] - RFC 6298 EWMA smoothed RTT
    float                 RttJitterMilliseconds   = 0.0f;                                     // [ms] - EWMA RTT mean deviation
    float                 ClockOffsetMilliseconds = 0.0f;                                     // [ms] - Estimated remote-minus-local clock
    bool                  RttInitialized          = false;                                    // [-]  - True after first RTT sample
    uint32_t              OutboundSequenceCounter = 0u;                                       // [-]  - Next sequence to stamp outbound
    uint32_t              InboundHighestSequence  = 0u;                                       // [-]  - Highest sequence received from peer
    uint32_t              InboundAckBitfield      = 0u;                                       // [mask] - 32 prior sequence presence bits
    bool                  InboundSequenceSeeded   = false;                                    // [-]  - True after first inbound packet
    uint64_t              PacketsSentCount        = 0u;                                       // [count] - Total outbound datagrams
    uint64_t              PacketsReceivedCount    = 0u;                                       // [count] - Total accepted inbound datagrams
    uint64_t              PacketsLostCount        = 0u;                                       // [count] - Detected unacknowledged gaps
    double                SecondsSinceLastInbound = 0.0;                                      // [s]  - Silence timer for timeout policy
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class NetworkEndpointRegister final
{
public:
    NetworkEndpointRegister() = default;

    [[nodiscard]] uint32_t RegisterRealEndpoint(
        uint64_t              ProductAccountHash,
        std::string_view      DisplayAlias,
        EndpointParticipation Participation,
        uint32_t              InitialAttributeMask       = 0u,
        uint32_t              ExplicitEndpointIdentifier = 0u);

    [[nodiscard]] uint32_t RegisterSyntheticReadinessEndpoint(std::string_view DisplayAlias);

    [[nodiscard]] bool     UnregisterEndpoint(uint32_t EndpointIdentifier) noexcept;
    void                   ClearAllEndpoints() noexcept;

    [[nodiscard]] bool     AssignEndpointAttributes(uint32_t EndpointIdentifier, uint32_t MaskToEnable, uint32_t MaskToClear = 0u) noexcept;

    [[nodiscard]] uint32_t IssueOutboundSequence(uint32_t EndpointIdentifier, uint32_t& OutAckLatest, uint32_t& OutAckBitfield) noexcept;
    [[nodiscard]] bool     RecordInboundSequence(uint32_t EndpointIdentifier, uint32_t InboundSequence, uint32_t RemoteAckLatest, uint32_t RemoteAckBitfield) noexcept;

    [[nodiscard]] bool     RecordRoundTripSample(
        uint32_t EndpointIdentifier,
        float    SampleRttMilliseconds,
        float    EstimatedClockOffsetMilliseconds = 0.0f) noexcept;

    void                   AdvanceSilenceTimers(double Δt) noexcept;

    [[nodiscard]] const EndpointTelemetryRecord* FindEndpoint(uint32_t EndpointIdentifier) const noexcept;
    [[nodiscard]] std::span<const EndpointTelemetryRecord> InspectAllEndpoints() const noexcept { return Records; }

    [[nodiscard]] uint32_t QueryRealEndpointCount() const noexcept;
    [[nodiscard]] uint32_t QuerySyntheticEndpointCount() const noexcept;
    [[nodiscard]] std::vector<SessionParticipantCandidate> CollectSessionCandidates() const;

private:
    [[nodiscard]] EndpointTelemetryRecord* FindMutableEndpoint(uint32_t EndpointIdentifier) noexcept;

    std::vector<EndpointTelemetryRecord> Records                {};
    uint32_t                             NextEndpointIdentifier = 1u;
};

} // namespace Frontier::NetworkingSpace
