/*============================================================================================================================================
                                                        FRACTUREWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoritative Fracture Editor session state: 9 analytical primitives, dual Outside/Interior materials, Baked vs Realtime, 6 solvers & Jolt sim

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Controls/SimulationTimelineFooter.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/GeometrySpace/Fracture/FractureMaterialRepository.h"
#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.h"
#include "Engine/Workspaces/Fracture/FractureIcons.h"
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

enum class FractureOutlinerFilter : uint8_t
{
    All      = 0u,
    Released = 1u,
    Held     = 2u,
    Hidden   = 3u,
    Seams    = 4u,
};

enum class FractureMethodMode : uint8_t
{
    Baked    = 0u,
    Realtime = 1u,
};

enum class FractureFaceDetail : uint8_t
{
    Low    = 0u,
    Medium = 1u,
    High   = 2u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FracturePresetEntry
{
    const char*                               Title          = "";
    const char*                               Subtitle       = "";
    FractureIcons::PrimitiveShapeId           Shape          = FractureIcons::PrimitiveShapeId::Torus;
    GeometrySpace::Fracture::FractureSolidCategory SolidCategory = GeometrySpace::Fracture::FractureSolidCategory::Boulder;
    const char*                               SurfaceMatId   = "concrete";
    const char*                               MaterialId     = "concrete";
    GeometrySpace::Fracture::FractureSolverMethod Solver     = GeometrySpace::Fracture::FractureSolverMethod::ClusteredVoronoi;
    GeometrySpace::Fracture::FractureExecutionMode ExecMode  = GeometrySpace::Fracture::FractureExecutionMode::Baked;
    double                                    ImpactEnergyJ  = 2500.0;
    uint32_t                                  Seed           = 42u;
    uint32_t                                  FragmentCeiling = 48u;
    FractureRenderViewMode                    DefaultLens    = FractureRenderViewMode::Energy;
};

struct FracturePrimitiveDescriptor
{
    FractureIcons::PrimitiveShapeId Shape       = FractureIcons::PrimitiveShapeId::Box;
    const char*                     Id          = "box";
    const char*                     Title       = "Box";
    const char*                     Formula     = "W · H · D";
    const char*                     FooterTitle = "Analytical box";
    bool                            Segmented   = false;
};

struct FractureTimingBreakdown
{
    double SeedingMs  = 5.40;
    double CellsMs    = 81.0;
    double NeedlesMs  = 57.5;
    double SortMs     = 0.70;
    double TotalMs    = 144.6;
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
    static constexpr uint32_t PresetCount    = 6u;
    static constexpr uint32_t PrimitiveCount = 9u;

    FractureWorkspaceSession();

    static const std::array<FracturePresetEntry, PresetCount>&        InspectPresets() noexcept;
    static const std::array<FracturePrimitiveDescriptor, PrimitiveCount>& InspectPrimitives() noexcept;

    void ApplyPreset(uint32_t PresetIndex);
    void ApplyPrimitiveShape(FractureIcons::PrimitiveShapeId Shape);
    void ApplySolverMethod(GeometrySpace::Fracture::FractureSolverMethod Method);
    void ApplyCameraView(uint32_t ViewIndex) noexcept;
    void RebuildSourceGeometry();
    void RebuildFracturePartition();
    void ExecuteFracturePartitionOnly();

    void TriggerViewportImpact(
        Mathematics::Axis3D WorldPoint,
        Mathematics::Axis3D WorldNormal,
        int32_t             HitFragmentIndex = -1);

    void TransitionExecutionPhase(ExecutionStripPhase TargetPhase);
    void TogglePlayPause();
    void RewindToStart();
    void StepSingleFrame();
    void ScrubToTime(double TimeSeconds);
    void Tick(double DeltaSeconds);

    [[nodiscard]] static const char* QuerySolverShortLabel(GeometrySpace::Fracture::FractureSolverMethod Method) noexcept;
    [[nodiscard]] const GeometrySpace::Fracture::FractureMaterialSpec& ActiveSurfaceMaterialSpec() const noexcept;
    [[nodiscard]] const GeometrySpace::Fracture::FractureMaterialSpec& ActiveMaterialSpec() const noexcept;
    [[nodiscard]] std::string                                          FormatPrimitiveSubtitle() const;
    [[nodiscard]] double                                               QueryExactVolumeM3() const noexcept;
    [[nodiscard]] double                                               QueryExactAreaM2() const noexcept;
    [[nodiscard]] uint32_t                                             CountReleasedFragments() const noexcept;
    [[nodiscard]] uint32_t                                             CountHeldFragments() const noexcept;
    [[nodiscard]] float                                                QueryFragmentReleaseShareJ(uint32_t Index) const noexcept;
    [[nodiscard]] bool                                                 QueryFragmentReleased(uint32_t Index) const noexcept;
    [[nodiscard]] uint64_t                                             ComputeViewportRenderSignature() const noexcept;
    [[nodiscard]] std::string                                          BuildWorkspaceReportMarkdown() const;

    // Active primitive & dimensions
    FractureIcons::PrimitiveShapeId                ActiveShape         = FractureIcons::PrimitiveShapeId::Torus;
    FractureFaceDetail                            FacetDetail         = FractureFaceDetail::Medium;
    uint32_t                                       SourceSegmentCount  = 40u;
    uint32_t                                       SourceFaceCount     = 800u;
    float                                          DimPrimaryA         = 0.50f; // Torus R / Box W / Sphere R / Pipe R / Pane W / Hex F
    float                                          DimPrimaryB         = 0.19f; // Torus r / Box H / Cyl H / Cone H / Cap L / Pipe r / Pane H / Hex H
    float                                          DimPrimaryC         = 0.80f; // Box D / Pipe H / Pane T(mm)

    // Preset & solver state
    uint32_t                                       ActivePresetIndex   = 0u;
    GeometrySpace::Fracture::FractureSolidCategory SolidCategory       = GeometrySpace::Fracture::FractureSolidCategory::Boulder;
    GeometrySpace::Fracture::FractureSolverMethod  SolverMethod        = GeometrySpace::Fracture::FractureSolverMethod::ClusteredVoronoi;
    GeometrySpace::Fracture::FractureExecutionMode ExecutionMode       = GeometrySpace::Fracture::FractureExecutionMode::Baked;
    FractureMethodMode                             MethodMode          = FractureMethodMode::Baked;
    uint32_t                                       RealtimeBudgetMs    = 36u;
    std::string                                    SurfaceMaterialId   = "concrete";
    std::string                                    MaterialId          = "concrete";

    // Impact & pattern recipe
    double                                         ImpactEnergyJ       = 2500.0;
    double                                         ImpactRadiusM       = 0.04;
    bool                                           DirModeCustom       = false;
    double                                         DirYawDeg           = 0.0;
    double                                         DirPitchDeg         = 0.0;
    uint32_t                                       Seed                = 42u;
    uint32_t                                       FragmentCeiling     = 48u;
    double                                         MinFragmentSpanM    = 0.014;
    double                                         GrainAngleDeg       = 0.0;
    double                                         BondStrengthScale   = 1.0;
    bool                                           NeedleRejection     = true;
    bool                                           ClosedCaps          = true;
    bool                                           QualityFans         = true;
    bool                                           ExactNormals        = true;

    // Simulation recipe
    double                                         TransferRatio       = 0.72;
    double                                         ReleaseThresholdJ   = 5.0;
    double                                         GravityMps2         = 9.81;
    double                                         Restitution         = 0.14;
    double                                         Friction            = 0.78;
    bool                                           CollisionsEnabled   = true;
    SimulationSpace::PhysicsSpace::FractureCollisionShapeMode CollisionShapeMode = SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::ConvexHull;

    Mathematics::Axis3D                            ImpactPointLocal    { 0.46f, 0.50f, 0.06f };
    Mathematics::Axis3D                            ImpactNormalLocal   { 0.65f, 0.72f, 0.24f };
    Mathematics::Scalar3D                          SolidScale          { 1.0f, 1.0f, 1.0f };

    // Geometry, Partition & Simulation Runtime
    GeometrySpace::Fracture::ConvexPolyhedron      SourceSolid         {};
    std::vector<GeometrySpace::Fracture::ConvexPolyhedron> SourceParts {};
    GeometrySpace::Fracture::FracturePartitionResult PartitionResult   {};
    SimulationSpace::PhysicsSpace::FractureSimulationRunner SimRunner  {};
    bool                                           HasFractured        = false;
    bool                                           HasSimulatedData    = false;

    // Viewport & Camera
    FractureViewportCamera                         Camera              {};
    FractureRenderOptions                          RenderOptions       {};
    std::vector<bool>                              FragmentVisibleFlags {};
    uint32_t                                       CameraViewIndex     = 0u;
    bool                                           AimingImpactTool    = false;
    bool                                           PrimitiveLibraryOpen = false;
    bool                                           ShowReportShade     = false;
    bool                                           ViewportMaximized   = false;
    bool                                           ShowReticle         = true;
    bool                                           OutlinerCollapsed   = false;
    bool                                           CompactOutliner     = false;

    // Outliner state
    FractureOutlinerFilter                         OutlinerFilter      = FractureOutlinerFilter::All;
    TextEditBuffer                                 OutlinerSearch      {};
    std::array<bool, 3>                            GroupExpanded       { true, false, true };
    bool                                           ShowAllFragments    = false;
    bool                                           FilterDropdownOpen  = false;
    bool                                           ShadingDropdownOpen = false;
    bool                                           DebugDropdownOpen   = false;
    bool                                           MaterialOutsideOpen = false;
    bool                                           MaterialInsideOpen  = false;

    // Inspector UI state
    std::array<bool, 10>                           SectionExpanded     { true, true, true, false, false, false, true, false, false, false };
    int32_t                                        ActiveSliderId      = -1;
    float                                          OutlinerScrollY     = 0.0f;
    float                                          InspectorScrollY    = 0.0f;
    int32_t                                        FragmentOverrideMode = 0; // 0=Auto, 1=Hold, 2=Release

    // Execution, Timeline & Diagnostics
    ExecutionStripPhase                            ExecutionPhase      = ExecutionStripPhase::Edit;
    double                                         ElapsedSeconds      = 0.0;
    double                                         DurationSeconds     = 4.0;
    bool                                           Playing             = false;
    bool                                           LoopEnabled         = true;
    float                                          TimeScale           = 1.0f;
    SimulationTimelineFooterSession                TimelineSession     {};
    std::vector<float>                             KineticCurveSamples {};
    std::vector<SimulationTimelineMarker>          TimelineMarkers     {};

    DiagnosticsOverlaySession                      DiagnosticsSession  {};
    std::vector<float>                             FrameTimeSamplesMs  {};
    FractureTimingBreakdown                        LastTimings         {};
    float                                          LastFrameTimeMs     = 16.67f;
    float                                          LastRenderTimeMs    = 2.10f;
    uint32_t                                       LastDrawnTriangles  = 0u;
    bool                                           VulkanSpirvReady    = false;
    uint32_t                                       SessionRevision     = 1u;

    std::vector<FractureLogEntry>                  LogEntries          {};
    std::string                                    StatusBannerText    {};

private:
    void AppendLog(const char* Channel, std::string Message, std::string Detail, bool WarmTone = false);
};

} // namespace Frontier::Workspaces::Fracture
