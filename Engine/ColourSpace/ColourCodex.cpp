/*============================================================================================================================================
                                                              COLOURCODEX.CPP
============================================================================================================================================*/
// 🎨 Implementation of Frontier::ColourSpace::ColourCodex — unified named alias resolution across solid pigments, ramps, blackbody & all colour spaces

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourCodex.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Frontier::ColourSpace
{

namespace
{

[[nodiscard]] float EvaluateRepresentativeSampleT(const ColourSourceProfile& Profile) noexcept
{
    // Solid pigments ignore t; gradients/fills/blackbody report a crisp midpoint representative swatch
    if (std::holds_alternative<BlackbodyThermalSource>(Profile.Source))
    {
        return 0.5f;
    }
    return 0.5f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONSTRUCTION & DEFAULTS
//------------------------------------------------------------------------------------------------------------------------

ColourCodex::ColourCodex()
{
    ResetToBuiltInAliases();
}

void ColourCodex::ResetToBuiltInAliases()
{
    Records.clear();

    // 1. Curated studio pigments (38 shipped pigments with natural short colour synonyms)
    struct SolidSeed
    {
        const char* Name;
        const char* Hex;
        const char* Series;
        const char* SynonymA;
        const char* SynonymB;
    };

    constexpr std::array<SolidSeed, 46> SolidSeeds = {{
        { "Fig Purple",       "#3B1F3A", "Botanical",            "Purple",    nullptr },
        { "Pineapple Yellow", "#F6B800", "Botanical",            "Yellow",    nullptr },
        { "Charcoal",         "#2B2D2F", "Neutral",              "Dark Gray", nullptr },
        { "Midnight",         "#191970", "Deep Blue",            "Navy",      nullptr },
        { "Royal Blue",       "#2454FF", "Blue",                 "Blue",      nullptr },
        { "Arctic",           "#F8FAFC", "Neutral",              "White",     nullptr },
        { "Strawberry",       "#E63956", "Botanical",            nullptr,     nullptr },
        { "Cherry",           "#990F2B", "Botanical",            "Dark Red",  nullptr },
        { "Banana",           "#FFE135", "Botanical",            nullptr,     nullptr },
        { "August Blaze",     "#FF7B00", "August Beige & Brown", "Orange",    nullptr },
        { "August Tangerine", "#FF8800", "August Beige & Brown", nullptr,     nullptr },
        { "August Marigold",  "#FF9500", "August Beige & Brown", nullptr,     nullptr },
        { "August Honey",     "#FFA200", "August Beige & Brown", nullptr,     nullptr },
        { "August Amber",     "#FFAA00", "August Beige & Brown", nullptr,     nullptr },
        { "August Saffron",   "#FFB700", "August Beige & Brown", nullptr,     nullptr },
        { "August Sunbeam",   "#FFC300", "August Beige & Brown", nullptr,     nullptr },
        { "August Gold",      "#FFD000", "August Beige & Brown", "Gold",      nullptr },
        { "August Canary",    "#FFDD00", "August Beige & Brown", nullptr,     nullptr },
        { "August Lemon",     "#FFEA00", "August Beige & Brown", nullptr,     nullptr },
        { "Coral Pink",       "#FF6F7D", "Botanical",            "Pink",      "Coral" },
        { "Raspberry",        "#D72660", "Botanical",            "Magenta",   nullptr },
        { "Crimson",          "#B81D24", "Mineral",              "Red",       nullptr },
        { "Plum",             "#58284F", "Botanical",            nullptr,     nullptr },
        { "Lavender",         "#A78BFA", "Botanical",            "Violet",    nullptr },
        { "Tangerine",        "#F28500", "Botanical",            nullptr,     nullptr },
        { "Amber",            "#F59E0B", "Mineral",              nullptr,     nullptr },
        { "Terracotta",       "#C85A32", "Mineral",              "Brown",     nullptr },
        { "Sandstone",        "#C9B49A", "Mineral",              "Beige",     "Tan"   },
        { "Matcha",           "#74A12E", "Botanical",            "Green",     nullptr },
        { "Forest Pine",      "#1B4332", "Botanical",            "Dark Green",nullptr },
        { "Teal",             "#0D9488", "Mineral",              "Turquoise", nullptr },
        { "Cobalt",           "#1E40AF", "Blue",                 nullptr,     nullptr },
        { "Sapphire",         "#0F52BA", "Blue",                 nullptr,     nullptr },
        { "Glacier Blue",     "#7DD3FC", "Blue",                 "Cyan",      "Sky Blue" },
        { "Obsidian",         "#141518", "Neutral",              "Black",     nullptr },
        { "Graphite",         "#474B52", "Neutral",              "Gray",      "Grey"  },
        { "Slate",            "#5B6770", "Neutral",              nullptr,     nullptr },
        { "Ivory",            "#F5F1E6", "Neutral",              "Cream",     nullptr },
        // Canonical primary technical primaries (exact RGB corners)
        { "Pure Red",         "#FF0000", "Primary",              "PrimaryRed",     nullptr },
        { "Pure Green",       "#00FF00", "Primary",              "PrimaryGreen",   nullptr },
        { "Pure Blue",        "#0000FF", "Primary",              "PrimaryBlue",    nullptr },
        { "Pure Cyan",        "#00FFFF", "Primary",              "PrimaryCyan",    nullptr },
        { "Pure Magenta",     "#FF00FF", "Primary",              "PrimaryMagenta", nullptr },
        { "Pure Yellow",      "#FFFF00", "Primary",              "PrimaryYellow",  nullptr },
        { "Pure White",       "#FFFFFF", "Primary",              "PrimaryWhite",   nullptr },
        { "Pure Black",       "#000000", "Primary",              "PrimaryBlack",   nullptr },
    }};

    for (const SolidSeed& Seed : SolidSeeds)
    {
        std::vector<std::string> Synonyms {};
        if (Seed.SynonymA != nullptr) Synonyms.emplace_back(Seed.SynonymA);
        if (Seed.SynonymB != nullptr) Synonyms.emplace_back(Seed.SynonymB);
        (void)RegisterSolidHexAlias(Seed.Name, Seed.Hex, Seed.Series, std::move(Synonyms), true);
    }

    // 2. Built-in multi-stop colour ramps & studio gradients (MultiStopGradientSource)
    for (const ColourRampPresetEntry& Ramp : QueryBuiltInColourRampPresets())
    {
        (void)SyncRampPreset(Ramp);
    }

    // 3. Physical Planckian blackbody illuminants (BlackbodyThermalSource)
    (void)RegisterBlackbodyAlias("Candlelight",  1900.0f, "Illuminant", { "Candle", "1900K" },             true);
    (void)RegisterBlackbodyAlias("Tungsten",     3200.0f, "Illuminant", { "Incandescent", "Warm White" },  true);
    (void)RegisterBlackbodyAlias("Daylight D50", 5000.0f, "Illuminant", { "D50", "Horizon Daylight" },     true);
    (void)RegisterBlackbodyAlias("Daylight D65", 6504.0f, "Illuminant", { "D65", "Daylight", "Noon Sun" }, true);
    (void)RegisterBlackbodyAlias("Overcast Sky", 7500.0f, "Illuminant", { "Cool Daylight", "Shade" },      true);
    (void)RegisterBlackbodyAlias("Blue Arc",    10000.0f, "Illuminant", { "Xenon Arc", "Electric Arc" },   true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              REGISTRATION & SYNC
//------------------------------------------------------------------------------------------------------------------------

ColourAliasRecord ColourCodex::ConstructRecord(
    std::string_view         PrimaryAlias,
    ColourSourceProfile      Profile,
    std::string_view         SeriesTitle,
    std::vector<std::string> AlternateAliases,
    bool                     ReadOnlyEngineAlias)
{
    ColourAliasRecord Record {};
    Record.PrimaryAlias        = std::string(PrimaryAlias);
    Record.SeriesTitle         = SeriesTitle.empty() ? std::string("Project") : std::string(SeriesTitle);
    Record.AlternateAliases    = std::move(AlternateAliases);
    Record.SourceProfile       = std::move(Profile);
    Record.ReadOnlyEngineAlias = ReadOnlyEngineAlias;

    const float SampleT          = EvaluateRepresentativeSampleT(Record.SourceProfile);
    Record.RepresentativeLinear  = SampleColourSource(Record.SourceProfile, { SampleT });
    Record.RepresentativeSrgb8   = ConvertLinearToSrgb8(Record.RepresentativeLinear, TransferCurveCategory::PiecewiseSrgb);
    Record.RepresentativeOklab   = ConvertLinearSrgbToOklab(Record.RepresentativeLinear);
    Record.RepresentativeHsl     = ConvertSrgb8ToHsl(Record.RepresentativeSrgb8);
    Record.RepresentativeCmyk    = ConvertSrgb8ToCmyk(Record.RepresentativeSrgb8);
    Record.HexUpper              = FormatHexUpper(Record.RepresentativeSrgb8);
    Record.RelativeLuminance     = ComputeWcagRelativeLuminance(Record.RepresentativeSrgb8);
    return Record;
}

std::size_t ColourCodex::RegisterSourceAlias(
    std::string_view         PrimaryAlias,
    ColourSourceProfile      Profile,
    std::string_view         SeriesTitle,
    std::vector<std::string> AlternateAliases,
    bool                     ReadOnlyEngineAlias)
{
    const std::string Key = NormalizeAliasKey(PrimaryAlias);
    for (std::size_t Index = 0u; Index < Records.size(); ++Index)
    {
        if (NormalizeAliasKey(Records[Index].PrimaryAlias) == Key)
        {
            if (Records[Index].ReadOnlyEngineAlias && !ReadOnlyEngineAlias)
            {
                return Index;
            }
            Records[Index] = ConstructRecord(
                PrimaryAlias,
                std::move(Profile),
                SeriesTitle,
                std::move(AlternateAliases),
                ReadOnlyEngineAlias);
            return Index;
        }
    }

    Records.push_back(ConstructRecord(
        PrimaryAlias,
        std::move(Profile),
        SeriesTitle,
        std::move(AlternateAliases),
        ReadOnlyEngineAlias));
    return Records.size() - 1u;
}

std::size_t ColourCodex::RegisterSolidHexAlias(
    std::string_view         PrimaryAlias,
    std::string_view         HexNotation,
    std::string_view         SeriesTitle,
    std::vector<std::string> AlternateAliases,
    bool                     ReadOnlyEngineAlias)
{
    SrgbRgba8 ParsedSrgb { 255u, 255u, 255u, 255u };
    (void)TryParseHexSrgb(HexNotation, ParsedSrgb);
    const LinearRgba LinearTint = ConvertSrgb8ToLinear(ParsedSrgb, TransferCurveCategory::PiecewiseSrgb);

    ColourSourceProfile Profile {};
    Profile.Source = SolidPigmentSource { LinearTint };
    return RegisterSourceAlias(PrimaryAlias, std::move(Profile), SeriesTitle, std::move(AlternateAliases), ReadOnlyEngineAlias);
}

std::size_t ColourCodex::RegisterSolidLinearAlias(
    std::string_view         PrimaryAlias,
    LinearRgba               LinearTint,
    std::string_view         SeriesTitle,
    std::vector<std::string> AlternateAliases,
    bool                     ReadOnlyEngineAlias)
{
    ColourSourceProfile Profile {};
    Profile.Source = SolidPigmentSource { LinearTint };
    return RegisterSourceAlias(PrimaryAlias, std::move(Profile), SeriesTitle, std::move(AlternateAliases), ReadOnlyEngineAlias);
}

std::size_t ColourCodex::RegisterBlackbodyAlias(
    std::string_view         PrimaryAlias,
    float                    KelvinTemperature,
    std::string_view         SeriesTitle,
    std::vector<std::string> AlternateAliases,
    bool                     ReadOnlyEngineAlias)
{
    const float ClampedK = std::clamp(KelvinTemperature, 600.0f, 40000.0f);
    ColourSourceProfile Profile {};
    Profile.Source = BlackbodyThermalSource { ClampedK, ClampedK, 1.0f, 1.0f };
    return RegisterSourceAlias(PrimaryAlias, std::move(Profile), SeriesTitle, std::move(AlternateAliases), ReadOnlyEngineAlias);
}

std::size_t ColourCodex::SyncRampPreset(const ColourRampPresetEntry& Preset)
{
    std::vector<std::string> Synonyms {};
    if (!Preset.Identifier.empty())
    {
        Synonyms.push_back(Preset.Identifier);
    }
    if (Preset.Identifier == "rainbow")
    {
        Synonyms.emplace_back("Spectrum");
        Synonyms.emplace_back("Rainbow Ramp");
    }
    else if (Preset.Identifier == "thermal")
    {
        Synonyms.emplace_back("Blackbody");
        Synonyms.emplace_back("Fire");
        Synonyms.emplace_back("Heat");
    }
    else if (Preset.Identifier == "sunset")
    {
        Synonyms.emplace_back("Sunset Neon");
    }
    else if (Preset.Identifier == "viridis")
    {
        Synonyms.emplace_back("Viridis Scientific");
    }
    else if (Preset.Identifier == "magma")
    {
        Synonyms.emplace_back("Magma Thermal");
    }
    else if (Preset.Identifier == "grayscale")
    {
        Synonyms.emplace_back("Linear Luminance");
        Synonyms.emplace_back("Monochrome");
    }

    const std::string Primary = Preset.DisplayTitle.empty() ? Preset.Identifier : Preset.DisplayTitle;
    return RegisterSourceAlias(
        Primary,
        ConstructProfileFromRampPreset(Preset),
        Preset.SeriesTitle.empty() ? std::string("Ramp") : Preset.SeriesTitle,
        std::move(Synonyms),
        Preset.ReadOnlyEngineEntry);
}

bool ColourCodex::RemoveCustomAlias(std::string_view AliasToken)
{
    const std::size_t Index = FindAliasIndex(AliasToken);
    if (Index >= Records.size() || Records[Index].ReadOnlyEngineAlias)
    {
        return false;
    }
    Records.erase(Records.begin() + static_cast<std::ptrdiff_t>(Index));
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    QUERIES
//------------------------------------------------------------------------------------------------------------------------

std::span<const ColourAliasRecord> ColourCodex::QueryAliases() const noexcept
{
    return Records;
}

const ColourAliasRecord* ColourCodex::QueryAliasAt(std::size_t Index) const noexcept
{
    return (Index < Records.size()) ? &Records[Index] : nullptr;
}

std::size_t ColourCodex::FindAliasIndex(std::string_view AliasToken) const noexcept
{
    const std::string QueryKey = NormalizeAliasKey(AliasToken);
    if (QueryKey.empty())
    {
        return static_cast<std::size_t>(-1);
    }

    // Pass 1: exact normalized match on PrimaryAlias (highest priority)
    for (std::size_t Index = 0u; Index < Records.size(); ++Index)
    {
        if (NormalizeAliasKey(Records[Index].PrimaryAlias) == QueryKey)
        {
            return Index;
        }
    }

    // Pass 2: exact normalized match on AlternateAliases (synonyms & preset IDs like "Blue", "rainbow", "D65")
    for (std::size_t Index = 0u; Index < Records.size(); ++Index)
    {
        for (const std::string& Alt : Records[Index].AlternateAliases)
        {
            if (NormalizeAliasKey(Alt) == QueryKey)
            {
                return Index;
            }
        }
    }
    return static_cast<std::size_t>(-1);
}

const ColourAliasRecord* ColourCodex::FindAlias(std::string_view AliasToken) const noexcept
{
    const std::size_t Index = FindAliasIndex(AliasToken);
    return (Index < Records.size()) ? &Records[Index] : nullptr;
}

std::vector<std::size_t> ColourCodex::QueryIndicesByCategory(ColourSourceCategory Category) const
{
    std::vector<std::size_t> Result {};
    for (std::size_t Index = 0u; Index < Records.size(); ++Index)
    {
        if (Records[Index].QuerySourceCategory() == Category)
        {
            Result.push_back(Index);
        }
    }
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        UNIVERSAL MULTI-FORMAT RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

bool ColourCodex::ResolveAliasToProfile(std::string_view Token, ColourSourceProfile& OutProfile) const
{
    // 1. Direct alias lookup across all modular categories (Solid, Fill, Multi-Stop Gradient, Quantized, Blackbody, Discrete, SpatialNoise)
    if (const ColourAliasRecord* Found = FindAlias(Token))
    {
        OutProfile = Found->SourceProfile;
        return true;
    }

    // 2. Literal Hex token ("#2454FF", "#FF8800AA", "#F60")
    SrgbRgba8 ParsedHex {};
    if (!Token.empty() && Token.front() == '#' && TryParseHexSrgb(Token, ParsedHex))
    {
        OutProfile        = {};
        OutProfile.Source = SolidPigmentSource { ConvertSrgb8ToLinear(ParsedHex, TransferCurveCategory::PiecewiseSrgb) };
        return true;
    }

    // 3. Literal Kelvin temperature token ("3200K", "6500K", "1900 K")
    float Kelvin = 0.0f;
    if (TryParseKelvinToken(Token, Kelvin))
    {
        OutProfile        = {};
        OutProfile.Source = BlackbodyThermalSource { Kelvin, Kelvin, 1.0f, 1.0f };
        return true;
    }

    return false;
}

bool ColourCodex::ResolveAliasToLinear(std::string_view Token, LinearRgba& OutLinear, float SampleT) const
{
    ColourSourceProfile Profile {};
    if (!ResolveAliasToProfile(Token, Profile))
    {
        return false;
    }
    OutLinear = SampleColourSource(Profile, { SampleT });
    return true;
}

bool ColourCodex::ResolveAliasInPrimaries(
    std::string_view      Token,
    ChromaticityPrimaries TargetPrimaries,
    LinearRgba&           OutTint,
    float                 SampleT) const
{
    LinearRgba LinearRec709 {};
    if (!ResolveAliasToLinear(Token, LinearRec709, SampleT))
    {
        return false;
    }
    OutTint = ConvertPrimaries(LinearRec709, ChromaticityPrimaries::LinearSrgbRec709, TargetPrimaries);
    return true;
}

bool ColourCodex::ResolveAliasToSrgb8(
    std::string_view      Token,
    SrgbRgba8&            OutSrgb8,
    float                 SampleT,
    TransferCurveCategory Curve) const
{
    LinearRgba LinearRec709 {};
    if (!ResolveAliasToLinear(Token, LinearRec709, SampleT))
    {
        return false;
    }
    OutSrgb8 = ConvertLinearToSrgb8(LinearRec709, Curve);
    return true;
}

bool ColourCodex::ResolveAliasToOklab(std::string_view Token, OklabTriplet& OutOklab, float SampleT) const
{
    LinearRgba LinearRec709 {};
    if (!ResolveAliasToLinear(Token, LinearRec709, SampleT))
    {
        return false;
    }
    OutOklab = ConvertLinearSrgbToOklab(LinearRec709);
    return true;
}

bool ColourCodex::ResolveAliasToOklch(std::string_view Token, OklchTriplet& OutOklch, float SampleT) const
{
    OklabTriplet Lab {};
    if (!ResolveAliasToOklab(Token, Lab, SampleT))
    {
        return false;
    }
    OutOklch = ConvertOklabToOklch(Lab);
    return true;
}

bool ColourCodex::ResolveAliasToHsl(std::string_view Token, HslTriplet& OutHsl, float SampleT) const
{
    SrgbRgba8 Srgb {};
    if (!ResolveAliasToSrgb8(Token, Srgb, SampleT))
    {
        return false;
    }
    OutHsl = ConvertSrgb8ToHsl(Srgb);
    return true;
}

bool ColourCodex::ResolveAliasToCmyk(std::string_view Token, CmykQuadruplet& OutCmyk, float SampleT) const
{
    SrgbRgba8 Srgb {};
    if (!ResolveAliasToSrgb8(Token, Srgb, SampleT))
    {
        return false;
    }
    OutCmyk = ConvertSrgb8ToCmyk(Srgb);
    return true;
}

bool ColourCodex::ResolveAliasToSpectrum(
    std::string_view      Token,
    SpectralCurveProfile& OutSpectrum,
    float                 SampleT) const
{
    ColourSourceProfile Profile {};
    if (!ResolveAliasToProfile(Token, Profile))
    {
        return false;
    }

    if (const auto* Thermal = std::get_if<BlackbodyThermalSource>(&Profile.Source))
    {
        const float ClampedT = std::clamp(SampleT, 0.0f, 1.0f);
        const float Kelvin   = Thermal->HotKelvin + (Thermal->ColdKelvin - Thermal->HotKelvin) * ClampedT;
        OutSpectrum          = ConstructBlackbodySpectrum(Kelvin);
        return true;
    }

    const LinearRgba Linear = SampleColourSource(Profile, { SampleT });
    OutSpectrum             = ConstructSpectrumFromLinearRgb(Linear);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            NORMALIZATION UTILITIES
//------------------------------------------------------------------------------------------------------------------------

std::string ColourCodex::NormalizeAliasKey(std::string_view Text)
{
    std::string Key {};
    Key.reserve(Text.size());
    for (char Ch : Text)
    {
        if (std::isalnum(static_cast<unsigned char>(Ch)))
        {
            Key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(Ch))));
        }
    }
    return Key;
}

bool ColourCodex::TryParseKelvinToken(std::string_view Token, float& OutKelvin) noexcept
{
    while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.front()))) Token.remove_prefix(1u);
    while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.back())))  Token.remove_suffix(1u);
    if (Token.size() < 2u || (Token.back() != 'K' && Token.back() != 'k'))
    {
        return false;
    }
    Token.remove_suffix(1u);
    while (!Token.empty() && std::isspace(static_cast<unsigned char>(Token.back()))) Token.remove_suffix(1u);
    if (Token.empty())
    {
        return false;
    }

    std::string Numeric(Token);
    char* EndPtr = nullptr;
    const float Parsed = std::strtof(Numeric.c_str(), &EndPtr);
    if (EndPtr == nullptr || *EndPtr != '\0' || !std::isfinite(Parsed) || Parsed < 600.0f || Parsed > 40000.0f)
    {
        return false;
    }
    OutKelvin = Parsed;
    return true;
}

} // namespace Frontier::ColourSpace
