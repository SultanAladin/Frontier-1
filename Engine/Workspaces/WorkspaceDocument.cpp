/*============================================================================================================================================
                                                           WORKSPACEDOCUMENT.CPP
============================================================================================================================================*/
// 🧩 Live .Space WorkspaceDocument persistence and revision integration implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/WorkspaceDocument.h"

#include <utility>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 DOCUMENT OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceDocument::WorkspaceDocument(
    WorkspaceToken        AssignedToken,
    std::string           Title,
    std::filesystem::path CanonicalPath,
    std::size_t           DocumentRevisionBudgetBytes)
    : Token(AssignedToken)
    , ContainerPath(std::move(CanonicalPath))
    , Revision(DocumentRevisionBudgetBytes)
{
    Payload.ExtensionCategory  = AssignedToken.ExtensionCategory;
    Payload.DocumentTitle      = std::move(Title);
    Payload.AuthoringWorkspace = "Frontier.WorkspaceSession";
    AssignCanonicalPath(ContainerPath);
}

void WorkspaceDocument::AssignCanonicalPath(std::filesystem::path NewPath)
{
    if (NewPath.empty())
    {
        ContainerPath.clear();
        return;
    }

    const std::string_view CanonicalExtension = Codecs::SpaceContainerEncoder::QueryCanonicalExtension(Payload.ExtensionCategory);
    NewPath.replace_extension(CanonicalExtension);
    ContainerPath = std::move(NewPath);
}

bool WorkspaceDocument::SaveToDisk(std::string& OutDiagnostic, bool EmbedRevisionHistory)
{
    if (ContainerPath.empty())
    {
        OutDiagnostic = "WorkspaceDocument cannot save without an assigned .Space path.";
        return false;
    }

    AssignCanonicalPath(ContainerPath);
    Codecs::SpaceContainerPayload OutgoingPayload = Payload;
    if (EmbedRevisionHistory)
    {
        Codecs::RevisionEncoder::AttachRevisionSections(Revision, OutgoingPayload);
    }

    if (!Codecs::SpaceContainerEncoder::EncodeToFile(ContainerPath, OutgoingPayload, OutDiagnostic))
    {
        return false;
    }

    Payload = std::move(OutgoingPayload);
    Revision.MarkSavedAtCurrentCursor();
    return true;
}

bool WorkspaceDocument::LoadFromDisk(const std::filesystem::path& SourcePath, std::string& OutDiagnostic)
{
    Codecs::SpaceContainerPayload DecodedPayload{};
    if (!Codecs::SpaceContainerDecoder::DecodeFromFile(SourcePath, DecodedPayload, OutDiagnostic))
    {
        return false;
    }

    RevisionTracker DecodedRevision(Revision.QueryDocumentBudgetBytes());
    if (!Codecs::RevisionDecoder::ExtractRevisionSections(DecodedPayload, DecodedRevision, OutDiagnostic))
    {
        return false;
    }

    Payload                 = std::move(DecodedPayload);
    Token.ExtensionCategory = Payload.ExtensionCategory;
    Revision                = std::move(DecodedRevision);
    AssignCanonicalPath(SourcePath);
    return true;
}

} // namespace Frontier::Workspaces
