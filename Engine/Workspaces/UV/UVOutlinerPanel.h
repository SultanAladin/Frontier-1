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

#include <utility>
#include <vector>

namespace Frontier::Workspaces
{

class UVOutlinerPanel
{
public:
    UVOutlinerPanel() noexcept = default;
    ~UVOutlinerPanel() noexcept;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void RefreshCachedAtlasTelemetry();

    UVWorkspaceSession* Session                 = nullptr;
    uint32_t            CreatedLayerCount       = 3u;
    uint32_t            UDIMFilterCode          = 0u; // 0 = All UDIMs
    bool                UDIMFilterDropdownOpen  = false;
    float               ScrollOffsetY           = 0.0f;
    TextEditBuffer      SearchEditBuffer        {};
    TextEditBuffer      RenameEditBuffer        {};
    uint16_t            RenamingLayerOrd        = 0u;
    uint16_t            ColorPickerLayerOrdinal = 0u;

    uint64_t            CachedNetworkRevision   = UINT64_MAX;
    uint64_t            CachedUVRevision        = UINT64_MAX;
    uint64_t            CachedLayerAssignmentSignature = UINT64_MAX;
    uint32_t            CachedIslandCount       = 0u;
    std::vector<uint32_t> CachedFaceIslandOrdinal {};
    std::vector<uint32_t> CachedIslandUDIMNumber {};
    std::vector<uint16_t> CachedIslandLayerOrdinal {};
    std::vector<float>   CachedIslandUVArea {};
    float               CachedUtilizationPct    = 0.0f;
    float               CachedStretchCautionPct = 0.0f;
    std::vector<std::pair<uint16_t, Rectangle2D>> RenderedLayerSlotBounds {};
    std::vector<uint16_t> PreviouslyRegisteredLayerOrdinals {};
    uint64_t            HeadlessTickSequence    = 0u;
};

} // namespace Frontier::Workspaces
