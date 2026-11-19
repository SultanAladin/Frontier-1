/*============================================================================================================================================
                                                             FLUIDPRESETS.H
============================================================================================================================================*/
// 🧩 Built-in Fire/Smoke/Explosion presets & blackbody color palette evaluator (ported from Experimental/FluidEditor)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/LinearColors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"

#include <span>
#include <string_view>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidPresetEntry
{
    std::string_view Key         {};
    std::string_view Title       {};
    std::string_view Category    {}; // "fire", "blast", "smoke", "dust", "plasma"
    std::string_view Summary     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLUID PRESET LIBRARY
//------------------------------------------------------------------------------------------------------------------------

class FluidPresets
{
public:
    [[nodiscard]] static std::span<const FluidPresetEntry> ListPresets() noexcept;

    [[nodiscard]] static FluidSceneDocument ConstructPreset(std::string_view PresetKey) noexcept;

    [[nodiscard]] static Mathematics::LinearRGB EvaluateBlackbodyPalette(
        float             RawTemperature,
        float             TemperatureScale,
        FluidColorScheme Palette) noexcept;

    [[nodiscard]] static std::string_view QueryPaletteLabel(FluidColorScheme Palette) noexcept;
    [[nodiscard]] static std::string_view QueryObstacleLabel(FluidObstacleType Type) noexcept;
    [[nodiscard]] static std::string_view QueryChannelLabel(FluidRenderChannel Channel) noexcept;
};

} // namespace Frontier::SimulationSpace::FluidSpace
