/*============================================================================================================================================
                                                            CLUSTERCULLENCODER.CPP
============================================================================================================================================*/
// 🧩 Two-Phase GPU/CPU Cluster Visibility Culler: Frustum + Normal-Cone + Continuous LOD Cut + Hi-Z Split (>=4 px HW vs <4 px SW)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Visibility/ClusterCullEncoder.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] bool TestClusterNormalConeVisible(
    const SpatialAcceleration::ClusterHeaderEntry& Cluster,
    Mathematics::Axis3D                            WorldClusterCenter,
    float                                          WorldRadius,
    Mathematics::Axis3D                            CameraOrigin) noexcept
{
    if (Cluster.NormalConeSinCutoff >= 0.995f)
    {
        return true;
    }

    const Mathematics::Axis3D EyeToCenter = WorldClusterCenter - CameraOrigin;
    const float Dist = Mathematics::EvaluateLength(EyeToCenter);
    if (Dist <= WorldRadius + 1.0e-5f)
    {
        return true;
    }
    const Mathematics::Axis3D ViewDir = EyeToCenter * (1.0f / Dist);
    // Cluster is wholly backfacing only if dot(ViewDir, NormalConeDirection) > NormalConeSinCutoff + WorldRadius/Dist
    const float SphereSlack = WorldRadius / Dist;
    return Mathematics::EvaluateDot(ViewDir, Cluster.NormalConeDirection) <= (Cluster.NormalConeSinCutoff + SphereSlack + 0.02f);
}

void RouteVisibleCommand(
    const VisibleClusterCommand& Cmd,
    bool                         TransparentGlass,
    float                        MicroThresholdPx,
    TwoPhaseCullReport&          OutReport)
{
    if (TransparentGlass)
    {
        OutReport.TransparentGlassBatches.push_back(Cmd);
    }
    else if (Cmd.ProjectedSpanPx < MicroThresholdPx)
    {
        OutReport.MicroSoftwareClusters.push_back(Cmd);
    }
    else
    {
        OutReport.HardwareClusterBatches.push_back(Cmd);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  CLUSTER CULL ENCODER
//------------------------------------------------------------------------------------------------------------------------

TwoPhaseCullReport ClusterCullEncoder::EncodePhaseOneVisibility(
    const PerspectiveCameraBasis&         CurrentCamera,
    std::span<const RigidClusterInstance> Instances,
    const DepthPyramidSequence*           PreviousFrameHiZ,
    const PerspectiveCameraBasis*         PreviousCamera,
    const ClusterCullSettings&            Settings)
{
    TwoPhaseCullReport Report {};
    const OrthonormalCameraBasis CurBasis  = ComputeCameraBasis(CurrentCamera);
    const OrthonormalCameraBasis PrevBasis = ComputeCameraBasis((PreviousCamera != nullptr) ? *PreviousCamera : CurrentCamera);
    const float PixelsPerRadian = static_cast<float>(CurBasis.HeightPx) / std::max(1.0e-5f, 2.0f * CurBasis.TanHalfFovY);

    for (uint32_t InstIdx = 0u; InstIdx < static_cast<uint32_t>(Instances.size()); ++InstIdx)
    {
        const RigidClusterInstance& Inst = Instances[InstIdx];
        if (Inst.PyramidAsset == nullptr || Inst.PyramidAsset->Clusters.empty())
        {
            continue;
        }

        const float Scale = std::max(1.0e-4f, Inst.UniformScale);
        const Mathematics::Axis3D LocalCameraPos = (CurrentCamera.CameraOrigin - Inst.WorldTranslation) * (1.0f / Scale);
        const auto CutSelection = Inst.PyramidAsset->SelectMonotonicCut(
            LocalCameraPos,
            PixelsPerRadian,
            Settings.MaxAllowedLodErrorPx,
            false);

        Report.TotalLodCutClusters += static_cast<uint32_t>(CutSelection.ActiveClusterIndices.size());

        for (uint32_t ClusterIdx : CutSelection.ActiveClusterIndices)
        {
            const auto& Cluster = Inst.PyramidAsset->Clusters[ClusterIdx];
            const Mathematics::Axis3D WorldCenter = Inst.WorldTranslation + Cluster.SphereCenter * Scale;
            const float               WorldRadius = Cluster.SphereRadiusMeters * Scale;

            const ProjectedScreenRect ScreenRect = ProjectBoundingSphereToScreen(CurBasis, WorldCenter, WorldRadius);
            if (!ScreenRect.ValidProjection)
            {
                ++Report.FrustumRejectedCount;
                continue;
            }

            if (Settings.EnableNormalConeBackface && !Inst.TransparentGlass &&
                !TestClusterNormalConeVisible(Cluster, WorldCenter, WorldRadius, CurBasis.CameraOrigin))
            {
                ++Report.NormalConeRejectedCount;
                continue;
            }

            VisibleClusterCommand Cmd {};
            Cmd.InstanceOrdinal  = InstIdx;
            Cmd.ClusterOrdinal   = ClusterIdx;
            Cmd.ProjectedSpanPx  = ScreenRect.ProjectedSpanPx;
            Cmd.NearestReversedZ = ScreenRect.NearestReversedZ;

            if (PreviousFrameHiZ != nullptr && PreviousFrameHiZ->QueryMipCount() > 0u && !Inst.TransparentGlass)
            {
                // Project previous-frame world bounding sphere into PreviousCamera space to test PreviousFrameHiZ accurately!
                const Mathematics::Axis3D PrevWorldCenter = Inst.PreviousTranslation + Cluster.SphereCenter * Scale;
                const ProjectedScreenRect PrevScreenRect  = ProjectBoundingSphereToScreen(PrevBasis, PrevWorldCenter, WorldRadius);
                if (PrevScreenRect.ValidProjection && !PreviousFrameHiZ->TestSphereVisibleAgainstPyramid(PrevScreenRect))
                {
                    Report.PhaseOneDeferredQueue.push_back(Cmd);
                    ++Report.HiZOccludedCount;
                    continue;
                }
            }

            RouteVisibleCommand(Cmd, Inst.TransparentGlass, Settings.MicroClusterThresholdPx, Report);
        }
    }

    const auto FrontToBack = [](const VisibleClusterCommand& Left, const VisibleClusterCommand& Right) noexcept
    {
        return Left.NearestReversedZ > Right.NearestReversedZ;
    };
    std::sort(Report.HardwareClusterBatches.begin(), Report.HardwareClusterBatches.end(), FrontToBack);
    std::sort(Report.TransparentGlassBatches.begin(), Report.TransparentGlassBatches.end(), FrontToBack);

    return Report;
}

void ClusterCullEncoder::ResolvePhaseTwoDisocclusions(
    const PerspectiveCameraBasis&         Camera,
    std::span<const RigidClusterInstance> Instances,
    const DepthPyramidSequence&           CurrentFrameHiZ,
    TwoPhaseCullReport&                   InOutReport,
    const ClusterCullSettings&            Settings)
{
    if (InOutReport.PhaseOneDeferredQueue.empty())
    {
        return;
    }

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    std::vector<VisibleClusterCommand> StillOccluded;
    StillOccluded.reserve(InOutReport.PhaseOneDeferredQueue.size());

    for (const VisibleClusterCommand& Cmd : InOutReport.PhaseOneDeferredQueue)
    {
        const RigidClusterInstance& Inst = Instances[Cmd.InstanceOrdinal];
        const float Scale = std::max(1.0e-4f, Inst.UniformScale);
        const auto& Cluster = Inst.PyramidAsset->Clusters[Cmd.ClusterOrdinal];
        const Mathematics::Axis3D WorldCenter = Inst.WorldTranslation + Cluster.SphereCenter * Scale;
        const float               WorldRadius = Cluster.SphereRadiusMeters * Scale;

        const ProjectedScreenRect ScreenRect = ProjectBoundingSphereToScreen(Basis, WorldCenter, WorldRadius);
        if (CurrentFrameHiZ.TestSphereVisibleAgainstPyramid(ScreenRect))
        {
            RouteVisibleCommand(Cmd, Inst.TransparentGlass, Settings.MicroClusterThresholdPx, InOutReport);
            ++InOutReport.PhaseTwoDisoccluded;
            if (InOutReport.HiZOccludedCount > 0u)
            {
                --InOutReport.HiZOccludedCount;
            }
        }
        else
        {
            StillOccluded.push_back(Cmd);
        }
    }

    InOutReport.PhaseOneDeferredQueue = std::move(StillOccluded);
}

} // namespace Frontier::GeometricRaster
