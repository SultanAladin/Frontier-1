/*============================================================================================================================================
                                                        COMPONENTWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Multi-tab document state, compiled geometry catalog, validation, & real-time Jolt/VehicleDynamics simulation for Component Editor

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/ComponentClass/ComponentClassValidator.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/ExecutionSequencePane.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentViewportRenderer.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/SimulationSpace/ComponentSimulationPreviewDriver.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class LeftDockTab : uint8_t
{
    Components = 0u,
    Content    = 1u,
};

enum class CenterViewTab : uint8_t
{
    Viewport         = 0u,
    ExecutionSequence = 1u,
};

enum class BottomDrawerTab : uint8_t
{
    CompiledPayload = 0u,
    ResponseCurves  = 1u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComponentClassDocument
{
    ComponentClass::ComponentClassDefinition            ClassDefinition {};
    ComponentClass::ComponentInstanceToken              SelectedToken   {};
    std::vector<ComponentClass::ClassDiagnosticIssue>   Diagnostics     {};
    bool                                                CompilePermitted      = true;
    bool                                                Dirty           = false;
    bool                                                IconBaked       = false;
    ExecutionSequenceViewport                            SequenceView    {};
    GeometricRaster::ComponentEditor::ComponentViewportCamera Camera    {};
};

struct BakedAssetThumbnail
{
    uint32_t             Width  = 0u;
    uint32_t             Height = 0u;
    std::vector<uint8_t> Rgba8  {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                               COMPONENT WORKSPACE SESSION
//------------------------------------------------------------------------------------------------------------------------

class ComponentWorkspaceSession
{
public:
    ComponentWorkspaceSession();
    ~ComponentWorkspaceSession();

    void InitializeDefaultWorkspace(const std::filesystem::path& RepositoryRoot);

    void AttachRenderer(GeometricRaster::ComponentEditor::ComponentViewportRenderer* RendererPtr) noexcept;

    [[nodiscard]] ComponentClassDocument*       AccessActiveDocument() noexcept;
    [[nodiscard]] const ComponentClassDocument* InspectActiveDocument() const noexcept;

    void SelectTab(std::size_t Index) noexcept;
    void CloseTab(std::size_t Index);
    void CreateNewClassDocument(std::string_view Title = "NewComponentClass", std::string_view InitialAssetId = "");

    void MarkActiveDirty() noexcept;
    void CompileActiveDocument();
    void BakePendingThumbnails();

    [[nodiscard]] bool ImportGeometryFile(const std::filesystem::path& FilePath, std::string& OutDiagnostic);

    void AddComponentToActive(
        ComponentClass::ComponentCategory     Category,
        std::string_view                      Title,
        ComponentClass::PhysicsConstraintCategory ConstraintCategory = ComponentClass::PhysicsConstraintCategory::Fixed,
        std::string_view                      AssetId        = "");

    void DuplicateSelectedComponent();
    void RemoveSelectedComponent();
    void FocusSelectedOrClass();

    void ToggleSimulation() noexcept;
    void TriggerConstraintKick(ComponentClass::ComponentInstanceToken Token) noexcept;
    void ResetMotionKinematics() noexcept;
    void Tick(double DeltaSeconds) noexcept;

#if FRONTIER_FLUID_HAS_VULKAN
    [[nodiscard]] bool AttachVulkanInterchange(
        GeometricRaster::VulkanInterchange* Interchange,
        const std::filesystem::path&        CompiledShaderDirectory = "CompiledShaders") noexcept;
#endif

    [[nodiscard]] uint64_t ComputeRenderSignature() const noexcept;

    // Workspace state
    std::filesystem::path                                                RepoRoot {};
    std::vector<ComponentClassDocument>                                  Documents {};
    std::size_t                                                          ActiveDocIndex = 0u;
    std::vector<ComponentClass::CompiledGeometryRepositoryEntry>            Repository {};
    std::unordered_map<std::string, BakedAssetThumbnail>                 AssetIcons {};

    LeftDockTab                                                          ActiveLeftTab     = LeftDockTab::Components;
    CenterViewTab                                                        ActiveCenterTab   = CenterViewTab::Viewport;
    BottomDrawerTab                                                      ActiveDrawerTab   = BottomDrawerTab::CompiledPayload;
    bool                                                                 DrawerExpanded    = false;
    bool                                                                 AddSheetOpen      = false;
    bool                                                                 VulkanReady       = false;
    std::string                                                          StatusNote        { "Ready" };

    // Viewport options
    GeometricRaster::ComponentEditor::ComponentRenderOptions             RenderOptions     {};
    float                                                                LastRenderMs      = 0.0f;
    float                                                                LastFps           = 60.0f;
    uint32_t                                                             LastDrawnTris     = 0u;
    uint32_t                                                             LastDrawnGeoms    = 0u;

    // Live simulation & powertrain telemetry
    bool                                                                 SimulationActive  = false;
    float                                                                ThrottleInput     = 0.0f;
    float                                                                BrakeInput        = 0.0f;
    float                                                                SteerInput        = 0.0f;
    SimulationSpace::PhysicsSpace::VehicleSimulationTelemetry            VehicleTelemetry  {};
    SimulationSpace::ComponentSimulationPreviewDriver                    SimulationPreview {};
    std::unordered_map<uint32_t, float>                                  SpringVelocities  {};
    std::unordered_map<uint32_t, float>                                  KickImpulses      {};

    // Search filters
    TextEditBuffer                                                       ComponentSearch   {};
    TextEditBuffer                                                       DetailsSearch     {};

private:
    void SyncEditPoseFromClass(ComponentClassDocument& Doc) noexcept;
    void StepLiveSimulation(double DeltaSeconds) noexcept;

    GeometricRaster::ComponentEditor::ComponentViewportRenderer* BoundRenderer = nullptr;
    uint64_t                                                     RenderMutator = 1ull;
};

} // namespace Frontier::Workspaces::ComponentEditor
