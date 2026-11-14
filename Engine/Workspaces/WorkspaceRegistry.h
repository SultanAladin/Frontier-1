/*============================================================================================================================================
                                                            WORKSPACEREGISTRY.H
============================================================================================================================================*/
// 🧩 Generation-checked open .Space document registry and Option 2A case-insensitive container router

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/EngineConstants.h"
#include "Engine/Workspaces/WorkspaceDocument.h"
#include "Engine/Workspaces/WorkspaceToken.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceRegistry
{
public:
    WorkspaceRegistry() = default;

    [[nodiscard]] WorkspaceToken CreateDocument(
        Codecs::SpaceExtensionCategory Category,
        std::string_view               Title,
        const std::filesystem::path&   InitialPath                 = {},
        std::size_t                    DocumentRevisionBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    [[nodiscard]] WorkspaceToken OpenDocumentFromFile(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic,
        std::size_t                  DocumentRevisionBudgetBytes = Constants::DefaultDocumentRevisionBudgetBytes);

    bool CloseDocument(WorkspaceToken Token) noexcept;
    void CloseAllDocuments() noexcept;

    [[nodiscard]] WorkspaceDocument*       ResolveDocument(WorkspaceToken Token) noexcept;
    [[nodiscard]] const WorkspaceDocument* InspectDocument(WorkspaceToken Token) const noexcept;
    [[nodiscard]] uint32_t                 QueryOpenDocumentCount() const noexcept;
    [[nodiscard]] std::vector<WorkspaceToken> EnumerateOpenTokens() const;

private:
    struct DocumentSlot
    {
        uint32_t                           Generation = 1u;      // [gen] - Monotonic generation counter
        bool                               Occupied   = false;   // [-]   - True while an open document resides in slot
        std::unique_ptr<WorkspaceDocument> Document   {};        // [-]   - Owned WorkspaceDocument instance
    };

    std::vector<DocumentSlot> Slots {}; // [-] - Generation-checked open document slot array
};

} // namespace Frontier::Workspaces
