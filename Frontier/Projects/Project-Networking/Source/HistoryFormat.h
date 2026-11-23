#pragma once
#include "LobbyState.h"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <string_view>

namespace Networking
{
inline bool ValidHistoryId(std::string_view Id)
{
    return Id.size() == 32 && std::all_of(Id.begin(), Id.end(), [](char C) {
        return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'); });
}
inline bool SafeRecordIdentifier(std::string_view Id)
{
    return Id.size() <= 128 && std::all_of(Id.begin(), Id.end(), [](unsigned char C) {
        return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_'; });
}
inline std::string EncodeHistory(const HistoryRecord& R)
{
    std::ostringstream S;
    S << "charge_history=1\nid=" << R.Id << "\nkind=" << R.Kind << "\nutc=" << R.Utc
      << "\nduration=" << R.Duration << "\nreal=" << R.RealPlayers << "\ndummy=" << R.DummyPlayers
      << "\nlobby=" << R.Lobby << "\nsession=" << R.Session << '\n';
    return S.str();
}
inline bool DecodeHistory(std::string_view Text, HistoryRecord& Output)
{
    if (Text.size() > 4096) return false;
    HistoryRecord R;
    unsigned Fields = 0;
    while (!Text.empty())
    {
        auto End = Text.find('\n');
        auto Line = Text.substr(0, End);
        Text = End == Text.npos ? std::string_view{} : Text.substr(End + 1);
        auto Equal = Line.find('=');
        if (Equal == Line.npos) return false;
        auto K = Line.substr(0, Equal), V = Line.substr(Equal + 1);
        unsigned Bit = 0;
        const auto Number = [V](auto& N) { auto P = std::from_chars(V.data(), V.data() + V.size(), N);
            return P.ec == std::errc{} && P.ptr == V.data() + V.size(); };
        if (K == "charge_history") { Bit = 1; if (V != "1") return false; }
        else if (K == "id") { Bit = 2; R.Id = V; }
        else if (K == "kind") { Bit = 4; R.Kind = V; }
        else if (K == "utc") { Bit = 8; if (!Number(R.Utc)) return false; }
        else if (K == "duration") { Bit = 16; if (!Number(R.Duration)) return false; }
        else if (K == "real") { Bit = 32; if (!Number(R.RealPlayers)) return false; }
        else if (K == "dummy") { Bit = 64; if (!Number(R.DummyPlayers)) return false; }
        else if (K == "lobby") { Bit = 128; R.Lobby = V; }
        else if (K == "session") { Bit = 256; R.Session = V; }
        else return false;
        if (Fields & Bit) return false;
        Fields |= Bit;
    }
    if (Fields != 511 || !ValidHistoryId(R.Id) || !SafeRecordIdentifier(R.Lobby) || !SafeRecordIdentifier(R.Session) ||
        (R.Kind != "login" && R.Kind != "session" && R.Kind != "completed" && R.Kind != "abandoned") ||
        R.Utc < 0 || R.Utc > 4102444800LL || R.Duration < 0 || R.Duration > 604800 ||
        R.RealPlayers > 8 || R.DummyPlayers > 3) return false;
    Output = R;
    return true;
}
inline void MergeHistory(HistoryReading& H, const HistoryRecord& R)
{
    for (const auto& Existing : H.Records) if (Existing.Id == R.Id) return;
    H.Records.push_back(R);
    std::sort(H.Records.begin(), H.Records.end(), [](const auto& A, const auto& B) { return A.Utc > B.Utc; });
    if (H.Records.size() > 64) H.Records.resize(64);
}
}
