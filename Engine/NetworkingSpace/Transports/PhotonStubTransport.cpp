/*============================================================================================================================================
                                                           PHOTONSTUBTRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Open-source zero-proprietary-SDK Photon Realtime stub enforcing Epic JWT verification, token scrubbing, and failover routing

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/PhotonRealtimeTransport.h"

namespace Frontier::NetworkingSpace
{

PhotonRealtimeTransport::~PhotonRealtimeTransport() noexcept
{
    CloseChannel();
    ScrubIdentityToken();
}

bool PhotonRealtimeTransport::QuerySdkCompiled() noexcept
{
    return false;
}

bool PhotonRealtimeTransport::ConfigureRoomAuthentication(const PhotonRoomConfiguration& Candidate)
{
    ScrubIdentityToken();
    if (Candidate.ApplicationIdentifier.empty() || !VerifyJwtTokenCharacters(Candidate.EpicIdentityJwtToken))
    {
        AuthConfigured     = false;
        RedactedDiagnostic = "photon=stub_rejected_missing_appid_or_malformed_jwt";
        return false;
    }

    Configuration      = Candidate;
    AuthConfigured     = true;
    RedactedDiagnostic = RedactCredentialText(
        "photon=stub_validated; region=" + std::string(InspectRelayClusterCode(Configuration.BoundRegionCluster)) +
        "; id_token=" + Configuration.EpicIdentityJwtToken);
    return true;
}

void PhotonRealtimeTransport::AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept
{
    SimulatedBridge = BridgeSwitch;
    LocalEndpoint   = (LocalEndpointIdentifier != 0u) ? LocalEndpointIdentifier : 1u;
}

void PhotonRealtimeTransport::ScrubIdentityToken() noexcept
{
    if (!Configuration.EpicIdentityJwtToken.empty())
    {
        ZeroSensitiveBytes(std::span<char>(Configuration.EpicIdentityJwtToken.data(), Configuration.EpicIdentityJwtToken.size()));
        Configuration.EpicIdentityJwtToken.clear();
    }
    AuthConfigured = false;
}

bool PhotonRealtimeTransport::OpenChannel() noexcept
{
    if (!AuthConfigured || SimulatedBridge == nullptr)
    {
        return false;
    }
    ChannelOpen = true;
    return true;
}

void PhotonRealtimeTransport::CloseChannel() noexcept
{
    ChannelOpen = false;
}

void PhotonRealtimeTransport::AdvanceChannel([[maybe_unused]] double Δt) noexcept
{
}

bool PhotonRealtimeTransport::TransmitDatagram(
    uint32_t                 TargetEndpointIdentifier,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    if (!ChannelOpen || SimulatedBridge == nullptr)
    {
        return false;
    }
    return SimulatedBridge->EnqueueFlightDatagram(LocalEndpoint, TargetEndpointIdentifier, Channel, WireBytes);
}

bool PhotonRealtimeTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    if (!ChannelOpen || SimulatedBridge == nullptr)
    {
        return false;
    }
    return SimulatedBridge->PopArrivedDatagram(LocalEndpoint, OutDatagram);
}

} // namespace Frontier::NetworkingSpace
