/*============================================================================================================================================
                                                              TEXTURECODEC.H
============================================================================================================================================*/
// 🧩 .TextureSpace encoder & decoder persisting .ChannelPack specifications, UDIM .TexelLayer entries, and 64B-aligned texels

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TextureTileLayerEntry
{
    uint32_t UDIMNumber      = 1001u; // [code]  - Associated UDIM UVTile code
    uint32_t MipChainLevels  = 1u;    // [count] - Precomputed mip level count
    uint32_t ResolutionWidth = 2048u; // [px]    - Top mip horizontal texel dimension
};

struct TextureSpaceDocumentModel
{
    std::string                        DocumentTitle      { "PrimaryAlbedoRoughness" }; // [-] - .TextureSpace title
    std::string                        ChannelPackLayout  { "RGBA16F_Linear" };         // [-] - .ChannelPack pixel packing
    std::string                        LinkedUVSpacePath  { "Asset.UVSpace" };          // [-] - Linked .UVSpace container path
    std::vector<TextureTileLayerEntry> TileLayers         {};                           // [-] - UDIM .TexelLayer descriptors
    std::vector<uint8_t>               PackedTexelBytes   {};                           // [B] - 64B-aligned packed texel bytes
};

class TextureEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const TextureSpaceDocumentModel&   Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const TextureSpaceDocumentModel&   Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class TextureDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        TextureSpaceDocumentModel&   OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        TextureSpaceDocumentModel&   OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct TextureCodec
{
    using Encoder = TextureEncoder;
    using Decoder = TextureDecoder;
};

} // namespace Frontier::Codecs
