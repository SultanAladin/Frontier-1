#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace Networking
{
inline bool ValidStorageKey(std::string_view Value)
{
    return Value.size() == 64 && std::all_of(Value.begin(), Value.end(), [](char C) {
        return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f') || (C >= 'A' && C <= 'F'); });
}
struct PortableSettings { std::string ClientId, Secret, StorageKey; };
inline void WipeSettings(PortableSettings& Settings)
{
    for (auto* Text : {&Settings.Secret, &Settings.StorageKey})
    {
        volatile char* P = Text->empty() ? nullptr : Text->data();
        for (size_t I = 0; I < Text->size(); ++I) P[I] = 0;
        Text->clear();
    }
}
inline bool ParsePortableSettings(std::string_view Text, PortableSettings& Output)
{
    if (Text.size() > 2048) return false;
    PortableSettings Candidate;
    unsigned Seen = 0;
    bool Valid = true;
    while (!Text.empty())
    {
        const auto End = Text.find('\n'); auto Line = Text.substr(0, End);
        Text = End == Text.npos ? std::string_view{} : Text.substr(End + 1);
        if (!Line.empty() && Line.back() == '\r') Line.remove_suffix(1);
        if (Line.empty() || Line.front() == '#') continue;
        const auto E = Line.find('='); if (E == Line.npos) { Valid = false; break; }
        auto K = Line.substr(0, E), V = Line.substr(E + 1);
        unsigned Bit = 0;
        if (K == "version") { Bit = 1; if (V != "1") Valid = false; }
        else if (K == "client_id") { Bit = 2; Candidate.ClientId = V; }
        else if (K == "client_secret") { Bit = 4; Candidate.Secret = V; }
        else if (K == "storage_key") { Bit = 8; Candidate.StorageKey = V; }
        else Valid = false;
        if (Seen & Bit) Valid = false;
        Seen |= Bit;
        if (!Valid) break;
    }
    const auto Credential = [](const std::string& V) { return !V.empty() && V.size() <= 64 &&
        std::all_of(V.begin(), V.end(), [](unsigned char C) { return C > 32 && C < 127; }); };
    Valid = Valid && Seen == 15 && Credential(Candidate.Secret) && Credential(Candidate.ClientId) && ValidStorageKey(Candidate.StorageKey);
    if (Valid) Output = Candidate;
    WipeSettings(Candidate); return Valid;
}
inline std::filesystem::path ExecutableDirectory()
{
#if defined(_WIN32)
    wchar_t Buffer[32768]{};
    const DWORD Count = GetModuleFileNameW(nullptr, Buffer, 32768);
    if (Count > 0 && Count < 32768) return std::filesystem::path(Buffer).parent_path();
#endif
    return std::filesystem::current_path();
}
inline std::filesystem::path UserCacheRoot()
{
#if defined(_WIN32)
    wchar_t Buffer[32768]{};
    const DWORD Count = GetEnvironmentVariableW(L"LOCALAPPDATA", Buffer, 32768);
    if (Count > 0 && Count < 32768) return std::filesystem::path(Buffer) / "FrontierCharge" / "dev-v1";
#else
    if (const char* Home = std::getenv("HOME")) return std::filesystem::path(Home) / ".local/share/FrontierCharge/dev-v1";
#endif
    return std::filesystem::temp_directory_path() / "FrontierCharge" / "dev-v1";
}
inline std::string PathUtf8(const std::filesystem::path& Path)
{
    const auto Text = Path.u8string(); return std::string(reinterpret_cast<const char*>(Text.data()), Text.size());
}
inline bool ReadPortableSettings(PortableSettings& Output)
{
    const auto Directory = ExecutableDirectory();
    for (const auto& File : {Directory.parent_path() / "Charge.local.ini", Directory / "Charge.local.ini"})
    {
        std::error_code Error;
        const auto Bytes = std::filesystem::file_size(File, Error);
        if (Error) continue;
        if (Bytes > 2048) return false;
        std::ifstream Input(File, std::ios::binary);
        std::string Contents(static_cast<size_t>(Bytes), '\0');
        Input.read(Contents.data(), static_cast<std::streamsize>(Contents.size()));
        const bool Valid = Input.good() && ParsePortableSettings(Contents, Output);
        volatile char* P = Contents.empty() ? nullptr : Contents.data();
        for (size_t I = 0; I < Contents.size(); ++I) P[I] = 0;
        return Valid;
    }
    return false;
}
}
