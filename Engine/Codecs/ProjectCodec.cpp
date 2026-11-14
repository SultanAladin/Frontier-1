/*============================================================================================================================================
                                                              PROJECTCODEC.CPP
============================================================================================================================================*/
// 🧩 .ProjectSpace encoder & decoder implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ProjectCodec.h"

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                PROJECT CODEC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

SpaceContainerPayload ProjectEncoder::BuildPayload(
    const ProjectSpaceDocumentModel&   Model,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    SpaceContainerPayload Payload{};
    Payload.ExtensionCategory  = SpaceExtensionCategory::ProjectSpace;
    Payload.DocumentTitle      = Model.ProjectTitle;
    Payload.AuthoringWorkspace = "Frontier.ProjectWorkspace";

    toml::table Settings;
    Settings.insert_or_assign("ProjectTitle", Model.ProjectTitle);
    Settings.insert_or_assign("StartupSpacePath", Model.StartupSpacePath);
    Settings.insert_or_assign("TargetTickRateHz", Model.TargetTickRateHz);
    Payload.DomainManifest.insert_or_assign(".ProjectSettings", std::move(Settings));

    if (!Model.DependencyLocks.empty())
    {
        toml::array LocksArray;
        for (const ProjectDependencyLockEntry& Lock : Model.DependencyLocks)
        {
            toml::table LockEntry;
            LockEntry.insert_or_assign("DependencyIdentifier", Lock.DependencyIdentifier);
            LockEntry.insert_or_assign("PinnedVersion", Lock.PinnedVersion);
            LocksArray.push_back(std::move(LockEntry));
        }
        Payload.DomainManifest.insert_or_assign(".DependencyLock", std::move(LocksArray));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }
    return Payload;
}

bool ProjectEncoder::EncodeToFile(
    const std::filesystem::path&       TargetPath,
    const ProjectSpaceDocumentModel&   Model,
    std::string&                       OutDiagnostic,
    const Workspaces::RevisionTracker* OptionalRevision)
{
    const SpaceContainerPayload Payload = BuildPayload(Model, OptionalRevision);
    return SpaceContainerEncoder::EncodeToFile(TargetPath, Payload, OutDiagnostic);
}

bool ProjectDecoder::ExtractModel(
    const SpaceContainerPayload& Payload,
    ProjectSpaceDocumentModel&   OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    if (Payload.ExtensionCategory != SpaceExtensionCategory::ProjectSpace)
    {
        OutDiagnostic = "Expected .ProjectSpace container category.";
        return false;
    }

    OutModel              = ProjectSpaceDocumentModel{};
    OutModel.ProjectTitle = Payload.DocumentTitle;
    if (const toml::table* Settings = Payload.DomainManifest[".ProjectSettings"].as_table())
    {
        OutModel.ProjectTitle     = (*Settings)["ProjectTitle"].value_or(Payload.DocumentTitle);
        OutModel.StartupSpacePath = (*Settings)["StartupSpacePath"].value_or(std::string{});
        OutModel.TargetTickRateHz = (*Settings)["TargetTickRateHz"].value_or(120.0);
    }

    if (const toml::array* LocksArray = Payload.DomainManifest[".DependencyLock"].as_array())
    {
        for (const auto& EntryRef : *LocksArray)
        {
            if (const toml::table* LockSection = EntryRef.as_table())
            {
                ProjectDependencyLockEntry Lock{};
                Lock.DependencyIdentifier = (*LockSection)["DependencyIdentifier"].value_or(std::string{});
                Lock.PinnedVersion        = (*LockSection)["PinnedVersion"].value_or(std::string{});
                OutModel.DependencyLocks.push_back(std::move(Lock));
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ProjectDecoder::DecodeFromFile(
    const std::filesystem::path& SourcePath,
    ProjectSpaceDocumentModel&   OutModel,
    std::string&                 OutDiagnostic,
    Workspaces::RevisionTracker* OptionalOutRevision)
{
    SpaceContainerPayload Payload{};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractModel(Payload, OutModel, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
