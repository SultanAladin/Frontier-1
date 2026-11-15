/*============================================================================================================================================
                                                      VISIBILITYCLUSTERRASTERIZATION.CPP
============================================================================================================================================*/
// 🧩 Exact 64-Bit (8 B/px) Visibility Buffer (ReversedDepth32 | VisibilityToken32) + HW/SW Micro-Raster + 2-Layer Glass Depth Peel

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/VisibilityClusterRasterization.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   64-BIT VISIBILITY TOKEN
//------------------------------------------------------------------------------------------------------------------------

uint32_t PackVisibilityToken32(
    uint32_t InstanceOrdinal,
    uint32_t ClusterOrdinal,
    uint32_t LocalTriangleOrdinal) noexcept
{
    const uint32_t EncInst = std::min(2047u, InstanceOrdinal + 1u);
    const uint32_t EncClus = ClusterOrdinal & 0x3FFFu;
    const uint32_t EncTri  = LocalTriangleOrdinal & 0x7Fu;
    return (EncInst << 21u) | (EncClus << 7u) | EncTri;
}

uint64_t PackVisibilityPayload64(float ReversedDepth, uint32_t VisibilityToken32) noexcept
{
    const float ClampedDepth = std::clamp(ReversedDepth, 0.0f, 1.0f);
    // IEEE-754 positive floats [0.0..1.0] preserve exact monotonic integer ordering under unsigned uint64_t comparison!
    const uint32_t DepthBits = std::bit_cast<uint32_t>(ClampedDepth);
    return (static_cast<uint64_t>(DepthBits) << 32u) | static_cast<uint64_t>(VisibilityToken32);
}

UnpackedVisibilityHit UnpackVisibilityPayload64(uint64_t PackedWord) noexcept
{
    UnpackedVisibilityHit Hit {};
    const uint32_t Token32 = static_cast<uint32_t>(PackedWord & 0xFFFFFFFFull);
    if (Token32 == 0u)
    {
        return Hit;
    }

    const uint32_t EncInst = (Token32 >> 21u) & 0x7FFu;
    if (EncInst == 0u)
    {
        return Hit;
    }

    Hit.ValidSurfaceHit      = true;
    Hit.ReversedDepth        = std::bit_cast<float>(static_cast<uint32_t>(PackedWord >> 32u));
    Hit.InstanceOrdinal      = EncInst - 1u;
    Hit.ClusterOrdinal       = (Token32 >> 7u) & 0x3FFFu;
    Hit.LocalTriangleOrdinal = Token32 & 0x7Fu;
    return Hit;
}

void VisibilityBufferTargets::AllocateClearedImage(uint32_t ViewportWidth, uint32_t ViewportHeight)
{
    WidthPx  = std::max(1u, ViewportWidth);
    HeightPx = std::max(1u, ViewportHeight);
    const std::size_t TotalPixels = static_cast<std::size_t>(WidthPx) * HeightPx;
    OpaquePrimary64.assign(TotalPixels, 0ull);
    GlassPeelFront64.assign(TotalPixels, 0ull);
    GlassPeelSecond64.assign(TotalPixels, 0ull);
    HardwareTriangles = 0u;
    MicroComputeTris  = 0u;
    GlassPeeledTris   = 0u;
}

namespace
{

struct ProjectedVertexScreen
{
    float ScreenX       = 0.0f; // [px]  - Jittered pixel X coordinate
    float ScreenY       = 0.0f; // [px]  - Jittered pixel Y coordinate
    float InvViewZ      = 0.0f; // [1/m] - Perspective 1 / ViewZ
    float ReversedDepth = 0.0f; // [0-1] - Exact hyperbolic reversed-Z depth (linear in screen space!)
    bool  BehindNear    = true; // [-]   - Clipped behind near plane
};

[[nodiscard]] ProjectedVertexScreen ProjectWorldVertex(
    const OrthonormalCameraBasis& Basis,
    Mathematics::Axis3D           WorldPos) noexcept
{
    ProjectedVertexScreen Out {};
    const Mathematics::Axis3D Rel = WorldPos - Basis.CameraOrigin;
    const float ViewX = Mathematics::EvaluateDot(Rel, Basis.RightAxis);
    const float ViewY = Mathematics::EvaluateDot(Rel, Basis.UpAxis);
    const float ViewZ = Mathematics::EvaluateDot(Rel, Basis.ForwardAxis);

    if (ViewZ <= Basis.NearClip * 0.5f)
    {
        return Out;
    }

    Out.BehindNear    = false;
    Out.InvViewZ      = 1.0f / ViewZ;
    Out.ReversedDepth = EncodeReversedLinearDepth(ViewZ, Basis.NearClip, Basis.FarClip);

    const float NdcX = (ViewX * Out.InvViewZ) / std::max(1.0e-5f, Basis.TanHalfFovX);
    const float NdcY = (ViewY * Out.InvViewZ) / std::max(1.0e-5f, Basis.TanHalfFovY);

    Out.ScreenX = (NdcX * 0.5f + 0.5f) * static_cast<float>(Basis.WidthPx) + Basis.JitterPx.X;
    Out.ScreenY = (0.5f - NdcY * 0.5f) * static_cast<float>(Basis.HeightPx) + Basis.JitterPx.Y;
    return Out;
}

template <typename PixelAcceptFn>
void RasterizeProjectedTriangle(
    const ProjectedVertexScreen& V0,
    const ProjectedVertexScreen& V1,
    const ProjectedVertexScreen& V2,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    bool                         CullBackface,
    PixelAcceptFn&&              AcceptPixel)
{
    if (V0.BehindNear || V1.BehindNear || V2.BehindNear)
    {
        return;
    }

    // Signed 2x screen-space area (screen +X right, +Y down -> CCW world front-face has Det2D < 0; handle signed orientation)
    const float Edge1X = V1.ScreenX - V0.ScreenX;
    const float Edge1Y = V1.ScreenY - V0.ScreenY;
    const float Edge2X = V2.ScreenX - V0.ScreenX;
    const float Edge2Y = V2.ScreenY - V0.ScreenY;
    const float SignedArea2 = Edge1X * Edge2Y - Edge1Y * Edge2X;

    // In Z-up right-handed camera with screen +Y pointing DOWN, a CCW front-facing triangle has SignedArea2 < 0
    if (CullBackface && SignedArea2 >= -1.0e-6f)
    {
        return;
    }
    if (std::fabs(SignedArea2) <= 1.0e-6f)
    {
        return;
    }

    const float MinXF = std::floor(std::min({ V0.ScreenX, V1.ScreenX, V2.ScreenX }));
    const float MaxXF = std::ceil(std::max({ V0.ScreenX, V1.ScreenX, V2.ScreenX }));
    const float MinYF = std::floor(std::min({ V0.ScreenY, V1.ScreenY, V2.ScreenY }));
    const float MaxYF = std::ceil(std::max({ V0.ScreenY, V1.ScreenY, V2.ScreenY }));

    if (MaxXF < 0.0f || MinXF >= static_cast<float>(WidthPx) ||
        MaxYF < 0.0f || MinYF >= static_cast<float>(HeightPx))
    {
        return;
    }

    const int X0 = std::max(0, static_cast<int>(MinXF));
    const int X1 = std::min(static_cast<int>(WidthPx) - 1, static_cast<int>(MaxXF));
    const int Y0 = std::max(0, static_cast<int>(MinYF));
    const int Y1 = std::min(static_cast<int>(HeightPx) - 1, static_cast<int>(MaxYF));

    const float InvArea2 = 1.0f / SignedArea2;
    const float Orient   = (SignedArea2 < 0.0f) ? -1.0f : 1.0f;

    for (int PY = Y0; PY <= Y1; ++PY)
    {
        const float SampleY = static_cast<float>(PY) + 0.5f;
        for (int PX = X0; PX <= X1; ++PX)
        {
            const float SampleX = static_cast<float>(PX) + 0.5f;

            // Edge functions evaluated at pixel center (PX + 0.5, PY + 0.5)
            const float W0Raw = (V1.ScreenX - SampleX) * (V2.ScreenY - SampleY) - (V1.ScreenY - SampleY) * (V2.ScreenX - SampleX);
            const float W1Raw = (V2.ScreenX - SampleX) * (V0.ScreenY - SampleY) - (V2.ScreenY - SampleY) * (V0.ScreenX - SampleX);
            const float W2Raw = (V0.ScreenX - SampleX) * (V1.ScreenY - SampleY) - (V0.ScreenY - SampleY) * (V1.ScreenX - SampleX);

            if (W0Raw * Orient >= -1.0e-6f && W1Raw * Orient >= -1.0e-6f && W2Raw * Orient >= -1.0e-6f)
            {
                const float Bary0 = W0Raw * InvArea2;
                const float Bary1 = W1Raw * InvArea2;
                const float Bary2 = 1.0f - Bary0 - Bary1;
                // Hyperbolic reversed-Z is an affine function of screen-space coordinates!
                const float PixelRevZ = std::clamp(
                    Bary0 * V0.ReversedDepth + Bary1 * V1.ReversedDepth + Bary2 * V2.ReversedDepth,
                    0.0f,
                    1.0f);
                AcceptPixel(static_cast<uint32_t>(PX), static_cast<uint32_t>(PY), PixelRevZ);
            }
        }
    }
}

void ExecuteOpaqueClusterSpan(
    const OrthonormalCameraBasis&          Basis,
    std::span<const RigidClusterInstance>  Instances,
    std::span<const VisibleClusterCommand> Commands,
    VisibilityBufferTargets&               InOutTargets,
    uint32_t&                              InOutTriCounter)
{
    for (const VisibleClusterCommand& Cmd : Commands)
    {
        const RigidClusterInstance& Inst = Instances[Cmd.InstanceOrdinal];
        const auto& Pyramid              = *Inst.PyramidAsset;
        const auto& Cluster              = Pyramid.Clusters[Cmd.ClusterOrdinal];
        const float Scale                = std::max(1.0e-4f, Inst.UniformScale);

        for (uint32_t LocalTri = 0u; LocalTri < Cluster.TriangleCount; ++LocalTri)
        {
            const auto& Tri = Pyramid.Triangles[Cluster.TriangleStartOffset + LocalTri];
            const Mathematics::Axis3D P0 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex0] * Scale;
            const Mathematics::Axis3D P1 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex1] * Scale;
            const Mathematics::Axis3D P2 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex2] * Scale;

            const ProjectedVertexScreen SV0 = ProjectWorldVertex(Basis, P0);
            const ProjectedVertexScreen SV1 = ProjectWorldVertex(Basis, P1);
            const ProjectedVertexScreen SV2 = ProjectWorldVertex(Basis, P2);
            const uint32_t Token32          = PackVisibilityToken32(Cmd.InstanceOrdinal, Cmd.ClusterOrdinal, LocalTri);

            ++InOutTriCounter;
            RasterizeProjectedTriangle(
                SV0, SV1, SV2, InOutTargets.WidthPx, InOutTargets.HeightPx, true,
                [&](uint32_t PX, uint32_t PY, float RevZ) noexcept
                {
                    const uint64_t Candidate = PackVisibilityPayload64(RevZ, Token32);
                    uint64_t&      Cell      = InOutTargets.OpaquePrimary64[static_cast<std::size_t>(PY) * InOutTargets.WidthPx + PX];
                    // Exact 64-bit atomicMax semantics: nearest Reversed-Z surface wins deterministically!
                    if (Candidate > Cell)
                    {
                        Cell = Candidate;
                    }
                });
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                            VISIBILITY CLUSTER RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

void VisibilityClusterRasterization::RasterizeOpaqueClusters(
    const PerspectiveCameraFrame&          Camera,
    std::span<const RigidClusterInstance>  Instances,
    std::span<const VisibleClusterCommand> HardwareBatches,
    std::span<const VisibleClusterCommand> MicroSoftwareBatches,
    VisibilityBufferTargets&               InOutTargets)
{
    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    ExecuteOpaqueClusterSpan(Basis, Instances, HardwareBatches, InOutTargets, InOutTargets.HardwareTriangles);
    ExecuteOpaqueClusterSpan(Basis, Instances, MicroSoftwareBatches, InOutTargets, InOutTargets.MicroComputeTris);
}

void VisibilityClusterRasterization::RasterizeTwoLayerGlassPeel(
    const PerspectiveCameraFrame&          Camera,
    std::span<const RigidClusterInstance>  Instances,
    std::span<const VisibleClusterCommand> GlassBatches,
    VisibilityBufferTargets&               InOutTargets)
{
    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);

    for (const VisibleClusterCommand& Cmd : GlassBatches)
    {
        const RigidClusterInstance& Inst = Instances[Cmd.InstanceOrdinal];
        const auto& Pyramid              = *Inst.PyramidAsset;
        const auto& Cluster              = Pyramid.Clusters[Cmd.ClusterOrdinal];
        const float Scale                = std::max(1.0e-4f, Inst.UniformScale);

        for (uint32_t LocalTri = 0u; LocalTri < Cluster.TriangleCount; ++LocalTri)
        {
            const auto& Tri = Pyramid.Triangles[Cluster.TriangleStartOffset + LocalTri];
            const Mathematics::Axis3D P0 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex0] * Scale;
            const Mathematics::Axis3D P1 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex1] * Scale;
            const Mathematics::Axis3D P2 = Inst.WorldTranslation + Pyramid.Positions[Tri.VertexIndex2] * Scale;

            const ProjectedVertexScreen SV0 = ProjectWorldVertex(Basis, P0);
            const ProjectedVertexScreen SV1 = ProjectWorldVertex(Basis, P1);
            const ProjectedVertexScreen SV2 = ProjectWorldVertex(Basis, P2);
            const uint32_t Token32          = PackVisibilityToken32(Cmd.InstanceOrdinal, Cmd.ClusterOrdinal, LocalTri);

            ++InOutTargets.GlassPeeledTris;
            // CullBackface = false so both entry (front) and exit (back) dielectric glass walls peel cleanly!
            RasterizeProjectedTriangle(
                SV0, SV1, SV2, InOutTargets.WidthPx, InOutTargets.HeightPx, false,
                [&](uint32_t PX, uint32_t PY, float RevZ) noexcept
                {
                    const std::size_t Offset    = static_cast<std::size_t>(PY) * InOutTargets.WidthPx + PX;
                    const uint64_t    OpaqueVal = InOutTargets.OpaquePrimary64[Offset];
                    const uint64_t    Candidate = PackVisibilityPayload64(RevZ, Token32);
                    // Must lie strictly in front of nearest opaque surface!
                    if (Candidate <= OpaqueVal)
                    {
                        return;
                    }

                    uint64_t& Front  = InOutTargets.GlassPeelFront64[Offset];
                    uint64_t& Second = InOutTargets.GlassPeelSecond64[Offset];
                    if (Candidate > Front)
                    {
                        // Push previous front layer to second peel layer if distinct depth
                        const float PrevFrontZ = std::bit_cast<float>(static_cast<uint32_t>(Front >> 32u));
                        if (Front != 0ull && (RevZ - PrevFrontZ) > 1.0e-5f)
                        {
                            Second = Front;
                        }
                        Front = Candidate;
                    }
                    else if (Candidate > Second)
                    {
                        const float CurFrontZ = std::bit_cast<float>(static_cast<uint32_t>(Front >> 32u));
                        if ((CurFrontZ - RevZ) > 1.0e-5f)
                        {
                            Second = Candidate;
                        }
                    }
                });
        }
    }
}

} // namespace Frontier::GeometricRaster
