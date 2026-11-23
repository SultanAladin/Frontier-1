/*==========================================================================================================================================
                                                               FONTCODEC.CPP
==========================================================================================================================================*/
// 🧩 Discover static typefaces and regenerate per-family descriptors from actual files.

#include "FontCodec.h"
#include "ContentDirectory.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Frontier
{

namespace fs = std::filesystem;

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

FontCodec::FontCodec() noexcept : DiscoveredFamilies{}
{
}

void FontCodec::Clear() noexcept
{
    DiscoveredFamilies.clear();
}

size_t FontCodec::QueryTotalVariantCount() const noexcept
{
    size_t Total = 0;
    for (const auto &Fam : DiscoveredFamilies)
    {
        Total += Fam.Variants.size();
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           STRING HELPER UTILITIES
//------------------------------------------------------------------------------------------------------------------------

static std::string ToLowerString(std::string_view Text)
{
    std::string Result(Text);
    std::transform(Result.begin(), Result.end(), Result.begin(),
                   [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Result;
}

// Strip optical-size, axis, and format suffixes from a filename stem before weight classification.
// Handles: Inter_18pt, Inter_24pt, Inter[Display], InterVariable, Inter-VariableFont_wght
static std::string StripOpticalSuffixes(std::string_view Stem)
{
    std::string Result(Stem);

    // Remove VariableFont axis descriptors e.g. "_wght", "_wdth", "_opsz"
    for (const char *Axis : {"_wght", "_wdth", "_opsz", "_ital", "_slnt"})
    {
        size_t Pos = Result.find(Axis);
        if (Pos != std::string::npos)
        {
            Result = Result.substr(0, Pos);
        }
    }

    // Remove optical size tokens: _18pt _24pt _36pt etc.
    // Pattern: underscore followed by digits and "pt"
    {
        size_t Pos = Result.find('_');
        while (Pos != std::string::npos)
        {
            size_t End = Pos + 1;
            while (End < Result.size() && std::isdigit(static_cast<unsigned char>(Result[End])))
                ++End;
            if (End < Result.size() - 1 && Result[End] == 'p' && Result[End + 1] == 't')
            {
                Result = Result.substr(0, Pos) + Result.substr(End + 2);
                Pos    = Result.find('_');
            }
            else
            {
                Pos = Result.find('_', Pos + 1);
            }
        }
    }

    // Remove bracketed axis tokens: [Display] [Body] [Caption]
    {
        size_t BracketOpen = Result.find('[');
        if (BracketOpen != std::string::npos)
        {
            Result = Result.substr(0, BracketOpen);
        }
    }

    // Remove trailing "Variable" or "VariableFont"
    for (const char *Token : {"VariableFont", "Variable"})
    {
        size_t Pos = Result.find(Token);
        if (Pos != std::string::npos)
        {
            Result = Result.substr(0, Pos);
        }
    }

    // Trim trailing separators
    while (!Result.empty() && (Result.back() == '-' || Result.back() == '_'))
    {
        Result.pop_back();
    }

    return Result;
}

// Normalize a family name for deduplication (strip spaces, dashes, underscores, lowercase)
static std::string NormalizeKey(std::string_view Str)
{
    std::string Clean;
    for (char C : Str)
    {
        if (C != ' ' && C != '_' && C != '-')
        {
            Clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(C))));
        }
    }
    return Clean;
}

// Insert spaces before uppercase letters in a CamelCase identifier
static std::string CamelToReadable(std::string_view CamelStr)
{
    std::string Result;
    for (size_t i = 0; i < CamelStr.size(); ++i)
    {
        char C = CamelStr[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(C)) &&
            std::islower(static_cast<unsigned char>(CamelStr[i - 1])))
        {
            Result.push_back(' ');
        }
        Result.push_back(C);
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                     FONT WEIGHT & STYLE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

static FontWeightCategory ClassifyWeight(std::string_view VariantLower)
{
    if (VariantLower.find("thin") != std::string_view::npos || VariantLower.find("hairline") != std::string_view::npos)
        return FontWeightCategory::Thin;
    if (VariantLower.find("extralight") != std::string_view::npos ||
        VariantLower.find("ultralight") != std::string_view::npos)
        return FontWeightCategory::ExtraLight;
    if (VariantLower.find("light") != std::string_view::npos)
        return FontWeightCategory::Light;
    if (VariantLower.find("semibold") != std::string_view::npos ||
        VariantLower.find("demibold") != std::string_view::npos)
        return FontWeightCategory::SemiBold;
    if (VariantLower.find("extrabold") != std::string_view::npos ||
        VariantLower.find("ultrabold") != std::string_view::npos)
        return FontWeightCategory::ExtraBold;
    if (VariantLower.find("bold") != std::string_view::npos)
        return FontWeightCategory::Bold;
    if (VariantLower.find("medium") != std::string_view::npos)
        return FontWeightCategory::Medium;
    if (VariantLower.find("black") != std::string_view::npos || VariantLower.find("heavy") != std::string_view::npos)
        return FontWeightCategory::Black;
    return FontWeightCategory::Regular;
}

static FontStyleCategory ClassifyStyle(std::string_view VariantLower)
{
    if (VariantLower.find("italic") != std::string_view::npos)
        return FontStyleCategory::Italic;
    if (VariantLower.find("oblique") != std::string_view::npos)
        return FontStyleCategory::Oblique;
    return FontStyleCategory::Normal;
}

static std::string InferCategory(std::string_view FamilyName)
{
    std::string Lower = ToLowerString(FamilyName);
    if (Lower.find("mono") != std::string::npos || Lower.find("code") != std::string::npos ||
        Lower.find("console") != std::string::npos)
        return "Monospace";
    if (Lower.find("display") != std::string::npos || Lower.find("grotesk") != std::string::npos)
        return "Display";
    return "SansSerif";
}

//------------------------------------------------------------------------------------------------------------------------
//                                     FAMILY FIND-OR-INSERT HELPER
//------------------------------------------------------------------------------------------------------------------------

FontFamilyIndex *FontCodec::FindOrInsertFamily(std::string_view FamilyName) noexcept
{
    std::string NormTarget = NormalizeKey(FamilyName);

    for (auto &Fam : DiscoveredFamilies)
    {
        if (NormalizeKey(Fam.FamilyName) == NormTarget)
        {
            return &Fam;
        }
    }

    FontFamilyIndex NewFamily;
    NewFamily.FamilyName   = std::string(FamilyName);
    NewFamily.CategoryName = InferCategory(FamilyName);
    NewFamily.Description  = "Auto-discovered";
    DiscoveredFamilies.push_back(std::move(NewFamily));
    return &DiscoveredFamilies.back();
}

void FontCodec::InsertVariant(FontFamilyIndex &Family, FontWeightCategory Weight, FontStyleCategory Style,
                              std::string_view VariantName, std::string_view FilePath, std::string_view Format,
                              uint32_t FileSizeBytes) noexcept
{
    for (auto &Var : Family.Variants)
    {
        if (Var.Weight == Weight && Var.Style == Style)
        {
            Var.FilePath = std::string(FilePath);
            Var.FileSize = FileSizeBytes;
            return;
        }
    }

    FontVariantIndex V;
    V.Weight      = Weight;
    V.Style       = Style;
    V.VariantName = std::string(VariantName);
    V.FilePath    = std::string(FilePath);
    V.FileFormat  = std::string(Format);
    V.FileSize    = FileSizeBytes;
    Family.Variants.push_back(std::move(V));
}

//------------------------------------------------------------------------------------------------------------------------
//                                           AUTOMATIC FILENAME CLASSIFIER
//------------------------------------------------------------------------------------------------------------------------

void FontCodec::ClassifyFontFileName(std::string_view FileName, std::string_view FilePath,
                                     uint32_t FileSizeBytes) noexcept
{
    std::string BaseName = fs::path(FileName).stem().string();
    std::string Ext      = fs::path(FileName).extension().string();
    std::string ExtLower = ToLowerString(Ext);

    // Strip optical-size and variable-font suffixes before splitting
    std::string Cleaned = StripOpticalSuffixes(BaseName);

    std::string FamilyPart  = Cleaned;
    std::string VariantPart = "Regular";

    // Split on first dash (Family-Weight convention)
    size_t DashPos = Cleaned.find('-');
    if (DashPos != std::string::npos)
    {
        FamilyPart  = Cleaned.substr(0, DashPos);
        VariantPart = Cleaned.substr(DashPos + 1);
    }
    else
    {
        // Fall back to underscore separator
        size_t UnderPos = Cleaned.find('_');
        if (UnderPos != std::string::npos)
        {
            FamilyPart  = Cleaned.substr(0, UnderPos);
            VariantPart = Cleaned.substr(UnderPos + 1);
        }
    }

    std::string ReadableFamily = CamelToReadable(FamilyPart);
    std::string VariantLower   = ToLowerString(VariantPart);

    FontWeightCategory Weight = ClassifyWeight(VariantLower);
    FontStyleCategory  Style  = ClassifyStyle(VariantLower);

    std::string Format = (ExtLower == ".otf") ? "OpenType" : "TrueType";

    FontFamilyIndex *TargetFam = FindOrInsertFamily(ReadableFamily);
    InsertVariant(*TargetFam, Weight, Style, VariantPart, FilePath, Format, FileSizeBytes);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TOML DESCRIPTOR — INGESTION
//
// Reads a <Family>.toml from disk (generated previously by WriteToml or hand-written once)
// and registers all declared variants. Any TTF that exists in the same folder but is not
// listed in the TOML will be picked up by the filesystem scan in Pass 2 of ScanDirectory.
//------------------------------------------------------------------------------------------------------------------------

bool FontCodec::IngestToml(std::string_view TomlPath) noexcept
{
    try
    {
        auto Table = toml::parse_file(TomlPath);

        auto FontArchive = Table["font_archive"];
        if (!FontArchive)
        {
            return false;
        }

        std::string Family      = FontArchive["family"].value_or(std::string{});
        std::string Category    = FontArchive["category"].value_or(std::string{"SansSerif"});
        std::string Description = FontArchive["description"].value_or(std::string{"Auto-discovered"});

        if (Family.empty())
        {
            return false;
        }

        FontFamilyIndex *FamilyEntry = FindOrInsertFamily(Family);
        FamilyEntry->CategoryName    = Category;
        FamilyEntry->Description     = Description;

        fs::path TomlDir = DecodeContentPath(TomlPath).parent_path();

        // Iterate [variants] table — key = weight label, value = filename
        if (auto Variants = Table["variants"].as_table())
        {
            for (auto &[Key, Value] : *Variants)
            {
                std::string FileName = Value.value_or(std::string{});
                if (FileName.empty())
                    continue;

                fs::path    FontPath   = TomlDir / DecodeContentPath(FileName);
                std::string VariantKey = std::string(Key.str());
                std::string VarLower   = ToLowerString(VariantKey);

                FontWeightCategory Weight = ClassifyWeight(VarLower);
                if (VarLower.size() >= 3 && std::isdigit(static_cast<unsigned char>(VarLower[0])))
                {
                    const auto NumericWeight = std::stoul(VarLower);
                    if (NumericWeight >= 100 && NumericWeight <= 900 && NumericWeight % 100 == 0)
                        Weight = static_cast<FontWeightCategory>(NumericWeight);
                }
                FontStyleCategory Style = ClassifyStyle(VarLower);

                std::error_code Ec;
                uint32_t        FileSize = static_cast<uint32_t>(fs::file_size(FontPath, Ec));
                if (Ec || FileSize == 0)
                    continue;

                std::string ExtLower = ToLowerString(FontPath.extension().string());
                std::string Format   = (ExtLower == ".otf") ? "OpenType" : "TrueType";

                InsertVariant(*FamilyEntry, Weight, Style, VariantKey, EncodeContentPath(FontPath), Format, FileSize);
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        TOML DESCRIPTOR — AUTO-GENERATION
//
// Writes a <Family>.toml beside the TTF files after a filesystem scan. Called automatically
// by ScanDirectory when no TOML exists or when it is older than the newest TTF in the folder.
// The file is human-readable but never needs to be hand-written — the codec regenerates it.
//------------------------------------------------------------------------------------------------------------------------

bool FontCodec::WriteToml(std::string_view TomlPath, const FontFamilyIndex &Family) noexcept
{
    try
    {
        std::ofstream Out(DecodeContentPath(TomlPath), std::ios::out | std::ios::trunc);
        if (!Out.is_open())
            return false;

        toml::table Variants;
        for (const auto &Variant : Family.Variants)
        {
            const auto Key = std::to_string(static_cast<uint32_t>(Variant.Weight)) +
                             (Variant.Style == FontStyleCategory::Normal ? "" : "_italic");
            Variants.insert_or_assign(Key, EncodeContentPath(DecodeContentPath(Variant.FilePath).filename()));
        }
        toml::table Document{{"font_archive", toml::table{{"family", Family.FamilyName},
                                                          {"category", Family.CategoryName},
                                                          {"description", Family.Description},
                                                          {"format", "StaticOpenType"}}},
                             {"variants", std::move(Variants)}};
        Out << Document << '\n';
        return Out.good();
    }
    catch (...)
    {
        return false;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           DIRECTORY SCANNING
//
// For each family subfolder inside DirectoryPath:
//   ① Read existing <Family>.toml if present and not stale
//   ② Scan all .ttf / .otf files via ClassifyFontFileName (auto-classifies weight + style)
//   ③ Write (or overwrite) <Family>.toml when the filesystem is newer than the cached descriptor
//------------------------------------------------------------------------------------------------------------------------

bool FontCodec::ScanDirectory(std::string_view DirectoryPath) noexcept
{
    Clear();
    try
    {
        const fs::path SearchPath = DecodeContentPath(DirectoryPath);
        if (!fs::is_directory(SearchPath))
            return false;
        std::vector<fs::path> Fonts;
        for (const auto &Entry : fs::recursive_directory_iterator(SearchPath))
        {
            if (!Entry.is_regular_file())
                continue;
            const std::string Extension = ToLowerString(Entry.path().extension().string());
            if (Extension == ".ttf" || Extension == ".otf")
                Fonts.push_back(Entry.path());
        }
        std::sort(Fonts.begin(), Fonts.end());
        toml::table Catalogue;
        const auto  CataloguePath = SearchPath / "FontCatalogue.toml";
        if (fs::is_regular_file(CataloguePath))
            Catalogue = toml::parse_file(EncodeContentPath(CataloguePath));
        for (const auto &FontPath : Fonts)
        {
            const auto ByteCount = fs::file_size(FontPath);
            if (ByteCount == 0 || ByteCount > 32u * 1024u * 1024u)
                continue;
            bool Catalogued = false;
            if (const auto *Faces = Catalogue["faces"].as_array())
            {
                for (const auto &FaceNode : *Faces)
                {
                    const toml::node_view<const toml::node> Face{FaceNode};
                    const auto                              Relative = Face["file"].value_or(std::string{});
                    if (Relative.empty() ||
                        (SearchPath / DecodeContentPath(Relative)).lexically_normal() != FontPath.lexically_normal())
                        continue;
                    const auto Name   = Face["family"].value_or(std::string{});
                    const auto Weight = Face["weight"].value_or(0u);
                    if (Name.empty() || Weight < 100 || Weight > 900 || Weight % 100 != 0)
                        continue;
                    auto      *Archive = FindOrInsertFamily(Name);
                    const auto Style =
                        Face["italic"].value_or(false) ? FontStyleCategory::Italic : FontStyleCategory::Normal;
                    InsertVariant(*Archive, static_cast<FontWeightCategory>(Weight), Style, std::to_string(Weight),
                                  EncodeContentPath(FontPath),
                                  ToLowerString(FontPath.extension().string()) == ".otf" ? "OpenType" : "TrueType",
                                  static_cast<uint32_t>(ByteCount));
                    Catalogued = true;
                    break;
                }
            }
            if (!Catalogued)
                ClassifyFontFileName(EncodeContentPath(FontPath.filename()), EncodeContentPath(FontPath),
                                     static_cast<uint32_t>(ByteCount));
        }
        // Rebuild from actual files, never from stale descriptors referencing removed faces.
        for (const auto &Family : DiscoveredFamilies)
        {
            if (Family.Variants.empty())
                continue;
            const auto Directory = DecodeContentPath(Family.Variants.front().FilePath).parent_path();
            (void)WriteToml(EncodeContentPath(Directory / DecodeContentPath(Family.FamilyName + ".toml")), Family);
        }
        return !DiscoveredFamilies.empty();
    }
    catch (...)
    {
        Clear();
        return false;
    }
}

bool FontCodec::ScanEngineAndGameContent(std::string_view EngineContentPath, std::string_view GameContentPath) noexcept
{
    FontCodec  EngineFonts;
    FontCodec  GameFonts;
    const bool EngineResult = EngineFonts.ScanDirectory(EngineContentPath);
    const bool GameResult   = GameFonts.ScanDirectory(GameContentPath);
    Clear();
    for (const auto *Catalogue : {&EngineFonts, &GameFonts})
    {
        for (const auto &Family : Catalogue->QueryDiscoveredFamilies())
        {
            auto *Destination = FindOrInsertFamily(Family.FamilyName);
            for (const auto &Variant : Family.Variants)
                InsertVariant(*Destination, Variant.Weight, Variant.Style, Variant.VariantName, Variant.FilePath,
                              Variant.FileFormat, Variant.FileSize);
        }
    }
    return EngineResult || GameResult;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           LOOKUP & QUERY IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

const FontFamilyIndex *FontCodec::QueryFamily(std::string_view DesiredFamilyName) const noexcept
{
    std::string QueryNorm = NormalizeKey(DesiredFamilyName);
    for (const auto &Fam : DiscoveredFamilies)
    {
        if (NormalizeKey(Fam.FamilyName) == QueryNorm)
            return &Fam;
    }
    // Partial match fallback
    for (const auto &Fam : DiscoveredFamilies)
    {
        if (NormalizeKey(Fam.FamilyName).find(QueryNorm) != std::string::npos)
            return &Fam;
    }
    return nullptr;
}

const FontVariantIndex *FontCodec::QueryVariant(std::string_view DesiredFamilyName, FontWeightCategory DesiredWeight,
                                                FontStyleCategory DesiredStyle) const noexcept
{
    const FontFamilyIndex *Fam = QueryFamily(DesiredFamilyName);
    if (!Fam || Fam->Variants.empty())
        return nullptr;

    const FontVariantIndex *ExactMatch    = nullptr;
    const FontVariantIndex *ClosestWeight = nullptr;
    int32_t                 MinDistance   = 9999;

    for (const auto &Var : Fam->Variants)
    {
        if (Var.Weight == DesiredWeight && Var.Style == DesiredStyle)
        {
            ExactMatch = &Var;
            break;
        }

        int32_t Dist = std::abs(static_cast<int32_t>(Var.Weight) - static_cast<int32_t>(DesiredWeight));
        if (Dist < MinDistance)
        {
            MinDistance   = Dist;
            ClosestWeight = &Var;
        }
    }

    return ExactMatch ? ExactMatch : ClosestWeight;
}

} // namespace Frontier
