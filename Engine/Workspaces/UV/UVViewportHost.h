/*============================================================================================================================================
                                                            UVVIEWPORTHOST.H
============================================================================================================================================*/
// 🧩 UV Workspace center viewport canvas matching Experimental/UVEditor (2-row header, tool rail, UV transform readout & native browser shelf)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/ViewportProjectionPane.h"
#include "Engine/Workspaces/UV/UVViewportInteractor.h"
#include "Engine/Workspaces/UV/UVWorkspaceSession.h"

namespace Frontier::Workspaces
{

enum class UVViewportHostRole : uint8_t
{
    DualCombined = 0u,
    Dedicated2D  = 1u,
    Dedicated3D  = 2u,
};

enum class ViewportDirectiveSheetKind : uint8_t
{
    ContextComponent = 0u,
    UnwrapMenu       = 1u,
};

class UVViewportHost
{
public:
    UVViewportHost();
    ~UVViewportHost();

    UVViewportHost(const UVViewportHost&)            = delete;
    UVViewportHost& operator=(const UVViewportHost&) = delete;

    void AttachSession(UVWorkspaceSession* TargetSession) noexcept;
    void ConfigureHostRole(UVViewportHostRole Role) noexcept;
    [[nodiscard]] UVViewportHostRole QueryHostRole() const noexcept { return HostRole; }

    [[nodiscard]] ViewportProjectionPane&       AccessProjectionPane() noexcept { return ProjectionPane; }
    [[nodiscard]] const ViewportProjectionPane& InspectProjectionPane() const noexcept { return ProjectionPane; }
    [[nodiscard]] UVViewportInteractor&         AccessInteractor() noexcept { return Interactor; }
    [[nodiscard]] bool                          QueryDirectiveSheetOpen() const noexcept { return ViewportDirectiveSheetOpen; }
    [[nodiscard]] ViewportDirectiveSheetKind    QueryDirectiveSheetKind() const noexcept { return ActiveDirectiveSheetKind; }

    void Render(
        PixelSpace&             Surface,
        Rectangle2D             Bounds,
        const PaneInputReading& Input,
        double                  Δt);

    void PresentInImGuiPane(double Δt, float ScaleFactor = 1.0f);

private:
    void SynchronizeProjectionPolicy() noexcept;
    void RefreshCachedIslandCount() noexcept;

    UVWorkspaceSession*         Session                 = nullptr;
    UVViewportHostRole          HostRole                = UVViewportHostRole::DualCombined;
    UVViewportInteractor        Interactor              {};
    ViewportProjectionPane      ProjectionPane          {};
    bool                        MoreShelfExpanded       = false;
    bool                        BottomToolRailEnabled   = false;
    bool                        SurfaceDropdownOpen     = false;
    bool                        CanvasDebugDropdownOpen = false;
    bool                        ProjectionDropdownOpen  = false;
    bool                        ViewportDirectiveSheetOpen = false;
    ViewportDirectiveSheetKind  ActiveDirectiveSheetKind   = ViewportDirectiveSheetKind::ContextComponent;
    Point2D                     ViewportDirectiveAnchor    { 0.0f, 0.0f };
    bool                        ViewportDirectiveInPlanar  = true;
    uint32_t               ActiveRailSlot          = 0u;
    uint64_t               CachedUVRevision        = UINT64_MAX;
    uint32_t               CachedChartCount        = 0u;
    Point2D                PaneScreenOrigin        { 0.0f, 0.0f };
    float                  PaneScaleFactor         = 1.0f;
    Rectangle2D            LastSpatialRectLogical  {};
    uint64_t               ActiveHistoryTickSeq    = 0u;
    uint64_t               HeadlessHistorySeq      = 0u;
};

} // namespace Frontier::Workspaces
