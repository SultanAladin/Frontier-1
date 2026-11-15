/*============================================================================================================================================
                                                            DEPTHPYRAMIDSEQUENCE.H
============================================================================================================================================*/
// 🧩 Conservative Reversed-Z Hierarchical Depth Pyramid (Hi-Z, Near=1.0, Far=0.0) for Two-Phase GPU/CPU Cluster Occlusion Culling

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct PerspectiveCameraFrame
{
    Mathematics::Axis3D CameraOrigin       { 0.0f, -4.5f, 1.8f }; // [m]   - World-space eye position (Z-up right-handed)
    Mathematics::Axis3D TargetPoint        { 0.0f,  0.0f, 0.4f }; // [m]   - World-space look-at target
    Mathematics::Axis3D WorldUpAxis        { 0.0f,  0.0f, 1.0f }; // [-]   - World Z-up reference direction
    float               VerticalFovRadians = 0.78539816f;         // [rad] - Vertical field of view (default 45 deg)
    float               NearClipMeters     = 0.10f;               // [m]   - Reversed-Z near plane -> Depth = 1.0
    float               FarClipMeters      = 500.0f;              // [m]   - Reversed-Z far plane  -> Depth = 0.0
    uint32_t            ViewportWidthPx    = 640u;                // [px]  - Render target width
    uint32_t            ViewportHeightPx   = 360u;                // [px]  - Render target height
    Mathematics::Axis2D SubpixelJitterPx   { 0.0f, 0.0f };        // [px]  - Temporal AA sub-pixel camera jitter offset
};

struct OrthonormalCameraBasis
{
    Mathematics::Axis3D CameraOrigin {};     // [m] - Eye origin in world space
    Mathematics::Axis3D RightAxis    {};     // [-] - Unit screen +X basis in world space
    Mathematics::Axis3D UpAxis       {};     // [-] - Unit screen -Y (upward) basis in world space
    Mathematics::Axis3D ForwardAxis  {};     // [-] - Unit view forward basis in world space
    float               TanHalfFovY  = 0.4f; // [-] - tan(VerticalFovRadians * 0.5)
    float               TanHalfFovX  = 0.7f; // [-] - TanHalfFovY * AspectRatio
    float               NearClip     = 0.1f; // [m] - Near plane distance
    float               FarClip      = 500.f;// [m] - Far plane distance
    uint32_t            WidthPx      = 1u;   // [px]- Viewport width
    uint32_t            HeightPx     = 1u;   // [px]- Viewport height
    Mathematics::Axis2D JitterPx     {};     // [px]- Sub-pixel jitter offset
};

struct ProjectedScreenRect
{
    bool     ValidProjection   = false; // [-]  - False if wholly behind camera or outside frustum
    bool     ClippedNearPlane  = false; // [-]  - True if bounding sphere intersects z <= NearClip (un-occludable)
    float    MinScreenX        = 0.0f;  // [px] - Clamped left pixel coordinate [0..Width]
    float    MinScreenY        = 0.0f;  // [px] - Clamped top pixel coordinate  [0..Height]
    float    MaxScreenX        = 0.0f;  // [px] - Clamped right pixel coordinate [0..Width]
    float    MaxScreenY        = 0.0f;  // [px] - Clamped bottom pixel coordinate [0..Height]
    float    NearestReversedZ  = 1.0f;  // [0-1]- Closest point's reversed-Z depth (1=near, 0=far)
    float    ProjectedSpanPx   = 0.0f;  // [px] - Maximum screen-space diameter in pixels
};

struct DepthPyramidLevel
{
    uint32_t           Width  = 0u; // [px]  - Mip width
    uint32_t           Height = 0u; // [px]  - Mip height
    std::vector<float> Texels {};   // [0-1] - Conservative MIN reversed-Z depth (farthest surface in footprint)
};

//------------------------------------------------------------------------------------------------------------------------
//                                               DEPTH PYRAMID SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class DepthPyramidSequence
{
public:
    void BuildFromReversedDepthBuffer(
        uint32_t               BaseWidth,
        uint32_t               BaseHeight,
        std::span<const float> ReversedDepthTexels);

    void BuildFromVisibilityBuffer64(
        uint32_t                  BaseWidth,
        uint32_t                  BaseHeight,
        std::span<const uint64_t> PackedVisibilityPixels);

    [[nodiscard]] bool TestSphereVisibleAgainstPyramid(
        const ProjectedScreenRect& ScreenBounds) const noexcept;

    [[nodiscard]] uint32_t QueryMipCount() const noexcept { return static_cast<uint32_t>(Levels.size()); }
    [[nodiscard]] const DepthPyramidLevel& InspectMipLevel(uint32_t MipIndex) const noexcept { return Levels[MipIndex]; }

private:
    std::vector<DepthPyramidLevel> Levels {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA PROJECTION HELPERS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] OrthonormalCameraBasis ComputeCameraBasis(const PerspectiveCameraFrame& Camera) noexcept;

[[nodiscard]] float EncodeReversedLinearDepth(float ViewSpaceDepthMeters, float NearClip, float FarClip) noexcept;

[[nodiscard]] ProjectedScreenRect ProjectBoundingSphereToScreen(
    const OrthonormalCameraBasis& Basis,
    Mathematics::Axis3D           WorldCenter,
    float                         WorldRadius) noexcept;

} // namespace Frontier::GeometricRaster
