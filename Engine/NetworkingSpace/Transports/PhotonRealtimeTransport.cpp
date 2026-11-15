/*============================================================================================================================================
                                                         PHOTONREALTIMETRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Proprietary Photon Realtime C++ SDK adapter compilation unit (compiled only when FRONTIER_ENABLE_PHOTON_SDK=ON)

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
    return true;
}

bool PhotonRealtimeTransport::ConfigureRoomAuthentication(const PhotonRoomConfiguration& Candidate)
{
    ScrubIdentityToken();
    if (Candidate.ApplicationIdentifier.empty() || !VerifyJwtTokenCharacters(Candidate.EpicIdentityJwtToken))
    {
        AuthConfigured     = false;
        RedactedDiagnostic = "photon=rejected_missing_appid_or_malformed_jwt";
        return false;
    }

    Configuration      = Candidate;
    AuthConfigured     = true;
    RedactedDiagnostic = RedactCredentialText(
        "photon=sdk_configured; region=" + std::string(InspectRelayClusterCode(Configuration.BoundRegionCluster)) +
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
    if (!AuthConfigured)
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
    // Photon LoadBalancingClient tick integration point when linked against Photon /MD libraries
}

bool PhotonRealtimeTransport::TransmitDatagram(
    uint32_t                 TargetEndpointIdentifier,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    if (!ChannelOpen)
    {
        return false;
    }
    if (SimulatedBridge != nullptr)
    {
        return SimulatedBridge->EnqueueFlightDatagram(LocalEndpoint, TargetEndpointIdentifier, Channel, WireBytes);
    }
    return false;
}

bool PhotonRealtimeTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    if (!ChannelOpen)
    {
        return false;
    }
    if (SimulatedBridge != nullptr)
    {
        return SimulatedBridge->PopArrivedDatagram(LocalEndpoint, OutDatagram);
    }
    return false;
}

} // namespace Frontier::NetworkingSpace
