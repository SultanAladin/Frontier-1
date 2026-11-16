/*============================================================================================================================================
                                                        FRACTUREVIEWPORTRENDERER.H
============================================================================================================================================*/
// 🧩 OpenPBR Studio + Energy/Cells/Normals fracture viewport renderer (Vulkan shader bridge + deterministic CPU rasterizer in RHR +Z Up)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Frontier::Workspaces::Fracture
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class FractureRenderViewMode : uint8_t
{
    StudioOpenPBR = 0u, // [-] - 10-lobe OpenPBR + 3 Studio Softboxes + IBL (No GI)
    Energy        = 1u, // [-] - Logarithmic stress/energy heatmap (Blue -> Red)
    Cells         = 2u, // [-] - Golden-angle partition cell colouration
    Normals       = 3u, // [-] - RHR +Z Up world-space surface normals
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FractureViewportCamera
{
    float               YawRadians      = 0.58f;                  // [rad] - Horizontal turntable orbit angle
    float               PitchRadians    = 0.34f;                  // [rad] - Elevation pitch above XY ground plane
    float               DistanceM       = 3.45f;                  // [m]   - Orbit distance from TargetWorld
    Mathematics::Axis3D TargetWorld     { 0.0f, 0.0f, 0.95f };    // [m]   - Orbit focus target (RHR +Z Up)
    float               VerticalFovDeg  = 42.0f;                  // [deg] - Vertical field of view
};

struct FractureRenderOptions
{
    FractureRenderViewMode RenderMode            = FractureRenderViewMode::StudioOpenPBR;
    float                  ExplodeFactor         = 0.0f;                 // [0-1] - Static outward centroid separation
    bool                   ShowBonds             = true;                 // [-]   - Draws intact/ruptured bond strain lines
    bool                   ShowWireframe         = false;                // [-]   - Overlay triangle/face edges
    bool                   ShowWavefronts        = true;                 // [-]   - Draws LEFM/dendritic wavefront polylines
    bool                   ShowImpactMarker      = true;                 // [-]   - Draws impact point & normal arrow
    bool                   ShowGroundGrid        = true;                 // [-]   - Draws Z=0 reference studio floor grid
    bool                   UseNonPlanarFractography = true;              // [-]   - Uses EngineeringSurface relief triangles when present
    int32_t                SelectedFragmentIndex = -1;                   // [idx] - Highlighted fragment (-1 = none)
    Mathematics::Axis3D    SolidWorldOffsetM     { 0.0f, 0.0f, 0.95f };  // [m]   - Placement offset for unsimulated display
};

struct FractureRenderOutput
{
    uint32_t             WidthPx          = 0u;
    uint32_t             HeightPx         = 0u;
    float                RenderMillisec   = 0.0f;
    uint32_t             DrawnTriangles   = 0u;
    bool                 VulkanSpirvReady = false;
    std::vector<uint8_t> DisplayRgba8     {};
};

struct FractureRayHitResult
{
    bool                Hit           = false;
    int32_t             FragmentIndex = -1;
    Mathematics::Axis3D HitPointWorld { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D HitNormalWorld { 0.0f, -1.0f, 0.0f };
    float               DistanceM     = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureViewportRenderer
{
public:
    FractureViewportRenderer() = default;
    ~FractureViewportRenderer();

    FractureViewportRenderer(const FractureViewportRenderer&)            = delete;
    FractureViewportRenderer& operator=(const FractureViewportRenderer&) = delete;

    [[nodiscard]] bool InitializeVulkanShaders(
        GeometricRaster::VulkanInterchange& TargetDevice,
        const std::filesystem::path&        CompiledShaderDirectory);

    void TeardownVulkanShaders() noexcept;

    [[nodiscard]] bool QueryVulkanShadersReady() const noexcept { return SpirvVerified; }

    [[nodiscard]] FractureRenderOutput RenderFrame(
        const GeometrySpace::Fracture::FracturePartitionResult*      Partition,
        const GeometrySpace::Fracture::ConvexPolyhedron*             IntactFallbackSolid,
        const GeometrySpace::Fracture::FractureMaterialSpec&         Material,
        const SimulationSpace::PhysicsSpace::FractureSimulationRunner* SimRunner,
        const FractureViewportCamera&                                Camera,
        const FractureRenderOptions&                                 Options,
        Mathematics::Axis3D                                          ImpactPointLocal,
        Mathematics::Axis3D                                          ImpactNormalLocal,
        uint32_t                                                     WidthPx,
        uint32_t                                                     HeightPx) const;

    [[nodiscard]] static FractureRayHitResult PickViewportSurface(
        const GeometrySpace::Fracture::FracturePartitionResult*      Partition,
        const GeometrySpace::Fracture::ConvexPolyhedron*             IntactFallbackSolid,
        const SimulationSpace::PhysicsSpace::FractureSimulationRunner* SimRunner,
        const FractureViewportCamera&                                Camera,
        const FractureRenderOptions&                                 Options,
        float                                                        NormalizedX,
        float                                                        NormalizedY,
        uint32_t                                                     ViewportWidthPx,
        uint32_t                                                     ViewportHeightPx) noexcept;

    static void BuildOpenPbrPairForFractureMaterial(
        const GeometrySpace::Fracture::FractureMaterialSpec& Spec,
        MaterialSpace::SurfaceMaterial&                      OutExterior,
        MaterialSpace::SurfaceMaterial&                      OutInterior) noexcept;

    [[nodiscard]] static Mathematics::Axis3D ComputeCameraPosition(const FractureViewportCamera& Camera) noexcept;

private:
    bool                 SpirvVerified     = false;
    std::vector<uint8_t> VertexSpirvBytes  {};
    std::vector<uint8_t> FragmentSpirvBytes {};
};

} // namespace Frontier::Workspaces::Fracture
