/*============================================================================================================================================
                                                          SESSIONLIFECYCLEPOLICY.CPP
============================================================================================================================================*/
// 🧩 Strict Lobby vs. Match-Session separation, synthetic participant filtering, and reverse-order teardown transitions

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/NetworkingSpace/Policies/SessionLifecyclePolicy.h"

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            LOBBY & MATCH-SESSION TRANSITIONS
//------------------------------------------------------------------------------------------------------------------------

bool OpenDiscoveryLobby(SessionLifecycleLedger& Ledger) noexcept
{
    if (Ledger.LobbyPhase != LobbyLifecyclePhase::Unjoined)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.LobbyPhase = LobbyLifecyclePhase::LobbyJoined;
    return true;
}

bool AllocateMatchSession(SessionLifecycleLedger& Ledger) noexcept
{
    if (Ledger.MatchPhase != MatchSessionPhase::Absent)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.MatchPhase                  = MatchSessionPhase::Created;
    Ledger.ClientPolicyUnregisteredRun = false;
    Ledger.RegisteredRealPeerCount     = 0u;
    Ledger.SyntheticFillerCount        = 0u;
    return true;
}

std::vector<uint32_t> FilterBackendRegistrablePeers(
    SessionLifecycleLedger&                      Ledger,
    std::span<const SessionParticipantCandidate> Candidates,
    bool                                         ClientPolicyUnregisteredMode)
{
    std::vector<uint32_t> BackendPeerIdentifiers;
    if (Ledger.MatchPhase != MatchSessionPhase::Created)
    {
        ++Ledger.ProtocolViolationCount;
        return BackendPeerIdentifiers;
    }

    uint32_t SyntheticCount = 0u;
    for (const SessionParticipantCandidate& Candidate : Candidates)
    {
        // 📝 Synthetic readiness fillers never cross into backend RegisterPlayers / UnregisterPlayers calls
        if (Candidate.Origin == SessionParticipantOrigin::SyntheticReadinessFiller)
        {
            ++SyntheticCount;
            continue;
        }
        if (Candidate.EndpointIdentifier != 0u && Candidate.ProductAccountHash != 0u)
        {
            BackendPeerIdentifiers.push_back(Candidate.EndpointIdentifier);
        }
    }

    Ledger.SyntheticFillerCount        = SyntheticCount;
    Ledger.ClientPolicyUnregisteredRun = ClientPolicyUnregisteredMode;

    if (ClientPolicyUnregisteredMode)
    {
        BackendPeerIdentifiers.clear();
        Ledger.RegisteredRealPeerCount = 0u;
    }
    else
    {
        Ledger.RegisteredRealPeerCount = static_cast<uint32_t>(BackendPeerIdentifiers.size());
    }

    Ledger.MatchPhase = MatchSessionPhase::ParticipantsBound;
    return BackendPeerIdentifiers;
}

bool BeginActiveMatchSession(SessionLifecycleLedger& Ledger) noexcept
{
    if (Ledger.MatchPhase != MatchSessionPhase::ParticipantsBound)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.MatchPhase = MatchSessionPhase::ActiveInProgress;
    return true;
}

bool ConcludeActiveMatchSession(SessionLifecycleLedger& Ledger) noexcept
{
    if (Ledger.MatchPhase != MatchSessionPhase::ActiveInProgress)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.MatchPhase = MatchSessionPhase::EndedAwaitingUnbind;
    return true;
}

bool UnbindRegisteredRealPeers(SessionLifecycleLedger& Ledger) noexcept
{
    if (Ledger.MatchPhase != MatchSessionPhase::EndedAwaitingUnbind)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.RegisteredRealPeerCount = 0u;
    Ledger.MatchPhase              = MatchSessionPhase::UnboundAwaitingDrop;
    return true;
}

bool ReleaseMatchSession(SessionLifecycleLedger& Ledger) noexcept
{
    // 🔴 Reject DestroySession if EndSession -> UnregisterPlayers has not completed (unless session was only Created)
    if (Ledger.MatchPhase != MatchSessionPhase::UnboundAwaitingDrop &&
        Ledger.MatchPhase != MatchSessionPhase::Created)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.MatchPhase                  = MatchSessionPhase::Absent;
    Ledger.ClientPolicyUnregisteredRun = false;
    Ledger.RegisteredRealPeerCount     = 0u;
    Ledger.SyntheticFillerCount        = 0u;
    return true;
}

bool DepartDiscoveryLobby(SessionLifecycleLedger& Ledger) noexcept
{
    // 🔴 Lobby departure is barred while a backend Match-Session is still alive
    if (Ledger.LobbyPhase != LobbyLifecyclePhase::LobbyJoined ||
        Ledger.MatchPhase != MatchSessionPhase::Absent)
    {
        ++Ledger.ProtocolViolationCount;
        return false;
    }
    Ledger.LobbyPhase = LobbyLifecyclePhase::Unjoined;
    return true;
}

std::string_view InspectMatchSessionPhaseTitle(MatchSessionPhase Phase) noexcept
{
    switch (Phase)
    {
    case MatchSessionPhase::Absent:              return "Absent";
    case MatchSessionPhase::Created:             return "Created";
    case MatchSessionPhase::ParticipantsBound:   return "ParticipantsBound";
    case MatchSessionPhase::ActiveInProgress:    return "ActiveInProgress";
    case MatchSessionPhase::EndedAwaitingUnbind: return "EndedAwaitingUnbind";
    case MatchSessionPhase::UnboundAwaitingDrop: return "UnboundAwaitingDrop";
    }
    return "Absent";
}

} // namespace Frontier::NetworkingSpace
