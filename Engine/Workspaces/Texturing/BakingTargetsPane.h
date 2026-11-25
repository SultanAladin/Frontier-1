/*==========================================================================================================================================
                                                             BAKINGTARGETSPANE.H
==========================================================================================================================================*/
// 🧩 Left-column Queue Outliner for Baking Studio: stat tiles, search/filter, preset chips & expandable 18-target outliner tree.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Texturing/BakingWorkspaceSession.h"

#include <array>
#include <cstdint>

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

    BakingWorkspaceSession* Session            = nullptr;
    float                   ScrollOffsetY      = 0.0f;
    float                   MaxScrollY         = 0.0f;
    uint32_t                ActiveFilterIndex  = 0u; // 0 = All, 1 = Queued, 2 = Ready, 3 = Volume
    bool                    FilterSheetOpen    = false;
    std::array<bool, 5u>    GroupExpanded      { false, true, true, true, true };
    TextEditBuffer          SearchBuffer       {};
};

} // namespace Frontier::Workspaces
