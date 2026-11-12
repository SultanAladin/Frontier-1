/*============================================================================================================================================
                                                             EDITORLIFECYCLE.H
============================================================================================================================================*/
// 🧩 Editor-only authoring loop orchestrating WorkspaceDockContainer, ControlCentrePanel, WorkspaceLifecycle, and GPU .slang overlay hooks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Configuration/BuildConfiguration.h"

#if FRONTIER_EDITOR_ENABLED

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Lifecycle/WorkspaceLifecycle.h"
#include "Engine/Windowing/PlatformWindow.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct EditorStartupConfiguration
{
    Windowing::WindowConfigurationDescriptor WindowDescriptor =
        Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", 1600u, 980u); // [-] - Primary editor window
    std::filesystem::path FontRepositoryRoot {};                                                      // [-] - Optional EngineContent/FontsArchives path
    std::filesystem::path ProjectSpacePath {};                                                         // [-] - Optional .projectspace persistence path
    OutlinerTitleVariant  OutlinerHeading  = OutlinerTitleVariant::Outliner;                           // [-] - Outliner vs LayerStack heading
    double                MaximumStep      = 0.100;                                                    // [s] - Upper Δt clamp per authoring tick
};

struct EditorLifecycleReading
{
    LifecyclePhase                 SequencePhase       = LifecyclePhase::Dormant; // [-]     - GPU overlay / editor participant phase
    LifecycleStepMetrics           SequenceMetrics     {};                        // [-]     - Authoring tick counters
    uint32_t                       ActiveWindowCount   = 0u;                      // [count] - Active OS / headless host windows
    uint32_t                       RegisteredPaneCount = 0u;                      // [count] - Registered trapezoidal dock panes
    uint32_t                       OpenWorkspaceCount  = 0u;                      // [count] - Open multi-document sessions
    uint32_t                       GpuOverlayCount     = 0u;                      // [count] - Registered GPU .slang overlay hooks
    bool                           ControlCentreOpen   = false;                   // [-]     - True while top Control Centre shade is open
    bool                           ContentBrowserOpen  = false;                   // [-]     - True while bottom ContentBrowser drawer is open
    bool                           AuthoringActive     = false;                   // [-]     - True between Initialize and Finalize
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class EditorLifecycle
{
public:
    EditorLifecycle() noexcept = default;
    ~EditorLifecycle() noexcept;

    EditorLifecycle(const EditorLifecycle&)            = delete;
    EditorLifecycle& operator=(const EditorLifecycle&) = delete;

    // 📝 Registers GPU .slang viewport overlay participants (NavigationOverlay, TransformGizmo, AnnotationExtension).
    template <LifecycleParticipant OverlayParticipantType>
    [[nodiscard]] uint32_t RegisterGpuViewportOverlay(std::string_view Identifier, OverlayParticipantType& OverlayParticipant)
    {
        ++RegisteredGpuOverlays;
        return EditorSequence.RegisterParticipant(Identifier, OverlayParticipant);
    }

    [[nodiscard]] bool Initialize(const EditorStartupConfiguration& Configuration = {}) noexcept;
    void               AttachUVWorkspaceHost(Workspaces::UVWorkspaceHost* TargetHost) noexcept { MountedUVHost = TargetHost; }
    [[nodiscard]] bool ExecuteAuthoringTick(double Δt);
    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               RequestTermination() noexcept;
    void               Finalize() noexcept;

    void PresentPrimaryEditorImGui(double Δt);

    [[nodiscard]] bool                                       QueryActive() const noexcept;
    [[nodiscard]] EditorLifecycleReading                     InspectReading() const noexcept;
    [[nodiscard]] Components::HeadlessWorkspaceDockContainer& AccessDockContainer() noexcept;
    [[nodiscard]] ControlCentrePanel&                        AccessControlCentre() noexcept;
    [[nodiscard]] Editor::Explorer::ContentBrowser&          AccessContentBrowser() noexcept;
    [[nodiscard]] WorkspaceLifecycle&                        AccessWorkspaces() noexcept;
    [[nodiscard]] Input::InputExtension&                     AccessInput() noexcept;
    [[nodiscard]] TypographyRepository&                      AccessTypography() noexcept;
    [[nodiscard]] Components::WorkspaceDockPaneToken         QueryOutlinerPaneToken() const noexcept;
    [[nodiscard]] Components::WorkspaceDockPaneToken         QueryViewportPaneToken() const noexcept;
    [[nodiscard]] Components::WorkspaceDockPaneToken         QueryInspectorPaneToken() const noexcept;

private:
    EditorStartupConfiguration                 StartupConfig          {};     // [-]     - Applied editor startup settings
    Components::HeadlessWorkspaceDockContainer DockContainer          {};     // [-]     - Intra-app trapezoidal docking container
    TypographyRepository                       Typography             {};     // [-]     - .FontSpace typeface & 13px tab-strip faces
    ControlCentrePanel                         ControlCentre          {};     // [-]     - Pull-down Control Centre & settings shade
    Editor::Explorer::ContentBrowser           ContentBrowserDrawer   {};     // [-]     - Pull-up ContentBrowser & ColourCodex drawer
    WorkspaceLifecycle                         Workspaces             {};     // [-]     - Multi-document workspace session loop
    Input::InputExtension                      InputCoordinator       {};     // [-]     - Hardware input & OS feedback coordinator
    LifecycleSequence                          EditorSequence         {};     // [-]     - Registered GPU .slang overlay & editor hooks
    Workspaces::UVWorkspaceHost*               MountedUVHost          = nullptr; // [-]  - Optional live UVWorkspaceHost
    Components::WorkspaceDockPaneToken         OutlinerPaneToken      {};     // [idx]   - Registered left Outliner/LayerStack pane
    Components::WorkspaceDockPaneToken         ViewportPaneToken      {};     // [idx]   - Registered central Viewport pane
    Components::WorkspaceDockPaneToken         InspectorPaneToken     {};     // [idx]   - Registered right Inspector pane
    double                                     CurrentStepSeconds     = 0.016;// [s]     - Active authoring step passed to pane presenters
    uint32_t                                   RegisteredGpuOverlays = 0u;   // [count] - Registered GPU .slang viewport overlay hooks
    bool                                       Initialized            = false;// [-]     - True between Initialize and Finalize
    bool                                       TerminationRequested   = false;// [-]     - Latch stopping authoring loop
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool EditorLifecycle::QueryActive() const noexcept
{
    return Initialized && !TerminationRequested;
}

inline Components::HeadlessWorkspaceDockContainer& EditorLifecycle::AccessDockContainer() noexcept
{
    return DockContainer;
}

inline ControlCentrePanel& EditorLifecycle::AccessControlCentre() noexcept
{
    return ControlCentre;
}

inline Editor::Explorer::ContentBrowser& EditorLifecycle::AccessContentBrowser() noexcept
{
    return ContentBrowserDrawer;
}

inline WorkspaceLifecycle& EditorLifecycle::AccessWorkspaces() noexcept
{
    return Workspaces;
}

inline Input::InputExtension& EditorLifecycle::AccessInput() noexcept
{
    return InputCoordinator;
}

inline TypographyRepository& EditorLifecycle::AccessTypography() noexcept
{
    return Typography;
}

inline Components::WorkspaceDockPaneToken EditorLifecycle::QueryOutlinerPaneToken() const noexcept
{
    return OutlinerPaneToken;
}

inline Components::WorkspaceDockPaneToken EditorLifecycle::QueryViewportPaneToken() const noexcept
{
    return ViewportPaneToken;
}

inline Components::WorkspaceDockPaneToken EditorLifecycle::QueryInspectorPaneToken() const noexcept
{
    return InspectorPaneToken;
}

} // namespace Frontier::Lifecycle

#endif // FRONTIER_EDITOR_ENABLED
