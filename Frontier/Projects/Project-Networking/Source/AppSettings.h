#pragma once
// Persisted app preferences: UI language, Epic auto-login, pending token
// revocation. Lives next to the cache (per-user, no credentials), same rules
// as the portable config parser (strict keys, bounded size, no secrets).
#include "LocalConfiguration.h"
#include "Localization.h"
#include <fstream>
#include <string>
#include <string_view>

namespace Networking
{
struct AppSettings
{
    AppLanguage Language = AppLanguage::English;
    bool AutoLogin = false;
    bool ForgetPersistent = false;
    bool PremiumMultiplayer = false;
};

inline bool ParseAppSettings(std::string_view Text, AppSettings& Out) noexcept
{
    if (Text.empty() || Text.size() > 1024) return false;
    AppSettings Candidate{};
    unsigned Seen = 0;
    bool Valid = true;
    std::size_t At = 0;
    while (At <= Text.size())
    {
        std::size_t End = Text.find_first_of("\r\n", At);
        if (End == std::string_view::npos) End = Text.size();
        const std::string_view Line = Text.substr(At, End - At);
        At = End + 1;
        if (Line.empty()) continue;
        const std::size_t Eq = Line.find('=');
        if (Eq == std::string_view::npos || Eq == 0) { Valid = false; break; }
        const std::string_view Key = Line.substr(0, Eq);
        const std::string_view Value = Line.substr(Eq + 1);
        if (Key == "version")
        {
            if ((Seen & 1u) || Value != "1") { Valid = false; break; }
            Seen |= 1u;
        }
        else if (Key == "language")
        {
            if (Seen & 2u) { Valid = false; break; }
            Seen |= 2u;
            if (Value == "en") Candidate.Language = AppLanguage::English;
            else if (Value == "zh") Candidate.Language = AppLanguage::ChineseSimplified;
            else { Valid = false; break; }
        }
        else if (Key == "auto_login" || Key == "forget_persistent" || Key == "premium_multiplayer")
        {
            const unsigned Bit = (Key == "auto_login") ? 4u : (Key == "forget_persistent" ? 8u : 16u);
            if (Seen & Bit) { Valid = false; break; }
            Seen |= Bit;
            if (Value != "0" && Value != "1") { Valid = false; break; }
            if (Bit == 4u) Candidate.AutoLogin = (Value == "1");
            else if (Bit == 8u) Candidate.ForgetPersistent = (Value == "1");
            else Candidate.PremiumMultiplayer = (Value == "1");
        }
        else { Valid = false; break; }
    }
    if (!Valid || (Seen & 7u) != 7u) return false;
    Out = Candidate;
    return true;
}

inline std::string EncodeAppSettings(const AppSettings& In)
{
    std::string Out;
    Out.reserve(48);
    Out += "version=1\nlanguage=";
    Out += (In.Language == AppLanguage::ChineseSimplified) ? "zh" : "en";
    Out += "\nauto_login=";
    Out += In.AutoLogin ? "1" : "0";
    Out += "\nforget_persistent=";
    Out += In.ForgetPersistent ? "1" : "0";
    Out += "\npremium_multiplayer=";
    Out += In.PremiumMultiplayer ? "1" : "0";
    Out += "\n";
    return Out;
}

inline std::filesystem::path AppSettingsPath() { return UserCacheRoot() / "app_settings.ini"; }

inline bool LoadAppSettings(AppSettings& Out) noexcept
{
    Out = AppSettings{};
    std::error_code Error;
    const std::filesystem::path Path = AppSettingsPath();
    if (!std::filesystem::exists(Path, Error) || Error) return true;
    std::ifstream File(Path, std::ios::binary);
    if (!File) return false;
    std::string Text((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    if (!File.eof() && File.fail()) return false;
    return ParseAppSettings(Text, Out);
}

inline bool SaveAppSettings(const AppSettings& In) noexcept
{
    std::error_code Error;
    std::filesystem::create_directories(AppSettingsPath().parent_path(), Error);
    if (Error) return false;
    std::ofstream File(AppSettingsPath(), std::ios::binary | std::ios::trunc);
    if (!File) return false;
    File << EncodeAppSettings(In);
    File.flush();
    return static_cast<bool>(File);
}
}
