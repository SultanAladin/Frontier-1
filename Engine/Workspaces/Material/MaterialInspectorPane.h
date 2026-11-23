/*============================================================================================================================================
                                                         MATERIALINSPECTORPANE.H
============================================================================================================================================*/
// 🧩 Right-docked Project Zero Material Inspector pane: OpenPBR 1.1 header, 4-tile telemetry (F0, CLASS, GLOSS, ALBEDO),
//    Tiling & UV collapsible card, and 6-group 28-channel stack with side-by-side multi-open channel editors and zero amber borders.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Workspaces/Material/MaterialOutlinerPane.h"
#include "Engine/Workspaces/Material/MaterialWorkspaceSession.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <cstdint>

namespace Frontier::Workspaces
{

class MaterialInspectorPane
{
public:
    MaterialInspectorPane() noexcept = default;
    ~MaterialInspectorPane() noexcept;

    void AttachSession(MaterialWorkspaceSession* TargetSession, MaterialOutlinerPane* TargetOutliner) noexcept
    {
        Session  = TargetSession;
        Outliner = TargetOutliner;
    }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  DeltaSeconds);

    void PresentInImGuiPane(double DeltaSeconds, float ScaleFactor = 1.0f);

public:
    bool    TilingSectionOpen = false;
    bool    ActiveOnlyFilter  = false;
    float   UvScaleX          = 1.0f;
    float   UvScaleY          = 1.0f;
    float   UvRotationDeg     = 0.0f;
    bool    TriplanarEnabled  = false;
    float   ScrollOffsetY     = 0.0f;

private:
    MaterialWorkspaceSession* Session        = nullptr;
    MaterialOutlinerPane*     Outliner       = nullptr;
    int32_t                   ActiveSliderId = -1;
    float                     MaxScrollY     = 0.0f;
};

} // namespace Frontier::Workspaces
