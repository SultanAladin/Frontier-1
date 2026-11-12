/*============================================================================================================================================
                                                           TYPOGRAPHYREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Discover and load .FontSpace typeface weights into Dear ImGui with CSS weight fallback resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"

#include <imgui.h>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Frontier
{

TypographyRepository* TypographyRepository::ActiveRepository = nullptr;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr FontWeightClass OrderedWeights[TypographyRepository::WeightCount] = {
    FontWeightClass::Thin,
    FontWeightClass::ExtraLight,
    FontWeightClass::Light,
    FontWeightClass::Regular,
    FontWeightClass::Medium,
    FontWeightClass::SemiBold,
    FontWeightClass::Bold,
    FontWeightClass::ExtraBold,
    FontWeightClass::Black,
};

constexpr ImWchar ExtendedUnicodeRanges[] = {
    0x0020, 0x00FF,
    0x2000, 0x206F,
    0x2190, 0x21FF,
    0x2200, 0x22FF,
    0x25A0, 0x25FF,
    0,
};

//------------------------------------------------------------------------------------------------------------------------
//                                              STRING & WEIGHT QUANTIZATION
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::string LowercaseAscii(std::string_view Input)
{
    std::string Lowered{};
    Lowered.reserve(Input.size());
    for (unsigned char Character : Input)
    {
        Lowered.push_back(static_cast<char>(std::tolower(Character)));
    }
    return Lowered;
}

[[nodiscard]] std::string NormalizeGroupKey(std::string_view Input)
{
    std::string Key{};
    for (unsigned char Character : Input)
    {
        if (std::isalnum(Character))
        {
            Key.push_back(static_cast<char>(std::tolower(Character)));
        }
    }
    return Key;
}

[[nodiscard]] FontWeightClass QuantizeWeight(int NumericWeight) noexcept
{
    const int Clamped = std::clamp((NumericWeight + 50) / 100, 1, 9) * 100;
    return static_cast<FontWeightClass>(static_cast<uint16_t>(Clamped));
}

[[nodiscard]] FontWeightClass ClassifyWeightFromFilename(std::string_view Stem, bool& ItalicDetected)
{
    const std::string Lower = LowercaseAscii(Stem);
    ItalicDetected          = Lower.find("italic") != std::string::npos || Lower.find("oblique") != std::string::npos;

    if (Lower.find("thin") != std::string::npos || Lower.find("hairline") != std::string::npos)
    {
        return FontWeightClass::Thin;
    }
    if (Lower.find("extralight") != std::string::npos || Lower.find("ultralight") != std::string::npos)
    {
        return FontWeightClass::ExtraLight;
    }
    if (Lower.find("light") != std::string::npos)
    {
        return FontWeightClass::Light;
    }
    if (Lower.find("semibold") != std::string::npos || Lower.find("demibold") != std::string::npos)
    {
        return FontWeightClass::SemiBold;
    }
    if (Lower.find("extrabold") != std::string::npos || Lower.find("ultrabold") != std::string::npos)
    {
        return FontWeightClass::ExtraBold;
    }
    if (Lower.find("black") != std::string::npos || Lower.find("heavy") != std::string::npos)
    {
        return FontWeightClass::Black;
    }
    if (Lower.find("bold") != std::string::npos)
    {
        return FontWeightClass::Bold;
    }
    if (Lower.find("medium") != std::string::npos)
    {
        return FontWeightClass::Medium;
    }
    return FontWeightClass::Regular;
}

[[nodiscard]] bool QueryMonospacedGroup(std::string_view Name)
{
    return LowercaseAscii(Name).find("mono") != std::string::npos;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                         REPOSITORY LOADING & WEIGHT RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

ImFont* TypographyRepository::LoadFontFile(const std::filesystem::path& FontPath, float PixelHeight) noexcept
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return nullptr;
    }

    std::error_code IoFailure{};
    if (!std::filesystem::is_regular_file(FontPath, IoFailure))
    {
        return nullptr;
    }

    ImFontConfig Configuration{};
    Configuration.OversampleH = 3;
    Configuration.OversampleV = 2;
    Configuration.GlyphRanges = ExtendedUnicodeRanges;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(FontPath.string().c_str(), PixelHeight, &Configuration);
}

uint32_t TypographyRepository::LoadRepository(const std::filesystem::path& RepositoryRoot) noexcept
{
    if (Loaded)
    {
        return QueryGroupCount();
    }
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return 0u;
    }

    std::error_code IoFailure{};
    if (!std::filesystem::exists(RepositoryRoot, IoFailure))
    {
        Loaded = true;
        return 0u;
    }

    struct DiscoveredFace
    {
        FontWeightClass       Weight = FontWeightClass::Regular; // [-] - CSS numeric weight class
        std::filesystem::path Path   {};                         // [-] - Resolved font file path
    };

    struct DiscoveredGroup
    {
        std::string                 Key         {}; // [-] - Normalized lookup key
        std::string                 DisplayName {}; // [-] - Human-readable typeface label
        std::vector<DiscoveredFace> Faces       {}; // [-] - Discovered upright faces
    };

    std::vector<DiscoveredGroup> DiscoveredGroups{};

    auto AppendFace = [&](const std::string& GroupLabel, FontWeightClass Weight, const std::filesystem::path& Path)
    {
        const std::string Key = NormalizeGroupKey(GroupLabel);
        for (DiscoveredGroup& Existing : DiscoveredGroups)
        {
            if (Existing.Key == Key)
            {
                Existing.Faces.push_back({ Weight, Path });
                return;
            }
        }
        DiscoveredGroup Created{};
        Created.Key         = Key;
        Created.DisplayName = GroupLabel;
        Created.Faces.push_back({ Weight, Path });
        DiscoveredGroups.push_back(std::move(Created));
    };

    const std::filesystem::path ManifestPath = RepositoryRoot / "FontRepository.FontSpace";
    bool                        Parsed       = false;
    if (std::filesystem::exists(ManifestPath, IoFailure))
    {
        try
        {
            const toml::table Document = toml::parse_file(ManifestPath.string());
            if (const auto* EntriesArray = Document["TypefaceEntries"].as_array())
            {
                for (const auto& Entry : *EntriesArray)
                {
                    const auto* Row = Entry.as_table();
                    if (Row == nullptr)
                    {
                        continue;
                    }
                    if ((*Row)["ItalicStyle"].value_or(false))
                    {
                        continue;
                    }
                    const std::string GroupLabel   = (*Row)["TypefaceGroup"].value_or(std::string{});
                    const std::string RelativePath = (*Row)["RelativePath"].value_or(std::string{});
                    const int         WeightNumber = (*Row)["WeightClass"].value_or(400);
                    if (GroupLabel.empty() || RelativePath.empty())
                    {
                        continue;
                    }
                    AppendFace(GroupLabel, QuantizeWeight(WeightNumber), RepositoryRoot / RelativePath);
                }
                Parsed = !DiscoveredGroups.empty();
            }
        }
        catch (...)
        {
            Parsed = false;
            DiscoveredGroups.clear();
        }
    }

    if (!Parsed)
    {
        for (const auto& DirectoryEntry : std::filesystem::recursive_directory_iterator(RepositoryRoot, IoFailure))
        {
            if (IoFailure || !DirectoryEntry.is_regular_file())
            {
                continue;
            }
            const std::string Extension = LowercaseAscii(DirectoryEntry.path().extension().string());
            if (Extension != ".ttf" && Extension != ".otf")
            {
                continue;
            }
            bool                  Italic = false;
            const FontWeightClass Weight = ClassifyWeightFromFilename(DirectoryEntry.path().stem().string(), Italic);
            if (Italic)
            {
                continue;
            }
            const std::string GroupLabel = DirectoryEntry.path().parent_path().filename().string();
            AppendFace(GroupLabel, Weight, DirectoryEntry.path());
        }
    }

    std::sort(
        DiscoveredGroups.begin(),
        DiscoveredGroups.end(),
        [](const DiscoveredGroup& Left, const DiscoveredGroup& Right)
        {
            if (Left.Key == "inter" && Right.Key != "inter")
            {
                return true;
            }
            if (Right.Key == "inter" && Left.Key != "inter")
            {
                return false;
            }
            return Left.DisplayName < Right.DisplayName;
        });

    for (const DiscoveredGroup& Discovered : DiscoveredGroups)
    {
        TypefaceGroup Group{};
        Group.Name       = Discovered.DisplayName;
        Group.Monospaced = QueryMonospacedGroup(Group.Name);

        for (FontWeightClass TargetWeight : OrderedWeights)
        {
            for (const DiscoveredFace& Candidate : Discovered.Faces)
            {
                if (Candidate.Weight != TargetWeight)
                {
                    continue;
                }
                if (ImFont* LoadedFont = LoadFontFile(Candidate.Path))
                {
                    Group.Faces.push_back({ TargetWeight, LoadedFont });
                    break;
                }
            }
        }
        if (!Group.Faces.empty())
        {
            Groups.push_back(std::move(Group));
        }
    }

    std::filesystem::path PreferredTabFontPath{};
    std::filesystem::path FallbackTabFontPath{};
    for (const DiscoveredGroup& Discovered : DiscoveredGroups)
    {
        for (const DiscoveredFace& Candidate : Discovered.Faces)
        {
            if (Candidate.Weight == FontWeightClass::Regular)
            {
                if (FallbackTabFontPath.empty())
                {
                    FallbackTabFontPath = Candidate.Path;
                }
                if (Discovered.Key == "dmsans")
                {
                    PreferredTabFontPath = Candidate.Path;
                }
            }
        }
    }
    const std::filesystem::path ChosenTabFontPath =
        !PreferredTabFontPath.empty() ? PreferredTabFontPath : FallbackTabFontPath;
    if (!ChosenTabFontPath.empty())
    {
        TabStripFont = LoadFontFile(ChosenTabFontPath, 13.0f);
    }

    Loaded = !Groups.empty();
    return QueryGroupCount();
}

const TypefaceGroup* TypographyRepository::QueryGroup(uint32_t GroupIndex) const noexcept
{
    return GroupIndex < Groups.size() ? &Groups[GroupIndex] : nullptr;
}

int32_t TypographyRepository::ResolveGroupIndex(std::string_view Name) const noexcept
{
    const std::string Key = NormalizeGroupKey(Name);
    for (std::size_t Index = 0u; Index < Groups.size(); ++Index)
    {
        if (NormalizeGroupKey(Groups[Index].Name) == Key)
        {
            return static_cast<int32_t>(Index);
        }
    }
    return -1;
}

bool TypographyRepository::ContainsWeight(uint32_t GroupIndex, FontWeightClass Weight) const noexcept
{
    const TypefaceGroup* Group = QueryGroup(GroupIndex);
    if (Group == nullptr)
    {
        return false;
    }
    for (const TypefaceWeightEntry& Face : Group->Faces)
    {
        if (Face.Weight == Weight)
        {
            return true;
        }
    }
    return false;
}

const TypefaceWeightEntry* TypographyRepository::ResolveWeight(uint32_t GroupIndex, FontWeightClass Wanted) const noexcept
{
    const TypefaceGroup* Group = QueryGroup(GroupIndex);
    if (Group == nullptr || Group->Faces.empty())
    {
        return nullptr;
    }

    const int                  WantedNumeric = static_cast<int>(Wanted);
    const TypefaceWeightEntry* Best          = nullptr;
    int                        BestDelta     = 10000;

    for (const TypefaceWeightEntry& Face : Group->Faces)
    {
        const int CurrentNumeric = static_cast<int>(Face.Weight);
        const int Delta          = std::abs(CurrentNumeric - WantedNumeric);
        if (Best == nullptr || Delta < BestDelta)
        {
            Best      = &Face;
            BestDelta = Delta;
            continue;
        }
        if (Delta == BestDelta)
        {
            const int  BestNumeric   = static_cast<int>(Best->Weight);
            const bool PreferHeavier = WantedNumeric >= 500;
            if ((PreferHeavier && CurrentNumeric > BestNumeric) ||
                (!PreferHeavier && CurrentNumeric < BestNumeric))
            {
                Best = &Face;
            }
        }
    }
    return Best;
}

ImFont* TypographyRepository::QueryFont(uint32_t GroupIndex, FontWeightClass Wanted) const noexcept
{
    if (const TypefaceWeightEntry* Entry = ResolveWeight(GroupIndex, Wanted))
    {
        return Entry->Font;
    }
    return nullptr;
}

const char* TypographyRepository::WeightLabel(FontWeightClass Weight) noexcept
{
    switch (Weight)
    {
        case FontWeightClass::Thin:       return "Thin";
        case FontWeightClass::ExtraLight: return "ExtraLight";
        case FontWeightClass::Light:      return "Light";
        case FontWeightClass::Regular:    return "Regular";
        case FontWeightClass::Medium:     return "Medium";
        case FontWeightClass::SemiBold:   return "SemiBold";
        case FontWeightClass::Bold:       return "Bold";
        case FontWeightClass::ExtraBold:  return "ExtraBold";
        case FontWeightClass::Black:      return "Black";
    }
    return "Regular";
}

FontWeightClass TypographyRepository::WeightAtOrdinal(uint32_t Ordinal) noexcept
{
    return Ordinal < WeightCount ? OrderedWeights[Ordinal] : FontWeightClass::Regular;
}

uint32_t TypographyRepository::WeightOrdinal(FontWeightClass Weight) noexcept
{
    for (uint32_t Ordinal = 0u; Ordinal < WeightCount; ++Ordinal)
    {
        if (OrderedWeights[Ordinal] == Weight)
        {
            return Ordinal;
        }
    }
    return 3u;
}

} // namespace Frontier
