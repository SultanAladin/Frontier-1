/*============================================================================================================================================
                                                           NETWORKINGEXTENSION.CPP
============================================================================================================================================*/
// 🧩 SuspendableParticipant networking orchestrator: deterministic failover, framing, chunk reassembly, and reverse-order teardown

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/NetworkingExtension.h"

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE PARTICIPANT HOOKS
//------------------------------------------------------------------------------------------------------------------------

bool NetworkingExtension::Initialize() noexcept
{
    if (ExtensionInitialized)
    {
        return true;
    }

    ElapsedRuntimeSeconds   = 0.0;
    FailedTransportBitmask  = 0u;
    UnregisteredOutSequence = 0u;
    ExtensionSuspended      = false;

    if (!LoopbackChannel.QueryPacketSwitchAttached())
    {
        LoopbackChannel.AttachPacketSwitch(&OwnedFallbackSwitch, LocalEndpointId);
    }
    SocketChannel.ConfigureBindPort(0u, LocalEndpointId);

    ExtensionInitialized = true;
    (void)ActivatePreferredOrFallbackTransport();
    return true;
}

void NetworkingExtension::Tick(double Δt) noexcept
{
    if (!ExtensionInitialized || ExtensionSuspended || Δt <= 0.0)
    {
        return;
    }

    ElapsedRuntimeSeconds += Δt;
    AdvanceContinuanceWindow(ConsentGate, Δt);
    EndpointRegister.AdvanceSilenceTimers(Δt);
    ReassemblyRegister.PurgeExpiredSlots(ElapsedRuntimeSeconds);

    for (ReplicationScheduleContract& Contract : ActiveConfiguration.ReplicationContracts)
    {
        AccumulateReplicationElapsed(Contract, Δt);
    }

    switch (ActiveCategory)
    {
    case TransportChannelCategory::Loopback:        LoopbackChannel.AdvanceChannel(Δt); break;
    case TransportChannelCategory::SovereignSocket: SocketChannel.AdvanceChannel(Δt);   break;
    case TransportChannelCategory::EpicOnline:      EpicChannel.AdvanceChannel(Δt);     break;
    case TransportChannelCategory::PhotonRealtime:  PhotonChannel.AdvanceChannel(Δt);   break;
    case TransportChannelCategory::None:            break;
    }

    PacketDatagram RawDatagram{};
    while (ReceiveRawFromActive(RawDatagram))
    {
        DecodedWireDatagram Decoded{};
        if (!DeserializeWireDatagram(RawDatagram.WireBytes, Decoded))
        {
            continue;
        }

        const uint32_t SenderId = Decoded.Envelope.SenderEndpointIdentifier;
        if (EndpointRegister.FindEndpoint(SenderId) != nullptr)
        {
            if (!EndpointRegister.RecordInboundSequence(
                    SenderId,
                    Decoded.Envelope.SequenceNumber,
                    Decoded.Envelope.AckLatestSequence,
                    Decoded.Envelope.AckHistoryBitfield))
            {
                continue; // Duplicate or stale replay packet dropped
            }
        }

        if (Decoded.Category == PacketMessageCategory::FragmentedChunkSlice)
        {
            CompletedChunkTransfer Reassembled{};
            bool Finished = false;
            if (ReassemblyRegister.IngestChunkPayload(
                    SenderId,
                    Decoded.Payload,
                    ElapsedRuntimeSeconds,
                    Reassembled,
                    Finished) && Finished)
            {
                CompletedChunkQueue.push_back(std::move(Reassembled));
            }
            continue;
        }

        InboundEnvelopeRecord Record{};
        Record.SourceEndpointIdentifier = SenderId;
        Record.ArrivalTimestampSeconds  = RawDatagram.ArrivalTimestampSeconds;
        Record.Datagram                 = std::move(Decoded);
        InboundEnvelopeQueue.push_back(std::move(Record));
    }
}

void NetworkingExtension::Suspend() noexcept
{
    if (!ExtensionInitialized || ExtensionSuspended)
    {
        return;
    }
    ExtensionSuspended = true;
}

void NetworkingExtension::Resume() noexcept
{
    if (!ExtensionInitialized || !ExtensionSuspended)
    {
        return;
    }
    ExtensionSuspended = false;
}

void NetworkingExtension::Finalize() noexcept
{
    if (!ExtensionInitialized)
    {
        return;
    }

    (void)ExecuteGracefulSessionTeardown();
    CloseActiveTransport();

    EpicChannel.ScrubSensitiveCredentials();
    PhotonChannel.ScrubIdentityToken();
    SocketChannel.DisableCipherProtection();

    EndpointRegister.ClearAllEndpoints();
    ResetContinuanceGate(ConsentGate);
    ReassemblyRegister.ResetAllSlots();
    InterpolationWindow.ClearHistory();
    LocalInputStream.ClearStream();
    ReliableRouter.ResetRouter();
    InboundEnvelopeQueue.clear();
    CompletedChunkQueue.clear();

    ExtensionSuspended   = false;
    ExtensionInitialized = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CONFIGURATION & TRANSPORT ROUTING
//------------------------------------------------------------------------------------------------------------------------

void NetworkingExtension::ConfigureNetworkSpace(
    const Codecs::NetworkSpaceConfiguration& Configuration,
    uint32_t                                 LocalEndpointIdentifier)
{
    ActiveConfiguration    = Configuration;
    LocalEndpointId        = (LocalEndpointIdentifier != 0u) ? LocalEndpointIdentifier : 1u;
    FailedTransportBitmask = 0u;
    SocketChannel.ConfigureBindPort(0u, LocalEndpointId);
}

void NetworkingExtension::AttachSharedLoopbackSwitch(
    LoopbackPacketSwitch* SharedSwitch,
    uint32_t              LocalEndpointIdentifier) noexcept
{
    LocalEndpointId = (LocalEndpointIdentifier != 0u) ? LocalEndpointIdentifier : 1u;
    LoopbackChannel.AttachPacketSwitch(
        SharedSwitch != nullptr ? SharedSwitch : &OwnedFallbackSwitch,
        LocalEndpointId);
}

uint8_t NetworkingExtension::ComputeTransportAvailabilityMask() const noexcept
{
    uint8_t Mask = static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableLoopback) |
                   static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableSovereignSocket);

    if (EpicOnlineTransport::QuerySdkCompiled() || EpicChannel.QueryChannelReady() || EpicChannel.QueryChannelOperational())
    {
        Mask |= static_cast<uint8_t>(TransportLinkAvailabilityMask::EnableEpicOnline);
    }
    if (PhotonRealtimeTransport::QuerySdkCompiled() || PhotonChannel.QueryChannelReady() || PhotonChannel.QueryChannelOperational())
    {
        Mask |= static_cast<uint8_t>(TransportLinkAvailabilityMask::EnablePhotonRealtime);
    }
    return Mask;
}

bool NetworkingExtension::OpenConcreteTransport(TransportChannelCategory Category) noexcept
{
    switch (Category)
    {
    case TransportChannelCategory::Loopback:        return LoopbackChannel.OpenChannel();
    case TransportChannelCategory::SovereignSocket: return SocketChannel.OpenChannel();
    case TransportChannelCategory::EpicOnline:      return EpicChannel.OpenChannel();
    case TransportChannelCategory::PhotonRealtime:  return PhotonChannel.OpenChannel();
    case TransportChannelCategory::None:            return false;
    }
    return false;
}

void NetworkingExtension::CloseActiveTransport() noexcept
{
    switch (ActiveCategory)
    {
    case TransportChannelCategory::Loopback:        LoopbackChannel.CloseChannel(); break;
    case TransportChannelCategory::SovereignSocket: SocketChannel.CloseChannel();   break;
    case TransportChannelCategory::EpicOnline:      EpicChannel.CloseChannel();     break;
    case TransportChannelCategory::PhotonRealtime:  PhotonChannel.CloseChannel();   break;
    case TransportChannelCategory::None:            break;
    }
    ActiveCategory = TransportChannelCategory::None;
}

TransportChannelCategory NetworkingExtension::ActivatePreferredOrFallbackTransport() noexcept
{
    CloseActiveTransport();

    // Attempt all four tiers until an operational channel opens cleanly
    for (uint32_t Attempt = 0u; Attempt < 4u; ++Attempt)
    {
        TransportSelectionInput SelectionInput{};
        SelectionInput.ExplicitOverride    = ActiveConfiguration.PreferredTransport;
        SelectionInput.AccessTier          = ActiveConfiguration.AccessTier;
        SelectionInput.AvailabilityMask    = ComputeTransportAvailabilityMask();
        SelectionInput.FailedTransportMask = FailedTransportBitmask;

        const TransportChannelCategory Chosen = ResolveActiveTransport(SelectionInput);
        if (Chosen == TransportChannelCategory::None)
        {
            break;
        }

        if (OpenConcreteTransport(Chosen))
        {
            ActiveCategory = Chosen;
            return ActiveCategory;
        }

        FailedTransportBitmask |= DeriveTransportBit(Chosen);
    }

    return TransportChannelCategory::None;
}

TransportChannelCategory NetworkingExtension::TriggerTransportFailover() noexcept
{
    if (ActiveCategory != TransportChannelCategory::None)
    {
        FailedTransportBitmask |= DeriveTransportBit(ActiveCategory);
    }
    return ActivatePreferredOrFallbackTransport();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TRANSMISSION, RECEPTION & TEARDOWN
//------------------------------------------------------------------------------------------------------------------------

bool NetworkingExtension::TransmitRawOnActive(
    uint32_t                 TargetEndpoint,
    DeliveryChannel          Channel,
    std::span<const uint8_t> WireBytes)
{
    switch (ActiveCategory)
    {
    case TransportChannelCategory::Loopback:        return LoopbackChannel.TransmitDatagram(TargetEndpoint, Channel, WireBytes);
    case TransportChannelCategory::SovereignSocket: return SocketChannel.TransmitDatagram(TargetEndpoint, Channel, WireBytes);
    case TransportChannelCategory::EpicOnline:      return EpicChannel.TransmitDatagram(TargetEndpoint, Channel, WireBytes);
    case TransportChannelCategory::PhotonRealtime:  return PhotonChannel.TransmitDatagram(TargetEndpoint, Channel, WireBytes);
    case TransportChannelCategory::None:            return false;
    }
    return false;
}

bool NetworkingExtension::ReceiveRawFromActive(PacketDatagram& OutDatagram)
{
    switch (ActiveCategory)
    {
    case TransportChannelCategory::Loopback:        return LoopbackChannel.ReceiveDatagram(OutDatagram);
    case TransportChannelCategory::SovereignSocket: return SocketChannel.ReceiveDatagram(OutDatagram);
    case TransportChannelCategory::EpicOnline:      return EpicChannel.ReceiveDatagram(OutDatagram);
    case TransportChannelCategory::PhotonRealtime:  return PhotonChannel.ReceiveDatagram(OutDatagram);
    case TransportChannelCategory::None:            return false;
    }
    return false;
}

bool NetworkingExtension::SendEnvelopePayload(
    uint32_t                 TargetEndpointIdentifier,
    PacketMessageCategory    Category,
    DeliveryChannel          Channel,
    uint32_t                 SimulationTick,
    std::span<const uint8_t> InnerPayload)
{
    if (ActiveCategory == TransportChannelCategory::None || ExtensionSuspended)
    {
        return false;
    }

    NetworkEnvelopeHeader Header{};
    Header.SimulationTick           = SimulationTick;
    Header.SenderEndpointIdentifier = LocalEndpointId;

    if (EndpointRegister.FindEndpoint(TargetEndpointIdentifier) != nullptr)
    {
        Header.SequenceNumber = EndpointRegister.IssueOutboundSequence(
            TargetEndpointIdentifier,
            Header.AckLatestSequence,
            Header.AckHistoryBitfield);
    }
    else
    {
        Header.SequenceNumber = ++UnregisteredOutSequence;
    }

    std::vector<uint8_t> FramedDatagram;
    if (!SerializeWireDatagram(Category, Channel, Header, InnerPayload, FramedDatagram))
    {
        return false;
    }

    return TransmitRawOnActive(TargetEndpointIdentifier, Channel, FramedDatagram);
}

bool NetworkingExtension::SendFragmentedTransfer(
    uint32_t                 TargetEndpointIdentifier,
    uint32_t                 TransferIdentifier,
    uint32_t                 SimulationTick,
    std::span<const uint8_t> LargePayload)
{
    std::vector<std::vector<uint8_t>> Chunks;
    if (!FragmentPayloadIntoChunks(TransferIdentifier, LargePayload, Chunks))
    {
        return false;
    }

    for (const std::vector<uint8_t>& ChunkSlice : Chunks)
    {
        if (!SendEnvelopePayload(
                TargetEndpointIdentifier,
                PacketMessageCategory::FragmentedChunkSlice,
                DeliveryChannel::ReliableUnordered,
                SimulationTick,
                ChunkSlice))
        {
            return false;
        }
    }
    return true;
}

void NetworkingExtension::DrainReceivedEnvelopes(std::vector<InboundEnvelopeRecord>& OutEnvelopes)
{
    OutEnvelopes = std::move(InboundEnvelopeQueue);
    InboundEnvelopeQueue.clear();
}

void NetworkingExtension::DrainCompletedChunkTransfers(std::vector<CompletedChunkTransfer>& OutTransfers)
{
    OutTransfers = std::move(CompletedChunkQueue);
    CompletedChunkQueue.clear();
}

bool NetworkingExtension::ExecuteGracefulSessionTeardown() noexcept
{
    bool Clean = true;
    if (SessionLedger.MatchPhase == MatchSessionPhase::ActiveInProgress)
    {
        Clean = ConcludeActiveMatchSession(SessionLedger) && Clean;
    }
    if (SessionLedger.MatchPhase == MatchSessionPhase::EndedAwaitingUnbind)
    {
        Clean = UnbindRegisteredRealPeers(SessionLedger) && Clean;
    }
    if (SessionLedger.MatchPhase == MatchSessionPhase::UnboundAwaitingDrop ||
        SessionLedger.MatchPhase == MatchSessionPhase::Created ||
        SessionLedger.MatchPhase == MatchSessionPhase::ParticipantsBound)
    {
        if (SessionLedger.MatchPhase == MatchSessionPhase::ParticipantsBound)
        {
            SessionLedger.MatchPhase = MatchSessionPhase::EndedAwaitingUnbind;
            Clean = UnbindRegisteredRealPeers(SessionLedger) && Clean;
        }
        Clean = ReleaseMatchSession(SessionLedger) && Clean;
    }
    if (SessionLedger.LobbyPhase == LobbyLifecyclePhase::LobbyJoined)
    {
        Clean = DepartDiscoveryLobby(SessionLedger) && Clean;
    }
    return Clean;
}

} // namespace Frontier::NetworkingSpace
