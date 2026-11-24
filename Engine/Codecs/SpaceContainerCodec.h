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

enum class SpaceSuffixCategory : uint8_t
{
    ProjectSpace    = 0,  // [-] - .ProjectSpace root project configuration & dependency locks
    ColourSpace     = 1,  // [-] - .ColourSpace radiometric swatches, ramps, & spectral curves
    GeometrySpace   = 2,  // [-] - .GeometrySpace 3D analytic, half-edge & spatial acceleration domain
    FontSpace       = 3,  // [-] - .FontSpace vector typography & kerning metrics
    TextureSpace    = 4,  // [-] - .TextureSpace GPU-ready tiled texel channels & mip chains
    MaterialSpace   = 5,  // [-] - .MaterialSpace physically based shading parameters
    ImageSpace      = 6,  // [-] - .ImageSpace layered linear-light image canvas
    VectorSpace     = 7,  // [-] - .VectorSpace resolution-independent Bezier/spline vector graphics
    SimulationSpace = 8,  // [-] - .SimulationSpace deterministic physics/solver setup
    VolumetricSpace = 9,  // [-] - .VolumetricSpace sparse density/temperature fields
    UVSpace         = 10, // [-] - .UVSpace UDIM & UVTile parameterization
    Pigment         = 11, // [-] - .Pigment Option 1A natural pigment preset
    Sketch          = 12, // [-] - .Sketch Option 1A natural 2D drafting preset
    Assembly        = 13, // [-] - .Assembly Option 1A natural multi-instance layout
    Motion          = 14, // [-] - .Motion Option 1A natural animation curves
    Render          = 15, // [-] - .Render Option 1A natural camera/output target
    Photometry      = 16, // [-] - .Photometry Option 1A natural IES luminaire profile
    Terrain         = 17, // [-] - .Terrain Option 1A natural heightfield tile
    Acoustic        = 18, // [-] - .Acoustic Option 1A natural impulse/propagation setup
    NetworkSpace          = 19, // [-] - .NetworkSpace distributed session routing & replication policies
    InputSpace            = 20, // [-] - .InputSpace hardware chord registers, navigation presets & axis calibration
    ComponentSpace        = 21, // [-] - .ComponentSpace modular ComponentClass hierarchy, physics constraints & behaviours
    WorldSpace            = 22, // [-] - .WorldSpace complete openable world scene hierarchy, placements & environment links
    PhotometrySpace       = 23, // [-] - .PhotometrySpace solar ephemeris, area luminaires & 128x32 goniophotometric grids
    RenderSpace           = 24, // [-] - .RenderSpace viewport render engine, shadow cascades, distance field & tone transforms
    SurfaceParameterSpace = 25, // [-] - .SurfaceParameterSpace 2D surface parameterization tiles, seams & pin coordinates
    InstanceSpace         = 26, // [-] - .InstanceSpace full unabbreviated domain-tagged instance container
    Instance              = 27  // [-] - .Instance multi-pillar world instance package (canonical Tier B suffix)
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
    SpaceSuffixCategory          SuffixCategory  = SpaceSuffixCategory::ProjectSpace; // [-] - Canonical .Space domain
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
    [[nodiscard]] static std::string_view QueryCanonicalSuffix(SpaceSuffixCategory Category) noexcept;

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
    [[nodiscard]] static bool ResolveSuffixCaseInsensitive(
        std::string_view        RawExtension,
        SpaceSuffixCategory& OutCategory,
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
