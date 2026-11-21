/*============================================================================================================================================
                                                          VOLUMETRICSPACECODEC.H
============================================================================================================================================*/
// 🧩 .VolumetricSpace Two-Tier Hybrid encoder & decoder for Planetary Sky, Celestials, Static Cloud Deck, Fog, Wind & Condensate Volumes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VolumetricSpaceDocumentModel
{
    std::string                                   DocumentTitle           { "ClearSkyDaylight" };
    GeometricRaster::WorldAtmosphereConfiguration Atmosphere              {};
    Mathematics::LinearRGB                        FallbackSkyRadiance     { 0.15f, 0.21f, 0.32f };
    uint32_t                                      ShapeVolumeDimension    = 0u; // [vox] - Cubic dimension when ShapeNoiseTexels present
    uint32_t                                      ErosionVolumeDimension  = 0u; // [vox] - Cubic dimension when ErosionNoiseTexels present
    std::vector<uint8_t>                          ShapeNoiseTexels        {};   // [rgba8]- Optional 64B-aligned 3D Perlin-Worley shape volume
    std::vector<uint8_t>                          ErosionNoiseTexels      {};   // [rgba8]- Optional 64B-aligned 3D Worley erosion volume
};

class VolumetricSpaceEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const VolumetricSpaceDocumentModel& Model,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const VolumetricSpaceDocumentModel& Model,
        std::vector<std::byte>&             OutContainerBytes,
        std::string&                        OutDiagnostic,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&        TargetPath,
        const VolumetricSpaceDocumentModel& Model,
        std::string&                        OutDiagnostic,
        const Workspaces::RevisionTracker*  OptionalRevision = nullptr);
};

class VolumetricSpaceDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload&  Payload,
        VolumetricSpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>    ContainerBytes,
        VolumetricSpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&  SourcePath,
        VolumetricSpaceDocumentModel& OutModel,
        std::string&                  OutDiagnostic,
        Workspaces::RevisionTracker*  OptionalOutRevision = nullptr);
};

struct VolumetricSpaceCodec
{
    using Encoder = VolumetricSpaceEncoder;
    using Decoder = VolumetricSpaceDecoder;
};

} // namespace Frontier::Codecs
