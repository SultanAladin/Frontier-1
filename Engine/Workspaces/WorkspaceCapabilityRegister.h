/*============================================================================================================================================
                                                        WORKSPACECAPABILITYREGISTER.H
============================================================================================================================================*/
// 🧩 Registry of active Frontier host workspace capabilities ("Frontier" World Editor & "UVWorkspace" UV Authoring) for Project Browser

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class HostWorkspaceTarget : uint8_t
{
    FrontierWorld            = 0u, // [-] - Open with Frontier (EditorLifecycle + TieredWorldRenderSequence + ShowcaseGalleryScene)
    UVWorkspace              = 1u, // [-] - Open with UVWorkspace (UVWorkspaceHost dual 2D UV + 3D Surface viewports)
    PatternWorkspace         = 2u, // [-] - Open with Pattern Studio (PatternStampWorkspaceHost seamless 2D vector & OpenPBR tile)
    StampWorkspace           = 3u, // [-] - Open with Stamp Studio (PatternStampWorkspaceHost alpha cutout vector stamp)
    BakingWorkspace          = 4u, // [-] - Open with Baking Studio (BakingWorkspaceHost pure-GPU GTX BVH4 + RTX RayQuery baker)
    FractureWorkspace        = 5u, // [-] - Open with FractureWorkspace (FractureWorkspaceHost 6-solver fracture & JoltPhysics)
    SketcherWorkspace        = 6u, // [-] - Open with Sketcher (SketcherWorkspaceHost parametric NURBS / B-rep CAD & 2D drawing sheet)
    MaterialWorkspace        = 7u, // [-] - Open with Material Studio (MaterialWorkspaceHost dynamic OpenPBR material preview)
    ComponentEditorWorkspace = 8u, // [-] - Open with Component Editor (ComponentWorkspaceHost 3-dock class, constraint & vehicle studio)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorkspaceCapabilityEntry
{
    HostWorkspaceTarget Target             = HostWorkspaceTarget::FrontierWorld;
    std::string         Identifier         {}; // [-] - Stable token stored in .projectspace ("Frontier", "UVWorkspace")
    std::string         ActionTitle        {}; // [-] - Launch button caption ("Open with Frontier", "Open with UVWorkspace")
    std::string         DomainDescription  {}; // [-] - Short capability summary shown in Details & New Project sheet
    bool                EnabledInBuild     = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class WorkspaceCapabilityRegister
{
public:
    WorkspaceCapabilityRegister();

    [[nodiscard]] std::span<const WorkspaceCapabilityEntry> QueryAllCapabilities() const noexcept;
    [[nodiscard]] std::vector<WorkspaceCapabilityEntry>     QueryPermittedForPath(const std::filesystem::path& FilePath) const;
    [[nodiscard]] const WorkspaceCapabilityEntry*           FindByTarget(HostWorkspaceTarget Target) const noexcept;
    [[nodiscard]] const WorkspaceCapabilityEntry*           FindByIdentifier(std::string_view Identifier) const noexcept;

private:
    std::vector<WorkspaceCapabilityEntry> RegisteredEntries {};
};

} // namespace Frontier::Workspaces
