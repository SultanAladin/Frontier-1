/*============================================================================================================================================
                                                           EPICONLINETRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Proprietary Epic Online Services SDK adapter compilation unit (compiled only when FRONTIER_ENABLE_EOS_SDK=ON)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Transports/EpicOnlineTransport.h"

namespace Frontier::NetworkingSpace
{

EpicOnlineTransport::~EpicOnlineTransport() noexcept
{
    CloseChannel();
    ScrubSensitiveCredentials();
}

bool EpicOnlineTransport::QuerySdkCompiled() noexcept
{
    return true;
}

bool EpicOnlineTransport::ConfigureCredentials(const EpicSessionCredentials& Candidate)
{
    ScrubSensitiveCredentials();
    if (Candidate.ProductIdentifier.empty() ||
        Candidate.SandboxIdentifier.empty() ||
        Candidate.DeploymentIdentifier.empty() ||
        !VerifyMandatoryIdentityScopes(Candidate.RequestedScopeMask) ||
        (!Candidate.EncryptionHexKey.empty() && !VerifyHexStorageKeyFormat(Candidate.EncryptionHexKey)))
    {
        CredentialsValid   = false;
        RedactedDiagnostic = "eos=rejected_invalid_scopes_or_storage_key";
        return false;
    }

    Credentials        = Candidate;
    CredentialsValid   = true;
    RedactedDiagnostic = RedactCredentialText(
        "eos=sdk_configured; product=" + Credentials.ProductIdentifier +
        "; storage_key=" + Credentials.EncryptionHexKey);
    return true;
}

void EpicOnlineTransport::AttachSimulatedBridgeSwitch(LoopbackPacketSwitch* BridgeSwitch, uint32_t LocalEndpointIdentifier) noexcept
{
    SimulatedBridge = BridgeSwitch;
    LocalEndpoint   = (LocalEndpointIdentifier != 0u) ? LocalEndpointIdentifier : 1u;
}

void EpicOnlineTransport::ScrubSensitiveCredentials() noexcept
{
    if (!Credentials.EncryptionHexKey.empty())
    {
        ZeroSensitiveBytes(std::span<char>(Credentials.EncryptionHexKey.data(), Credentials.EncryptionHexKey.size()));
        Credentials.EncryptionHexKey.clear();
    }
    CredentialsValid = false;
}

bool EpicOnlineTransport::OpenChannel() noexcept
{
    if (!CredentialsValid)
    {
        return false;
    }
    ChannelOpen = true;
    return true;
}

void EpicOnlineTransport::CloseChannel() noexcept
{
    ChannelOpen = false;
}

void EpicOnlineTransport::AdvanceChannel([[maybe_unused]] double Δt) noexcept
{
    // EOS_Platform_Tick integration point when linked against EOSSDK
}

bool EpicOnlineTransport::TransmitDatagram(
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

bool EpicOnlineTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
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
