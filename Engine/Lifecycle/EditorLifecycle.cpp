/*============================================================================================================================================
                                                             EDITORLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Editor-only authoring loop implementation

#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"

#if FRONTIER_EDITOR_ENABLED

#include "Engine/Components/Controls/CapsuleButton.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Lifecycle
{

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

EditorLifecycle::~EditorLifecycle() noexcept
{
    Finalize();
}

bool EditorLifecycle::Initialize(const EditorStartupConfiguration& Configuration) noexcept
{
    Finalize();

    StartupConfig        = Configuration;
    TerminationRequested = false;
    CurrentStepSeconds   = 1.0 / 60.0;

    if (!DockContainer.Initialize(StartupConfig.WindowDescriptor))
    {
        return false;
    }

    InputParticipant.Initialize();

    if (!Workspaces.Initialize(false))
    {
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    WorkspaceDocumentDescriptor DefaultDocument{};
    DefaultDocument.DocumentTitle   = "Scene";
    DefaultDocument.OutlinerVariant = StartupConfig.OutlinerHeading;
    DefaultDocument.InitialMode     = ViewportExecutionMode::Edit;
    if (!Workspaces.OpenSession(DefaultDocument).Valid())
    {
        Workspaces.Finalize();
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (StartupConfig.ProjectSpacePath.empty())
    {
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();
    }
    else
    {
        (void)ControlCentre.AttachProjectSpace(StartupConfig.ProjectSpacePath);
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& Io = ImGui::GetIO();
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        if (!StartupConfig.FontRepositoryRoot.empty() && Typography.LoadRepository(StartupConfig.FontRepositoryRoot) > 0u)
        {
            TypographyRepository::AssignCurrent(&Typography);
            ControlCentre.AttachTypefaces(&Typography);
            if (ImFont* TabFont = Typography.QueryTabStripFont())
            {
                Io.FontDefault = TabFont;
            }
        }

        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    ControlCentre.ConfigureWorkspaceCallbacks(
        [this](bool Allowed) noexcept
        {
            DockContainer.ConfigureDetachedWindowsAllowed(Allowed);
        },
        [this]() noexcept
        {
            DockContainer.ResetPrimaryLayout();
        });
    DockContainer.ConfigureDetachedWindowsAllowed(ControlCentre.QueryDetachedWindowsAllowed());

    const char* LeftPaneTitle = (StartupConfig.OutlinerHeading == OutlinerTitleVariant::LayerStack) ? "LayerStack" : "Outliner";

    Components::WorkspaceDockPaneDescriptor LeftDesc{};
    LeftDesc.PaneTitle         = LeftPaneTitle;
    LeftDesc.InitialDirection  = Components::DockPlacementDirection::SplitLeft;
    LeftDesc.InitialSplitRatio = 0.21f;
    LeftDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    LeftDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Outliner.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    OutlinerPaneToken = DockContainer.RegisterPane(LeftDesc);

    Components::WorkspaceDockPaneDescriptor CenterDesc{};
    CenterDesc.PaneTitle         = "Viewport";
    CenterDesc.InitialDirection  = Components::DockPlacementDirection::CenterTabGroup;
    CenterDesc.InitialSplitRatio = 0.58f;
    CenterDesc.Directives        = Components::PaneDirectiveMask::CentralViewportDirectives;
    CenterDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Viewport.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    ViewportPaneToken = DockContainer.RegisterPane(CenterDesc);

    Components::WorkspaceDockPaneDescriptor RightDesc{};
    RightDesc.PaneTitle         = "Inspector";
    RightDesc.InitialDirection  = Components::DockPlacementDirection::SplitRight;
    RightDesc.InitialSplitRatio = 0.21f;
    RightDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    RightDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Inspector.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    InspectorPaneToken = DockContainer.RegisterPane(RightDesc);

    if (!EditorSequence.Initialize())
    {
        Workspaces.Finalize();
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        Components::ConfigureImGuiWorkspaceStyle(DockContainer.ActiveTabMetrics());
        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    Initialized = true;
    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        std::vector<OutlinerConstructTemplate> Templates;
        Templates.reserve(8u);
        Templates.push_back({ "Add Box Geometry",      "Box Specimen",      "Geometry", "Box · OpenPBR Surface",      ConstructSlotCategory::GeometryVolume   });
        Templates.push_back({ "Add Sphere Geometry",   "Sphere Specimen",   "Geometry", "Sphere · OpenPBR Surface",   ConstructSlotCategory::GeometryVolume   });
        Templates.push_back({ "Add Cylinder Geometry", "Cylinder Specimen", "Geometry", "Cylinder · OpenPBR Surface", ConstructSlotCategory::GeometryVolume   });
        Templates.push_back({ "Add Cone Geometry",     "Cone Specimen",     "Geometry", "Cone · OpenPBR Surface",     ConstructSlotCategory::GeometryVolume   });
        Templates.push_back({ "Add Torus Geometry",    "Torus Specimen",    "Geometry", "Torus · OpenPBR Surface",    ConstructSlotCategory::GeometryVolume   });
        Templates.push_back({ "Add Area Light",        "Area Quad Light",   "Lighting", "LTC Polygonal Quad Light",   ConstructSlotCategory::PointLuminaire   });
        Templates.push_back({ "Add Scene Camera",      "Scene Camera",      "Camera",   "Perspective Optical Camera", ConstructSlotCategory::OpticsCamera     });
        Panes->Outliner.ConfigureConstructTemplates(std::move(Templates));

        Panes->Viewport.ConfigureOpenContentBrowserCallback(
            [this]()
            {
                ContentBrowserDrawer.AccessPanel().ConfigureOriginFilter(ContentOriginFilter::All);
                (void)ContentBrowserDrawer.AccessPanel().ActivateCategory(ContentCategory::Geometry);
                ContentBrowserDrawer.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
            });

        ContentBrowserDrawer.AccessPanel().AssignContentActivationCallback(
            [this](const ContentRepositoryEntry& Entry)
            {
                const auto Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
                if (Category == ContentCategory::Geometry && ShowcaseBundle != nullptr)
                {
                    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
                    {
                        GeometricRaster::ShowcasePrimitiveShape Shape = GeometricRaster::ShowcasePrimitiveShape::Box;
                        if (Entry.DisplayTitle.find("Sphere") != std::string::npos || Entry.DisplayTitle.find("Orb") != std::string::npos)
                        {
                            Shape = GeometricRaster::ShowcasePrimitiveShape::Sphere;
                        }
                        else if (Entry.DisplayTitle.find("Cylinder") != std::string::npos)
                        {
                            Shape = GeometricRaster::ShowcasePrimitiveShape::Cylinder;
                        }
                        else if (Entry.DisplayTitle.find("Cone") != std::string::npos)
                        {
                            Shape = GeometricRaster::ShowcasePrimitiveShape::Cone;
                        }
                        else if (Entry.DisplayTitle.find("Torus") != std::string::npos)
                        {
                            Shape = GeometricRaster::ShowcasePrimitiveShape::Torus;
                        }
                        const auto Pos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.72f, 0.36f);
                        const bool GpuActive = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
                        (void)GeometricRaster::ConstructShowcasePrimitiveInOutliner(
                            ActivePanes->Outliner,
                            *ShowcaseBundle,
                            Shape,
                            Pos);
                        if (GpuActive)
                        {
                            GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, true);
                        }
                        else if (ShowcaseRenderer != nullptr)
                        {
                            ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                        }
                        RefreshShowcaseViewportRaster();
                    }
                }
            });
    }
    if (StartupConfig.VulkanDevice != nullptr && !StartupConfig.CompiledShaderDirectory.empty())
    {
        (void)AttachVulkanGizmoOverlay(StartupConfig.VulkanDevice, StartupConfig.CompiledShaderDirectory);
    }
    if (StartupConfig.PopulateShowcaseScene)
    {
        PopulateShowcaseGallerySession();
    }
    return true;
}

bool EditorLifecycle::AttachVulkanGizmoOverlay(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    if (GpuWorldRenderer == nullptr || !GpuWorldRenderer->QueryReady())
    {
        auto CandidateWorldGpu = std::make_unique<GeometricRaster::VulkanWorldRenderSequence>();
        if (CandidateWorldGpu->InitializeSequence(*BoundDevice, CompiledShaderDirectory))
        {
            GpuWorldRenderer = std::move(CandidateWorldGpu);
            if (ShowcaseBundle != nullptr)
            {
                GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, true);
            }
        }
    }

    if (WorldGizmoRasterizer == nullptr || !WorldGizmoRasterizer->QueryReady())
    {
        WorldGizmoRasterizer = std::make_unique<GeometricRaster::WorldGizmoOverlayRasterization>();
        if (!WorldGizmoRasterizer->InitializeOverlay(BoundDevice, CompiledShaderDirectory))
        {
            WorldGizmoRasterizer.reset();
            return false;
        }
    }

    WorldTransformGizmo.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
    if (ShowcaseBundle != nullptr)
    {
        RefreshShowcaseViewportRaster();
    }
    return true;
}

void EditorLifecycle::AssignReturnToProjectBrowserCallback(std::function<void()> Callback)
{
    ReturnToBrowserCallback = std::move(Callback);
}

void EditorLifecycle::ConfigureActiveProjectTitle(std::string_view ProjectTitle)
{
    ActiveProjectTitle = std::string(ProjectTitle);
    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        if (!ActiveProjectTitle.empty())
        {
            Panes->Viewport.ConfigureSceneTitle(ActiveProjectTitle + " · Cornell-Studio Gallery");
        }
    }
}

void EditorLifecycle::PopulateShowcaseGallerySession(uint32_t RenderWidthPx, uint32_t RenderHeightPx)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    ShowcaseBundle     = std::make_unique<GeometricRaster::ShowcaseWorldSceneBundle>();
    ShowcaseRenderer   = std::make_unique<GeometricRaster::TieredWorldRenderSequence>();
    CachedWorldRgba8.clear();
    CachedWorldWidthPx  = 0u;
    CachedWorldHeightPx = 0u;
    WorldTransformGizmo.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);

    const bool GpuActive = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    GeometricRaster::BuildShowcaseGalleryScene(*ShowcaseBundle, RenderWidthPx, RenderHeightPx);
    {
        const auto& DisplayCfg = Panes->Viewport.QueryDisplaySettings();
        auto& Atmo = ShowcaseBundle->Scene.Atmosphere;
        Atmo.Medium.AtmosphereEnabled  = DisplayCfg.AtmosphereSkyEnabled;
        Atmo.Clouds.CloudsEnabled      = DisplayCfg.VolumetricCloudsEnabled;
        Atmo.Clouds.CloudFraction      = DisplayCfg.CloudFraction;
        Atmo.Clouds.CloudShadow.ShadowEnabled      = DisplayCfg.CloudShadowsEnabled;
        Atmo.Clouds.WindSpeedMetersPerSecond       = DisplayCfg.CloudWindSpeedMps;
        Atmo.Wind.SpeedMetersPerSecond             = std::max(Atmo.Wind.SpeedMetersPerSecond, DisplayCfg.CloudWindSpeedMps);
        Atmo.Stars.StarfieldEnabled    = DisplayCfg.CelestialStarsEnabled;
        Atmo.Solar.SolarEnabled        = DisplayCfg.CelestialStarsEnabled;
        Atmo.Moons[0].Visible          = DisplayCfg.CelestialStarsEnabled;
        Atmo.Solar.CycleAnimationEnabled = DisplayCfg.DayCyclePlaying;
        Atmo.Solar.LocalHours          = DisplayCfg.SolarTimeOfDayHours;
        if (Atmo.Medium.AtmosphereEnabled)
        {
            ShowcaseRenderer->SynchronizeWorldAtmosphere(ShowcaseBundle->Scene);
        }
    }
    if (GpuActive)
    {
        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, true);
    }
    else
    {
        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
    }
    GeometricRaster::RegisterShowcaseGalleryInOutliner(Panes->Outliner, *ShowcaseBundle, true);
    Panes->Viewport.ConfigureSceneTitle(
        ActiveProjectTitle.empty() ? "Sky & Plane Studio" : (ActiveProjectTitle + " · Sky & Plane Studio"));

    Panes->Viewport.ConfigureInteractionCallback(
        [this](uint32_t PaneIdx, Rectangle2D PaneBounds, const PaneInputReading& Input)
        {
            ProcessShowcaseViewportInteraction(PaneIdx, PaneBounds, Input);
        });
    Panes->Viewport.AssignNavigationCallback(
        [this](ViewportNavigationDirective Directive)
        {
            ProcessShowcaseNavigationDirective(Directive);
        });
    Panes->Viewport.ConfigureConstructCanvasDropCallback(
        [this](int32_t TemplateIndex, int32_t /*PresetIndex*/, Point2D CanvasLogicalPos)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            const Rectangle2D CanvasBounds = ActivePanes->Viewport.QueryLastCanvasPaneBounds();
            const float U = std::clamp((CanvasLogicalPos.X - CanvasBounds.X) / std::max(1.0f, CanvasBounds.Width), 0.0f, 1.0f);
            const float V = std::clamp((CanvasLogicalPos.Y - CanvasBounds.Y) / std::max(1.0f, CanvasBounds.Height), 0.0f, 1.0f);
            const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.36f);

            if (TemplateIndex >= 0)
            {
                const auto Templates = ActivePanes->Outliner.QueryConstructTemplates();
                if (static_cast<std::size_t>(TemplateIndex) < Templates.size())
                {
                    const auto& Tpl = Templates[static_cast<std::size_t>(TemplateIndex)];
                    const std::string_view Title = Tpl.DefaultTitle.empty() ? std::string_view(Tpl.DropdownLabel) : std::string_view(Tpl.DefaultTitle);
                    (void)GeometricRaster::ConstructShowcaseModelInOutliner(
                        ActivePanes->Outliner,
                        *ShowcaseBundle,
                        Title,
                        { 212u, 208u, 200u, 255u },
                        DropPos,
                        {});
                    SynchronizeShowcaseRendererAfterEdit(true);
                }
            }
            else
            {
                (void)ActivePanes->Outliner.ConstructFolder("Folder");
            }
        });

    auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
    BrowserPanel.AttachOutlinerFallback(&Panes->Outliner);

    Panes->Inspector.ConfigureCustomSectionCallback(
        [this](
            PixelSpace&             Surface,
            Rectangle2D             ScrollViewport,
            float                   LeftX,
            float                   TopY,
            float                   InnerWidth,
            OutlinerRecordEntry&    Entry,
            const PaneInputReading& Input) -> float
        {
            (void)ScrollViewport;
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return TopY;
            }
            const bool IsCamera = (Entry.Token == ShowcaseBundle->EditorCameraToken) ||
                                  (Entry.Category == ConstructSlotCategory::OpticsCamera);
            if (!IsCamera)
            {
                return TopY;
            }

            auto& Disp = ActivePanes->Viewport.AccessDisplaySettings();
            const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            const auto HitTest = [&](Rectangle2D R) -> bool
            {
                return Input.PrimaryPointer.Pressed && R.Contains(Cursor);
            };

            float Y = TopY + 10.0f;

            // Card 1: Render Settings (mirrors Inspectors.jsx Card + Control)
            const float Card1H = 186.0f;
            const Rectangle2D Card1{ LeftX, Y, InnerWidth, Card1H };
            Surface.FillRectangle(Card1, WorkspaceSurfaceColors::SectionSurface, 20.0f);
            Surface.StrokeRectangle(Card1, WorkspaceSurfaceColors::HairlineLight, 20.0f, 1.0f);

            float CY = Y + 14.0f;
            const float CX = LeftX + 14.0f;
            const float CW = InnerWidth - 28.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CX, CY }, 11.0f, WorkspaceSurfaceColors::PrimaryInk, "Render Settings");
            CY += 24.0f;

            static constexpr const char* kEngines[6] = { "MatCap", "OpenPBR Direct", "GTX SDF GI", "HighEnd BVH4", "Game Realtime", "Scene Final" };
            const uint32_t EngIdx = (Disp.RenderEngineIndex < 6u) ? Disp.RenderEngineIndex : 2u;
            const Rectangle2D EngRow{ CX, CY, CW, 26.0f };
            Surface.FillRectangle(EngRow, WorkspaceSurfaceColors::ControlWell, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX + 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::SecondaryInk, "Engine");
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { CX + CW - 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::PrimaryInk, kEngines[EngIdx]);
            if (HitTest(EngRow)) { Disp.RenderEngineIndex = (EngIdx + 1u) % 6u; }
            CY += 32.0f;

            static constexpr const char* kSdfModes[6] = { "0. Point SDF", "1. Analytic Cone", "2. Voxel Clipmap", "3. Voxel + Cone GI", "4. Hybrid Sobol", "5. MC PT Ref" };
            const uint32_t SdfIdx = (Disp.SdfShadowPenumbraIndex < 6u) ? Disp.SdfShadowPenumbraIndex : 3u;
            const Rectangle2D SdfRow{ CX, CY, CW, 26.0f };
            Surface.FillRectangle(SdfRow, WorkspaceSurfaceColors::ControlWell, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX + 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::SecondaryInk, "SDF Shadow");
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { CX + CW - 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::PrimaryInk, kSdfModes[SdfIdx]);
            if (HitTest(SdfRow)) { Disp.SdfShadowPenumbraIndex = (SdfIdx + 1u) % 6u; }
            CY += 32.0f;

            static constexpr const char* kQuals[5] = { "Low", "Med", "Std", "High", "Ultra" };
            const uint32_t QIdx = static_cast<uint32_t>(Disp.QualityTier) % 5u;
            const float SegW = (CW - 8.0f) / 5.0f;
            for (uint32_t Q = 0u; Q < 5u; ++Q)
            {
                const Rectangle2D Seg{ CX + static_cast<float>(Q) * (SegW + 2.0f), CY, SegW, 24.0f };
                Surface.FillRectangle(Seg, (Q == QIdx) ? ColorRgba{ 46, 50, 57, 255 } : WorkspaceSurfaceColors::ControlWell, 12.0f);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Seg.X + 8.0f, Seg.Y + 6.0f }, 9.0f,
                    (Q == QIdx) ? WorkspaceSurfaceColors::PrimaryInk : WorkspaceSurfaceColors::SecondaryInk, kQuals[Q]);
                if (HitTest(Seg)) { Disp.QualityTier = static_cast<decltype(Disp.QualityTier)>(Q); }
            }
            CY += 30.0f;

            const Rectangle2D RtRow{ CX, CY, CW, 24.0f };
            Surface.FillRectangle(RtRow, WorkspaceSurfaceColors::ControlWell, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX + 10.0f, CY + 5.0f }, 9.5f, WorkspaceSurfaceColors::SecondaryInk, "Raytracing");
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { CX + CW - 10.0f, CY + 5.0f }, 9.5f,
                Disp.RaytracingEnabled ? ColorRgba{ 74, 222, 128, 255 } : WorkspaceSurfaceColors::SecondaryInk, Disp.RaytracingEnabled ? "ON" : "OFF");
            if (HitTest(RtRow)) { Disp.RaytracingEnabled = !Disp.RaytracingEnabled; }

            Y += Card1H + 12.0f;

            // Card 2: Sky, Sun, Moon & Clouds
            const float Card2H = 132.0f;
            const Rectangle2D Card2{ LeftX, Y, InnerWidth, Card2H };
            Surface.FillRectangle(Card2, WorkspaceSurfaceColors::SectionSurface, 20.0f);
            Surface.StrokeRectangle(Card2, WorkspaceSurfaceColors::HairlineLight, 20.0f, 1.0f);

            CY = Y + 14.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { CX, CY }, 11.0f, WorkspaceSurfaceColors::PrimaryInk, "Sky, Sun, Moon & Clouds");
            CY += 24.0f;

            char SunBuf[24];
            std::snprintf(SunBuf, sizeof(SunBuf), "%.1f h", Disp.SolarTimeOfDayHours);
            const Rectangle2D SunRow{ CX, CY, CW, 26.0f };
            Surface.FillRectangle(SunRow, WorkspaceSurfaceColors::ControlWell, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX + 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::SecondaryInk, "Solar Time");
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { CX + CW - 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::PrimaryInk, SunBuf);
            if (HitTest(SunRow)) { Disp.SolarTimeOfDayHours = (Disp.SolarTimeOfDayHours >= 22.0f) ? 6.0f : (Disp.SolarTimeOfDayHours + 2.0f); }
            CY += 32.0f;

            char CloudBuf[24];
            std::snprintf(CloudBuf, sizeof(CloudBuf), "%.0f%%", Disp.CloudFraction * 100.0f);
            const Rectangle2D CloudRow{ CX, CY, CW, 26.0f };
            Surface.FillRectangle(CloudRow, WorkspaceSurfaceColors::ControlWell, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CX + 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::SecondaryInk, "Cloud Cover");
            WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Medium, { CX + CW - 10.0f, CY + 6.0f }, 9.5f, WorkspaceSurfaceColors::PrimaryInk, CloudBuf);
            if (HitTest(CloudRow))
            {
                Disp.CloudFraction = (Disp.CloudFraction >= 0.80f) ? 0.0f : (Disp.CloudFraction + 0.20f);
                Disp.VolumetricCloudsEnabled = (Disp.CloudFraction > 0.01f);
            }

            return Y + Card2H + 10.0f;
        });
    BrowserPanel.AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            if (Entry.IndexHeader.MatCapPresetMarker != 0u)
            {
                ActivePanes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                    static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap);
                ActivePanes->Viewport.AccessDisplaySettings().DebugChannelIndex =
                    (Entry.IndexHeader.MatCapSlotOrdinal == 9u) ? 3u : 0u;
                RefreshShowcaseViewportRaster();
                return;
            }
            const Rectangle2D DropBounds = ContentBrowserDrawer.QueryPanel().MapPhysicalToLogicalBounds(
                ActivePanes->Viewport.QueryLastCanvasPhysicalBounds());
            HandleContentDropOnViewport(
                Entry,
                { DropBounds.X + DropBounds.Width * 0.5f, DropBounds.Y + DropBounds.Height * 0.62f },
                DropBounds);
        });

    SynchronizeShowcaseGizmoAnchor();
    RefreshShowcaseViewportRaster();
}

void EditorLifecycle::SynchronizeShowcaseGizmoAnchor() noexcept
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const OutlinerSlotToken PrimarySlot = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Entry    = PrimarySlot.Valid() ? Panes->Outliner.FindEntry(PrimarySlot) : nullptr;
    if (Entry == nullptr ||
        Entry->Category == ConstructSlotCategory::Folder ||
        Entry->Token == ShowcaseBundle->EditorCameraToken ||
        Entry->Token == ShowcaseBundle->SunEmitterToken ||
        Entry->Locked)
    {
        LastGizmoSlot = {};
        return;
    }

    const Mathematics::Axis3D Pivot{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
    const float DistToCam = std::sqrt(
        (Pivot.X - ShowcaseBundle->EditorCamera.CameraOrigin.X) * (Pivot.X - ShowcaseBundle->EditorCamera.CameraOrigin.X) +
        (Pivot.Y - ShowcaseBundle->EditorCamera.CameraOrigin.Y) * (Pivot.Y - ShowcaseBundle->EditorCamera.CameraOrigin.Y) +
        (Pivot.Z - ShowcaseBundle->EditorCamera.CameraOrigin.Z) * (Pivot.Z - ShowcaseBundle->EditorCamera.CameraOrigin.Z));
    const float WorldRad = std::clamp(DistToCam * 0.17f, 0.28f, 1.85f);
    WorldTransformGizmo.ConfigureAnchor(Pivot, WorldRad);
    LastGizmoSlot = PrimarySlot;
}

void EditorLifecycle::FrameShowcaseCameraSelection() noexcept
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const OutlinerSlotToken    Primary = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Entry   = Primary.Valid() ? Panes->Outliner.FindEntry(Primary) : nullptr;
    if (Entry != nullptr &&
        Entry->Category != ConstructSlotCategory::Folder &&
        Entry->Token != ShowcaseBundle->EditorCameraToken &&
        Entry->Token != ShowcaseBundle->SunEmitterToken)
    {
        const Mathematics::Axis3D TargetPos{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
        const float MaxExtent = std::max(0.45f, std::max({ std::fabs(Entry->ScaleX), std::fabs(Entry->ScaleY), std::fabs(Entry->ScaleZ) }));
        const Mathematics::Axis3D ViewOffset = Mathematics::NormalizeAxis(
            ShowcaseBundle->EditorCamera.CameraOrigin - TargetPos,
            { 0.0f, -1.0f, 0.12f });
        const float Dist = std::clamp(MaxExtent * 2.25f, 1.45f, 7.0f);
        ShowcaseBundle->EditorCamera.TargetPoint  = TargetPos;
        ShowcaseBundle->EditorCamera.CameraOrigin = TargetPos + ViewOffset * Dist;
    }
    else
    {
        ShowcaseBundle->EditorCamera.CameraOrigin = { 0.00f, -3.55f, 0.42f };
        ShowcaseBundle->EditorCamera.TargetPoint  = { 0.00f,  0.55f, 0.15f };
    }

    if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
    {
        CamEntry->PositionX = ShowcaseBundle->EditorCamera.CameraOrigin.X;
        CamEntry->PositionY = ShowcaseBundle->EditorCamera.CameraOrigin.Y;
        CamEntry->PositionZ = ShowcaseBundle->EditorCamera.CameraOrigin.Z;
    }
    SynchronizeShowcaseGizmoAnchor();
    PendingInteractionWorldRefresh = true;
}

void EditorLifecycle::ProcessShowcaseNavigationDirective(ViewportNavigationDirective Directive)
{
    if (ShowcaseBundle == nullptr)
    {
        return;
    }

    if (Directive == ViewportNavigationDirective::FitView)
    {
        FrameShowcaseCameraSelection();
        return;
    }

    const Mathematics::Axis3D ToTarget = ShowcaseBundle->EditorCamera.TargetPoint - ShowcaseBundle->EditorCamera.CameraOrigin;
    const float               CurDist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(ToTarget, ToTarget)));
    const Mathematics::Axis3D Fwd      = Mathematics::NormalizeAxis(ToTarget, { 0.0f, 1.0f, 0.0f });
    const float               NextDist = (Directive == ViewportNavigationDirective::ZoomIn)
                                             ? std::clamp(CurDist * 0.85f, 0.55f, 18.0f)
                                             : std::clamp(CurDist * 1.18f, 0.55f, 18.0f);
    ShowcaseBundle->EditorCamera.CameraOrigin = ShowcaseBundle->EditorCamera.TargetPoint - Fwd * NextDist;

    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
        {
            CamEntry->PositionX = ShowcaseBundle->EditorCamera.CameraOrigin.X;
            CamEntry->PositionY = ShowcaseBundle->EditorCamera.CameraOrigin.Y;
            CamEntry->PositionZ = ShowcaseBundle->EditorCamera.CameraOrigin.Z;
        }
    }
    SynchronizeShowcaseGizmoAnchor();
    PendingInteractionWorldRefresh = true;
}

void EditorLifecycle::ProcessShowcaseViewportInteraction(
    uint32_t                PaneIndex,
    Rectangle2D             PaneBounds,
    const PaneInputReading& Input)
{
    (void)PaneIndex;
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || PaneBounds.Width < 16.0f || PaneBounds.Height < 16.0f)
    {
        return;
    }

    ImGuiIO& Io = ImGui::GetIO();
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float DeltaX = PointerTrackingValid ? (Cursor.X - LastPointerPosition.X) : 0.0f;
    const float DeltaY = PointerTrackingValid ? (Cursor.Y - LastPointerPosition.Y) : 0.0f;
    LastPointerPosition  = Cursor;
    PointerTrackingValid = true;

    const float NormU = std::clamp((Cursor.X - PaneBounds.X) / PaneBounds.Width, 0.0f, 1.0f);
    const float NormV = std::clamp((Cursor.Y - PaneBounds.Y) / PaneBounds.Height, 0.0f, 1.0f);

    bool CameraMoved  = false;
    bool SceneChanged = false;

    auto& Cam = ShowcaseBundle->EditorCamera;
    Mathematics::Axis3D Fwd   = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });

    // 1. Z-up (+Z) Camera Navigation: RMB mouse-look + WASD, MMB pan, Alt+LMB orbit, Scroll wheel dolly
    const bool RightHeld  = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool MiddleHeld = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool AltHeld    = Io.KeyAlt;

    CameraInteractiveDrag = RightHeld || MiddleHeld || (AltHeld && Input.PrimaryPointer.Down) || WorldTransformGizmo.QueryDragging();

    if (RightHeld && !Io.KeyShift)
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const float Dist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(Cam.TargetPoint - Cam.CameraOrigin, Cam.TargetPoint - Cam.CameraOrigin)));
            float       Yaw   = std::atan2(Fwd.X, Fwd.Y) + DeltaX * 0.0042f;
            float       Pitch = std::asin(std::clamp(Fwd.Z, -0.98f, 0.98f)) - DeltaY * 0.0042f;
            Pitch             = std::clamp(Pitch, -1.42f, 1.42f);
            const float CosP  = std::cos(Pitch);
            Fwd               = Mathematics::NormalizeAxis({ std::sin(Yaw) * CosP, std::cos(Yaw) * CosP, std::sin(Pitch) }, Fwd);
            Cam.TargetPoint   = Cam.CameraOrigin + Fwd * Dist;
            Right             = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
            Up                = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });
            CameraMoved       = true;
        }

        const float FlySpeed = (Io.KeyShift ? 6.0f : 2.8f) * static_cast<float>(std::max(0.001, CurrentStepSeconds));
        Mathematics::Axis3D MoveVec { 0.0f, 0.0f, 0.0f };
        if (ImGui::IsKeyDown(ImGuiKey_W)) MoveVec = MoveVec + Fwd * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) MoveVec = MoveVec - Fwd * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) MoveVec = MoveVec + Right * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) MoveVec = MoveVec - Right * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) MoveVec = MoveVec + Cam.WorldUpAxis * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) MoveVec = MoveVec - Cam.WorldUpAxis * FlySpeed;
        if (Mathematics::EvaluateDot(MoveVec, MoveVec) > 1.0e-8f)
        {
            Cam.CameraOrigin = Cam.CameraOrigin + MoveVec;
            Cam.TargetPoint  = Cam.TargetPoint + MoveVec;
            CameraMoved      = true;
        }
    }
    else if (MiddleHeld || (RightHeld && Io.KeyShift))
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const float PanScale = 0.0038f;
            const Mathematics::Axis3D PanOffset = Right * (-DeltaX * PanScale) + Up * (DeltaY * PanScale);
            Cam.CameraOrigin = Cam.CameraOrigin + PanOffset;
            Cam.TargetPoint  = Cam.TargetPoint + PanOffset;
            CameraMoved      = true;
        }
    }
    else if (AltHeld && Input.PrimaryPointer.Down)
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const Mathematics::Axis3D Offset = Cam.CameraOrigin - Cam.TargetPoint;
            const float Dist  = std::max(0.45f, std::sqrt(Mathematics::EvaluateDot(Offset, Offset)));
            float       Yaw   = std::atan2(Offset.X, Offset.Y) - DeltaX * 0.0055f;
            float       Pitch = std::asin(std::clamp(Offset.Z / Dist, -0.98f, 0.98f)) + DeltaY * 0.0055f;
            Pitch             = std::clamp(Pitch, -1.35f, 1.35f);
            const float CosP  = std::cos(Pitch);
            Cam.CameraOrigin  = Cam.TargetPoint + Mathematics::Axis3D{ std::sin(Yaw) * CosP * Dist, std::cos(Yaw) * CosP * Dist, std::sin(Pitch) * Dist };
            CameraMoved       = true;
        }
    }

    if (std::fabs(Input.WheelDeltaY) > 0.01f)
    {
        const Mathematics::Axis3D ToTarget = Cam.TargetPoint - Cam.CameraOrigin;
        const float               CurDist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(ToTarget, ToTarget)));
        const float               Step     = std::clamp(CurDist * 0.12f, 0.12f, 1.25f) * Input.WheelDeltaY;
        const float               NextDist = std::clamp(CurDist - Step, 0.50f, 20.0f);
        Cam.CameraOrigin                   = Cam.TargetPoint - Fwd * NextDist;
        CameraMoved                        = true;
    }

    // 2. Gizmo & Frame-Selection Keyboard Shortcuts (when not flying RMB and not typing in text fields)
    bool GizmoOverlayOnly = false;
    bool GizmoDragEnded   = false;
    if (!RightHeld && !Io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::TranslateOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::RotateOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::ScaleOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_U, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            FrameShowcaseCameraSelection();
            return;
        }
    }

    // 3. GPU Gizmo Hover / Drag & 3D Ray Picking
    const float Aspect  = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    const float TanHalf = std::tan(std::clamp(Cam.VerticalFovRadians, 0.10f, 2.60f) * 0.5f);
    const float NdcX    = (NormU * 2.0f - 1.0f) * Aspect * TanHalf;
    const float NdcY    = (1.0f - NormV * 2.0f) * TanHalf;
    const Editor::ViewportRay3D CursorRay{
        Cam.CameraOrigin,
        Mathematics::NormalizeAxis(Fwd + Right * NdcX + Up * NdcY, Fwd),
    };

    if (WorldTransformGizmo.QueryDragging())
    {
        if (Input.PrimaryPointer.Down && LastGizmoSlot.Valid())
        {
            Configuration::GizmoSnapSettings SnapCfg {};
            const auto Delta = WorldTransformGizmo.EvaluateDragInteraction(CursorRay, Io.KeyCtrl, SnapCfg, GizmoActiveTransform);
            if (Delta.TransformChanged)
            {
                if (OutlinerRecordEntry* SelectedEntry = Panes->Outliner.AccessEntry(LastGizmoSlot))
                {
                    SelectedEntry->PositionX = GizmoActiveTransform.Position.X;
                    SelectedEntry->PositionY = GizmoActiveTransform.Position.Y;
                    SelectedEntry->PositionZ = GizmoActiveTransform.Position.Z;
                    SelectedEntry->ScaleX    = std::max(0.08f, GizmoActiveTransform.Scale.X);
                    SelectedEntry->ScaleY    = std::max(0.08f, GizmoActiveTransform.Scale.Y);
                    SelectedEntry->ScaleZ    = std::max(0.08f, GizmoActiveTransform.Scale.Z);
                    SelectedEntry->RotationZ += Delta.RotationDegrees.Z;
                    WorldTransformGizmo.ConfigureAnchor(GizmoActiveTransform.Position, WorldTransformGizmo.QueryWorldRadius());
                    SceneChanged = true;
                }
            }
        }
        else
        {
            WorldTransformGizmo.EndDragInteraction();
            CameraInteractiveDrag = false;
            GizmoDragEnded        = true;
            SceneChanged          = true;
        }
    }
    else if (!AltHeld && !RightHeld && !MiddleHeld)
    {
        Editor::GizmoHandleOrdinal Hovered = Editor::GizmoHandleOrdinal::None;
        if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
        {
            const uint32_t PixX = static_cast<uint32_t>(NormU * static_cast<float>(std::max(1u, Cam.ViewportWidthPx) - 1u));
            const uint32_t PixY = static_cast<uint32_t>(NormV * static_cast<float>(std::max(1u, Cam.ViewportHeightPx) - 1u));
            Hovered             = WorldGizmoRasterizer->QueryLastGpuPickAtPixel(PixX, PixY);
        }
        if (Hovered != WorldTransformGizmo.QueryHoveredHandle())
        {
            WorldTransformGizmo.AssignHoveredHandle(Hovered);
            GizmoOverlayOnly = true;
        }

        if (Input.PrimaryPointer.Pressed)
        {
            if (Hovered != Editor::GizmoHandleOrdinal::None && LastGizmoSlot.Valid())
            {
                if (const OutlinerRecordEntry* SelectedEntry = Panes->Outliner.FindEntry(LastGizmoSlot))
                {
                    GizmoActiveTransform          = {};
                    GizmoActiveTransform.Position = { SelectedEntry->PositionX, SelectedEntry->PositionY, SelectedEntry->PositionZ };
                    GizmoActiveTransform.Scale    = { SelectedEntry->ScaleX, SelectedEntry->ScaleY, SelectedEntry->ScaleZ };
                    (void)WorldTransformGizmo.BeginDragInteraction(Hovered, CursorRay, GizmoActiveTransform, Right, Up);
                    CameraInteractiveDrag = true;
                    GizmoOverlayOnly      = true;
                }
            }
            else
            {
                const OutlinerSlotToken Picked = GeometricRaster::PickShowcaseGalleryEntryAtUv(
                    Panes->Outliner,
                    *ShowcaseBundle,
                    NormU,
                    NormV);
                if (Picked.Valid() && Picked != Panes->Outliner.QueryPrimarySelection())
                {
                    Panes->Outliner.SelectSingle(Picked);
                    SynchronizeShowcaseGizmoAnchor();
                    GizmoOverlayOnly = true;
                }
            }
        }
    }

    if (CameraMoved)
    {
        if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
        {
            CamEntry->PositionX = Cam.CameraOrigin.X;
            CamEntry->PositionY = Cam.CameraOrigin.Y;
            CamEntry->PositionZ = Cam.CameraOrigin.Z;
        }
        SynchronizeShowcaseGizmoAnchor();
    }

    if (CameraMoved || SceneChanged)
    {
        PendingInteractionWorldRefresh = true;
        PendingInteractionSceneSync    = true;
        PendingInteractionFastDrag     = WorldTransformGizmo.QueryDragging() && !GizmoDragEnded;
    }
    else if (GizmoOverlayOnly)
    {
        PendingInteractionGizmoRefresh = true;
    }
}

OutlinerSlotToken EditorLifecycle::SpawnComponentClassPlacement(
    std::string_view             ClassTitle,
    Mathematics::Axis3D          WorldPosition,
    OutlinerSlotToken            ExplicitEnclosure,
    const std::filesystem::path& OptionalAssetPath)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return {};
    }

    uint32_t ClassIndex = UINT32_MAX;
    for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(ComponentDefinitions.size()); ++Idx)
    {
        if (ComponentDefinitions[Idx].Title == ClassTitle)
        {
            ClassIndex = Idx;
            break;
        }
    }

    if (ClassIndex == UINT32_MAX)
    {
        ComponentClass::ComponentClassDefinition Def {};
        bool Loaded = false;
        if (!OptionalAssetPath.empty() && std::filesystem::exists(OptionalAssetPath))
        {
            std::string Diag {};
            Loaded = Codecs::ComponentSpaceDecoder::DecodeFromFile(OptionalAssetPath, Def, Diag);
        }
        if (!Loaded)
        {
            std::filesystem::path ModelsDir = std::filesystem::path("Experimental/Shared/Models");
            if (!std::filesystem::exists(ModelsDir) && !StartupConfig.FontRepositoryRoot.empty())
            {
                ModelsDir = StartupConfig.FontRepositoryRoot.parent_path().parent_path() / "Experimental/Shared/Models";
            }
            ComponentClass::ControlVehicleBuildResult BuildRes {};
            std::string Diag {};
            if (ComponentClass::ControlVehicleClassBuilder::ConstructControlVehicle(ModelsDir, BuildRes, Diag))
            {
                Def = std::move(BuildRes.ClassDefinition);
                if (!ClassTitle.empty() && ClassTitle != "ControlVehicle")
                {
                    Def.Title = std::string(ClassTitle);
                }
                Loaded = true;
            }
        }
        if (!Loaded)
        {
            return {};
        }

        const auto ValidationIssues = ComponentClass::ComponentClassValidator::ValidateClass(Def);
        bool HasErrors = false;
        for (const auto& Issue : ValidationIssues)
        {
            if (Issue.Severity == ComponentClass::DiagnosticSeverity::Error)
            {
                HasErrors = true;
                break;
            }
        }
        if (!HasErrors)
        {
            Def.FinalizeCompiledRevision();
        }
        ClassIndex = static_cast<uint32_t>(ComponentDefinitions.size());
        ComponentDefinitions.push_back(std::move(Def));
    }

    const ComponentClass::ComponentClassDefinition& ClassDef = ComponentDefinitions[ClassIndex];
    const uint32_t PlacementOrdinal = WorldPlacements.QueryLivePlacementCount() + 1u;
    char TitleBuf[128]{};
    std::snprintf(TitleBuf, sizeof(TitleBuf), "%s_%02u", ClassDef.Title.c_str(), PlacementOrdinal);

    const Mathematics::Axis3D FloorOrigin {
        WorldPosition.X,
        WorldPosition.Y,
        std::max(-0.64f, WorldPosition.Z),
    };
    ComponentClass::ComponentTransform PlacementXform {};
    PlacementXform.LocationMeters = FloorOrigin;
    PlacementXform.ScaleFactors   = { 0.38f, 0.38f, 0.38f };

    const ComponentClass::ComponentClassToken ClassTok { ClassIndex + 1u, 1u };
    const ComponentClass::WorldPlacementToken PlacementTok =
        WorldPlacements.SpawnPlacement(ClassTok, TitleBuf, PlacementXform);

    OutlinerSlotToken TargetEnclosure = ShowcaseBundle->ShowcaseFolderToken;
    if (ExplicitEnclosure.Valid())
    {
        if (const OutlinerRecordEntry* ExplicitEntry = Panes->Outliner.FindEntry(ExplicitEnclosure))
        {
            TargetEnclosure = (ExplicitEntry->Category == ConstructSlotCategory::Folder)
                                  ? ExplicitEntry->Token
                                  : ExplicitEntry->EnclosingToken;
        }
    }

    const OutlinerSlotToken RootOutlinerSlot = Panes->Outliner.ConstructFolder(TitleBuf, TargetEnclosure);
    if (OutlinerRecordEntry* RootRow = Panes->Outliner.AccessEntry(RootOutlinerSlot))
    {
        RootRow->Expanded      = true;
        RootRow->FolderTint    = ColorRgba { 242u, 224u, 48u, 255u };
        RootRow->CategoryLabel = "ComponentClass · Placement";
        RootRow->Subtitle      = ClassDef.Title + " · .ComponentSpace";
        RootRow->PositionX     = FloorOrigin.X;
        RootRow->PositionY     = FloorOrigin.Y;
        RootRow->PositionZ     = FloorOrigin.Z;
    }

    const ComponentClass::ComponentInstanceToken OriginTok = ClassDef.Storage.QueryOriginToken();
    PlacementLinks.push_back({ RootOutlinerSlot, PlacementTok, OriginTok, ClassIndex });

    std::vector<std::pair<uint32_t, OutlinerSlotToken>> CompToOutliner {};
    auto LookupEnclosingOutliner = [&](uint32_t EnclosingIdx) -> OutlinerSlotToken
    {
        if (EnclosingIdx == ComponentClass::InvalidNestingIndex)
        {
            return RootOutlinerSlot;
        }
        const auto* EncHeader = ClassDef.Storage.InspectHeaderByIndex(EnclosingIdx);
        if (EncHeader == nullptr || EncHeader->Token == OriginTok)
        {
            return RootOutlinerSlot;
        }
        for (const auto& Pair : CompToOutliner)
        {
            if (Pair.first == EncHeader->Token.SlotIndex)
            {
                return Pair.second;
            }
        }
        return RootOutlinerSlot;
    };

    auto FindTemplateIndex = [&](ConstructSlotCategory Cat) -> uint32_t
    {
        const auto Templates = Panes->Outliner.QueryConstructTemplates();
        for (uint32_t Idx = 0u; Idx < static_cast<uint32_t>(Templates.size()); ++Idx)
        {
            if (Templates[Idx].Category == Cat)
            {
                return Idx;
            }
        }
        return 0u;
    };

    const float WorldScale = 0.38f;
    ClassDef.Storage.WalkSpatialNesting(
        [&](const ComponentClass::ComponentInstanceHeader& Hdr)
        {
            if (Hdr.Token == OriginTok)
            {
                return;
            }

            const OutlinerSlotToken EncOutliner = LookupEnclosingOutliner(Hdr.EnclosingIndex);
            const ComponentClass::ComponentTransform LocalXform =
                WorldPlacements.EvaluateEffectiveLocalTransform(ClassDef, PlacementTok, Hdr.Token);
            const Mathematics::Axis3D WorldPos {
                FloorOrigin.X + LocalXform.LocationMeters.X * WorldScale,
                FloorOrigin.Y + LocalXform.LocationMeters.Z * WorldScale,
                FloorOrigin.Z + LocalXform.LocationMeters.Y * WorldScale + 0.14f,
            };

            OutlinerSlotToken SpawnedSlot {};
            if (Hdr.Category == ComponentClass::ComponentCategory::GeometryInstance)
            {
                const ColorRgba PartTint = (Hdr.Title.find("Body") != std::string::npos || Hdr.Title.find("Wing") != std::string::npos)
                                               ? ColorRgba { 242u, 224u, 48u, 255u }
                                               : ColorRgba { 52u, 56u, 64u, 255u };
                SpawnedSlot = GeometricRaster::ConstructShowcaseModelInOutliner(
                    Panes->Outliner,
                    *ShowcaseBundle,
                    Hdr.Title,
                    PartTint,
                    WorldPos,
                    EncOutliner);
                if (OutlinerRecordEntry* Row = Panes->Outliner.AccessEntry(SpawnedSlot))
                {
                    Row->PositionX         = LocalXform.LocationMeters.X;
                    Row->PositionY         = LocalXform.LocationMeters.Y;
                    Row->PositionZ         = LocalXform.LocationMeters.Z;
                    Row->RotationX         = LocalXform.RotationDegrees.X;
                    Row->RotationY         = LocalXform.RotationDegrees.Y;
                    Row->RotationZ         = LocalXform.RotationDegrees.Z;
                    Row->ScaleX            = LocalXform.ScaleFactors.X;
                    Row->ScaleY            = LocalXform.ScaleFactors.Y;
                    Row->ScaleZ            = LocalXform.ScaleFactors.Z;
                    Row->VisibilityEnabled = Hdr.VisibilityEnabled;
                    Row->CategoryLabel     = "Component · GeometryInstance";
                    Row->Expanded          = true;
                    if (const auto* Geom = ClassDef.Storage.InspectGeometry(Hdr.Token))
                    {
                        Row->Subtitle = Geom->AssetIdentifier;
                    }
                }
            }
            else if (Hdr.Category == ComponentClass::ComponentCategory::LightInstance)
            {
                const auto* Light = ClassDef.Storage.InspectLight(Hdr.Token);
                SpawnedSlot = Panes->Outliner.ConstructFromTemplate(
                    FindTemplateIndex(ConstructSlotCategory::PointLuminaire),
                    EncOutliner);
                if (OutlinerRecordEntry* Row = Panes->Outliner.AccessEntry(SpawnedSlot))
                {
                    Row->Category          = ConstructSlotCategory::PointLuminaire;
                    Row->Title             = Hdr.Title;
                    Row->CategoryLabel     = "Component · LightInstance";
                    Row->Subtitle          = "SpotBeam · OpenPBR Direct";
                    Row->PositionX         = LocalXform.LocationMeters.X;
                    Row->PositionY         = LocalXform.LocationMeters.Y;
                    Row->PositionZ         = LocalXform.LocationMeters.Z;
                    Row->VisibilityEnabled = Hdr.VisibilityEnabled;
                }
                if (Light != nullptr)
                {
                    bool NeedsRebuild = false;
                    (void)GeometricRaster::SynchronizeShowcaseGallerySceneLive(Panes->Outliner, *ShowcaseBundle, NeedsRebuild);
                    for (auto& Binding : ShowcaseBundle->AreaLightBindings)
                    {
                        if (Binding.SlotToken == SpawnedSlot && Binding.AreaLightIndex < ShowcaseBundle->Scene.AreaLights.size())
                        {
                            const float ScaleRad = std::clamp(Light->IntensityCandela / 250.0f, 1.0f, 12.0f);
                            const Mathematics::LinearRGB Rad {
                                Light->LinearColourRGB.X * ScaleRad,
                                Light->LinearColourRGB.Y * ScaleRad,
                                Light->LinearColourRGB.Z * ScaleRad,
                            };
                            Binding.BaseEmittedRadiance = Rad;
                            ShowcaseBundle->Scene.AreaLights[Binding.AreaLightIndex].EmittedRadiance = Rad;
                        }
                    }
                }
            }
            else
            {
                const ConstructSlotCategory RowCat =
                    (Hdr.Category == ComponentClass::ComponentCategory::CameraInstance)
                        ? ConstructSlotCategory::OpticsCamera
                        : ConstructSlotCategory::LayerGroup;
                SpawnedSlot = Panes->Outliner.ConstructFromTemplate(FindTemplateIndex(RowCat), EncOutliner);
                if (OutlinerRecordEntry* Row = Panes->Outliner.AccessEntry(SpawnedSlot))
                {
                    Row->Category          = RowCat;
                    Row->Title             = Hdr.Title;
                    Row->CategoryLabel     = "Component · " + std::string(ComponentClass::QueryCategoryLabel(Hdr.Category));
                    Row->Subtitle          = ComponentClass::QueryCategoryLabel(Hdr.Category);
                    Row->MotionTagText     = (Hdr.Category == ComponentClass::ComponentCategory::ConstraintInstance) ? "JOLT" : "";
                    Row->PositionX         = LocalXform.LocationMeters.X;
                    Row->PositionY         = LocalXform.LocationMeters.Y;
                    Row->PositionZ         = LocalXform.LocationMeters.Z;
                    Row->ScaleX            = LocalXform.ScaleFactors.X;
                    Row->ScaleY            = LocalXform.ScaleFactors.Y;
                    Row->ScaleZ            = LocalXform.ScaleFactors.Z;
                    Row->VisibilityEnabled = Hdr.VisibilityEnabled;
                    Row->Expanded          = true;
                }
            }

            if (SpawnedSlot.Valid())
            {
                CompToOutliner.emplace_back(Hdr.Token.SlotIndex, SpawnedSlot);
                PlacementLinks.push_back({ SpawnedSlot, PlacementTok, Hdr.Token, ClassIndex });
            }
        });

    std::vector<ComponentClass::ComponentInstanceToken> BehaviourTokens {};
    ClassDef.Storage.CollectBehaviourTokens(BehaviourTokens);
    for (const auto& BehTok : BehaviourTokens)
    {
        if (const auto* Hdr = ClassDef.Storage.InspectHeader(BehTok))
        {
            const OutlinerSlotToken BehSlot = Panes->Outliner.ConstructFromTemplate(
                FindTemplateIndex(ConstructSlotCategory::LayerGroup),
                RootOutlinerSlot);
            if (OutlinerRecordEntry* Row = Panes->Outliner.AccessEntry(BehSlot))
            {
                Row->Category      = ConstructSlotCategory::LayerGroup;
                Row->Title         = Hdr->Title;
                Row->CategoryLabel = "Component · Behaviour";
                Row->Subtitle      = ComponentClass::QueryCategoryLabel(Hdr->Category);
                Row->MotionTagText = "DRIVE";
            }
            if (BehSlot.Valid())
            {
                PlacementLinks.push_back({ BehSlot, PlacementTok, BehTok, ClassIndex });
            }
        }
    }

    SynchronizeShowcaseRendererAfterEdit(true);
    Panes->Outliner.SelectSingle(RootOutlinerSlot);
    return RootOutlinerSlot;
}

void EditorLifecycle::SynchronizeComponentPlacementOverrides()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || PlacementLinks.empty())
    {
        return;
    }

    auto NearlyDiff = [](float A, float B) noexcept { return std::fabs(A - B) > 1.0e-4f; };
    for (const OutlinerPlacementLink& Link : PlacementLinks)
    {
        if (Link.ClassIndex >= ComponentDefinitions.size())
        {
            continue;
        }
        const OutlinerRecordEntry* Row = Panes->Outliner.FindEntry(Link.OutlinerSlot);
        const ComponentClass::ComponentClassDefinition& ClassDef = ComponentDefinitions[Link.ClassIndex];
        const auto* Hdr = ClassDef.Storage.InspectHeader(Link.ComponentToken);
        if (Row == nullptr || Hdr == nullptr || Hdr->Token == ClassDef.Storage.QueryOriginToken())
        {
            continue;
        }

        if (ComponentClass::QueryCategoryHasTransform(Hdr->Category))
        {
            const auto& AuthoredXform = Hdr->LocalTransform;
            const bool TransformModified =
                NearlyDiff(Row->PositionX, AuthoredXform.LocationMeters.X) ||
                NearlyDiff(Row->PositionY, AuthoredXform.LocationMeters.Y) ||
                NearlyDiff(Row->PositionZ, AuthoredXform.LocationMeters.Z) ||
                NearlyDiff(Row->RotationX, AuthoredXform.RotationDegrees.X) ||
                NearlyDiff(Row->RotationY, AuthoredXform.RotationDegrees.Y) ||
                NearlyDiff(Row->RotationZ, AuthoredXform.RotationDegrees.Z) ||
                NearlyDiff(Row->ScaleX, AuthoredXform.ScaleFactors.X) ||
                NearlyDiff(Row->ScaleY, AuthoredXform.ScaleFactors.Y) ||
                NearlyDiff(Row->ScaleZ, AuthoredXform.ScaleFactors.Z);
            if (TransformModified)
            {
                auto& Delta = WorldPlacements.AcquireOverride(Link.PlacementToken, Link.ComponentToken);
                Delta.OverrideTransform               = true;
                Delta.LocalTransform.LocationMeters   = { Row->PositionX, Row->PositionY, Row->PositionZ };
                Delta.LocalTransform.RotationDegrees  = { Row->RotationX, Row->RotationY, Row->RotationZ };
                Delta.LocalTransform.ScaleFactors     = { Row->ScaleX, Row->ScaleY, Row->ScaleZ };
            }
        }
        if (Row->VisibilityEnabled != Hdr->VisibilityEnabled)
        {
            auto& Delta = WorldPlacements.AcquireOverride(Link.PlacementToken, Link.ComponentToken);
            Delta.OverrideVisibility = true;
            Delta.VisibilityEnabled  = Row->VisibilityEnabled;
        }
    }
}

void EditorLifecycle::SynchronizeShowcaseRendererAfterEdit(bool RebuildGeometry)
{
    if (ShowcaseBundle == nullptr)
    {
        return;
    }

    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
    {
        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, RebuildGeometry);
    }
    else if (ShowcaseRenderer != nullptr)
    {
        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
    }
    SynchronizeShowcaseGizmoAnchor();
    RefreshShowcaseViewportRaster();
}

void EditorLifecycle::HandleContentDropOnViewport(
    const ContentRepositoryEntry& Entry,
    Point2D                       DropLogicalPos,
    Rectangle2D                   ViewportDropBounds)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const float U = std::clamp((DropLogicalPos.X - ViewportDropBounds.X) / std::max(1.0f, ViewportDropBounds.Width), 0.0f, 1.0f);
    const float V = std::clamp((DropLogicalPos.Y - ViewportDropBounds.Y) / std::max(1.0f, ViewportDropBounds.Height), 0.0f, 1.0f);
    const ColorRgba Tint { Entry.IndexHeader.TintRed, Entry.IndexHeader.TintGreen, Entry.IndexHeader.TintBlue, 255u };
    const ContentCategory Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);

    if (Entry.ContainerFormat == ".ComponentSpace" || Entry.DiskPath.extension() == ".ComponentSpace")
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.36f);
        (void)SpawnComponentClassPlacement(Entry.DisplayTitle, DropPos, {}, Entry.DiskPath);
        return;
    }

    if (Category == ContentCategory::Geometry || Category == ContentCategory::Photometry)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.36f);
        (void)GeometricRaster::ConstructShowcaseModelInOutliner(
            Panes->Outliner,
            *ShowcaseBundle,
            Entry.DisplayTitle,
            Tint,
            DropPos,
            {});
        SynchronizeShowcaseRendererAfterEdit(true);
        return;
    }

    if (Category == ContentCategory::Materials && Entry.IndexHeader.MatCapPresetMarker == 0u)
    {
        OutlinerSlotToken Target = GeometricRaster::PickShowcaseGalleryEntryAtUv(Panes->Outliner, *ShowcaseBundle, U, V);
        if (!Target.Valid())
        {
            Target = Panes->Outliner.QueryPrimarySelection();
        }
        else
        {
            Panes->Outliner.SelectSingle(Target);
        }
        const MaterialSpace::SurfaceMaterial Mat = GeometricRaster::BuildMaterialFromContentPreset(Entry.DisplayTitle, Tint);
        if (GeometricRaster::AssignShowcaseEntryMaterial(Panes->Outliner, *ShowcaseBundle, Target, Mat, Entry.DisplayTitle))
        {
            if (Panes->Viewport.QueryDisplaySettings().RenderEngineIndex == static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap))
            {
                Panes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                    static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::OpenPbrDirect);
                Panes->Viewport.AccessDisplaySettings().DebugChannelIndex = 0u;
            }
            SynchronizeShowcaseRendererAfterEdit(false);
        }
        return;
    }
}

void EditorLifecycle::HandleContentDropOnOutliner(
    const ContentRepositoryEntry& Entry,
    Point2D                       DropLogicalPos,
    Rectangle2D                   OutlinerDropBounds)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const Rectangle2D OutlinerPhys = Panes->Outliner.QueryLastPhysicalBounds();
    const float RelX = (DropLogicalPos.X - OutlinerDropBounds.X) / std::max(1.0f, OutlinerDropBounds.Width);
    const float RelY = (DropLogicalPos.Y - OutlinerDropBounds.Y) / std::max(1.0f, OutlinerDropBounds.Height);
    const Point2D PhysicalPt {
        OutlinerPhys.X + RelX * OutlinerPhys.Width,
        OutlinerPhys.Y + RelY * OutlinerPhys.Height,
    };

    OutlinerSlotToken HitSlot = Panes->Outliner.QuerySlotAtPhysicalPoint(PhysicalPt);
    const ColorRgba Tint { Entry.IndexHeader.TintRed, Entry.IndexHeader.TintGreen, Entry.IndexHeader.TintBlue, 255u };
    const ContentCategory Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);

    if (Entry.ContainerFormat == ".ComponentSpace" || Entry.DiskPath.extension() == ".ComponentSpace")
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.62f, 0.36f);
        (void)SpawnComponentClassPlacement(Entry.DisplayTitle, DropPos, HitSlot, Entry.DiskPath);
        return;
    }

    if (Category == ContentCategory::Geometry || Category == ContentCategory::Photometry)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.62f, 0.36f);
        (void)GeometricRaster::ConstructShowcaseModelInOutliner(
            Panes->Outliner,
            *ShowcaseBundle,
            Entry.DisplayTitle,
            Tint,
            DropPos,
            HitSlot);
        SynchronizeShowcaseRendererAfterEdit(true);
        return;
    }

    if (Category == ContentCategory::Materials && Entry.IndexHeader.MatCapPresetMarker == 0u)
    {
        if (!HitSlot.Valid())
        {
            HitSlot = Panes->Outliner.QueryPrimarySelection();
        }
        else
        {
            Panes->Outliner.SelectSingle(HitSlot);
        }
        const MaterialSpace::SurfaceMaterial Mat = GeometricRaster::BuildMaterialFromContentPreset(Entry.DisplayTitle, Tint);
        if (GeometricRaster::AssignShowcaseEntryMaterial(Panes->Outliner, *ShowcaseBundle, HitSlot, Mat, Entry.DisplayTitle))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
        return;
    }
}

void EditorLifecycle::SynchronizeContentBrowserColourTargets()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
    const Rectangle2D ViewportPhys       = Panes->Viewport.QueryLastCanvasPhysicalBounds();
    const Rectangle2D ViewportDropBounds = BrowserPanel.MapPhysicalToLogicalBounds(ViewportPhys);
    const Rectangle2D OutlinerPhys       = Panes->Outliner.QueryLastPhysicalBounds();
    const Rectangle2D OutlinerDropBounds = BrowserPanel.MapPhysicalToLogicalBounds(OutlinerPhys);

    const OutlinerSlotToken    PrimarySlot = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Selected    = PrimarySlot.Valid() ? Panes->Outliner.FindEntry(PrimarySlot) : nullptr;
    const std::string          TargetLabel = (Selected != nullptr) ? (Selected->Title + " BaseColor") : std::string("Active World Surface");

    BrowserPanel.ClearColourSlots();
    ColourSlotRegistration WorldSlot {};
    WorldSlot.SlotIdentifier = "ActiveWorldSurfaceColour";
    WorldSlot.DisplayTitle   = TargetLabel;
    WorldSlot.ScreenBounds   = ViewportDropBounds;
    WorldSlot.NotifyColourAssigned = [this](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const OutlinerSlotToken Target = ActivePanes->Outliner.QueryPrimarySelection();
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    WorldSlot.NotifyColourAssignedAt = [this, ViewportDropBounds](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry, Point2D DropLogicalPos)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const float U = std::clamp((DropLogicalPos.X - ViewportDropBounds.X) / std::max(1.0f, ViewportDropBounds.Width), 0.0f, 1.0f);
        const float V = std::clamp((DropLogicalPos.Y - ViewportDropBounds.Y) / std::max(1.0f, ViewportDropBounds.Height), 0.0f, 1.0f);
        OutlinerSlotToken Target = GeometricRaster::PickShowcaseGalleryEntryAtUv(ActivePanes->Outliner, *ShowcaseBundle, U, V);
        if (Target.Valid())
        {
            ActivePanes->Outliner.SelectSingle(Target);
        }
        else
        {
            Target = ActivePanes->Outliner.QueryPrimarySelection();
        }
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    BrowserPanel.RegisterColourSlot(WorldSlot);

    ColourSlotRegistration OutlinerColourSlot {};
    OutlinerColourSlot.SlotIdentifier = "OutlinerSurfaceColour";
    OutlinerColourSlot.DisplayTitle   = "Outliner Row Colour";
    OutlinerColourSlot.ScreenBounds   = OutlinerDropBounds;
    OutlinerColourSlot.NotifyColourAssignedAt = [this, OutlinerDropBounds](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry, Point2D DropLogicalPos)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const Rectangle2D OutlinerPhysNow = ActivePanes->Outliner.QueryLastPhysicalBounds();
        const float RelX = (DropLogicalPos.X - OutlinerDropBounds.X) / std::max(1.0f, OutlinerDropBounds.Width);
        const float RelY = (DropLogicalPos.Y - OutlinerDropBounds.Y) / std::max(1.0f, OutlinerDropBounds.Height);
        const Point2D PhysicalPt {
            OutlinerPhysNow.X + RelX * OutlinerPhysNow.Width,
            OutlinerPhysNow.Y + RelY * OutlinerPhysNow.Height,
        };
        OutlinerSlotToken Target = ActivePanes->Outliner.QuerySlotAtPhysicalPoint(PhysicalPt);
        if (Target.Valid())
        {
            ActivePanes->Outliner.SelectSingle(Target);
        }
        else
        {
            Target = ActivePanes->Outliner.QueryPrimarySelection();
        }
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    BrowserPanel.RegisterColourSlot(OutlinerColourSlot);
    BrowserPanel.FocusColourSlot("ActiveWorldSurfaceColour");

    BrowserPanel.ClearMatCapSlots();
    BrowserPanel.RegisterMatCapSlot(MatCapSlotRegistration{
        "WorldViewportMatCap",
        "3D World MatCap",
        ViewportDropBounds,
        [this](uint8_t MatCapSlot, const ContentRepositoryEntry&)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            ActivePanes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap);
            ActivePanes->Viewport.AccessDisplaySettings().DebugChannelIndex =
                (MatCapSlot == 9u) ? 3u : 0u;
            RefreshShowcaseViewportRaster();
        },
    });

    BrowserPanel.ClearContentDropZones();
    BrowserPanel.RegisterContentDropZone(ContentDropZoneRegistration{
        "world.viewport",
        "3D World Viewport",
        ViewportDropBounds,
        ContentCategory::All,
        [this, ViewportDropBounds](const ContentRepositoryEntry& Entry, Point2D DropLogicalPos)
        {
            HandleContentDropOnViewport(Entry, DropLogicalPos, ViewportDropBounds);
        },
    });
    BrowserPanel.RegisterContentDropZone(ContentDropZoneRegistration{
        "world.outliner",
        "World Outliner",
        OutlinerDropBounds,
        ContentCategory::All,
        [this, OutlinerDropBounds](const ContentRepositoryEntry& Entry, Point2D DropLogicalPos)
        {
            HandleContentDropOnOutliner(Entry, DropLogicalPos, OutlinerDropBounds);
        },
    });
}

void EditorLifecycle::RefreshGizmoOverlayFromCachedWorld()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const bool GpuReady = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    const bool UseGpuViewport =
        GpuReady &&
        StartupConfig.ImGuiPresenter != nullptr &&
        WorldGizmoRasterizer != nullptr &&
        WorldGizmoRasterizer->QueryReady() &&
        WorldGizmoRasterizer->QueryGpuCompositeSupported();

    if (UseGpuViewport)
    {
        if (LastWorldImageView == VK_NULL_HANDLE || LastTargetWidthPx == 0u || LastTargetHeightPx == 0u)
        {
            RefreshShowcaseViewportRaster();
            return;
        }

        SynchronizeShowcaseGizmoAnchor();

        const Rectangle2D CanvasBounds = Panes->Viewport.QueryLastCanvasPaneBounds();
        const bool CursorInCanvas = PointerTrackingValid && CanvasBounds.Width >= 16.0f &&
                                    CanvasBounds.Height >= 16.0f && CanvasBounds.Contains(LastPointerPosition);
        uint32_t PickX = LastTargetWidthPx / 2u;
        uint32_t PickY = LastTargetHeightPx / 2u;
        if (CursorInCanvas)
        {
            const float U = std::clamp((LastPointerPosition.X - CanvasBounds.X) / CanvasBounds.Width, 0.0f, 1.0f);
            const float V = std::clamp((LastPointerPosition.Y - CanvasBounds.Y) / CanvasBounds.Height, 0.0f, 1.0f);
            PickX = static_cast<uint32_t>(U * static_cast<float>(std::max(1u, LastTargetWidthPx) - 1u));
            PickY = static_cast<uint32_t>(V * static_cast<float>(std::max(1u, LastTargetHeightPx) - 1u));
        }

        GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportOverlayRequest CompReq {};
        CompReq.WorldSourceView      = LastWorldImageView;
        CompReq.TargetWidthPx        = LastTargetWidthPx;
        CompReq.TargetHeightPx       = LastTargetHeightPx;
        CompReq.Camera               = ShowcaseBundle->EditorCamera;
        CompReq.Gizmo3D              = &WorldTransformGizmo;
        CompReq.DrawGizmo            = LastGizmoSlot.Valid();
        CompReq.RequestPickPatch     = !CameraInteractiveDrag && CursorInCanvas && LastGizmoSlot.Valid();
        CompReq.PickCursorX          = PickX;
        CompReq.PickCursorY          = PickY;

        GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportOverlayResult CompOut {};
        const auto GizmoStart = std::chrono::steady_clock::now();
        const bool CompOk = WorldGizmoRasterizer->CompositeOntoGpuViewport(CompReq, CompOut);
        const auto GizmoEnd = std::chrono::steady_clock::now();
        LatestViewportTiming.GizmoOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

        if (CompOk && CompOut.ColorImageView != VK_NULL_HANDLE)
        {
            const uint64_t NativeTex = StartupConfig.ImGuiPresenter->RegisterOrUpdateExternalImage(
                0x46525456503031ull,
                CompOut.ImageGeneration,
                CompOut.ColorImageView);
            if (NativeTex != 0ull)
            {
                Panes->Viewport.AssignCanvasGpuTexture(NativeTex, CompOut.WidthPx, CompOut.HeightPx);
                return;
            }
        }
    }

    if (CachedWorldRgba8.empty() || CachedWorldWidthPx == 0u || CachedWorldHeightPx == 0u)
    {
        RefreshShowcaseViewportRaster();
        return;
    }

    SynchronizeShowcaseGizmoAnchor();
    std::vector<uint8_t> ViewportRgba = CachedWorldRgba8;

    const auto GizmoStart = std::chrono::steady_clock::now();
    if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
    {
        (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
            ViewportRgba,
            CachedWorldWidthPx,
            CachedWorldHeightPx,
            ShowcaseBundle->EditorCamera,
            WorldTransformGizmo);
    }
    const auto GizmoEnd = std::chrono::steady_clock::now();
    LatestViewportTiming.GizmoOverlayMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());
    Panes->Viewport.AssignCanvasRasterImage(ViewportRgba.data(), CachedWorldWidthPx, CachedWorldHeightPx);
}

void EditorLifecycle::RefreshShowcaseViewportRaster()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || ShowcaseRenderer == nullptr)
    {
        return;
    }

    const auto& DisplayCfg = Panes->Viewport.QueryDisplaySettings();
    const bool  GpuReady   = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    const bool  HwGpu      = GpuReady && GpuWorldRenderer->QueryHardwareGpuActive();
    const bool  UseGpuViewport =
        GpuReady &&
        StartupConfig.ImGuiPresenter != nullptr &&
        WorldGizmoRasterizer != nullptr &&
        WorldGizmoRasterizer->QueryReady() &&
        WorldGizmoRasterizer->QueryGpuCompositeSupported();

    const uint32_t TraceResIdx = DisplayCfg.TraceResolutionIndex % 3u;
    const uint32_t TraceDiv    = (TraceResIdx == 2u) ? 4u : ((TraceResIdx == 1u) ? 2u : 1u);

    uint32_t TargetW = 320u;
    uint32_t TargetH = 190u;
    const Rectangle2D CanvasBounds = Panes->Viewport.QueryLastCanvasPaneBounds();
    if (CanvasBounds.Width >= 120.0f && CanvasBounds.Height >= 90.0f)
    {
        const float Aspect    = std::clamp(CanvasBounds.Width / CanvasBounds.Height, 0.5f, 3.2f);
        const float ScaleNorm = std::clamp(DisplayCfg.RenderScalePercent / 100.0f, 0.50f, 1.50f);
        if (HwGpu)
        {
            const float DesiredW = (UseGpuViewport ? 960.0f : (CameraInteractiveDrag ? 680.0f : 960.0f)) * ScaleNorm;
            TargetW              = static_cast<uint32_t>(std::clamp(std::round(DesiredW), 320.0f, 1600.0f));
        }
        else
        {
            const float DesiredW = (UseGpuViewport ? 384.0f : (CameraInteractiveDrag ? 280.0f : 384.0f)) * ScaleNorm;
            TargetW              = static_cast<uint32_t>(std::clamp(std::round(DesiredW), 192.0f, 640.0f));
        }
        TargetH = std::max(140u, static_cast<uint32_t>(std::round(static_cast<float>(TargetW) / Aspect)));
        TargetW = std::max(192u, static_cast<uint32_t>(std::round(static_cast<float>(TargetH) * Aspect)));
    }
    else if (HwGpu)
    {
        TargetW = 960u;
        TargetH = 640u;
    }

    const uint32_t TraceW = std::max(96u, (TargetW + TraceDiv - 1u) / TraceDiv);
    const uint32_t TraceH = std::max(70u, (TargetH + TraceDiv - 1u) / TraceDiv);

    ShowcaseBundle->EditorCamera.ViewportWidthPx  = UseGpuViewport ? TargetW : TraceW;
    ShowcaseBundle->EditorCamera.ViewportHeightPx = UseGpuViewport ? TargetH : TraceH;

    const uint32_t QTier = std::min(4u, DisplayCfg.QualityTier);

    GeometricRaster::WorldRenderConfiguration RenderConfig {};
    RenderConfig.Engine           = static_cast<GeometricRaster::WorldRenderEngine>(std::min(5u, DisplayCfg.RenderEngineIndex));
    RenderConfig.ShadowAlgorithm  = static_cast<GeometricRaster::SolarShadowAlgorithm>(std::min(4u, DisplayCfg.ShadowAlgorithmIndex));
    RenderConfig.SdfPenumbraMode  = static_cast<GeometricRaster::DirectLighting::SdfShadowPenumbraMode>(std::min(5u, DisplayCfg.SdfShadowPenumbraIndex));
    RenderConfig.ReflectionSource = (DisplayCfg.ReflectionsEnabled && DisplayCfg.RaytracingEnabled && DisplayCfg.ReflectionSourceIndex == 0u)
        ? GeometricRaster::SpecularReflectionSource::WideTreeRaytrace
        : GeometricRaster::SpecularReflectionSource::SkyEnvironment;
    RenderConfig.DebugChannel       = std::min(11u, DisplayCfg.DebugChannelIndex);
    RenderConfig.QualityTier        = QTier;
    RenderConfig.ReflectionsEnabled = DisplayCfg.ReflectionsEnabled;
    RenderConfig.RaytracingEnabled  = DisplayCfg.RaytracingEnabled;

    std::vector<uint8_t> CleanWorldRgba {};
    uint32_t             OutWidthPx     = TraceW;
    uint32_t             OutHeightPx    = TraceH;
    bool                 RenderedOnGpu  = false;
    bool                 GpuCompositeDone = false;

    if (UseGpuViewport)
    {
        auto TraceCam = ShowcaseBundle->EditorCamera;
        TraceCam.ViewportWidthPx  = TraceW;
        TraceCam.ViewportHeightPx = TraceH;
        VkImageView WorldView = VK_NULL_HANDLE;
        uint32_t    TracedW   = 0u;
        uint32_t    TracedH   = 0u;
        if (GpuWorldRenderer->RenderWorldViewportToGpuImage(
                RenderConfig,
                0u,
                TraceCam,
                ShowcaseBundle->Scene,
                WorldView,
                TracedW,
                TracedH) && WorldView != VK_NULL_HANDLE)
        {
            RenderedOnGpu = true;
            LastWorldImageView = WorldView;
            LastTargetWidthPx  = TargetW;
            LastTargetHeightPx = TargetH;

            const float SavedTickMs = LatestViewportTiming.EditorTickMs;
            LatestViewportTiming = GpuWorldRenderer->QueryLastTimingBreakdown();
            LatestViewportTiming.EditorTickMs = SavedTickMs;

            SynchronizeShowcaseGizmoAnchor();
            const bool CursorInCanvas = PointerTrackingValid && CanvasBounds.Width >= 16.0f &&
                                        CanvasBounds.Height >= 16.0f && CanvasBounds.Contains(LastPointerPosition);
            uint32_t PickX = TargetW / 2u;
            uint32_t PickY = TargetH / 2u;
            if (CursorInCanvas)
            {
                const float U = std::clamp((LastPointerPosition.X - CanvasBounds.X) / CanvasBounds.Width, 0.0f, 1.0f);
                const float V = std::clamp((LastPointerPosition.Y - CanvasBounds.Y) / CanvasBounds.Height, 0.0f, 1.0f);
                PickX = static_cast<uint32_t>(U * static_cast<float>(std::max(1u, TargetW) - 1u));
                PickY = static_cast<uint32_t>(V * static_cast<float>(std::max(1u, TargetH) - 1u));
            }

            GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportOverlayRequest CompReq {};
            CompReq.WorldSourceView      = WorldView;
            CompReq.TargetWidthPx        = TargetW;
            CompReq.TargetHeightPx       = TargetH;
            CompReq.Camera               = ShowcaseBundle->EditorCamera;
            CompReq.Gizmo3D              = &WorldTransformGizmo;
            CompReq.DrawGizmo            = LastGizmoSlot.Valid();
            CompReq.RequestPickPatch     = !CameraInteractiveDrag && CursorInCanvas && LastGizmoSlot.Valid();
            CompReq.PickCursorX          = PickX;
            CompReq.PickCursorY          = PickY;

            GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportOverlayResult CompOut {};
            const auto GizmoStart = std::chrono::steady_clock::now();
            const bool CompOk = WorldGizmoRasterizer->CompositeOntoGpuViewport(CompReq, CompOut);
            const auto GizmoEnd = std::chrono::steady_clock::now();
            LatestViewportTiming.GizmoOverlayMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

            if (CompOk && CompOut.ColorImageView != VK_NULL_HANDLE)
            {
                const uint64_t NativeTex = StartupConfig.ImGuiPresenter->RegisterOrUpdateExternalImage(
                    0x46525456503031ull,
                    CompOut.ImageGeneration,
                    CompOut.ColorImageView);
                if (NativeTex != 0ull)
                {
                    Panes->Viewport.AssignCanvasGpuTexture(NativeTex, CompOut.WidthPx, CompOut.HeightPx);
                    GpuCompositeDone = true;
                }
            }
        }
    }
    else if (GpuReady)
    {
        RenderedOnGpu = GpuWorldRenderer->RenderWorldViewportWithEngine(
            RenderConfig,
            0u,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene,
            CleanWorldRgba);
        if (RenderedOnGpu)
        {
            const float SavedTickMs = LatestViewportTiming.EditorTickMs;
            LatestViewportTiming = GpuWorldRenderer->QueryLastTimingBreakdown();
            LatestViewportTiming.EditorTickMs = SavedTickMs;
        }
    }

    if (!RenderedOnGpu)
    {
        if (GpuReady)
        {
            std::fprintf(stderr, "[FrontierEditor] GPU world compute failed; skipping CPU fallback to protect desktop responsiveness.\n");
            return;
        }
        auto CpuOutput = ShowcaseRenderer->RenderWorldFrameWithEngine(
            RenderConfig,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene);
        OutWidthPx     = CpuOutput.WidthPx;
        OutHeightPx    = CpuOutput.HeightPx;
        CleanWorldRgba = std::move(CpuOutput.DisplayRgba8);
    }

    if (!GpuCompositeDone)
    {
        CachedWorldRgba8    = CleanWorldRgba;
        CachedWorldWidthPx  = OutWidthPx;
        CachedWorldHeightPx = OutHeightPx;

        SynchronizeShowcaseGizmoAnchor();
        const auto GizmoStart = std::chrono::steady_clock::now();
        if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
        {
            (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
                CleanWorldRgba,
                OutWidthPx,
                OutHeightPx,
                ShowcaseBundle->EditorCamera,
                WorldTransformGizmo);
        }
        const auto GizmoEnd = std::chrono::steady_clock::now();
        LatestViewportTiming.GizmoOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

        Panes->Viewport.AssignCanvasRasterImage(CleanWorldRgba.data(), OutWidthPx, OutHeightPx);
    }

    const float SunElevDeg = std::asin(std::clamp(ShowcaseBundle->Scene.Sun.DirectionToSun.Z, -1.0f, 1.0f)) * 57.2957795f;
    uint32_t    ActiveLtc  = 0u;
    for (const auto& Quad : ShowcaseBundle->Scene.AreaLights)
    {
        if (Quad.EmittedRadiance.R + Quad.EmittedRadiance.G + Quad.EmittedRadiance.B > 0.01f)
        {
            ++ActiveLtc;
        }
    }

    OutlinerFooterReadings Footer {};
    const char* EngineShortTag = "OpenPBR";
    switch (RenderConfig.Engine)
    {
    case GeometricRaster::WorldRenderEngine::MatCap:                     EngineShortTag = "MatCap";   break;
    case GeometricRaster::WorldRenderEngine::OpenPbrDirect:              EngineShortTag = "OpenPBR";  break;
    case GeometricRaster::WorldRenderEngine::RealTimeGiGtxLowEnd:        EngineShortTag = "GTX GI";   break;
    case GeometricRaster::WorldRenderEngine::RealTimeGiHighEnd:          EngineShortTag = "BVH4 GI";  break;
    case GeometricRaster::WorldRenderEngine::PathTracerGameRealtime:     EngineShortTag = "PT RT";    break;
    case GeometricRaster::WorldRenderEngine::PathTracerSceneFinalRender: EngineShortTag = "PT Final"; break;
    }
    if (RenderedOnGpu)
    {
        const float GpuMs  = std::max(0.2f, GpuWorldRenderer->QueryLastDispatchMilliseconds());
        const int   EstFps = std::clamp(static_cast<int>(std::lround(1000.0f / std::max(4.16f, GpuMs))), 1, 240);
        char FpsBuf[32]{};
        std::snprintf(FpsBuf, sizeof(FpsBuf), "%d fps", EstFps);
        Footer.RealtimeText = FpsBuf;
        Footer.QualityText  = std::string(HwGpu ? "GPU " : "VK ") + EngineShortTag;
    }
    else
    {
        Footer.RealtimeText = "CPU GI";
        Footer.QualityText  = std::string("CPU ") + EngineShortTag;
    }
    char SunBuf[32]{};
    std::snprintf(SunBuf, sizeof(SunBuf), "Sun %.0f°", std::max(0.0f, SunElevDeg));
    Footer.PrimarySlot = SunBuf;
    char LtcBuf[32]{};
    std::snprintf(LtcBuf, sizeof(LtcBuf), "LTC %u", ActiveLtc);
    Footer.SecondarySlot = LtcBuf;
    char CamBuffer[48]{};
    std::snprintf(
        CamBuffer,
        sizeof(CamBuffer),
        "%.1f, %.1f, %.1f",
        ShowcaseBundle->EditorCamera.CameraOrigin.X,
        ShowcaseBundle->EditorCamera.CameraOrigin.Y,
        ShowcaseBundle->EditorCamera.CameraOrigin.Z);
    Footer.CameraText = CamBuffer;
    Panes->Outliner.ConfigureFooterReadings(Footer);
}

bool EditorLifecycle::ExecuteExternalDesktopTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const auto TickStart = std::chrono::steady_clock::now();
    LatestViewportTiming.WorldDispatched = false;
    LatestViewportTiming.GizmoOverlayMs  = 0.0f;
    LatestViewportTiming.SceneSyncGpuMs  = 0.0f;

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    Workspaces.Tick(SafeStep);
    if (ShowcaseBundle != nullptr)
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            const OutlinerSlotToken CurrentSel = Panes->Outliner.QueryPrimarySelection();
            const Rectangle2D       CanvasRect = Panes->Viewport.QueryLastCanvasPaneBounds();
            auto&                   DisplayCfg = Panes->Viewport.AccessDisplaySettings();
            const uint32_t          CurCanvasW = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Width)));
            const uint32_t          CurCanvasH = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Height)));
            const int32_t           CurScale   = static_cast<int32_t>(std::lround(DisplayCfg.RenderScalePercent));

            auto& Atmo = ShowcaseBundle->Scene.Atmosphere;
            if (DisplayCfg.DayCyclePlaying && SafeStep > 0.0)
            {
                Atmo.Solar.CycleAnimationEnabled = true;
                GeometricRaster::CelestialEphemerisSolver::AdvanceDayCycleHours(Atmo.Solar, SafeStep);
                DisplayCfg.SolarTimeOfDayHours = Atmo.Solar.LocalHours;
            }
            const bool CloudsMoving =
                DisplayCfg.VolumetricCloudsEnabled &&
                Atmo.Clouds.FollowWindEnabled &&
                Atmo.Clouds.WindSpeedMetersPerSecond > 0.01f;
            const bool AtmoAnimTick =
                DisplayCfg.AtmosphereSkyEnabled &&
                (DisplayCfg.DayCyclePlaying || CloudsMoving) &&
                SafeStep > 0.0;
            if (AtmoAnimTick)
            {
                Atmo.SimulationSeconds += static_cast<float>(SafeStep);
            }

            const bool ViewportTogglesChanged =
                DisplayCfg.AtmosphereSkyEnabled != LastAtmosphereSky ||
                DisplayCfg.VolumetricCloudsEnabled != LastCloudsEnabled ||
                DisplayCfg.CloudShadowsEnabled != LastCloudShadowsEnabled ||
                DisplayCfg.CelestialStarsEnabled != LastCelestialsEnabled ||
                std::fabs(DisplayCfg.SolarTimeOfDayHours - LastSolarTimeHours) > 1.0e-3f ||
                std::fabs(DisplayCfg.CloudFraction - LastCloudFraction) > 1.0e-3f ||
                std::fabs(DisplayCfg.CloudWindSpeedMps - LastCloudWindSpeedMps) > 1.0e-2f;

            if (DisplayCfg.AtmosphereSkyEnabled != LastAtmosphereSky)
            {
                Atmo.Medium.AtmosphereEnabled = DisplayCfg.AtmosphereSkyEnabled;
            }
            if (DisplayCfg.VolumetricCloudsEnabled != LastCloudsEnabled)
            {
                Atmo.Clouds.CloudsEnabled = DisplayCfg.VolumetricCloudsEnabled;
            }
            if (DisplayCfg.CloudShadowsEnabled != LastCloudShadowsEnabled)
            {
                Atmo.Clouds.CloudShadow.ShadowEnabled = DisplayCfg.CloudShadowsEnabled;
            }
            if (std::fabs(DisplayCfg.CloudFraction - LastCloudFraction) > 1.0e-3f)
            {
                Atmo.Clouds.CloudFraction = DisplayCfg.CloudFraction;
            }
            if (std::fabs(DisplayCfg.CloudWindSpeedMps - LastCloudWindSpeedMps) > 1.0e-2f)
            {
                Atmo.Clouds.WindSpeedMetersPerSecond = DisplayCfg.CloudWindSpeedMps;
                Atmo.Wind.SpeedMetersPerSecond       = std::max(Atmo.Wind.SpeedMetersPerSecond, DisplayCfg.CloudWindSpeedMps);
            }
            if (DisplayCfg.CelestialStarsEnabled != LastCelestialsEnabled)
            {
                Atmo.Stars.StarfieldEnabled = DisplayCfg.CelestialStarsEnabled;
                Atmo.Solar.SolarEnabled     = DisplayCfg.CelestialStarsEnabled;
                Atmo.Moons[0].Visible       = DisplayCfg.CelestialStarsEnabled;
            }
            if (std::fabs(DisplayCfg.SolarTimeOfDayHours - LastSolarTimeHours) > 1.0e-3f)
            {
                Atmo.Solar.LocalHours = DisplayCfg.SolarTimeOfDayHours;
            }
            Atmo.Solar.CycleAnimationEnabled = DisplayCfg.DayCyclePlaying;

            const bool GpuReady = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
            const bool AtmoChanged = ViewportTogglesChanged || AtmoAnimTick;
            const bool NeedCpuSkySync =
                ShowcaseRenderer != nullptr && Atmo.Medium.AtmosphereEnabled &&
                (ViewportTogglesChanged || (!GpuReady && (DisplayCfg.DayCyclePlaying || AtmoAnimTick)));
            if (NeedCpuSkySync)
            {
                ShowcaseRenderer->SynchronizeWorldAtmosphere(ShowcaseBundle->Scene);
            }
            else if (Atmo.Medium.AtmosphereEnabled && DisplayCfg.DayCyclePlaying)
            {
                const auto Solved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
                ShowcaseBundle->Scene.Sun.DirectionToSun = Solved.WorldDirectionToSun;
            }

            const ViewportProjectionPreset CurProjection = Panes->Viewport.QueryPrimaryProjection();
            bool ProjectionChanged = false;
            if (CurProjection != LastPrimaryProjection)
            {
                LastPrimaryProjection = CurProjection;
                ProjectionChanged     = true;
                auto& Cam             = ShowcaseBundle->EditorCamera;
                Cam.TargetPoint       = { 0.00f, 0.55f, 0.15f };
                Cam.WorldUpAxis       = { 0.00f, 0.00f, 1.00f };
                switch (CurProjection)
                {
                case ViewportProjectionPreset::Top:
                    Cam.CameraOrigin = { 0.00f, 0.50f, 5.20f };
                    Cam.WorldUpAxis  = { 0.00f, 1.00f, 0.00f };
                    break;
                case ViewportProjectionPreset::Front:
                    Cam.CameraOrigin = { 0.00f, -4.40f, 0.45f };
                    break;
                case ViewportProjectionPreset::Right:
                    Cam.CameraOrigin = { 4.60f, 0.55f, 0.45f };
                    break;
                case ViewportProjectionPreset::Perspective:
                case ViewportProjectionPreset::Orthographic:
                default:
                    Cam.CameraOrigin = { 0.00f, -3.55f, 0.42f };
                    break;
                }
            }

            const bool              DisplayChanged =
                (CurCanvasW >= 120u && CurCanvasH >= 90u &&
                 (std::abs(static_cast<int>(CurCanvasW) - static_cast<int>(LastCanvasPaneWidth)) > 8 ||
                  std::abs(static_cast<int>(CurCanvasH) - static_cast<int>(LastCanvasPaneHeight)) > 8)) ||
                DisplayCfg.QualityTier != LastViewportTier ||
                DisplayCfg.DebugChannelIndex != LastViewportDebug ||
                DisplayCfg.RenderEngineIndex != LastRenderEngine ||
                DisplayCfg.ShadowAlgorithmIndex != LastShadowAlgorithm ||
                DisplayCfg.SdfShadowPenumbraIndex != LastSdfPenumbraIndex ||
                DisplayCfg.ReflectionSourceIndex != LastReflectionSource ||
                DisplayCfg.TraceResolutionIndex != LastTraceResolutionIdx ||
                DisplayCfg.ReflectionsEnabled != LastReflectionsEnabled ||
                DisplayCfg.AntiAliasingEnabled != LastAaEnabled ||
                DisplayCfg.RaytracingEnabled != LastRtEnabled ||
                AtmoChanged ||
                ProjectionChanged ||
                CurScale != LastRenderScalePct;

            if (CurCanvasW >= 120u && CurCanvasH >= 90u)
            {
                LastCanvasPaneWidth  = CurCanvasW;
                LastCanvasPaneHeight = CurCanvasH;
            }
            LastViewportTier       = DisplayCfg.QualityTier;
            LastViewportDebug      = DisplayCfg.DebugChannelIndex;
            LastRenderEngine       = DisplayCfg.RenderEngineIndex;
            LastShadowAlgorithm    = DisplayCfg.ShadowAlgorithmIndex;
            LastSdfPenumbraIndex   = DisplayCfg.SdfShadowPenumbraIndex;
            LastReflectionSource   = DisplayCfg.ReflectionSourceIndex;
            LastTraceResolutionIdx = DisplayCfg.TraceResolutionIndex;
            LastReflectionsEnabled = DisplayCfg.ReflectionsEnabled;
            LastAaEnabled          = DisplayCfg.AntiAliasingEnabled;
            LastRtEnabled          = DisplayCfg.RaytracingEnabled;
            LastAtmosphereSky      = DisplayCfg.AtmosphereSkyEnabled;
            LastCloudsEnabled      = DisplayCfg.VolumetricCloudsEnabled;
            LastCloudShadowsEnabled = DisplayCfg.CloudShadowsEnabled;
            LastCelestialsEnabled  = DisplayCfg.CelestialStarsEnabled;
            LastSolarTimeHours     = DisplayCfg.SolarTimeOfDayHours;
            LastCloudFraction      = DisplayCfg.CloudFraction;
            LastCloudWindSpeedMps  = DisplayCfg.CloudWindSpeedMps;
            LastRenderScalePct     = CurScale;

            const bool InteractionWorldRefresh = PendingInteractionWorldRefresh;
            const bool InteractionGizmoRefresh = PendingInteractionGizmoRefresh;
            const bool InteractionSceneSync    = PendingInteractionSceneSync;
            const bool InteractionFastDrag     = PendingInteractionFastDrag;
            PendingInteractionWorldRefresh = false;
            PendingInteractionGizmoRefresh = false;
            PendingInteractionSceneSync    = false;
            PendingInteractionFastDrag     = false;

            SynchronizeComponentPlacementOverrides();

            bool       NeedsPrep = false;
            const bool SceneMutated = GeometricRaster::SynchronizeShowcaseGallerySceneLive(
                Panes->Outliner,
                *ShowcaseBundle,
                NeedsPrep);
            if (InteractionSceneSync && !NeedsPrep && !InteractionFastDrag && !WorldTransformGizmo.QueryDragging())
            {
                if (!GpuReady)
                {
                    GeometricRaster::RebuildShowcaseSceneAcceleration(*ShowcaseBundle);
                }
                NeedsPrep = true;
            }
            if (NeedsPrep)
            {
                if (GpuReady)
                {
                    GpuWorldRenderer->SynchronizeSceneResources(
                        ShowcaseBundle->Scene,
                        !WorldTransformGizmo.QueryDragging() && !InteractionFastDrag);
                }
                else if (ShowcaseRenderer != nullptr)
                {
                    ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                }
            }
            const auto TickPrepEnd = std::chrono::steady_clock::now();
            LatestViewportTiming.EditorTickMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(TickPrepEnd - TickStart).count());
            if (InteractionWorldRefresh || SceneMutated || DisplayChanged)
            {
                RefreshShowcaseViewportRaster();
            }
            else if (InteractionGizmoRefresh || CurrentSel != LastGizmoSlot)
            {
                RefreshGizmoOverlayFromCachedWorld();
            }
        }
    }
    EditorSequence.Tick(SafeStep);
    return !TerminationRequested;
}

void EditorLifecycle::RecordFramePresentationTiming(
    float DesktopPollAndUiMs,
    float SwapchainPresentMs,
    float TotalFrameMs) noexcept
{
    ++TimingFrameCounter;
    LatestViewportTiming.FrameIndex         = TimingFrameCounter;
    LatestViewportTiming.DesktopPollAndUiMs = DesktopPollAndUiMs;
    LatestViewportTiming.SwapchainPresentMs = SwapchainPresentMs;
    LatestViewportTiming.TotalFrameMs       = TotalFrameMs;

    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady() && LatestViewportTiming.RenderWidthPx == 0u)
    {
        LatestViewportTiming.HardwareGpuActive = GpuWorldRenderer->QueryHardwareGpuActive();
        LatestViewportTiming.AdapterName       = std::string(GpuWorldRenderer->QueryAdapterTitle());
    }

    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        Panes->Viewport.ConfigureViewportTimingBreakdown(LatestViewportTiming);
    }

    if (!TimingLogInitialized)
    {
        InitializeTimingLogFile(
            LatestViewportTiming.TimingLogPath,
            LatestViewportTiming.AdapterName,
            LatestViewportTiming.HardwareGpuActive);
        TimingLogInitialized = true;
    }

    const bool LogStartup = (TimingFrameCounter <= 5ull);
    const bool LogPeriodic = (TimingFrameCounter % 60ull) == 0ull;
    const bool LogSpike = (TotalFrameMs > 30.0f) || (LatestViewportTiming.SceneSyncGpuMs > 10.0f);
    if (LogStartup || LogPeriodic || LogSpike)
    {
        const char* Tag = LogStartup ? "INIT" : (LogSpike ? "SPIKE" : "SAMPLE");
        AppendTimingLogLine(LatestViewportTiming.TimingLogPath, LatestViewportTiming, Tag);
    }
}

bool EditorLifecycle::ExecuteAuthoringTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    InputParticipant.BeginTick();
    DockContainer.PollOperatingWindows(InputParticipant);
    (void)InputParticipant.TickExchange();

    if (Windowing::HeadlessPlatformWindow* Primary = DockContainer.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
    {
        if (Primary->CloseRequested())
        {
            TerminationRequested = true;
        }
        else
        {
            (void)InputParticipant.FlushOutboundDirectives(*Primary);
        }
    }

    if (!TerminationRequested)
    {
        return ExecuteExternalDesktopTick(SafeStep);
    }

    return !TerminationRequested;
}

void EditorLifecycle::PresentPrimaryEditorImGui(double Δt)
{
    if (!Initialized || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? Δt : CurrentStepSeconds;
    CurrentStepSeconds    = SafeStep;

    Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    ContentBrowserDrawer.AttachThemeRepository(&ControlCentre.QueryThemeRepository());

    const float UiScale = std::clamp(ControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
    const Components::HostWindowReading HostReading = DockContainer.InspectHostWindow(Components::HostWindowToken{ 0u });
    const bool HostValid = HostReading.Active && HostReading.SurfaceExtent.PixelWidth > 0u && HostReading.SurfaceExtent.PixelHeight > 0u;
    const Rectangle2D Bounds = HostValid
                                   ? Rectangle2D{
                                         0.0f,
                                         0.0f,
                                         static_cast<float>(HostReading.SurfaceExtent.PixelWidth),
                                         static_cast<float>(HostReading.SurfaceExtent.PixelHeight),
                                     }
                                   : Rectangle2D{};

    if (HostValid)
    {
        auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
        BrowserPanel.SynchronizeHostBounds(Bounds, UiScale);
        ControlCentre.SynchronizeHostBounds(Bounds, UiScale);
        if (ControlCentre.QueryFullScreenBlocking() || BrowserPanel.QueryShelfDragging())
        {
            WorkspaceAppearance::AssignOverlayPointerShield(Bounds);
        }
        else
        {
            WorkspaceAppearance::AssignOverlayPointerShield(
                BrowserPanel.QueryDrawerPhysicalBounds(),
                BrowserPanel.QueryNotchPhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryShadePhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryTabPhysicalBounds());
        }
    }

    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
    {
        ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
    }

    DockContainer.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
    WorkspaceAppearance::ClearOverlayPointerShield();

    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
    {
        ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
        SynchronizeContentBrowserColourTargets();
    }

    if (HostValid)
    {
        const float RefreshHz = HostReading.SurfaceExtent.RefreshRateHz > 0u
                                    ? static_cast<float>(HostReading.SurfaceExtent.RefreshRateHz)
                                    : 60.0f;
        ContentBrowserDrawer.Render(Bounds, SafeStep, UiScale);
        ControlCentre.Render(Bounds, SafeStep, RefreshHz);

        if (ReturnToBrowserCallback && !ControlCentre.QueryOpen())
        {
            PixelSpace TopOverlay(ImGui::GetForegroundDrawList(), Point2D{ 0.0f, 0.0f }, UiScale);
            const Point2D MouseLogical = TopOverlay.QueryMousePosition();
            ControlPointer Ptr {};
            Ptr.X        = MouseLogical.X;
            Ptr.Y        = MouseLogical.Y;
            Ptr.Down     = TopOverlay.QueryMouseDown(0);
            Ptr.Pressed  = TopOverlay.QueryMouseClicked(0);
            Ptr.Released = TopOverlay.QueryMouseReleased(0);

            const float LogicalW = Bounds.Width / UiScale;
            const Rectangle2D BtnBounds { std::max(320.0f, LogicalW * 0.5f + 98.0f), 3.0f, 82.0f, 23.0f };
            ButtonDescriptor Desc {};
            Desc.Label    = "Projects";
            Desc.Tone     = ButtonTone::Secondary;
            Desc.FontSize = 10.5f;
            Desc.PaddingX = 12.0f;
            if (CapsuleButton::Construct(TopOverlay, BtnBounds, Desc, Ptr).Pressed)
            {
                ReturnToBrowserCallback();
            }
        }
    }
}

bool EditorLifecycle::Suspend() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool OverlaySuspended   = EditorSequence.Suspend();
    const bool WorkspaceSuspended = Workspaces.Suspend();
    return OverlaySuspended && WorkspaceSuspended;
}

bool EditorLifecycle::Resume() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool WorkspaceResumed = Workspaces.Resume();
    const bool OverlayResumed   = EditorSequence.Resume();
    return WorkspaceResumed && OverlayResumed;
}

void EditorLifecycle::RequestTermination() noexcept
{
    TerminationRequested = true;
}

void EditorLifecycle::Finalize() noexcept
{
    if (!Initialized)
    {
        EditorSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down GPU overlay hooks, then workspace sessions, then input, then dock windows.
    EditorSequence.Finalize();
    if (StartupConfig.ImGuiPresenter != nullptr)
    {
        StartupConfig.ImGuiPresenter->ReleaseExternalImages();
    }
    CachedWorldRgba8.clear();
    LastWorldImageView = VK_NULL_HANDLE;
    WorldGizmoRasterizer.reset();
    GpuWorldRenderer.reset();
    ShowcaseRenderer.reset();
    ShowcaseBundle.reset();
    Workspaces.Finalize();

    if (TypographyRepository::QueryCurrent() == &Typography)
    {
        TypographyRepository::AssignCurrent(nullptr);
    }

    InputParticipant.Finalize();
    DockContainer.Finalize();

    OutlinerPaneToken    = {};
    ViewportPaneToken    = {};
    InspectorPaneToken   = {};
    Initialized          = false;
    TerminationRequested = false;
}

EditorLifecycleReading EditorLifecycle::InspectReading() const noexcept
{
    EditorLifecycleReading Reading{};
    Reading.SequencePhase       = EditorSequence.QueryPhase();
    Reading.SequenceMetrics     = EditorSequence.InspectMetrics();
    Reading.ActiveWindowCount   = DockContainer.ActiveOperatingWindowCount();
    Reading.RegisteredPaneCount = static_cast<uint32_t>(DockContainer.RegisteredPaneCount());
    Reading.OpenWorkspaceCount  = Workspaces.OpenSessionCount();
    Reading.GpuOverlayCount     = RegisteredGpuOverlays;
    Reading.ControlCentreOpen   = ControlCentre.QueryOpen();
    Reading.ContentBrowserOpen  = ContentBrowserDrawer.QueryPanel().QueryOpen();
    Reading.AuthoringActive     = QueryActive();
    return Reading;
}

} // namespace Frontier::Lifecycle

#endif // FRONTIER_EDITOR_ENABLED
