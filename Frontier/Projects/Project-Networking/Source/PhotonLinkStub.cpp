//============================================================================================================================================
//                                                         PHOTONLINKSTUB.CPP
//============================================================================================================================================
// 📦 Stand-in Photon facade while the official /MD x64 libs are pending. Linked INSTEAD of
// PhotonTransport.cpp (never together). Reports honestly that Photon is unavailable so the router
// keeps players on the EOS path. Deleted from the link the moment the real libs land.

#include "PhotonTransport.h"
#include <cstdio>

namespace Networking
{
namespace
{
DiagnosticReception Reception = nullptr;
}

bool PhotonLinkAvailable() noexcept { return false; }

bool StartPhotonTransport(const PhotonCredentials& /*Credentials*/, DiagnosticReception ActiveReception) noexcept
{
    Reception = ActiveReception;
    if (Reception)
        Reception("photon=unavailable link_pending=1; official Photon SDK libs are not linked yet");
    return false;
}

void TickPhotonTransport() noexcept {}

void StopPhotonTransport() noexcept { Reception = nullptr; }

bool SendPhotonPacket(const std::uint8_t* /*Bytes*/, std::size_t /*Length*/, bool /*Reliable*/) noexcept
{
    return false;
}

PhotonStatus InspectPhotonStatus() noexcept
{
    PhotonStatus Status;
    Status.State = PhotonState::Unlinked;
    std::snprintf(Status.Reading, sizeof(Status.Reading), "Photon: SDK libs not linked yet");
    return Status;
}
}
