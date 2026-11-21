/*============================================================================================================================================
                                                       SKETCHERWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of SketcherWorkspaceHost: Vulkan offscreen B-rep rasterization + ImTextureData upload + 5 dockable tabs

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherWorkspaceHost.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherIcons.h"

#include <volk.h>

#include <algorithm>
#include <cstring>

namespace Frontier::Workspaces
{

SketcherWorkspaceHost::~SketcherWorkspaceHost() noexcept
{
    Finalize();
}

bool SketcherWorkspaceHost::Initialize(
    GeometricRaster::VulkanInterchange* Interchange,
    const std::filesystem::path&        CompiledShaderDir,
    const std::filesystem::path&        IconsRoot,
    const std::string&                  InitialDocumentPath,
    std::string*                        OutDiagnostic)
{
    Finalize();

    Device  = Interchange;
    Options = Sketcher::SketcherOptions{};
    if (!InitialDocumentPath.empty())
    {
        Options.DocumentPath = InitialDocumentPath;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        SketcherIconPresentation::Attach(IconsRoot);
        Sketcher::ConfigureSketcherInterface(Options);
    }
    Sketcher::InitialiseSketcherWorkspace(State, Options);

    if (!InitialDocumentPath.empty() && std::filesystem::exists(InitialDocumentPath))
    {
        const Sketcher::SketcherDocumentFile File = Sketcher::LoadSketcherDocumentFile(InitialDocumentPath);
        if (File.Refusal.empty())
        {
            State.Document.Title        = File.Title;
            State.Document.ImagePlanes  = File.ImagePlanes;
            State.Document.Appearance   = File.Appearance;
            State.Document.DrawingSheet = File.DrawingSheet;
            State.Document.Dimensions   = File.Dimensions;
            (void)Sketcher::ReplaceSketcherTree(State.Document, File.Tree, File.Captions, {});
            State.DocumentPath = InitialDocumentPath;
            Sketcher::RefreshSketcherScene(State, false);
            Sketcher::ConfigureSketcherStartupViews(State);
        }
    }

    if (Device != nullptr)
    {
        std::string RasterDiag {};
        if (Rasterizer.Create(*Device, CompiledShaderDir, RasterDiag, 1u))
        {
            Rasterizer.Resize(640u, 360u);
            RasterReady = true;
            State.DeviceLabel = std::string(Device->QueryAdapterName());
            Sketcher::RefreshSketcherMatcaps(State);
            RenderViewportTarget();
        }
        else if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = RasterDiag;
        }
    }

    Initialized = true;
    return true;
}

void SketcherWorkspaceHost::Finalize() noexcept
{
    ViewportTexture.DestroyPixels();
    UploadedMatcapGen = 0u;

    if (RasterReady)
    {
        Rasterizer.Destroy();
        RasterReady = false;
    }
    Device      = nullptr;
    Initialized = false;
}

void SketcherWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::GeometryCad);
    }
}

void SketcherWorkspaceHost::SynchronizeHostScale(float ScaleFactor) noexcept
{
    HostScaleFactor = std::clamp(ScaleFactor, 0.25f, 4.0f);
}

void SketcherWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.20f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Sketcher::DrawSketcherOutlinerPanel(State); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Viewport",
        DockPlacementDirection::CenterTabGroup,
        0.57f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]()
        {
            RenderViewportTarget();
            const ImTextureRef TexRef =
                (ViewportTexture.Pixels != nullptr) ? ViewportTexture.GetTexRef() : ImTextureRef();
            Sketcher::DrawSketcherViewportPanel(State, TexRef);
        },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Drawing Sheet",
        DockPlacementDirection::CenterTabGroup,
        0.57f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Sketcher::DrawSketcherDrawingSheetPanel(State); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Construct",
        DockPlacementDirection::CenterTabGroup,
        0.57f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Sketcher::DrawSketcherConstructPanel(State); },
    });
    ++RegisteredTabCount;

    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.23f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Sketcher::DrawSketcherInspectorPanel(State); },
    });
    ++RegisteredTabCount;
}

void SketcherWorkspaceHost::BeginFrame(double DeltaSeconds)
{
    LastDeltaSeconds = std::max(1.0e-4, DeltaSeconds);
    Sketcher::BeginSketcherHostedFrame(State);
}

void SketcherWorkspaceHost::EndFrame(double DeltaSeconds, bool DrawFloatingOverlays, bool HostedOverlayMode)
{
    Sketcher::EndSketcherHostedFrame(State, std::max(1.0e-4, DeltaSeconds), DrawFloatingOverlays, HostedOverlayMode);
    // ApplyShortcuts runs in EndSketcherHostedFrame. Recompose once afterward so the
    // CPU pick geometry and the next GPU frame agree immediately after hotkeys.
    const bool Single = State.SplitView.Layout == Sketcher::SketcherViewportLayout::Single;
    const auto& Rect = Single ? State.ViewportRect : State.SplitView.FullRect;
    const uint32_t Width = static_cast<uint32_t>(std::max(1.0f, Rect[2]));
    const uint32_t Height = static_cast<uint32_t>(std::max(1.0f, Rect[3]));
    const auto Panes = Sketcher::ComposeSketcherRasterPanes(State, Width, Height);
    State.Style.Gizmo = Panes.empty() ? GizmoGeometry{} : Panes.front().Style.Gizmo;
    Sketcher::ConcludeSketcherFrame(State);
}

bool SketcherWorkspaceHost::IntakeExternalFileDrop(const std::string& FilePath, std::string* OutDiagnostic)
{
    return Sketcher::IntakeSketcherDroppedFile(State, FilePath, OutDiagnostic);
}

void SketcherWorkspaceHost::RenderViewportTarget()
{
    if (!RasterReady || Device == nullptr)
    {
        return;
    }

    try
    {
        const bool Single = State.SplitView.Layout == Sketcher::SketcherViewportLayout::Single;
        const auto& TargetRect = Single ? State.ViewportRect : State.SplitView.FullRect;
        const uint32_t TargetW = std::clamp(static_cast<uint32_t>(std::max(320.0f, TargetRect[2])), 320u, 1920u);
        const uint32_t TargetH = std::clamp(static_cast<uint32_t>(std::max(200.0f, TargetRect[3])), 200u, 1080u);
        if (TargetW != Rasterizer.Width() || TargetH != Rasterizer.Height())
        {
            Rasterizer.Resize(TargetW, TargetH);
        }

        if (UploadedMatcapGen != State.Matcaps.Generation)
        {
            Rasterizer.UploadMatcaps(State.Matcaps.Library.Pictures);
            UploadedMatcapGen = State.Matcaps.Generation;
        }

        const auto& Scene = Sketcher::DisplayedSketcherScene(State);
        if (Scene.Generation != Rasterizer.SceneGeneration())
        {
            Rasterizer.UploadScene(Scene);
        }

        // Decode each distinct image-plane source only when its path set changes,
        // then rebuild the lightweight draw quads from current document placement.
        std::string PictureKey;
        std::vector<std::string> PicturePaths;
        for (const Sketcher::SketcherImagePlane& Plane : State.Document.ImagePlanes)
        {
            if (std::find(PicturePaths.begin(), PicturePaths.end(), Plane.Path) == PicturePaths.end())
                PicturePaths.push_back(Plane.Path);
        }
        for (const std::string& Path : PicturePaths) PictureKey += Path + "\n";
        if (PictureKey != State.Pictures.Key)
        {
            std::vector<Sketcher::SketcherPicture> Pictures;
            State.Pictures = {};
            State.Pictures.Key = PictureKey;
            for (const std::string& Path : PicturePaths)
            {
                Pictures.push_back(Sketcher::DecodeSketcherPicture(Path));
                State.Pictures.Paths.push_back(Path);
                State.Pictures.Widths.push_back(Pictures.back().Width);
                State.Pictures.Heights.push_back(Pictures.back().Height);
                State.Pictures.Refusals.push_back(Pictures.back().Refusal);
            }
            Rasterizer.UploadPictures(Pictures);
        }

        State.Style.ImagePlanes.clear();
        if (State.ImagePlanesEnabled)
        {
            for (const Sketcher::SketcherImagePlane& Plane : State.Document.ImagePlanes)
            {
                const auto Found = std::find(State.Pictures.Paths.begin(), State.Pictures.Paths.end(), Plane.Path);
                const size_t Index = static_cast<size_t>(Found - State.Pictures.Paths.begin());
                if (!Plane.VisibleCondition || Found == State.Pictures.Paths.end() || !State.Pictures.Refusals[Index].empty()) continue;
                const float Width  = static_cast<float>(State.Pictures.Widths[Index]);
                const float Height = static_cast<float>(State.Pictures.Heights[Index]);
                if (Width <= 0.0f || Height <= 0.0f) continue;
                const bool Whole = Plane.Crop[2] <= Plane.Crop[0] || Plane.Crop[3] <= Plane.Crop[1];
                Sketcher::SketcherImagePlaneQuad Draw;
                Draw.Picture = static_cast<uint32_t>(Index);
                Draw.Corner  = { Plane.Corner.X, Plane.Corner.Y, Plane.Corner.Z };
                Draw.Right   = { Plane.Right.X, Plane.Right.Y, Plane.Right.Z };
                Draw.Down    = { Plane.Down.X, Plane.Down.Y, Plane.Down.Z };
                if (!Whole) Draw.Crop = { Plane.Crop[0] / Width, Plane.Crop[1] / Height, Plane.Crop[2] / Width, Plane.Crop[3] / Height };
                if (Plane.FlipAcrossCondition) std::swap(Draw.Crop[0], Draw.Crop[2]);
                if (Plane.FlipDownCondition) std::swap(Draw.Crop[1], Draw.Crop[3]);
                Draw.Opacity = static_cast<float>(Plane.Opacity);
                Draw.Mode = Plane.PhotoCondition ? 1.0f : 0.0f;
                Draw.FrontOnlyCondition = Plane.FrontOnlyCondition;
                State.Style.ImagePlanes.push_back(Draw);
            }
        }
        const OrbitCameraBasis ActiveFrame = FrameOrbitCamera(State.Camera, 1.0, 1.0);
        State.Pictures.Drawn = static_cast<int>(std::count_if(State.Style.ImagePlanes.begin(), State.Style.ImagePlanes.end(),
            [&](const Sketcher::SketcherImagePlaneQuad& Draw) { return Sketcher::FacesSketcherImagePlane(Draw, ActiveFrame); }));

        const auto States = Sketcher::DisplayedSketcherStates(State);
        Rasterizer.AssignStates(States.Faces, States.Edges);
        State.Style.EdgeStroke = State.Options.EdgeStroke;

        const auto Panes = Sketcher::ComposeSketcherRasterPanes(State, Rasterizer.Width(), Rasterizer.Height());
        int ProbeX = -1;
        int ProbeY = -1;
        if (State.ViewportHovered && ImGui::GetCurrentContext() != nullptr && TargetRect[2] > 1.0f && TargetRect[3] > 1.0f)
        {
            const ImVec2 Mouse = ImGui::GetIO().MousePos;
            const float U = (Mouse.x - TargetRect[0]) / TargetRect[2];
            const float V = (Mouse.y - TargetRect[1]) / TargetRect[3];
            if (U >= 0.0f && U < 1.0f && V >= 0.0f && V < 1.0f)
            {
                ProbeX = std::clamp(static_cast<int>(U * float(Rasterizer.Width())), 0, int(Rasterizer.Width()) - 1);
                ProbeY = std::clamp(static_cast<int>(V * float(Rasterizer.Height())), 0, int(Rasterizer.Height()) - 1);
            }
        }

        VkCommandBuffer Cmd = Device->BeginSingleTimeCommands();
        if (Cmd == VK_NULL_HANDLE) return;
        Rasterizer.RecordPanes(Cmd, Panes, State.Style.Background, true, ProbeX, ProbeY);

        // DispatchCommandBufferAsync ends Cmd internally; do not call vkEndCommandBuffer twice.
        const uint64_t Tick = Device->DispatchCommandBufferAsync(Cmd);
        if (Device->AwaitTimelineTick(Tick))
        {
            State.HoverIdentity = State.ViewportHovered ? Rasterizer.ProbedIdentity() : 0u;
            UploadViewportPixels(Rasterizer.ReadbackColour(), Rasterizer.Width(), Rasterizer.Height());
        }
        Device->FreeCommandBuffer(Cmd);
    }
    catch (const std::exception& Failure)
    {
        std::fprintf(stderr, "[SketcherWorkspaceHost] RenderViewportTarget error: %s\n", Failure.what());
        RasterReady = false;
    }
}

void SketcherWorkspaceHost::UploadViewportPixels(const uint32_t* Pixels, uint32_t Width, uint32_t Height)
{
    if (Pixels == nullptr || Width == 0u || Height == 0u)
    {
        return;
    }

    const bool NeedsCreate =
        ViewportTexture.Width != static_cast<int>(Width) ||
        ViewportTexture.Height != static_cast<int>(Height) ||
        ViewportTexture.Pixels == nullptr;

    if (NeedsCreate)
    {
        ViewportTexture.DestroyPixels();
        ViewportTexture.SetStatus(ImTextureStatus_Destroyed);
        ViewportTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(Width), static_cast<int>(Height));
    }

    if (ViewportTexture.Pixels != nullptr)
    {
        std::memcpy(ViewportTexture.GetPixels(), Pixels, static_cast<size_t>(Width) * Height * 4u);
        if (!NeedsCreate)
        {
            ViewportTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(Width), static_cast<unsigned short>(Height) };
        }
        ViewportTexture.SetStatus(NeedsCreate ? ImTextureStatus_WantCreate : ImTextureStatus_WantUpdates);
    }
}

} // namespace Frontier::Workspaces
