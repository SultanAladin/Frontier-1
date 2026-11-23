/*============================================================================================================================================
                                                           FORMATCODECSUITE.H
============================================================================================================================================*/
// 🧩 Typed Sectioned Document codecs and complete 63-format concrete example generator (including paused engine pillars)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Codecs/FormatSpecifications.h"
#include "Engine/Serialization/Container/DocumentContainerCodec.h"

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
        DocumentContainer&      OutContainer);

    [[nodiscard]] static bool DecodeEngineSettings(
        const DocumentContainer& Container,
        EngineSettingsDocument&        OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeRuntimeConfiguration(
        const RuntimeConfigurationDocument& Document,
        DocumentContainer&            OutContainer);

    [[nodiscard]] static bool DecodeRuntimeConfiguration(
        const DocumentContainer& Container,
        RuntimeConfigurationDocument&  OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodePlatformSettings(
        const PlatformSettingsDocument& Document,
        DocumentContainer&        OutContainer);

    [[nodiscard]] static bool DecodePlatformSettings(
        const DocumentContainer& Container,
        PlatformSettingsDocument&      OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeProjectIndex(
        const ProjectIndexDocument& Document,
        DocumentContainer&    OutContainer);

    [[nodiscard]] static bool DecodeProjectIndex(
        const DocumentContainer& Container,
        ProjectIndexDocument&          OutDocument,
        std::string&                   OutDiagnostic);

    // ② CameraOptics, Atmosphere, FractureNetwork & WorldSpace Codecs
    [[nodiscard]] static bool EncodeCameraOptics(
        const CameraOpticsDocument& Document,
        DocumentContainer&    OutContainer);

    [[nodiscard]] static bool DecodeCameraOptics(
        const DocumentContainer& Container,
        CameraOpticsDocument&          OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeAtmosphere(
        const AtmosphereDocument& Document,
        DocumentContainer&  OutContainer);

    [[nodiscard]] static bool DecodeAtmosphere(
        const DocumentContainer& Container,
        AtmosphereDocument&            OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeFractureNetwork(
        const FractureNetworkDocument& Document,
        DocumentContainer&       OutContainer);

    [[nodiscard]] static bool DecodeFractureNetwork(
        const DocumentContainer& Container,
        FractureNetworkDocument&       OutDocument,
        std::string&                   OutDiagnostic);

    [[nodiscard]] static bool EncodeWorldSpace(
        const WorldSpaceDocument& Document,
        DocumentContainer&  OutContainer);

    [[nodiscard]] static bool DecodeWorldSpace(
        const DocumentContainer& Container,
        WorldSpaceDocument&            OutDocument,
        std::string&                   OutDiagnostic);

    // ③ Universal Example Builder & On-Disk Generator for ALL 63 Frontier Formats (including paused pillars)
    [[nodiscard]] static DocumentContainer ConstructExampleDocument(FormatSuffixCategory Category);

    [[nodiscard]] static WorldSpaceDocument ConstructVehicleClearSkyWorldExample();

    [[nodiscard]] static bool GenerateAllFormatExamples(
        const std::filesystem::path&        OutputRootDirectory,
        std::vector<std::filesystem::path>& OutWrittenFilePaths,
        std::string&                        OutDiagnostic);
};

} // namespace Frontier::Serialization
