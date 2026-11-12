/*============================================================================================================================================
                                                               PROJECTCODEC.H
============================================================================================================================================*/
// 🧩 .ProjectSpace encoder & decoder persisting project configuration, startup workspace, and dependency locks

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

struct ProjectDependencyLockEntry
{
    std::string DependencyIdentifier {}; // [-] - Canonical package or sub-project identifier
    std::string PinnedVersion        {}; // [-] - Semantic version or revision digest lock
};

struct ProjectSpaceDocumentModel
{
    std::string                             ProjectTitle      { "FrontierProject" };   // [-]  - Human-readable project name
    std::string                             StartupSpacePath  { "Main.UVSpace" };      // [-]  - Default workspace document on launch
    double                                  TargetTickRateHz  = 120.0;                 // [Hz] - Deterministic runtime tick frequency
    std::vector<ProjectDependencyLockEntry> DependencyLocks   {};                      // [-]  - Locked external/workspace dependencies
};

class ProjectEncoder
{
public:
    [[nodiscard]] static SpaceContainerPayload BuildPayload(
        const ProjectSpaceDocumentModel&   Model,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path&       TargetPath,
        const ProjectSpaceDocumentModel&   Model,
        std::string&                       OutDiagnostic,
        const Workspaces::RevisionTracker* OptionalRevision = nullptr);
};

class ProjectDecoder
{
public:
    [[nodiscard]] static bool ExtractModel(
        const SpaceContainerPayload& Payload,
        ProjectSpaceDocumentModel&   OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& SourcePath,
        ProjectSpaceDocumentModel&   OutModel,
        std::string&                 OutDiagnostic,
        Workspaces::RevisionTracker* OptionalOutRevision = nullptr);
};

struct ProjectCodec
{
    using Encoder = ProjectEncoder;
    using Decoder = ProjectDecoder;
};

} // namespace Frontier::Codecs
