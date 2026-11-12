/*============================================================================================================================================
                                                          UVPROJECTIONSOLVERS.H
============================================================================================================================================*/
// 🧩 Angle-Based Flattening (ABF++), Smart UV Angle-Cluster Projection, Analytic Box/Cylinder/Sphere/View & Follow Active Quads solvers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Viewports/ViewportProjection.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <cstdint>
#include <span>

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class UVProjectionMethod : uint8_t
{
    ConformalLSCM     = 0u, // [-] - Cotangent LSCM harmonic conformal unwrap
    AngleBasedABF     = 1u, // [-] - Angle-Based Flattening (ABF++) interior-angle optimizer
    SmartAngleProject = 2u, // [-] - Normal-clustered planar projection (66 deg default)
    CubicBox          = 3u, // [-] - 6-axis triplanar box projection
    Cylindrical       = 4u, // [-] - Polar cylindrical wrap around Y axis
    Spherical         = 5u, // [-] - Equirectangular spherical projection
    CameraView        = 6u, // [-] - Planar projection from active ViewportProjection camera
    FollowActiveQuads = 7u, // [-] - Strip-aligned quad propagation from active reference quad
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class UVProjectionSolvers
{
public:
    static uint32_t FlattenAngleBasedABF(
        GeometrySpace::PolygonNetwork& Network,
        uint32_t                       AngleIterations = 12u);

    static uint32_t ProjectSmartAngleClusters(
        GeometrySpace::PolygonNetwork& Network,
        float                          AngleLimitDegrees = 66.0f,
        float                          IslandMarginUV    = 0.02f);

    static void ProjectCubicBox(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      TargetFaces = {});

    static void ProjectCylindrical(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      TargetFaces = {});

    static void ProjectSpherical(
        GeometrySpace::PolygonNetwork& Network,
        std::span<const uint32_t>      TargetFaces = {});

    static void ProjectFromCameraView(
        GeometrySpace::PolygonNetwork&    Network,
        const Editor::ViewportProjection& Projection,
        std::span<const uint32_t>         TargetFaces = {});

    static uint32_t FollowActiveQuads(
        GeometrySpace::PolygonNetwork& Network,
        uint32_t                       ActiveFaceIndex,
        std::span<const uint32_t>      CandidateFaces = {});
};

} // namespace Frontier::Workspaces
