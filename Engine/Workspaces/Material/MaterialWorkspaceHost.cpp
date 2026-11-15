/*==========================================================================================================================================
                                                    MATERIALWORKSPACEHOST.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialWorkspaceHost: docked Material Viewport + ContentBrowser dynamic preset thumbnail sync & drop assignment

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialWorkspaceHost.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] uint8_t ChannelByte(float LinearChannel) noexcept
{
    const float Clamped = std::clamp(LinearChannel, 0.0f, 1.0f);
    const float Srgb    = std::pow(Clamped, 1.0f / 2.2f);
    return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
}

[[nodiscard]] ColorRgba BuildRecipeSwatchRgba(const MaterialSpace::Dynamic::DynamicMaterialRecipe& Recipe) noexcept
{
    const auto& Col = Recipe.Parameters.PrimaryColorRgba;
    return { ChannelByte(Col[0]), ChannelByte(Col[1]), ChannelByte(Col[2]), 255u };
}

} // namespace

MaterialWorkspaceHost::MaterialWorkspaceHost()
{
    ViewportHost.AttachSession(&Session);
    ViewportHost.AttachRenderer(&PreviewRenderer);
}

void MaterialWorkspaceHost::ConfigureLaunchPolicy(const Configuration::EngineConfiguration& Config) noexcept
{
    ActiveEngineConfig = Config;
}

bool MaterialWorkspaceHost::AttachVulkanDevice(
    GeometricRaster::VulkanInterchange* ExternalDevice,
    const std::filesystem::path&        CompiledShaderDir)
{
    if (ExternalDevice == nullptr)
    {
        return false;
    }

    std::filesystem::path ShaderDir = CompiledShaderDir;
    if (ShaderDir.empty())
    {
#ifdef FRONTIER_COMPILED_SHADER_DIR
        ShaderDir = std::filesystem::path(FRONTIER_COMPILED_SHADER_DIR);
#else
        ShaderDir = std::filesystem::path("build/CompiledShaders");
#endif
    }

    const bool Attached = PreviewRenderer.InitializeVulkanPreview(*ExternalDevice, ShaderDir);
    ViewportHost.InvalidatePreviewCache();
    if (Attached && BoundBrowserPanel != nullptr)
    {
        PopulateDynamicMaterialThumbnails();
    }
    return Attached;
}

void MaterialWorkspaceHost::PopulateDynamicMaterialThumbnails()
{
    if (BoundBrowserPanel == nullptr)
    {
        return;
    }

    PopulatedThumbnailCount = 0u;
    PreviewCameraSetup ThumbCam {};
    ThumbCam.OrbitYawRadians   = 0.42f;
    ThumbCam.OrbitPitchRadians = 0.14f;
    ThumbCam.OrbitDistanceM    = 2.48f;

    for (const auto& Recipe : Session.PresetRepository)
    {
        auto EvalRecipe = Recipe;
        MaterialSpace::Dynamic::SynchronizeRecipeParameters(EvalRecipe);
        EvalRecipe.Parameters.ClockSeconds = 1.45f;

        PreviewRenderResult Thumb = PreviewRenderer.RenderPreview(
            EvalRecipe,
            EvalRecipe.Parameters,
            PreviewSpecimenGeometry::ShaderballPedestal,
            PreviewRenderMode::DirectStudioOpenPbr,
            ThumbCam,
            128u,
            128u);

        const std::filesystem::path VirtualPath =
            std::filesystem::path("preset://material") / EvalRecipe.RecipeId;

        (void)BoundBrowserPanel->AccessThumbnailRepository().RegisterOrUpdateProjectEntry(
            EvalRecipe.DisplayTitle,
            ContentCategory::Materials,
            BuildRecipeSwatchRgba(EvalRecipe),
            VirtualPath,
            !Thumb.DisplayRgba8.empty() ? std::span<const uint8_t>(Thumb.DisplayRgba8) : std::span<const uint8_t>{});
        ++PopulatedThumbnailCount;
    }
}

bool MaterialWorkspaceHost::ApplyContentBrowserEntry(const ContentRepositoryEntry& Entry)
{
    const std::string NormalizedPath = Entry.DiskPath.generic_string();
    constexpr std::string_view Prefix = "preset://material/";
    if (NormalizedPath.starts_with(Prefix))
    {
        const std::string_view RecipeKey = std::string_view(NormalizedPath).substr(Prefix.size());
        if (Session.SelectPresetIdentifier(RecipeKey))
        {
            ViewportHost.InvalidatePreviewCache();
            return true;
        }
    }

    for (const auto& Recipe : Session.PresetRepository)
    {
        if (Recipe.DisplayTitle == Entry.DisplayTitle)
        {
            Session.SelectPresetIdentity(Recipe.PresetIdentity);
            ViewportHost.InvalidatePreviewCache();
            return true;
        }
    }
    return false;
}

void MaterialWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel)
{
    BoundBrowserPanel = TargetPanel;
    if (TargetPanel == nullptr)
    {
        return;
    }

    TargetPanel->ConfigureWorkspaceDomain(WorkspaceCategory::Material);
    (void)TargetPanel->ActivateCategory(ContentCategory::Materials);
    PopulateDynamicMaterialThumbnails();

    TargetPanel->ClearContentDropZones();
    TargetPanel->RegisterContentDropZone(ContentDropZoneRegistration{
        "material.viewport",
        "Material Viewport",
        ViewportHost.QueryCanvasLogicalBounds(),
        ContentCategory::Materials,
        [this](const ContentRepositoryEntry& Entry, Point2D)
        {
            (void)ApplyContentBrowserEntry(Entry);
        },
    });

    TargetPanel->AssignContentActivationCallback([this](const ContentRepositoryEntry& Entry)
    {
        (void)ApplyContentBrowserEntry(Entry);
    });
}

#if FRONTIER_EDITOR_ENABLED
void MaterialWorkspaceHost::AttachContentBrowser(Editor::Explorer::ContentBrowser* TargetBrowser)
{
    BoundBrowser = TargetBrowser;
    AttachContentBrowserPanel(TargetBrowser != nullptr ? &TargetBrowser->AccessPanel() : nullptr);
}
#endif

void MaterialWorkspaceHost::SynchronizeContentBrowserOcclusion(float ViewportScaleFactor) noexcept
{
    const float SafeScale = std::max(0.25f, ViewportScaleFactor);
    if (BoundBrowserPanel == nullptr)
    {
        ViewportHost.AssignBottomOcclusionHeight(0.0f);
        return;
    }

    const Rectangle2D DrawerPhys = BoundBrowserPanel->QueryDrawerPhysicalBounds();
    const float Ratio            = BoundBrowserPanel->QueryVisibleDrawerHeightRatio();
    const float OcclusionLogical = (Ratio > 0.02f && DrawerPhys.Height > 1.0f) ? (DrawerPhys.Height / SafeScale) : 0.0f;
    ViewportHost.AssignBottomOcclusionHeight(OcclusionLogical);

    const Rectangle2D ViewportScreen = ViewportHost.QueryScreenBounds();
    if (ViewportScreen.Width > 8.0f && ViewportScreen.Height > 8.0f)
    {
        const Rectangle2D CanvasLocal = ViewportHost.QueryCanvasLogicalBounds();
        const Rectangle2D DropBounds {
            ViewportScreen.X / SafeScale + CanvasLocal.X,
            ViewportScreen.Y / SafeScale + CanvasLocal.Y,
            CanvasLocal.Width,
            CanvasLocal.Height,
        };
        BoundBrowserPanel->UpdateContentDropZoneBounds("material.viewport", DropBounds);
    }
}

void MaterialWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer& DockContainer)
{
    using Components::DockPlacementDirection;
    using Components::PaneDirectiveMask;
    using Components::WorkspaceDockPaneDescriptor;

    RegisteredTabCount = 0u;

    // Viewport-only Material Workspace dock (Inspector & Layer Stack intentionally deferred pending redesign)
    (void)DockContainer.RegisterPane(WorkspaceDockPaneDescriptor{
        "Material",
        DockPlacementDirection::CenterTabGroup,
        1.0f,
        PaneDirectiveMask::CentralViewportDirectives,
        [this]()
        {
            SynchronizeContentBrowserOcclusion(1.0f);
            ViewportHost.PresentImGuiPane(1.0 / 60.0, 1.0f);
        },
    });
    ++RegisteredTabCount;
}

} // namespace Frontier::Workspaces
