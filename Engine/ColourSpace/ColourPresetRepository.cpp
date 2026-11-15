/*============================================================================================================================================
                                                          COLOURPRESETREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 Implementation of built-in Studio/Scientific/Utility ramp presets & runtime ColourPresetRepository

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourPresetRepository.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"

#include <algorithm>
#include <initializer_list>

namespace Frontier::ColourSpace
{

namespace
{

[[nodiscard]] ColourRampPresetEntry ConstructBuiltInPreset(
    std::string_view                   Identifier,
    std::string_view                   DisplayTitle,
    std::string_view                   SeriesTitle,
    std::initializer_list<const char*> HexStops,
    TransferCurveCategory              DecodeCurve = TransferCurveCategory::PureGamma22)
{
    ColourRampPresetEntry Entry {};
    Entry.Identifier          = std::string(Identifier);
    Entry.DisplayTitle        = std::string(DisplayTitle);
    Entry.SeriesTitle         = std::string(SeriesTitle);
    Entry.ReadOnlyEngineEntry = true;

    const std::size_t Count = HexStops.size();
    Entry.Stops.reserve(Count);
    std::size_t Index = 0u;
    for (const char* Hex : HexStops)
    {
        const float Position = (Count > 1u) ? static_cast<float>(Index) / static_cast<float>(Count - 1u) : 0.0f;
        Entry.Stops.push_back({ Position, ConvertHexSrgbToLinear(Hex, 1.0f, DecodeCurve) });
        ++Index;
    }
    return Entry;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              BUILT-IN STUDIO & SCIENTIFIC RAMPS
//------------------------------------------------------------------------------------------------------------------------

const std::vector<ColourRampPresetEntry>& QueryBuiltInColourRampPresets()
{
    static const std::vector<ColourRampPresetEntry> BuiltInPresets = {
        // 📝 Exact 6 HTML Particle Studio presets verbatim (decoded with PureGamma22 for bit-exact parity)
        ConstructBuiltInPreset("rainbow",   "Spectrum (7 stops)",          "Studio",     { "#ff3355", "#ff8822", "#ffe033", "#38e86b", "#22ccff", "#5566ff", "#d44cff" }),
        ConstructBuiltInPreset("thermal",   "Blackbody (6 stops)",         "Studio",     { "#140505", "#961408", "#f05a14", "#ffbe38", "#fff6c8", "#9be2ff" }),
        ConstructBuiltInPreset("aurora",    "Aurora (5 stops)",            "Studio",     { "#1a1464", "#10b981", "#38f2d6", "#a3ff4d", "#a855f7" }),
        ConstructBuiltInPreset("plasma",    "Plasma (6 stops)",            "Studio",     { "#0d0887", "#6a00a8", "#b12a90", "#e16462", "#fca636", "#f0f921" }),
        ConstructBuiltInPreset("ocean",     "Ocean (5 stops)",             "Studio",     { "#071e3d", "#134e6f", "#1e96a8", "#7be3d0", "#eefbff" }),
        ConstructBuiltInPreset("sunset",    "Sunset Neon (6 stops)",       "Studio",     { "#2b1055", "#9d1763", "#f0544f", "#ffad42", "#ffd57e", "#3b82f6" }),
        // 📝 Frontier scientific & utility perceptual ramps
        ConstructBuiltInPreset("viridis",   "Viridis Scientific (6 stops)","Scientific", { "#440154", "#414487", "#2a788e", "#22a884", "#7ad151", "#fde725" }),
        ConstructBuiltInPreset("magma",     "Magma Thermal (6 stops)",     "Scientific", { "#000004", "#3b0f70", "#8c2981", "#de4968", "#fe9f6d", "#fcfdbf" }),
        ConstructBuiltInPreset("grayscale", "Linear Luminance (2 stops)",  "Utility",    { "#000000", "#ffffff" }),
    };
    return BuiltInPresets;
}

const ColourRampPresetEntry* FindBuiltInColourRampPreset(std::string_view Identifier) noexcept
{
    const auto& Presets = QueryBuiltInColourRampPresets();
    for (const auto& Entry : Presets)
    {
        if (Entry.Identifier == Identifier)
        {
            return &Entry;
        }
    }
    return nullptr;
}

ColourSourceProfile ConstructProfileFromRampPreset(const ColourRampPresetEntry& Preset)
{
    ColourSourceProfile Profile {};
    MultiStopGradientSource Gradient {};
    Gradient.LinkedPresetIdentifier = Preset.Identifier;
    Gradient.Stops                  = Preset.Stops;
    Profile.Source                  = std::move(Gradient);
    Profile.Range                   = Preset.DefaultRange;
    Profile.BlendEquation           = Preset.DefaultBlend;
    return Profile;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              COLOUR PRESET REPOSITORY
//------------------------------------------------------------------------------------------------------------------------

ColourPresetRepository::ColourPresetRepository()
{
    ResetToBuiltInPresets();
}

void ColourPresetRepository::ResetToBuiltInPresets()
{
    RampEntries = QueryBuiltInColourRampPresets();
    SwatchEntries.clear();
}

std::size_t ColourPresetRepository::AddOrUpdateCustomRamp(ColourRampPresetEntry CustomEntry)
{
    CustomEntry.ReadOnlyEngineEntry = false;
    for (std::size_t Index = 0u; Index < RampEntries.size(); ++Index)
    {
        if (RampEntries[Index].Identifier == CustomEntry.Identifier && !RampEntries[Index].ReadOnlyEngineEntry)
        {
            RampEntries[Index] = std::move(CustomEntry);
            return Index;
        }
    }
    RampEntries.push_back(std::move(CustomEntry));
    return RampEntries.size() - 1u;
}

bool ColourPresetRepository::RemoveCustomRamp(std::string_view Identifier)
{
    const auto It = std::find_if(RampEntries.begin(), RampEntries.end(), [Identifier](const ColourRampPresetEntry& Entry) {
        return Entry.Identifier == Identifier && !Entry.ReadOnlyEngineEntry;
    });
    if (It == RampEntries.end())
    {
        return false;
    }
    RampEntries.erase(It);
    return true;
}

std::size_t ColourPresetRepository::AddOrUpdateCustomSwatch(NamedColourSwatchEntry CustomSwatch)
{
    CustomSwatch.ReadOnlyEngineEntry = false;
    for (std::size_t Index = 0u; Index < SwatchEntries.size(); ++Index)
    {
        if (SwatchEntries[Index].SwatchName == CustomSwatch.SwatchName && !SwatchEntries[Index].ReadOnlyEngineEntry)
        {
            SwatchEntries[Index] = std::move(CustomSwatch);
            return Index;
        }
    }
    SwatchEntries.push_back(std::move(CustomSwatch));
    return SwatchEntries.size() - 1u;
}

bool ColourPresetRepository::RemoveCustomSwatch(std::string_view SwatchName)
{
    const auto It = std::find_if(SwatchEntries.begin(), SwatchEntries.end(), [SwatchName](const NamedColourSwatchEntry& Entry) {
        return Entry.SwatchName == SwatchName && !Entry.ReadOnlyEngineEntry;
    });
    if (It == SwatchEntries.end())
    {
        return false;
    }
    SwatchEntries.erase(It);
    return true;
}

std::span<const ColourRampPresetEntry> ColourPresetRepository::QueryRampPresets() const noexcept
{
    return RampEntries;
}

const ColourRampPresetEntry* ColourPresetRepository::FindRampPreset(std::string_view Identifier) const noexcept
{
    for (const auto& Entry : RampEntries)
    {
        if (Entry.Identifier == Identifier)
        {
            return &Entry;
        }
    }
    return nullptr;
}

std::span<const NamedColourSwatchEntry> ColourPresetRepository::QuerySwatches() const noexcept
{
    return SwatchEntries;
}

const NamedColourSwatchEntry* ColourPresetRepository::FindSwatch(std::string_view SwatchName) const noexcept
{
    for (const auto& Entry : SwatchEntries)
    {
        if (Entry.SwatchName == SwatchName)
        {
            return &Entry;
        }
    }
    return nullptr;
}

} // namespace Frontier::ColourSpace
