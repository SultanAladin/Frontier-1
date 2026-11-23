/*============================================================================================================================================
                                                        SKETCHERWORKSPACEHOST.H
============================================================================================================================================*/
// 🧩 Top-level dockable host for Parametric Sketcher: Vulkan viewport rasterizer, ThorVG SVG icon atlas, Outliner/Viewport/DrawingSheet/Construct/Inspector panes

#pragma once

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherOptions.h"
#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherRasterization.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::Workspaces
{

class SketcherWorkspaceHost
{
public:
    SketcherWorkspaceHost() = default;
    ~SketcherWorkspaceHost() noexcept;

    SketcherWorkspaceHost(const SketcherWorkspaceHost&)            = delete;
    SketcherWorkspaceHost& operator=(const SketcherWorkspaceHost&) = delete;

    bool Initialize(
        GeometricRaster::VulkanInterchange* Interchange,
        const std::filesystem::path&        CompiledShaderDir,
        const std::filesystem::path&        IconsRoot,
        const std::string&                  InitialDocumentPath = {},
        std::string*                        OutDiagnostic       = nullptr);

    void Finalize() noexcept;

    void AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept;
    void SynchronizeHostScale(float ScaleFactor) noexcept;

    void RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer);

    void BeginFrame(double DeltaSeconds);
    void EndFrame(double DeltaSeconds, bool DrawFloatingOverlays = true, bool HostedOverlayMode = true);
    bool IntakeExternalFileDrop(const std::string& FilePath, std::string* OutDiagnostic = nullptr);

    [[nodiscard]] Sketcher::SketcherWorkspaceStatus&       AccessState() noexcept        { return State; }
    [[nodiscard]] const Sketcher::SketcherWorkspaceStatus& InspectState() const noexcept { return State; }
    [[nodiscard]] uint32_t QueryRegisteredTabCount() const noexcept { return RegisteredTabCount; }

private:
    void RenderViewportTarget();
    void UploadViewportPixels(const uint32_t* Pixels, uint32_t Width, uint32_t Height);

    GeometricRaster::VulkanInterchange* Device            = nullptr;
    Sketcher::SketcherWorkspaceStatus    State              {};
    Sketcher::SketcherOptions           Options            {};
    Sketcher::SketcherRasterization     Rasterizer         {};
    ImTextureData                       ViewportTexture    {};
    uint64_t                            UploadedMatcapGen  = 0u;
    bool                                Initialized        = false;
    bool                                RasterReady        = false;
    ContentBrowserPanel*             BoundBrowser       = nullptr;
    float                            HostScaleFactor    = 1.0f;
    uint32_t                         RegisteredTabCount = 0u;
    double                           LastDeltaSeconds   = 1.0 / 60.0;
};

} // namespace Frontier::Workspaces
