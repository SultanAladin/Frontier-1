/*============================================================================================================================================
                                                          PHOTONREALTIMETRANSPORT.H
============================================================================================================================================*/
// 🧩 Photon Realtime Core cloud room transport with Epic JWT custom-authentication hygiene, relay region assignment, and open-source stub

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"
#include "Engine/NetworkingSpace/Policies/TransportSelectionPolicy.h"
#include "Engine/NetworkingSpace/Transports/LoopbackTransport.h"
#include "Engine/NetworkingSpace/Transports/TransportInterface.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PhotonRoomConfiguration
{
    std::string        ApplicationIdentifier {};                                       // [-] - Non-secret Photon AppId GUID
    std::string        EpicIdentityJwtToken  {};                                       // [-] - Short-lived Epic JWT (scrubbed on close)
    RelayRegionCluster BoundRegionCluster    = RelayRegionCluster::EuCentralFrankfurt; // [-] - Minimax selected global relay cluster
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class PhotonRealtimeTransport final
{
public:
    PhotonRealtimeTransport() = default;
    ~PhotonRealtimeTransport() noexcept;

    PhotonRealtimeTransport(const PhotonRealtimeTransport&)            = delete;
    PhotonRealtimeTransport& operator=(const PhotonRealtimeTransport&) = delete;
    PhotonRealtimeTransport(PhotonRealtimeTransport&&) noexcept        = default;
    PhotonRealtimeTransport& operator=(PhotonRealtimeTransport&&) noexcept = default;

    [[nodiscard]] bool ConfigureRoomAuthentication(const PhotonRoomConfiguration& Configuration);
    void               AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept;
    void               ScrubIdentityToken() noexcept;

    [[nodiscard]] bool OpenChannel() noexcept;
    void               CloseChannel() noexcept;
    void               AdvanceChannel(double Δt) noexcept;

    [[nodiscard]] bool TransmitDatagram(
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);

    [[nodiscard]] bool ReceiveDatagram(PacketDatagram& OutDatagram);

    [[nodiscard]] TransportChannelCategory QueryChannelCategory() const noexcept { return TransportChannelCategory::PhotonRealtime; }
    [[nodiscard]] bool                     QueryChannelOperational() const noexcept { return ChannelOpen; }
    [[nodiscard]] bool                     QueryChannelReady() const noexcept { return AuthConfigured && (QuerySdkCompiled() || SimulatedBridge != nullptr); }
    [[nodiscard]] RelayRegionCluster       QueryBoundRegionCluster() const noexcept { return Configuration.BoundRegionCluster; }
    [[nodiscard]] static bool              QuerySdkCompiled() noexcept;
    [[nodiscard]] std::string_view         InspectRedactedDiagnostic() const noexcept { return RedactedDiagnostic; }

private:
    PhotonRoomConfiguration Configuration      {};
    LoopbackPacketSwitch*   SimulatedBridge    = nullptr;
    uint32_t                LocalEndpoint      = 1u;
    bool                    AuthConfigured     = false;
    bool                    ChannelOpen        = false;
    std::string             RedactedDiagnostic = "photon=unconfigured";
};

static_assert(PacketTransportChannel<PhotonRealtimeTransport>, "PhotonRealtimeTransport must satisfy PacketTransportChannel");

} // namespace Frontier::NetworkingSpace
