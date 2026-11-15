/*============================================================================================================================================
                                                           SESSIONLIFECYCLEPOLICY.H
============================================================================================================================================*/
// 🧩 Strict Lobby vs. Match-Session separation, synthetic participant isolation, and mandatory reverse-order teardown enforcement

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Frontier::NetworkingSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LobbyLifecyclePhase : uint8_t
{
    Unjoined     = 0, // [-] - Not participating in any discovery/readiness lobby
    LobbyJoined  = 1, // [-] - Lobby attributes, ready Roster, and chat/signaling active
    LobbyLeaving = 2, // [-] - Leave/Destroy issued; awaiting completion
};

enum class MatchSessionPhase : uint8_t
{
    Absent              = 0, // [-] - No backend match session created
    Created             = 1, // [-] - Match session allocated; ready to register real participants
    ParticipantsBound   = 2, // [-] - Real participants registered (or skipped under client-policy fallback)
    ActiveInProgress    = 3, // [-] - Match session started; authoritative simulation ticking
    EndedAwaitingUnbind = 4, // [-] - EndSession completed; real participants must be unregistered next
    UnboundAwaitingDrop = 5, // [-] - UnregisterPlayers completed; DestroySession permitted next
};

enum class SessionParticipantOrigin : uint8_t
{
    RealNetworkPeer          = 0, // [-] - Authenticated remote or local network endpoint
    SyntheticReadinessFiller = 1, // [-] - Local synthetic slot filler; strictly barred from backend session APIs
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SessionParticipantCandidate
{
    uint32_t                 EndpointIdentifier = 0u;                                             // [-] - Local endpoint slot id
    uint64_t                 ProductAccountHash = 0u;                                             // [-] - Non-zero backend account hash
    SessionParticipantOrigin Origin             = SessionParticipantOrigin::RealNetworkPeer;      // [-] - Real vs. synthetic origin
    bool                     MarkedReady        = false;                                          // [-] - Lobby readiness status
};

struct SessionLifecycleLedger
{
    LobbyLifecyclePhase LobbyPhase                  = LobbyLifecyclePhase::Unjoined; // [-] - Independent lobby phase
    MatchSessionPhase   MatchPhase                  = MatchSessionPhase::Absent;     // [-] - Independent match-session phase
    bool                ClientPolicyUnregisteredRun = false;                         // [-] - True when RegisterPlayers skipped by policy
    uint32_t            RegisteredRealPeerCount     = 0u;                            // [count] - Real peers registered with backend
    uint32_t            SyntheticFillerCount        = 0u;                            // [count] - Synthetic peers kept purely local
    uint32_t            ProtocolViolationCount      = 0u;                            // [count] - Rejected out-of-order transitions
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool                  OpenDiscoveryLobby(SessionLifecycleLedger& Ledger) noexcept;
[[nodiscard]] bool                  AllocateMatchSession(SessionLifecycleLedger& Ledger) noexcept;

[[nodiscard]] std::vector<uint32_t> FilterBackendRegistrablePeers(
    SessionLifecycleLedger&                        Ledger,
    std::span<const SessionParticipantCandidate>   Candidates,
    bool                                           ClientPolicyUnregisteredMode);

[[nodiscard]] bool                  BeginActiveMatchSession(SessionLifecycleLedger& Ledger) noexcept;

// Mandatory reverse-order teardown sequence:
//   1. ConcludeActiveMatchSession   (EndSession)
//   2. UnbindRegisteredRealPeers    (UnregisterPlayers — automatic no-op transition if ClientPolicyUnregisteredRun)
//   3. ReleaseMatchSession          (DestroySession)
//   4. DepartDiscoveryLobby         (LeaveLobby / DestroyLobby)
[[nodiscard]] bool                  ConcludeActiveMatchSession(SessionLifecycleLedger& Ledger) noexcept;
[[nodiscard]] bool                  UnbindRegisteredRealPeers(SessionLifecycleLedger& Ledger) noexcept;
[[nodiscard]] bool                  ReleaseMatchSession(SessionLifecycleLedger& Ledger) noexcept;
[[nodiscard]] bool                  DepartDiscoveryLobby(SessionLifecycleLedger& Ledger) noexcept;

[[nodiscard]] std::string_view      InspectMatchSessionPhaseTitle(MatchSessionPhase Phase) noexcept;

} // namespace Frontier::NetworkingSpace
