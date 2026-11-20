/*============================================================================================================================================
                                                               MATERIALCODEC.H
============================================================================================================================================*/
// 🧩 .MaterialSpace encoder & decoder persisting .ShadingModel, .ParameterSlot, 10-lobe SurfaceMaterial, 128 B registers, & 32 B summaries

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialParameterSlotEntry
{
    std::string ParameterName     {};    // [-] - Shader parameter identifier
    double      ScalarMagnitude   = 0.0; // [-] - Physical scalar parameter magnitude
    std::string TextureSpaceAsset {};    // [-] - Optional .TextureSpace asset path
};

struct MaterialSpaceDocumentModel
{
    std::string                                    MaterialTitle            { "BrushedTitanium" };                  // [-]     - .MaterialSpace title
    std::string                                    ShaderSourceTag          { "OpenPbrSurfaceScattering.slang" };   // [-]     - Canonical Slang shading program
    double                                         IndexOfRefraction        = 1.5;                                  // [-]     - Dielectric IOR
    std::vector<MaterialParameterSlotEntry>        ParameterSlots           {};                                     // [-]     - .ParameterSlot assignments
    bool                                           PhysicalMaterialIncluded = false;                                // [-]     - True if 10-lobe SurfaceMaterial attached
    MaterialSpace::SurfaceMaterial                 AuthoredMaterial         {};                                     // [-]     - Full 10-lobe OpenPBR + Glinty specification
    std::vector<MaterialSpace::CompactMaterialEntry> CompiledRegisters      {};                                     // [128 B] - Pre-compiled GPU bindless registers
    std::vector<MaterialSpace::BounceSlabEntry>    CompiledBounceSummaries  {};                                     // [32 B]  - Pre-integrated secondary-bounce summaries
};

class MaterialEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const MaterialSpaceDocumentModel&  Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const MaterialSpaceDocumentModel&  Model,
        std::vector<std::byte>&            OutContainerBytes,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const MaterialSpaceDocumentModel&  Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class MaterialDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        MaterialSpaceDocumentModel&  OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>   ContainerBytes,
        MaterialSpaceDocumentModel&  OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        MaterialSpaceDocumentModel&  OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct MaterialCodec
{
    using Encoder = MaterialEncoder;
    using Decoder = MaterialDecoder;
};

} // namespace Frontier::Codecs
