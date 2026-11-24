/*============================================================================================================================================
                                                             INSTANCECODEC.H
============================================================================================================================================*/
// 🧩 .Instance TomlWithBinarySection encoder & decoder for multi-format world instances & Shipping cook stripping

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/World/InstanceToken.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Single multi-format slot reference attached to an .Instance package (.GeometrySpace, .ParticleSpace, .ForceField, etc.)
struct InstanceSpaceSlotEntry
{
    std::string SlotRole        {}; // [-] - Semantic role ("PrimarySurface", "SurfaceMaterial", "ParticleEmitter", etc.)
    std::string FormatExtension {}; // [-] - Canonical suffix (".GeometrySpace", ".MaterialSpace", ".ParticleSpace", etc.)
    std::string DocumentPath    {}; // [-] - Relative workspace document path
    uint64_t    DocumentHash64  = 0ull; // [hash] - Deterministic 64-bit FNV-1a path digest
};

// Optional Editor-only authoring directives stripped during Shipping cook
struct InstanceEditorDirectives
{
    std::string OutlinerTitle      {};    // [-] - Outliner row title
    std::string CategoryLabel      {};    // [-] - Outliner category label
    std::string Subtitle           {};    // [-] - Outliner secondary caption
    std::string IconPath           {};    // [-] - Relative SVG icon path
    bool        RemovalProtected   = false; // [-] - Prevent accidental deletion in Editor
    bool        DuplicateProtected = false; // [-] - Prevent duplicate singleton entries
};

// Complete .Instance document model serialized via SpaceContainerCodec
struct InstanceDocumentModel
{
    std::string                         DocumentTitle        { "UntitledInstance" };             // [-]   - Instance display name
    World::InstanceCategory             Category             = World::InstanceCategory::SurfaceGeometry; // [-] - Primary category
    uint32_t                            SchemaVersion        = 1u;                               // [ver] - Document schema version
    std::array<float, 3>                PositionMeters       { 0.0f, 0.0f, 0.0f };               // [m]   - Default world position
    std::array<float, 3>                RotationDegrees      { 0.0f, 0.0f, 0.0f };               // [deg] - Default Euler rotation
    std::array<float, 3>                ScaleScalar          { 1.0f, 1.0f, 1.0f };               // [x]   - Default scale multiplier
    uint32_t                            DirectiveMask        =
        static_cast<uint32_t>(World::InstanceDirectiveMask::EnableVisibility |
                              World::InstanceDirectiveMask::EnableShadowProjection);             // [mask] - InstanceDirectiveMask bits
    InstanceEditorDirectives            EditorDirectives     {};                                 // [-]   - Authoring directives (stripped in Shipping)
    std::vector<InstanceSpaceSlotEntry> SpaceSlots           {};                                 // [-]   - Multi-format referenced .Space documents
    std::vector<std::byte>              BinaryDefaultPayload {};                                 // [B]   - Optional .InstanceDefaults binary payload
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE ENCODER
//------------------------------------------------------------------------------------------------------------------------

class InstanceEncoder
{
public:
    [[nodiscard]] static uint64_t ComputeDocumentHash64(std::string_view RelativePath) noexcept;

    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const InstanceDocumentModel&       Model,
        bool                               StripEditorDirectives = false,
        const Workspaces::RevisionTracker* OptionalRevision      = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const InstanceDocumentModel&       Model,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        bool                               StripEditorDirectives = false,
        const Workspaces::RevisionTracker* OptionalRevision      = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const InstanceDocumentModel&       Model,
        std::string&                       OutDiagnostic,
        bool                               StripEditorDirectives = false,
        const Workspaces::RevisionTracker* OptionalRevision      = nullptr);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTANCE DECODER
//------------------------------------------------------------------------------------------------------------------------

class InstanceDecoder
{
public:
    [[nodiscard]] static bool DecodeFromPayload(
        const SpaceContainerPayload& Payload,
        InstanceDocumentModel&       OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OutOptionalRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   InputBytes,
        InstanceDocumentModel&       OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OutOptionalRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        InstanceDocumentModel&       OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OutOptionalRevision = nullptr);
};

struct InstanceCodec
{
    using Encoder = InstanceEncoder;
    using Decoder = InstanceDecoder;
};

} // namespace Frontier::Codecs
