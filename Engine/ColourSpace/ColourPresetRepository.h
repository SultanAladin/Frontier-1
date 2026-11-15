/*============================================================================================================================================
                                                           COLOURPRESETREPOSITORY.H
============================================================================================================================================*/
// 🧩 Engine-wide built-in & project colour ramp presets (all 6 HTML Studio ramps verbatim + scientific/utility sets) & named swatch entries

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourSourceTypes.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ColourSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ColourRampPresetEntry
{
    std::string             Identifier          {};     // [-] - Stable preset token (rainbow, thermal, aurora, plasma, ocean, sunset, ...)
    std::string             DisplayTitle        {};     // [-] - Human-readable title shown in ContentBrowser & ColourRampEditor
    std::string             SeriesTitle         {};     // [-] - Grouping caption (Studio, Scientific, Utility, Project)
    std::vector<ColourStop> Stops               {};     // [-] - Ordered linear-light RGBA stops in [0, 1]
    ColourRangeTransform    DefaultRange        {};     // [-] - Recommended wrap/curve/interpolation defaults
    OpticalBlendEquation    DefaultBlend        = OpticalBlendEquation::Additive; // [-] - Recommended optical blend equation
    bool                    ReadOnlyEngineEntry = true; // [-] - True for shipped read-only built-in presets
};

struct NamedColourSwatchEntry
{
    std::string SwatchName          {};                           // [-] - Semantic swatch alias
    std::string SeriesTitle         { "Custom" };                 // [-] - Collection caption
    LinearRgba  LinearTint          { 1.0f, 1.0f, 1.0f, 1.0f };   // [0-1+] - Scene-referred linear RGBA
    bool        ReadOnlyEngineEntry = false;                      // [-] - True for shipped built-in entries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] const std::vector<ColourRampPresetEntry>& QueryBuiltInColourRampPresets();
[[nodiscard]] const ColourRampPresetEntry*              FindBuiltInColourRampPreset(std::string_view Identifier) noexcept;
[[nodiscard]] ColourSourceProfile                       ConstructProfileFromRampPreset(const ColourRampPresetEntry& Preset);

class ColourPresetRepository
{
public:
    ColourPresetRepository();

    void ResetToBuiltInPresets();

    std::size_t AddOrUpdateCustomRamp(ColourRampPresetEntry CustomEntry);
    bool        RemoveCustomRamp(std::string_view Identifier);

    std::size_t AddOrUpdateCustomSwatch(NamedColourSwatchEntry CustomSwatch);
    bool        RemoveCustomSwatch(std::string_view SwatchName);

    [[nodiscard]] std::span<const ColourRampPresetEntry>  QueryRampPresets() const noexcept;
    [[nodiscard]] const ColourRampPresetEntry*            FindRampPreset(std::string_view Identifier) const noexcept;
    [[nodiscard]] std::span<const NamedColourSwatchEntry> QuerySwatches() const noexcept;
    [[nodiscard]] const NamedColourSwatchEntry*           FindSwatch(std::string_view SwatchName) const noexcept;

private:
    std::vector<ColourRampPresetEntry>  RampEntries   {}; // [-] - Built-in & project multi-stop ramp presets
    std::vector<NamedColourSwatchEntry> SwatchEntries {}; // [-] - Project & custom linear colour swatches
};

} // namespace Frontier::ColourSpace
