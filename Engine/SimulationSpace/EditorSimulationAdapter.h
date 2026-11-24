/*============================================================================================================================================
                                                  EDITORSIMULATIONADAPTER.H
============================================================================================================================================*/
// 🧩 Isolated Editor <-> SimulationSpace adapter keeping EditorLifecycle.cpp merge footprint minimal and conflict-free
//    with WorldInstanceRegister / WorldSceneSyncBridge / RuntimeWorldHost. Owns Vulkan attachment, Play/Pause/Step/Stop
//    toolbar synchronization, and live Vulkan SimulationViewportOverlayPass compositing.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationViewportOverlayPass.h"
#include "Engine/SimulationSpace/WorldSimulationSession.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace Frontier::SimulationSpace
{

struct EditorSimulationToolbarState
{
    bool                 PlayOrSimulateActive = false;
    bool                 Paused               = false;
    bool                 StepRequested        = false;
    double               DeltaSeconds         = 0.0;
    std::array<float, 3> CameraWorldMeters    { 4.10f, 2.35f, 4.95f };
};

class EditorSimulationAdapter
{
public:
    EditorSimulationAdapter() = default;
    ~EditorSimulationAdapter() = default;

    EditorSimulationAdapter(const EditorSimulationAdapter&)            = delete;
    EditorSimulationAdapter& operator=(const EditorSimulationAdapter&) = delete;

#if FRONTIER_SIM_OVERLAY_HAS_VULKAN
    /// Attaches a live VulkanInterchange to both `worldSession` and the live `SimulationViewportOverlayPass`.
    [[nodiscard]] bool AttachVulkanDevice(
        WorldSimulationSession&             worldSession,
        GeometricRaster::VulkanInterchange* boundDevice,
        const std::filesystem::path&        compiledShaderDirectory);

    /// Composites active GPU simulation domains (Fluid 3D volume, Particle cloud, Deformable mesh) over `worldSourceView`
    /// with zero CPU readback. Returns `worldSourceView` unchanged when no GPU simulation domain is active.
    [[nodiscard]] VkImageView CompositeOverWorldImage(
        const WorldSimulationSession&  worldSession,
        VkImageView                    worldSourceView,
        uint32_t                       widthPx,
        uint32_t                       heightPx,
        const SimulationOverlayCamera& camera) noexcept;
#endif

    /// Synchronizes Editor Viewport toolbar Play / Pause / Step / Stop state with `worldSession`.
    /// Returns true if a simulation tick or single-step ran this frame (triggering viewport refresh).
    [[nodiscard]] bool SynchronizeToolbarControls(
        WorldSimulationSession&            worldSession,
        const EditorSimulationToolbarState& toolbarState,
        const std::function<bool()>&       beginPlayCallback);

    void Teardown() noexcept;

    [[nodiscard]] bool QueryOverlayReady() const noexcept { return OverlayPass_.QueryReady(); }
    [[nodiscard]] const SimulationViewportOverlayPass& InspectOverlayPass() const noexcept { return OverlayPass_; }
    [[nodiscard]] SimulationViewportOverlayPass&       AccessOverlayPass() noexcept { return OverlayPass_; }

private:
    SimulationViewportOverlayPass OverlayPass_              {};
    SimulationGpuRenderFeedPacket ReusableFeedPacket_       {};
    bool                          LastToolbarActive_        = false;
    bool                          LastToolbarPaused_        = false;
    bool                          HeroSurfaceUploaded_      = false;
};

} // namespace Frontier::SimulationSpace
