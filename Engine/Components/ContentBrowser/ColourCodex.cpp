/*============================================================================================================================================
                                                              COLOURCODEX.CPP
============================================================================================================================================*/
// 🎨 Implementation of ColourCodex named colour aliases, spectrum categorization, contrast ink selection, and TOML serialization

#include "Engine/Components/ContentBrowser/ColourCodex.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace Frontier
{

namespace
{

[[nodiscard]] int ParseHexNibble(char Character) noexcept
{
    if (Character >= '0' && Character <= '9')
    {
        return Character - '0';
    }
    if (Character >= 'a' && Character <= 'f')
    {
        return 10 + (Character - 'a');
    }
    if (Character >= 'A' && Character <= 'F')
    {
        return 10 + (Character - 'A');
    }
    return -1;
}

[[nodiscard]] ColorRgba ParseHexToRgba(std::string_view RawHex, ColorRgba Fallback = { 59u, 31u, 58u, 255u }) noexcept
{
    while (!RawHex.empty() && std::isspace(static_cast<unsigned char>(RawHex.front())))
    {
        RawHex.remove_prefix(1u);
    }
    while (!RawHex.empty() && std::isspace(static_cast<unsigned char>(RawHex.back())))
    {
        RawHex.remove_suffix(1u);
    }
    if (!RawHex.empty() && RawHex.front() == '#')
    {
        RawHex.remove_prefix(1u);
    }
    if (RawHex.size() == 3u)
    {
        const int R = ParseHexNibble(RawHex[0]);
        const int G = ParseHexNibble(RawHex[1]);
        const int B = ParseHexNibble(RawHex[2]);
        if (R >= 0 && G >= 0 && B >= 0)
        {
            return {
                static_cast<uint8_t>(R * 17),
                static_cast<uint8_t>(G * 17),
                static_cast<uint8_t>(B * 17),
                255u,
            };
        }
        return Fallback;
    }
    if (RawHex.size() == 6u)
    {
        const int RHi = ParseHexNibble(RawHex[0]);
        const int RLo = ParseHexNibble(RawHex[1]);
        const int GHi = ParseHexNibble(RawHex[2]);
        const int GLo = ParseHexNibble(RawHex[3]);
        const int BHi = ParseHexNibble(RawHex[4]);
        const int BLo = ParseHexNibble(RawHex[5]);
        if (RHi >= 0 && RLo >= 0 && GHi >= 0 && GLo >= 0 && BHi >= 0 && BLo >= 0)
        {
            return {
                static_cast<uint8_t>((RHi << 4) | RLo),
                static_cast<uint8_t>((GHi << 4) | GLo),
                static_cast<uint8_t>((BHi << 4) | BLo),
                255u,
            };
        }
    }
    return Fallback;
}

void ComputeHslMetrics(ColorRgba Fill, float& OutHueDegrees, float& OutSaturationPercent) noexcept
{
    const float Rn    = static_cast<float>(Fill.Red) / 255.0f;
    const float Gn    = static_cast<float>(Fill.Green) / 255.0f;
    const float Bn    = static_cast<float>(Fill.Blue) / 255.0f;
    const float MaxCh = std::max({ Rn, Gn, Bn });
    const float MinCh = std::min({ Rn, Gn, Bn });
    const float Delta = MaxCh - MinCh;

    float Hue = 0.0f;
    if (Delta > 1.0e-6f)
    {
        if (MaxCh == Rn)
        {
            Hue = std::fmod((Gn - Bn) / Delta, 6.0f);
        }
        else if (MaxCh == Gn)
        {
            Hue = ((Bn - Rn) / Delta) + 2.0f;
        }
        else
        {
            Hue = ((Rn - Gn) / Delta) + 4.0f;
        }
        Hue *= 60.0f;
        if (Hue < 0.0f)
        {
            Hue += 360.0f;
        }
    }

    const float Lightness  = (MaxCh + MinCh) * 0.5f;
    const float Saturation = (Delta <= 1.0e-6f) ? 0.0f : (Delta / (1.0f - std::fabs(2.0f * Lightness - 1.0f)));
    OutHueDegrees          = Hue;
    OutSaturationPercent   = Saturation * 100.0f;
}

[[nodiscard]] std::array<uint8_t, 4> ComputeCmykPercentages(ColorRgba Fill) noexcept
{
    const float Rn = static_cast<float>(Fill.Red) / 255.0f;
    const float Gn = static_cast<float>(Fill.Green) / 255.0f;
    const float Bn = static_cast<float>(Fill.Blue) / 255.0f;
    const float K  = 1.0f - std::max({ Rn, Gn, Bn });
    if (K >= 0.99999f)
    {
        return { 0u, 0u, 0u, 100u };
    }
    const auto RoundPercent = [](float Fraction) noexcept -> uint8_t
    {
        return static_cast<uint8_t>(std::clamp(std::lround(Fraction * 100.0f), 0L, 100L));
    };
    return {
        RoundPercent((1.0f - Rn - K) / (1.0f - K)),
        RoundPercent((1.0f - Gn - K) / (1.0f - K)),
        RoundPercent((1.0f - Bn - K) / (1.0f - K)),
        RoundPercent(K),
    };
}

[[nodiscard]] std::string LowercaseAscii(std::string_view Text)
{
    std::string Lowered;
    Lowered.reserve(Text.size());
    for (const char Character : Text)
    {
        Lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Character))));
    }
    return Lowered;
}

[[nodiscard]] std::string SanitizePascalIdentifier(std::string_view Name)
{
    std::string Identifier;
    bool        CapitalizeNext = true;
    for (const char Character : Name)
    {
        if (std::isalnum(static_cast<unsigned char>(Character)))
        {
            Identifier.push_back(
                CapitalizeNext
                    ? static_cast<char>(std::toupper(static_cast<unsigned char>(Character)))
                    : Character);
            CapitalizeNext = false;
        }
        else
        {
            CapitalizeNext = true;
        }
    }
    return Identifier.empty() ? std::string("ColourAlias") : Identifier;
}

[[nodiscard]] ColourFilterGroup ParseFilterGroupByName(std::string_view Title) noexcept
{
    const std::string Lowered = LowercaseAscii(Title);
    if (Lowered.find("august") != std::string::npos || Lowered.find("beige") != std::string::npos)
    {
        return ColourFilterGroup::AugustBeigeAndBrown;
    }
    if (Lowered.find("red") != std::string::npos || Lowered.find("berr") != std::string::npos)
    {
        return ColourFilterGroup::RedsAndBerries;
    }
    if (Lowered.find("yellow") != std::string::npos)
    {
        return ColourFilterGroup::Yellows;
    }
    if (Lowered.find("green") != std::string::npos)
    {
        return ColourFilterGroup::Greens;
    }
    if (Lowered.find("blue") != std::string::npos)
    {
        return ColourFilterGroup::Blues;
    }
    if (Lowered.find("purple") != std::string::npos)
    {
        return ColourFilterGroup::Purples;
    }
    if (Lowered.find("neutral") != std::string::npos)
    {
        return ColourFilterGroup::Neutrals;
    }
    return ColourFilterGroup::All;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 COLOURCODEX METHODS
//------------------------------------------------------------------------------------------------------------------------

ColourCodex::ColourCodex()
{
    ResetToBuiltInRepository();
}

void ColourCodex::ResetToBuiltInRepository()
{
    RepositoryEntries.clear();

    struct BuiltInSeed
    {
        const char*       Name;
        const char*       Hex;
        const char*       Series;
        ColourFilterGroup Group;
    };

    constexpr std::array<BuiltInSeed, 38> BuiltInSeeds = {{
        // Reference tiles & primary named aliases
        { "Fig Purple",       "#3B1F3A", "Botanical",            ColourFilterGroup::Purples             },
        { "Pineapple Yellow", "#F6B800", "Botanical",            ColourFilterGroup::Yellows             },
        { "Charcoal",         "#2B2D2F", "Neutral",              ColourFilterGroup::Neutrals            },
        { "Midnight",         "#191970", "Deep Blue",            ColourFilterGroup::Blues               },
        { "Royal Blue",       "#2454FF", "Blue",                 ColourFilterGroup::Blues               },
        { "Arctic",           "#F8FAFC", "Neutral",              ColourFilterGroup::Neutrals            },
        { "Strawberry",       "#E63956", "Botanical",            ColourFilterGroup::RedsAndBerries      },
        { "Cherry",           "#990F2B", "Botanical",            ColourFilterGroup::RedsAndBerries      },
        { "Banana",           "#FFE135", "Botanical",            ColourFilterGroup::Yellows             },
        // "August Beige & Brown" 10-band warm spectrum ramp
        { "August Blaze",     "#FF7B00", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Tangerine", "#FF8800", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Marigold",  "#FF9500", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Honey",     "#FFA200", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Amber",     "#FFAA00", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Saffron",   "#FFB700", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Sunbeam",   "#FFC300", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Gold",      "#FFD000", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Canary",    "#FFDD00", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        { "August Lemon",     "#FFEA00", "August Beige & Brown", ColourFilterGroup::AugustBeigeAndBrown },
        // Extended botanical, mineral, and studio aliases
        { "Coral Pink",       "#FF6F7D", "Botanical",            ColourFilterGroup::RedsAndBerries      },
        { "Raspberry",        "#D72660", "Botanical",            ColourFilterGroup::RedsAndBerries      },
        { "Crimson",          "#B81D24", "Mineral",              ColourFilterGroup::RedsAndBerries      },
        { "Plum",             "#58284F", "Botanical",            ColourFilterGroup::Purples             },
        { "Lavender",         "#A78BFA", "Botanical",            ColourFilterGroup::Purples             },
        { "Tangerine",        "#F28500", "Botanical",            ColourFilterGroup::AugustBeigeAndBrown },
        { "Amber",            "#F59E0B", "Mineral",              ColourFilterGroup::AugustBeigeAndBrown },
        { "Terracotta",       "#C85A32", "Mineral",              ColourFilterGroup::AugustBeigeAndBrown },
        { "Sandstone",        "#C9B49A", "Mineral",              ColourFilterGroup::AugustBeigeAndBrown },
        { "Matcha",           "#74A12E", "Botanical",            ColourFilterGroup::Greens              },
        { "Forest Pine",      "#1B4332", "Botanical",            ColourFilterGroup::Greens              },
        { "Teal",             "#0D9488", "Mineral",              ColourFilterGroup::Greens              },
        { "Cobalt",           "#1E40AF", "Blue",                 ColourFilterGroup::Blues               },
        { "Sapphire",         "#0F52BA", "Blue",                 ColourFilterGroup::Blues               },
        { "Glacier Blue",     "#7DD3FC", "Blue",                 ColourFilterGroup::Blues               },
        { "Obsidian",         "#141518", "Neutral",              ColourFilterGroup::Neutrals            },
        { "Graphite",         "#474B52", "Neutral",              ColourFilterGroup::Neutrals            },
        { "Slate",            "#5B6770", "Neutral",              ColourFilterGroup::Neutrals            },
        { "Ivory",            "#F5F1E6", "Neutral",              ColourFilterGroup::Neutrals            },
    }};

    RepositoryEntries.reserve(BuiltInSeeds.size());
    for (const BuiltInSeed& Seed : BuiltInSeeds)
    {
        RepositoryEntries.push_back(ConstructEntryFromHex(Seed.Name, Seed.Hex, Seed.Series, Seed.Group, true));
    }
}

ColourCodexEntry ColourCodex::ConstructEntryFromHex(
    std::string_view  AliasName,
    std::string_view  HexNotation,
    std::string_view  SeriesTitle,
    ColourFilterGroup ExplicitGroup,
    bool              ReadOnlyEngineEntry)
{
    const ColorRgba Fill = ParseHexToRgba(HexNotation);

    char HexBuf[8]{};
    std::snprintf(HexBuf, sizeof(HexBuf), "#%02X%02X%02X", Fill.Red, Fill.Green, Fill.Blue);

    float Hue        = 0.0f;
    float Saturation = 0.0f;
    ComputeHslMetrics(Fill, Hue, Saturation);

    const ColourFilterGroup ResolvedGroup =
        (ExplicitGroup != ColourFilterGroup::All) ? ExplicitGroup : ClassifyHueFilterGroup(Fill);

    ColourCodexEntry Entry{};
    Entry.AliasName           = AliasName.empty() ? std::string("Untitled Colour") : std::string(AliasName);
    Entry.HexUpper            = HexBuf;
    Entry.HexCompact          = Entry.HexUpper.substr(1u);
    Entry.SeriesTitle         = SeriesTitle.empty() ? std::string(QueryFilterGroupTitle(ResolvedGroup)) : std::string(SeriesTitle);
    Entry.FillColour          = Fill;
    Entry.CmykPercent         = ComputeCmykPercentages(Fill);
    Entry.HueDegrees          = Hue;
    Entry.SaturationPercent   = Saturation;
    Entry.RelativeLuminance   = ComputeRelativeLuminance(Fill);
    Entry.FilterCategory      = ResolvedGroup;
    Entry.ReadOnlyEngineEntry = ReadOnlyEngineEntry;
    return Entry;
}

float ColourCodex::ComputeRelativeLuminance(ColorRgba FillColour) noexcept
{
    const auto Linearize = [](uint8_t Channel8) noexcept -> float
    {
        const float Srgb = static_cast<float>(Channel8) / 255.0f;
        return (Srgb <= 0.04045f) ? (Srgb / 12.92f) : std::pow((Srgb + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * Linearize(FillColour.Red) +
           0.7152f * Linearize(FillColour.Green) +
           0.0722f * Linearize(FillColour.Blue);
}

ColourContrastInk ColourCodex::QueryContrastInk(ColorRgba FillColour) noexcept
{
    // Threshold 0.405 places #FF7B00 (0.354) and #FF8800 (0.389) in White ink and
    // #FF9500 (0.428) .. #FFEA00 + #F6B800 in Black ink, matching the spectrum strip reference.
    if (ComputeRelativeLuminance(FillColour) > 0.405f)
    {
        return {
            { 17u, 17u, 17u, 255u },
            { 17u, 17u, 17u, 198u },
            { 0u,  0u,  0u,  36u  },
            true,
        };
    }
    return {
        { 255u, 255u, 255u, 255u },
        { 255u, 255u, 255u, 222u },
        { 255u, 255u, 255u, 38u  },
        false,
    };
}

const char* ColourCodex::QueryFilterGroupTitle(ColourFilterGroup Group) noexcept
{
    switch (Group)
    {
        case ColourFilterGroup::All:
            return "All";
        case ColourFilterGroup::AugustBeigeAndBrown:
            return "August Beige & Brown";
        case ColourFilterGroup::RedsAndBerries:
            return "Reds & Berries";
        case ColourFilterGroup::Yellows:
            return "Yellows";
        case ColourFilterGroup::Greens:
            return "Greens";
        case ColourFilterGroup::Blues:
            return "Blues";
        case ColourFilterGroup::Purples:
            return "Purples";
        case ColourFilterGroup::Neutrals:
            return "Neutrals";
    }
    return "All";
}

const char* ColourCodex::QuerySortOrderTitle(ColourSortOrder Order) noexcept
{
    switch (Order)
    {
        case ColourSortOrder::Spectrum:
            return "Spectrum";
        case ColourSortOrder::Luminance:
            return "Luminance";
        case ColourSortOrder::Alphabetical:
            return "Name";
    }
    return "Spectrum";
}

ColourFilterGroup ColourCodex::ClassifyHueFilterGroup(ColorRgba FillColour) noexcept
{
    float Hue        = 0.0f;
    float Saturation = 0.0f;
    ComputeHslMetrics(FillColour, Hue, Saturation);
    if (Saturation < 12.0f)
    {
        return ColourFilterGroup::Neutrals;
    }
    if (Hue >= 16.0f && Hue < 46.0f)
    {
        return ColourFilterGroup::AugustBeigeAndBrown;
    }
    if (Hue >= 46.0f && Hue < 72.0f)
    {
        return ColourFilterGroup::Yellows;
    }
    if (Hue >= 72.0f && Hue < 176.0f)
    {
        return ColourFilterGroup::Greens;
    }
    if (Hue >= 176.0f && Hue < 258.0f)
    {
        return ColourFilterGroup::Blues;
    }
    if (Hue >= 258.0f && Hue < 325.0f)
    {
        return ColourFilterGroup::Purples;
    }
    return ColourFilterGroup::RedsAndBerries;
}

bool ColourCodex::MatchesFilterGroup(const ColourCodexEntry& Entry, ColourFilterGroup Group) noexcept
{
    if (Group == ColourFilterGroup::All)
    {
        return true;
    }
    if (Group == ColourFilterGroup::AugustBeigeAndBrown)
    {
        return Entry.SeriesTitle == "August Beige & Brown";
    }
    if (Entry.FilterCategory == Group)
    {
        return true;
    }
    if (Group == ColourFilterGroup::Yellows &&
        Entry.HueDegrees >= 44.0f &&
        Entry.HueDegrees <= 68.0f &&
        Entry.SaturationPercent >= 18.0f)
    {
        return true;
    }
    return ClassifyHueFilterGroup(Entry.FillColour) == Group;
}

std::string ColourCodex::FormatHexUpper(ColorRgba FillColour)
{
    char Buffer[16]{};
    std::snprintf(
        Buffer,
        sizeof(Buffer),
        "#%02X%02X%02X",
        static_cast<unsigned>(FillColour.Red),
        static_cast<unsigned>(FillColour.Green),
        static_cast<unsigned>(FillColour.Blue));
    return Buffer;
}

std::string ColourCodex::FormatRgbTriplet(ColorRgba FillColour)
{
    char Buffer[32]{};
    std::snprintf(
        Buffer,
        sizeof(Buffer),
        "%u, %u, %u",
        static_cast<unsigned>(FillColour.Red),
        static_cast<unsigned>(FillColour.Green),
        static_cast<unsigned>(FillColour.Blue));
    return Buffer;
}

std::string ColourCodex::FormatCmykQuadruplet(const std::array<uint8_t, 4>& CmykPercent)
{
    char Buffer[40]{};
    std::snprintf(
        Buffer,
        sizeof(Buffer),
        "%u, %u, %u, %u",
        static_cast<unsigned>(CmykPercent[0]),
        static_cast<unsigned>(CmykPercent[1]),
        static_cast<unsigned>(CmykPercent[2]),
        static_cast<unsigned>(CmykPercent[3]));
    return Buffer;
}

std::size_t ColourCodex::AddProjectColour(
    std::string_view AliasName,
    std::string_view HexNotation,
    std::string_view SeriesTitle)
{
    RepositoryEntries.push_back(
        ConstructEntryFromHex(AliasName, HexNotation, SeriesTitle, ColourFilterGroup::All, false));
    return RepositoryEntries.size() - 1u;
}

bool ColourCodex::RenameEntry(std::size_t EntryIndex, std::string_view NewAliasName)
{
    if (EntryIndex >= RepositoryEntries.size() || RepositoryEntries[EntryIndex].ReadOnlyEngineEntry || NewAliasName.empty())
    {
        return false;
    }
    RepositoryEntries[EntryIndex].AliasName = std::string(NewAliasName);
    return true;
}

bool ColourCodex::UpdateEntryHex(std::size_t EntryIndex, std::string_view NewHexNotation)
{
    if (EntryIndex >= RepositoryEntries.size() || RepositoryEntries[EntryIndex].ReadOnlyEngineEntry)
    {
        return false;
    }
    const ColourCodexEntry& Current = RepositoryEntries[EntryIndex];
    RepositoryEntries[EntryIndex] = ConstructEntryFromHex(
        Current.AliasName,
        NewHexNotation,
        Current.SeriesTitle,
        ColourFilterGroup::All,
        false);
    return true;
}

bool ColourCodex::RemoveCustomEntry(std::size_t EntryIndex)
{
    if (EntryIndex >= RepositoryEntries.size() || RepositoryEntries[EntryIndex].ReadOnlyEngineEntry)
    {
        return false;
    }
    RepositoryEntries.erase(RepositoryEntries.begin() + static_cast<std::ptrdiff_t>(EntryIndex));
    return true;
}

std::span<const ColourCodexEntry> ColourCodex::QueryEntries() const noexcept
{
    return RepositoryEntries;
}

const ColourCodexEntry* ColourCodex::QueryEntryAt(std::size_t EntryIndex) const noexcept
{
    return EntryIndex < RepositoryEntries.size() ? &RepositoryEntries[EntryIndex] : nullptr;
}

std::size_t ColourCodex::FindEntryByName(std::string_view AliasName) const noexcept
{
    const std::string Target = LowercaseAscii(AliasName);
    for (std::size_t Index = 0u; Index < RepositoryEntries.size(); ++Index)
    {
        if (LowercaseAscii(RepositoryEntries[Index].AliasName) == Target)
        {
            return Index;
        }
    }
    return RepositoryEntries.size();
}

uint32_t ColourCodex::CountFilterMatches(ColourFilterGroup Group, std::string_view SearchText) const
{
    const std::string Needle = LowercaseAscii(SearchText);
    uint32_t          Count  = 0u;
    for (const ColourCodexEntry& Entry : RepositoryEntries)
    {
        if (!MatchesFilterGroup(Entry, Group))
        {
            continue;
        }
        if (!Needle.empty())
        {
            const std::string Haystack =
                LowercaseAscii(Entry.AliasName) + " " + LowercaseAscii(Entry.HexUpper) + " " + LowercaseAscii(Entry.SeriesTitle);
            if (Haystack.find(Needle) == std::string::npos)
            {
                continue;
            }
        }
        ++Count;
    }
    return Count;
}

std::vector<std::size_t> ColourCodex::QueryFilteredIndices(
    ColourFilterGroup Group,
    ColourSortOrder   Order,
    std::string_view  SearchText) const
{
    const std::string        Needle = LowercaseAscii(SearchText);
    std::vector<std::size_t> Indices;
    Indices.reserve(RepositoryEntries.size());

    for (std::size_t Index = 0u; Index < RepositoryEntries.size(); ++Index)
    {
        const ColourCodexEntry& Entry = RepositoryEntries[Index];
        if (!MatchesFilterGroup(Entry, Group))
        {
            continue;
        }
        if (!Needle.empty())
        {
            const std::string Haystack =
                LowercaseAscii(Entry.AliasName) + " " + LowercaseAscii(Entry.HexUpper) + " " + LowercaseAscii(Entry.SeriesTitle);
            if (Haystack.find(Needle) == std::string::npos)
            {
                continue;
            }
        }
        Indices.push_back(Index);
    }

    if (Order == ColourSortOrder::Alphabetical)
    {
        std::sort(
            Indices.begin(),
            Indices.end(),
            [this](std::size_t Left, std::size_t Right)
            {
                return RepositoryEntries[Left].AliasName < RepositoryEntries[Right].AliasName;
            });
    }
    else if (Order == ColourSortOrder::Luminance)
    {
        std::sort(
            Indices.begin(),
            Indices.end(),
            [this](std::size_t Left, std::size_t Right)
            {
                return RepositoryEntries[Left].RelativeLuminance < RepositoryEntries[Right].RelativeLuminance;
            });
    }
    else
    {
        std::sort(
            Indices.begin(),
            Indices.end(),
            [this](std::size_t Left, std::size_t Right)
            {
                const ColourCodexEntry& A        = RepositoryEntries[Left];
                const ColourCodexEntry& B        = RepositoryEntries[Right];
                const bool              NeutralA = A.SaturationPercent < 12.0f;
                const bool              NeutralB = B.SaturationPercent < 12.0f;
                if (NeutralA != NeutralB)
                {
                    return !NeutralA;
                }
                if (NeutralA && NeutralB)
                {
                    return A.RelativeLuminance < B.RelativeLuminance;
                }
                if (std::fabs(A.HueDegrees - B.HueDegrees) > 1.5f)
                {
                    return A.HueDegrees < B.HueDegrees;
                }
                return A.RelativeLuminance < B.RelativeLuminance;
            });
    }
    return Indices;
}

std::string ColourCodex::EncodeToTomlString() const
{
    std::ostringstream Stream;
    Stream << "# Frontier ColourCodex — Named Colour Alias Library\n";
    Stream << "Format = \"FrontierColourCodex.v1\"\n";
    Stream << "Count = " << RepositoryEntries.size() << "\n\n";

    std::vector<std::string> UsedKeys;
    UsedKeys.reserve(RepositoryEntries.size());

    for (const ColourCodexEntry& Entry : RepositoryEntries)
    {
        const std::string RootKey = SanitizePascalIdentifier(Entry.AliasName);
        std::string       Key     = RootKey;
        uint32_t          Suffix  = 2u;
        while (std::find(UsedKeys.begin(), UsedKeys.end(), Key) != UsedKeys.end())
        {
            Key = RootKey + std::to_string(Suffix++);
        }
        UsedKeys.push_back(Key);

        Stream << "[Colours." << Key << "]\n";
        Stream << "Name = \"" << Entry.AliasName << "\"\n";
        Stream << "Hex = \"" << Entry.HexUpper << "\"\n";
        Stream << "RGB = [" << static_cast<unsigned>(Entry.FillColour.Red) << ", "
               << static_cast<unsigned>(Entry.FillColour.Green) << ", "
               << static_cast<unsigned>(Entry.FillColour.Blue) << "]\n";
        Stream << "CMYK = [" << static_cast<unsigned>(Entry.CmykPercent[0]) << ", "
               << static_cast<unsigned>(Entry.CmykPercent[1]) << ", "
               << static_cast<unsigned>(Entry.CmykPercent[2]) << ", "
               << static_cast<unsigned>(Entry.CmykPercent[3]) << "]\n";
        Stream << "Series = \"" << Entry.SeriesTitle << "\"\n";
        Stream << "FilterGroup = \"" << QueryFilterGroupTitle(Entry.FilterCategory) << "\"\n\n";
    }
    return Stream.str();
}

bool ColourCodex::DecodeFromTomlString(std::string_view TomlDocument, std::string& OutDiagnostic, bool AppendAsImported)
{
    toml::table Root{};
    try
    {
        Root = toml::parse(TomlDocument);
    }
    catch (const toml::parse_error& Failure)
    {
        OutDiagnostic = std::string("ColourCodex TOML parse error: ") + std::string(Failure.description());
        return false;
    }

    const toml::table* ColoursSection = Root["Colours"].as_table();
    if (ColoursSection == nullptr)
    {
        ColoursSection = Root["ColourCodex"].as_table();
    }
    if (ColoursSection == nullptr)
    {
        OutDiagnostic = "ColourCodex TOML missing [Colours] section.";
        return false;
    }

    std::vector<ColourCodexEntry> DecodedEntries;
    for (const auto& [KeyName, EntrySubtree] : *ColoursSection)
    {
        if (const toml::table* SubSection = EntrySubtree.as_table())
        {
            const std::string NameStr   = SubSection->get("Name") ? SubSection->get("Name")->value_or(std::string(KeyName.str())) : std::string(KeyName.str());
            const std::string HexStr    = SubSection->get("Hex") ? SubSection->get("Hex")->value_or(std::string("#3B1F3A")) : std::string("#3B1F3A");
            const std::string SeriesStr = SubSection->get("Series") ? SubSection->get("Series")->value_or(std::string("Imported")) : (SubSection->get("Group") ? SubSection->get("Group")->value_or(std::string("Imported")) : std::string("Imported"));
            const std::string GroupStr  = SubSection->get("FilterGroup") ? SubSection->get("FilterGroup")->value_or(std::string("")) : std::string("");
            DecodedEntries.push_back(
                ConstructEntryFromHex(
                    NameStr,
                    HexStr,
                    SeriesStr,
                    ParseFilterGroupByName(GroupStr),
                    !AppendAsImported));
        }
    }

    if (DecodedEntries.empty())
    {
        OutDiagnostic = "ColourCodex TOML contained zero valid colour entries.";
        return false;
    }

    if (!AppendAsImported)
    {
        RepositoryEntries = std::move(DecodedEntries);
    }
    else
    {
        for (ColourCodexEntry& Imported : DecodedEntries)
        {
            RepositoryEntries.push_back(std::move(Imported));
        }
    }
    OutDiagnostic.clear();
    return true;
}

bool ColourCodex::SaveToTomlFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic) const
{
    std::error_code FsErr{};
    if (FilePath.has_parent_path())
    {
        std::filesystem::create_directories(FilePath.parent_path(), FsErr);
    }
    std::ofstream Output(FilePath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutDiagnostic = "Unable to open ColourCodex TOML path for writing: " + FilePath.string();
        return false;
    }
    const std::string Encoded = EncodeToTomlString();
    Output.write(Encoded.data(), static_cast<std::streamsize>(Encoded.size()));
    if (!Output.good())
    {
        OutDiagnostic = "Write failure while saving ColourCodex TOML: " + FilePath.string();
        return false;
    }
    OutDiagnostic.clear();
    return true;
}

bool ColourCodex::LoadFromTomlFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic, bool AppendAsImported)
{
    std::ifstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        OutDiagnostic = "Unable to open ColourCodex TOML path for reading: " + FilePath.string();
        return false;
    }
    std::ostringstream Buffer;
    Buffer << Input.rdbuf();
    return DecodeFromTomlString(Buffer.str(), OutDiagnostic, AppendAsImported);
}

bool ColourCodex::MatchesSwatchExtension(const std::filesystem::path& FilePath) noexcept
{
    std::string Ext = FilePath.extension().string();
    std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char Ch) {
        return static_cast<char>(std::tolower(Ch));
    });
    return Ext == ".colourswatch";
}

std::string ColourCodex::EncodeSingleSwatchString(std::size_t EntryIndex) const
{
    const ColourCodexEntry* Entry = QueryEntryAt(EntryIndex);
    if (Entry == nullptr)
    {
        return {};
    }

    std::ostringstream Stream;
    Stream << "# Frontier Singular Colour Swatch (.ColourSwatch)\n";
    Stream << "Format = \"FrontierColourSwatch.v1\"\n\n";
    Stream << "[ColourSwatch]\n";
    Stream << "Name = \"" << Entry->AliasName << "\"\n";
    Stream << "Hex = \"" << Entry->HexUpper << "\"\n";
    Stream << "RGB = [" << static_cast<unsigned>(Entry->FillColour.Red) << ", "
           << static_cast<unsigned>(Entry->FillColour.Green) << ", "
           << static_cast<unsigned>(Entry->FillColour.Blue) << "]\n";
    Stream << "CMYK = [" << static_cast<unsigned>(Entry->CmykPercent[0]) << ", "
           << static_cast<unsigned>(Entry->CmykPercent[1]) << ", "
           << static_cast<unsigned>(Entry->CmykPercent[2]) << ", "
           << static_cast<unsigned>(Entry->CmykPercent[3]) << "]\n";
    Stream << "Series = \"" << Entry->SeriesTitle << "\"\n";
    Stream << "FilterGroup = \"" << QueryFilterGroupTitle(Entry->FilterCategory) << "\"\n";
    return Stream.str();
}

bool ColourCodex::DecodeSingleSwatchString(
    std::string_view SwatchDocument,
    std::string&     OutDiagnostic,
    std::size_t*     OutAddedIndex)
{
    toml::table Root{};
    try
    {
        Root = toml::parse(SwatchDocument);
    }
    catch (const toml::parse_error& Failure)
    {
        OutDiagnostic = std::string(".ColourSwatch parse error: ") + std::string(Failure.description());
        return false;
    }

    const toml::table* SwatchSection = Root["ColourSwatch"].as_table();
    if (SwatchSection == nullptr)
    {
        OutDiagnostic = ".ColourSwatch document missing [ColourSwatch] section.";
        return false;
    }

    const std::string NameStr   = SwatchSection->get("Name") ? SwatchSection->get("Name")->value_or(std::string("Imported Swatch")) : std::string("Imported Swatch");
    const std::string HexStr    = SwatchSection->get("Hex") ? SwatchSection->get("Hex")->value_or(std::string("#3B1F3A")) : std::string("#3B1F3A");
    const std::string SeriesStr = SwatchSection->get("Series") ? SwatchSection->get("Series")->value_or(std::string("Imported")) : std::string("Imported");
    const std::string GroupStr  = SwatchSection->get("FilterGroup") ? SwatchSection->get("FilterGroup")->value_or(std::string("")) : std::string("");

    RepositoryEntries.push_back(
        ConstructEntryFromHex(NameStr, HexStr, SeriesStr, ParseFilterGroupByName(GroupStr), false));
    if (OutAddedIndex != nullptr)
    {
        *OutAddedIndex = RepositoryEntries.size() - 1u;
    }
    OutDiagnostic.clear();
    return true;
}

bool ColourCodex::SaveSingleSwatchFile(
    std::size_t                  EntryIndex,
    const std::filesystem::path& FilePath,
    std::string&                 OutDiagnostic) const
{
    const std::string Encoded = EncodeSingleSwatchString(EntryIndex);
    if (Encoded.empty())
    {
        OutDiagnostic = "Invalid ColourCodex entry index for .ColourSwatch save.";
        return false;
    }

    std::filesystem::path CanonicalPath = FilePath;
    if (!MatchesSwatchExtension(CanonicalPath))
    {
        CanonicalPath.replace_extension(CanonicalSwatchExtension);
    }
    else
    {
        CanonicalPath.replace_extension(CanonicalSwatchExtension);
    }

    std::error_code FsErr{};
    if (CanonicalPath.has_parent_path())
    {
        std::filesystem::create_directories(CanonicalPath.parent_path(), FsErr);
    }

    std::ofstream Output(CanonicalPath, std::ios::binary | std::ios::trunc);
    if (!Output.is_open())
    {
        OutDiagnostic = "Unable to open .ColourSwatch path for writing: " + CanonicalPath.string();
        return false;
    }
    Output.write(Encoded.data(), static_cast<std::streamsize>(Encoded.size()));
    if (!Output.good())
    {
        OutDiagnostic = "Write failure while saving .ColourSwatch: " + CanonicalPath.string();
        return false;
    }
    OutDiagnostic.clear();
    return true;
}

bool ColourCodex::LoadSingleSwatchFile(
    const std::filesystem::path& FilePath,
    std::string&                 OutDiagnostic,
    std::size_t*                 OutAddedIndex)
{
    if (!MatchesSwatchExtension(FilePath))
    {
        OutDiagnostic = "Expected .ColourSwatch extension: " + FilePath.string();
        return false;
    }
    std::ifstream Input(FilePath, std::ios::binary);
    if (!Input.is_open())
    {
        OutDiagnostic = "Unable to open .ColourSwatch file: " + FilePath.string();
        return false;
    }
    std::ostringstream Buffer;
    Buffer << Input.rdbuf();
    return DecodeSingleSwatchString(Buffer.str(), OutDiagnostic, OutAddedIndex);
}

} // namespace Frontier
