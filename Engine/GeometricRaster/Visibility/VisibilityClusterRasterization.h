/*============================================================================================================================================
                                                       VISIBILITYCLUSTERRASTERIZATION.H
============================================================================================================================================*/
// 🧩 Exact 64-Bit (8 B/px) Visibility Buffer (ReversedDepth32 | VisibilityToken32) + HW/SW Micro-Raster + 2-Layer Glass Depth Peel

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/ClusterCullEncoder.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   64-BIT VISIBILITY TOKEN
//------------------------------------------------------------------------------------------------------------------------

// 📝 VisibilityToken32 layout (non-zero for valid hits; 0 = Sky / Background):
//    Bits [31..21] : InstanceOrdinal + 1   (11 bits -> up to 2,047 instances; 0 = Sky)
//    Bits [20.. 7] : ClusterOrdinal        (14 bits -> up to 16,384 clusters/instance)
//    Bits [ 6.. 0] : LocalTriangleOrdinal  ( 7 bits -> 0..127 triangles/cluster)
struct UnpackedVisibilityHit
{
    bool     ValidSurfaceHit      = false; // [-]   - False when VisibilityToken32 == 0
    float    ReversedDepth        = 0.0f;  // [0-1] - Reversed-Z depth (1.0 = near, 0.0 = far)
    uint32_t InstanceOrdinal      = 0u;    // [idx] - RigidClusterInstance index
    uint32_t ClusterOrdinal       = 0u;    // [idx] - ClusterHeaderEntry index
    uint32_t LocalTriangleOrdinal = 0u;    // [idx] - Triangle index [0..127] inside cluster
};

[[nodiscard]] uint32_t PackVisibilityToken32(
    uint32_t InstanceOrdinal,
    uint32_t ClusterOrdinal,
    uint32_t LocalTriangleOrdinal) noexcept;

[[nodiscard]] uint64_t PackVisibilityPayload64(float ReversedDepth, uint32_t VisibilityToken32) noexcept;

[[nodiscard]] UnpackedVisibilityHit UnpackVisibilityPayload64(uint64_t PackedWord) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VisibilityBufferTargets
{
    uint32_t              WidthPx             = 0u;   // [px]    - Viewport width
    uint32_t              HeightPx            = 0u;   // [px]    - Viewport height
    std::vector<uint64_t> OpaquePrimary64     {};     // [8B/px] - AtomicMax64 primary opaque visibility buffer
    std::vector<uint64_t> GlassPeelFront64    {};     // [8B/px] - Nearest transparent dielectric layer in front of opaque
    std::vector<uint64_t> GlassPeelSecond64   {};     // [8B/px] - Second transparent dielectric exit/back layer
    uint32_t              HardwareTriangles   = 0u;   // [count] - Triangles rasterized via >=4 px HW path
    uint32_t              MicroComputeTris    = 0u;   // [count] - Triangles rasterized via <4 px SW atomicMax64 path
    uint32_t              GlassPeeledTris     = 0u;   // [count] - Transparent triangles rasterized into 2-layer peel

    void AllocateClearedImage(uint32_t ViewportWidth, uint32_t ViewportHeight);
};

//------------------------------------------------------------------------------------------------------------------------
//                                            VISIBILITY CLUSTER RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

class VisibilityClusterRasterization
{
public:
    // Executes Hardware (>=4 px) + Software Compute (<4 px) opaque visibility passes into OpaquePrimary64
    static void RasterizeOpaqueClusters(
        const PerspectiveCameraFrame&            Camera,
        std::span<const RigidClusterInstance>    Instances,
        std::span<const VisibleClusterCommand>   HardwareBatches,
        std::span<const VisibleClusterCommand>   MicroSoftwareBatches,
        VisibilityBufferTargets&                 InOutTargets);

    // Executes 2-layer Order-Independent Dielectric Depth Peel strictly in front of OpaquePrimary64
    static void RasterizeTwoLayerGlassPeel(
        const PerspectiveCameraFrame&            Camera,
        std::span<const RigidClusterInstance>    Instances,
        std::span<const VisibleClusterCommand>   GlassBatches,
        VisibilityBufferTargets&                 InOutTargets);
};

} // namespace Frontier::GeometricRaster
