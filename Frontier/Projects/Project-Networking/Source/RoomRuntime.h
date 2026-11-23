#pragma once
#include "EpicExchange.h"
#include "LobbyState.h"
#include <eos_sdk.h>
#include <filesystem>
namespace Networking
{
void BindRoomRuntime(EOS_HPlatform Platform, EOS_ProductUserId User, DiagnosticReception Log,
    const std::filesystem::path& CacheRoot, bool CloudEnabled);
void TickRoomRuntime();
void SetRoomDisplayName(const char* Name);
void DetachRoomRuntime();
void BindHistory(EOS_HPlatform Platform, EOS_ProductUserId User, DiagnosticReception Log,
    const std::filesystem::path& CacheRoot, bool Enabled);
void TickHistory();
void RecordHistory(const char* Kind, const RoomReading& Room, std::int64_t Duration = 0);
void DetachHistory();
}
