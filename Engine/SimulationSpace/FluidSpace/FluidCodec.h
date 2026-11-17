/*============================================================================================================================================
                                                               FLUIDCODEC.H
============================================================================================================================================*/
// 🧩 Pure TOML serializer & deserializer for FluidSceneDocument (.fluid.toml & legacy Y-up .gasscene.toml conversion)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FLUID CODEC
//------------------------------------------------------------------------------------------------------------------------

class FluidCodec
{
public:
    static constexpr std::string_view FormatIdentifier = "frontier-fluid-scene";
    static constexpr int64_t          NativeVersion    = 2; // Version 2 = native +Z-Up Frontier Engine schema

    // Serializes FluidSceneDocument to canonical +Z-Up TOML text (never JSON)
    [[nodiscard]] static std::string SerializeSceneToml(const FluidSceneDocument& Document);

    // Parses TOML text (supports both native v2 +Z-Up and legacy v1 Y-Up .gasscene.toml)
    [[nodiscard]] static bool DeserializeSceneToml(
        std::string_view    TomlText,
        FluidSceneDocument& OutDocument,
        std::string*        OutError = nullptr);

    [[nodiscard]] static bool LoadSceneFile(
        const std::filesystem::path& FilePath,
        FluidSceneDocument&          OutDocument,
        std::string*                 OutError = nullptr);

    [[nodiscard]] static bool SaveSceneFile(
        const std::filesystem::path& FilePath,
        const FluidSceneDocument&    Document,
        std::string*                 OutError = nullptr);
};

} // namespace Frontier::SimulationSpace::FluidSpace
