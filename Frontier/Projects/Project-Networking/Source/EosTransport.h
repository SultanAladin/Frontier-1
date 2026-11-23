//============================================================================================================================================
//                                                            EOSTRANSPORT.H
//============================================================================================================================================
// 📦 EOS direct-path transport placeholder. Lobby, voice and match sessions already run over EOS;
// packet relay over EOS stays unimplemented until the engine defines what rides it. Selecting this
// path always succeeds and reports honestly that no relay channel exists yet.

#pragma once
#include "EpicExchange.h"

namespace Networking
{
struct EosStatus
{
    bool Active = false;
    char Reading[256]{};
};

bool StartEosTransport(DiagnosticReception Reception) noexcept;
void TickEosTransport() noexcept;
void StopEosTransport() noexcept;
// Always false: the EOS packet relay is not implemented in this slice.
bool SendEosPacket(const unsigned char* Bytes, unsigned Length, bool Reliable) noexcept;
EosStatus InspectEosStatus() noexcept;
}
