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
    UploadedSceneGen  = 0u;

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

    const uint32_t TargetW = std::clamp(static_cast<uint32_t>(std::max(320.0f, State.ViewportRect[2])), 320u, 1920u);
    const uint32_t TargetH = std::clamp(static_cast<uint32_t>(std::max(200.0f, State.ViewportRect[3])), 200u, 1080u);
    if (TargetW != Rasterizer.Width() || TargetH != Rasterizer.Height())
    {
        Rasterizer.Resize(TargetW, TargetH);
    }

    if (UploadedMatcapGen != State.Matcaps.Generation)
    {
        Rasterizer.UploadMatcaps(State.Matcaps.Library.Pictures);
        UploadedMatcapGen = State.Matcaps.Generation;
    }

    const auto& Scene  = Sketcher::DisplayedSketcherScene(State);
    const auto  States = Sketcher::DisplayedSketcherStates(State);
    if (UploadedSceneGen != Scene.Revision)
    {
        Rasterizer.UploadScene(Scene);
        UploadedSceneGen = Scene.Revision;
    }
    Rasterizer.AssignStates(States.Faces, States.Edges);

    const auto Panes = Sketcher::ComposeSketcherRasterPanes(State, Rasterizer.Width(), Rasterizer.Height());
    int ProbeX = -1;
    int ProbeY = -1;
    if (State.ViewportHovered && ImGui::GetCurrentContext() != nullptr && State.ViewportRect[2] > 1.0f && State.ViewportRect[3] > 1.0f)
    {
        const ImVec2 Mouse = ImGui::GetIO().MousePos;
        const float U = (Mouse.x - State.ViewportRect[0]) / State.ViewportRect[2];
        const float V = (Mouse.y - State.ViewportRect[1]) / State.ViewportRect[3];
        if (U >= 0.0f && U < 1.0f && V >= 0.0f && V < 1.0f)
        {
            ProbeX = std::clamp(static_cast<int>(U * float(Rasterizer.Width())), 0, int(Rasterizer.Width()) - 1);
            ProbeY = std::clamp(static_cast<int>(V * float(Rasterizer.Height())), 0, int(Rasterizer.Height()) - 1);
        }
    }

    VkCommandBuffer Cmd = Device->BeginSingleTimeCommands();
    if (Cmd == VK_NULL_HANDLE)
    {
        return;
    }

    if (Panes.size() > 1)
    {
        Rasterizer.RecordPanes(Cmd, Panes, State.Style.Background, true, ProbeX, ProbeY);
    }
    else
    {
        const OrbitCameraFrame Frame =
            FrameOrbitCamera(State.Camera, double(Rasterizer.Width()), double(Rasterizer.Height()));
        Rasterizer.Record(Cmd, Frame, State.Style, true, ProbeX, ProbeY);
    }
    vkEndCommandBuffer(Cmd);

    const uint64_t Tick = Device->DispatchCommandBufferAsync(Cmd);
    if (Device->AwaitTimelineTick(Tick))
    {
        State.HoverIdentity = Rasterizer.ProbedIdentity();
        UploadViewportPixels(Rasterizer.ReadbackColour(), Rasterizer.Width(), Rasterizer.Height());
    }
    Device->FreeCommandBuffer(Cmd);
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
        ViewportTexture.SetStatus(NeedsCreate ? ImTextureStatus_WantCreate : ImTextureStatus_WantUpdates);
    }
}

} // namespace Frontier::Workspaces
