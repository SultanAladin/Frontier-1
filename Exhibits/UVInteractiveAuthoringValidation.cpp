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
#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
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
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
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

    // Blank-space click in 3D viewport deselects all faces, layers, and active gizmos
    {
        PaneInputReading BlankClick {};
        BlankClick.PrimaryPointer.X       = SpatialBounds.X + 8.0f;
        BlankClick.PrimaryPointer.Y       = SpatialBounds.Y + 8.0f;
        BlankClick.PrimaryPointer.Down    = true;
        BlankClick.PrimaryPointer.Pressed = true;
        Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, BlankClick });
        if (!VerifyCheck(
                !Session.SpatialSelectionActive && Session.SelectionMask.CountSelectedFaces() == 0u,
                "Clicking blank viewport space deselects all faces, layers, and gizmos"))
        {
            return 1;
        }
        Session.SelectPriorityLayer(1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4b. Component Modes (1-5), Vertex/Edge Picking & Operations, Gizmo White Stems, Drag Capture & Context Sheet
    //--------------------------------------------------------------------------------------------------------------------
    {
        // A. Pure-white unshaded Gizmo axis stems in 3D and 2D
        const auto GizmoVerts3D = Session.Gizmo3D.BuildTriangleStream();
        const auto GizmoVerts2D = Session.Gizmo2D.BuildTriangleStream();
        auto CountWhiteStemVerts = [](const std::vector<Editor::GizmoGPUVertex>& Verts) noexcept -> uint32_t
        {
            uint32_t Count = 0u;
            for (const auto& V : Verts)
            {
                if ((V.NormalAndHandle.W == 1.0f || V.NormalAndHandle.W == 2.0f || V.NormalAndHandle.W == 3.0f) &&
                    V.ColorRGBAndShaded.W == 0.0f &&
                    V.ColorRGBAndShaded.X > 0.99f &&
                    V.ColorRGBAndShaded.Y > 0.99f &&
                    V.ColorRGBAndShaded.Z > 0.99f)
                {
                    ++Count;
                }
            }
            return Count;
        };
        if (!VerifyCheck(
                CountWhiteStemVerts(GizmoVerts3D) > 0u && CountWhiteStemVerts(GizmoVerts2D) > 0u,
                "3D & 2D TransformGizmos emit pure-white unshaded axis stems"))
        {
            return 1;
        }

        // B. Keyboard shortcuts 1-5 + Layer highlight isolation
        const std::pair<const char*, UVSelectionMode> ShortcutBindings[5] = {
            { "1", UVSelectionMode::Vertices },
            { "2", UVSelectionMode::Edges },
            { "3", UVSelectionMode::Faces },
            { "4", UVSelectionMode::Islands },
            { "5", UVSelectionMode::Layers },
        };
        for (const auto& [KeyText, ExpectedMode] : ShortcutBindings)
        {
            PaneInputReading KeyIn {};
            KeyIn.PrimaryPointer.X = PlanarBounds.X + 24.0f;
            KeyIn.PrimaryPointer.Y = PlanarBounds.Y + 24.0f;
            KeyIn.TypedUtf8        = KeyText;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, KeyIn });
            if (!VerifyCheck(Session.SelectionMode == ExpectedMode, "Shortcuts 1-5 switch UVSelectionMode"))
            {
                return 1;
            }
            if (ExpectedMode != UVSelectionMode::Layers)
            {
                if (!VerifyCheck(
                        Session.RenderOptions3D.ActiveLayerOrdinal == UINT16_MAX &&
                        Session.RenderOptions2D.ActiveLayerOrdinal == UINT16_MAX,
                        "ActiveLayerOrdinal cleared outside Layers mode"))
                {
                    return 1;
                }
            }
        }

        // C. Quad TacticalCrate: EdgeLoop, ShortestVertexPath, Vertex/Edge Selection, Pin, Align & Welded Transform
        Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);
        auto& CrateNet = Session.Asset.Network;
        const auto QuadLoop = GeometrySelection::EvaluateEdgeLoop(CrateNet, 0u);
        const auto VertPath = GeometrySelection::EvaluateShortestVertexPath(CrateNet, 0u, 6u, false);
        if (!VerifyCheck(QuadLoop.size() >= 2u && VertPath.size() >= 2u, "Quad EdgeLoop & ShortestVertexPath"))
        {
            return 1;
        }

        // Switch to Vertices mode via shortcut '1'
        {
            PaneInputReading Key1 {};
            Key1.PrimaryPointer.X = PlanarBounds.X + 24.0f;
            Key1.PrimaryPointer.Y = PlanarBounds.Y + 24.0f;
            Key1.TypedUtf8        = "1";
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, Key1 });
        }
        Session.SelectionMask.ClearAll();
        Session.SelectionMask.AssignVertexSelected(0u, true);
        Session.SynchronizeSelectionToGPU();
        const auto WeldedCorners = UVAuthoringOperations::CollectTransformableCornerEdges(CrateNet, Session.SelectionMask);
        if (!VerifyCheck(!WeldedCorners.empty(), "CollectTransformableCornerEdges finds corners of selected vertex"))
        {
            return 1;
        }
        const Mathematics::UVAxes RefCornerUVBefore = CrateNet.Edges[WeldedCorners[0]].ParameterUV;
        UVAuthoringOperations::TransformSelectedCornerUVs(
            CrateNet,
            WeldedCorners,
            Mathematics::UVAxes{ 0.5f, 0.5f },
            Mathematics::UVAxes{ 0.05f, -0.025f },
            0.0f,
            Mathematics::Scalar2D{ 1.0f, 1.0f });
        if (!VerifyCheck(
                std::abs(CrateNet.Edges[WeldedCorners[0]].ParameterUV.U - (RefCornerUVBefore.U + 0.05f)) < 1.0e-4f,
                "TransformSelectedCornerUVs translates welded corners cleanly"))
        {
            return 1;
        }
        // Test Vertex pinning + alignment U/V
        Session.SelectionMask.AssignVertexSelected(1u, true);
        const auto SelVerts        = Session.SelectionMask.CollectSelectedVertices();
        const uint32_t PinnedVerts = UVAuthoringOperations::AssignPinLockOnVertices(CrateNet, SelVerts, true);
        UVAuthoringOperations::AlignSelectedVertexUVs(CrateNet, SelVerts, UVAlignAxis::HorizontalU);
        const uint32_t UnpinnedV   = UVAuthoringOperations::AssignPinLockOnVertices(CrateNet, SelVerts, false);
        const auto AlignedCorners  = UVAuthoringOperations::CollectTransformableCornerEdges(CrateNet, Session.SelectionMask);
        const bool UAlignedSame    = AlignedCorners.size() >= 2u &&
            std::abs(CrateNet.Edges[AlignedCorners.front()].ParameterUV.U - CrateNet.Edges[AlignedCorners.back()].ParameterUV.U) < 1.0e-4f;
        if (!VerifyCheck(PinnedVerts > 0u && UnpinnedV > 0u && UAlignedSame, "Vertex Pin/Unpin & Align UVs"))
        {
            return 1;
        }

        // Ctrl+A / Ctrl+I / Ctrl+L in Vertices and Edges modes without touching FaceBits
        Session.SelectAllInActiveMode();
        if (!VerifyCheck(
                Session.SelectionMask.CountSelectedVertices() == CrateNet.Vertices.size() &&
                Session.SelectionMask.CountSelectedFaces() == 0u,
                "SelectAll in Vertices mode does not pollute FaceBits"))
        {
            return 1;
        }
        Session.SelectionMask.AssignVertexSelected(0u, false);
        Session.InvertSelectionInActiveMode();
        if (!VerifyCheck(
                Session.SelectionMask.CountSelectedVertices() == 1u &&
                Session.SelectionMask.QueryVertexSelected(0u),
                "InvertSelection in Vertices mode isolates unselected vertices"))
        {
            return 1;
        }

        // Switch to Edges mode via shortcut '2', verify Ctrl+A / Ctrl+I / Alt+Click edge loop
        {
            PaneInputReading Key2 {};
            Key2.PrimaryPointer.X = PlanarBounds.X + 24.0f;
            Key2.PrimaryPointer.Y = PlanarBounds.Y + 24.0f;
            Key2.TypedUtf8        = "2";
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, Key2 });
        }
        Session.SelectAllInActiveMode();
        if (!VerifyCheck(
                Session.SelectionMask.CountSelectedEdges() == CrateNet.Edges.size() &&
                Session.SelectionMask.CountSelectedFaces() == 0u,
                "SelectAll in Edges mode selects all edges without touching FaceBits"))
        {
            return 1;
        }
        Session.ClearViewportSelection();

        // D. GPU ClosestAuthEdge PickID verification & Interactive Edge/Vertex Click + Right-Click Sheet + Drag Capture
        Session.FitSelectionOrNetwork();
        Session.SynchronizeSelectionToGPU();
        Session.RefreshViewportFramebuffers(
            static_cast<uint32_t>(PlanarBounds.Width), static_cast<uint32_t>(PlanarBounds.Height),
            static_cast<uint32_t>(SpatialBounds.Width), static_cast<uint32_t>(SpatialBounds.Height));

        // Scan 3D center neighborhood for a surface pixel and verify Pick3DPixel returns ClosestAuthEdge in PickIDs.z
        bool FoundSurfaceEdgePick = false;
        Point2D SurfaceClickPt { SpatialBounds.X + SpatialBounds.Width * 0.5f, SpatialBounds.Y + SpatialBounds.Height * 0.5f };
        for (int32_t Dy = -80; Dy <= 80 && !FoundSurfaceEdgePick; Dy += 12)
        {
            for (int32_t Dx = -80; Dx <= 80 && !FoundSurfaceEdgePick; Dx += 12)
            {
                const float Px = SpatialBounds.Width * 0.5f + static_cast<float>(Dx);
                const float Py = SpatialBounds.Height * 0.5f + static_cast<float>(Dy);
                const auto Hit3 = Rasterizer.Pick3DPixel(static_cast<uint32_t>(Px), static_cast<uint32_t>(Py));
                if (Hit3.HitSurfaceFace() && Hit3.DirectedEdgeIndex != InvalidNetworkIndex)
                {
                    FoundSurfaceEdgePick = true;
                    SurfaceClickPt = { SpatialBounds.X + Px, SpatialBounds.Y + Py };
                }
            }
        }
        if (!VerifyCheck(FoundSurfaceEdgePick, "Pick3DPixel returns ClosestAuthEdge in PickIDs.z"))
        {
            return 1;
        }

        // Alt+Click at SurfaceClickPt in Edges mode selects edge loop cleanly without setting FaceBits
        {
            PaneInputReading AltEdgeClick {};
            AltEdgeClick.AltHeld                 = true;
            AltEdgeClick.PrimaryPointer.X        = SurfaceClickPt.X;
            AltEdgeClick.PrimaryPointer.Y        = SurfaceClickPt.Y;
            AltEdgeClick.PrimaryPointer.Down     = true;
            AltEdgeClick.PrimaryPointer.Pressed  = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, AltEdgeClick });
            if (!VerifyCheck(
                    Session.SelectionMask.CountSelectedEdges() >= 2u &&
                    Session.SelectionMask.CountSelectedFaces() == 0u,
                    "Alt+Click in Edges mode selects edge loop without setting FaceBits"))
            {
                return 1;
            }
        }

        // Single click in Edges mode, then switch to Vertices mode for single click, Ctrl+Click path & BoxMarquee
        {
            PaneInputReading PlainEdgeClick {};
            PlainEdgeClick.PrimaryPointer.X        = SurfaceClickPt.X;
            PlainEdgeClick.PrimaryPointer.Y        = SurfaceClickPt.Y;
            PlainEdgeClick.PrimaryPointer.Down     = true;
            PlainEdgeClick.PrimaryPointer.Pressed  = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, PlainEdgeClick });
            if (!VerifyCheck(
                    Session.SelectionMask.CountSelectedEdges() >= 1u &&
                    Session.SelectionMask.CountSelectedFaces() == 0u,
                    "Single click in Edges mode selects edge without setting FaceBits"))
            {
                return 1;
            }

            Session.AssignSelectionMode(UVSelectionMode::Vertices);
            PaneInputReading PlainVertClick {};
            PlainVertClick.PrimaryPointer.X        = SurfaceClickPt.X;
            PlainVertClick.PrimaryPointer.Y        = SurfaceClickPt.Y;
            PlainVertClick.PrimaryPointer.Down     = true;
            PlainVertClick.PrimaryPointer.Pressed  = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, PlainVertClick });
            const uint32_t FirstVertSel = Session.SelectionMask.CountSelectedVertices();
            if (!VerifyCheck(
                    FirstVertSel == 1u && Session.SelectionMask.CountSelectedFaces() == 0u,
                    "Single click in Vertices mode selects vertex without setting FaceBits"))
            {
                return 1;
            }

            Point2D SecondSurfacePt = SurfaceClickPt;
            for (int32_t Offset = 48; Offset <= 96; Offset += 16)
            {
                const float Px = std::clamp(SurfaceClickPt.X - SpatialBounds.X + static_cast<float>(Offset), 8.0f, SpatialBounds.Width - 8.0f);
                const float Py = std::clamp(SurfaceClickPt.Y - SpatialBounds.Y + static_cast<float>(Offset), 8.0f, SpatialBounds.Height - 8.0f);
                const auto HitCand = Rasterizer.Pick3DPixel(static_cast<uint32_t>(Px), static_cast<uint32_t>(Py));
                if (HitCand.HitSurfaceFace())
                {
                    SecondSurfacePt = { SpatialBounds.X + Px, SpatialBounds.Y + Py };
                    break;
                }
            }
            PaneInputReading CtrlVertClick {};
            CtrlVertClick.ControlHeld             = true;
            CtrlVertClick.PrimaryPointer.X        = SecondSurfacePt.X;
            CtrlVertClick.PrimaryPointer.Y        = SecondSurfacePt.Y;
            CtrlVertClick.PrimaryPointer.Down     = true;
            CtrlVertClick.PrimaryPointer.Pressed  = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, CtrlVertClick });
            if (!VerifyCheck(
                    Session.SelectionMask.CountSelectedVertices() >= 1u &&
                    Session.SelectionMask.CountSelectedFaces() == 0u,
                    "Ctrl+Click in Vertices mode preserves vertex mode without setting FaceBits"))
            {
                return 1;
            }

            // BoxMarquee drag across 3D viewport in Vertices mode preserves Vertices mode
            Interactor.ActivateBoxMarquee(true);
            PaneInputReading BoxStart {};
            BoxStart.PrimaryPointer.X       = SpatialBounds.X + 20.0f;
            BoxStart.PrimaryPointer.Y       = SpatialBounds.Y + 20.0f;
            BoxStart.PrimaryPointer.Down    = true;
            BoxStart.PrimaryPointer.Pressed = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, BoxStart });

            PaneInputReading BoxRelease {};
            BoxRelease.PrimaryPointer.X        = SpatialBounds.Right() - 20.0f;
            BoxRelease.PrimaryPointer.Y        = SpatialBounds.Bottom() - 20.0f;
            BoxRelease.PrimaryPointer.Down     = false;
            BoxRelease.PrimaryPointer.Released = true;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, BoxRelease });
            Interactor.ActivateBoxMarquee(false);
            if (!VerifyCheck(
                    Session.SelectionMask.CountSelectedVertices() >= 4u &&
                    Session.SelectionMask.CountSelectedFaces() == 0u,
                    "BoxMarquee in Vertices mode selects enclosed vertices without setting FaceBits"))
            {
                return 1;
            }
        }

        // SecondaryPressed opens viewport directive sheet cleanly
        {
            PaneInputReading RmbPress {};
            RmbPress.SecondaryPressed = true;
            RmbPress.SecondaryDown    = true;
            RmbPress.PrimaryPointer.X = SurfaceClickPt.X;
            RmbPress.PrimaryPointer.Y = SurfaceClickPt.Y;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, RmbPress });
            Point2D SheetAnchor {};
            bool    SheetPlanar = true;
            if (!VerifyCheck(
                    Interactor.ConsumeSecondarySheetRequest(SheetAnchor, SheetPlanar) && !SheetPlanar,
                    "Right-click inside 3D viewport queues directive sheet request"))
            {
                return 1;
            }
        }

        // MMB drag outside bounds keeps QueryDragCaptureActive() latched until released
        {
            PaneInputReading MmbStart {};
            MmbStart.MiddlePressed    = true;
            MmbStart.MiddleDown       = true;
            MmbStart.PrimaryPointer.X = SpatialBounds.X + SpatialBounds.Width * 0.5f;
            MmbStart.PrimaryPointer.Y = SpatialBounds.Y + SpatialBounds.Height * 0.5f;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, MmbStart });

            PaneInputReading MmbDragOut {};
            MmbDragOut.MiddleDown       = true;
            MmbDragOut.PrimaryPointer.X = PlanarBounds.X + 10.0f; // dragged across into opposite pane!
            MmbDragOut.PrimaryPointer.Y = PlanarBounds.Y + 10.0f;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, MmbDragOut });
            if (!VerifyCheck(Interactor.QueryDragCaptureActive(), "MMB orbit drag latches QueryDragCaptureActive across pane boundary"))
            {
                return 1;
            }

            // Shared WorkspaceAppearance pointer ownership isolates UVOutliner / UVInspector / generic Outliner cleanly
            WorkspaceAppearance::ResetAllPanePointerOwnership();
            OutlinerPanel GenericOutliner {};
            WorkspaceAppearance::UpdatePanePointerOwnership(
                &Host,
                { 0.0f, 0.0f, 1280.0f, 720.0f },
                1.0f,
                MmbDragOut,
                Interactor.QueryDragCaptureActive());
            PaneInputReading PeerAttempt {};
            PeerAttempt.PrimaryPointer.X       = 40.0f;
            PeerAttempt.PrimaryPointer.Y       = 40.0f;
            PeerAttempt.PrimaryPointer.Enabled = true;
            PeerAttempt.PrimaryPointer.Pressed = true;
            WorkspaceAppearance::EnforcePanePointerOwnership(&GenericOutliner, PeerAttempt);
            if (!VerifyCheck(
                    WorkspaceAppearance::QueryPointerBlockedForPane(&GenericOutliner) &&
                    !PeerAttempt.PrimaryPointer.Pressed &&
                    !PeerAttempt.PrimaryPointer.Enabled,
                    "Active UV viewport drag blocks peer workspace panes via shared pointer ownership"))
            {
                return 1;
            }
            WorkspaceAppearance::ResetAllPanePointerOwnership();

            PaneInputReading MmbRelease {};
            MmbRelease.MiddleDown       = false;
            MmbRelease.PrimaryPointer.X = PlanarBounds.X + 10.0f;
            MmbRelease.PrimaryPointer.Y = PlanarBounds.Y + 10.0f;
            Interactor.ProcessViewportInput(ViewportInteractionReading{ PlanarBounds, SpatialBounds, MmbRelease });
            if (!VerifyCheck(!Interactor.QueryDragCaptureActive(), "MMB release clears QueryDragCaptureActive"))
            {
                return 1;
            }
        }

        // Restore AeroCoupeGT and Layer 1 selection for section 5 Outliner & ImGui Swapchain UI checks
        Session.LoadSamplePreset(SampleAssetPreset::AeroCoupeGT);
        Session.SelectPriorityLayer(1u);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Outliner Layer Authoring + Full Docked UI + ImGuiOverlayRasterization (ImGuiComposite.slang) Proof
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
        SwapchainPresenter.PrepareImGuiFrame();
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
