/*============================================================================================================================================
                                                          PARTICLEWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoritative Particle Physics Workspace UI session holding scene tabs, authored particle rigs, Vector Field Forces, presets & telemetry

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/ColourAssetRegister.h"
#include "Engine/ColourSpace/ColourSourceTypes.h"
#include "Engine/Components/Controls/ColourRampEditor.h"
#include "Engine/Components/Controls/ColourSwatchPicker.h"
#include "Engine/Components/Controls/ScalarCurveEditor.h"
#include "Engine/Components/Controls/SimulationTimeRuler.h"
#include "Engine/Components/Diagnostics/LogStreamDrawer.h"
#include "Engine/ParticleSpace/ParticleSimulationSession.h"
#include "Engine/GeometricRaster/Compute/ParticleVulkanExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ParticleNavView : uint8_t
{
    Workspace = 0u, // [-] - Three-panel live particle studio workspace
    Assets    = 1u, // [-] - Modal 93-preset Particle & VFX Asset Browser overlay
    LogStream = 2u, // [-] - Focuses / toggles collapsible Log Stream drawer
};

enum class ParticleInspectorTab : uint8_t
{
    Overview      = 0u, // [-] - Capacity metric, Bokeh/Schematic infographic, 3-KPI highlight trio, 4 Quick Controls
    Source        = 1u, // [-] - Release origin, capacity, rate, 3D centre/extents, kinematics & ScalarCurveEditor
    MotionFx      = 2u, // [-] - Visual carrier shape, optical blend, birth/death sizes, ColourRampEditor & ColourSwatchPicker
    MetricsCodec = 3u, // [-] - Async GPU reduction telemetry & .ParticleSpace TOML record preview
};

enum class ParticleSelectionCategory : uint8_t
{
    ParticleRig     = 0u, // [-] - Authored particle rig selected
    ForceFieldRoot  = 1u, // [-] - Global Force Field lattice container selected
    AmbientWind     = 2u, // [-] - Prevailing ambient wind selected
    ForceSource     = 3u, // [-] - Specific SimulationSpace::ForceFields::ForceSourceRecordEntry selected
};

enum class ViewportProjectionCategory : uint8_t
{
    PerspectiveOrbit = 0u,
    TopLattice       = 1u,
    FrontAxis        = 2u,
    SideElevation    = 3u,
};

enum class FieldOverlayChannel : uint8_t
{
    CleanViewport = 0u, // [-] - No overlay
    VelocityDrift = 1u, // [-] - Wind & stream speed (m/s)
    Acceleration  = 2u, // [-] - Force acceleration (m/s^2)
    Damping       = 3u, // [-] - Drag decay (1/s)
};

enum class ViewportToolCategory : uint8_t
{
    OrbitCamera = 0u,
    Handles3D   = 1u,
    ThrowBall   = 2u,
    WindProbe   = 3u,
    Turntable   = 4u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleSceneTabEntry
{
    std::string Identifier {};
    std::string Title      {};
};

struct AuthoredParticleRigUi
{
    std::string                      Identifier          {};
    std::string                      DisplayName         {};
    std::string                      CategoryTitle       { "Silk & Curl" };
    std::string                      FilterTag           { "Curl" };
    std::string                      BadgeTypeLine       { "VFX · ADD" };
    std::string                      ShapeTitle          { "Bokeh Bead" };
    std::string                      ShapeCaption        { "additive glow" };
    std::string                      ManifoldTitle       { "Bokeh ribbon" };
    std::string                      ManifoldCaption     { "1.6 m/s stream" };
    bool                             Visible             = true;
    bool                             WindDriven          = true;
    bool                             AdditiveGlow        = true;
    bool                             FloorBounce         = false;
    bool                             FloorBounceLocked   = true;

    // Release origin & kinematics
    uint32_t                         CapacityCount       = 3400u;
    uint32_t                         AliveCount          = 3186u;
    float                            ReleaseRatePerSec   = 620.0f;
    std::array<float, 3>             OriginCentreMeters  { 0.0f, 2.25f, 0.0f };
    std::array<float, 3>             OriginHalfExtentsM  { 1.65f, 1.65f, 1.65f };
    float                            LifeMinSeconds      = 3.40f;
    float                            LifeMaxSeconds      = 5.20f;
    float                            LaunchSpeedMeters   = 1.60f;
    float                            WindCouplingRate    = 0.25f;

    // Visual shading & reusable ColourSpace source profile
    uint32_t                         VisualCarrierIndex  = 7u;   // Bokeh glow bead
    uint32_t                         BokehApertureStyle  = 0u;   // 0=Mixed, 1=Sharp, 2=Creamy, 3=Rim Ring
    float                            SizeAtBirthMeters   = 0.048f;
    float                            SizeAtDeathMeters   = 0.032f;
    float                            SizeMultiplier      = 1.00f;
    ScalarCurveProfile               SizeOverLifeCurve   {};
    ColourSpace::ColourSourceProfile ColourProfile       {};
};

struct AmbientWindProfile
{
    bool  Enabled        = true;
    float SpeedMetersSec = 2.2f;
    float BearingDegrees = 68.0f;
    float GustAmplitude  = 0.45f;
};

struct ParticleAssetPresetCard
{
    std::string                      Identifier    {};
    std::string                      Title         {};
    std::string                      CategoryTitle {};
    std::string                      MetaTags      {};
    std::string                      Description   {};
    std::string                      RampPresetId  { "thermal" };
    bool                             InScene       = false;
    bool                             Quarantined   = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ParticleWorkspaceSession
{
public:
    ParticleWorkspaceSession();

    void ResetStudioDefaultScene();
    void StepSimulationClock(double DeltaSeconds) noexcept;
    void TriggerBurst() noexcept;
    void ResetPlayback() noexcept;

    [[nodiscard]] AuthoredParticleRigUi*                                    AccessSelectedRig() noexcept;
    [[nodiscard]] const AuthoredParticleRigUi*                              QuerySelectedRig() const noexcept;
    [[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry*     AccessSelectedForce() noexcept;
    [[nodiscard]] const SimulationSpace::ForceFields::ForceSourceRecordEntry* QuerySelectedForce() const noexcept;

    [[nodiscard]] uint32_t CountVisibleEntities() const noexcept;
    [[nodiscard]] uint32_t CountHiddenEntities() const noexcept;
    [[nodiscard]] uint32_t CountTotalAliveParticles() const noexcept;
    [[nodiscard]] uint32_t CountTotalParticleCapacity() const noexcept;

    void SelectRig(std::size_t RigIndex) noexcept;
    void SelectForceSource(std::size_t ForceIndex) noexcept;
    void SelectAmbientWind() noexcept;
    void SelectForceFieldRoot() noexcept;
    void AppendLog(LogChannelCategory Channel, std::string_view Message);
    void RecordParameterEdit(std::string_view Summary) noexcept;
    void AddPresetToScene(std::size_t AssetIndex);

    // Scene tabs & top-level navigation
    std::vector<ParticleSceneTabEntry> SceneTabs        {};
    std::size_t                        ActiveSceneIndex = 0u;
    ParticleNavView                    ActiveNavView    = ParticleNavView::Workspace;

    // Live simulation clock & playback controls
    bool                               Playing          = true;
    double                             ElapsedSeconds   = 0.166;
    double                             LoopDurationSec  = 30.0;
    uint32_t                           StepCounter      = 4u;
    float                              TimeScale        = 1.0f;
    uint32_t                           BurstCount       = 0u;

    // Viewport controls & camera posture
    ViewportProjectionCategory         Projection       = ViewportProjectionCategory::PerspectiveOrbit;
    FieldOverlayChannel                OverlayChannel   = FieldOverlayChannel::CleanViewport;
    ViewportToolCategory               ActiveTool       = ViewportToolCategory::OrbitCamera;
    float                              OrbitYawRad      = 0.62f;
    float                              OrbitPitchRad    = 0.28f;
    float                              OrbitDistanceM   = 9.8f;
    bool                               ShowFieldOverlay = false;
    bool                               ShowFieldArrows  = false;
    bool                               ShowMinimap      = false;
    bool                               ShowLogDrawer    = false;
    bool                               ViewportMaximized= false;
    bool                               ShowHandles3D    = true;
    bool                               ForceFieldOverlay= false;

    // Outliner & Asset Browser filters
    bool                               CompactOutliner  = false;
    std::string                        OutlinerSearch   {};
    std::string                        OutlinerFilter   { "All" };
    std::string                        AssetSearch      {};
    std::string                        AssetCategory    { "All" };
    float                              OutlinerScrollY  = 0.0f;
    float                              InspectorScrollY = 0.0f;
    float                              AssetScrollY     = 0.0f;

    // Selection & Inspector tab
    ParticleSelectionCategory          SelectionKind    = ParticleSelectionCategory::ParticleRig;
    std::size_t                        SelectedRigIndex = 0u;
    std::size_t                        SelectedForceIdx = 0u;
    ParticleInspectorTab               InspectorTab     = ParticleInspectorTab::Overview;

    // Entities: Authored Particle Rigs + Vector Field Force Sources + Ambient Wind
    std::vector<AuthoredParticleRigUi>                                 ParticleRigs {};
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry>  ForceSources {};
    AmbientWindProfile                                                 AmbientWind  {};
    std::vector<ParticleAssetPresetCard>                               AssetPresets {};

    // Telemetry metrics & live revision counter
    float                              MeasuredFps      = 60.0f;
    float                              ComputeTimeMs    = 1.84f;
    float                              TransferLatencyMs= 0.14f;
    uint32_t                           CompletedSamples = 28u;
    uint32_t                           ParameterRevision= 0u;

    // Shared sub-component sessions & live C++ ParticleSpace runtime
    ColourSpace::ColourAssetRegister          ColourRegister   {};
    ParticleSpace::ParticleSimulationSession      SimulationSession {};
    ParticleSpace::ParticleVulkanExecutor VulkanExecutor      {};
    ColourRampEditorSession               RampEditorSession   {};
    ColourSwatchPickerSession          SwatchSession     {};
    ScalarCurveEditorSession           CurveSession      {};
    SimulationTimeRulerSession         RulerSession      {};
    LogStreamDrawerSession             LogDrawerSession  {};
    std::vector<LogStreamRecord>       LogRecords        {};
    RevisionTracker                    Revisions         {};
};

} // namespace Frontier::Workspaces
