/*============================================================================================================================================
                                                            BAKINGINSPECTORPANE.H
============================================================================================================================================*/
// 🧩 Right-column inspector for Baking Studio: KPI trio, bake progress box, surface ray settings & Texture Paint export bridge.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

namespace Frontier::Workspaces
{

class BakingInspectorPane
{
public:
    BakingInspectorPane() noexcept = default;
    ~BakingInspectorPane() noexcept;

    void AttachSession(BakingWorkspaceSession* TargetSession) noexcept;
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);
    void AssignScrollOffsetY(float OffsetY) noexcept { ScrollOffsetY = OffsetY; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

private:
    BakingWorkspaceSession* Session            = nullptr;
    float                   ScrollOffsetY      = 0.0f;
    float                   MaxScrollY         = 0.0f;
    int32_t                 ActiveSliderId     = -1;
    bool                    SurfaceSectionOpen = true;
    bool                    ExportSectionOpen  = true;
};

} // namespace Frontier::Workspaces
