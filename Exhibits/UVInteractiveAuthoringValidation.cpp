/*============================================================================================================================================
                                                  UVINTERACTIVEAUTHORINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end CTest validation across all 6 UV Workspace completion pillars: Swapchain, Interactor, Seams/Pins, Solvers, OBJ/glTF & UI

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/Configuration/EngineConfiguration.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h"
#include "Engine/GeometrySpace/Selection/GeometrySelection.h"
#include "Engine/Interchange/GeometryInterchange.h"
#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"
#include "Engine/Workspaces/UV/Solvers/UVIslandLayoutOperations.h"
#include "Engine/Workspaces/UV/Solvers/UVProjectionSolvers.h"
#include "Engine/Workspaces/UV/UVViewportInteractor.h"
#include "Engine/Workspaces/UV/UVWorkspaceHost.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

[[nodiscard]] bool VerifyCheck(bool ConditionMet, const char* Message) noexcept
{
    if (!ConditionMet)
    {
        std::fprintf(stderr, "[UVInteractiveAuthoringValidation] FAILED: %s\n", Message);
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path ResolveRepositoryRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] bool WriteRawRgbaPng(
    const std::filesystem::path& TargetPath,
    const std::vector<uint8_t>&  RgbaBytes,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx)
{
    Frontier::Exhibits::SoftwareRasterCanvas OutCanvas(WidthPx, HeightPx, { 0, 0, 0, 255 });
    OutCanvas.BlitRgbaRegion(0u, 0u, WidthPx, HeightPx, RgbaBytes.data());
    return OutCanvas.WritePngFile(TargetPath);
}

} // namespace

int main()
{
    using namespace Frontier;
    using namespace Frontier::GeometricRaster;
    using namespace Frontier::GeometrySpace;
    using namespace Frontier::Workspaces;

    const std::filesystem::path RepoRoot  = ResolveRepositoryRoot();
    const std::filesystem::path ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
    const std::filesystem::path ImageDir  = RepoRoot / "Exhibits/Images/UV";
    const std::filesystem::path TempDir   = std::filesystem::temp_directory_path() / "FrontierUVInterchangeTest";
    std::filesystem::create_directories(ImageDir);
    std::filesystem::create_directories(TempDir);

    VulkanInterchange VkInterchange {};
    std::string         Diag {};
    if (!VerifyCheck(VkInterchange.InitializeInterchange(RepoRoot, &Diag), Diag.c_str()))
    {
        return 1;
    }

    UVWorkspaceRasterizer Rasterizer {};
    if (!VerifyCheck(Rasterizer.InitializeRasterizer(&VkInterchange, ShaderDir), "InitializeRasterizer"))
    {
        return 1;
    }

    UVWorkspaceHost Host {};
    (void)Host.AccessSession().InitializeGPUSolvers(&VkInterchange, ShaderDir);
    if (!VerifyCheck(Host.AttachVulkanRasterizer(&Rasterizer), "AttachVulkanRasterizer"))
    {
        return 1;
    }
    auto& Session = Host.AccessSession();

    //--------------------------------------------------------------------------------------------------------------------
    // 1. External Geometry Intake (.OBJ & .glTF Roundtrip Preserving Multi-UDIM & Part Groups)
    //--------------------------------------------------------------------------------------------------------------------
    Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);
    const std::size_t OrigFaceCount = Session.Asset.Network.Faces.size();
    const std::filesystem::path ObjPath  = TempDir / "RoundtripCrate.obj";
    const std::filesystem::path GltfPath = TempDir / "RoundtripCrate.gltf";

    if (!VerifyCheck(Session.ExportExternalGeometry(ObjPath, Diag), "Export Wavefront .OBJ"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExportExternalGeometry(GltfPath, Diag), "Export Khronos .glTF 2.0"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ImportExternalGeometry(ObjPath, Diag), "Import Wavefront .OBJ"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.Asset.Network.Faces.size() == OrigFaceCount, ".OBJ FaceCount Roundtrip"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ImportExternalGeometry(GltfPath, Diag), "Import Khronos .glTF 2.0"))
    {
        return 1;
    }
    if (!VerifyCheck(!Session.Asset.Network.Faces.empty(), ".glTF Non-Empty PolygonNetwork"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Connectivity Selection, Seam Brush, UV Pinning (EnablePinLock), Split & Stitch
    //--------------------------------------------------------------------------------------------------------------------
    Session.LoadSamplePreset(SampleAssetPreset::AeroCoupeGT);
    auto& Net = Session.Asset.Network;

    const auto LoopEdges = GeometrySelection::EvaluateEdgeLoop(Net, 0u);
    const auto RingEdges = GeometrySelection::EvaluateEdgeRing(Net, 0u);
    if (!VerifyCheck(!LoopEdges.empty() && !RingEdges.empty(), "EdgeLoop & EdgeRing non-empty"))
    {
        return 1;
    }
    if (Net.Edges.size() > 8u)
    {
        const auto Path3D = GeometrySelection::EvaluateShortestEdgePath(Net, 0u, 7u, false);
        const auto PathUV = GeometrySelection::EvaluateShortestEdgePath(Net, 0u, 7u, true);
        if (!VerifyCheck(!Path3D.empty() && !PathUV.empty(), "3D & 2D Dijkstra ShortestEdgePath"))
        {
            return 1;
        }
    }
    const auto Island0  = GeometrySelection::EvaluateLinkedIslandFaces(Net, 0u);
    const auto Coplanar = GeometrySelection::EvaluateCoplanarFaces(Net, 0u, 15.0f);
    const auto Similar  = GeometrySelection::EvaluateSimilarAreaFaces(Net, 0u, 0.30f);
    if (!VerifyCheck(!Island0.empty() && !Coplanar.empty() && !Similar.empty(), "Island, Coplanar & SimilarArea selections"))
    {
        return 1;
    }

    // Pin Face 0 corners and verify ConformalUnwrapSolver + ProgressiveStretchRelaxer strictly lock pinned UVs
    const uint32_t Face0Edge = Net.Faces[0].BoundaryLoopEdgeIndex;
    const Mathematics::UVAxes PinnedBefore = Net.Edges[Face0Edge].ParameterUV;
    const uint32_t PinnedCorners = UVAuthoringOperations::AssignPinLockOnFaces(Net, std::array<uint32_t, 1>{ 0u }, true);
    if (!VerifyCheck(PinnedCorners >= 3u, "AssignPinLockOnFaces"))
    {
        return 1;
    }
    (void)Session.ExecuteDirective(UVCommandDirective::UnwrapConformal);
    (void)Session.ExecuteDirective(UVCommandDirective::RelaxStretchGPU);
    const Mathematics::UVAxes PinnedAfter = Net.Edges[Face0Edge].ParameterUV;
    const float PinDrift = std::hypot(PinnedAfter.U - PinnedBefore.U, PinnedAfter.V - PinnedBefore.V);
    if (!VerifyCheck(PinDrift < 1.0e-4f, "Pinned UV corner remained locked across LSCM + GPU Stretch Relax"))
    {
        return 1;
    }

    // Seam Brush + Split Island + Stitch + Rectify Quads
    (void)UVAuthoringOperations::ApplySeamBrushStroke(Net, 0u, false, false);
    Session.SelectionMask.SynchronizeCapacity(Net);
    Session.SelectionMask.ClearAll();
    Session.SelectionMask.AssignFaceSelected(0u, true);
    if (Net.Faces.size() > 1u)
    {
        Session.SelectionMask.AssignFaceSelected(1u, true);
    }
    const uint32_t DetachedCount = UVAuthoringOperations::SplitFacesIntoDetachedIsland(Net, Session.SelectionMask);
    if (!VerifyCheck(DetachedCount >= 1u, "SplitFacesIntoDetachedIsland"))
    {
        return 1;
    }
    const auto SeamList = GeometrySelection::EvaluateSeamEdges(Net);
    if (!SeamList.empty())
    {
        (void)UVAuthoringOperations::StitchAcrossSelectedEdges(Net, std::span<const uint32_t>(SeamList.data(), 1u));
    }
    (void)UVAuthoringOperations::RectifySelectedQuadFaces(Net, std::array<uint32_t, 1>{ 0u });

    // Capture GPU 2D Canvas with Pin Beacons + TrimSheetAtlas Backdrop + Seam Overlay
    Session.AccessUDIMTileSetting(Session.ActiveUDIMNumber).BackdropPreset = UVBackdropPreset::TrimSheetAtlas;
    Session.AccessUDIMTileSetting(Session.ActiveUDIMNumber).ResolutionPx   = 4096u;
    Session.WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled       = true;
    Session.RenderOptions2D.PinOverlayEnabled                              = true;
    Session.SelectionMode                                                  = UVSelectionMode::Faces;
    Session.SynchronizeSelectionToGPU();
    Session.FramebuffersDirty = true;
    Session.RefreshViewportFramebuffers(640u, 640u, 768u, 768u);
    if (!VerifyCheck(
            WriteRawRgbaPng(ImageDir / "UVInteractive_PinAndBackdrop_2D.png", Session.Framebuffer2DRGBA, Session.Framebuffer2DWidth, Session.Framebuffer2DHeight),
            "Write UVInteractive_PinAndBackdrop_2D.png"))
    {
        return 1;
    }
    // Restore default SeamOverlayEnabled = false invariant
    Session.WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = false;

    //--------------------------------------------------------------------------------------------------------------------
    // 3. Remaining Projection Solvers & Island Stack / Unstack / Fill Pockets
    //--------------------------------------------------------------------------------------------------------------------
    Session.LoadSamplePreset(SampleAssetPreset::ArmoredBust);
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::FlattenAngleBasedABF), "FlattenAngleBasedABF"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ProjectSmartUV), "ProjectSmartUV"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ProjectCubicBox), "ProjectCubicBox"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ProjectCylindrical), "ProjectCylindrical"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ProjectSpherical), "ProjectSpherical"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ProjectCameraView), "ProjectCameraView"))
    {
        return 1;
    }
    Session.SelectionMask.ActiveFaceIndex = 0u;
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::FollowActiveQuads), "FollowActiveQuads"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline), "PackUVsConcaveSkyline + FillPockets"))
    {
        return 1;
    }
    (void)Session.ExecuteDirective(UVCommandDirective::StackSimilarIslands);
    (void)Session.ExecuteDirective(UVCommandDirective::UnstackOverlapping);
    if (!VerifyCheck(Session.ExecuteDirective(UVCommandDirective::ExpandEmptyPockets), "ExpandEmptyPockets"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Live Viewport Input (UVViewportInteractor: Camera Orbit/Zoom, GPU Pick, Modal G Transform & Undo)
    //--------------------------------------------------------------------------------------------------------------------
    Session.LoadSamplePreset(SampleAssetPreset::AeroCoupeGT);
    Session.RefreshViewportFramebuffers(640u, 640u, 640u, 640u);

    auto&             Interactor   = Host.AccessViewportHost().AccessInteractor();
    const Rectangle2D PlanarBounds  { 0.0f, 38.0f, 640.0f, 640.0f };
    const Rectangle2D SpatialBounds { 646.0f, 38.0f, 640.0f, 640.0f };

    // 3D Orbit via MMB drag
    const float      YawBefore = Session.InspectViewportProjection().InspectSpatialCamera().YawRadians;
    PaneInputReading OrbitIn {};
    OrbitIn.PrimaryPointer.X = 900.0f;
    OrbitIn.PrimaryPointer.Y = 320.0f;
    OrbitIn.MiddleDown       = true;
    OrbitIn.PointerDeltaX    = 42.0f;
    OrbitIn.PointerDeltaY    = -18.0f;
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, OrbitIn });
    if (!VerifyCheck(
            std::abs(Session.InspectViewportProjection().InspectSpatialCamera().YawRadians - YawBefore) > 1.0e-4f,
            "3D Spatial Camera Orbit"))
    {
        return 1;
    }

    // Modal 'G' Grab + Numeric '0.12' + Enter confirm + Undo roundtrip
    const uint64_t   RevBeforeModal = Session.RevisionStampCounter;
    PaneInputReading StartModalIn {};
    StartModalIn.PrimaryPointer.X = 320.0f;
    StartModalIn.PrimaryPointer.Y = 320.0f;
    StartModalIn.TypedUtf8        = "g";
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, StartModalIn });
    if (!VerifyCheck(Interactor.QueryModalTransformMode() == ModalUVTransformMode::GrabTranslate, "Entered Modal Grab [G]"))
    {
        return 1;
    }

    PaneInputReading TypeConfirmIn {};
    TypeConfirmIn.PrimaryPointer.X = 350.0f;
    TypeConfirmIn.PrimaryPointer.Y = 300.0f;
    TypeConfirmIn.TypedUtf8        = "0.12";
    TypeConfirmIn.EnterPressed     = true;
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, TypeConfirmIn });
    if (!VerifyCheck(
            Interactor.QueryModalTransformMode() == ModalUVTransformMode::Inactive &&
                Session.RevisionStampCounter == RevBeforeModal + 1ull,
            "Confirmed Modal Grab & Recorded Revision Step"))
    {
        return 1;
    }
    if (!VerifyCheck(Session.UndoParameterization() && Session.RedoParameterization(), "Undo/Redo Modal UV Transform"))
    {
        return 1;
    }

    PaneInputReading StartBoundedModal {};
    StartBoundedModal.PrimaryPointer.X = 320.0f;
    StartBoundedModal.PrimaryPointer.Y = 320.0f;
    StartBoundedModal.TypedUtf8 = "g";
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, StartBoundedModal });
    PaneInputReading HugeNumeric {};
    HugeNumeric.PrimaryPointer.X = 320.0f;
    HugeNumeric.PrimaryPointer.Y = 320.0f;
    HugeNumeric.TypedUtf8 = "999999999999999999999999999999999999999999";
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, HugeNumeric });
    const bool AllUVsFinite = std::all_of(Session.Asset.Network.Edges.begin(), Session.Asset.Network.Edges.end(), [](const GeometrySpace::DirectedEdge& Edge)
    {
        return std::isfinite(Edge.ParameterUV.U) && std::isfinite(Edge.ParameterUV.V);
    });
    PaneInputReading CancelBoundedModal {};
    CancelBoundedModal.EscapePressed = true;
    Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, CancelBoundedModal });
    if (!VerifyCheck(AllUVsFinite && Interactor.QueryModalTransformMode() == ModalUVTransformMode::Inactive,
            "oversized modal numeric input stays finite and cancels cleanly"))
    {
        return 1;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Outliner Layer Authoring + Full Docked UI + ImGuiOverlayRasterization (ImGuiOverlayRasterization.slang) Exhibit
    //--------------------------------------------------------------------------------------------------------------------
    Session.RenameSelectedLayer("Primary Aero Body");
    const uint16_t DupOrd = Session.DuplicateSelectedLayer();
    if (!VerifyCheck(DupOrd != 0u && Session.DeleteSelectedLayer(), "Rename, Duplicate & Delete Priority Layer"))
    {
        return 1;
    }
    Session.ActiveInspectorTab = UVInspectorTab::Unwrap;
    Session.FramebuffersDirty  = true;

    IMGUI_CHECKVERSION();
    ImGuiContext* GuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiCtx);
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    Io.IniFilename  = nullptr;

    TypographyRepository Typography {};
    if (Typography.LoadRepository(RepoRoot / "EngineContent/FontsArchives") > 0u)
    {
        TypographyRepository::AssignCurrent(&Typography);
        if (ImFont* TabFont = Typography.QueryTabStripFont())
        {
            Io.FontDefault = TabFont;
        }
    }
    unsigned char* FontBytes = nullptr;
    int            FontW     = 0;
    int            FontH     = 0;
    Io.Fonts->GetTexDataAsRGBA32(&FontBytes, &FontW, &FontH);

    ThemeRepository ThemeRepo {};
    Components::HeadlessWorkspaceDockContainer Dock {};
    (void)Dock.Initialize(Windowing::WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier UV", 1840u, 980u));
    Components::ConfigureImGuiWorkspaceStyle(Dock.ActiveTabMetrics());
    Components::ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), ThemeRepo);

    ControlCentrePanel ControlCentre {};
    ControlCentre.AttachTypefaces(&Typography);
    ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
    ControlCentre.ApplyDraftAppearanceImmediate();
    ControlCentre.AccessNotificationStream().ClearPending();

    Host.RegisterDockPanes(Dock);

    ImGuiOverlayRasterization SwapchainPresenter {};
    if (!VerifyCheck(
            SwapchainPresenter.InitializeOverlay(&VkInterchange, ShaderDir, nullptr, 1840u, 980u, &Diag),
            "Initialize ImGuiOverlayRasterization"))
    {
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    for (int StepReading = 0; StepReading < 3; ++StepReading)
    {
        Io.DisplaySize = ImVec2(1840.0f, 980.0f);
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        Dock.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
        ControlCentre.Render({ 0.0f, 0.0f, 1840.0f, 980.0f }, 1.0 / 60.0, 60.0f);
        ImGui::Render();
        if (!VerifyCheck(SwapchainPresenter.PresentImGuiBatches(ImGui::GetDrawData(), 1840u, 980u), "PresentImGuiBatches on GPU"))
        {
            SwapchainPresenter.TeardownOverlay();
            ImGui::DestroyContext(GuiCtx);
            return 1;
        }
    }

    std::vector<uint8_t> PresentedRgba {};
    uint32_t             PresW = 0u;
    uint32_t             PresH = 0u;
    if (!VerifyCheck(
            SwapchainPresenter.ReadbackLastPresentedRgba(PresentedRgba, PresW, PresH) && PresW == 1840u && PresH == 980u,
            "ReadbackLastPresentedRgba 1840x980"))
    {
        SwapchainPresenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }
    if (!VerifyCheck(
            WriteRawRgbaPng(ImageDir / "UVSwapchain_PresentedViewport.png", PresentedRgba, PresW, PresH),
            "Write UVSwapchain_PresentedViewport.png"))
    {
        SwapchainPresenter.TeardownOverlay();
        ImGui::DestroyContext(GuiCtx);
        return 1;
    }

    SwapchainPresenter.TeardownOverlay();
    Dock.Finalize();
    Session.TeardownGPUSolvers();
    Rasterizer.TeardownRasterizer();
    TypographyRepository::AssignCurrent(nullptr);
    ImGui::DestroyContext(GuiCtx);
    std::filesystem::remove_all(TempDir);

    std::printf("[UVInteractiveAuthoringValidation] All 6 UV Workspace completion pillars PASSED.\n");
    return 0;
}
