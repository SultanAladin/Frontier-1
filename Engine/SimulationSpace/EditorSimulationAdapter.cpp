/*============================================================================================================================================
                                                 EDITORSIMULATIONADAPTER.CPP
============================================================================================================================================*/
// 🧩 Implementation of EditorSimulationAdapter isolating EditorLifecycle simulation wiring behind minimal adapter hooks.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EditorSimulationAdapter.h"

#include <cmath>

namespace Frontier::SimulationSpace
{

namespace
{

GeometrySpace::Deformation::CageEmbeddedSurface BuildUniversalCageSurface(const Deformable::DeformableBody& body)
{
    GeometrySpace::Deformation::CageEmbeddedSurface surf {};
    surf.Mode = GeometrySpace::Deformation::CageEmbedMode::Tetrahedral3D;
    if (body.PointCount == 0u || body.RestPositions.size() < static_cast<size_t>(body.PointCount) * 3u)
    {
        return surf;
    }

    auto appendTri = [&](uint32_t i0, uint32_t i1, uint32_t i2, uint32_t hostTet)
    {
        if (i0 >= body.PointCount || i1 >= body.PointCount || i2 >= body.PointCount) return;
        const float* p0 = &body.RestPositions[i0 * 3u];
        const float* p1 = &body.RestPositions[i1 * 3u];
        const float* p2 = &body.RestPositions[i2 * 3u];
        const float e1[3] { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        const float e2[3] { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float n[3] {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0]
        };
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1.0e-6f)
        {
            n[0] /= len; n[1] /= len; n[2] /= len;
        }
        else
        {
            n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f;
        }

        const uint32_t baseV = static_cast<uint32_t>(surf.RestSurface.RestPositions.size() / 3u);
        const uint32_t triIdx[3] { i0, i1, i2 };
        for (uint32_t k = 0u; k < 3u; ++k)
        {
            const uint32_t pt = triIdx[k];
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 0u]);
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 1u]);
            surf.RestSurface.RestPositions.push_back(body.RestPositions[pt * 3u + 2u]);
            surf.RestSurface.RestNormals.push_back(n[0]);
            surf.RestSurface.RestNormals.push_back(n[1]);
            surf.RestSurface.RestNormals.push_back(n[2]);
            surf.CageIndices.push_back(pt); surf.CageIndices.push_back(0u); surf.CageIndices.push_back(0u); surf.CageIndices.push_back(0u);
            surf.CageWeights.push_back(1.0f); surf.CageWeights.push_back(0.0f); surf.CageWeights.push_back(0.0f); surf.CageWeights.push_back(0.0f);
            surf.ExtraParams.push_back(static_cast<float>(hostTet));
            surf.ExtraParams.push_back(0.0f);
            surf.ExtraParams.push_back(0.0f);
            surf.ExtraParams.push_back(0.0f);
            surf.RestSurface.Indices.push_back(baseV + k);
        }
    };

    if (body.TetCount > 0u && body.TetIndices.size() >= static_cast<size_t>(body.TetCount) * 4u)
    {
        for (uint32_t t = 0u; t < body.TetCount; ++t)
        {
            const uint32_t a = body.TetIndices[t * 4u + 0u];
            const uint32_t b = body.TetIndices[t * 4u + 1u];
            const uint32_t c = body.TetIndices[t * 4u + 2u];
            const uint32_t d = body.TetIndices[t * 4u + 3u];
            appendTri(a, c, b, t);
            appendTri(a, b, d, t);
            appendTri(b, c, d, t);
            appendTri(c, a, d, t);
        }
    }
    else
    {
        for (const auto& sheet : body.ClothSheets)
        {
            if (sheet.Columns < 2u || sheet.Rows < 2u) continue;
            for (uint32_t r = 0u; r + 1u < sheet.Rows; ++r)
            {
                for (uint32_t c = 0u; c + 1u < sheet.Columns; ++c)
                {
                    const uint32_t i00 = sheet.StartPointIndex + r * sheet.Columns + c;
                    const uint32_t i10 = i00 + 1u;
                    const uint32_t i01 = i00 + sheet.Columns;
                    const uint32_t i11 = i01 + 1u;
                    appendTri(i00, i10, i11, 0u);
                    appendTri(i00, i11, i01, 0u);
                }
            }
        }
    }
    return surf;
}

} // namespace

#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
bool EditorSimulationAdapter::AttachVulkanDevice(
    WorldSimulationSession&             worldSession,
    GeometricRaster::VulkanInterchange* boundDevice,
    const std::filesystem::path&        compiledShaderDirectory)
{
    if (boundDevice == nullptr || !boundDevice->Ready())
    {
        return false;
    }
    const bool simOk     = worldSession.AttachVulkanInterchange(boundDevice, compiledShaderDirectory);
    const bool overlayOk = OverlayPass_.InitializeOverlay(boundDevice, compiledShaderDirectory);
    HeroSurfaceUploaded_ = false;
    return simOk && overlayOk;
}

VkImageView EditorSimulationAdapter::CompositeOverWorldImage(
    const WorldSimulationSession&  worldSession,
    VkImageView                    worldSourceView,
    uint32_t                       widthPx,
    uint32_t                       heightPx,
    const SimulationOverlayCamera& camera) noexcept
{
    if (!OverlayPass_.QueryReady() || worldSourceView == VK_NULL_HANDLE || widthPx == 0u || heightPx == 0u)
    {
        return worldSourceView;
    }
    if (worldSession.QueryStatus() == PreviewRunStatus::Idle)
    {
        return worldSourceView;
    }

    worldSession.PopulateGpuRenderFeed(ReusableFeedPacket_);
    if (!ReusableFeedPacket_.QueueSync.AnyBackendGpuResident)
    {
        return worldSourceView;
    }

    if (!HeroSurfaceUploaded_ && ReusableFeedPacket_.HeroDeformable.GpuResident &&
        ReusableFeedPacket_.HeroDeformable.PointCount > 0u)
    {
        try
        {
            if (const auto* coord = worldSession.InspectCoordinator(); coord != nullptr)
            {
                const auto& heroBody = coord->InspectDeformables().InspectHeroGpuBody();
                if (heroBody.PointCount > 0u)
                {
                    const auto embedded = BuildUniversalCageSurface(heroBody);
                    if (embedded.VertexCount() > 0u)
                    {
                        HeroSurfaceUploaded_ = OverlayPass_.UploadDeformableSurfaceMesh(
                            embedded, std::max(1u, heroBody.PointCount), std::max(1u, heroBody.TetCount));
                    }
                }
            }
        }
        catch (...)
        {
            HeroSurfaceUploaded_ = false;
        }
    }

    SimulationViewportCompositeRequest req {};
    req.WorldSourceView = worldSourceView;
    req.TargetWidthPx   = widthPx;
    req.TargetHeightPx  = heightPx;
    req.Camera          = camera;
    req.RenderFeed      = ReusableFeedPacket_;

    SimulationViewportCompositeResult res {};
    if (OverlayPass_.CompositeOntoGpuViewport(req, res) && res.ColorImageView != VK_NULL_HANDLE)
    {
        return res.ColorImageView;
    }
    return worldSourceView;
}
#endif

bool EditorSimulationAdapter::SynchronizeToolbarControls(
    WorldSimulationSession&             worldSession,
    const EditorSimulationToolbarState& toolbarState,
    const std::function<bool()>&        beginPlayCallback)
{
    bool stepped = false;

    if (toolbarState.PlayOrSimulateActive)
    {
        // Play / Simulate started from Edit mode
        if (!LastToolbarActive_ && worldSession.QueryStatus() == PreviewRunStatus::Idle)
        {
            if (beginPlayCallback)
            {
                (void)beginPlayCallback();
                HeroSurfaceUploaded_ = false;
            }
        }

        // Pause / Resume transition
        if (toolbarState.Paused && !LastToolbarPaused_)
        {
            (void)worldSession.Pause();
        }
        else if (!toolbarState.Paused && LastToolbarPaused_)
        {
            (void)worldSession.Resume();
        }

        // Single-step while paused (or active)
        if (toolbarState.StepRequested)
        {
            stepped = worldSession.StepSingleTick(1.0 / 60.0, toolbarState.CameraWorldMeters);
        }
        else if (!toolbarState.Paused && worldSession.QueryPlaying() && toolbarState.DeltaSeconds > 0.0)
        {
            worldSession.Tick(toolbarState.DeltaSeconds, toolbarState.CameraWorldMeters);
            stepped = true;
        }
    }
    else if (LastToolbarActive_)
    {
        // Stopped back to Edit mode
        if (worldSession.QueryStatus() != PreviewRunStatus::Idle)
        {
            worldSession.Stop();
            HeroSurfaceUploaded_ = false;
        }
    }
    else if (worldSession.QueryPlaying() && toolbarState.DeltaSeconds > 0.0)
    {
        // Programmatic BeginWorldSimulationPlay() without toolbar button toggle
        worldSession.Tick(toolbarState.DeltaSeconds, toolbarState.CameraWorldMeters);
        stepped = true;
    }

    LastToolbarActive_ = toolbarState.PlayOrSimulateActive;
    LastToolbarPaused_ = toolbarState.Paused;
    return stepped;
}

void EditorSimulationAdapter::Teardown() noexcept
{
    OverlayPass_.TeardownOverlay();
    LastToolbarActive_   = false;
    LastToolbarPaused_   = false;
    HeroSurfaceUploaded_ = false;
}

} // namespace Frontier::SimulationSpace
