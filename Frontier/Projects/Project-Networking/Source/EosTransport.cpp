//============================================================================================================================================
//                                                            EOSTRANSPORT.CPP
//============================================================================================================================================
// 📦 EOS direct-path transport placeholder implementation. Lobby, voice and match sessions already
// run over EOS; packet relay over EOS stays unimplemented until the engine defines its payloads.

#include "EosTransport.h"
#include <cstdio>

namespace Networking
{
namespace
{
DiagnosticReception Reception = nullptr;
bool Active = false;
}

bool StartEosTransport(DiagnosticReception ActiveReception) noexcept
{
    Reception = ActiveReception;
    Active = true;
    if (Reception)
        Reception("transport=eos lobby_and_voice_only relay=not_implemented");
    return true;
}

void TickEosTransport() noexcept {}

void StopEosTransport() noexcept
{
    Active = false;
    Reception = nullptr;
}

bool SendEosPacket(const unsigned char* /*Bytes*/, unsigned /*Length*/, bool /*Reliable*/) noexcept
{
    return false;
}

EosStatus InspectEosStatus() noexcept
{
    EosStatus Status;
    Status.Active = Active;
    std::snprintf(Status.Reading, sizeof(Status.Reading), "%s",
        Active ? "EOS: direct path (lobby and voice)" : "EOS: idle");
    return Status;
}
}
