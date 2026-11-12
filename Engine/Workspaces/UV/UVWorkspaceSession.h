/*============================================================================================================================================
                                                           UVWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Shared UV Workspace authoring session joining PolygonNetwork, SelectionMask, Interchange, GPU/Parallel Solvers, .UVSpace & Revisions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/UVCodec.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/Configuration/WorkspaceConfiguration.h"
#include "Engine/Editor/Viewports/TransformGizmo.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/GeometryInstance.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/GeometrySpace/Selection/ComponentSelectionMask.h"
#include "Engine/GeometrySpace/Selection/GeometrySelection.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Threading/Tasks/ExecutionChannels.h"
#include "Engine/Threading/Threads/Workspace/WorkspaceThread.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"
#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"
#include "Engine/Workspaces/UV/Solvers/ConcaveSkylinePacker.h"
#include "Engine/Workspaces/UV/Solvers/ConformalUnwrapSolver.h"
#include "Engine/Workspaces/UV/Solvers/ProgressiveStretchRelaxer.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"
#include "Engine/Workspaces/UV/Solvers/TexelDensityEqualizer.h"
#include "Engine/Workspaces/UV/Solvers/UVIslandLayoutOperations.h"
#include "Engine/Workspaces/UV/Solvers/UVProjectionSolvers.h"
#include "Engine/Workspaces/WorkspaceDocument.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class UVInspectorTab : uint8_t
{
    Packing = 0u, // [-] - Layer share slider, Fill Pockets, Stack/Unstack & Concave Packing Engine
    Unwrap  = 1u, // [-] - LSCM/ABF++/Smart/Box/Cyl/Sphere/FollowQuads, Seams, Pins, Stitch & Relax
    UDIM    = 2u, // [-] - Multi-UDIM tile grid, 512..8K resolution, Backdrop & texel density
    Shading = 3u, // [-] - 16-slot MatCap set & Surface Material PBR sliders
    Check   = 4u, // [-] - GPU Stretch/Overlap/Flip validator & viewport overlay switches
};

enum class UVSelectionMode : uint8_t
{
    Faces   = 0u,
    Edges   = 1u,
    Islands = 2u,
    Layers  = 3u,
};

enum class UVCommandDirective : uint8_t
{
    None                     = 0u,
    PackUVsConcaveSkyline    = 1u,
    UnwrapConformal          = 2u,
    RelaxStretchGPU          = 3u,
    MarkSharpSeams           = 4u,
    EqualizeTexelDensity     = 5u,
    AutoDistributeUDIMs      = 6u,
    ValidateDistortionGPU    = 7u,
    FlattenAngleBasedABF     = 8u,
    ProjectSmartUV           = 9u,
    ProjectCubicBox          = 10u,
    ProjectCylindrical       = 11u,
    ProjectSpherical         = 12u,
    ProjectCameraView        = 13u,
    FollowActiveQuads        = 14u,
    MarkSelectedSeams        = 15u,
    ClearSelectedSeams       = 16u,
    DeriveSeamsFromIslands   = 17u,
    PinSelectedUVs           = 18u,
    UnpinSelectedUVs         = 19u,
    StitchSelectedSeams      = 20u,
    SplitSelectedIsland      = 21u,
    RectifySelectedQuads     = 22u,
    StackSimilarIslands      = 23u,
    UnstackOverlapping       = 24u,
    ExpandEmptyPockets       = 25u,
    SelectFlippedFaces       = 26u,
    SelectOverlappingFaces   = 27u,
    FitActiveSelection     = 28u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct UDIMTileSetting
{
    uint32_t                          ResolutionPx   = 2048u; // [px] - Per-UDIM target texture resolution (512..8192)
    GeometricRaster::UVBackdropPreset BackdropPreset = GeometricRaster::UVBackdropPreset::None; // [-] - Custom preview backdrop
};

class UVWorkspaceSession
{
public:
    UVWorkspaceSession();

    [[nodiscard]] bool AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer);
    [[nodiscard]] GeometricRaster::UVWorkspaceRasterizer* AccessRasterizer() noexcept { return Rasterizer; }

    [[nodiscard]] bool InitializeGPUSolvers(
        GeometricRaster::VulkanInterchange* BoundDevice,
        const std::filesystem::path&          CompiledShaderDir);
    void TeardownGPUSolvers() noexcept;

    void LoadSamplePreset(GeometrySpace::SampleAssetPreset Preset);

    [[nodiscard]] bool ImportExternalGeometry(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic);

    [[nodiscard]] bool ExportExternalGeometry(
        const std::filesystem::path& TargetPath,
        std::string&                 OutDiagnostic) const;

    void     SelectPriorityLayer(uint16_t LayerOrdinal);
    void     BindSelectionToPriorityLayer(uint16_t LayerOrdinal);
    uint16_t CreatePriorityLayer(std::string_view Title, Mathematics::Scalar3D LinearTint, float InitialSharePercent);
    void     RenameSelectedLayer(std::string_view NewTitle);
    uint16_t DuplicateSelectedLayer();
    bool     DeleteSelectedLayer();
    void     AssignSelectedLayerTint(Mathematics::Scalar3D LinearTint) noexcept;
    void     AdjustSelectedLayerShare(float RequestedSharePercent) noexcept;
    void     ApplyMatCapToActiveSelection(uint8_t MatCapSlot);
    void     ApplySurfaceRoughnessMetallic(float Roughness, float Metallic, float MatCapBlend);

    uint32_t ConstructNextUDIMTile();
    [[nodiscard]] UDIMTileSetting& AccessUDIMTileSetting(uint32_t UDIMNumber);
    [[nodiscard]] UDIMTileSetting  QueryUDIMTileSetting(uint32_t UDIMNumber) const noexcept;

    void SynchronizeSelectionToGPU();
    void RefreshGizmoAnchorsFromSelection() noexcept;
    void FinalizeInteractiveUVRevision(std::string_view OperationTag);
    void FitSelectionOrNetwork() noexcept;

    void                             QueueDirective(UVCommandDirective Directive) noexcept;
    [[nodiscard]] UVCommandDirective ConsumePendingDirective() noexcept;

    bool ExecuteDirective(
        UVCommandDirective            Directive,
        Threading::ExecutionChannels* OptionalChannels = nullptr);

    [[nodiscard]] bool DispatchDirectiveAsync(
        Threading::WorkspaceThread&   BackgroundSolverThread,
        Threading::ExecutionChannels* OptionalChannels,
        UVCommandDirective            Directive,
        Threading::CompletionCounter* OptionalCompletion = nullptr);

    bool UndoParameterization();
    bool RedoParameterization();

    [[nodiscard]] bool SaveUVSpaceDocument(
        const std::filesystem::path& TargetPath,
        std::string&                 OutDiagnostic) const;

    [[nodiscard]] bool LoadUVSpaceDocument(
        const std::filesystem::path& SourcePath,
        std::string&                 OutDiagnostic);

    void RefreshViewportFramebuffers(uint32_t Viewport3DW, uint32_t Viewport3DH, uint32_t Canvas2DW, uint32_t Canvas2DH);

    [[nodiscard]] WorkspaceDocument&                AccessDocument() noexcept { return ActiveDocument; }
    [[nodiscard]] const WorkspaceDocument&          InspectDocument() const noexcept { return ActiveDocument; }
    [[nodiscard]] Editor::ViewportProjection&       AccessViewportProjection() noexcept { return ActiveDocument.AccessViewportProjection(); }
    [[nodiscard]] const Editor::ViewportProjection& InspectViewportProjection() const noexcept { return ActiveDocument.InspectViewportProjection(); }

    GeometrySpace::SampleAssetPreset         ActivePreset         = GeometrySpace::SampleAssetPreset::ControlledIntegrator;
    GeometrySpace::ConstructedSampleAsset    Asset                {};
    GeometrySpace::GeometryInstanceRegistry  Instances            {};
    GeometrySpace::GeometryInstanceToken     RootInstanceToken    {};
    GeometrySpace::ComponentSelectionMask    SelectionMask        {};
    Configuration::WorkspaceConfiguration    WorkspaceConfig      {};
    WorkspaceDocument                        ActiveDocument;
    RevisionTracker&                         RevisionHistory;
    std::vector<float>                       BaselineUVPairs      {};
    uint64_t                                 RevisionStampCounter = 1ull;

    ProgressiveStretchRelaxer                StretchRelaxer       {};
    ConcaveSkylinePacker                     SkylinePacker        {};
    ConcavePackReport                        LatestPackReport     {};

    UVInspectorTab                           ActiveInspectorTab   = UVInspectorTab::Packing;
    UVSelectionMode                          SelectionMode        = UVSelectionMode::Layers;
    UVProjectionMethod                       ActiveUnwrapMethod   = UVProjectionMethod::ConformalLSCM;
    uint16_t                                 SelectedLayerOrdinal = 1u;
    uint32_t                                 ActiveUDIMNumber     = 1001u;
    uint32_t                                 OutlinerUDIMFilter   = 0u; // 0 = All UDIMs
    std::string                              OutlinerSearchQuery  {};
    bool                                     FillPocketsEnabled   = true;
    bool                                     SeamBrushActive      = false;
    float                                    SharpAngleThreshold  = 52.0f;
    uint32_t                                 RelaxIterations      = 12u;
    std::unordered_map<uint32_t, UDIMTileSetting> UDIMSettings    {};

    Editor::TransformGizmo                   Gizmo3D              {};
    Editor::TransformGizmo                   Gizmo2D              {};
    GeometricRaster::Viewport3DRenderOptions RenderOptions3D      {};
    GeometricRaster::Canvas2DRenderOptions   RenderOptions2D      {};
    GeometricRaster::GPUUVDistortionSummary  LatestMetrics        {};
    GeometricRaster::ViewportPickHit         LastHoverHit         {};
    std::string                              StatusBannerText     { "Ready  |  Zero Flips, Zero Overlaps" };

    std::vector<uint8_t>                     Framebuffer3DRGBA    {};
    uint32_t                                 Framebuffer3DWidth   = 0u;
    uint32_t                                 Framebuffer3DHeight  = 0u;

    std::vector<uint8_t>                     Framebuffer2DRGBA    {};
    uint32_t                                 Framebuffer2DWidth   = 0u;
    uint32_t                                 Framebuffer2DHeight  = 0u;
    bool                                     FramebuffersDirty    = true;

private:
    void InitializeLoadedAssetLayout();
    void RegisterUVRevisionStep(std::string_view OperationTag);
    bool RestoreUVFromRevisionCursor();
    void SyncNetworkToGPU();

    GeometricRaster::UVWorkspaceRasterizer*  Rasterizer           = nullptr;
    UVCommandDirective                       PendingDirective     = UVCommandDirective::None;
};

} // namespace Frontier::Workspaces
