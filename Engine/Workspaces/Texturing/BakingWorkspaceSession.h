/*============================================================================================================================================
                                                           BAKINGWORKSPACESESSION.H
============================================================================================================================================*/
// 🧩 Authoring session for Frontier Baking Studio: 17 PBR surface map targets, High/Low studies & Vulkan GPU baker bridge.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Frontier::GeometricRaster
{
class VulkanInterchange;
}

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class BakerTargetId : uint8_t
{
    TangentNormal      = 0,
    WorldNormal        = 1,
    ModelSpaceNormal   = 2,
    BentNormal         = 3,
    BevelNormal        = 4,
    AmbientOcclusion   = 5,
    SecondaryAo        = 6,
    Curvature          = 7,
    Convexity          = 8,
    Concavity          = 9,
    Thickness          = 10,
    HeightDisplacement = 11,
    TopDownDust        = 12,
    UvIslandId         = 13,
    PartMaterialId     = 14,
    WorldPosition      = 15,
    UvWireframe        = 16,
    Count              = 17,
};

enum class BakerTargetGroup : uint8_t
{
    Normals         = 0,
    Surface         = 1,
    DiagnosticMasks = 2,
    Count           = 3,
};

enum class BakerStudyShape : uint8_t
{
    UtahTeapot = 0,
    ShaderBall = 1,
    ChamferBox = 2,
    UvSphere   = 3,
    TorusKnot  = 4,
    Count      = 5,
};

enum class BakerViewportLayout : uint8_t
{
    ThreeDOnly = 0,
    TwoDOnly   = 1,
    SplitView  = 2,
    Count      = 3,
};

enum class BakerComputeDevice : uint8_t
{
    AutoGpu             = 0,
    GtxComputeBvh4      = 1,
    RtxHardwareRayQuery = 2,
    Count               = 3,
};

enum class TangentNormalConvention : uint8_t
{
    OpenGlPlusY   = 0,
    DirectXMinusY = 1,
    Count         = 2,
};

enum class BakerQuickPreset : uint8_t
{
    PbrStandard   = 0,
    OcclusionPlus = 1,
    Normals       = 2,
    SelectAll     = 3,
    Count         = 4,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BakerTargetDescriptor
{
    BakerTargetId    Id           = BakerTargetId::TangentNormal;
    BakerTargetGroup Group        = BakerTargetGroup::Normals;
    const char*      Label        = "Tangent Normal";
    const char*      ShortSummary = "High-to-Low normal";
};

struct BakerStudyDescriptor
{
    BakerStudyShape Study    = BakerStudyShape::UtahTeapot;
    const char*     Label    = "Utah Teapot (High/Low)";
    uint32_t        LowTris  = 16256u;
    uint32_t        HighTris = 16256u;
};

struct BakedPaintChannelTransfer
{
    bool                 Ready              = false;
    uint32_t             ResolutionPx       = 0u;
    std::vector<uint8_t> TangentNormalRgba8 {};
    std::vector<uint8_t> AmbientOccRgba8    {};
    std::vector<uint8_t> CurvatureRgba8     {};
    std::vector<uint8_t> HeightRgba8        {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  SESSION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct BakingGpuEngineStorage;

struct BakingWorkspaceSession
{
    static constexpr std::size_t TargetCount = static_cast<std::size_t>(BakerTargetId::Count);

    BakerStudyShape         Study                   = BakerStudyShape::UtahTeapot;
    BakerViewportLayout     ViewportLayout          = BakerViewportLayout::ThreeDOnly;
    BakerComputeDevice      ComputeDevice           = BakerComputeDevice::AutoGpu;
    uint32_t                TargetResolutionPx      = 512u;
    TangentNormalConvention NormalConvention        = TangentNormalConvention::OpenGlPlusY;
    int32_t                 AoSampleCount           = 12;
    float                   FrontCageOffsetMeters   = 0.14f;
    float                   RearCageOffsetMeters    = 0.14f;
    float                   AoMaxRayDistanceMeters  = 0.45f;
    int32_t                 UvEdgePaddingPx         = 6;
    bool                    ShowHighWireOverlay     = false;
    bool                    ShowCageOverlay         = false;

    BakerTargetId           InspectedTarget         = BakerTargetId::TangentNormal;
    std::array<bool, TargetCount> SelectedTargets   {};
    std::array<bool, TargetCount> BakedReadyTargets {};

    bool                    BakingActive            = false;
    bool                    SurfaceBaked            = false;
    bool                    BridgedToPaint          = false;
    float                   BakeProgress            = 0.0f;
    std::string             ProgressStatus          = "Ready to bake";
    std::string             DeviceIndicatorText     = "Vulkan GPU Ready";
    std::string             CustomLowAssetLabel     {};
    std::string             CustomHighAssetLabel    {};
    std::string             LastExportedPath        {};
    BakedPaintChannelTransfer PaintTransferSnapshot {};

    // Interactive 3D Orbit Camera angles synced with BakingViewportPane
    float                   CameraYawRadians        = 0.72f;
    float                   CameraPitchRadians      = 0.36f;
    float                   CameraDistanceMeters    = 2.25f;
    float                   CameraPivotX            = 0.0f;
    float                   CameraPivotY            = 0.0f;
    float                   CameraPivotZ            = 0.45f;

    std::unique_ptr<BakingGpuEngineStorage> GpuStorage {};

    BakingWorkspaceSession() noexcept;
    ~BakingWorkspaceSession() noexcept;

    BakingWorkspaceSession(BakingWorkspaceSession&&) noexcept            = default;
    BakingWorkspaceSession& operator=(BakingWorkspaceSession&&) noexcept = default;
    BakingWorkspaceSession(const BakingWorkspaceSession&)                = delete;
    BakingWorkspaceSession& operator=(const BakingWorkspaceSession&)     = delete;

    void AttachExternalVulkanDevice(
        GeometricRaster::VulkanInterchange* ExternalDevice,
        const std::filesystem::path&        ShaderDirectory = {});

    [[nodiscard]] bool EnsureGpuEngineReady();

    void ResetDefaults() noexcept;
    void ApplyQuickPreset(BakerQuickPreset Preset) noexcept;
    void ToggleTargetSelected(BakerTargetId Target) noexcept;
    void InspectTarget(BakerTargetId Target) noexcept;
    void SelectStudy(BakerStudyShape NextStudy) noexcept;
    void AssignResolution(uint32_t NextResPx) noexcept;
    void ExecuteBakeSimulation() noexcept;
    void SendBakedTargetsToPaint() noexcept;

    void ResetOrbitCamera() noexcept;
    void OrbitCameraByDelta(float DeltaYawRad, float DeltaPitchRad) noexcept;
    void PanCameraByDelta(float RightMeters, float UpMeters) noexcept;
    void ZoomCameraByFactor(float ScaleMultiplier) noexcept;

    [[nodiscard]] bool RenderSpatialViewportRgba8(
        uint32_t             WidthPx,
        uint32_t             HeightPx,
        std::vector<uint8_t>& OutRgbaPixels);

    [[nodiscard]] bool QueryInspectedPlanarRgba8(
        const uint8_t*& OutPixels,
        uint32_t&       OutWidthPx,
        uint32_t&       OutHeightPx) const noexcept;

    [[nodiscard]] bool LoadCustomStudyAsset(
        const std::filesystem::path& AssetFilePath,
        bool                         ImportAsHighPoly,
        std::string*                 OutDiagnostic = nullptr);

    [[nodiscard]] bool ExportInspectedTargetPng(const std::filesystem::path& ExplicitOutputPath = {});

    [[nodiscard]] uint32_t QueryActiveLowTriangles() const noexcept;
    [[nodiscard]] uint32_t QueryActiveHighTriangles() const noexcept;
    [[nodiscard]] std::size_t CountSelectedTargets() const noexcept;
    [[nodiscard]] bool        QueryPresetMatched(BakerQuickPreset Preset) const noexcept;
    [[nodiscard]] bool        QueryTargetReady(BakerTargetId Target) const noexcept;
    [[nodiscard]] const BakerStudyDescriptor& QueryActiveStudyDescriptor() const noexcept;

    [[nodiscard]] static const std::array<BakerTargetDescriptor, TargetCount>& QueryTargetRegistry() noexcept;
    [[nodiscard]] static const BakerTargetDescriptor& QueryTargetDescriptor(BakerTargetId Target) noexcept;
    [[nodiscard]] static const std::array<BakerStudyDescriptor, 5u>& QueryStudyRegistry() noexcept;
    [[nodiscard]] static const char* QueryGroupHeading(BakerTargetGroup Group) noexcept;
};

} // namespace Frontier::Workspaces
