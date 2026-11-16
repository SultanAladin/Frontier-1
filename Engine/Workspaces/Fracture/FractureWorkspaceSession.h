/*============================================================================================================================================
                                                        FRACTUREWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoritative state session for Fracture Studio: solid, material, solver, execution mode, Jolt simulation, logs & diagnostics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Controls/SimulationTimelineFooter.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.h"
#include "Engine/Workspaces/Fracture/FractureViewportRenderer.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FractureNavView : uint8_t
{
    Workspace = 0u,
    Solvers   = 1u,
    LogStream = 2u,
};

enum class FractureOutlinerFilter : uint8_t
{
    All        = 0u,
    Primitives = 1u,
    Fragments  = 2u,
    Released   = 3u,
    Held       = 4u,
};

enum class FractureViewportTool : uint8_t
{
    Strike = 0u,
    Select = 1u,
    Locus  = 2u,
    Reset  = 3u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FracturePresetEntry
{
    const char*                                    Title         = "";
    const char*                                    Subtitle      = "";
    GeometrySpace::Fracture::FractureSolidCategory Solid         = GeometrySpace::Fracture::FractureSolidCategory::Monolith;
    const char*                                    MaterialId    = "concrete";
    GeometrySpace::Fracture::FractureSolverMethod  Solver        = GeometrySpace::Fracture::FractureSolverMethod::FastRuntimeBoussinesq;
    double                                         EnergyJoules  = 3200.0;
    uint32_t                                       Ceiling       = 28u;
    Mathematics::Axis3D                            ImpactPoint   { 0.0f, -0.48f, 0.25f };
    Mathematics::Axis3D                            ImpactNormal  { 0.0f, -1.0f, 0.0f };
};

struct FractureLogEntry
{
    double      TimeSec    = 0.0;
    std::string Channel    {};
    std::string Message    {};
    std::string Detail     {};
    bool        IsWarmTone = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureWorkspaceSession
{
public:
    FractureWorkspaceSession();

    void ApplyPreset(uint32_t PresetIndex) noexcept;
    void RebuildFracturePartition() noexcept;
    void ExecuteFracturePartitionOnly() noexcept;
    void StartOrResetSimulation(bool AutoPlay) noexcept;
    void TransitionExecutionPhase(ExecutionStripPhase NextPhase) noexcept;
    void TogglePlayPause() noexcept;
    void RewindToStart() noexcept;
    void StepSingleFrame() noexcept;
    void ScrubToTime(double TargetSeconds) noexcept;
    void AdvanceFrame(double DeltaSeconds) noexcept;
    void RerollSeed() noexcept;
    void ApplyCameraView(uint32_t CameraViewIdx) noexcept;
    void AppendLog(const char* Channel, std::string Message, std::string Detail, bool WarmTone = false);
    void TriggerViewportImpact(
        Mathematics::Axis3D WorldHitPoint,
        Mathematics::Axis3D WorldHitNormal,
        int32_t             HitFragmentIndex) noexcept;

    [[nodiscard]] const GeometrySpace::Fracture::FractureMaterialSpec& ActiveMaterialSpec() const noexcept;
    [[nodiscard]] GeometrySpace::Fracture::SdfVolumeFootprint          QuerySdfFootprint() const noexcept;
    [[nodiscard]] uint64_t                                             ComputeViewportRenderSignature() const noexcept;
    [[nodiscard]] double                                               QueryFragmentEnergyJ(std::size_t FragIndex) const noexcept;
    [[nodiscard]] bool                                                 QueryFragmentReleased(std::size_t FragIndex) const noexcept;
    [[nodiscard]] uint32_t                                             CountReleasedFragments() const noexcept;
    [[nodiscard]] uint32_t                                             CountHeldFragments() const noexcept;

    static constexpr uint32_t PresetCount = 6u;
    [[nodiscard]] static const std::array<FracturePresetEntry, PresetCount>& InspectPresets() noexcept;

    // Authoring & solver state
    uint32_t                                      ActivePresetIndex = 0u;
    GeometrySpace::Fracture::FractureSolidCategory SolidCategory     = GeometrySpace::Fracture::FractureSolidCategory::Monolith;
    std::string                                   MaterialId        = "concrete";
    GeometrySpace::Fracture::FractureSolverMethod SolverMethod      = GeometrySpace::Fracture::FractureSolverMethod::FastRuntimeBoussinesq;
    GeometrySpace::Fracture::FractureExecutionMode ExecutionMode    = GeometrySpace::Fracture::FractureExecutionMode::HybridBonds;

    uint32_t Seed              = 42u;
    uint32_t FragmentCeiling   = 28u;
    double   ImpactEnergyJ     = 3200.0;
    double   MinimumSizeM      = 0.045;
    double   GrainAngleDeg     = 0.0;
    double   BondStrengthScale = 1.0;
    double   ReleaseThresholdJ = 5.0;

    bool     SdfEnabled        = true;
    uint32_t SdfResolution     = 48u;
    bool     NeedleRejection   = true;
    bool     ClosedCaps        = true;

    Mathematics::Axis3D ImpactPointLocal  { 0.05f, -0.48f, 0.25f };
    Mathematics::Axis3D ImpactNormalLocal { 0.0f, -1.0f, 0.0f };

    // Geometry, partition & rigid physics runner
    GeometrySpace::Fracture::ConvexPolyhedron            SourceSolid     {};
    GeometrySpace::Fracture::FracturePartitionResult     PartitionResult {};
    SimulationSpace::PhysicsSpace::FractureSimulationRunner SimRunner    {};
    bool                                                 HasFractured    = false;
    std::vector<bool>                                    FragmentVisible {};

    // Camera & viewport render options
    FractureViewportCamera Camera          {};
    FractureRenderOptions  RenderOptions   {};
    uint32_t               CameraViewIndex = 0u; // 0=Orbit, 1=Top, 2=Front, 3=Side
    bool                   CameraDropdownOpen   = false;
    bool                   PipelineDropdownOpen = false;
    bool                   LensDropdownOpen     = false;

    // Execution phase & timeline
    ExecutionStripPhase                    ExecutionPhase      = ExecutionStripPhase::Edit;
    bool                                   Playing             = false;
    bool                                   LoopEnabled         = true;
    double                                 ElapsedSeconds      = 0.0;
    double                                 DurationSeconds     = 4.0;
    float                                  TimeScale           = 1.0f;
    bool                                   HasSimulatedData    = false;
    std::vector<float>                     KineticCurveSamples {};
    std::vector<SimulationTimelineMarker>  TimelineMarkers     {};
    SimulationTimelineFooterSession        TimelineSession     {};

    // Studio shell, Outliner & Inspector state
    FractureNavView        NavView            = FractureNavView::Workspace;
    FractureOutlinerFilter OutlinerFilter     = FractureOutlinerFilter::All;
    FractureViewportTool   ViewportTool       = FractureViewportTool::Strike;
    std::string            SearchFilterText   {};
    bool                   SearchFieldFocused = false;
    bool                   CompactOutliner    = false;
    float                  OutlinerScrollY    = 0.0f;
    float                  InspectorScrollY   = 0.0f;
    uint32_t               ActiveInspectorTab = 0u; // 0=Overview, 1=Material, 2=Solver, 3=Receipt
    bool                   ShowSolverMatrix   = false;
    bool                   ShowLogDrawer      = false;
    bool                   ShowReticle        = true;
    bool                   ViewportMaximized  = false;

    // Slider active drag tracking (-1 = none)
    int32_t                ActiveSliderId     = -1;

    // Log stream & diagnostics overlay
    std::vector<FractureLogEntry>     LogEntries         {};
    DiagnosticsOverlaySession         DiagnosticsSession {};
    std::vector<float>                FrameTimeSamplesMs { 16.67f };
    float                             LastFrameTimeMs    = 16.67f;
    float                             LastRenderTimeMs   = 4.2f;
    uint32_t                          LastDrawnTriangles = 0u;
    bool                              VulkanSpirvReady   = false;
    uint64_t                          RevisionCounter    = 1ull;
};

} // namespace Frontier::Workspaces::Fracture
