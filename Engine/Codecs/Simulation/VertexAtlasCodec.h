/*============================================================================================================================================
                                                           VERTEXATLASCODEC.H
============================================================================================================================================*/
// 🧩 Phase D07 .VertexAtlas two-tier codec (TOML metadata + 64B-aligned RGBA32F position/normal texels & rigid slice poses + FNV-1a)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.6

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   VERTEX ATLAS DOCUMENT MODEL
//------------------------------------------------------------------------------------------------------------------------

struct VertexAtlasDocumentModel
{
    std::string                                        DocumentTitle { "UntitledVertexAtlas" };
    uint32_t                                           SchemaVersion = kSimulationCodecSchemaVersion;
    GeometrySpace::Deformation::VertexMotionAtlas      Atlas         {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      VERTEX ATLAS CODEC
//------------------------------------------------------------------------------------------------------------------------

class VertexAtlasCodec
{
public:
    [[nodiscard]] static bool EncodeToBytes(
        const VertexAtlasDocumentModel& model,
        std::vector<std::byte>& outBytes,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte> bytes,
        VertexAtlasDocumentModel& outModel,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const VertexAtlasDocumentModel& model,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        VertexAtlasDocumentModel& outModel,
        std::string* outError = nullptr);
};

} // namespace Frontier::Codecs::Simulation
