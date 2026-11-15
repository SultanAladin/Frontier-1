/*============================================================================================================================================
                                                            EPICONLINETRANSPORT.H
============================================================================================================================================*/
// 🧩 Epic Online Services P2P + Lobby/Session transport adapter with mandatory 4-scope enforcement and zero-SDK open-source stub seam

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/AuthenticationScopePolicy.h"
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

struct EpicSessionCredentials
{
    std::string ProductIdentifier   {};                                                                     // [-] - Non-secret EOS ProductId
    std::string SandboxIdentifier   {};                                                                     // [-] - Non-secret EOS SandboxId
    std::string DeploymentIdentifier{};                                                                     // [-] - Non-secret EOS DeploymentId
    std::string EncryptionHexKey    {};                                                                     // [-] - 64-char hex key (scrubbed on close)
    uint32_t    RequestedScopeMask  = static_cast<uint32_t>(IdentityScopeMask::EnableMandatoryPortalMask);  // [mask] - Must include all 4 portal scopes
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CLASSES
//------------------------------------------------------------------------------------------------------------------------

class EpicOnlineTransport final
{
public:
    EpicOnlineTransport() = default;
    ~EpicOnlineTransport() noexcept;

    EpicOnlineTransport(const EpicOnlineTransport&)            = delete;
    EpicOnlineTransport& operator=(const EpicOnlineTransport&) = delete;
    EpicOnlineTransport(EpicOnlineTransport&&) noexcept        = default;
    EpicOnlineTransport& operator=(EpicOnlineTransport&&) noexcept = default;

    [[nodiscard]] bool ConfigureCredentials(const EpicSessionCredentials& Credentials);
    void               AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept;
    void               ScrubSensitiveCredentials() noexcept;

    [[nodiscard]] bool OpenChannel() noexcept;
    void               CloseChannel() noexcept;
    void               AdvanceChannel(double Δt) noexcept;

    [[nodiscard]] bool TransmitDatagram(
        uint32_t                 TargetEndpointIdentifier,
        DeliveryChannel          Channel,
        std::span<const uint8_t> WireBytes);

    [[nodiscard]] bool ReceiveDatagram(PacketDatagram& OutDatagram);

    [[nodiscard]] TransportChannelCategory QueryChannelCategory() const noexcept { return TransportChannelCategory::EpicOnline; }
    [[nodiscard]] bool                     QueryChannelOperational() const noexcept { return ChannelOpen; }
    [[nodiscard]] bool                     QueryChannelReady() const noexcept { return CredentialsValid && (QuerySdkCompiled() || SimulatedBridge != nullptr); }
    [[nodiscard]] static bool              QuerySdkCompiled() noexcept;
    [[nodiscard]] std::string_view         InspectRedactedDiagnostic() const noexcept { return RedactedDiagnostic; }

private:
    EpicSessionCredentials Credentials        {};
    LoopbackPacketSwitch*  SimulatedBridge    = nullptr;
    uint32_t               LocalEndpoint      = 1u;
    bool                   CredentialsValid   = false;
    bool                   ChannelOpen        = false;
    std::string            RedactedDiagnostic = "eos=unconfigured";
};

static_assert(PacketTransportChannel<EpicOnlineTransport>, "EpicOnlineTransport must satisfy PacketTransportChannel");

} // namespace Frontier::NetworkingSpace
