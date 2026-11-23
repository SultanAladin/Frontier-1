/*============================================================================================================================================
                                                           FORMATCODECSUITE.H
============================================================================================================================================*/
// 🧩 Typed Two-Tier Hybrid codecs and complete 63-format concrete example generator (including paused engine pillars)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Codecs/FormatSpecifications.h"
#include "Engine/Serialization/Container/HybridContainerCodec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Serialization
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FormatCodecSuite
{
public:
    // ① Configuration Family Codecs
    [[nodiscard]] static bool EncodeEngineSettings(
        const EngineSettingsDocument& Document,
        HybridContainerDocument&      OutContainer);

    [[nodiscard]] static bool DecodeEngineSettings(
        const HybridContainerDocument& Container,
        EngineSettingsDocument&        OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeRuntimeConfiguration(
        const RuntimeConfigurationDocument& Document,
        HybridContainerDocument&            OutContainer);

    [[nodiscard]] static bool DecodeRuntimeConfiguration(
        const HybridContainerDocument& Container,
        RuntimeConfigurationDocument&  OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodePlatformSettings(
        const PlatformSettingsDocument& Document,
        HybridContainerDocument&        OutContainer);

    [[nodiscard]] static bool DecodePlatformSettings(
        const HybridContainerDocument& Container,
        PlatformSettingsDocument&      OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeProjectIndex(
        const ProjectIndexDocument& Document,
        HybridContainerDocument&    OutContainer);

    [[nodiscard]] static bool DecodeProjectIndex(
        const HybridContainerDocument& Container,
        ProjectIndexDocument&          OutDocument,
        std::string&                   OutDiagnostic);

    // ② CameraOptics, Atmosphere, FractureNetwork & WorldSpace Codecs
    [[nodiscard]] static bool EncodeCameraOptics(
        const CameraOpticsDocument& Document,
        HybridContainerDocument&    OutContainer);

    [[nodiscard]] static bool DecodeCameraOptics(
        const HybridContainerDocument& Container,
        CameraOpticsDocument&          OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeAtmosphere(
        const AtmosphereDocument& Document,
        HybridContainerDocument&  OutContainer);

    [[nodiscard]] static bool DecodeAtmosphere(
        const HybridContainerDocument& Container,
        AtmosphereDocument&            OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeFractureNetwork(
        const FractureNetworkDocument& Document,
        HybridContainerDocument&       OutContainer);

    [[nodiscard]] static bool DecodeFractureNetwork(
        const HybridContainerDocument& Container,
        FractureNetworkDocument&       OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeWorldSpace(
        const WorldSpaceDocument& Document,
        HybridContainerDocument&  OutContainer);

    [[nodiscard]] static bool DecodeWorldSpace(
        const HybridContainerDocument& Container,
        WorldSpaceDocument&            OutDocument,
        std::string&                   OutDiagnostic);

    // ③ Universal Example Builder & On-Disk Generator for ALL 63 Frontier Formats (including paused pillars)
    [[nodiscard]] static HybridContainerDocument ConstructExampleDocument(FormatSuffixCategory Category);

    [[nodiscard]] static WorldSpaceDocument ConstructVehicleClearSkyWorldExample();

    [[nodiscard]] static bool GenerateAllFormatExamples(
        const std::filesystem::path&        OutputRootDirectory,
        std::vector<std::filesystem::path>& OutWrittenFilePaths,
        std::string&                        OutDiagnostic);
};

} // namespace Frontier::Serialization
