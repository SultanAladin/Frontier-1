/*============================================================================================================================================
                                                            UVOUTLINERPANEL.H
============================================================================================================================================*/
// 🧩 UV Space Outliner pane matching Experimental/UVEditor pixel-for-pixel with ControlCentre ThemeRepository styling & 0px layer boxes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

class UVOutlinerPanel
{
public:
    UVOutlinerPanel() noexcept = default;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void RefreshChartSheetTelemetry();

    UVWorkspaceSession* Session                 = nullptr;
    uint32_t            CreatedLayerCount       = 3u;
    uint32_t            UDIMFilterCode          = 0u; // 0 = All UDIMs
    float               ScrollOffsetY           = 0.0f;
    TextEditBuffer      SearchEditBuffer        {};
    TextEditBuffer      RenameEditBuffer        {};
    uint16_t            RenamingLayerOrd        = 0u;

    uint64_t            RetainedNetworkRevision   = UINT64_MAX;
    uint64_t            RetainedUVRevision        = UINT64_MAX;
    uint32_t            RetainedIslandCount       = 0u;
    float               RetainedUtilizationPct    = 0.0f;
    float               RetainedStretchCautionPct = 0.0f;
};

} // namespace Frontier::Workspaces
