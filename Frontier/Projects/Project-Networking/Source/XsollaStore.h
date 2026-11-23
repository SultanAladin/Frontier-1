//============================================================================================================================================
//                                                              XSOLLASTORE.H
//============================================================================================================================================
// 📦 Xsolla store client for this native C++ app. Not the unpublished C++ SDK zip and not the
// C# DLL. Shop Builder REST + Pay Station 4 URLs, catalog SKUs, and a fail-closed entitlement
// parser that will feed ResolvePremiumAccess later. No merchant API key, no vendor .lib/.dll.

#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace Networking
{
inline constexpr const char* XsollaStoreHost = "https://store.xsolla.com/api";
inline constexpr const char* XsollaPayStationSandbox = "https://sandbox-secure.xsolla.com/paystation4/";
inline constexpr const char* XsollaPayStationLive = "https://secure.xsolla.com/paystation4/";
inline constexpr unsigned XsollaMaxTokenLength = 2048;
inline constexpr unsigned XsollaMaxEntitlementLength = 512;

enum class XsollaItemKind : unsigned
{
    PremiumLifetime,
    PremiumMonthly,
    PremiumYearly,
    NitronPack,
};

struct XsollaCatalogEntry
{
    const char* Sku;
    XsollaItemKind Kind;
    unsigned NitronUnits;   // Granted Nitron; 0 unless NitronPack.
    unsigned BonusPercent;  // 0, 10, or 20 for pack tiers.
};

inline constexpr XsollaCatalogEntry XsollaCatalog[] = {
    {"premium_pass_lifetime", XsollaItemKind::PremiumLifetime, 0, 0},
    {"premium_monthly", XsollaItemKind::PremiumMonthly, 0, 0},
    {"premium_yearly", XsollaItemKind::PremiumYearly, 0, 0},
    {"nitron_stack_100", XsollaItemKind::NitronPack, 100, 0},
    {"nitron_vault_550", XsollaItemKind::NitronPack, 550, 10},
    {"nitron_reserve_1200", XsollaItemKind::NitronPack, 1200, 20},
};

inline constexpr unsigned XsollaCatalogCount =
    static_cast<unsigned>(sizeof(XsollaCatalog) / sizeof(XsollaCatalog[0]));

inline const XsollaCatalogEntry* FindXsollaSku(const char* Id) noexcept
{
    if (!Id || !*Id)
        return nullptr;
    for (const auto& Entry : XsollaCatalog)
    {
        if (std::strcmp(Entry.Sku, Id) == 0)
            return &Entry;
    }
    return nullptr;
}

inline bool XsollaSkuGrantsPremium(const XsollaCatalogEntry& Entry) noexcept
{
    return Entry.Kind == XsollaItemKind::PremiumLifetime ||
        Entry.Kind == XsollaItemKind::PremiumMonthly ||
        Entry.Kind == XsollaItemKind::PremiumYearly;
}

inline unsigned XsollaNitronBaseUnits(const XsollaCatalogEntry& Entry) noexcept
{
    if (Entry.Kind != XsollaItemKind::NitronPack || Entry.BonusPercent >= 100)
        return Entry.NitronUnits;
    return (Entry.NitronUnits * 100u) / (100u + Entry.BonusPercent);
}

inline bool XsollaTokenCharsetOk(const char* Token) noexcept
{
    if (!Token || !*Token)
        return false;
    std::size_t Length = 0;
    for (const char* Cursor = Token; *Cursor; ++Cursor, ++Length)
    {
        if (Length >= XsollaMaxTokenLength)
            return false;
        const unsigned char Character = static_cast<unsigned char>(*Cursor);
        const bool Ok = (Character >= '0' && Character <= '9') ||
            (Character >= 'A' && Character <= 'Z') ||
            (Character >= 'a' && Character <= 'z') ||
            Character == '-' || Character == '_' || Character == '.' || Character == '~';
        if (!Ok)
            return false;
    }
    return Length > 0;
}

inline bool BuildPayStationUrl(bool Sandbox, const char* Token, char* Out, std::size_t Capacity) noexcept
{
    if (Out && Capacity)
        Out[0] = 0;
    if (!Out || Capacity < 64 || !XsollaTokenCharsetOk(Token))
        return false;
    const char* Host = Sandbox ? XsollaPayStationSandbox : XsollaPayStationLive;
    const int Written = std::snprintf(Out, Capacity, "%s?token=%s", Host, Token);
    if (Written < 0 || static_cast<std::size_t>(Written) >= Capacity)
    {
        Out[0] = 0;
        return false;
    }
    return true;
}

inline bool RedactPayStationUrl(const char* Url, char* Out, std::size_t Capacity) noexcept
{
    if (!Out || Capacity < 8)
        return false;
    Out[0] = 0;
    if (!Url || !*Url)
        return false;
    const char* Marker = std::strstr(Url, "token=");
    if (!Marker)
    {
        if (std::strlen(Url) >= Capacity)
            return false;
        std::snprintf(Out, Capacity, "%s", Url);
        return true;
    }
    const std::size_t Prefix = static_cast<std::size_t>(Marker - Url) + 6;
    if (Prefix + 10 >= Capacity)
        return false;
    std::memcpy(Out, Url, Prefix);
    std::snprintf(Out + Prefix, Capacity - Prefix, "[redacted]");
    return std::strstr(Out, "token=[redacted]") != nullptr && std::strstr(Out, Marker + 6) == nullptr;
}

inline bool BuildVirtualItemsUrl(unsigned ProjectId, char* Out, std::size_t Capacity) noexcept
{
    if (Out && Capacity)
        Out[0] = 0;
    if (!Out || Capacity < 64 || ProjectId == 0)
        return false;
    const int Written = std::snprintf(Out, Capacity, "%s/v2/project/%u/items/virtual_items",
        XsollaStoreHost, ProjectId);
    if (Written < 0 || static_cast<std::size_t>(Written) >= Capacity)
    {
        Out[0] = 0;
        return false;
    }
    return true;
}

inline bool BuildOrderItemUrl(unsigned ProjectId, const char* Sku, char* Out, std::size_t Capacity) noexcept
{
    if (Out && Capacity)
        Out[0] = 0;
    if (!Out || Capacity < 64 || ProjectId == 0 || !FindXsollaSku(Sku))
        return false;
    const int Written = std::snprintf(Out, Capacity, "%s/v2/project/%u/payment/item/%s",
        XsollaStoreHost, ProjectId, Sku);
    if (Written < 0 || static_cast<std::size_t>(Written) >= Capacity)
    {
        Out[0] = 0;
        return false;
    }
    return true;
}

enum class XsollaPremiumKind : unsigned
{
    None,
    Lifetime,
    Subscription,
};

struct XsollaEntitlement
{
    XsollaPremiumKind Premium = XsollaPremiumKind::None;
    char SubExpiresUtc[32]{};
    unsigned Nitron = 0;
};

inline bool EntitlementGrantsPremium(const XsollaEntitlement& Entitlement) noexcept
{
    return Entitlement.Premium != XsollaPremiumKind::None;
}

namespace XsollaJson
{
inline std::string_view SkipSpace(std::string_view Text) noexcept
{
    std::size_t Index = 0;
    while (Index < Text.size() && (Text[Index] == ' ' || Text[Index] == '\t' ||
        Text[Index] == '\n' || Text[Index] == '\r'))
        ++Index;
    return Text.substr(Index);
}

inline bool Take(std::string_view& Text, char Character) noexcept
{
    Text = SkipSpace(Text);
    if (Text.empty() || Text[0] != Character)
        return false;
    Text = Text.substr(1);
    return true;
}

inline bool TakeString(std::string_view& Text, std::string_view& Value) noexcept
{
    Text = SkipSpace(Text);
    if (Text.empty() || Text[0] != '"')
        return false;
    std::size_t End = 1;
    while (End < Text.size() && Text[End] != '"')
    {
        if (static_cast<unsigned char>(Text[End]) < 32)
            return false;
        ++End;
    }
    if (End >= Text.size())
        return false;
    Value = Text.substr(1, End - 1);
    Text = Text.substr(End + 1);
    return true;
}

inline bool TakeUnsigned(std::string_view& Text, unsigned& Value) noexcept
{
    Text = SkipSpace(Text);
    if (Text.empty() || Text[0] < '0' || Text[0] > '9')
        return false;
    unsigned Acc = 0;
    std::size_t Index = 0;
    while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
    {
        const unsigned Digit = static_cast<unsigned>(Text[Index] - '0');
        if (Acc > (4294967295u - Digit) / 10u)
            return false;
        Acc = Acc * 10u + Digit;
        ++Index;
    }
    if (Index > 1 && Text[0] == '0')
        return false;
    Value = Acc;
    Text = Text.substr(Index);
    return true;
}
}

inline bool ParseXsollaEntitlement(std::string_view Text, XsollaEntitlement& Out) noexcept
{
    if (Text.empty() || Text.size() > XsollaMaxEntitlementLength)
        return false;
    XsollaEntitlement Candidate{};
    unsigned Seen = 0;
    std::string_view Cursor = XsollaJson::SkipSpace(Text);
    if (!XsollaJson::Take(Cursor, '{'))
        return false;
    bool First = true;
    while (true)
    {
        Cursor = XsollaJson::SkipSpace(Cursor);
        if (!Cursor.empty() && Cursor[0] == '}')
        {
            Cursor = Cursor.substr(1);
            break;
        }
        if (!First && !XsollaJson::Take(Cursor, ','))
            return false;
        First = false;
        std::string_view Key;
        if (!XsollaJson::TakeString(Cursor, Key) || !XsollaJson::Take(Cursor, ':'))
            return false;
        if (Key == "premium")
        {
            if (Seen & 1u)
                return false;
            Seen |= 1u;
            std::string_view Value;
            if (!XsollaJson::TakeString(Cursor, Value))
                return false;
            if (Value == "none")
                Candidate.Premium = XsollaPremiumKind::None;
            else if (Value == "lifetime")
                Candidate.Premium = XsollaPremiumKind::Lifetime;
            else if (Value == "subscription")
                Candidate.Premium = XsollaPremiumKind::Subscription;
            else
                return false;
        }
        else if (Key == "sub_expires_utc")
        {
            if (Seen & 2u)
                return false;
            Seen |= 2u;
            std::string_view Value;
            if (!XsollaJson::TakeString(Cursor, Value) || Value.size() >= sizeof(Candidate.SubExpiresUtc))
                return false;
            for (char Character : Value)
            {
                if (static_cast<unsigned char>(Character) < 32)
                    return false;
            }
            std::memcpy(Candidate.SubExpiresUtc, Value.data(), Value.size());
            Candidate.SubExpiresUtc[Value.size()] = 0;
        }
        else if (Key == "nitron")
        {
            if (Seen & 4u)
                return false;
            Seen |= 4u;
            if (!XsollaJson::TakeUnsigned(Cursor, Candidate.Nitron))
                return false;
        }
        else
            return false;
    }
    Cursor = XsollaJson::SkipSpace(Cursor);
    if (!Cursor.empty() || (Seen & 1u) == 0)
        return false;
    if (Candidate.Premium == XsollaPremiumKind::Subscription && !Candidate.SubExpiresUtc[0])
        return false;
    if (Candidate.Premium != XsollaPremiumKind::Subscription && Candidate.SubExpiresUtc[0])
        return false;
    Out = Candidate;
    return true;
}

struct XsollaClientConfig
{
    unsigned ProjectId = 0;
    bool Sandbox = true;
    bool MerchantSecretOnClient = false;
    bool Configured = false;
};

XsollaClientConfig InspectXsollaConfig() noexcept;
void DescribeXsollaConfig(char* Out, std::size_t Capacity) noexcept;
}
