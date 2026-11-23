/*============================================================================================================================================
                                                           COMPONENTSPACECODEC.H
============================================================================================================================================*/
// 🧩 .ComponentSpace Two-Tier Hybrid encoder & decoder for ComponentClassDefinition hierarchies, physics constraints & behaviours

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/ComponentClass/ComponentClassDefinition.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentSpaceEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload ConstructPayload(
        const ComponentClass::ComponentClassDefinition& Definition,
        const Workspaces::RevisionTracker*              OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToBytes(
        const ComponentClass::ComponentClassDefinition& Definition,
        std::vector<std::byte>&                         OutContainerBytes,
        std::string&                                    OutDiagnostic,
        const Workspaces::RevisionTracker*              OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&                    TargetPath,
        const ComponentClass::ComponentClassDefinition& Definition,
        std::string&                                    OutDiagnostic,
        const Workspaces::RevisionTracker*              OptionalRevision = nullptr);
};

class ComponentSpaceDecoder
{
public:
    [[nodiscard]] static bool ExtractDefinition(
        const SpaceContainerPayload&              Payload,
        ComponentClass::ComponentClassDefinition& OutDefinition,
        std::string&                              OutDiagnostic,
        Workspaces::RevisionTracker*              OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte>                RawContainerBytes,
        ComponentClass::ComponentClassDefinition& OutDefinition,
        std::string&                              OutDiagnostic,
        Workspaces::RevisionTracker*              OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path&              SourcePath,
        ComponentClass::ComponentClassDefinition& OutDefinition,
        std::string&                              OutDiagnostic,
        Workspaces::RevisionTracker*              OptionalOutRevision = nullptr);
};

} // namespace Frontier::Codecs
