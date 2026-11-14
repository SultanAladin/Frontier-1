/*============================================================================================================================================
                                                            WORKSPACEDOCUMENT.H
============================================================================================================================================*/
// 🧩 Live open .Space document coupling container payload, canonical disk path, and 215 MiB parametric RevisionTracker

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/RevisionCodec.h"
#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Constants/EngineConstants.h"
#include "Engine/Editor/Viewports/ViewportProjection.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Engine/Workspaces/WorkspaceToken.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceDocument
{
public:
    WorkspaceDocument(
        WorkspaceToken                 AssignedToken,
        std::string                    Title,
        std::filesystem::path          CanonicalPath,
        std::size_t                    DocumentRevisionBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    [[nodiscard]] bool SaveToDisk(std::string& OutDiagnostic, bool EmbedRevisionHistory = true);
    [[nodiscard]] bool LoadFromDisk(const std::filesystem::path& SourcePath, std::string& OutDiagnostic);

    [[nodiscard]] ReconstructedDocumentProjection ReconstructProjection(
        const ReconstructionTargetSpec& TargetSpec = {}) const
    {
        return Revision.ReconstructAtCursor(TargetSpec);
    }

    [[nodiscard]] WorkspaceToken                      QueryToken() const noexcept { return Token; }
    [[nodiscard]] uint32_t                            QueryDocumentIdentifier() const noexcept { return Token.SlotIndex + 1u; }
    [[nodiscard]] const std::string&                  QueryTitle() const noexcept { return Payload.DocumentTitle; }
    [[nodiscard]] const std::filesystem::path&        QueryCanonicalPath() const noexcept { return ContainerPath; }
    [[nodiscard]] Codecs::SpaceExtensionCategory      QueryCategory() const noexcept { return Payload.ExtensionCategory; }
    [[nodiscard]] bool                                QueryModifiedSinceSave() const noexcept { return Revision.QueryModifiedSinceSave(); }
    [[nodiscard]] RevisionTracker&                    AccessRevisionTracker() noexcept { return Revision; }
    [[nodiscard]] const RevisionTracker&              InspectRevisionTracker() const noexcept { return Revision; }
    [[nodiscard]] Codecs::SpaceContainerPayload&      AccessContainerPayload() noexcept { return Payload; }
    [[nodiscard]] const Codecs::SpaceContainerPayload& InspectContainerPayload() const noexcept { return Payload; }
    [[nodiscard]] Editor::ViewportProjection&         AccessViewportProjection() noexcept { return Projection; }
    [[nodiscard]] const Editor::ViewportProjection&   InspectViewportProjection() const noexcept { return Projection; }

    void AssignCanonicalPath(std::filesystem::path NewPath);

private:
    WorkspaceToken                Token         {}; // [-] - Generation-checked registry token
    std::filesystem::path         ContainerPath {}; // [-] - Canonical PascalCase .Space file path
    Codecs::SpaceContainerPayload Payload       {}; // [-] - Two-Tier Hybrid manifest + binary sections
    RevisionTracker               Revision      ;   // [-] - Per-document 215 MiB parametric revision tracker
    Editor::ViewportProjection    Projection    {}; // [-] - Reusable 3D spatial & 2D planar viewport projection
};

} // namespace Frontier::Workspaces
