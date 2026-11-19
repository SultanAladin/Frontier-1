/*============================================================================================================================================
                                                      COMPONENTVIEWPORTRENDERER.H
============================================================================================================================================*/
// 🧩 Vulkan 1.3 OpenPbrDirect (Zero GI) + 7-channel .slang inspection & Layer-1 X-ray constraint renderer for Component Editor

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/GeometricRaster/ComponentEditor/ComponentGizmoGeometry.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometricRaster::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ComponentInspectionChannel : uint8_t
{
    Lit         = 0u, // [-] - OpenPbrDirect (Zero GI) + studio lights + emissives
    Unlit       = 1u, // [-] - Flat slot albedo + emissives
    Density     = 2u, // [-] - Triangle density heat ramp (blue sparse -> red dense)
    Normals     = 3u, // [-] - World-space surface normals
    UvChecker   = 4u, // [-] - Procedural 8x8 UV checkerboard (or #7a2d5a missing UV)
    LodClusters = 5u, // [-] - Golden-angle cluster LOD colouring
    Collision   = 6u, // [-] - Translucent ghost surface + green/cyan/amber Fresnel collision shells
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComponentViewportCamera
{
    Mathematics::Axis3D FocusPointMeters { 0.0f, 0.55f, 0.0f }; // [m]   - Orbit target (Y-up meters)
    float               YawRadians       = 0.72f;                // [rad] - Azimuth around +Y
    float               PitchRadians     = 0.28f;                // [rad] - Elevation above XZ ground
    float               DistanceMeters   = 7.20f;                // [m]   - Distance from FocusPoint
    float               VerticalFovDeg   = 39.6f;                // [deg] - 35 mm cinema lens vertical FOV
    bool                PilotActive      = false;                // [-]   - True when piloting a CameraInstance
    ComponentClass::ComponentInstanceToken PilotCameraToken {};  // [id]  - Active piloted CameraInstance
};

struct ComponentRenderOptions
{
    ComponentInspectionChannel Channel             = ComponentInspectionChannel::Lit;
    bool                       ShowDotFloor        = true;
    bool                       ShowDebugHelpers    = true;
    bool                       TurntableEnabled    = false;
    bool                       TransparentBackdrop = false; // [-] - Alpha=0 studio cutout (for thumbnail baking)
    float                      ElapsedSeconds      = 0.0f;
    ComponentClass::ComponentInstanceToken SelectedToken {};
    std::unordered_map<uint32_t, float>              LiveConstraintCoordinates {}; // SlotIndex -> rad or m
    std::unordered_map<uint32_t, bool>               DockIntactMask           {}; // SlotIndex -> latched
    std::unordered_map<uint32_t, std::array<float, 16>> SimulationWorldOverrides {}; // Optional 4x4 row-major body matrices
};

struct ComponentViewportOutput
{
    uint32_t             WidthPx          = 0u;
    uint32_t             HeightPx         = 0u;
    float                RenderMillisec   = 0.0f;
    uint32_t             DrawnTriangles   = 0u;
    uint32_t             DrawnGeometries  = 0u;
    bool                 VulkanGpuActive  = false;
    VkImageView          ColorImageView   = VK_NULL_HANDLE;
    std::vector<uint8_t> DisplayRgba8     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                               COMPONENT VIEWPORT RENDERER
//------------------------------------------------------------------------------------------------------------------------

class ComponentViewportRenderer
{
public:
    ComponentViewportRenderer();
    ~ComponentViewportRenderer();

    ComponentViewportRenderer(const ComponentViewportRenderer&)            = delete;
    ComponentViewportRenderer& operator=(const ComponentViewportRenderer&) = delete;

    [[nodiscard]] bool InitializeVulkan(
        VulkanInterchange&           TargetDevice,
        const std::filesystem::path& CompiledShaderDirectory);

    void TeardownVulkan() noexcept;

    [[nodiscard]] bool QueryVulkanReady() const noexcept;

    void ClearGeometryCache() noexcept;

    [[nodiscard]] ComponentViewportCamera ComputeFramedCameraForClass(
        const ComponentClass::ComponentClassDefinition&               ClassDef,
        std::span<const ComponentClass::CompiledGeometryRepositoryEntry> Repository,
        float                                                         YawRadians   = 0.72f,
        float                                                         PitchRadians = 0.28f) const;

    [[nodiscard]] ComponentViewportOutput RenderClassViewport(
        const ComponentClass::ComponentClassDefinition&               ClassDef,
        std::span<const ComponentClass::CompiledGeometryRepositoryEntry> Repository,
        const ComponentViewportCamera&                                Camera,
        const ComponentRenderOptions&                                 Options,
        uint32_t                                                      WidthPx,
        uint32_t                                                      HeightPx,
        bool                                                          PerformHostReadback = true);

    [[nodiscard]] ComponentClass::ComponentInstanceToken QueryPickAtPixel(
        const ComponentClass::ComponentClassDefinition& ClassDef,
        uint32_t                                        PixelX,
        uint32_t                                        PixelY) const noexcept;

    [[nodiscard]] static bool WritePngFile(
        const std::filesystem::path& FilePath,
        uint32_t                     WidthPx,
        uint32_t                     HeightPx,
        std::span<const uint8_t>     Rgba8Pixels);

private:
    struct RendererImplementation;
    std::unique_ptr<RendererImplementation> Impl;
};

} // namespace Frontier::GeometricRaster::ComponentEditor
