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
#include "Engine/Components/Diagnostics/FrameTimingTelemetry.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Editor/Explorer/ContentBrowser.h"
#include "Engine/GeometricRaster/Gizmo/TransformGizmo.h"
#include "Engine/Editor/Workspaces/Simulation/ForceInspectorView.h"
#include "Engine/Editor/Workspaces/Simulation/ForcePresetBrowserPanel.h"
#include "Engine/GeometricRaster/Gizmo/WorldGizmoOverlayRasterization.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/Simulation/VectorFieldOverlaySequence.h"
#include "Engine/Input/InputExtension.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/Lifecycle/WorkspaceLifecycle.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/Windowing/PlatformWindow.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <thread>
#include <memory>
#include <string_view>
#include <vector>

namespace Frontier::GeometricRaster
{
class ImGuiOverlayRasterization;
}

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
    OutlinerTitleVariant                  OutlinerHeading         = OutlinerTitleVariant::Outliner; // [-] - Outliner vs LayerStack heading
    double                                MaximumStep             = 0.100;                          // [s] - Upper Δt clamp per authoring tick
    bool                                        PopulateShowcaseScene   = false;                          // [-] - Populate Cornell-Studio Gallery in Outliner & GTX Viewport
    GeometricRaster::VulkanInterchange*         VulkanDevice            = nullptr;                        // [-] - Optional hardware Vulkan device for GPU .slang World & Gizmo passes
    GeometricRaster::ImGuiOverlayRasterization* ImGuiPresenter          = nullptr;                        // [-] - Optional ImGui Vulkan presenter for zero-readback GPU viewport
    std::filesystem::path                       CompiledShaderDirectory {};                               // [-] - Optional CompiledShaders/ path for GPU .slang SPIR-V modules
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
    [[nodiscard]] bool AttachVulkanGizmoOverlay(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const std::filesystem::path&        CompiledShaderDirectory);
    void               AssignReturnToProjectBrowserCallback(std::function<void()> Callback);
    void               ConfigureActiveProjectTitle(std::string_view ProjectTitle);
    void               PopulateShowcaseGallerySession(uint32_t RenderWidthPx = 640u, uint32_t RenderHeightPx = 384u);
    void               RefreshShowcaseViewportRaster();
    void               FrameShowcaseCameraSelection() noexcept;
    [[nodiscard]] OutlinerSlotToken SpawnForceFieldEntry(const SimulationSpace::ForceFields::ForceSourceRecordEntry& SourcePreset);
    [[nodiscard]] GeometricRaster::ShowcaseWorldSceneBundle* AccessShowcaseScene() noexcept { return ShowcaseBundle.get(); }
    [[nodiscard]] Editor::TransformGizmo&                    AccessTransformGizmo() noexcept { return WorldTransformGizmo; }
    [[nodiscard]] SimulationSpace::ForceFields::ForceFieldAccumulator& AccessForceAccumulator() noexcept { return ForceAccumulator; }
    [[nodiscard]] bool ExecuteAuthoringTick(double Δt);
    [[nodiscard]] bool ExecuteExternalDesktopTick(double Δt);
    [[nodiscard]] bool Suspend() noexcept;
    [[nodiscard]] bool Resume() noexcept;
    void               RequestTermination() noexcept;
    void               Finalize() noexcept;

    void PresentPrimaryEditorImGui(double Δt);
    void RecordFramePresentationTiming(float DesktopPollAndUiMs, float SwapchainPresentMs, float TotalFrameMs) noexcept;
    [[nodiscard]] const FrameTimingBreakdown& QueryLatestFrameTiming() const noexcept { return LatestFrameTiming; }

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
    struct MappedEditorForceSource
    {
        OutlinerSlotToken                                  OutlinerSlot {};
        SimulationSpace::ForceFields::ForceSourceRecordEntry Source       {};
    };

    void AttachForceFieldAuthoringToActivePanes();
    void SynchronizeForceFieldsWithOutliner(double Δt);
    void SynchronizeShowcaseGizmoAnchor() noexcept;
    [[nodiscard]] bool UpdateForceFieldOverlayCache(uint32_t WidthPx, uint32_t HeightPx, bool& OutUpdated);
    void CompositeForceFieldOverlayOntoBuffer(std::vector<uint8_t>& InOutRgba, uint32_t WidthPx, uint32_t HeightPx);
    void RefreshGizmoOverlayFromCachedWorld();
    void ProcessShowcaseViewportInteraction(uint32_t PaneIndex, Rectangle2D PaneBounds, const PaneInputReading& Input);
    void ProcessShowcaseNavigationDirective(ViewportNavigationDirective Directive);
    void SynchronizeContentBrowserColourTargets();
    void SynchronizeShowcaseRendererAfterEdit(bool RebuildGeometry = false);
    void HandleContentDropOnViewport(const ContentRepositoryEntry& Entry, Point2D DropLogicalPos, Rectangle2D ViewportDropBounds);
    void HandleContentDropOnOutliner(const ContentRepositoryEntry& Entry, Point2D DropLogicalPos, Rectangle2D OutlinerDropBounds);
    void InvalidateRtxLattice() noexcept;
    void BeginAsyncRtxLatticeBake();
    [[nodiscard]] bool ConsumeCompletedRtxLatticeBake();
    void JoinRtxLatticeThread() noexcept;

    EditorStartupConfiguration                 StartupConfig          {};     // [-]     - Applied editor startup settings
    Components::HeadlessWorkspaceDockContainer DockContainer          {};     // [-]     - Intra-app trapezoidal docking container
    TypographyRepository                       Typography             {};     // [-]     - .FontSpace typeface & 13px tab-strip faces
    ControlCentrePanel                         ControlCentre          {};     // [-]     - Pull-down Control Centre & settings shade
    Editor::Explorer::ContentBrowser           ContentBrowserDrawer   {};     // [-]     - Pull-up ContentBrowser & ColourCodex drawer
    WorkspaceLifecycle                         Workspaces             {};     // [-]     - Multi-document workspace session loop
    Input::InputExtension                      InputParticipant       {};     // [-]     - Hardware input & OS feedback participant
    LifecycleSequence                          EditorSequence         {};     // [-]     - Registered GPU .slang overlay & editor hooks
    std::unique_ptr<GeometricRaster::ShowcaseWorldSceneBundle>       ShowcaseBundle       {}; // [-] - Mounted Cornell-Studio Gallery scene & Outliner tokens
    std::unique_ptr<GeometricRaster::TieredWorldRenderSequence>      ShowcaseRenderer     {}; // [-] - Calibrated Tier 1 GTX / Tier 2 RTX world renderer
    std::unique_ptr<GeometricRaster::VulkanWorldRenderSequence>      GpuWorldRenderer     {}; // [-] - Hardware Vulkan .slang compute world render sequence
    std::unique_ptr<GeometricRaster::WorldGizmoOverlayRasterization> WorldGizmoRasterizer {}; // [-] - Vulkan .slang 3D Gizmo overlay rasterizer
    std::unique_ptr<Editor::Simulation::VectorFieldOverlaySequence>  ForceOverlayRasterizer{}; // [-] - Vulkan 1.3 dynamic-rendering ForceField debug overlay
    std::vector<uint8_t>                                             CachedWorldRgba8     {}; // [-] - Clean world RGBA8 buffer before gizmo overlay rasterization
    uint32_t                                                         CachedWorldWidthPx   = 0u;
    uint32_t                                                         CachedWorldHeightPx  = 0u;
    VkImageView                                                      LastWorldImageView   = VK_NULL_HANDLE;
    uint32_t                                                         LastTargetWidthPx    = 0u;
    uint32_t                                                         LastTargetHeightPx   = 0u;
    uint32_t                                                         LastCanvasPaneWidth  = 0u;
    uint32_t                                                         LastCanvasPaneHeight = 0u;
    uint32_t                                                         LastViewportTier     = UINT32_MAX;
    uint32_t                                                         LastViewportDebug    = UINT32_MAX;
    uint32_t                                                         LastRenderEngine     = UINT32_MAX;
    uint32_t                                                         LastShadowAlgorithm  = UINT32_MAX;
    uint32_t                                                         LastReflectionSource = UINT32_MAX;
    uint32_t                                                         LastTraceResolutionIdx = UINT32_MAX;
    bool                                                             LastGiEnabled        = true;
    bool                                                             LastReflectionsEnabled = true;
    bool                                                             LastAaEnabled        = true;
    bool                                                             LastRtEnabled        = true;
    bool                                                             LastAtmosphereSky      = true;
    bool                                                             LastCloudsEnabled      = true;
    bool                                                             LastCloudShadowsEnabled = true;
    bool                                                             LastCelestialsEnabled  = true;
    float                                                            LastSolarTimeHours     = 10.5f;
    float                                                            LastCloudFraction      = 0.34f;
    float                                                            LastCloudWindSpeedMps  = 7.0f;
    SimulationSpace::ForceFields::ForceOverlaySettings               LastForceOverlaySettings {};
    uint32_t                                                         LastForceSourceCount   = 0u;
    uint32_t                                                         PathTraceAccumIndex  = 0u;
    uint32_t                                                         LastPtEngineIndex    = UINT32_MAX;
    uint32_t                                                         LastPtShadowAlg      = UINT32_MAX;
    uint32_t                                                         LastPtReflSrc        = UINT32_MAX;
    uint32_t                                                         LastPtDebugChan      = UINT32_MAX;
    uint32_t                                                         LastPtQualityTier    = UINT32_MAX;
    uint32_t                                                         LastPtTraceResIdx    = UINT32_MAX;
    uint32_t                                                         LastPtWidthPx        = 0u;
    uint32_t                                                         LastPtHeightPx       = 0u;
    bool                                                             LastPtAtmoSky        = true;
    bool                                                             LastPtClouds         = true;
    bool                                                             LastPtCelestials     = true;
    bool                                                             LastPtGi             = true;
    bool                                                             LastPtRefl           = true;
    bool                                                             LastPtAa             = true;
    bool                                                             LastPtRt             = true;
    float                                                            LastPtSolarHours     = 10.5f;
    float                                                            LastPtCloudFraction  = 0.34f;
    ViewportProjectionPreset                                         LastPrimaryProjection = ViewportProjectionPreset::Perspective;
    Mathematics::Axis3D                                              LastPtCameraOrigin   {};
    Mathematics::Axis3D                                              LastPtCameraTarget   {};
    int32_t                                                          LastRenderScalePct   = 100;
    bool                                                             RtxLatticePrepared   = false;
    std::thread                                                      RtxLatticeThread     {};
    std::mutex                                                       RtxLatticeMutex      {};
    GeometricRaster::IrradianceLatticeSequence                       PendingRtxLattice    {};
    std::atomic<bool>                                                RtxLatticeReady      { false };
    std::atomic<bool>                                                RtxLatticeInFlight   { false };
    uint64_t                                                         RtxLatticeGeneration = 0ull;
    uint64_t                                                         PendingRtxLatticeGen = 0ull;
    Editor::TransformGizmo                                           WorldTransformGizmo  {}; // [-] - 13-handle 3D Slate GizmoPRO manipulator
    Mathematics::SpatialTransform                                    GizmoActiveTransform {}; // [-] - Working SpatialTransform during active drag
    OutlinerSlotToken                                                LastGizmoSlot        {}; // [id]- Slot currently anchored by WorldTransformGizmo
    Point2D                                                          LastPointerPosition  {}; // [px]- Previous frame viewport pointer coordinate
    bool                                                             PointerTrackingValid = false;
    bool                                                             CameraInteractiveDrag = false;
    bool                                                             PendingInteractionWorldRefresh = false;
    bool                                                             PendingInteractionGizmoRefresh = false;
    bool                                                             PendingInteractionSceneSync    = false;
    bool                                                             PendingInteractionFastDrag     = false;
    std::function<void()>                                            ReturnToBrowserCallback {};
    std::string                                                      ActiveProjectTitle   {};
    SimulationSpace::ForceFields::ForceFieldAccumulator ForceAccumulator    {};   // [-]  - Analytical ForceModel / VectorField evaluator (no physics)
    SimulationSpace::ForceFields::FieldLatticeCascade   ForceLatticeCascade {};   // [-]  - 4-tier ForceField clipmap metrics
    Editor::Simulation::ForceInspectorView              ForceInspector      {};   // [-]  - 4-tab ForceField Inspector (Field/Formula/Layers/Cache)
    ForcePresetBrowserPanel                             ForcePresetCatalog  {};   // [-]  - Curated .ForceModel & Formula preset library
    std::vector<MappedEditorForceSource>                AuthoredForces      {};   // [-]  - Scene-bound ForceField sources
    std::array<float, 96>                               AnemometerHistory   {};   // [m/s]- Rolling Beaufort probe samples
    OutlinerSlotToken                                   DefaultForcesFolder {};   // [idx]- Optional Forces folder in Outliner
    int32_t                                             SelectedWaypointIdx = -1; // [idx]- Active ForceField motion waypoint
    double                                              ForceClockSeconds   = 0.0;// [s]  - ForceField evaluation clock
    uint32_t                                            NextForceToken      = 1u; // [id] - Monotonic ForceSourceRecordEntry token
    Components::WorkspaceDockPaneToken         OutlinerPaneToken      {};     // [idx]   - Registered left Outliner/LayerStack pane
    Components::WorkspaceDockPaneToken         ViewportPaneToken      {};     // [idx]   - Registered central Viewport pane
    Components::WorkspaceDockPaneToken         InspectorPaneToken     {};     // [idx]   - Registered right Inspector pane
    double                                     CurrentStepSeconds     = 0.016;// [s]     - Active authoring step passed to pane presenters
    uint32_t                                   RegisteredGpuOverlays = 0u;   // [count] - Registered GPU .slang viewport overlay hooks
    FrameTimingBreakdown                       LatestFrameTiming      {};     // [ms]    - Latest per-stage GPU/CPU telemetry
    bool                                       TimingLogInitialized   = false;
    uint64_t                                   TimingFrameCounter     = 0ull;
    std::chrono::steady_clock::time_point      LastForceOverlayRenderAt {};
    std::vector<uint8_t>                       CachedForceOverlayRgba {};
    uint32_t                                   CachedForceOverlayW    = 0u;
    uint32_t                                   CachedForceOverlayH    = 0u;
    Mathematics::Axis3D                        LastForceOverlayCamOrigin {};
    Mathematics::Axis3D                        LastForceOverlayCamTarget {};
    uint32_t                                   LastForceOverlaySelection = 0u;
    bool                                       ForceOverlayCacheDirty = true;
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
    return InputParticipant;
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
