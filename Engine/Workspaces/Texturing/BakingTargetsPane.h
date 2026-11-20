/*============================================================================================================================================
                                                             BAKINGTARGETSPANE.H
============================================================================================================================================*/
// 🧩 Left-column pane for Baking Studio: 4 target-set presets + 18 surface & volume SDF target swatches with check selection.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

#include <array>

namespace Frontier::Workspaces
{

class BakingTargetsPane
{
public:
    BakingTargetsPane() noexcept = default;
    ~BakingTargetsPane() noexcept;

    void AttachSession(BakingWorkspaceSession* TargetSession) noexcept;
    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor);
    void AssignScrollOffsetY(float OffsetY) noexcept { ScrollOffsetY = OffsetY; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

private:
    void RenderTargetSwatchGraphic(
        PixelSpace&       Surface,
        Rectangle2D       SwatchBounds,
        BakerTargetId     Target,
        bool              Ready) const noexcept;

    BakingWorkspaceSession* Session       = nullptr;
    float                   ScrollOffsetY = 0.0f;
    float                   MaxScrollY    = 0.0f;
};

} // namespace Frontier::Workspaces
