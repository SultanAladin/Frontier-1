/*============================================================================================================================================
                                                      PROJECTBROWSERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation of WorkspaceCapabilityRegister, ProjectGenerator (3-target CMake + SessionOutput), ShowcaseGalleryScene & ProjectBrowser

#include "Engine/Codecs/WorldSpaceCodec.h"
#include "Engine/Configuration/ProjectGenerator.h"
#include "Engine/Configuration/ProjectSpaceRepository.h"
#include "Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.h"
#include "Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Lifecycle/ProjectExecutionRunner.h"
#include "Engine/Workspaces/WorkspaceCapabilityRegister.h"

#if FRONTIER_EDITOR_ENABLED
#include "Engine/Editor/Explorer/ProjectBrowser.h"
#include "Engine/Lifecycle/EditorLifecycle.h"
#endif

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace
{

[[nodiscard]] std::string ReadEntireFileText(const std::filesystem::path& FilePath)
{
    std::ifstream Stream(FilePath, std::ios::binary);
    std::ostringstream Buffer {};
    Buffer << Stream.rdbuf();
    return Buffer.str();
}

} // namespace

int main()
{
    using namespace Frontier;

    // 1. Validate WorkspaceCapabilityRegister
    Workspaces::WorkspaceCapabilityRegister CapReg {};
    const auto AllCaps = CapReg.QueryAllCapabilities();
    if (AllCaps.size() != 9u ||
        AllCaps[0].Identifier != "Frontier" ||
        AllCaps[0].ActionTitle != "Open with Frontier" ||
        AllCaps[1].Identifier != "UVWorkspace" ||
        AllCaps[1].ActionTitle != "Open with UVWorkspace" ||
        AllCaps[2].Identifier != "PatternWorkspace" ||
        AllCaps[2].ActionTitle != "Open with Pattern Studio" ||
        AllCaps[3].Identifier != "StampWorkspace" ||
        AllCaps[3].ActionTitle != "Open with Stamp Studio" ||
        AllCaps[4].Identifier != "BakingWorkspace" ||
        AllCaps[4].ActionTitle != "Open with Baking Studio" ||
        AllCaps[5].Identifier != "FractureWorkspace" ||
        AllCaps[5].ActionTitle != "Open with FractureWorkspace" ||
        AllCaps[6].Identifier != "Sketcher" ||
        AllCaps[6].ActionTitle != "Open with Sketcher" ||
        AllCaps[7].Identifier != "MaterialWorkspace" ||
        AllCaps[7].ActionTitle != "Open with Material Studio" ||
        AllCaps[8].Identifier != "ComponentEditor" ||
        AllCaps[8].ActionTitle != "Open with Component Editor")
    {
        std::fprintf(stderr, "[FAIL] WorkspaceCapabilityRegister expected all 9 Frontier workspace capabilities.\n");
        return 1;
    }

    const auto ProjPermitted      = CapReg.QueryPermittedForPath("Demo.projectspace");
    const auto UvPermitted        = CapReg.QueryPermittedForPath("Housing.UVSpace");
    const auto VecPermitted       = CapReg.QueryPermittedForPath("MotifTile.VectorSpace");
    const auto BakePermitted      = CapReg.QueryPermittedForPath("Statue.stl");
    const auto FracturePermitted  = CapReg.QueryPermittedForPath("Column.FractureSpace");
    const auto SketcherPermitted  = CapReg.QueryPermittedForPath("Bracket.solidarc");
    const auto ComponentPermitted = CapReg.QueryPermittedForPath("Chassis.ComponentSpace");
    if (ProjPermitted.empty() || ProjPermitted.front().Target != Workspaces::HostWorkspaceTarget::FrontierWorld ||
        UvPermitted.empty() || UvPermitted.front().Target != Workspaces::HostWorkspaceTarget::UVWorkspace ||
        VecPermitted.empty() || VecPermitted.front().Target != Workspaces::HostWorkspaceTarget::PatternWorkspace ||
        BakePermitted.empty() || BakePermitted.front().Target != Workspaces::HostWorkspaceTarget::BakingWorkspace ||
        FracturePermitted.empty() || FracturePermitted.front().Target != Workspaces::HostWorkspaceTarget::FractureWorkspace ||
        SketcherPermitted.empty() || SketcherPermitted.front().Target != Workspaces::HostWorkspaceTarget::SketcherWorkspace ||
        ComponentPermitted.empty() || ComponentPermitted.front().Target != Workspaces::HostWorkspaceTarget::ComponentEditorWorkspace)
    {
        std::fprintf(stderr, "[FAIL] WorkspaceCapabilityRegister path priority mismatch.\n");
        return 1;
    }

    // 2. Validate ProjectGenerator scaffolding (SessionOutput/, 3 CMake targets, .projectspace round-trip)
    const std::filesystem::path TempBase = std::filesystem::temp_directory_path() / "FrontierProjectGenValidation";
    std::error_code CleanupEc {};
    std::filesystem::remove_all(TempBase, CleanupEc);

    const std::filesystem::path RepoRoot(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ActiveBinDir = std::filesystem::current_path() / "Binaries" / "Win64";

    Configuration::ProjectScaffoldSpecification Spec {};
    Spec.DisplayName          = "Aero Gallery";
    Spec.DestinationDirectory = TempBase;
    Spec.FrontierRootPath     = RepoRoot;
    Spec.FrontierBinariesPath = ActiveBinDir;
    Spec.InitialTheme         = ThemePreset::Oled;

    const auto Scaffold = Configuration::ProjectGenerator::GenerateProject(Spec);
    if (!Scaffold.Succeeded || Scaffold.PascalIdentifier != "AeroGallery")
    {
        std::fprintf(stderr, "[FAIL] ProjectGenerator failed: %s\n", Scaffold.Diagnostic.c_str());
        return 1;
    }

    if (!std::filesystem::exists(Scaffold.ProjectRootDirectory / "SessionOutput") ||
        std::filesystem::exists(Scaffold.ProjectRootDirectory / "Saved") ||
        std::filesystem::exists(Scaffold.ProjectRootDirectory / "Sdk") ||
        std::filesystem::exists(Scaffold.ProjectRootDirectory / "SDK") ||
        !std::filesystem::exists(Scaffold.ProjectRootDirectory / "Engine" / "UserInterface" / "AeroGalleryViewportOverlay.h") ||
        !std::filesystem::exists(Scaffold.ProjectSpaceFile) ||
        !std::filesystem::exists(Scaffold.CMakeListsFile))
    {
        std::fprintf(stderr, "[FAIL] Scaffolded directory structure violated SessionOutput / zero-SDK / UserInterface contract.\n");
        return 1;
    }

    ProjectSpaceDocument LoadedSpace {};
    std::string          SpaceDiag   {};
    if (!ProjectSpaceRepository::Load(Scaffold.ProjectSpaceFile, LoadedSpace, SpaceDiag) ||
        LoadedSpace.ProjectTitle != "Aero Gallery" ||
        LoadedSpace.Appearance.Theme != ThemePreset::Oled)
    {
        std::fprintf(stderr, "[FAIL] .projectspace round-trip failed: %s\n", SpaceDiag.c_str());
        return 1;
    }

    // New projects start from a blank scene: one Ground Plane placement under a clear sky (no preset gallery)
    Codecs::WorldSpaceDocumentModel BlankWorld {};
    std::string                     BlankWorldDiag {};
    if (!Codecs::WorldSpaceDecoder::DecodeFromFile(Scaffold.MainWorldFile, BlankWorld, BlankWorldDiag) ||
        BlankWorld.GeometryPlacements.size() != 1u ||
        BlankWorld.GeometryPlacements[0].PlacementTitle != "GroundPlane" ||
        !BlankWorld.AreaLuminairePlacements.empty() ||
        !BlankWorld.MeasuredLuminairePlacements.empty())
    {
        std::fprintf(stderr, "[FAIL] Generated MainWorld.WorldSpace is not a blank Ground Plane scene: %s\n", BlankWorldDiag.c_str());
        return 1;
    }

    GeometricRaster::ShowcaseWorldSceneBundle BlankScene {};
    GeometricRaster::BuildBlankPlaneSkyScene(BlankScene, 160u, 90u);
    if (BlankScene.Objects.size() != 1u ||
        BlankScene.Objects[0].Title != "GroundPlane" ||
        !BlankScene.Scene.Atmosphere.Medium.AtmosphereEnabled ||
        !BlankScene.Scene.Atmosphere.Solar.SolarEnabled ||
        BlankScene.Scene.Atmosphere.Clouds.CloudsEnabled ||
        !BlankScene.Scene.AreaLights.empty())
    {
        std::fprintf(stderr, "[FAIL] BuildBlankPlaneSkyScene is not a single Ground Plane under clear sky.\n");
        return 1;
    }

    const std::string CMakeText = ReadEntireFileText(Scaffold.CMakeListsFile);
    if (CMakeText.find("add_executable(AeroGallery ") == std::string::npos ||
        CMakeText.find("add_executable(AeroGalleryEditor ") == std::string::npos ||
        CMakeText.find("add_executable(AeroGalleryShipping ") == std::string::npos ||
        CMakeText.find("Sdk") != std::string::npos ||
        CMakeText.find("SDK") != std::string::npos)
    {
        std::fprintf(stderr, "[FAIL] Generated CMakeLists.txt missing 3 targets or contains forbidden SDK naming.\n");
        return 1;
    }

    // 3. Validate FrontierRunShippingProjectLoop directly in-process
    double TickedTime = 0.0;
    FrontierProjectLaunchDescriptor ShipDesc {};
    ShipDesc.StructureVersion = 1u;
    ShipDesc.ProjectTitle     = "AeroGallery";
    ShipDesc.InitialWidthPx   = 640u;
    ShipDesc.InitialHeightPx  = 360u;
    ShipDesc.BoundedTickCount = 3u;
    ShipDesc.UserPayload          = &TickedTime;
    ShipDesc.NotifySimulationStep = [](double Dt, void* Ptr)
    {
        *static_cast<double*>(Ptr) += Dt;
    };
    if (FrontierRunShippingProjectLoop(&ShipDesc) != 0 || TickedTime <= 0.0)
    {
        std::fprintf(stderr, "[FAIL] FrontierRunShippingProjectLoop failed.\n");
        return 1;
    }

    // 3b. When shared binaries exist in Binaries/Win64 (Editor / Development builds), compile & run all 3 generated targets externally
#if !FRONTIER_PROFILE_SHIPPING && !defined(_WIN32)
    if (std::filesystem::exists(ActiveBinDir / "libFrontierEngine.so") &&
        std::filesystem::exists(ActiveBinDir / "libFrontierWorkspaceUi.so"))
    {
        const std::filesystem::path ExtBuildDir = Scaffold.ProjectRootDirectory / "build-ext";
        const std::string CMakeConfigureCmd =
            "cmake -S \"" + Scaffold.ProjectRootDirectory.string() +
            "\" -B \"" + ExtBuildDir.string() +
            "\" -DFRONTIER_ENGINE_ROOT=\"" + RepoRoot.string() +
            "\" -DFRONTIER_BINARIES_DIR=\"" + ActiveBinDir.string() + "\"";
        const std::string CMakeBuildCmd =
            "cmake --build \"" + ExtBuildDir.string() + "\" --parallel 2";

        if (std::system(CMakeConfigureCmd.c_str()) != 0 || std::system(CMakeBuildCmd.c_str()) != 0)
        {
            std::fprintf(stderr, "[FAIL] External CMake build of generated 3-target project failed.\n");
            return 1;
        }

        const std::filesystem::path OutBin = Scaffold.ProjectRootDirectory / "Binaries" / "Win64";
        for (const char* TargetName : { "AeroGallery", "AeroGalleryEditor", "AeroGalleryShipping" })
        {
            const std::filesystem::path ExePath = OutBin / TargetName;
            if (!std::filesystem::exists(ExePath))
            {
                std::fprintf(stderr, "[FAIL] Generated target binary missing: %s\n", ExePath.string().c_str());
                return 1;
            }
            const std::string RunCmd = "\"" + ExePath.string() + "\" --verify-ticks";
            if (std::system(RunCmd.c_str()) != 0)
            {
                std::fprintf(stderr, "[FAIL] Generated target execution failed: %s\n", TargetName);
                return 1;
            }
        }
    }
#endif

#if FRONTIER_EDITOR_ENABLED
    // 4. Validate ShowcaseGalleryScene + EditorLifecycle live authoring hooks
    {
        Lifecycle::EditorLifecycle Editor {};
        Lifecycle::EditorStartupConfiguration EditorCfg {};
        EditorCfg.FontRepositoryRoot    = RepoRoot / "EngineContent/FontsArchives";
        EditorCfg.PopulateShowcaseScene = true;
        if (!Editor.Initialize(EditorCfg))
        {
            std::fprintf(stderr, "[FAIL] EditorLifecycle initialization failed.\n");
            return 1;
        }

        auto* Panes  = Editor.AccessWorkspaces().AccessActivePanes();
        auto* Bundle = Editor.AccessShowcaseScene();
        if (Panes == nullptr || Bundle == nullptr)
        {
            std::fprintf(stderr, "[FAIL] Active panes or ShowcaseBundle missing.\n");
            return 1;
        }

        const auto ConstructTemplates = Panes->Outliner.QueryConstructTemplates();
        if (ConstructTemplates.size() < 26u)
        {
            std::fprintf(stderr, "[FAIL] Expected >= 26 unified Construct templates, got %zu.\n", ConstructTemplates.size());
            return 1;
        }

        const std::size_t InitialClusterCount = Bundle->Scene.ClusterInstances.size();
        const OutlinerSlotToken SpawnedSphere = GeometricRaster::ConstructShowcasePrimitiveInOutliner(
            Panes->Outliner,
            *Bundle,
            GeometricRaster::ShowcasePrimitiveShape::Sphere,
            { 0.15f, -0.35f, -0.30f });
        if (!SpawnedSphere.Valid() || Bundle->Scene.ClusterInstances.size() <= InitialClusterCount)
        {
            std::fprintf(stderr, "[FAIL] ConstructShowcasePrimitiveInOutliner failed to adopt Sphere into WorldSceneSpecification.\n");
            return 1;
        }

        if (!GeometricRaster::AssignShowcaseEntryBaseColor(
                Panes->Outliner,
                *Bundle,
                SpawnedSphere,
                ColorRgba{ 210u, 72u, 58u, 255u },
                "Vermilion Specimen"))
        {
            std::fprintf(stderr, "[FAIL] AssignShowcaseEntryBaseColor failed.\n");
            return 1;
        }

        const OutlinerSlotToken PickedCenter = GeometricRaster::PickShowcaseGalleryEntryAtUv(Panes->Outliner, *Bundle, 0.50f, 0.54f);
        if (!PickedCenter.Valid())
        {
            std::fprintf(stderr, "[FAIL] PickShowcaseGalleryEntryAtUv failed to intersect central Showcase geometry.\n");
            return 1;
        }

        Editor.FrameShowcaseCameraSelection();
        if (!Editor.ExecuteAuthoringTick(1.0 / 60.0))
        {
            std::fprintf(stderr, "[FAIL] EditorLifecycle::ExecuteAuthoringTick returned false.\n");
            return 1;
        }
        Editor.Finalize();
    }

    // 5. Validate ProjectBrowser collection, Tier 1 GTX thumbnails, Reader mode, & ImGui PixelSpace rendering
    {
        IMGUI_CHECKVERSION();
        ImGuiContext* Ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(Ctx);
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(1600.0f, 920.0f);
        unsigned char* FontPixels = nullptr;
        int FontW = 0, FontH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

        Editor::Explorer::ProjectBrowser Browser {};
        Browser.InitializeDefaultCollection(TempBase, RepoRoot, ActiveBinDir);
        bool HasDefaultProject = false;
        for (const auto& Entry : Browser.QueryCollection())
        {
            if (Entry.Title == "Default Project")
            {
                HasDefaultProject = true;
                break;
            }
        }
        if (!HasDefaultProject ||
            Browser.QueryCollection().empty() ||
            Browser.QueryCollection()[0].ThumbnailW != 264u ||
            Browser.QueryCollection()[0].ThumbnailH != 160u)
        {
            std::fprintf(stderr, "[FAIL] ProjectBrowser failed to populate the Default Project with rendered Tier 1 GTX thumbnails.\n");
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        ImGui::NewFrame();
        Browser.Render({ 0.0f, 0.0f, 1600.0f, 920.0f }, 1.0 / 60.0, 1.0f);
        ImGui::Render();

        Browser.ActivateEntryByIndex(0);
        if (!Browser.QueryReaderActive())
        {
            std::fprintf(stderr, "[FAIL] ProjectBrowser::ActivateEntryByIndex failed to enter Reader view.\n");
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        ImGui::NewFrame();
        Browser.Render({ 0.0f, 0.0f, 1600.0f, 920.0f }, 1.0 / 60.0, 1.0f);
        ImGui::Render();

        Browser.ReleaseGpuTextures();
        ImGui::DestroyContext(Ctx);
    }

    // 7. Validate ContentBrowser close controls (X button over Inspector, top notch over Viewport, 1.25x UI scale, Escape, Ctrl+Space)
    {
        IMGUI_CHECKVERSION();
        ImGuiContext* Ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(Ctx);
        ImGuiIO& Io    = ImGui::GetIO();
        Io.DisplaySize = ImVec2(1920.0f, 1080.0f);
        unsigned char* FontPixels = nullptr;
        int FontW = 0, FontH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH);

        Lifecycle::EditorStartupConfiguration EditorCfg {};
        EditorCfg.HostWidthPx        = 1920u;
        EditorCfg.HostHeightPx       = 1080u;
        EditorCfg.FontRepositoryRoot = RepoRoot / "EngineContent/FontsArchives";

        Lifecycle::EditorLifecycle EditorSession {};
        if (!EditorSession.Initialize(EditorCfg))
        {
            std::fprintf(stderr, "[FAIL] EditorLifecycle failed to initialize for ContentBrowser close validation.\n");
            ImGui::DestroyContext(Ctx);
            return 1;
        }
        EditorSession.PopulateShowcaseGallerySession(160u, 96u);

        auto StepEditorFrame = [&](ImVec2 MousePos, bool ClickDown, bool ClickPrev, double Dt = 1.0 / 60.0)
        {
            Io.MousePos     = MousePos;
            Io.MouseDown[0] = ClickDown;
            ImGui::NewFrame();
            if (ClickDown && !ClickPrev)
            {
                Io.MouseClicked[0] = true;
            }
            EditorSession.PresentPrimaryEditorImGui(Dt);
            ImGui::Render();
        };

        auto& Drawer = EditorSession.AccessContentBrowser().AccessPanel();

        // Settle open at SplitHalf
        Drawer.SetShelfExtent(Components::ContentShelfExtent::SplitHalf, true);
        StepEditorFrame(ImVec2(100.0f, 100.0f), false, false);

        // (a) Click X close button on right side (directly over docked InspectorPanel at X ~ 1879, Y ~ 562)
        const Components::Rectangle2D SplitPhys = Drawer.QueryDrawerPhysicalBounds();
        const ImVec2 CloseBtnCenter(SplitPhys.Right() - 41.0f, SplitPhys.Y + 22.0f);
        StepEditorFrame(CloseBtnCenter, true, false);
        StepEditorFrame(CloseBtnCenter, false, true);
        if (Drawer.QueryTargetShelfExtent() != Components::ContentShelfExtent::Closed)
        {
            std::fprintf(stderr, "[FAIL] Clicking ContentBrowser X button over InspectorPanel failed to close drawer.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        // (b) Re-open to SplitHalf and click top notch over ViewportPanel
        Drawer.SetShelfExtent(Components::ContentShelfExtent::SplitHalf, true);
        StepEditorFrame(ImVec2(100.0f, 100.0f), false, false);
        const Components::Rectangle2D NotchPhys = Drawer.QueryNotchPhysicalBounds();
        const ImVec2 NotchCenter(NotchPhys.X + NotchPhys.Width * 0.5f, NotchPhys.Y + NotchPhys.Height * 0.5f);
        StepEditorFrame(NotchCenter, true, false);
        StepEditorFrame(NotchCenter, false, true);
        if (Drawer.QueryTargetShelfExtent() != Components::ContentShelfExtent::Closed)
        {
            std::fprintf(stderr, "[FAIL] Clicking ContentBrowser top notch over ViewportPanel failed to close drawer.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        // (c) Re-open to SplitHalf at Windows 125% UI Scale (1.25f) and click X close button
        EditorSession.AccessControlCentre().AccessSettings().UiScale = 1.25f;
        Drawer.SetShelfExtent(Components::ContentShelfExtent::SplitHalf, true);
        StepEditorFrame(ImVec2(100.0f, 100.0f), false, false);
        const Components::Rectangle2D ScaledPhys = Drawer.QueryDrawerPhysicalBounds();
        const ImVec2 ScaledCloseCenter(ScaledPhys.Right() - 41.0f * 1.25f, ScaledPhys.Y + 22.0f * 1.25f);
        StepEditorFrame(ScaledCloseCenter, true, false);
        StepEditorFrame(ScaledCloseCenter, false, true);
        if (Drawer.QueryTargetShelfExtent() != Components::ContentShelfExtent::Closed)
        {
            std::fprintf(stderr, "[FAIL] Clicking ContentBrowser X button at 1.25x UI scale failed to close drawer.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }
        EditorSession.AccessControlCentre().AccessSettings().UiScale = 1.0f;

        // (d) Re-open to SplitHalf and press Escape key
        Drawer.SetShelfExtent(Components::ContentShelfExtent::SplitHalf, true);
        StepEditorFrame(ImVec2(100.0f, 100.0f), false, false);
        Io.AddKeyEvent(ImGuiKey_Escape, true);
        StepEditorFrame(ImVec2(960.0f, 700.0f), false, false);
        Io.AddKeyEvent(ImGuiKey_Escape, false);
        StepEditorFrame(ImVec2(960.0f, 700.0f), false, false);
        if (Drawer.QueryTargetShelfExtent() != Components::ContentShelfExtent::Closed)
        {
            std::fprintf(stderr, "[FAIL] Pressing Escape failed to close ContentBrowser drawer.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        // (e) Settle fully closed, then toggle open & closed with Ctrl+Space
        Drawer.SetShelfExtent(Components::ContentShelfExtent::Closed, true);
        StepEditorFrame(ImVec2(500.0f, 300.0f), false, false);
        Io.AddKeyEvent(ImGuiMod_Ctrl, true);
        Io.AddKeyEvent(ImGuiKey_Space, true);
        StepEditorFrame(ImVec2(500.0f, 300.0f), false, false);
        Io.AddKeyEvent(ImGuiKey_Space, false);
        Io.AddKeyEvent(ImGuiMod_Ctrl, false);
        StepEditorFrame(ImVec2(500.0f, 300.0f), false, false);
        if (Drawer.QueryTargetShelfExtent() != Components::ContentShelfExtent::SplitHalf)
        {
            std::fprintf(stderr, "[FAIL] Ctrl+Space failed to open ContentBrowser drawer.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        // (f) Validate Unreal Engine Viewport Camera Controls: RMB + WASD + Q/E + Shift (3.0x Sprint) + Pointer Capture
        EditorSession.AccessControlCentre().AccessSettings().UiScale = 1.0f;
        Drawer.SetShelfExtent(Components::ContentShelfExtent::Closed, true);
        StepEditorFrame(ImVec2(960.0f, 420.0f), false, false);

        auto* LiveBundle = EditorSession.AccessShowcaseScene();
        if (LiveBundle == nullptr)
        {
            std::fprintf(stderr, "[FAIL] Missing ShowcaseScene for Unreal Engine camera validation.\n");
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        auto StepRmbFly = [&](ImVec2 Pos, bool RmbDown, ImGuiKey HeldKey, bool HoldShift, double Dt = 0.050)
        {
            Io.AddMousePosEvent(Pos.x, Pos.y);
            Io.AddMouseButtonEvent(ImGuiMouseButton_Right, RmbDown);
            Io.AddKeyEvent(ImGuiMod_Shift, HoldShift);
            Io.AddKeyEvent(ImGuiKey_LeftShift, HoldShift);
            if (HeldKey != ImGuiKey_None)
            {
                Io.AddKeyEvent(HeldKey, true);
            }
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(Dt);
            ImGui::Render();
            if (HeldKey != ImGuiKey_None)
            {
                Io.AddKeyEvent(HeldKey, false);
            }
            Io.AddKeyEvent(ImGuiMod_Shift, false);
            Io.AddKeyEvent(ImGuiKey_LeftShift, false);
        };

        const Mathematics::Axis3D StartOrigin = LiveBundle->EditorCamera.CameraOrigin;
        const Mathematics::Axis3D InitFwd = Mathematics::NormalizeAxis(
            LiveBundle->EditorCamera.TargetPoint - StartOrigin,
            { 0.0f, 1.0f, 0.0f });

        // RMB + W normal fly forward
        StepRmbFly(ImVec2(960.0f, 420.0f), true, ImGuiKey_W, false, 0.050);
        const Mathematics::Axis3D AfterW = LiveBundle->EditorCamera.CameraOrigin;
        const float NormalStepDist = Mathematics::EvaluateDot(AfterW - StartOrigin, InitFwd);

        // RMB + Shift + W sprint fly forward (must be ~3.0x normal speed and MUST NOT hijack RMB into pan!)
        StepRmbFly(ImVec2(960.0f, 420.0f), true, ImGuiKey_W, true, 0.050);
        const Mathematics::Axis3D AfterShiftW = LiveBundle->EditorCamera.CameraOrigin;
        const float SprintStepDist = Mathematics::EvaluateDot(AfterShiftW - AfterW, InitFwd);

        // RMB + E vertical ascend (+Z) and RMB + Q vertical descend (-Z)
        const float ZBeforeE = LiveBundle->EditorCamera.CameraOrigin.Z;
        StepRmbFly(ImVec2(960.0f, 420.0f), true, ImGuiKey_E, false, 0.050);
        const float ZAfterE = LiveBundle->EditorCamera.CameraOrigin.Z;
        StepRmbFly(ImVec2(960.0f, 420.0f), true, ImGuiKey_Q, true, 0.050);
        const float ZAfterShiftQ = LiveBundle->EditorCamera.CameraOrigin.Z;

        // RMB + A / D horizontal strafe + continuous drag capture outside Viewport bounds (X = 40 inside Outliner)
        const Mathematics::Axis3D BeforeStrafe = LiveBundle->EditorCamera.CameraOrigin;
        StepRmbFly(ImVec2(40.0f, 420.0f), true, ImGuiKey_D, false, 0.050);
        const Mathematics::Axis3D AfterOutBoundsStrafe = LiveBundle->EditorCamera.CameraOrigin;
        const float StrafeDistSq = Mathematics::EvaluateDot(
            AfterOutBoundsStrafe - BeforeStrafe,
            AfterOutBoundsStrafe - BeforeStrafe);

        // Release RMB cleanly
        StepRmbFly(ImVec2(960.0f, 420.0f), false, ImGuiKey_None, false, 1.0 / 60.0);

        // Wheel must adjust Fly Speed (never Dolly-Zoom), with and without RMB held
        auto StepWheel = [&](bool RmbDown, float WheelNotches)
        {
            Io.AddMousePosEvent(960.0f, 420.0f);
            Io.AddMouseButtonEvent(ImGuiMouseButton_Right, RmbDown);
            Io.AddMouseWheelEvent(0.0f, WheelNotches);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            Io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        };

        const float               SpeedBeforeWheel   = EditorSession.QueryCameraFlySpeedScalar();
        const Mathematics::Axis3D OriginBeforeWheel  = LiveBundle->EditorCamera.CameraOrigin;
        const Mathematics::Axis3D TargetBeforeWheel  = LiveBundle->EditorCamera.TargetPoint;
        StepWheel(false, 2.0f);
        const float               SpeedAfterWheelUp  = EditorSession.QueryCameraFlySpeedScalar();
        StepWheel(true, -1.0f);
        const float               SpeedAfterRmbWheel = EditorSession.QueryCameraFlySpeedScalar();
        const Mathematics::Axis3D OriginAfterWheel   = LiveBundle->EditorCamera.CameraOrigin;
        const Mathematics::Axis3D TargetAfterWheel   = LiveBundle->EditorCamera.TargetPoint;
        const float WheelOriginDriftSq = Mathematics::EvaluateDot(
            OriginAfterWheel - OriginBeforeWheel,
            OriginAfterWheel - OriginBeforeWheel);
        const float WheelTargetDriftSq = Mathematics::EvaluateDot(
            TargetAfterWheel - TargetBeforeWheel,
            TargetAfterWheel - TargetBeforeWheel);

        // First-frame steps are damped by the editor camera lag component; sprint ratio must still hold.
        if (NormalStepDist < 0.08f ||
            SprintStepDist < NormalStepDist * 2.5f ||
            (ZAfterE - ZBeforeE) < 0.08f ||
            (ZAfterE - ZAfterShiftQ) < (ZAfterE - ZBeforeE) * 2.5f ||
            StrafeDistSq < 0.01f ||
            SpeedAfterWheelUp < SpeedBeforeWheel * 1.35f ||
            SpeedAfterRmbWheel > SpeedAfterWheelUp * 0.90f ||
            SpeedAfterRmbWheel < SpeedBeforeWheel * 1.05f ||
            WheelOriginDriftSq > 1.0e-6f ||
            WheelTargetDriftSq > 1.0e-6f)
        {
            std::fprintf(
                stderr,
                "[FAIL] Unreal Engine camera controls validation failed (NormalW=%.3f, SprintW=%.3f, dZE=%.3f, dZShiftQ=%.3f, OutBoundsStrafeSq=%.4f, SpeedBefore=%.3f, SpeedWheelUp=%.3f, SpeedRmbWheel=%.3f, WheelDriftSq=%.6f/%.6f).\n",
                NormalStepDist,
                SprintStepDist,
                ZAfterE - ZBeforeE,
                ZAfterE - ZAfterShiftQ,
                StrafeDistSq,
                SpeedBeforeWheel,
                SpeedAfterWheelUp,
                SpeedAfterRmbWheel,
                WheelOriginDriftSq,
                WheelTargetDriftSq);
            EditorSession.Finalize();
            ImGui::DestroyContext(Ctx);
            return 1;
        }

        // (g) Single camera model: an LMB drag must NEVER move the camera (LMB = gizmo & selection only),
        //     and the CPU gizmo ray hover must resolve handles without any GPU pick patch.
        {
            const Mathematics::Axis3D OriginBeforeLmb = LiveBundle->EditorCamera.CameraOrigin;
            const Mathematics::Axis3D TargetBeforeLmb = LiveBundle->EditorCamera.TargetPoint;
            Io.AddMousePosEvent(960.0f, 420.0f);
            Io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            Io.AddMousePosEvent(1060.0f, 360.0f);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            Io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            const Mathematics::Axis3D LmbDrift = LiveBundle->EditorCamera.CameraOrigin - OriginBeforeLmb;
            const Mathematics::Axis3D LmbTargetDrift = LiveBundle->EditorCamera.TargetPoint - TargetBeforeLmb;
            if (Mathematics::EvaluateDot(LmbDrift, LmbDrift) > 1.0e-6f ||
                Mathematics::EvaluateDot(LmbTargetDrift, LmbTargetDrift) > 1.0e-6f)
            {
                std::fprintf(stderr, "[FAIL] LMB drag moved the editor camera; LMB must stay gizmo/selection only.\n");
                EditorSession.Finalize();
                ImGui::DestroyContext(Ctx);
                return 1;
            }

            Editor::TransformGizmo HoverProbe {};
            HoverProbe.ConfigureAnchor({ 0.0f, 0.0f, 0.0f }, 1.0f);
            const Editor::ViewportRay3D ConeRay { { -3.0f, 0.2f, 0.1f }, Mathematics::NormalizeAxis({ 3.95f, -0.2f, -0.1f }, { 1.0f, 0.0f, 0.0f }) };
            const Editor::ViewportRay3D MissRay { { -3.0f, 3.0f, 3.0f }, { 1.0f, 0.0f, 0.0f } };
            if (HoverProbe.EvaluateRayHover(ConeRay) != Editor::GizmoHandleOrdinal::TranslateX ||
                HoverProbe.EvaluateRayHover(MissRay) != Editor::GizmoHandleOrdinal::None)
            {
                std::fprintf(stderr, "[FAIL] CPU gizmo ray hover failed to resolve the +X translate handle.\n");
                EditorSession.Finalize();
                ImGui::DestroyContext(Ctx);
                return 1;
            }
        }

        // (h) Function keys: F3 toggles the telemetry overlay, F1 cycles debug views, F focuses.
        {
            auto* Panes = EditorSession.AccessWorkspaces().AccessActivePanes();
            if (Panes == nullptr)
            {
                std::fprintf(stderr, "[FAIL] No active panes for function-key validation.\n");
                EditorSession.Finalize();
                ImGui::DestroyContext(Ctx);
                return 1;
            }
            const bool DiagBefore = Panes->Viewport.QueryDiagnosticsVisible();
            Io.AddKeyEvent(ImGuiKey_F3, true);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            Io.AddKeyEvent(ImGuiKey_F3, false);
            const bool DiagAfterF3 = Panes->Viewport.QueryDiagnosticsVisible();

            const uint32_t DebugBefore = Panes->Viewport.QueryDisplaySettings().DebugChannelIndex;
            Io.AddKeyEvent(ImGuiKey_F1, true);
            ImGui::NewFrame();
            EditorSession.PresentPrimaryEditorImGui(1.0 / 60.0);
            ImGui::Render();
            Io.AddKeyEvent(ImGuiKey_F1, false);
            const uint32_t DebugAfterF1 = Panes->Viewport.QueryDisplaySettings().DebugChannelIndex;

            if (DiagAfterF3 == DiagBefore || DebugAfterF1 != (DebugBefore + 1u) % 19u)
            {
                std::fprintf(
                    stderr,
                    "[FAIL] Function-key shortcuts failed (F3 diag %d->%d, F1 debug %u->%u).\n",
                    DiagBefore ? 1 : 0, DiagAfterF3 ? 1 : 0, DebugBefore, DebugAfterF1);
                EditorSession.Finalize();
                ImGui::DestroyContext(Ctx);
                return 1;
            }
            Panes->Viewport.ConfigureDiagnosticsVisible(DiagBefore);
        }

        EditorSession.Finalize();
        ImGui::DestroyContext(Ctx);
    }
#endif

    // 8. Validate VulkanWorldRenderSequence GPU compute shader (WorldViewportRadiometricCompute.comp.spv) across Tier 1 GTX, Tier 2 RTX & Debug Views
    {
        std::string VkDiag {};
        GeometricRaster::VulkanInterchange VkDevice {};
        if (!VkDevice.InitializeInterchange(RepoRoot, &VkDiag))
        {
#if defined(_WIN32)
            std::printf("[SKIP] No Vulkan ICD available on headless Windows runner (%s); skipping GPU compute section.\n", VkDiag.c_str());
#else
            std::fprintf(stderr, "[FAIL] VulkanInterchange failed to initialize: %s\n", VkDiag.c_str());
            return 1;
#endif
        }
        else
        {
            GeometricRaster::VulkanWorldRenderSequence GpuWorldSeq {};
            const std::filesystem::path CompiledShaderDir(FRONTIER_COMPILED_SHADER_DIR);
            if (!GpuWorldSeq.InitializeSequence(VkDevice, CompiledShaderDir))
            {
                std::fprintf(stderr, "[FAIL] VulkanWorldRenderSequence failed to load WorldViewportRadiometricCompute.comp.spv.\n");
                VkDevice.TeardownInterchange();
                return 1;
            }

            GeometricRaster::ShowcaseWorldSceneBundle GpuGallery {};
            GeometricRaster::BuildVehicleClearSkyDefaultScene(GpuGallery, 160u, 96u);
            GpuWorldSeq.SynchronizeSceneResources(GpuGallery.Scene, true);

            std::vector<uint8_t> RgbaTier1 {};
            if (!GpuWorldSeq.RenderWorldViewportRgba8(
                    GeometricRaster::WorldRenderTier::Tier1GtxCalibrated,
                    GpuGallery.EditorCamera,
                    GpuGallery.Scene,
                    0u,
                    RgbaTier1) ||
                RgbaTier1.size() != 160u * 96u * 4u)
            {
                std::fprintf(stderr, "[FAIL] VulkanWorldRenderSequence Tier 1 dispatch failed.\n");
                GpuWorldSeq.TeardownSequence();
                VkDevice.TeardownInterchange();
                return 1;
            }

            // Verify the default scene renders lit: upper-sky pixel is blue-dominant and the image is non-black overall
            const std::size_t SkyIdx = (12u * 160u + 80u) * 4u;
            uint64_t LumaTotal = 0u;
            for (std::size_t Pix = 0u; Pix + 3u < RgbaTier1.size(); Pix += 4u)
            {
                LumaTotal += static_cast<uint64_t>(RgbaTier1[Pix]) + RgbaTier1[Pix + 1u] + RgbaTier1[Pix + 2u];
            }
            const uint64_t MeanLuma = LumaTotal / (160ull * 96ull * 3ull);
            if (RgbaTier1[SkyIdx + 2u] <= RgbaTier1[SkyIdx + 0u] || MeanLuma < 8u)
            {
                std::fprintf(
                    stderr,
                    "[FAIL] VulkanWorldRenderSequence sky/luminance check failed (Sky RGB=%u,%u,%u MeanLuma=%llu).\n",
                    RgbaTier1[SkyIdx + 0u], RgbaTier1[SkyIdx + 1u], RgbaTier1[SkyIdx + 2u],
                    static_cast<unsigned long long>(MeanLuma));
                GpuWorldSeq.TeardownSequence();
                VkDevice.TeardownInterchange();
                return 1;
            }

            // Verify Tier 2 + dynamic resolution resize (200x120) + Normal debug channel (channel 3)
            GpuGallery.EditorCamera.ViewportWidthPx  = 200u;
            GpuGallery.EditorCamera.ViewportHeightPx = 120u;
            std::vector<uint8_t> RgbaTier2 {};
            std::vector<uint8_t> RgbaNormal {};
            if (!GpuWorldSeq.RenderWorldViewportRgba8(
                    GeometricRaster::WorldRenderTier::Tier2RtxDeterministic,
                    GpuGallery.EditorCamera,
                    GpuGallery.Scene,
                    0u,
                    RgbaTier2) ||
                RgbaTier2.size() != 200u * 120u * 4u ||
                !GpuWorldSeq.RenderWorldViewportRgba8(
                    GeometricRaster::WorldRenderTier::Tier2RtxDeterministic,
                    GpuGallery.EditorCamera,
                    GpuGallery.Scene,
                    3u,
                    RgbaNormal) ||
                RgbaNormal.size() != 200u * 120u * 4u)
            {
                std::fprintf(stderr, "[FAIL] VulkanWorldRenderSequence Tier 2 or Normal debug dispatch failed.\n");
                GpuWorldSeq.TeardownSequence();
                VkDevice.TeardownInterchange();
                return 1;
            }

            // Progressive PT must render continuously until it converges at the target spp:
            // batch 4/frame, target 16 -> exactly 4 dispatches, then reset restarts at 0.
            {
                GeometricRaster::WorldRenderConfiguration PtConfig {};
                PtConfig.Engine           = GeometricRaster::WorldRenderEngine::PathTracerSceneFinalRender;
                PtConfig.PathTraceSamples = 4u;
                PtConfig.PathTraceTargetSpp = 16u;
                GpuWorldSeq.ResetProgressiveAccumulation();
                std::vector<uint8_t> RgbaPt {};
                uint32_t Dispatches = 0u;
                while (GpuWorldSeq.QueryProgressiveSampleCount() < 16u && Dispatches < 16u)
                {
                    if (!GpuWorldSeq.RenderWorldViewportWithEngine(PtConfig, Dispatches, GpuGallery.EditorCamera, GpuGallery.Scene, RgbaPt))
                    {
                        std::fprintf(stderr, "[FAIL] Progressive PT dispatch failed.\n");
                        GpuWorldSeq.TeardownSequence();
                        VkDevice.TeardownInterchange();
                        return 1;
                    }
                    ++Dispatches;
                }
                uint64_t PtLuma = 0u;
                for (std::size_t Pix = 0u; Pix + 3u < RgbaPt.size(); Pix += 4u)
                {
                    PtLuma += static_cast<uint64_t>(RgbaPt[Pix]) + RgbaPt[Pix + 1u] + RgbaPt[Pix + 2u];
                }
                if (Dispatches != 4u || GpuWorldSeq.QueryProgressiveSampleCount() != 16u ||
                    RgbaPt.size() != 200u * 120u * 4u || PtLuma == 0u)
                {
                    std::fprintf(
                        stderr,
                        "[FAIL] Progressive PT convergence check failed (Dispatches=%u Spp=%u Bytes=%zu Luma=%llu).\n",
                        Dispatches,
                        GpuWorldSeq.QueryProgressiveSampleCount(),
                        RgbaPt.size(),
                        static_cast<unsigned long long>(PtLuma));
                    GpuWorldSeq.TeardownSequence();
                    VkDevice.TeardownInterchange();
                    return 1;
                }
                GpuWorldSeq.ResetProgressiveAccumulation();
                if (GpuWorldSeq.QueryProgressiveSampleCount() != 0u)
                {
                    std::fprintf(stderr, "[FAIL] Progressive PT reset did not restart accumulation.\n");
                    GpuWorldSeq.TeardownSequence();
                    VkDevice.TeardownInterchange();
                    return 1;
                }
            }

            GpuWorldSeq.TeardownSequence();
            VkDevice.TeardownInterchange();
        }
    }

    std::filesystem::remove_all(TempBase, CleanupEc);
    std::printf("[PASS] ProjectBrowserValidation verified WorkspaceCapabilityRegister, ProjectGenerator, ContentBrowser close & VulkanWorldRenderSequence GPU compute.\n");
    return 0;
}
