/*============================================================================================================================================
                                                     VERTEXMOTIONATLASPLAYER.CPP
============================================================================================================================================*/
// 🧩 Phase D05 0.00 ms CPU-physics GPU/CPU Vertex Motion Atlas playback cursor, sub-slice linear interpolation & axle pose sync

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasPlayer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 VERTEX MOTION ATLAS PLAYER
//------------------------------------------------------------------------------------------------------------------------

void VertexMotionAtlasPlayer::SetAtlas(const VertexMotionAtlas* atlas) noexcept
{
    Atlas_       = atlas;
    CursorSlice_ = 0.0f;
}

void VertexMotionAtlasPlayer::ScrubToSlice(float sliceOrdinal) noexcept
{
    if (Atlas_ == nullptr || Atlas_->SliceCount == 0u)
    {
        CursorSlice_ = 0.0f;
        return;
    }
    const float F = static_cast<float>(Atlas_->SliceCount);
    CursorSlice_ = std::fmod(std::fmod(sliceOrdinal, F) + F, F);
}

AtlasPlaybackReading VertexMotionAtlasPlayer::AdvanceAndSamplePose(float dtSeconds) noexcept
{
    AtlasPlaybackReading out {};
    if (Atlas_ == nullptr || !Atlas_->Ready())
    {
        return out;
    }

    const float F = static_cast<float>(Atlas_->SliceCount);
    if (Playing_)
    {
        CursorSlice_ = std::fmod(std::fmod(CursorSlice_ + dtSeconds * Atlas_->SlicesPerSecond * Speed_, F) + F, F);
    }

    const uint32_t f0 = static_cast<uint32_t>(std::floor(CursorSlice_)) % Atlas_->SliceCount;
    const uint32_t f1 = (f0 + 1u) % Atlas_->SliceCount;
    const float t     = CursorSlice_ - std::floor(CursorSlice_);

    const RigidSlicePose& a = Atlas_->SlicePoses[f0];
    const RigidSlicePose& b = Atlas_->SlicePoses[f1];
    const bool colJump = std::fabs(b.ColliderX - a.ColliderX) > 1.5f;

    out.Slice0        = f0;
    out.Slice1        = f1;
    out.SliceFraction = colJump ? 0.0f : t;
    out.Pose.AxleX        = a.AxleX + (b.AxleX - a.AxleX) * t;
    out.Pose.AxleY        = a.AxleY + (b.AxleY - a.AxleY) * t;
    out.Pose.SpinAngleRad = a.SpinAngleRad + (b.SpinAngleRad - a.SpinAngleRad) * (colJump ? 0.0f : t);
    out.Pose.ColliderX    = colJump ? a.ColliderX : (a.ColliderX + (b.ColliderX - a.ColliderX) * t);
    out.Pose.ColliderY    = a.ColliderY + (b.ColliderY - a.ColliderY) * t;
    out.Pose.ColliderZ    = a.ColliderZ + (b.ColliderZ - a.ColliderZ) * t;
    return out;
}

void VertexMotionAtlasPlayer::SampleInterpolatedSurface(
    const VertexMotionAtlas& atlas,
    const AtlasPlaybackReading& reading,
    DeformedSurfaceSample& outSample)
{
    const uint32_t V = atlas.VertexCount;
    outSample.Positions.resize(static_cast<size_t>(V) * 3u);
    outSample.Normals.resize(static_cast<size_t>(V) * 3u);
    outSample.Strain.resize(V);

    const float frac = reading.SliceFraction;
    const float invF = 1.0f - frac;

    for (uint32_t vi = 0u; vi < V; ++vi)
    {
        const size_t pA = atlas.TexelOffset(vi, reading.Slice0);
        const size_t pB = atlas.TexelOffset(vi, reading.Slice1);
        const size_t v3 = static_cast<size_t>(vi) * 3u;

        outSample.Positions[v3 + 0u] = atlas.PositionStrainRgba[pA + 0u] * invF + atlas.PositionStrainRgba[pB + 0u] * frac;
        outSample.Positions[v3 + 1u] = atlas.PositionStrainRgba[pA + 1u] * invF + atlas.PositionStrainRgba[pB + 1u] * frac;
        outSample.Positions[v3 + 2u] = atlas.PositionStrainRgba[pA + 2u] * invF + atlas.PositionStrainRgba[pB + 2u] * frac;
        outSample.Strain[vi]         = atlas.PositionStrainRgba[pA + 3u] * invF + atlas.PositionStrainRgba[pB + 3u] * frac;

        const float nx = atlas.NormalRgba[pA + 0u] * invF + atlas.NormalRgba[pB + 0u] * frac;
        const float ny = atlas.NormalRgba[pA + 1u] * invF + atlas.NormalRgba[pB + 1u] * frac;
        const float nz = atlas.NormalRgba[pA + 2u] * invF + atlas.NormalRgba[pB + 2u] * frac;
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1.0e-5f)
        {
            outSample.Normals[v3 + 0u] = nx / nl;
            outSample.Normals[v3 + 1u] = ny / nl;
            outSample.Normals[v3 + 2u] = nz / nl;
        }
        else
        {
            outSample.Normals[v3 + 0u] = 0.0f;
            outSample.Normals[v3 + 1u] = 1.0f;
            outSample.Normals[v3 + 2u] = 0.0f;
        }
    }
}

VertexMotionAtlasUniforms VertexMotionAtlasPlayer::BuildUniforms(
    const VertexMotionAtlas& atlas,
    const AtlasPlaybackReading& reading,
    uint32_t deformMode,
    bool showHeatmap) noexcept
{
    VertexMotionAtlasUniforms u {};
    u.PackingSlices = { atlas.AtlasWidth, atlas.RowsPerSlice, reading.Slice0, reading.Slice1 };
    u.BlendFlags    = { reading.SliceFraction, static_cast<float>(deformMode), showHeatmap ? 1.0f : 0.0f, 0.0f };
    u.BoundsMinPad  = { atlas.DisplacementMin[0], atlas.DisplacementMin[1], atlas.DisplacementMin[2], 0.0f };
    u.BoundsMaxPad  = { atlas.DisplacementMax[0], atlas.DisplacementMax[1], atlas.DisplacementMax[2], 0.0f };
    return u;
}

} // namespace Frontier::GeometrySpace::Deformation
