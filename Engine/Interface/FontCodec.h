/*==========================================================================================================================================
                                                                FONTCODEC.H
==========================================================================================================================================*/
// 🧩 Discover static typefaces and regenerate per-family descriptors from actual files.

#pragma once

#if defined(_MSC_VER)
#pragma warning(disable : 4324) // Disable structure padding alignment warning under /WX
#endif

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <toml++/toml.hpp>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                           FONT WEIGHT & STYLE CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

enum class FontWeightCategory : uint32_t
{
    Thin       = 100, // 100: Thin / Hairline
    ExtraLight = 200, // 200: Extra Light / Ultra Light
    Light      = 300, // 300: Light
    Regular    = 400, // 400: Regular / Normal / Book
    Medium     = 500, // 500: Medium
    SemiBold   = 600, // 600: Semi Bold / Demi Bold
    Bold       = 700, // 700: Bold
    ExtraBold  = 800, // 800: Extra Bold / Ultra Bold
    Black      = 900  // 900: Black / Heavy
};

enum class FontStyleCategory : uint32_t
{
    Normal  = 0, // Upright / Normal
    Italic  = 1, // Italicized
    Oblique = 2  // Slanted / Oblique
};

//------------------------------------------------------------------------------------------------------------------------
//                                           FONT VARIANT & FAMILY INDEX
//------------------------------------------------------------------------------------------------------------------------

struct FontVariantIndex
{
    FontWeightCategory Weight;      // [category] numeric weight tier (100 - 900)
    FontStyleCategory  Style;       // [category] italic / normal
    std::string        VariantName; // [name] e.g. "SemiBold", "Regular", "BoldItalic"
    std::string        FilePath;    // [path] absolute or relative file path to font asset
    std::string        FileFormat;  // [format] "TrueType", "OpenType", "WOFF2"
    uint32_t           FileSize;    // [bytes] file byte count
};

struct FontFamilyIndex
{
    std::string                   FamilyName;   // [name] e.g. "General Sans", "Inter", "JetBrains Mono"
    std::string                   CategoryName; // [category] "SansSerif", "Display", "Monospace"
    std::string                   Description;  // [desc] e.g. "Clean & Modern", "Monospaced Code"
    std::vector<FontVariantIndex> Variants;     // [variants] list of discovered weights/styles
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FONT CODEC
//------------------------------------------------------------------------------------------------------------------------

class FontCodec
{
  public:
    FontCodec() noexcept;
    ~FontCodec() noexcept = default;

    FontCodec(const FontCodec &)                = default;
    FontCodec &operator=(const FontCodec &)     = default;
    FontCodec(FontCodec &&) noexcept            = default;
    FontCodec &operator=(FontCodec &&) noexcept = default;

    // Content Folder Scanning — recursively scans actual TTF/OTF files and regenerates per-family TOML descriptors
    bool ScanDirectory(std::string_view DirectoryPath) noexcept;
    bool ScanEngineAndGameContent(std::string_view EngineContentPath, std::string_view GameContentPath) noexcept;

    // TOML descriptor ingestion and generation (replaces .manifest)
    bool IngestToml(std::string_view TomlPath) noexcept;
    bool WriteToml(std::string_view TomlPath, const FontFamilyIndex &Family) noexcept;

    // Dynamic Font & Variant Query Lookups
    [[nodiscard]] const FontFamilyIndex  *QueryFamily(std::string_view DesiredFamilyName) const noexcept;
    [[nodiscard]] const FontVariantIndex *QueryVariant(
        std::string_view DesiredFamilyName, FontWeightCategory DesiredWeight,
        FontStyleCategory DesiredStyle = FontStyleCategory::Normal) const noexcept;

    [[nodiscard]] const std::vector<FontFamilyIndex> &QueryDiscoveredFamilies() const noexcept
    {
        return DiscoveredFamilies;
    }
    [[nodiscard]] size_t QueryFamilyCount() const noexcept { return DiscoveredFamilies.size(); }
    [[nodiscard]] size_t QueryTotalVariantCount() const noexcept;

    void Clear() noexcept;

    // Single unified conversion operator for discovered family count
    template <typename TargetType> [[nodiscard]] TargetType Convert() const noexcept;

  private:
    void ClassifyFontFileName(std::string_view FileName, std::string_view FilePath, uint32_t FileSizeBytes) noexcept;

    FontFamilyIndex *FindOrInsertFamily(std::string_view FamilyName) noexcept;

    void InsertVariant(FontFamilyIndex &Family, FontWeightCategory Weight, FontStyleCategory Style,
                       std::string_view VariantName, std::string_view FilePath, std::string_view Format,
                       uint32_t FileSizeBytes) noexcept;

    std::vector<FontFamilyIndex> DiscoveredFamilies; // [families] auto-discovered + TOML-cached font index
};

template <> inline size_t FontCodec::Convert<size_t>() const noexcept
{
    return DiscoveredFamilies.size();
}

template <> inline bool FontCodec::Convert<bool>() const noexcept
{
    return !DiscoveredFamilies.empty();
}

} // namespace Frontier
