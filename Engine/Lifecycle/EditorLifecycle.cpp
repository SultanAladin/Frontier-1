/*============================================================================================================================================
                                                            EDITORLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Editor-only authoring loop implementation

#include "Engine/Lifecycle/EditorLifecycle.h"

#if FRONTIER_EDITOR_ENABLED

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

EditorLifecycle::~EditorLifecycle() noexcept
{
    Finalize();
}

bool EditorLifecycle::Initialize(const EditorStartupConfiguration& Configuration) noexcept
{
    Finalize();

    StartupConfig        = Configuration;
    TerminationRequested = false;
    CurrentStepSeconds   = 1.0 / 60.0;

    if (!DockContainer.Initialize(StartupConfig.WindowDescriptor))
    {
        return false;
    }

    InputCoordinator.Initialize();

    if (!Workspaces.Initialize(false))
    {
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    WorkspaceDocumentDescriptor DefaultDocument{};
    DefaultDocument.DocumentTitle   = "Scene";
    DefaultDocument.OutlinerVariant = StartupConfig.OutlinerHeading;
    DefaultDocument.InitialMode     = ViewportExecutionMode::Edit;
    if (!Workspaces.OpenSession(DefaultDocument).Valid())
    {
        Workspaces.Finalize();
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (StartupConfig.ProjectSpacePath.empty())
    {
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();
    }
    else
    {
        (void)ControlCentre.AttachProjectSpace(StartupConfig.ProjectSpacePath);
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& Io = ImGui::GetIO();
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        if (!StartupConfig.FontRepositoryRoot.empty() && Typography.LoadRepository(StartupConfig.FontRepositoryRoot) > 0u)
        {
            TypographyRepository::AssignCurrent(&Typography);
            ControlCentre.AttachTypefaces(&Typography);
            if (ImFont* TabFont = Typography.QueryTabStripFont())
            {
                Io.FontDefault = TabFont;
            }
        }

        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    ControlCentre.ConfigureWorkspaceCallbacks(
        [this](bool Allowed) noexcept
        {
            DockContainer.ConfigureDetachedWindowsAllowed(Allowed);
        },
        [this]() noexcept
        {
            DockContainer.ResetPrimaryLayout();
        });
    DockContainer.ConfigureDetachedWindowsAllowed(ControlCentre.QueryDetachedWindowsAllowed());

    if (MountedUVHost != nullptr)
    {
        MountedUVHost->AttachContentBrowser(&ContentBrowserDrawer);
        MountedUVHost->RegisterDockPanes(DockContainer);
    }
    else
    {
        const char* LeftPaneTitle = (StartupConfig.OutlinerHeading == OutlinerTitleVariant::LayerStack) ? "LayerStack" : "Outliner";

        Components::WorkspaceDockPaneDescriptor LeftDesc{};
        LeftDesc.PaneTitle         = LeftPaneTitle;
        LeftDesc.InitialDirection  = Components::DockPlacementDirection::SplitLeft;
        LeftDesc.InitialSplitRatio = 0.21f;
        LeftDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
        LeftDesc.ContentPresenter  = [this]()
        {
            if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
            {
                Panes->Outliner.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
            }
        };
        OutlinerPaneToken = DockContainer.RegisterPane(LeftDesc);

        Components::WorkspaceDockPaneDescriptor CenterDesc{};
        CenterDesc.PaneTitle         = "Viewport";
        CenterDesc.InitialDirection  = Components::DockPlacementDirection::CenterTabGroup;
        CenterDesc.InitialSplitRatio = 0.58f;
        CenterDesc.Directives        = Components::PaneDirectiveMask::CentralViewportDirectives;
        CenterDesc.ContentPresenter  = [this]()
        {
            if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
            {
                Panes->Viewport.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
            }
        };
        ViewportPaneToken = DockContainer.RegisterPane(CenterDesc);

        Components::WorkspaceDockPaneDescriptor RightDesc{};
        RightDesc.PaneTitle         = "Inspector";
        RightDesc.InitialDirection  = Components::DockPlacementDirection::SplitRight;
        RightDesc.InitialSplitRatio = 0.21f;
        RightDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
        RightDesc.ContentPresenter  = [this]()
        {
            if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
            {
                Panes->Inspector.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
            }
        };
        InspectorPaneToken = DockContainer.RegisterPane(RightDesc);
    }

    if (!EditorSequence.Initialize())
    {
        Workspaces.Finalize();
        InputCoordinator.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        Components::ConfigureImGuiWorkspaceStyle(DockContainer.ActiveTabMetrics());
        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    Initialized = true;
    return true;
}

bool EditorLifecycle::ExecuteAuthoringTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    InputCoordinator.BeginTick();
    DockContainer.PollOperatingWindows(InputCoordinator);
    (void)InputCoordinator.TickExchange();

    if (Windowing::HeadlessPlatformWindow* Primary = DockContainer.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
    {
        if (Primary->CloseRequested())
        {
            TerminationRequested = true;
        }
        else
        {
            (void)InputCoordinator.FlushOutboundDirectives(*Primary);
        }
    }

    if (!TerminationRequested)
    {
        Workspaces.Tick(SafeStep);
        EditorSequence.Tick(SafeStep);
    }

    return !TerminationRequested;
}

void EditorLifecycle::PresentPrimaryEditorImGui(double Δt)
{
    if (!Initialized || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? Δt : CurrentStepSeconds;
    CurrentStepSeconds    = SafeStep;

    Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    ContentBrowserDrawer.AttachThemeRepository(&ControlCentre.QueryThemeRepository());
    DockContainer.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });

    if (MountedUVHost == nullptr)
    {
        if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
        {
            ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
        }
    }

    const Components::HostWindowReading HostReading = DockContainer.InspectHostWindow(Components::HostWindowToken{ 0u });
    if (HostReading.Active && HostReading.SurfaceExtent.PixelWidth > 0u && HostReading.SurfaceExtent.PixelHeight > 0u)
    {
        const Rectangle2D Bounds{
            0.0f,
            0.0f,
            static_cast<float>(HostReading.SurfaceExtent.PixelWidth),
            static_cast<float>(HostReading.SurfaceExtent.PixelHeight),
        };
        const float RefreshHz = HostReading.SurfaceExtent.RefreshRateHz > 0u
                                    ? static_cast<float>(HostReading.SurfaceExtent.RefreshRateHz)
                                    : 60.0f;
        const float ScaleFactor = HostReading.SurfaceExtent.DpiScaleFactor > 0.5f
                                      ? HostReading.SurfaceExtent.DpiScaleFactor
                                      : 1.0f;
        ContentBrowserDrawer.Render(Bounds, SafeStep, ScaleFactor);
        ControlCentre.Render(Bounds, SafeStep, RefreshHz);
    }
}

bool EditorLifecycle::Suspend() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool OverlaySuspended   = EditorSequence.Suspend();
    const bool WorkspaceSuspended = Workspaces.Suspend();
    return OverlaySuspended && WorkspaceSuspended;
}

bool EditorLifecycle::Resume() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool WorkspaceResumed = Workspaces.Resume();
    const bool OverlayResumed   = EditorSequence.Resume();
    return WorkspaceResumed && OverlayResumed;
}

void EditorLifecycle::RequestTermination() noexcept
{
    TerminationRequested = true;
}

void EditorLifecycle::Finalize() noexcept
{
    if (!Initialized)
    {
        EditorSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down GPU overlay hooks, then workspace sessions, then input, then dock windows.
    EditorSequence.Finalize();
    Workspaces.Finalize();

    if (TypographyRepository::QueryCurrent() == &Typography)
    {
        TypographyRepository::AssignCurrent(nullptr);
    }

    InputCoordinator.Finalize();
    DockContainer.Finalize();

    OutlinerPaneToken    = {};
    ViewportPaneToken    = {};
    InspectorPaneToken   = {};
    Initialized          = false;
    TerminationRequested = false;
}

EditorLifecycleReading EditorLifecycle::InspectReading() const noexcept
{
    EditorLifecycleReading Reading{};
    Reading.SequencePhase       = EditorSequence.QueryPhase();
    Reading.SequenceMetrics     = EditorSequence.InspectMetrics();
    Reading.ActiveWindowCount   = DockContainer.ActiveOperatingWindowCount();
    Reading.RegisteredPaneCount = static_cast<uint32_t>(DockContainer.RegisteredPaneCount());
    Reading.OpenWorkspaceCount  = Workspaces.OpenSessionCount();
    Reading.GpuOverlayCount     = RegisteredGpuOverlays;
    Reading.ControlCentreOpen   = ControlCentre.QueryOpen();
    Reading.ContentBrowserOpen  = ContentBrowserDrawer.QueryPanel().QueryOpen();
    Reading.AuthoringActive     = QueryActive();
    return Reading;
}

} // namespace Frontier::Lifecycle

#endif // FRONTIER_EDITOR_ENABLED
