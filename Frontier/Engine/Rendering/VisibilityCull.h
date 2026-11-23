/*==========================================================================================================================================
                                                      VISIBILITY CULL DECLARATION
                                     ------------------------------------------------------------------
                 Frustum and occlusion (HiZ) tests over cluster bounds — the portable half of the
                      demo's compute visibility group (plan section 8.1; decision D5: cull + HiZ).
==========================================================================================================================================*/

// VisibilityCull: camera frustum extraction, sphere-frustum tests, and a hierarchical-Z pyramid with a
// conservative occlusion query, all over the ClusterNesting bounds. Depth convention in this portable
// layer: NDC z in [0, 1], 0 at the near plane, 1 at the far plane; the pyramid stores the MAXIMUM
// (farthest) occluder depth per texel. A sphere is occluded only when its nearest point lies beyond the
// farthest recorded occluder in every covered texel — the test may keep occluded geometry, it must never
// cull visible geometry. The GPU phase mirrors these exact predicates in compute.

#pragma once

#include "ClusterNesting.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CAMERA
//------------------------------------------------------------------------------------------------------------------------

struct CullCamera
{
    SpatialVector Position;               // [m]
    SpatialVector Forward{0.0, 0.0, -1.0};
    SpatialVector Right{1.0, 0.0, 0.0};
    SpatialVector Up{0.0, 1.0, 0.0};
    double        FovYRadians = 1.0472;   // [rad] ~60 degrees
    double        Aspect      = 16.0 / 9.0;
    double        NearPlane   = 0.1;      // [m]
    double        FarPlane    = 1000.0;   // [m]
};

struct FrustumPlane
{
    SpatialVector Normal;          // Points INTO the frustum, normalised
    double        Offset = 0.0;    // [m] - Signed distance test: dot(Normal, P) + Offset >= 0 is inside
};

class CameraFrustum
{
  public:
    // Builds the six inward planes from the camera. Returns false for a degenerate basis or clip range.
    bool Build(const CullCamera& Camera);

    [[nodiscard]] bool Intersects(const BoundingSphere& Sphere) const noexcept;
    [[nodiscard]] bool Contains(const SpatialVector& Point) const noexcept;

    [[nodiscard]] const std::vector<FrustumPlane>& Planes() const noexcept { return Planes_; }

  private:
    std::vector<FrustumPlane> Planes_;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      HI-Z PYRAMID
//------------------------------------------------------------------------------------------------------------------------

class HiZPyramid
{
  public:
    // Builds mip levels of maximum depth from a row-major NDC-z buffer ([0] near .. [1] far), level 0
    // first. Returns false for an empty or mismatched buffer.
    bool BuildFromDepth(const double* NdcDepth, uint32_t Width, uint32_t Height);

    [[nodiscard]] uint32_t Levels() const noexcept { return static_cast<uint32_t>(Levels_.size()); }
    [[nodiscard]] uint32_t LevelWidth(uint32_t Level) const noexcept;
    [[nodiscard]] uint32_t LevelHeight(uint32_t Level) const noexcept;
    [[nodiscard]] double   Sample(uint32_t Level, uint32_t X, uint32_t Y) const noexcept;

    // Conservative occlusion query for a world-space sphere under Camera. Projects the sphere to an NDC
    // rectangle, picks the mip whose texel roughly covers it, and reports occlusion only when the
    // sphere's nearest NDC z exceeds the farthest occluder depth across the covered texels.
    [[nodiscard]] bool IsOccluded(const CullCamera& Camera, const BoundingSphere& Sphere) const noexcept;

  private:
    struct Level
    {
        uint32_t            Width  = 0;
        uint32_t            Height = 0;
        std::vector<double> MaxDepth;
    };
    std::vector<Level> Levels_;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CULL DRIVERS
//------------------------------------------------------------------------------------------------------------------------

// Visible subset of Cut (node indices into Nesting) surviving the frustum and, when Pyramid is non-null,
// the occlusion test. Input order is preserved so results are deterministic.
void CullCut(const ClusterNesting& Nesting, const std::vector<uint32_t>& Cut, const CameraFrustum& Frustum,
             const CullCamera& Camera, const HiZPyramid* Pyramid, std::vector<uint32_t>& OutVisible);

} // namespace Frontier
