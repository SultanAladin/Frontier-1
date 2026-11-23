//============================================================================================================================================
//                                                          PHOTONTRANSPORT.H
//============================================================================================================================================
// 📦 Photon Realtime transport facade. Implemented twice: PhotonTransport.cpp links the real
// Photon Realtime Core C++ SDK (needs the official /MD x64 libs), PhotonLinkStub.cpp keeps the
// build green until those libs land. Only one implementation is ever linked. No Photon headers
// here, so dependents and unit checks stay light. Tokens are accepted but never logged.

#pragma once
#include "EpicExchange.h"
#include <cstddef>
#include <cstdint>

namespace Networking
{
enum class PhotonState
{
    Unlinked,    // Official SDK libs not linked yet (stub implementation).
    Idle,        // Linked, but no session requested.
    Connecting,  // Cloud handshake / Epic custom authentication in flight.
    Joining,     // Authenticated; room join-or-create in flight.
    Joined,      // In a room; replication packets may flow.
    Failed,      // Last attempt failed; see Reading for the redacted cause.
    Disconnected,// Was joined, then left the room or lost the connection.
};

struct PhotonCredentials
{
    const char* AppId = nullptr;          // Null selects the built-in Realtime AppId.
    const char* AppVersion = nullptr;     // Null selects the built-in dev version tag.
    const char* IdTokenJwt = nullptr;     // EOS Auth ID token (Photon `token` parameter).
    const char* OwnershipTokenOrNull = nullptr; // Ecom ownership token, or null while dormant.
    const char* RoomName = nullptr;       // Null selects the built-in dev room.
};

struct PhotonStatus
{
    PhotonState State = PhotonState::Idle;
    char Reading[256]{};
    int LastDisconnectCause = 0;
    unsigned PacketsSent = 0;
    unsigned PacketsReceived = 0;
    bool FirstPacketSent = false;
};

// True only when the real Photon implementation was linked (official libs present).
bool PhotonLinkAvailable() noexcept;
bool StartPhotonTransport(const PhotonCredentials& Credentials, DiagnosticReception Reception) noexcept;
void TickPhotonTransport() noexcept;
void StopPhotonTransport() noexcept;
// Sends one codec packet. Only valid while Joined; returns false otherwise.
bool SendPhotonPacket(const std::uint8_t* Bytes, std::size_t Length, bool Reliable) noexcept;
PhotonStatus InspectPhotonStatus() noexcept;
}
