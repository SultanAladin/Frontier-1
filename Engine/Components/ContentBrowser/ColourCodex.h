/*============================================================================================================================================
                                                               COLOURCODEX.H
============================================================================================================================================*/
// 🎨 Named colour alias library supporting HEX/RGB/CMYK/HSL metrics, contrast ink selection, spectrum filtering, and TOML persistence

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ColourFilterGroup : uint8_t
{
    All                 = 0, // [-] - Every colour alias in the active library scope
    AugustBeigeAndBrown = 1, // [-] - Warm amber, marigold, beige, terracotta, and golden ramp
    RedsAndBerries      = 2, // [-] - Strawberry, cherry, raspberry, coral, and crimson hues
    Yellows             = 3, // [-] - Pineapple yellow, banana, canary, and solar lemon hues
    Greens              = 4, // [-] - Matcha, forest pine, and mineral teal hues
    Blues               = 5, // [-] - Midnight, royal blue, cobalt, sapphire, and glacier hues
    Purples             = 6, // [-] - Fig purple, plum, and lavender hues
    Neutrals            = 7, // [-] - Charcoal, obsidian, graphite, slate, arctic, and ivory
};

enum class ColourSortOrder : uint8_t
{
    Spectrum     = 0, // [-] - Chromatic hue progression with neutrals grouped cleanly
    Luminance    = 1, // [-] - Darkest to brightest WCAG relative luminance
    Alphabetical = 2, // [-] - Lexicographical alias ordering
};

enum class ColourRepositoryLayout : uint8_t
{
    GridCircles      = 0, // [-] - Flat circular colour swatches in a multi-column grid
    VerticalStrips   = 1, // [-] - Flush full-height vertical spectrum strips with rotated HEX labels
    HorizontalStrips = 2, // [-] - Flush full-width horizontal spectrum bands with name and HEX/RGB
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourCodexEntry
{
    std::string            AliasName           {};                           // [-]   - Human-readable colour alias
    std::string            HexUpper            {};                           // [-]   - Canonical "#RRGGBB" uppercase notation
    std::string            HexCompact          {};                           // [-]   - Compact "RRGGBB" notation for strip labels
    std::string            SeriesTitle         {};                           // [-]   - Collection or material series caption
    ColorRgba              FillColour          { 59u, 31u, 58u, 255u };      // [-]   - Flat sRGB display colour
    std::array<uint8_t, 4> CmykPercent         { 0u, 47u, 2u, 77u };         // [%]   - Integer C, M, Y, K percentages [0..100]
    float                  HueDegrees          = 0.0f;                       // [deg] - Chromatic hue angle [0..360)
    float                  SaturationPercent   = 0.0f;                       // [%]   - HSL saturation [0..100]
    float                  RelativeLuminance   = 0.0f;                       // [0-1] - Linearized WCAG 2.1 luminance
    ColourFilterGroup      FilterCategory      = ColourFilterGroup::Purples; // [-]   - Dedicated Colour Filters bucket
    bool                   ReadOnlyEngineEntry = true;                       // [-]   - True for shipped built-in engine aliases
};

struct ColourContrastInk
{
    ColorRgba PrimaryInk { 255u, 255u, 255u, 255u }; // [-] - Crisp #111111 black or #FFFFFF white ink
    ColorRgba MutedInk   { 255u, 255u, 255u, 218u }; // [-] - Secondary spec readout ink
    ColorRgba BorderInk  { 255u, 255u, 255u, 38u };  // [-] - Subtle tile outline ink
    bool      DarkInk    = false;                    // [-] - True when black ink was chosen over bright fill
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ColourCodex
{
public:
    ColourCodex();

    void ResetToBuiltInRepository();

    [[nodiscard]] static ColourCodexEntry ConstructEntryFromHex(
        std::string_view  AliasName,
        std::string_view  HexNotation,
        std::string_view  SeriesTitle         = "Custom",
        ColourFilterGroup ExplicitGroup       = ColourFilterGroup::All,
        bool              ReadOnlyEngineEntry = false);

    [[nodiscard]] static ColourContrastInk   QueryContrastInk(ColorRgba FillColour) noexcept;
    [[nodiscard]] static float               ComputeRelativeLuminance(ColorRgba FillColour) noexcept;
    [[nodiscard]] static const char*         QueryFilterGroupTitle(ColourFilterGroup Group) noexcept;
    [[nodiscard]] static const char*         QuerySortOrderTitle(ColourSortOrder Order) noexcept;
    [[nodiscard]] static ColourFilterGroup   ClassifyHueFilterGroup(ColorRgba FillColour) noexcept;
    [[nodiscard]] static bool                MatchesFilterGroup(const ColourCodexEntry& Entry, ColourFilterGroup Group) noexcept;
    [[nodiscard]] static std::string         FormatHexUpper(ColorRgba FillColour);
    [[nodiscard]] static std::string         FormatRgbTriplet(ColorRgba FillColour);
    [[nodiscard]] static std::string         FormatCmykQuadruplet(const std::array<uint8_t, 4>& CmykPercent);

    std::size_t AddProjectColour(
        std::string_view AliasName,
        std::string_view HexNotation,
        std::string_view SeriesTitle = "Project");

    bool RenameEntry(std::size_t EntryIndex, std::string_view NewAliasName);
    bool UpdateEntryHex(std::size_t EntryIndex, std::string_view NewHexNotation);
    bool RemoveCustomEntry(std::size_t EntryIndex);

    [[nodiscard]] std::span<const ColourCodexEntry> QueryEntries() const noexcept;
    [[nodiscard]] const ColourCodexEntry*           QueryEntryAt(std::size_t EntryIndex) const noexcept;
    [[nodiscard]] std::size_t                       FindEntryByName(std::string_view AliasName) const noexcept;
    [[nodiscard]] uint32_t                          CountFilterMatches(ColourFilterGroup Group, std::string_view SearchText = {}) const;

    [[nodiscard]] std::vector<std::size_t> QueryFilteredIndices(
        ColourFilterGroup Group,
        ColourSortOrder   Order,
        std::string_view  SearchText = {}) const;

    [[nodiscard]] std::string EncodeToTomlString() const;
    [[nodiscard]] bool        DecodeFromTomlString(std::string_view TomlDocument, std::string& OutDiagnostic, bool AppendAsImported = true);
    [[nodiscard]] bool        SaveToTomlFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic) const;
    [[nodiscard]] bool        LoadFromTomlFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic, bool AppendAsImported = true);

    // 📝 Singular named colour swatch container (.ColourSwatch — case-insensitive load, canonical PascalCase write)
    static constexpr std::string_view CanonicalSwatchExtension = ".ColourSwatch";
    [[nodiscard]] static bool         MatchesSwatchExtension(const std::filesystem::path& FilePath) noexcept;
    [[nodiscard]] std::string         EncodeSingleSwatchString(std::size_t EntryIndex) const;
    [[nodiscard]] bool                DecodeSingleSwatchString(std::string_view SwatchDocument, std::string& OutDiagnostic, std::size_t* OutAddedIndex = nullptr);
    [[nodiscard]] bool                SaveSingleSwatchFile(std::size_t EntryIndex, const std::filesystem::path& FilePath, std::string& OutDiagnostic) const;
    [[nodiscard]] bool                LoadSingleSwatchFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic, std::size_t* OutAddedIndex = nullptr);

private:
    std::vector<ColourCodexEntry> RepositoryEntries {}; // [-] - Ordered built-in and custom ColourCodex entries
};

} // namespace Frontier
