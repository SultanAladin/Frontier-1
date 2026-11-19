/*============================================================================================================================================
                                                            NETWORKINGEXTENSION.H
============================================================================================================================================*/
// 🧩 SuspendableParticipant networking orchestrator unifying transports, endpoint telemetry, policies, framing, and chunk reassembly

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/NetworkCodec.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/NetworkingSpace/DifferentialWorldEncoder.h"
#include "Engine/NetworkingSpace/NetworkEndpointRegister.h"
#include "Engine/NetworkingSpace/PacketSerializer.h"
#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"
#include "Engine/NetworkingSpace/Policies/ReplicationPolicy.h"
#include "Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"
#include "Engine/NetworkingSpace/RemoteDirectiveRouter.h"
#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/NetworkingSpace/Transports/PhotonRealtimeTransport.h"
#include "Engine/NetworkingSpace/Transports/SocketTransport.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct InboundEnvelopeEntry
{
    uint32_t              SourceEndpointIdentifier = 0u;                                  // [-] - Verified remote sender slot id
    double                ArrivalTimestampSeconds  = 0.0;                                 // [s] - Local arrival timestamp
    DecodedWireDatagram   Datagram                 {};                                    // [-] - Verified 'FP1' + 24B envelope + payload
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class NetworkingExtension final
{
public:
    NetworkingExtension() = default;
    ~NetworkingExtension() noexcept { Finalize(); }

    NetworkingExtension(const NetworkingExtension&)            = delete;
    NetworkingExtension& operator=(const NetworkingExtension&) = delete;

    // Lifecycle::SuspendableParticipant contract
    [[nodiscard]] bool Initialize() noexcept;
    void               Tick(double Δt) noexcept;
    void               Suspend() noexcept;
    void               Resume() noexcept;
    void               Finalize() noexcept;

    // Configuration & transport routing
    void               ConfigureNetworkSpace(const Codecs::NetworkSpaceConfiguration& Configuration, uint32_t LocalEndpointIdentifier = 1u);
    void               AttachSharedLoopbackSwitch(LoopbackPacketSwitch* SharedSwitch, uint32_t LocalEndpointIdentifier) noexcept;

    [[nodiscard]] TransportChannelCategory ActivatePreferredOrFallbackTransport() noexcept;
    [[nodiscard]] TransportChannelCategory TriggerTransportFailover() noexcept;

    // Packet transmission & reception
    [[nodiscard]] bool SendEnvelopePayload(
        uint32_t                 TargetEndpointIdentifier,
        PacketMessageCategory    Category,
        DeliveryChannel          Channel,
        uint32_t                 SimulationTick,
        std::span<const uint8_t> InnerPayload);

    [[nodiscard]] bool SendFragmentedTransfer(
        uint32_t                 TargetEndpointIdentifier,
        uint32_t                 TransferIdentifier,
        uint32_t                 SimulationTick,
        std::span<const uint8_t> LargePayload);

    void               DrainReceivedEnvelopes(std::vector<InboundEnvelopeEntry>& OutEnvelopes);
    void               DrainCompletedChunkTransfers(std::vector<CompletedChunkTransfer>& OutTransfers);

    // Graceful reverse-order session teardown routine
    [[nodiscard]] bool ExecuteGracefulSessionTeardown() noexcept;

    // Sub-system accessors
    [[nodiscard]] LoopbackTransport&                       AccessLoopbackTransport() noexcept { return LoopbackChannel; }
    [[nodiscard]] SocketTransport&                         AccessSocketTransport() noexcept { return SocketChannel; }
    [[nodiscard]] EpicOnlineTransport&                     AccessEpicTransport() noexcept { return EpicChannel; }
    [[nodiscard]] PhotonRealtimeTransport&                 AccessPhotonTransport() noexcept { return PhotonChannel; }

    [[nodiscard]] NetworkEndpointRegister&                 AccessEndpointRegister() noexcept { return EndpointRegister; }
    [[nodiscard]] const NetworkEndpointRegister&           InspectEndpointRegister() const noexcept { return EndpointRegister; }

    [[nodiscard]] SessionLifecycleLedger&                  AccessSessionLedger() noexcept { return SessionLedger; }
    [[nodiscard]] const SessionLifecycleLedger&            InspectSessionLedger() const noexcept { return SessionLedger; }

    [[nodiscard]] ContinuanceConsentGate&                  AccessContinuanceGate() noexcept { return ConsentGate; }
    [[nodiscard]] const ContinuanceConsentGate&            InspectContinuanceGate() const noexcept { return ConsentGate; }

    [[nodiscard]] SpatialInterpolationWindow&              AccessInterpolationWindow() noexcept { return InterpolationWindow; }
    [[nodiscard]] InputDirectiveStream&                    AccessInputStream() noexcept { return LocalInputStream; }
    [[nodiscard]] ReliableDirectiveRouter&                 AccessReliableRouter() noexcept { return ReliableRouter; }

    [[nodiscard]] TransportChannelCategory                 QueryActiveTransportCategory() const noexcept { return ActiveCategory; }
    [[nodiscard]] bool                                     QueryExtensionSuspended() const noexcept { return ExtensionSuspended; }
    [[nodiscard]] double                                   QueryElapsedRuntimeSeconds() const noexcept { return ElapsedRuntimeSeconds; }

private:
    [[nodiscard]] uint8_t ComputeTransportAvailabilityMask() const noexcept;
    [[nodiscard]] bool    OpenConcreteTransport(TransportChannelCategory Category) noexcept;
    void                  CloseActiveTransport() noexcept;
    [[nodiscard]] bool    TransmitRawOnActive(uint32_t TargetEndpoint, DeliveryChannel Channel, std::span<const uint8_t> WireBytes);
    [[nodiscard]] bool    ReceiveRawFromActive(PacketDatagram& OutDatagram);

    Codecs::NetworkSpaceConfiguration      ActiveConfiguration    {};
    LoopbackPacketSwitch                   OwnedFallbackSwitch    {};
    LoopbackTransport                      LoopbackChannel        {};
    SocketTransport                        SocketChannel          {};
    EpicOnlineTransport                    EpicChannel            {};
    PhotonRealtimeTransport                PhotonChannel          {};

    NetworkEndpointRegister                EndpointRegister       {};
    SessionLifecycleLedger                 SessionLedger          {};
    ContinuanceConsentGate                 ConsentGate            {};
    ChunkReassemblyRegister                ReassemblyRegister     {};
    SpatialInterpolationWindow             InterpolationWindow    {};
    InputDirectiveStream                   LocalInputStream       {};
    ReliableDirectiveRouter                ReliableRouter         {};

    std::vector<InboundEnvelopeEntry>     InboundEnvelopeQueue   {};
    std::vector<CompletedChunkTransfer>    CompletedChunkQueue    {};

    TransportChannelCategory               ActiveCategory           = TransportChannelCategory::None;
    uint8_t                                FailedTransportBitmask   = 0u;
    uint32_t                               LocalEndpointId          = 1u;
    uint32_t                               UnregisteredOutSequence  = 0u;
    double                                 ElapsedRuntimeSeconds    = 0.0;
    bool                                   ExtensionInitialized   = false;
    bool                                   ExtensionSuspended     = false;
};

static_assert(Lifecycle::SuspendableParticipant<NetworkingExtension>, "NetworkingExtension must satisfy Lifecycle::SuspendableParticipant");

} // namespace Frontier::NetworkingSpace
