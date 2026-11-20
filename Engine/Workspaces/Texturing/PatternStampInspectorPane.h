/*============================================================================================================================================
                                                         PATTERNSTAMPINSPECTORPANE.H
============================================================================================================================================*/
// 🧩 Right-docked Project Zero Artwork Inspector pane shared by Pattern Studio and Stamp Studio (cards-pattern.png, rows-side-by-side.png,
//    fill-linear-scene.png, fill-point-field.png, menu-shapes.png): Document hero, 3 KPI tiles (Instances, Coverage, Palette), 7 vertical
//    collapsible layer property rows (Shape, Fill, Outline, Placement, Blend, Surface, Mask), Modifiers list, Document fold, and Export bar.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Workspaces/Texturing/PatternStampStackSession.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <array>
#include <cstdint>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLASS DECLARATION
//------------------------------------------------------------------------------------------------------------------------

class PatternStampInspectorPane
{
public:
    ~PatternStampInspectorPane() noexcept;

    void AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

public:
    float ScrollOffsetY = 0.0f;

    // 7 vertical collapsible layer property rows: 0=Shape, 1=Fill, 2=Outline, 3=Placement, 4=Blend, 5=Surface, 6=Mask
    std::array<bool, 7u> PropertyRowOpen {{ false, false, false, false, false, false, false }};
    bool                 WholeCanvasRamp = true;

private:
    void CloseAllSheets() noexcept;

    PatternStampWorkspaceSession* Session             = nullptr;
    int32_t                       ActiveSliderId      = -1;
    float                         CachedContentHeight = 0.0f;
};

} // namespace Frontier::Workspaces
