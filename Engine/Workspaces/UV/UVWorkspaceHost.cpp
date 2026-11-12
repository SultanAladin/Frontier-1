/*============================================================================================================================================
                                                           UVWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVWorkspaceHost registering StandaloneWorkspace / FeatureSet docked tabs, external drops & ContentBrowser integration

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVWorkspaceHost.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <imgui.h>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] std::string NormalizeLowerExt(const std::filesystem::path& FilePath)
{
    std::string Ext = FilePath.extension().string();
    std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
    return Ext;
}

} // namespace

UVWorkspaceHost::UVWorkspaceHost()
{
    Outliner.AttachSession(&Session);
    ViewportHost.AttachSession(&Session);
    Inspector.AttachSession(&Session);
}

void UVWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

bool UVWorkspaceHost::AttachVulkanRasterizer(GeometricRaster::UVWorkspaceRasterizer* BoundRasterizer)
{
    return Session.AttachVulkanRasterizer(BoundRasterizer);
}

#if FRONTIER_EDITOR_ENABLED
void UVWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    if (BoundBrowser == nullptr)
    {
        return;
    }

    // Enforce WorkspaceCategory::UV filter (exposes Geometries, Materials, Textures, Images, Colours; hides Simulations/Particles/Audio)
    BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::UV);
    BoundBrowser->AccessPanel().AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            using GeometrySpace::SampleAssetPreset;
            if (Entry.DisplayTitle == "Controlled Integrator")
            {
                Session.LoadSamplePreset(SampleAssetPreset::ControlledIntegrator);
            }
            else if (Entry.DisplayTitle == "Aero Coupe GT")
            {
                Session.LoadSamplePreset(SampleAssetPreset::AeroCoupeGT);
            }
            else if (Entry.DisplayTitle == "Tactical Crate")
            {
                Session.LoadSamplePreset(SampleAssetPreset::TacticalCrate);
            }
            else if (Entry.DisplayTitle == "Armored Bust")
            {
                Session.LoadSamplePreset(SampleAssetPreset::ArmoredBust);
            }
            else if (Entry.DisplayTitle == "UDIM Arch Kit")
            {
                Session.LoadSamplePreset(SampleAssetPreset::UDIMArchKit);
            }
        });

    BoundBrowser->AccessPanel().RegisterColourSlot(ColourSlotRegistration{
        "uv.priority_layer.tint",
        "Active UV Priority Layer Tint",
        Rectangle2D{ 16.0f, 96.0f, 260.0f, 44.0f },
        nullptr,
        [this](ColorRgba AssignedRgba, const ColourCodexEntry&)
        {
            Session.AssignSelectedLayerTint({
                static_cast<float>(AssignedRgba.Red) / 255.0f,
                static_cast<float>(AssignedRgba.Green) / 255.0f,
                static_cast<float>(AssignedRgba.Blue) / 255.0f,
            });
        },
    });
    BoundBrowser->AccessPanel().FocusColourSlot("uv.priority_layer.tint");
}
#endif

bool UVWorkspaceHost::IntakeExternalFileDrop(
    const std::filesystem::path& DroppedPath,
    std::string&                 OutDiagnostic)
{
    const std::string Ext = NormalizeLowerExt(DroppedPath);
    if (Ext == ".obj" || Ext == ".gltf")
    {
#if FRONTIER_EDITOR_ENABLED
        if (BoundBrowser != nullptr)
        {
            (void)BoundBrowser->IntakeExternalAssetDrop(DroppedPath);
        }
#endif
        return Session.ImportExternalGeometry(DroppedPath, OutDiagnostic);
    }

    if (Ext == ".uvspace")
    {
        return Session.LoadUVSpaceDocument(DroppedPath, OutDiagnostic);
    }

    if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".tga" || Ext == ".hdr")
    {
#if FRONTIER_EDITOR_ENABLED
        if (BoundBrowser != nullptr)
        {
            (void)BoundBrowser->IntakeExternalAssetDrop(DroppedPath);
        }
#endif
        auto& TileCfg          = Session.AccessUDIMTileSetting(Session.ActiveUDIMNumber);
        TileCfg.BackdropPreset = GeometricRaster::UVBackdropPreset::TrimSheetAtlas;
        Session.FramebuffersDirty = true;
        Session.StatusBannerText  = "Applied Texture Backdrop Preview on Active UDIM";
        OutDiagnostic.clear();
        return true;
    }

    OutDiagnostic = "Unsupported UV Workspace drop extension: " + Ext;
    return false;
}

void UVWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    // Left Pane: Outliner (UV Space)
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.19f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Outliner.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });

    // Center Pane: Viewport (Standalone) or UV (FeatureSet multi-workspace group)
    const char* CentralTitle =
        (ActiveEngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::FeatureSet) ? "UV" : "Viewport";
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        CentralTitle,
        DockPlacementDirection::CenterTabGroup,
        0.57f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]() { ViewportHost.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
    ++RegisteredTabCount;

    // In HostLaunchMode::FeatureSet, register peer domain workspace tabs in the central dock group
    if (ActiveEngineConfig.QueryLaunchMode() == Configuration::HostLaunchMode::FeatureSet)
    {
        const char* PeerWorkspaces[4] = {
            "Geometry",
            "Texture",
            "Material",
            "Simulate",
        };
        for (const char* PeerTitle : PeerWorkspaces)
        {
            (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
                PeerTitle,
                DockPlacementDirection::CenterTabGroup,
                0.57f,
                PaneDirectiveMask::CentralViewportDirectives,
                [PeerTitle]()
                {
                    ImGui::TextUnformatted(PeerTitle);
                },
            });
            ++RegisteredTabCount;
        }
    }

    // Right Pane: 5-Tab UV Inspector
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.24f,
        PaneDirectiveMask::StandardPaneDirectives,
        [this]() { Inspector.PresentInImGuiPane(1.0 / 60.0, 1.0f); },
    });
}

} // namespace Frontier::Workspaces
