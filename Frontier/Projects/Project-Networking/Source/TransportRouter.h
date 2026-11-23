//============================================================================================================================================
//                                                          TRANSPORTTROUTER.H
//============================================================================================================================================
// 📦 Picks the multiplayer transport after EOS login: premium access routes to Photon, everyone
// else stays on the EOS direct path. Photon failures fall back to EOS instead of breaking login.
// Premium is a manual toggle for now; Xsolla entitlement checks plug into ResolvePremiumAccess.

#pragma once
#include "EpicExchange.h"

namespace Networking
{
enum class TransportKind
{
    None,
    Eos,
    Photon,
};

enum class TransportPhase
{
    Idle,
    StartingOwnership,
    StartingPhoton,
    Active,
    Failed,
};

struct TransportStatus
{
    TransportKind Active = TransportKind::None;
    TransportKind Wanted = TransportKind::None;
    TransportPhase Phase = TransportPhase::Idle;
    char Reading[256]{};
};

// Pure selection rule, unit-tested without any SDK.
inline TransportKind SelectTransportKind(bool PremiumAccess, bool PhotonLinked) noexcept
{
    return (PremiumAccess && PhotonLinked) ? TransportKind::Photon : TransportKind::Eos;
}

// Manual toggle today (Setup switch + FRONTIER_PREMIUM_MULTIPLAYER env override); Xsolla later.
bool ResolvePremiumAccess() noexcept;
void SetPremiumMultiplayer(bool Enabled) noexcept;

// Called once EOS login reaches Connected; safe to call again (restarts selection).
bool StartMultiplayerTransport(DiagnosticReception Reception) noexcept;
void TickMultiplayerTransport() noexcept;
void StopMultiplayerTransport() noexcept;
TransportStatus InspectTransportStatus() noexcept;
// Short display word for the UI; the returned pointer stays valid across calls.
const char* InspectTransportDisplayName() noexcept;
}
