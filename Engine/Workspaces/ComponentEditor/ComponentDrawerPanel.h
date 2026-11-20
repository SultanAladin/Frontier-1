/*============================================================================================================================================
                                                         COMPONENTDRAWERPANEL.H
============================================================================================================================================*/
// 🧩 Bottom collapsible drawer: Compiled payload (5 tiles + Cluster LOD bar chart) & Response curves (interactive Engine torque spline)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.h"

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentDrawerPanel
{
public:
    void Render(
        PixelSpace&                Surface,
        Rectangle2D                Bounds,
        ComponentWorkspaceSession& Session,
        const PaneInputReading&    Input) noexcept;

private:
    ScalarCurveEditorSession CurveSession {};
    std::string              ExportStatus {};
};

} // namespace Frontier::Workspaces::ComponentEditor
