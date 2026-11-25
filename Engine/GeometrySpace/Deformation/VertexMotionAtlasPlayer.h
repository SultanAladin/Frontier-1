/*============================================================================================================================================
                                                      VERTEXMOTIONATLASPLAYER.H
============================================================================================================================================*/
// 🧩 Phase D05 0.00 ms CPU-physics GPU/CPU Vertex Motion Atlas playback cursor, sub-slice linear interpolation & axle pose sync
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.4

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  INTERPOLATED PLAYBACK POSE
//------------------------------------------------------------------------------------------------------------------------

struct AtlasPlaybackReading
{
    uint32_t       Slice0        = 0u;
    uint32_t       Slice1        = 0u;
    float          SliceFraction = 0.0f;
    RigidSlicePose Pose          {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VERTEX MOTION ATLAS PLAYER
//------------------------------------------------------------------------------------------------------------------------

class VertexMotionAtlasPlayer
{
public:
    void SetAtlas(const VertexMotionAtlas* atlas) noexcept;
    void SetPlaying(bool playing) noexcept { Playing_ = playing; }
    void SetPlaybackSpeed(float speed) noexcept { Speed_ = speed; }
    void ScrubToSlice(float sliceOrdinal) noexcept;

    [[nodiscard]] bool  IsPlaying() const noexcept { return Playing_; }
    [[nodiscard]] float QueryCursorSlice() const noexcept { return CursorSlice_; }

    /// Advances the cursor by `dtSeconds` (if playing) and returns the interpolated GPU slice indices & rigid axle pose.
    [[nodiscard]] AtlasPlaybackReading AdvanceAndSamplePose(float dtSeconds) noexcept;

    /// Evaluates the exact interpolated vertex positions, normals, and strain on CPU (matches VertexMotionAtlasSampling.slang).
    static void SampleInterpolatedSurface(
        const VertexMotionAtlas& atlas,
        const AtlasPlaybackReading& reading,
        DeformedSurfaceSample& outSample);

    /// Packs the 64-byte GPU uniform block matching VertexMotionAtlasSampling.slang.
    [[nodiscard]] static VertexMotionAtlasUniforms BuildUniforms(
        const VertexMotionAtlas& atlas,
        const AtlasPlaybackReading& reading,
        uint32_t deformMode,
        bool showHeatmap) noexcept;

private:
    const VertexMotionAtlas* Atlas_       = nullptr;
    float                    CursorSlice_ = 0.0f;
    float                    Speed_       = 1.0f;
    bool                     Playing_     = true;
};

} // namespace Frontier::GeometrySpace::Deformation
