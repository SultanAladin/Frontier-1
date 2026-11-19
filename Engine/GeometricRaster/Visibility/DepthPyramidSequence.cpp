/*============================================================================================================================================
                                                           DEPTHPYRAMIDSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Conservative Reversed-Z Hierarchical Depth Pyramid (Hi-Z) builder with odd-dimension 3-tap floor guard & sphere occlusion test

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA PROJECTION HELPERS
//------------------------------------------------------------------------------------------------------------------------

OrthonormalCameraBasis ComputeCameraBasis(const PerspectiveCameraBasis& Camera) noexcept
{
    OrthonormalCameraBasis Basis {};
    Basis.CameraOrigin = Camera.CameraOrigin;
    Basis.ForwardAxis  = Mathematics::NormalizeAxis(Camera.TargetPoint - Camera.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    Basis.RightAxis    = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Basis.ForwardAxis, Camera.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    Basis.UpAxis       = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(Basis.RightAxis, Basis.ForwardAxis), { 0.0f, 0.0f, 1.0f });

    Basis.WidthPx     = std::max(1u, Camera.ViewportWidthPx);
    Basis.HeightPx    = std::max(1u, Camera.ViewportHeightPx);
    Basis.NearClip    = std::max(1.0e-3f, Camera.NearClipMeters);
    Basis.FarClip     = std::max(Basis.NearClip + 1.0f, Camera.FarClipMeters);
    Basis.TanHalfFovY = std::tan(std::clamp(Camera.VerticalFovRadians, 0.05f, 2.8f) * 0.5f);
    Basis.TanHalfFovX = Basis.TanHalfFovY * (static_cast<float>(Basis.WidthPx) / static_cast<float>(Basis.HeightPx));
    Basis.JitterPx    = Camera.SubpixelJitterPx;
    return Basis;
}

float EncodeReversedLinearDepth(float ViewSpaceDepthMeters, float NearClip, float FarClip) noexcept
{
    if (ViewSpaceDepthMeters <= NearClip)
    {
        return 1.0f;
    }
    if (ViewSpaceDepthMeters >= FarClip)
    {
        return 0.0f;
    }
    // Exact reversed-Z hyperbolic projection mapping z=Near -> 1.0 and z=Far -> 0.0:
    // d_rev(z) = (Far * (Near / z) - Near * (Near / Far)...) -> standard infinite/finite reversed-Z:
    // d_rev(z) = Near * (Far - z) / (z * (Far - Near))
    return std::clamp((NearClip * (FarClip - ViewSpaceDepthMeters)) /
                      std::max(1.0e-7f, ViewSpaceDepthMeters * (FarClip - NearClip)),
                      0.0f,
                      1.0f);
}

ProjectedScreenRect ProjectBoundingSphereToScreen(
    const OrthonormalCameraBasis& Basis,
    Mathematics::Axis3D           WorldCenter,
    float                         WorldRadius) noexcept
{
    ProjectedScreenRect Out {};
    const Mathematics::Axis3D Rel = WorldCenter - Basis.CameraOrigin;
    const float ViewX = Mathematics::EvaluateDot(Rel, Basis.RightAxis);
    const float ViewY = Mathematics::EvaluateDot(Rel, Basis.UpAxis);
    const float ViewZ = Mathematics::EvaluateDot(Rel, Basis.ForwardAxis);
    const float R     = std::max(0.0f, WorldRadius);

    // Frustum culling against far & behind-camera planes
    if (ViewZ + R <= Basis.NearClip || ViewZ - R >= Basis.FarClip)
    {
        return Out;
    }

    // Exact lateral frustum cone planes: |ViewX| - TanHalfFovX * ViewZ <= R * sqrt(1 + TanHalfFovX^2)
    const float SecX = std::sqrt(1.0f + Basis.TanHalfFovX * Basis.TanHalfFovX);
    const float SecY = std::sqrt(1.0f + Basis.TanHalfFovY * Basis.TanHalfFovY);
    if (std::fabs(ViewX) - Basis.TanHalfFovX * ViewZ > R * SecX ||
        std::fabs(ViewY) - Basis.TanHalfFovY * ViewZ > R * SecY)
    {
        return Out;
    }

    Out.ValidProjection = true;
    const float NearestZ = ViewZ - R;
    if (NearestZ <= Basis.NearClip)
    {
        Out.ClippedNearPlane = true;
        Out.NearestReversedZ = 1.0f;
        Out.MinScreenX       = 0.0f;
        Out.MinScreenY       = 0.0f;
        Out.MaxScreenX       = static_cast<float>(Basis.WidthPx);
        Out.MaxScreenY       = static_cast<float>(Basis.HeightPx);
        Out.ProjectedSpanPx  = static_cast<float>(std::max(Basis.WidthPx, Basis.HeightPx));
        return Out;
    }

    Out.NearestReversedZ = EncodeReversedLinearDepth(NearestZ, Basis.NearClip, Basis.FarClip);

    // Conservative perspective projection of bounding sphere silhouette (Mara & Morgan 2013)
    const float SafeZ   = std::max(Basis.NearClip, ViewZ);
    const float InvTanX = 1.0f / std::max(1.0e-5f, Basis.TanHalfFovX);
    const float InvTanY = 1.0f / std::max(1.0e-5f, Basis.TanHalfFovY);

    const float DistSqXZ = ViewX * ViewX + SafeZ * SafeZ;
    const float DistSqYZ = ViewY * ViewY + SafeZ * SafeZ;
    float HalfExtentNdcX = (R * InvTanX) / std::sqrt(std::max(1.0e-6f, DistSqXZ - R * R));
    float HalfExtentNdcY = (R * InvTanY) / std::sqrt(std::max(1.0e-6f, DistSqYZ - R * R));
    if (DistSqXZ <= R * R * 1.02f) HalfExtentNdcX = 2.0f;
    if (DistSqYZ <= R * R * 1.02f) HalfExtentNdcY = 2.0f;

    const float CenterNdcX = (ViewX / SafeZ) * InvTanX;
    const float CenterNdcY = (ViewY / SafeZ) * InvTanY;

    const float WidthF  = static_cast<float>(Basis.WidthPx);
    const float HeightF = static_cast<float>(Basis.HeightPx);

    const float RawMinX = ((CenterNdcX - HalfExtentNdcX) * 0.5f + 0.5f) * WidthF;
    const float RawMaxX = ((CenterNdcX + HalfExtentNdcX) * 0.5f + 0.5f) * WidthF;
    // Screen Y runs top-down: NDC +Y = top (0), NDC -Y = bottom (Height)
    const float RawMinY = ((0.5f - (CenterNdcY + HalfExtentNdcY) * 0.5f)) * HeightF;
    const float RawMaxY = ((0.5f - (CenterNdcY - HalfExtentNdcY) * 0.5f)) * HeightF;

    if (RawMaxX <= 0.0f || RawMinX >= WidthF || RawMaxY <= 0.0f || RawMinY >= HeightF)
    {
        Out.ValidProjection = false;
        return Out;
    }

    Out.MinScreenX      = std::clamp(RawMinX, 0.0f, WidthF);
    Out.MaxScreenX      = std::clamp(RawMaxX, 0.0f, WidthF);
    Out.MinScreenY      = std::clamp(RawMinY, 0.0f, HeightF);
    Out.MaxScreenY      = std::clamp(RawMaxY, 0.0f, HeightF);
    Out.ProjectedSpanPx = std::max(Out.MaxScreenX - Out.MinScreenX, Out.MaxScreenY - Out.MinScreenY);
    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               DEPTH PYRAMID SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

void DepthPyramidSequence::BuildFromReversedDepthBuffer(
    uint32_t               BaseWidth,
    uint32_t               BaseHeight,
    std::span<const float> ReversedDepthTexels)
{
    Levels.clear();
    if (BaseWidth == 0u || BaseHeight == 0u ||
        ReversedDepthTexels.size() < static_cast<std::size_t>(BaseWidth) * BaseHeight)
    {
        return;
    }

    DepthPyramidLevel Mip0 {};
    Mip0.Width  = BaseWidth;
    Mip0.Height = BaseHeight;
    Mip0.Texels.assign(ReversedDepthTexels.begin(),
                       ReversedDepthTexels.begin() + static_cast<std::ptrdiff_t>(BaseWidth) * BaseHeight);
    Levels.push_back(std::move(Mip0));

    // Conservative floor-halving MIN reduction (in Reversed-Z, smaller depth = FARTHER, so MIN is the conservative occluder horizon!)
    // Includes 3rd row/column whenever previous dimension is odd so zero border texels are ever dropped!
    while (Levels.back().Width > 1u || Levels.back().Height > 1u)
    {
        const DepthPyramidLevel& Prev = Levels.back();
        const uint32_t NextW = std::max(1u, Prev.Width >> 1u);
        const uint32_t NextH = std::max(1u, Prev.Height >> 1u);
        const bool OddW      = (Prev.Width & 1u) != 0u && Prev.Width > 1u;
        const bool OddH      = (Prev.Height & 1u) != 0u && Prev.Height > 1u;

        DepthPyramidLevel Next {};
        Next.Width  = NextW;
        Next.Height = NextH;
        Next.Texels.resize(static_cast<std::size_t>(NextW) * NextH, 0.0f);

        const auto SamplePrev = [&](uint32_t X, uint32_t Y) noexcept -> float
        {
            const uint32_t CX = std::min(X, Prev.Width - 1u);
            const uint32_t CY = std::min(Y, Prev.Height - 1u);
            return Prev.Texels[static_cast<std::size_t>(CY) * Prev.Width + CX];
        };

        for (uint32_t Y = 0u; Y < NextH; ++Y)
        {
            const uint32_t SrcY = Y * 2u;
            for (uint32_t X = 0u; X < NextW; ++X)
            {
                const uint32_t SrcX = X * 2u;
                float MinDepth = std::min({
                    SamplePrev(SrcX + 0u, SrcY + 0u),
                    SamplePrev(SrcX + 1u, SrcY + 0u),
                    SamplePrev(SrcX + 0u, SrcY + 1u),
                    SamplePrev(SrcX + 1u, SrcY + 1u),
                });
                if (OddW)
                {
                    MinDepth = std::min({ MinDepth, SamplePrev(SrcX + 2u, SrcY + 0u), SamplePrev(SrcX + 2u, SrcY + 1u) });
                }
                if (OddH)
                {
                    MinDepth = std::min({ MinDepth, SamplePrev(SrcX + 0u, SrcY + 2u), SamplePrev(SrcX + 1u, SrcY + 2u) });
                }
                if (OddW && OddH)
                {
                    MinDepth = std::min(MinDepth, SamplePrev(SrcX + 2u, SrcY + 2u));
                }
                Next.Texels[static_cast<std::size_t>(Y) * NextW + X] = MinDepth;
            }
        }
        Levels.push_back(std::move(Next));
    }
}

void DepthPyramidSequence::BuildFromVisibilityBuffer64(
    uint32_t                  BaseWidth,
    uint32_t                  BaseHeight,
    std::span<const uint64_t> PackedVisibilityPixels)
{
    std::vector<float> DepthF32(static_cast<std::size_t>(BaseWidth) * BaseHeight, 0.0f);
    const std::size_t Count = std::min(DepthF32.size(), PackedVisibilityPixels.size());
    for (std::size_t Idx = 0u; Idx < Count; ++Idx)
    {
        const uint32_t DepthBits = static_cast<uint32_t>(PackedVisibilityPixels[Idx] >> 32u);
        DepthF32[Idx] = std::bit_cast<float>(DepthBits);
    }
    BuildFromReversedDepthBuffer(BaseWidth, BaseHeight, DepthF32);
}

bool DepthPyramidSequence::TestSphereVisibleAgainstPyramid(const ProjectedScreenRect& ScreenBounds) const noexcept
{
    if (!ScreenBounds.ValidProjection)
    {
        return false;
    }
    if (Levels.empty() || ScreenBounds.ClippedNearPlane)
    {
        return true;
    }

    const DepthPyramidLevel& Base = Levels.front();
    const float SpanX = std::max(1.0f, ScreenBounds.MaxScreenX - ScreenBounds.MinScreenX);
    const float SpanY = std::max(1.0f, ScreenBounds.MaxScreenY - ScreenBounds.MinScreenY);
    const float MaxSpan = std::max(SpanX, SpanY);

    // Select mip where projected rectangle covers at most a 2x2 texel quad: Mip = ceil(log2(MaxSpan))
    const int RawMip = static_cast<int>(std::ceil(std::log2(MaxSpan)));
    const uint32_t MipIdx = static_cast<uint32_t>(
        std::clamp(RawMip, 0, static_cast<int>(Levels.size()) - 1));
    const DepthPyramidLevel& Level = Levels[MipIdx];

    const float NormMinX = ScreenBounds.MinScreenX / static_cast<float>(Base.Width);
    const float NormMaxX = ScreenBounds.MaxScreenX / static_cast<float>(Base.Width);
    const float NormMinY = ScreenBounds.MinScreenY / static_cast<float>(Base.Height);
    const float NormMaxY = ScreenBounds.MaxScreenY / static_cast<float>(Base.Height);

    const uint32_t X0 = std::min(Level.Width - 1u, static_cast<uint32_t>(std::max(0.0f, std::floor(NormMinX * static_cast<float>(Level.Width)))));
    const uint32_t X1 = std::min(Level.Width - 1u, static_cast<uint32_t>(std::max(0.0f, std::floor(NormMaxX * static_cast<float>(Level.Width)))));
    const uint32_t Y0 = std::min(Level.Height - 1u, static_cast<uint32_t>(std::max(0.0f, std::floor(NormMinY * static_cast<float>(Level.Height)))));
    const uint32_t Y1 = std::min(Level.Height - 1u, static_cast<uint32_t>(std::max(0.0f, std::floor(NormMaxY * static_cast<float>(Level.Height)))));

    // Conservative 2x2 horizon query across [X0..X1] x [Y0..Y1]:
    // In Reversed-Z, cluster is OCCLUDED only if its NEAREST depth is strictly behind (<) the FARTHEST occluder depth (min of Hi-Z)!
    float OccluderFarthestReversedZ = 1.0f;
    for (uint32_t Y = Y0; Y <= Y1; ++Y)
    {
        for (uint32_t X = X0; X <= X1; ++X)
        {
            OccluderFarthestReversedZ = std::min(
                OccluderFarthestReversedZ, Level.Texels[static_cast<std::size_t>(Y) * Level.Width + X]);
        }
    }

    return ScreenBounds.NearestReversedZ >= (OccluderFarthestReversedZ - 1.0e-5f);
}

} // namespace Frontier::GeometricRaster
