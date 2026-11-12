/*============================================================================================================================================
                                                              MATERIALCODEC.H
============================================================================================================================================*/
// 🧩 .MaterialSpace encoder & decoder persisting .ShadingModel (DirectionalLitShading.slang) and .ParameterSlot definitions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MaterialParameterSlotEntry
{
    std::string ParameterName     {};  // [-] - Shader parameter identifier
    double      ScalarMagnitude   = 0.0; // [-] - Physical scalar parameter magnitude
    std::string TextureSpaceAsset {};  // [-] - Optional .TextureSpace asset path
};

struct MaterialSpaceDocumentModel
{
    std::string                             MaterialTitle   { "BrushedTitanium" };            // [-] - .MaterialSpace title
    std::string                             ShaderSourceTag { "DirectionalLitShading.slang" };// [-] - Canonical Slang shading program
    double                                  IndexOfRefraction = 1.5;                          // [-] - Dielectric IOR
    std::vector<MaterialParameterSlotEntry> ParameterSlots  {};                               // [-] - .ParameterSlot assignments
};

class MaterialEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const MaterialSpaceDocumentModel&  Model,
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
