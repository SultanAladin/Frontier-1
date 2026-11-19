// =================================================================================================
// Frontier Engine - Drawer Gesture & World Renderer Validation Exhibit
// -------------------------------------------------------------------------------------------------
// Verifies:
//   1. Unified 3-detent (Closed / Half / Full) touch-like mouse drag, flick, tap, and half-open
//      pointer-shield behaviour for PanelExpansionTab + ControlCentrePanel (top) and
//      ContentBrowserPanel (bottom notch + empty top header strip).
//   2. World Renderer acceleration fixes:
//      - Per-object cached 49-degree smooth normals transform cleanly into WideBoundingTree without
//        CPU SDF construction when GPU SDF is available.
//      - GPU SignedDistanceGridCompute.comp.spv constructs the dense narrow-band SDF directly on
//        the GPU from uploaded BVH4 + triangles.
//      - Tier 2 RTX Irradiance Lattice upload is decoupled from SDF rebuilds so background RTX
//        refinement uploads immediately without stalling or requiring an SDF rebuild.
// =================================================================================================

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Notch/PanelExpansionTab.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/PixelSpace.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <imgui.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
    int FailureCount = 0;

    void VerifyCheck(const bool Condition, const char* Message)
    {
        if (!Condition)
        {
            std::cerr << "[FAIL] " << Message << "\n";
            ++FailureCount;
        }
        else
        {
            std::cout << "[PASS] " << Message << "\n";
        }
    }

    void RunTopNotchDetentChecks()
    {
        using namespace Frontier;

        const Rectangle2D HostBounds { 0.0f, 0.0f, 1280.0f, 720.0f };
        const float       HalfHeight = 720.0f * 0.50f;
        const float       FullHeight = 720.0f;

        PanelExpansionTab Tab;
        Tab.ConfigureExtent(HostBounds.Width, HostBounds.Height, 1.0f);
        Tab.SnapDetent(DrawerDetentStop::Closed);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Closed && Tab.QueryHeight() <= 1.0f,
                    "PanelExpansionTab starts Closed at 0 px height");

        auto StepTab = [&](const Point2D Cursor, const bool Pressed, const bool Down, const bool Released, const double Dt, const bool Surface = false)
        {
            Tab.IntegratePointer(Cursor.X, Cursor.Y, Pressed, Down, Released, Dt, Surface);
            Tab.Integrate(Dt);
        };

        // 1. Short tap (< 6 px, <= 350 ms) toggles Closed -> Full -> Closed
        const Rectangle2D ClosedTabRect = Tab.QueryTabBounds();
        const Point2D     TabCentre { ClosedTabRect.X + ClosedTabRect.Width * 0.5f, ClosedTabRect.Y + ClosedTabRect.Height * 0.5f };
        StepTab(TabCentre, true, true, false, 0.016);
        StepTab({ TabCentre.X, TabCentre.Y + 2.0f }, false, true, false, 0.016);
        StepTab({ TabCentre.X, TabCentre.Y + 2.0f }, false, false, true, 0.016);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Full,
                    "Short tap on top Frontier tab expands Closed -> Full");
        for (int Step = 0; Step < 90; ++Step)
        {
            StepTab({ 20.0f, 20.0f }, false, false, false, 0.016);
        }
        VerifyCheck(std::fabs(Tab.QueryHeight() - FullHeight) < 2.0f,
                    "Top tab settles cleanly at Full height");

        const Rectangle2D OpenTabRect = Tab.QueryTabBounds();
        const Point2D     OpenTabCentre { OpenTabRect.X + OpenTabRect.Width * 0.5f, OpenTabRect.Y + OpenTabRect.Height * 0.5f };
        StepTab(OpenTabCentre, true, true, false, 0.016);
        StepTab(OpenTabCentre, false, false, true, 0.016);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Closed,
                    "Short tap on expanded top Frontier tab collapses Full -> Closed");
        for (int Step = 0; Step < 90; ++Step)
        {
            StepTab({ 20.0f, 20.0f }, false, false, false, 0.016);
        }
        VerifyCheck(Tab.QueryHeight() < 1.0f,
                    "Top tab settles cleanly back to Closed");

        // 2. Direct 1:1 tracking and gentle drag to ~36% settles cleanly at Half
        const Rectangle2D StartRect = Tab.QueryTabBounds();
        const Point2D     StartPt { StartRect.X + StartRect.Width * 0.5f, StartRect.Y + StartRect.Height * 0.5f };
        StepTab(StartPt, true, true, false, 0.016);
        for (int Step = 1; Step <= 30; ++Step)
        {
            const float Y = StartPt.Y + (260.0f * static_cast<float>(Step) / 30.0f);
            StepTab({ StartPt.X, Y }, false, true, false, 0.016);
        }
        VerifyCheck(Tab.QueryVerticalDragging() && std::fabs(Tab.QueryHeight() - 260.0f) < 12.0f,
                    "Top notch tracks pointer 1:1 during downward drag");
        StepTab({ StartPt.X, StartPt.Y + 260.0f }, false, true, false, 0.040);
        StepTab({ StartPt.X, StartPt.Y + 260.0f }, false, false, true, 0.016);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Half,
                    "Gentle drag to ~36% height snaps to Half detent");
        for (int Step = 0; Step < 90; ++Step)
        {
            StepTab({ 20.0f, 20.0f }, false, false, false, 0.016);
        }
        VerifyCheck(std::fabs(Tab.QueryHeight() - HalfHeight) < 2.0f,
                    "Half detent settles at 50% host height");

        // 3. SurfaceContact drag from Half downward past 58% settles at Full
        StepTab({ 640.0f, 220.0f }, true, true, false, 0.016, true);
        for (int Step = 1; Step <= 20; ++Step)
        {
            StepTab({ 640.0f, 220.0f + 160.0f * (static_cast<float>(Step) / 20.0f) }, false, true, false, 0.016, true);
        }
        StepTab({ 640.0f, 380.0f }, false, true, false, 0.040, true);
        StepTab({ 640.0f, 380.0f }, false, false, true, 0.016, false);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Full,
                    "Empty-surface downward drag from Half past 58% settles at Full");

        // 4. Fast closing flick from Full collapses straight to Closed
        Tab.SnapDetent(DrawerDetentStop::Full);
        StepTab({ 640.0f, 520.0f }, true, true, false, 0.016, true);
        StepTab({ 640.0f, 440.0f }, false, true, false, 0.016, true);
        StepTab({ 640.0f, 350.0f }, false, true, false, 0.016, true);
        StepTab({ 640.0f, 350.0f }, false, false, true, 0.016, false);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Closed,
                    "Fast upward closing flick from Full snaps cleanly to Closed");

        // 5. Fast opening flick from Closed expands straight to Full
        Tab.SnapDetent(DrawerDetentStop::Closed);
        const Rectangle2D FlickRect = Tab.QueryTabBounds();
        const Point2D     FlickPt { FlickRect.X + FlickRect.Width * 0.5f, FlickRect.Y + FlickRect.Height * 0.5f };
        StepTab(FlickPt, true, true, false, 0.016);
        StepTab({ FlickPt.X, FlickPt.Y + 45.0f }, false, true, false, 0.016);
        StepTab({ FlickPt.X, FlickPt.Y + 110.0f }, false, true, false, 0.016);
        StepTab({ FlickPt.X, FlickPt.Y + 110.0f }, false, false, true, 0.016);
        VerifyCheck(Tab.QueryDetent() == DrawerDetentStop::Full,
                    "Fast downward opening flick from Closed snaps straight to Full");

        // 6. ControlCentre half-open pointer shield covers only upper shade + tab, not lower workspace
        ControlCentrePanel Centre;
        Centre.SynchronizeHostBounds(HostBounds, 1.0f);
        Centre.ConfigureDetent(DrawerDetentStop::Half, true);
        const Rectangle2D ShadeShield = Centre.QueryShadePhysicalBounds();
        VerifyCheck(!Centre.QueryFullScreenBlocking(),
                    "ControlCentre at Half detent does not block full workspace");
        VerifyCheck(ShadeShield.Height > 320.0f && ShadeShield.Height < 380.0f,
                    "ControlCentre half-open shield matches upper 50% shade bounds");
        WorkspaceAppearance::AssignOverlayPointerShield(ShadeShield, Centre.QueryTabPhysicalBounds());
        VerifyCheck(WorkspaceAppearance::QueryOverlayPointerBlockedAt({ 640.0f, 180.0f }),
                    "Overlay pointer shield blocks clicks over half-open ControlCentre shade");
        VerifyCheck(!WorkspaceAppearance::QueryOverlayPointerBlockedAt({ 640.0f, 600.0f }),
                    "Overlay pointer shield leaves lower workspace live while ControlCentre is Half");
        WorkspaceAppearance::ClearOverlayPointerShield();

        Centre.ConfigureDetent(DrawerDetentStop::Full, true);
        VerifyCheck(Centre.QueryFullScreenBlocking(),
                    "ControlCentre at Full detent enables full-screen scrim blocking");
    }

    void RunBottomContentBrowserDetentChecks()
    {
        using namespace Frontier;

        ImGuiContext* Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(Context);
        ImGuiIO& Io = ImGui::GetIO();
        Io.DisplaySize = ImVec2(1280.0f, 720.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        unsigned char* AtlasPixels = nullptr;
        int AtlasW = 0;
        int AtlasH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&AtlasPixels, &AtlasW, &AtlasH);

        ThemeRepository   Theme {};
        const Rectangle2D Viewport { 0.0f, 0.0f, 1280.0f, 720.0f };

        ContentBrowserPanel Browser;
        Browser.AttachThemeRepository(&Theme);
        Browser.SynchronizeHostBounds(Viewport, 1.0f);
        Browser.ConfigureShelfExtent(ContentShelfExtent::Closed, true);

        auto StepFrame = [&](const Point2D Cursor, const bool Down, const bool Pressed, const bool Released, const double Dt = 1.0 / 60.0)
        {
            Io.DisplaySize = ImVec2(Viewport.Width, Viewport.Height);
            Io.DeltaTime   = static_cast<float>(Dt);
            ImGui::NewFrame();
            PixelSpace Canvas(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f);
            ContentBrowserPointerInput Ptr {};
            Ptr.CursorX  = Cursor.X;
            Ptr.CursorY  = Cursor.Y;
            Ptr.HeldDown = Down;
            Ptr.Pressed  = Pressed;
            Ptr.Released = Released;
            Browser.RenderWithInput(Canvas, Viewport, Ptr, Dt);
            ImGui::Render();
        };

        // 1. Drag bottom notch upward gently to ~36% -> SplitHalf
        StepFrame({ 640.0f, 360.0f }, false, false, false);
        const Rectangle2D ClosedNotch = Browser.QueryNotchTabBounds(Viewport);
        const Point2D     NotchPt { ClosedNotch.X + ClosedNotch.Width * 0.5f, ClosedNotch.Y + ClosedNotch.Height * 0.5f };
        StepFrame(NotchPt, true, true, false);
        for (int Step = 1; Step <= 28; ++Step)
        {
            const float Y = NotchPt.Y - (250.0f * static_cast<float>(Step) / 28.0f);
            StepFrame({ NotchPt.X, Y }, true, false, false);
        }
        VerifyCheck(Browser.QueryShelfDragging(),
                    "ContentBrowser enters active shelf-drag capture while pulling bottom notch");
        StepFrame({ NotchPt.X, NotchPt.Y - 250.0f }, true, false, false, 0.045);
        StepFrame({ NotchPt.X, NotchPt.Y - 250.0f }, false, false, true);
        VerifyCheck(!Browser.QueryShelfDragging() && Browser.InspectReading().ShelfExtent == ContentShelfExtent::SplitHalf,
                    "Gentle upward drag on ContentBrowser notch settles cleanly at SplitHalf");

        for (int Step = 0; Step < 90; ++Step)
        {
            StepFrame({ 20.0f, 20.0f }, false, false, false);
        }

        // 2. Drag empty top header strip downward gently from SplitHalf -> Closed
        const float SplitHeight = Browser.QueryVisibleDrawerHeightRatio() * Viewport.Height;
        VerifyCheck(std::fabs(SplitHeight - 720.0f * 0.48f) < 6.0f,
                    "ContentBrowser SplitHalf settles at ~48% viewport height");
        const float   HeaderGrabY = Viewport.Bottom() - SplitHeight + 22.0f;
        const Point2D HeaderGrab { 120.0f, HeaderGrabY };
        StepFrame(HeaderGrab, true, true, false);
        for (int Step = 1; Step <= 24; ++Step)
        {
            StepFrame({ HeaderGrab.X, HeaderGrab.Y + (260.0f * static_cast<float>(Step) / 24.0f) }, true, false, false);
        }
        StepFrame({ HeaderGrab.X, HeaderGrab.Y + 260.0f }, true, false, false, 0.045);
        StepFrame({ HeaderGrab.X, HeaderGrab.Y + 260.0f }, false, false, true);
        VerifyCheck(Browser.InspectReading().ShelfExtent == ContentShelfExtent::Closed,
                    "Pulling empty ContentBrowser top header downward closes drawer cleanly");

        for (int Step = 0; Step < 90; ++Step)
        {
            StepFrame({ 20.0f, 20.0f }, false, false, false);
        }

        // 3. Fast upward flick on bottom notch snaps straight from Closed -> FullHeight
        const Rectangle2D FlickNotch = Browser.QueryNotchTabBounds(Viewport);
        const Point2D     FlickStart { FlickNotch.X + FlickNotch.Width * 0.5f, FlickNotch.Y + FlickNotch.Height * 0.5f };
        StepFrame(FlickStart, true, true, false);
        StepFrame({ FlickStart.X, FlickStart.Y - 48.0f }, true, false, false);
        StepFrame({ FlickStart.X, FlickStart.Y - 125.0f }, true, false, false);
        StepFrame({ FlickStart.X, FlickStart.Y - 125.0f }, false, false, true);
        VerifyCheck(Browser.InspectReading().ShelfExtent == ContentShelfExtent::FullHeight,
                    "Fast upward flick on ContentBrowser notch snaps straight to FullHeight");

        // 4. Short tap on expanded bottom notch toggles FullHeight -> Closed
        for (int Step = 0; Step < 90; ++Step)
        {
            StepFrame({ 20.0f, 20.0f }, false, false, false);
        }
        const Rectangle2D OpenNotch = Browser.QueryNotchTabBounds(Viewport);
        const Point2D     OpenPt { OpenNotch.X + OpenNotch.Width * 0.5f, OpenNotch.Y + OpenNotch.Height * 0.5f };
        StepFrame(OpenPt, true, true, false);
        StepFrame(OpenPt, false, false, true);
        VerifyCheck(Browser.InspectReading().ShelfExtent == ContentShelfExtent::Closed,
                    "Short tap on ContentBrowser notch toggles FullHeight -> Closed");

        ImGui::DestroyContext(Context);
    }

    void RunWorldRendererChecks()
    {
        using namespace Frontier::GeometricRaster;

        // 1. Build Showcase Gallery skipping CPU SDF -> cached local smooth triangles & BVH4 smooth normals ready
        ShowcaseWorldSceneBundle Bundle {};
        BuildShowcaseGalleryScene(Bundle, 160u, 90u, false);
        VerifyCheck(!Bundle.Objects.empty(),
                    "ShowcaseGalleryScene populated opaque objects");
        VerifyCheck(Bundle.Scene.SceneSdf.BrickCountX == 0u,
                    "BuildShowcaseGalleryScene(..., BuildCpuSdf = false) skips CPU SDF construction");
        VerifyCheck(!Bundle.Scene.SceneTree.Primitives.empty() &&
                        Bundle.Scene.SceneTree.PrimitiveNormals.size() == Bundle.Scene.SceneTree.Primitives.size(),
                    "WideBoundingTree populated cached per-triangle smooth vertex normals");

        bool AllOpaqueCached = true;
        for (const ShowcaseWorldAssetEntry& Object : Bundle.Objects)
        {
            if (!Object.GlassShell && Object.LocalSmoothTriangles.empty())
            {
                AllOpaqueCached = false;
                break;
            }
        }
        VerifyCheck(AllOpaqueCached,
                    "Every opaque ShowcaseWorldObjectEntry retains pre-split LocalSmoothTriangles cache");

        // 2. Mutate object position + yaw and rebuild BVH4 without CPU SDF
        const std::size_t FirstTriCount = Bundle.Scene.SceneTree.Primitives.size();
        for (ShowcaseWorldAssetEntry& Object : Bundle.Objects)
        {
            if (!Object.GlassShell)
            {
                Object.CenterWorld.X    += 0.35f;
                Object.RotationZDegrees += 22.5f;
                break;
            }
        }
        RebuildShowcaseSceneAcceleration(Bundle, false);
        VerifyCheck(Bundle.Scene.SceneSdf.BrickCountX == 0u,
                    "RebuildShowcaseSceneAcceleration(Bundle, false) preserves GPU-only SDF fast path");
        VerifyCheck(Bundle.Scene.SceneTree.Primitives.size() == FirstTriCount &&
                        Bundle.Scene.SceneTree.PrimitiveNormals.size() == FirstTriCount,
                    "Incremental transform rebuild preserves full triangle & smooth-normal count");

        // 3. Standalone RTX irradiance lattice bake for decoupled async path
        TieredWorldRenderSequence Tiered;
        Tiered.PrepareRtxIrradianceLatticeOnly(Bundle.Scene);
        VerifyCheck(!Tiered.InspectIrradianceLattice().InspectCells().empty(),
                    "PrepareRtxIrradianceLatticeOnly computes Tier 2 RTX lattice independently");

        // 4. GPU Vulkan World Renderer: GPU SDF build + decoupled RTX lattice upload
        VulkanInterchange Device;
        if (Device.InitializeInterchange(std::filesystem::path(FRONTIER_REPO_ROOT_DIR)))
        {
            VulkanWorldRenderSequence Gpu;
            const bool Ready = Gpu.InitializeSequence(Device, std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR));
            VerifyCheck(Ready && Gpu.QueryGpuSdfComputeReady(),
                        "VulkanWorldRenderSequence loaded SignedDistanceGridCompute.comp.spv");

            if (Ready)
            {
                Gpu.SynchronizeSceneResources(Bundle.Scene, nullptr, true);
                VerifyCheck(Gpu.QueryCachedSdfVoxelCount() > 0u,
                            "GPU SDF compute shader generated dense narrow-band SDF directly from BVH4 without CPU SDF");
                VerifyCheck(Gpu.QueryIrradianceLatticeUploaded() && Gpu.QueryDdgiManagedByGpu(),
                            "GPU DDGI irradiance lattice builds autonomously before async RTX bake arrives");

                Gpu.SynchronizeSceneResources(Bundle.Scene, &Tiered.InspectIrradianceLattice(), false);
                VerifyCheck(Gpu.QueryIrradianceLatticeUploaded() && !Gpu.QueryDdgiManagedByGpu(),
                            "Decoupled RTX Irradiance Lattice uploads cleanly when RebuildSdfGrid = false");

                std::vector<std::uint8_t> PixelsGtx;
                std::vector<std::uint8_t> PixelsRtx;
                const bool RenderedGtx = Gpu.RenderWorldViewportRgba8(
                    WorldRenderTier::Tier1GtxCalibrated,
                    Bundle.EditorCamera,
                    Bundle.Scene,
                    0u,
                    PixelsGtx);
                const bool RenderedRtx = Gpu.RenderWorldViewportRgba8(
                    WorldRenderTier::Tier2RtxDeterministic,
                    Bundle.EditorCamera,
                    Bundle.Scene,
                    0u,
                    PixelsRtx);

                double MeanLumaGtx = 0.0;
                double DiffSum     = 0.0;
                if (RenderedGtx && RenderedRtx && PixelsGtx.size() == PixelsRtx.size() && !PixelsGtx.empty())
                {
                    const std::size_t PixelCount = PixelsGtx.size() / 4u;
                    for (std::size_t P = 0u; P < PixelCount; ++P)
                    {
                        const std::size_t O = P * 4u;
                        MeanLumaGtx += (0.2126 * PixelsGtx[O] + 0.7152 * PixelsGtx[O + 1u] + 0.0722 * PixelsGtx[O + 2u]);
                        DiffSum += std::fabs(static_cast<double>(PixelsRtx[O]) - static_cast<double>(PixelsGtx[O])) +
                                   std::fabs(static_cast<double>(PixelsRtx[O + 1u]) - static_cast<double>(PixelsGtx[O + 1u])) +
                                   std::fabs(static_cast<double>(PixelsRtx[O + 2u]) - static_cast<double>(PixelsGtx[O + 2u]));
                    }
                    MeanLumaGtx /= static_cast<double>(PixelCount);
                    DiffSum     /= static_cast<double>(PixelCount * 3u);
                }
                VerifyCheck(RenderedGtx && MeanLumaGtx > 5.0,
                            "GPU Tier 1 GTX viewport render with GPU-built SDF produced lit image");
                VerifyCheck(RenderedRtx && DiffSum > 0.25,
                            "GPU Tier 2 RTX viewport render reflected decoupled Irradiance Lattice");
                Gpu.TeardownSequence();
            }
            Device.TeardownInterchange();
        }
        else
        {
#if defined(_WIN32)
            std::cout << "[INFO] Vulkan GPU compute unavailable on runner; GPU SDF/RTX checks skipped on _WIN32.\n";
#else
            VerifyCheck(false, "VulkanInterchange failed to initialize on Linux runner");
#endif
        }
    }
}

int main()
{
    RunTopNotchDetentChecks();
    RunBottomContentBrowserDetentChecks();
    RunWorldRendererChecks();

    if (FailureCount > 0)
    {
        std::cerr << "\nDrawerGestureWorldValidation FAILED (" << FailureCount << " checks failed).\n";
        return 1;
    }

    std::cout << "\nDrawerGestureWorldValidation PASSED.\n";
    return 0;
}
