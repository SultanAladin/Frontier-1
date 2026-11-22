/*============================================================================================================================================
                                                         COMPONENTWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level 3-dock Component Editor host: WorkspaceTabStrip (128x128 icons, dirty dot, Frontier · Component Editor), Left/Center/Right docks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Docking/WorkspaceTabStrip.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ComponentEditor/ComponentContentTray.h"
#include "Engine/Workspaces/ComponentEditor/ComponentDetailsPanel.h"
#include "Engine/Workspaces/ComponentEditor/ComponentDrawerPanel.h"
#include "Engine/Workspaces/ComponentEditor/ComponentTreePanel.h"
#include "Engine/Workspaces/ComponentEditor/ComponentViewportHost.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"

#include <filesystem>

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentWorkspaceHost
{
public:
    ComponentWorkspaceHost();

    void AttachVulkanDevice(
        GeometricRaster::VulkanInterchange& TargetDevice,
        const std::filesystem::path&        CompiledShaderDirectory,
        const std::filesystem::path&        RepositoryRoot);

    void RenderWorkspace(
        PixelSpace&                       Surface,
        Rectangle2D                       WindowBounds,
        const PaneInputReading&           Input,
        double                            DeltaSeconds = 1.0 / 60.0) noexcept;

    [[nodiscard]] ComponentWorkspaceSession&       AccessSession() noexcept       { return Session; }
    [[nodiscard]] const ComponentWorkspaceSession& InspectSession() const noexcept { return Session; }

private:
    ComponentWorkspaceSession                                    Session {};
    GeometricRaster::ComponentEditor::ComponentViewportRenderer  Renderer {};
    ComponentTreePanel                                           TreePanel {};
    ComponentContentTray                                         ContentTray {};
    ComponentViewportHost                                        ViewportHost {};
    ComponentDrawerPanel                                         DrawerPanel {};
    ComponentDetailsPanel                                        DetailsPanel {};
};

} // namespace Frontier::Workspaces::ComponentEditor
