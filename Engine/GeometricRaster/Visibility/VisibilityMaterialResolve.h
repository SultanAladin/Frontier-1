/*============================================================================================================================================
                                                         VISIBILITYMATERIALRESOLVE.H
============================================================================================================================================*/
// 🧩 Zero-G-Buffer Analytic Ray-Differential Barycentric Resolver, UV Footprint Jacobians, Motion Vectors, & 10-Lobe OpenPBR Shade

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"
#include "Engine/Mathematics/LinearColors.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct AnalyticResolvedSurfaceSample
{
    bool                                              ValidHit            = false; // [-]     - True when pixel hit a surface
    uint16_t                                          MaterialSlot        = 0u;    // [idx]   - Resolved CompactMaterialRegister slot
    uint32_t                                          InstanceOrdinal     = 0u;    // [idx]   - Originating RigidClusterInstance
    uint32_t                                          ClusterOrdinal      = 0u;    // [idx]   - Originating ClusterHeaderEntry
    uint32_t                                          TriangleOrdinal     = 0u;    // [idx]   - Local triangle index [0..127]
    Mathematics::Axis3D                               BarycentricWeights  {};      // [-]     - Exact perspective-correct (L0, L1, L2)
    Mathematics::Axis3D                               WorldPosition       {};      // [m]     - Reconstructed world hit position
    Mathematics::Axis3D                               WorldViewDirection  {};      // [-]     - Unit eye vector toward CameraOrigin
    MaterialSpace::Scattering::SurfaceShadingFrame    TangentFrame        {};      // [-]     - Interpolated Oct32 Mikk/Duff frame
    MaterialSpace::Scattering::TextureFootprintJacobian Footprint         {};      // [uv/px] - Exact analytic UV + dUV/dx + dUV/dy!
    Mathematics::Axis2D                               MotionVectorPx      {};      // [px]    - CurrentPx - PreviousPx sub-pixel velocity
};

struct VisibilityResolveFrameBuffer
{
    uint32_t                          WidthPx                  = 0u;  // [px]    - Viewport width
    uint32_t                          HeightPx                 = 0u;  // [px]    - Viewport height
    std::vector<Mathematics::LinearRGB> LinearHdrRadiance      {};    // [cd/m2] - Linear HDR shaded scene radiance
    std::vector<uint8_t>              ToneMappedSrgb8          {};    // [rgba8] - ACES filmic + sRGB8 display image
    std::vector<Mathematics::Axis2D>  MotionVectorsPx          {};    // [px]    - Per-pixel differential velocity buffer
    std::vector<float>                FootprintAreaDetUv       {};    // [uv^2]  - |dU/dx * dV/dy - dU/dy * dV/dx| per pixel
    uint32_t                          ActiveWaveTileCount      = 0u;  // [count] - 8x8 tiles shaded via wave-uniform lobe masks
    uint32_t                          SingleLobeFastTiles      = 0u;  // [count] - Tiles skipping unused lobes via WaveActiveBitOr
    uint32_t                          GlassCompositePixelCount = 0u;  // [count] - Pixels composited through 2-layer glass peel
};

//------------------------------------------------------------------------------------------------------------------------
//                                              VISIBILITY MATERIAL RESOLVE
//------------------------------------------------------------------------------------------------------------------------

class VisibilityMaterialResolve
{
public:
    // Reconstructs exact world hit, perspective barycentrics, analytic ray-differential UV Jacobians, and motion vector at (PX, PY)
    [[nodiscard]] static AnalyticResolvedSurfaceSample ReconstructAnalyticPixelHit(
        uint32_t                              PixelX,
        uint32_t                              PixelY,
        uint64_t                              PackedVisibility64,
        const OrthonormalCameraBasis&         CurrentBasis,
        const OrthonormalCameraBasis&         PreviousBasis,
        std::span<const RigidClusterInstance> Instances) noexcept;

    // Resolves the full 64-bit VisibilityBuffer + 2-layer Glass Peel in 8x8 wave tiles with WaveActiveBitOr lobe-mask culling
    [[nodiscard]] static VisibilityResolveFrameBuffer ResolveVisibilityFrame(
        const PerspectiveCameraFrame&               CurrentCamera,
        const PerspectiveCameraFrame&               PreviousCamera,
        std::span<const RigidClusterInstance>       Instances,
        const VisibilityBufferTargets&              VisibilityTargets,
        const MaterialSpace::CompactMaterialRegister& MaterialRegister);
};

} // namespace Frontier::GeometricRaster
