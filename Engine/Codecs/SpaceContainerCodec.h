/*============================================================================================================================================
                                                           SPACECONTAINERCODEC.H
============================================================================================================================================*/
// 🧩 Universal Two-Tier Hybrid .Space container encoder & decoder (PascalCase TOML manifest + 64B-aligned binary sections)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class SpaceExtensionCategory : uint8_t
{
    ProjectSpace    = 0,  // [-] - .ProjectSpace root project configuration & dependency locks
    ColourSpace     = 1,  // [-] - .ColourSpace radiometric swatches, ramps, & spectral curves
    GeometrySpace   = 2,  // [-] - .GeometrySpace 3D analytic & manifold topology (deferred domain)
    FontSpace       = 3,  // [-] - .FontSpace vector typography & kerning metrics
    TextureSpace    = 4,  // [-] - .TextureSpace GPU-ready tiled texel channels & mip chains
    MaterialSpace   = 5,  // [-] - .MaterialSpace physically based shading parameters
    ImageSpace      = 6,  // [-] - .ImageSpace layered linear-light image canvas
    VectorSpace     = 7,  // [-] - .VectorSpace resolution-independent Bezier/spline artwork
    SimulationSpace = 8,  // [-] - .SimulationSpace deterministic physics/solver setup
    VolumetricSpace = 9,  // [-] - .VolumetricSpace sparse density/temperature fields
    ParticleSpace   = 10, // [-] - .ParticleSpace procedural particle dynamics
    UVSpace         = 11, // [-] - .UVSpace UDIM & UVTile parameterization
    Pigment         = 12, // [-] - .Pigment Option 1A natural pigment preset
    Sketch          = 13, // [-] - .Sketch Option 1A natural 2D drafting preset
    Assembly        = 14, // [-] - .Assembly Option 1A natural multi-instance layout
    Motion          = 15, // [-] - .Motion Option 1A natural animation curves
    Render          = 16, // [-] - .Render Option 1A natural camera/output target
    Photometry      = 17, // [-] - .Photometry Option 1A natural IES luminaire profile
    Terrain         = 18, // [-] - .Terrain Option 1A natural heightfield tile
    Acoustic        = 19  // [-] - .Acoustic Option 1A natural impulse/propagation setup
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BinarySectionBlock
{
    std::string            SectionIdentifier         {};  // [-]   - Unique section name inside container
    std::string            SubFormatTag              {};  // [-]   - Canonical PascalCase sub-format tag (e.g. .UVTile, .Revision)
    std::vector<std::byte> PayloadBytes              {};  // [B]   - Raw unpadded section payload bytes
    uint64_t               Checksum64                = 0; // [hash]- 64-bit FNV-1a integrity digest
    std::size_t            DecodedAbsoluteByteOffset = 0; // [B]   - Verified 64B-aligned offset in container stream
};

struct SpaceContainerPayload
{
    SpaceExtensionCategory          ExtensionCategory  = SpaceExtensionCategory::ProjectSpace; // [-] - Canonical .Space domain
    uint32_t                        SchemaVersion      = 1u;                                   // [ver] - Format schema version
    std::string                     DocumentTitle      { "Untitled" };                         // [-]   - Human-readable document title
    std::string                     AuthoringWorkspace { "Frontier.Workspace" };               // [-]   - Originating workspace identifier
    toml::table                     DomainManifest     {};                                     // [-]   - Tier 1 structured metadata manifest
    std::vector<BinarySectionBlock> BinarySections     {};                                     // [-]   - Tier 2 64B-aligned binary sections
    std::size_t                     Tier2StartOffset    = 0u;                                   // [B]   - Absolute 64B-aligned start of Tier 2
};

class SpaceContainerEncoder
{
public:
    [[nodiscard]] static uint64_t ComputeFnv1aChecksum64(std::span<const std::byte> Bytes) noexcept;
    [[nodiscard]] static std::string_view QueryCanonicalExtension(SpaceExtensionCategory Category) noexcept;

    [[nodiscard]] static bool NormalizeSubFormatTag(
        std::string_view RawTag,
        std::string&     OutCanonicalTag,
        std::string&     OutDiagnostic);

    [[nodiscard]] static bool EncodeToBytes(
        const SpaceContainerPayload& Payload,
        std::vector<std::byte>&      OutContainerBytes,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& TargetPath,
        const SpaceContainerPayload& Payload,
        std::string&                 OutDiagnostic);
};

class SpaceContainerDecoder
{
public:
    [[nodiscard]] static bool ResolveExtensionCaseInsensitive(
        std::string_view        RawExtension,
        SpaceExtensionCategory& OutCategory,
        std::string&            OutCanonicalExtension,
        std::string&            OutDiagnostic);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte> InputBytes,
        SpaceContainerPayload&     OutPayload,
        std::string&               OutDiagnostic);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        SpaceContainerPayload&       OutPayload,
        std::string&                 OutDiagnostic);
};

struct SpaceContainerCodec
{
    using Encoder = SpaceContainerEncoder;
    using Decoder = SpaceContainerDecoder;
};

} // namespace Frontier::Codecs
