/*============================================================================================================================================
                                                            EPICSTUBTRANSPORT.CPP
============================================================================================================================================*/
// 🧩 Open-source zero-proprietary-SDK Epic Online Services stub enforcing scope/hex-key validation and graceful transport failover

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
    return false;
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
        RedactedDiagnostic = "eos=stub_rejected_invalid_scopes_or_storage_key";
        return false;
    }

    Credentials        = Candidate;
    CredentialsValid   = true;
    RedactedDiagnostic = RedactCredentialText(
        "eos=stub_validated; product=" + Credentials.ProductIdentifier +
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
    // Open only when an explicit simulated bridge switch is attached for deterministic failover validation
    if (!CredentialsValid || SimulatedBridge == nullptr)
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
}

bool EpicOnlineTransport::TransmitDatagram(
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

bool EpicOnlineTransport::ReceiveDatagram(PacketDatagram& OutDatagram)
{
    if (!ChannelOpen || SimulatedBridge == nullptr)
    {
        return false;
    }
    return SimulatedBridge->PopArrivedDatagram(LocalEndpoint, OutDatagram);
}

} // namespace Frontier::NetworkingSpace
