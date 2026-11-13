/*============================================================================================================================================
                                                           UVVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportHost matching Experimental/UVEditor viewport canvas header, command strip, tool rail & status bar

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportHost.h"

#include "Engine/Components/ContentBrowser/ContentBrowserPanel.h"
#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] const char* QuerySurfaceDisplayLabel(GeometricRaster::SurfaceDisplayMode Mode) noexcept
{
    switch (Mode)
    {
        case GeometricRaster::SurfaceDisplayMode::SurfaceMaterial: return "Surface Material";
        case GeometricRaster::SurfaceDisplayMode::PriorityLayerTint:  return "Priority Tint";
        case GeometricRaster::SurfaceDisplayMode::StretchHeatmap:     return "Stretch Heat";
        case GeometricRaster::SurfaceDisplayMode::UDIMCheckerPreview: return "UDIM Checker";
        case GeometricRaster::SurfaceDisplayMode::MatCapPreview:      return "MatCap Preview";
        case GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap:  return "Material + MatCap";
    }
    return "Surface Material";
}

[[nodiscard]] const char* QueryCanvasDebugLabel(GeometricRaster::UVCanvasDisplayMode Mode) noexcept
{
    switch (Mode)
    {
        case GeometricRaster::UVCanvasDisplayMode::PriorityLayerFill: return "Debug: Priority";
        case GeometricRaster::UVCanvasDisplayMode::StretchHeatmap:    return "Debug: Stretch";
        case GeometricRaster::UVCanvasDisplayMode::WindingAndOverlap: return "Debug: Overlaps";
    }
    return "Debug: Priority";
}

[[nodiscard]] const char* QuerySpatialProjectionLabel(Editor::SpatialProjectionPreset Preset) noexcept
{
    switch (Preset)
    {
        case Editor::SpatialProjectionPreset::Perspective:  return "Perspective";
        case Editor::SpatialProjectionPreset::Orthographic: return "Orthographic";
        case Editor::SpatialProjectionPreset::Top:          return "Top";
        case Editor::SpatialProjectionPreset::Front:        return "Front";
        case Editor::SpatialProjectionPreset::Right:        return "Right";
    }
    return "Perspective";
}

} // namespace

UVViewportHost::UVViewportHost()
{
    ProjectionPane.ConfigurePaneIndicators("2D UDIM UVTILE", "3D FAST LIT + MATCAP");
}

UVViewportHost::~UVViewportHost() = default;

void UVViewportHost::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session                 = TargetSession;
    SurfaceDropdownOpen     = false;
    CanvasDebugDropdownOpen = false;
    ProjectionDropdownOpen  = false;
    CachedUVRevision        = UINT64_MAX;
    Interactor.AttachSession(Session);

    if (Session == nullptr)
    {
        ProjectionPane.AttachDocument(nullptr);
        ProjectionPane.AssignInteractionCallback({});
        ProjectionPane.AssignRasterBufferSource({});
        ProjectionPane.AssignSplitModeCallback({});
        ProjectionPane.AssignSplitRatioCallback({});
        return;
    }

    ProjectionPane.AttachDocument(&Session->AccessDocument());

    ProjectionPane.AssignInteractionCallback(
        [this](const ViewportInteractionReading& Reading)
        {
            Interactor.ProcessViewportInput(Reading, ActiveHistoryTickSeq);
        });

    ProjectionPane.AssignSplitModeCallback(
        [this](Editor::ViewportSplitMode NewMode)
        {
            if (Session == nullptr)
            {
                return;
            }
            Session->WorkspaceConfig.AccessUVPreferences().InitialViewportLayout =
                static_cast<Configuration::UVViewportLayoutMode>(static_cast<uint8_t>(NewMode));
            Session->FramebuffersDirty = true;
        });

    ProjectionPane.AssignSplitRatioCallback(
        [this](float NewRatio)
        {
            if (Session == nullptr)
            {
                return;
            }
            Session->WorkspaceConfig.AccessUVPreferences().SplitRatioNormalized = NewRatio;
            Session->FramebuffersDirty = true;
        });

    ProjectionPane.AssignRasterBufferSource(
        [this](uint32_t SpatialW, uint32_t SpatialH, uint32_t PlanarW, uint32_t PlanarH) -> DualViewportFramebuffers
        {
            DualViewportFramebuffers Result {};
            if (Session == nullptr)
            {
                return Result;
            }

            Session->RefreshViewportFramebuffers(SpatialW, SpatialH, PlanarW, PlanarH);
            Result.Spatial3D = ViewportRasterBuffer{
                Session->Framebuffer3DRGBA.empty() ? nullptr : Session->Framebuffer3DRGBA.data(),
                Session->Framebuffer3DWidth,
                Session->Framebuffer3DHeight,
            };
            Result.Planar2D = ViewportRasterBuffer{
                Session->Framebuffer2DRGBA.empty() ? nullptr : Session->Framebuffer2DRGBA.data(),
                Session->Framebuffer2DWidth,
                Session->Framebuffer2DHeight,
            };
            return Result;
        });
}

void UVViewportHost::SynchronizeProjectionPolicy() noexcept
{
    if (Session == nullptr)
    {
        return;
    }
    auto& Projection = ProjectionPane.AccessProjection();
    const auto TargetSplit = static_cast<Editor::ViewportSplitMode>(
        static_cast<uint8_t>(Session->WorkspaceConfig.InspectUVPreferences().InitialViewportLayout));
    if (Projection.QuerySplitMode() != TargetSplit)
    {
        Projection.ConfigureSplitMode(TargetSplit);
    }
    Projection.ConfigureSplitRatio(Session->WorkspaceConfig.InspectUVPreferences().SplitRatioNormalized);
}

void UVViewportHost::RefreshCachedIslandCount() noexcept
{
    if (Session == nullptr)
    {
        return;
    }
    if (CachedUVRevision != Session->Asset.Network.UVRevision)
    {
        CachedUVRevision = Session->Asset.Network.UVRevision;
        CachedChartCount = static_cast<uint32_t>(SeamCutGenerator::PartitionConnectedUVIslands(Session->Asset.Network).size());
    }
}

void UVViewportHost::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    ActiveHistoryTickSeq = (ImGui::GetCurrentContext() != nullptr)
        ? (static_cast<uint64_t>(ImGui::GetFrameCount()) + 1ull)
        : ++HeadlessHistorySeq;
    {
        bool WantsUndo = false;
        bool WantsRedo = false;
        ResolveHistoryShortcuts(Input, WantsUndo, WantsRedo);
        (void)Session->TryConsumeGlobalHistoryShortcut(WantsUndo, WantsRedo, false, ActiveHistoryTickSeq);
    }

    SynchronizeProjectionPolicy();
    Session->AdvanceWorkspaceCamera(static_cast<float>(Δt));
    RefreshCachedIslandCount();

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    if (Input.EscapePressed)
    {
        SurfaceDropdownOpen        = false;
        CanvasDebugDropdownOpen    = false;
        ProjectionDropdownOpen     = false;
        ViewportDirectiveSheetOpen = false;
    }
    Rectangle2D SurfaceDropdownBounds{};
    Rectangle2D SurfaceSheetAnchor{};
    Rectangle2D SurfaceSheetBounds{};
    Rectangle2D CanvasDebugDropdownBounds{};
    Rectangle2D CanvasDebugSheetAnchor{};
    Rectangle2D CanvasDebugSheetBounds{};
    Rectangle2D ViewProjectionDropdownBounds{};
    Rectangle2D ProjectionSheetAnchor{};
    Rectangle2D ProjectionSheetBounds{};

    const float LeftX  = Bounds.X + 14.0f;
    const float RightX = Bounds.Right() - 14.0f;
    bool        OverlayCapturedClick = false;

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Row 1: .pz-viewport canvas-header (Scene Cube + Document Title + UV indicators | Split / 2D UV / 3D Surface Capsule)
    //--------------------------------------------------------------------------------------------------------------------
    const float HeaderTopY = Bounds.Y + 10.0f;
    {
        const Rectangle2D CubeIndicator{ LeftX, HeaderTopY + 3.0f, 26.0f, 26.0f };
        Surface.FillRectangle(CubeIndicator, Theme.RaisedBg, 7.0f);
        Surface.StrokeRectangle(CubeIndicator, Theme.BorderStrong, 7.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::CubeShape,
            VectorIconPlacement{ CubeIndicator.X + 5.0f, CubeIndicator.Y + 5.0f, 16.0f, Theme.TextPrimary, 1.5f });

        const std::string& DocTitle = Session->Asset.Network.NetworkTitle;
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { CubeIndicator.Right() + 10.0f, HeaderTopY + 1.0f },
            13.0f,
            Theme.TextPrimary,
            DocTitle);

        const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 13.0f, DocTitle);
        const Rectangle2D UVTag{ CubeIndicator.Right() + 16.0f + TitleW, HeaderTopY + 2.0f, 28.0f, 17.0f };
        Surface.FillRectangle(UVTag, Theme.AccentSoft, 8.5f);
        Surface.StrokeRectangle(UVTag, Theme.AccentBorder, 8.5f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { UVTag.X + 7.5f, UVTag.Y + 2.5f },
            8.5f,
            Theme.AccentFill,
            "UV");

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { CubeIndicator.Right() + 10.0f, HeaderTopY + 19.5f },
            8.5f,
            Theme.TextMuted,
            "UV WORKSPACE · FAST LIT + MULTI-MATCAP");

        // Right-aligned .pz-mode-capsule (Split | 2D UV | 3D Surface)
        constexpr float   SplitW = 54.0f;
        constexpr float   TwoDW  = 58.0f;
        constexpr float   ThreeW = 78.0f;
        const float       TrackW = SplitW + TwoDW + ThreeW + 10.0f;
        const Rectangle2D TrackRect{ RightX - TrackW, HeaderTopY + 1.0f, TrackW, 31.0f };
        Surface.FillRectangle(TrackRect, Theme.WellBg, 15.5f);
        Surface.StrokeRectangle(TrackRect, Theme.BorderSubtle, 15.5f, 1.0f);

        auto& Projection = ProjectionPane.AccessProjection();
        const auto ActiveSplit = Projection.QuerySplitMode();

        auto ConstructModeSegment = [&](Rectangle2D Seg, const char* Label, Editor::ViewportSplitMode Mode)
        {
            const bool Active  = (ActiveSplit == Mode);
            const bool Hovered = Seg.Contains(Cursor);
            if (Hovered)
            {
                const char* Tip = Mode == Editor::ViewportSplitMode::SplitPlanarAndSpatial
                    ? "Show the UV tile canvas and 3D surface side by side."
                    : (Mode == Editor::ViewportSplitMode::Planar2DOnly
                        ? "Expand the UV tile canvas to fill the viewport."
                        : "Expand the 3D surface view to fill the viewport.");
                ControlAppearance::RecordHoveredTooltip(
                    Seg, OptionalTooltip::FromTitled(Label, Tip));
            }
            if (Active)
            {
                Surface.FillRectangle(Seg, Theme.RaisedBg, 12.5f);
                Surface.StrokeRectangle(Seg, Theme.AccentBorder, 12.5f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Seg, Theme.HoverBg, 12.5f);
            }
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, Label);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Seg.X + (Seg.Width - TW) * 0.5f, Seg.Y + 6.0f },
                9.8f,
                Active ? Theme.TextPrimary : Theme.TextMuted,
                Label);
            if (Hovered && Pressed)
            {
                OverlayCapturedClick = true;
                Projection.ConfigureSplitMode(Mode);
                Session->WorkspaceConfig.AccessUVPreferences().InitialViewportLayout =
                    static_cast<Configuration::UVViewportLayoutMode>(static_cast<uint8_t>(Mode));
                Session->FramebuffersDirty = true;
            }
        };

        const Rectangle2D Seg0{ TrackRect.X + 3.0f, TrackRect.Y + 3.0f, SplitW, 25.0f };
        const Rectangle2D Seg1{ Seg0.Right() + 2.0f, TrackRect.Y + 3.0f, TwoDW, 25.0f };
        const Rectangle2D Seg2{ Seg1.Right() + 2.0f, TrackRect.Y + 3.0f, ThreeW, 25.0f };
        ConstructModeSegment(Seg0, "Split",      Editor::ViewportSplitMode::SplitPlanarAndSpatial);
        ConstructModeSegment(Seg1, "2D UV",      Editor::ViewportSplitMode::Planar2DOnly);
        ConstructModeSegment(Seg2, "3D Surface", Editor::ViewportSplitMode::Spatial3DOnly);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Row 2: .pz-command-strip (+ Unwrap U | Pack ^P | [1 2 3 4] | Surface Material | Debug | View | Wire Seams More)
    //--------------------------------------------------------------------------------------------------------------------
    const float SepY = HeaderTopY + 38.0f;
    Surface.StrokeLine({ LeftX, SepY }, { RightX, SepY }, Theme.BorderSubtle, 1.0f);
    const float StripY = SepY + 7.0f;

    {
        float CurX = LeftX;

        // Primary cream "+ Unwrap | U" capsule (118x31)
        const Rectangle2D UnwrapBtn{ CurX, StripY, 118.0f, 31.0f };
        {
            const bool Hovered = UnwrapBtn.Contains(Cursor);
            Surface.FillRectangle(UnwrapBtn, Hovered ? Theme.PrimaryHover : Theme.PrimaryFill, 15.5f);
            Surface.StrokeRectangle(UnwrapBtn, Theme.PrimaryFill, 15.5f, 1.0f);
            WorkspaceAppearance::ConstructConstructIcon(Surface, { UnwrapBtn.X + 10.0f, UnwrapBtn.Y + 7.5f }, 15.5f, Theme.PrimaryInk);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { UnwrapBtn.X + 30.0f, UnwrapBtn.Y + 8.5f }, 10.5f, Theme.PrimaryInk, "Unwrap");
            Surface.StrokeLine({ UnwrapBtn.X + 86.0f, UnwrapBtn.Y + 8.0f }, { UnwrapBtn.X + 86.0f, UnwrapBtn.Bottom() - 8.0f }, ColorRgba{ 0, 0, 0, 42 }, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { UnwrapBtn.X + 96.0f, UnwrapBtn.Y + 9.5f }, 9.2f, ColorRgba{ 98, 98, 98, 255 }, "U");
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    UnwrapBtn, OptionalTooltip::FromTitled("Unwrap", "Generate UV coordinates for the current selection or mesh."));
            }
            if (Hovered && Pressed)
            {
                OverlayCapturedClick = true;
                (void)Session->ExecuteDirective(UVCommandDirective::UnwrapConformal, nullptr);
            }
        }
        CurX = UnwrapBtn.Right() + 6.0f;

        // Secondary "Pack | ^P" capsule (98x31)
        const Rectangle2D PackBtn{ CurX, StripY, 96.0f, 31.0f };
        {
            const bool Hovered = PackBtn.Contains(Cursor);
            Surface.FillRectangle(PackBtn, Hovered ? Theme.HoverBg : Theme.RaisedBg, 15.5f);
            Surface.StrokeRectangle(PackBtn, Theme.BorderStrong, 15.5f, 1.0f);
            VectorIconRepository::ConstructControl(
                Surface, ControlIcon::LayoutGrid,
                VectorIconPlacement{ PackBtn.X + 11.0f, PackBtn.Y + 8.5f, 14.0f, Theme.TextPrimary, 1.6f });
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PackBtn.X + 30.0f, PackBtn.Y + 8.5f }, 10.5f, Theme.TextPrimary, "Pack");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PackBtn.X + 66.0f, PackBtn.Y + 9.5f }, 8.8f, Theme.TextFaint, "^P");
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    PackBtn, OptionalTooltip::FromTitled("Pack UVs", "Pack islands using the active layer budgets and packing options."));
            }
            if (Hovered && Pressed)
            {
                OverlayCapturedClick = true;
                (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
            }
        }
        CurX = PackBtn.Right() + 8.0f;

        Surface.StrokeLine({ CurX, StripY + 6.0f }, { CurX, StripY + 25.0f }, Theme.BorderSubtle, 1.0f);
        CurX += 8.0f;

        // [ 1 | 2 | 3 | 4 | 5 ] Component Selection Mode Capsule (146x31)
        const Rectangle2D CompGroup{ CurX, StripY, 146.0f, 31.0f };
        Surface.FillRectangle(CompGroup, Theme.WellBg, 15.5f);
        Surface.StrokeRectangle(CompGroup, Theme.BorderSubtle, 15.5f, 1.0f);
        constexpr std::array<UVSelectionMode, 5u> SlotModes {{
            UVSelectionMode::Vertices,
            UVSelectionMode::Edges,
            UVSelectionMode::Faces,
            UVSelectionMode::Islands,
            UVSelectionMode::Layers,
        }};
        constexpr std::array<const char*, 5u> SelectionModeNames {{ "Vertices", "Edges", "Faces", "Islands", "Layers" }};
        constexpr std::array<const char*, 5u> SelectionModeTips {{
            "Select mesh and UV vertices. [1]",
            "Select mesh edges. [2]",
            "Select UV faces. [3]",
            "Select complete UV islands. [4]",
            "Select priority layers. [5]" }};
        constexpr std::array<uint32_t, 4u> ComponentIconOrdinals {{ 0u, 1u, 2u, 3u }};
        for (uint32_t ModeIdx = 0u; ModeIdx < 5u; ++ModeIdx)
        {
            const Rectangle2D Cell{ CompGroup.X + 3.0f + static_cast<float>(ModeIdx) * 28.0f, CompGroup.Y + 3.0f, 26.0f, 25.0f };
            const auto        TargetMode = SlotModes[ModeIdx];
            const bool        Active     = (Session->SelectionMode == TargetMode);
            const bool        Hovered    = Cell.Contains(Cursor);
            if (Active)
            {
                Surface.FillRectangle(Cell, Theme.AccentSoft, 11.0f);
                Surface.StrokeRectangle(Cell, Theme.AccentBorder, 11.0f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Cell, Theme.HoverBg, 11.0f);
            }
            if (TargetMode == UVSelectionMode::Layers)
            {
                VectorIconRepository::ConstructControl(
                    Surface, ControlIcon::LayersSlabs,
                    VectorIconPlacement{ Cell.X + 6.0f, Cell.Y + 5.5f, 14.0f, Active ? Theme.TextPrimary : Theme.TextMuted, 1.6f });
            }
            else
            {
                ConstructComponentModeIcon(
                    Surface, { Cell.X + 6.5f, Cell.Y + 6.0f }, 13.0f,
                    ComponentIconOrdinals[ModeIdx], Active ? Theme.TextPrimary : Theme.TextMuted);
            }
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Cell, OptionalTooltip::FromTitled(SelectionModeNames[ModeIdx], SelectionModeTips[ModeIdx]));
            }
            if (Hovered && Pressed)
            {
                OverlayCapturedClick = true;
                Session->AssignSelectionMode(TargetMode);
            }
        }
        CurX = CompGroup.Right() + 8.0f;

        // Right-aligned Wire | Seams | More capsules (Zoom/Fit moved into per-viewport vertical bottom-right capsules)
        const Rectangle2D MoreBtn { RightX - 58.0f, StripY, 58.0f, 31.0f };
        const Rectangle2D SeamBtn { MoreBtn.X - 6.0f - 58.0f, StripY, 58.0f, 31.0f };
        const Rectangle2D WireBtn { SeamBtn.X - 6.0f - 52.0f, StripY, 52.0f, 31.0f };

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
        if (ConstructCapsuleButton(
                Surface, WireBtn, "Wire", Theme, Cursor, Pressed, Prefs.WireframeOverlayEnabled, false, 9.8f, -1.0f,
                OptionalTooltip::FromText("Toggle 3D surface wireframe overlay.")))
        {
            OverlayCapturedClick          = true;
            Prefs.WireframeOverlayEnabled = !Prefs.WireframeOverlayEnabled;
            Session->FramebuffersDirty    = true;
        }
        if (ConstructCapsuleButton(
                Surface, SeamBtn, "Seams", Theme, Cursor, Pressed, Prefs.SeamOverlayEnabled, false, 9.8f, -1.0f,
                OptionalTooltip::FromText("Toggle the seam overlay in the UV canvas.")))
        {
            OverlayCapturedClick       = true;
            Prefs.SeamOverlayEnabled   = !Prefs.SeamOverlayEnabled;
            Session->FramebuffersDirty = true;
        }
        if (ConstructCapsuleButton(
                Surface, MoreBtn, MoreShelfExpanded ? "More ^" : "More v", Theme, Cursor, Pressed, MoreShelfExpanded, false, 9.8f, -1.0f,
                OptionalTooltip::FromText("Show additional UV workspace tools.")))
        {
            OverlayCapturedClick = true;
            MoreShelfExpanded    = !MoreShelfExpanded;
        }

        // Middle display/debug/view dropdowns when horizontal width permits
        const float MiddleControlsRight = WireBtn.X - 10.0f;
        if (CurX + 138.0f < MiddleControlsRight)
        {
            SurfaceDropdownBounds = { CurX, StripY, 134.0f, 31.0f };
            const ControlContact SurfHit = DropdownSelector::ConstructButton(
                Surface,
                SurfaceDropdownBounds,
                QuerySurfaceDisplayLabel(Session->RenderOptions3D.DisplayMode),
                SurfaceDropdownOpen,
                Input.PrimaryPointer,
                1.0f,
                ControlIcon::LayersSlabs,
                OptionalTooltip::FromTitled(
                    "3D Surface Shading", "Select the 3D surface shading channel."));
            if (SurfHit.Pressed || SurfHit.Clicked)
            {
                OverlayCapturedClick = true;
            }
            if (SurfHit.Clicked)
            {
                SurfaceDropdownOpen        = !SurfaceDropdownOpen;
                CanvasDebugDropdownOpen    = false;
                ProjectionDropdownOpen     = false;
                ViewportDirectiveSheetOpen = false;
            }
            CurX = SurfaceDropdownBounds.Right() + 6.0f;
        }
        if (CurX + 130.0f < MiddleControlsRight)
        {
            CanvasDebugDropdownBounds = { CurX, StripY, 126.0f, 31.0f };
            const ControlContact DbgHit = DropdownSelector::ConstructButton(
                Surface,
                CanvasDebugDropdownBounds,
                QueryCanvasDebugLabel(Session->RenderOptions2D.DisplayMode),
                CanvasDebugDropdownOpen,
                Input.PrimaryPointer,
                1.0f,
                ControlIcon::SlidersQuality,
                OptionalTooltip::FromTitled(
                    "2D UV Debug", "Select the 2D UV canvas inspection channel."));
            if (DbgHit.Pressed || DbgHit.Clicked)
            {
                OverlayCapturedClick = true;
            }
            if (DbgHit.Clicked)
            {
                CanvasDebugDropdownOpen    = !CanvasDebugDropdownOpen;
                SurfaceDropdownOpen        = false;
                ProjectionDropdownOpen     = false;
                ViewportDirectiveSheetOpen = false;
            }
            CurX = CanvasDebugDropdownBounds.Right() + 6.0f;
        }
        if (CurX + 128.0f < MiddleControlsRight)
        {
            ViewProjectionDropdownBounds = { CurX, StripY, 124.0f, 31.0f };
            const auto& Projection = Session->AccessViewportProjection();
            const ControlContact ViewHit = DropdownSelector::ConstructButton(
                Surface,
                ViewProjectionDropdownBounds,
                QuerySpatialProjectionLabel(Projection.InspectSpatialCamera().Preset),
                ProjectionDropdownOpen,
                Input.PrimaryPointer,
                1.0f,
                ControlIcon::DisplayMonitor,
                OptionalTooltip::FromTitled(
                    "3D View Projection", "Choose perspective, orthographic, or an axial camera view."));
            if (ViewHit.Pressed || ViewHit.Clicked)
            {
                OverlayCapturedClick = true;
            }
            if (ViewHit.Clicked)
            {
                ProjectionDropdownOpen     = !ProjectionDropdownOpen;
                SurfaceDropdownOpen        = false;
                CanvasDebugDropdownOpen    = false;
                ViewportDirectiveSheetOpen = false;
            }
        }
    }

    float CanvasTopY = StripY + 39.0f;

    // Optional collapsible quick-ops shelf (.pz-collapsible-ops)
    if (MoreShelfExpanded)
    {
        const Rectangle2D ShelfRect{ LeftX, CanvasTopY, RightX - LeftX, 32.0f };
        Surface.FillRectangle(ShelfRect, Theme.WellBg, 10.0f);
        Surface.StrokeRectangle(ShelfRect, Theme.BorderSubtle, 10.0f, 1.0f);

        float OpX = ShelfRect.X + 6.0f;
        const float OpY = ShelfRect.Y + 4.0f;
        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

        if (ConstructCapsuleButton(
                Surface, { OpX, OpY, 66.0f, 24.0f }, "Checker", Theme, Cursor, Pressed, Prefs.CheckerOverlayEnabled, false, 9.2f, -1.0f,
                OptionalTooltip::FromText("Toggle the UDIM checker background.")))
        {
            OverlayCapturedClick        = true;
            Prefs.CheckerOverlayEnabled = !Prefs.CheckerOverlayEnabled;
            Session->FramebuffersDirty  = true;
        }
        OpX += 70.0f;

        if (ConstructCapsuleButton(
                Surface, { OpX, OpY, 74.0f, 24.0f }, BottomToolRailEnabled ? "Rail: Btm" : "Rail: Left", Theme, Cursor, Pressed, BottomToolRailEnabled, false, 9.0f, -1.0f,
                OptionalTooltip::FromText("Toggle the authoring tool rail between the left vertical edge and bottom horizontal dock.")))
        {
            OverlayCapturedClick  = true;
            BottomToolRailEnabled = !BottomToolRailEnabled;
        }
        OpX += 78.0f;

        struct QuickOperation
        {
            const char* Label;
            const char* Tooltip;
            UVCommandDirective Directive;
        };
        constexpr std::array<QuickOperation, 5u> QuickOps {{
            { "Relax GPU",   "Relax UV stretch using the GPU solver.", UVCommandDirective::RelaxStretchGPU },
            { "Avg Scale",   "Equalize selected island texel density.", UVCommandDirective::EqualizeTexelDensity },
            { "Smart Seams", "Mark seams at sharp geometric boundaries.", UVCommandDirective::MarkSharpSeams },
            { "Stack",       "Stack similar UV islands.", UVCommandDirective::StackSimilarIslands },
            { "Unstack",     "Separate overlapping stacked UV islands.", UVCommandDirective::UnstackOverlapping },
        }};
        for (const auto& Op : QuickOps)
        {
            if (OpX + 78.0f > ShelfRect.Right() - 120.0f)
            {
                break;
            }
            if (ConstructCapsuleButton(
                    Surface, { OpX, OpY, 76.0f, 24.0f }, Op.Label, Theme, Cursor, Pressed, false, false, 9.2f, -1.0f,
                    OptionalTooltip::FromText(Op.Tooltip)))
            {
                OverlayCapturedClick = true;
                (void)Session->ExecuteDirective(Op.Directive, nullptr);
            }
            OpX += 80.0f;
        }

        if (ConstructCapsuleButton(
                Surface, { ShelfRect.Right() - 108.0f, OpY, 48.0f, 24.0f }, "Undo", Theme, Cursor, Pressed, false, false, 9.2f, -1.0f,
                OptionalTooltip::FromText("Undo the last UV workspace edit.")))
        {
            OverlayCapturedClick = true;
            (void)Session->UndoParameterization();
        }
        if (ConstructCapsuleButton(
                Surface, { ShelfRect.Right() - 54.0f, OpY, 48.0f, 24.0f }, "Redo", Theme, Cursor, Pressed, false, false, 9.2f, -1.0f,
                OptionalTooltip::FromText("Redo the last undone UV workspace edit.")))
        {
            OverlayCapturedClick = true;
            (void)Session->RedoParameterization();
        }

        CanvasTopY += 38.0f;
    }

    auto ResolveDropdownSheetAnchor = [&](
        bool&        SheetOpen,
        Rectangle2D  ButtonBounds,
        uint32_t     OptionCount,
        Rectangle2D& OutAnchor,
        Rectangle2D& OutSheetBounds) noexcept
    {
        if (ButtonBounds.Width <= 0.0f)
        {
            SheetOpen = false;
            return;
        }
        if (!SheetOpen)
        {
            return;
        }
        OutAnchor      = ButtonBounds;
        OutSheetBounds = DropdownSelector::QuerySheetBounds(OutAnchor, OptionCount);
        if (OutSheetBounds.Bottom() > Bounds.Bottom())
        {
            OutAnchor.Y -= OutSheetBounds.Height +
                2.0f * ControlMetrics::DropdownPaddingY + OutAnchor.Height;
            const Rectangle2D AboveSheet = DropdownSelector::QuerySheetBounds(OutAnchor, OptionCount);
            if (AboveSheet.Y >= Bounds.Y)
            {
                OutSheetBounds = AboveSheet;
            }
            else
            {
                OutAnchor = ButtonBounds;
            }
        }
        OverlayCapturedClick = OverlayCapturedClick || ButtonBounds.Contains(Cursor) ||
            OutSheetBounds.Contains(Cursor) || Input.PrimaryPointer.Released;
    };

    ResolveDropdownSheetAnchor(SurfaceDropdownOpen, SurfaceDropdownBounds, 6u, SurfaceSheetAnchor, SurfaceSheetBounds);
    ResolveDropdownSheetAnchor(CanvasDebugDropdownOpen, CanvasDebugDropdownBounds, 3u, CanvasDebugSheetAnchor, CanvasDebugSheetBounds);
    ResolveDropdownSheetAnchor(ProjectionDropdownOpen, ViewProjectionDropdownBounds, 5u, ProjectionSheetAnchor, ProjectionSheetBounds);

    //--------------------------------------------------------------------------------------------------------------------
    // 3. viewport canvas Canvas (.pz-viewport canvas-wrap: 10px rounded #080809 GPU viewport + overlay tool Rail + UV Transform capsules)
    //--------------------------------------------------------------------------------------------------------------------
    constexpr float   BottomBarH = 42.0f;
    const Rectangle2D CanvasBounds{
        LeftX,
        CanvasTopY,
        std::max(80.0f, RightX - LeftX),
        std::max(80.0f, Bounds.Bottom() - BottomBarH - CanvasTopY)
    };

    // Pre-compute sub-viewport rectangles and overlay capsules so clicks on the tool rail or bottom-right Zoom/Fit
    // capsules never leak through into 2D/3D GPU picking or empty-space deselection!
    constexpr uint32_t RailToolCount   = 11u;
    const float        HorizontalRailW = std::min(CanvasBounds.Width - 20.0f, 326.0f);
    const Rectangle2D  RailBounds      = BottomToolRailEnabled
        ? Rectangle2D{
              CanvasBounds.X + (CanvasBounds.Width - HorizontalRailW) * 0.5f,
              CanvasBounds.Bottom() - 48.0f,
              HorizontalRailW,
              38.0f }
        : Rectangle2D{
              CanvasBounds.X + 10.0f,
              CanvasBounds.Y + 10.0f,
              38.0f,
              std::min(CanvasBounds.Height - 20.0f, 326.0f) };

    Rectangle2D ExpectedPlanarRect  {};
    Rectangle2D ExpectedSpatialRect {};
    {
        auto& Projection = ProjectionPane.AccessProjection();
        switch (Projection.QuerySplitMode())
        {
            case Editor::ViewportSplitMode::SplitPlanarAndSpatial:
            {
                const float Gap   = 4.0f;
                const float LeftW = std::floor((CanvasBounds.Width - Gap) * Projection.QuerySplitRatio());
                ExpectedPlanarRect  = { CanvasBounds.X, CanvasBounds.Y, LeftW, CanvasBounds.Height };
                ExpectedSpatialRect = { CanvasBounds.X + LeftW + Gap, CanvasBounds.Y, CanvasBounds.Width - LeftW - Gap, CanvasBounds.Height };
                break;
            }
            case Editor::ViewportSplitMode::Planar2DOnly:
                ExpectedPlanarRect = CanvasBounds;
                break;
            case Editor::ViewportSplitMode::Spatial3DOnly:
                ExpectedSpatialRect = CanvasBounds;
                break;
        }
    }

    auto ComputeZoomFitCapsuleBounds = [](Rectangle2D SubViewportRect) noexcept -> Rectangle2D
    {
        if (SubViewportRect.Width < 120.0f || SubViewportRect.Height < 140.0f)
        {
            return {};
        }
        return Rectangle2D{
            SubViewportRect.Right() - 44.0f,
            SubViewportRect.Bottom() - 106.0f,
            34.0f,
            96.0f
        };
    };

    const Rectangle2D PrePlanarNavCapsule  = ComputeZoomFitCapsuleBounds(ExpectedPlanarRect);
    const Rectangle2D PreSpatialNavCapsule = ComputeZoomFitCapsuleBounds(ExpectedSpatialRect);

    std::vector<DropdownDirectiveEntry> DirectiveEntries {};
    std::vector<UVCommandDirective>     DirectiveOps {};
    Rectangle2D                         DirectiveSheetBounds {};
    auto RebuildDirectiveSheet = [&]()
    {
        DirectiveEntries.clear();
        DirectiveOps.clear();
        auto PushRow = [&](const char* Label, const char* Hint, ControlIcon Icon, UVCommandDirective Op, bool Div = false)
        {
            DropdownDirectiveEntry E {};
            E.Label        = Label;
            E.ShortcutHint = Hint;
            E.LeadingIcon  = Icon;
            E.DividerAbove = Div;
            DirectiveEntries.push_back(E);
            DirectiveOps.push_back(Op);
        };
        auto PushCaption = [&](const char* Label)
        {
            DropdownDirectiveEntry E {};
            E.Label          = Label;
            E.SectionCaption = true;
            DirectiveEntries.push_back(E);
            DirectiveOps.push_back(UVCommandDirective::None);
        };

        switch (Session->SelectionMode)
        {
            case UVSelectionMode::Vertices: PushCaption("VERTEX DIRECTIVES"); break;
            case UVSelectionMode::Edges:    PushCaption("EDGE DIRECTIVES");   break;
            case UVSelectionMode::Faces:    PushCaption("FACE DIRECTIVES");   break;
            case UVSelectionMode::Islands:  PushCaption("ISLAND DIRECTIVES"); break;
            case UVSelectionMode::Layers:   PushCaption("LAYER DIRECTIVES");  break;
        }
        PushRow("Select Linked",    "Ctrl+L", ControlIcon::LayersSlabs,  UVCommandDirective::SelectLinkedIsland);
        PushRow("Select All",       "Ctrl+A", ControlIcon::LayoutGrid,   UVCommandDirective::SelectAllComponents);
        PushRow("Invert Selection", "Ctrl+I", ControlIcon::RefreshCycle, UVCommandDirective::InvertComponentSelection);

        if (Session->SelectionMode == UVSelectionMode::Edges)
        {
            PushRow("Mark Seams",          "",      ControlIcon::SparklesAntiAliasing, UVCommandDirective::MarkSelectedSeams, true);
            PushRow("Clear & Weld Seams",  "",      ControlIcon::RefreshCycle,         UVCommandDirective::ClearSelectedSeams);
            PushRow("Stitch Across Seams", "V",     ControlIcon::LayoutSplit,          UVCommandDirective::StitchSelectedSeams);
            PushRow("Align Vertical (U)",  "",      ControlIcon::LayoutColumns,        UVCommandDirective::AlignSelectedUVsU);
            PushRow("Align Horizontal (V)","",      ControlIcon::LayoutRows,           UVCommandDirective::AlignSelectedUVsV);
            PushRow("Pin Edges",           "P",     ControlIcon::LockClosed,           UVCommandDirective::PinSelectedUVs);
            PushRow("Unpin Edges",         "Alt+P", ControlIcon::LockOpen,             UVCommandDirective::UnpinSelectedUVs);
        }
        else if (Session->SelectionMode == UVSelectionMode::Vertices)
        {
            PushRow("Align Vertical (U)",  "",      ControlIcon::LayoutColumns,        UVCommandDirective::AlignSelectedUVsU, true);
            PushRow("Align Horizontal (V)","",      ControlIcon::LayoutRows,           UVCommandDirective::AlignSelectedUVsV);
            PushRow("Pin Vertices",        "P",     ControlIcon::LockClosed,           UVCommandDirective::PinSelectedUVs);
            PushRow("Unpin Vertices",      "Alt+P", ControlIcon::LockOpen,             UVCommandDirective::UnpinSelectedUVs);
        }
        else
        {
            PushRow("Unwrap Conformal", "U",      ControlIcon::LayoutGrid,    UVCommandDirective::UnwrapConformal, true);
            PushRow("Pack UVs",         "Ctrl+P", ControlIcon::CubeShape,     UVCommandDirective::PackUVsConcaveSkyline);
            PushRow("Split Island",     "Y",      ControlIcon::LayoutSplit,   UVCommandDirective::SplitSelectedIsland);
            PushRow("Rectify Quads",    "",       ControlIcon::WrenchRepair,  UVCommandDirective::RectifySelectedQuads);
            PushRow("Mirror U",         "",       ControlIcon::LayoutColumns, UVCommandDirective::MirrorSelectedHorizontal);
            PushRow("Mirror V",         "",       ControlIcon::LayoutRows,    UVCommandDirective::MirrorSelectedVertical);
            PushRow("Pin UVs",          "P",      ControlIcon::LockClosed,    UVCommandDirective::PinSelectedUVs);
            PushRow("Unpin UVs",        "Alt+P",  ControlIcon::LockOpen,      UVCommandDirective::UnpinSelectedUVs);
        }
        PushRow("Fit Selection",   "F", ControlIcon::ExternalOpen, UVCommandDirective::FitActiveSelection, true);
        PushRow("Clear Selection", "",  ControlIcon::CloseCross,   UVCommandDirective::ClearComponentSelection);

        constexpr float SheetW = 232.0f;
        DirectiveSheetBounds = DropdownSelector::QueryDirectiveSheetBounds(ViewportDirectiveAnchor, SheetW, DirectiveEntries);
        if (DirectiveSheetBounds.Right() > Bounds.Right() - 6.0f)
        {
            DirectiveSheetBounds.X = std::max(Bounds.X + 6.0f, Bounds.Right() - 6.0f - DirectiveSheetBounds.Width);
        }
        if (DirectiveSheetBounds.Bottom() > Bounds.Bottom() - 6.0f)
        {
            DirectiveSheetBounds.Y = std::max(Bounds.Y + 6.0f, Bounds.Bottom() - 6.0f - DirectiveSheetBounds.Height);
        }
    };

    if (ViewportDirectiveSheetOpen)
    {
        RebuildDirectiveSheet();
        if (Input.SecondaryPressed && CanvasBounds.Contains(Cursor) && !DirectiveSheetBounds.Contains(Cursor))
        {
            ViewportDirectiveAnchor   = Cursor;
            ViewportDirectiveInPlanar = ExpectedPlanarRect.Contains(Cursor);
            RebuildDirectiveSheet();
        }
    }

    const bool ViewportDragCaptured =
        Interactor.QueryDragCaptureActive() || ProjectionPane.QuerySplitterDragActive();
    const bool SheetCapturedClick = ViewportDirectiveSheetOpen &&
        (DirectiveSheetBounds.Contains(Cursor) || Input.PrimaryPointer.Pressed || Input.SecondaryPressed);
    const bool PointerOverOverlayControls =
        !ViewportDragCaptured && (
            OverlayCapturedClick ||
            SheetCapturedClick ||
            RailBounds.Contains(Cursor) ||
            (PrePlanarNavCapsule.Width > 0.0f && PrePlanarNavCapsule.Contains(Cursor)) ||
            (PreSpatialNavCapsule.Width > 0.0f && PreSpatialNavCapsule.Contains(Cursor)));
    if (PointerOverOverlayControls)
    {
        // The interaction callback is intentionally suppressed over overlays; clear its
        // navigation state so a deferred planar refresh cannot remain stuck indefinitely.
        Session->SpatialNavigationActive = false;
    }

    Rectangle2D PlanarRect  {};
    Rectangle2D SpatialRect {};
    ProjectionPane.RenderViewportCanvas(Surface, CanvasBounds, Input, !PointerOverOverlayControls, &PlanarRect, &SpatialRect);
    LastSpatialRectLogical = SpatialRect;

    {
        Point2D ReqAnchor {};
        bool    ReqPlanar = true;
        if (Interactor.ConsumeSecondarySheetRequest(ReqAnchor, ReqPlanar))
        {
            ViewportDirectiveSheetOpen = true;
            ViewportDirectiveAnchor    = ReqAnchor;
            ViewportDirectiveInPlanar  = ReqPlanar;
            SurfaceDropdownOpen        = false;
            CanvasDebugDropdownOpen    = false;
            ProjectionDropdownOpen     = false;
            RebuildDirectiveSheet();
        }
    }

    // Live Marquee [B] & Circle Brush [C] viewport overlays
    {
        Surface.PushClip(CanvasBounds);
        if (Interactor.QueryRegionStrokeActive() &&
            Interactor.QueryRegionSelectTool() == RegionSelectTool::BoxMarquee)
        {
            const Rectangle2D Marquee = Interactor.QueryMarqueeOverlayRect();
            const Rectangle2D SubClip = Interactor.QueryRegionStrokePlanar() ? PlanarRect : SpatialRect;
            if (SubClip.Width > 1.0f && SubClip.Height > 1.0f)
            {
                Surface.PushClip(SubClip);
                Surface.FillRectangle(Marquee, Theme.AccentSoft, 2.0f);
                Surface.StrokeRectangle(Marquee, Theme.AccentBorder, 2.0f, 1.5f);
                Surface.PopClip();
            }
        }
        else if (Interactor.QueryRegionBrushHoverActive())
        {
            const Point2D BrushCenter = Interactor.QueryBrushOverlayCenter();
            const float   BrushRadius = Interactor.QueryBrushRadiusPixels();
            const Rectangle2D SubClip = PlanarRect.Contains(BrushCenter) ? PlanarRect : SpatialRect;
            if (SubClip.Width > 1.0f && SubClip.Height > 1.0f)
            {
                Surface.PushClip(SubClip);
                Surface.FillCircle(BrushCenter, BrushRadius, ColorRgba{ Theme.AccentBorder.Red, Theme.AccentBorder.Green, Theme.AccentBorder.Blue, 32u });
                Surface.StrokeCircle(BrushCenter, BrushRadius, Theme.AccentBorder, 1.5f);
                Surface.PopClip();
            }
        }
        Surface.PopClip();
    }

    // Subtle viewport corner indicators
    if (PlanarRect.Width >= 140.0f)
    {
        char TileIndicator[64]{};
        const auto TileCfg = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
        std::snprintf(TileIndicator, sizeof(TileIndicator), "UDIM %u · %u px", Session->ActiveUDIMNumber, TileCfg.ResolutionPx);
        const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, TileIndicator) + 18.0f;
        const Rectangle2D CornerTag{ PlanarRect.Right() - BW - 10.0f, PlanarRect.Y + 10.0f, BW, 22.0f };
        Surface.FillRectangle(CornerTag, ColorRgba{ 16, 16, 18, 204 }, 11.0f);
        Surface.StrokeRectangle(CornerTag, ColorRgba{ 255, 255, 255, 24 }, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CornerTag.X + 9.0f, CornerTag.Y + 4.5f }, 9.0f, ColorRgba{ 226, 226, 226, 255 }, TileIndicator);
    }
    if (SpatialRect.Width >= 160.0f)
    {
        const char* ShaderIndicator = QuerySurfaceDisplayLabel(Session->RenderOptions3D.DisplayMode);
        const float BW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, ShaderIndicator) + 18.0f;
        const Rectangle2D CornerTag{ SpatialRect.Right() - BW - 10.0f, SpatialRect.Y + 10.0f, BW, 22.0f };
        Surface.FillRectangle(CornerTag, ColorRgba{ 16, 16, 18, 204 }, 11.0f);
        Surface.StrokeRectangle(CornerTag, ColorRgba{ 255, 255, 255, 24 }, 11.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CornerTag.X + 9.0f, CornerTag.Y + 4.5f }, 9.0f, ColorRgba{ 226, 226, 226, 255 }, ShaderIndicator);
    }

    // 3a. Authoring Tool Rail (Left vertical rail by default; toggleable to Bottom horizontal dock via More shelf)
    {
        auto SyncActiveRailSlotFromState = [&]() noexcept
        {
            if (Session->SeamBrushActive)
            {
                ActiveRailSlot = 7u;
            }
            else if (Interactor.QueryRegionSelectTool() == RegionSelectTool::BoxMarquee)
            {
                ActiveRailSlot = 1u;
            }
            else if (Interactor.QueryRegionSelectTool() == RegionSelectTool::CircleBrush)
            {
                ActiveRailSlot = 2u;
            }
            else
            {
                switch (Session->Gizmo2D.QueryHandleFilter())
                {
                    case Editor::GizmoHandleFilter::TranslateOnly: ActiveRailSlot = 3u; break;
                    case Editor::GizmoHandleFilter::RotateOnly:    ActiveRailSlot = 4u; break;
                    case Editor::GizmoHandleFilter::ScaleOnly:     ActiveRailSlot = 5u; break;
                    case Editor::GizmoHandleFilter::Universal:
                    default:                                       ActiveRailSlot = 0u; break;
                }
            }
        };
        SyncActiveRailSlotFromState();

        Surface.FillRectangle(RailBounds, ColorRgba{ 20, 20, 22, 226 }, 19.0f);
        Surface.StrokeRectangle(RailBounds, ColorRgba{ 255, 255, 255, 28 }, 19.0f, 1.0f);

        constexpr std::array<const char*, RailToolCount> RailToolNames {{
            "Select", "Box Select", "Circle Select", "Move", "Rotate", "Scale",
            "Split Island", "Seam Brush", "Stitch Seams", "Pin UVs", "Relax Stretch" }};
        constexpr std::array<const char*, RailToolCount> RailToolTips {{
            "Select UV or surface components.",
            "Select components inside a rectangular marquee.",
            "Paint-select components with a circular brush.",
            "Move the active UV or spatial selection.",
            "Rotate the active UV or spatial selection.",
            "Scale the active UV or spatial selection.",
            "Split the selected UV island at selected seams.",
            "Toggle seam edges with the seam brush.",
            "Stitch selected seam boundaries together.",
            "Pin selected UV corners in place.",
            "Relax UV stretch using the available solver." }};

        float ToolPos = BottomToolRailEnabled ? (RailBounds.X + 6.0f) : (RailBounds.Y + 6.0f);
        for (uint32_t Slot = 0u; Slot < RailToolCount; ++Slot)
        {
            if (!BottomToolRailEnabled && (ToolPos + 26.0f > RailBounds.Bottom() - 4.0f))
            {
                break;
            }
            if (BottomToolRailEnabled && (ToolPos + 26.0f > RailBounds.Right() - 4.0f))
            {
                break;
            }
            if (Slot == 3u || Slot == 6u)
            {
                if (BottomToolRailEnabled)
                {
                    Surface.StrokeLine({ ToolPos + 1.0f, RailBounds.Y + 8.0f }, { ToolPos + 1.0f, RailBounds.Bottom() - 8.0f }, ColorRgba{ 255, 255, 255, 24 }, 1.0f);
                }
                else
                {
                    Surface.StrokeLine({ RailBounds.X + 8.0f, ToolPos + 1.0f }, { RailBounds.Right() - 8.0f, ToolPos + 1.0f }, ColorRgba{ 255, 255, 255, 24 }, 1.0f);
                }
                ToolPos += 5.0f;
            }
            const Rectangle2D Btn = BottomToolRailEnabled
                ? Rectangle2D{ ToolPos, RailBounds.Y + 6.0f, 26.0f, 26.0f }
                : Rectangle2D{ RailBounds.X + 4.0f, ToolPos, 30.0f, 25.0f };
            const bool SlotActive = (ActiveRailSlot == Slot);
            const bool Hovered    = Btn.Contains(Cursor);
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Btn, OptionalTooltip::FromTitled(RailToolNames[Slot], RailToolTips[Slot]));
            }

            if (SlotActive)
            {
                Surface.FillRectangle(Btn, Theme.AccentSoft, 8.0f);
                Surface.StrokeRectangle(Btn, Theme.AccentBorder, 8.0f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Btn, ColorRgba{ 48, 48, 52, 230 }, 8.0f);
            }

            ConstructRailToolIcon(
                Surface,
                { Btn.X + (Btn.Width - 15.0f) * 0.5f, Btn.Y + (Btn.Height - 15.0f) * 0.5f },
                15.0f,
                Slot,
                SlotActive ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 168, 168, 172, 255 });

            if (Hovered && Pressed)
            {
                switch (Slot)
                {
                    case 0u:
                        Interactor.ResetActiveTools();
                        Session->SeamBrushActive = false;
                        Session->Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
                        Session->Gizmo3D.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
                        Session->StatusBannerText = "Selection Tool Active";
                        break;
                    case 1u:
                        Interactor.ActivateBoxMarquee(Interactor.QueryRegionSelectTool() != RegionSelectTool::BoxMarquee);
                        break;
                    case 2u:
                        Interactor.ActivateCircleBrush(Interactor.QueryRegionSelectTool() != RegionSelectTool::CircleBrush);
                        break;
                    case 3u:
                        Interactor.ResetActiveTools();
                        Session->SeamBrushActive = false;
                        Session->Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::TranslateOnly);
                        Session->Gizmo3D.ConfigureHandleFilter(Editor::GizmoHandleFilter::TranslateOnly);
                        Session->StatusBannerText = "Move Gizmo Active";
                        break;
                    case 4u:
                        Interactor.ResetActiveTools();
                        Session->SeamBrushActive = false;
                        Session->Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::RotateOnly);
                        Session->Gizmo3D.ConfigureHandleFilter(Editor::GizmoHandleFilter::RotateOnly);
                        Session->StatusBannerText = "Rotate Gizmo Active";
                        break;
                    case 5u:
                        Interactor.ResetActiveTools();
                        Session->SeamBrushActive = false;
                        Session->Gizmo2D.ConfigureHandleFilter(Editor::GizmoHandleFilter::ScaleOnly);
                        Session->Gizmo3D.ConfigureHandleFilter(Editor::GizmoHandleFilter::ScaleOnly);
                        Session->StatusBannerText = "Scale Gizmo Active";
                        break;
                    case 6u:
                        (void)Session->ExecuteDirective(UVCommandDirective::SplitSelectedIsland, nullptr);
                        break;
                    case 7u:
                        Session->SeamBrushActive = !Session->SeamBrushActive;
                        if (Session->SeamBrushActive)
                        {
                            Interactor.ResetActiveTools();
                            Session->WorkspaceConfig.AccessUVPreferences().SeamOverlayEnabled = true;
                        }
                        Session->StatusBannerText = Session->SeamBrushActive
                            ? "Seam Brush Active [K]  |  Click Edges to Mark, Alt+Click to Clear, Ctrl+Click Loop"
                            : "Seam Brush Disabled";
                        break;
                    case 8u:
                        (void)Session->ExecuteDirective(UVCommandDirective::StitchSelectedSeams, nullptr);
                        break;
                    case 9u:
                        (void)Session->ExecuteDirective(UVCommandDirective::PinSelectedUVs, nullptr);
                        break;
                    default:
                        (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
                        break;
                }
                SyncActiveRailSlotFromState();
                Session->FramebuffersDirty = true;
            }

            ToolPos += BottomToolRailEnabled ? 28.0f : 27.0f;
        }
    }

    // 3b. Vertical Bottom-Right Zoom/Fit Navigation Capsules (+ | - | Fit icon) on BOTH 2D UV Canvas & 3D Surface Viewport
    auto ConstructViewportZoomFitCapsule = [&](
        Rectangle2D SubViewportRect,
        const char* ZoomInTip,
        const char* ZoomOutTip,
        const char* FitViewTip,
        auto&&      ZoomInCallback,
        auto&&      ZoomOutCallback,
        auto&&      FitCallback)
    {
        const Rectangle2D CapsuleRect = ComputeZoomFitCapsuleBounds(SubViewportRect);
        if (CapsuleRect.Width <= 0.0f)
        {
            return;
        }

        Surface.FillRectangle(CapsuleRect, ColorRgba{ 20, 20, 22, 228 }, 17.0f);
        Surface.StrokeRectangle(CapsuleRect, ColorRgba{ 255, 255, 255, 30 }, 17.0f, 1.0f);

        const Rectangle2D ZoomInCell { CapsuleRect.X + 4.0f, CapsuleRect.Y + 4.0f,  26.0f, 26.0f };
        const Rectangle2D ZoomOutCell{ CapsuleRect.X + 4.0f, CapsuleRect.Y + 35.0f, 26.0f, 26.0f };
        const Rectangle2D FitCell    { CapsuleRect.X + 4.0f, CapsuleRect.Y + 66.0f, 26.0f, 26.0f };

        Surface.StrokeLine(
            { CapsuleRect.X + 8.0f, CapsuleRect.Y + 32.5f },
            { CapsuleRect.Right() - 8.0f, CapsuleRect.Y + 32.5f },
            ColorRgba{ 255, 255, 255, 22 },
            1.0f);
        Surface.StrokeLine(
            { CapsuleRect.X + 8.0f, CapsuleRect.Y + 63.5f },
            { CapsuleRect.Right() - 8.0f, CapsuleRect.Y + 63.5f },
            ColorRgba{ 255, 255, 255, 22 },
            1.0f);

        // 1. Top '+' Zoom In
        {
            const bool Hovered = ZoomInCell.Contains(Cursor);
            if (Hovered)
            {
                Surface.FillRectangle(ZoomInCell, ColorRgba{ 50, 50, 56, 235 }, 13.0f);
                ControlAppearance::RecordHoveredTooltip(ZoomInCell, OptionalTooltip::FromText(ZoomInTip));
            }
            const Point2D C = ZoomInCell.Center();
            const ColorRgba Ink = Hovered ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 196, 196, 202, 255 };
            Surface.StrokeLine({ C.X - 4.8f, C.Y }, { C.X + 4.8f, C.Y }, Ink, 1.55f);
            Surface.StrokeLine({ C.X, C.Y - 4.8f }, { C.X, C.Y + 4.8f }, Ink, 1.55f);
            if (Hovered && Pressed)
            {
                ZoomInCallback();
            }
        }

        // 2. Middle '-' Zoom Out
        {
            const bool Hovered = ZoomOutCell.Contains(Cursor);
            if (Hovered)
            {
                Surface.FillRectangle(ZoomOutCell, ColorRgba{ 50, 50, 56, 235 }, 13.0f);
                ControlAppearance::RecordHoveredTooltip(ZoomOutCell, OptionalTooltip::FromText(ZoomOutTip));
            }
            const Point2D C = ZoomOutCell.Center();
            const ColorRgba Ink = Hovered ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 196, 196, 202, 255 };
            Surface.StrokeLine({ C.X - 4.8f, C.Y }, { C.X + 4.8f, C.Y }, Ink, 1.55f);
            if (Hovered && Pressed)
            {
                ZoomOutCallback();
            }
        }

        // 3. Bottom Fit Icon (4-corner frame brackets)
        {
            const bool Hovered = FitCell.Contains(Cursor);
            if (Hovered)
            {
                Surface.FillRectangle(FitCell, ColorRgba{ 50, 50, 56, 235 }, 13.0f);
                ControlAppearance::RecordHoveredTooltip(FitCell, OptionalTooltip::FromText(FitViewTip));
            }
            const ColorRgba Ink = Hovered ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 196, 196, 202, 255 };
            WorkspaceAppearance::ConstructFocusIcon(
                Surface,
                { FitCell.X + 5.5f, FitCell.Y + 5.5f },
                15.0f,
                Ink);
            if (Hovered && Pressed)
            {
                FitCallback();
            }
        }
    };

    ConstructViewportZoomFitCapsule(
        PlanarRect,
        "Zoom in on the 2D UV canvas.",
        "Zoom out on the 2D UV canvas.",
        "Fit the active UV selection or full UDIM layout.",
        [&]()
        {
            const uint32_t W = static_cast<uint32_t>(std::max(16.0f, PlanarRect.Width));
            const uint32_t H = static_cast<uint32_t>(std::max(16.0f, PlanarRect.Height));
            Session->AccessViewportProjection().ZoomPlanarAroundPoint(
                W, H, PlanarRect.Width * 0.5f, PlanarRect.Height * 0.5f, 1.25f);
            Session->FramebuffersDirty = true;
        },
        [&]()
        {
            const uint32_t W = static_cast<uint32_t>(std::max(16.0f, PlanarRect.Width));
            const uint32_t H = static_cast<uint32_t>(std::max(16.0f, PlanarRect.Height));
            Session->AccessViewportProjection().ZoomPlanarAroundPoint(
                W, H, PlanarRect.Width * 0.5f, PlanarRect.Height * 0.5f, 0.80f);
            Session->FramebuffersDirty = true;
        },
        [&]()
        {
            Session->FitPlanarSelectionOrNetwork();
        });

    ConstructViewportZoomFitCapsule(
        SpatialRect,
        "Zoom the 3D camera in.",
        "Zoom the 3D camera out.",
        "Fit the active selection or complete 3D surface.",
        [&]()
        {
            Session->AccessViewportProjection().DollySpatialCamera(0.80f);
            Session->FramebuffersDirty = true;
        },
        [&]()
        {
            Session->AccessViewportProjection().DollySpatialCamera(1.25f);
            Session->FramebuffersDirty = true;
        },
        [&]()
        {
            Session->FitSpatialSelectionOrNetwork();
        });

    // 3c. Overlay Bottom-Left 2D UV Transform Readout (.pz-uv-transform-capsules)
    if (PlanarRect.Width >= 340.0f)
    {
        const auto Anchor2D = Session->Gizmo2D.QueryPivotOrigin();
        const float ReadoutX = BottomToolRailEnabled ? (PlanarRect.X + 12.0f) : (PlanarRect.X + 56.0f);
        const float ReadoutY = BottomToolRailEnabled ? (PlanarRect.Bottom() - 80.0f) : (PlanarRect.Bottom() - 34.0f);
        const Rectangle2D ReadoutBox{ ReadoutX, ReadoutY, 266.0f, 24.0f };
        Surface.FillRectangle(ReadoutBox, ColorRgba{ 18, 18, 20, 218 }, 12.0f);
        Surface.StrokeRectangle(ReadoutBox, ColorRgba{ 255, 255, 255, 24 }, 12.0f, 1.0f);

        char UText[24]{}, VText[24]{}, SText[24]{}, UdimText[24]{};
        std::snprintf(UText, sizeof(UText), "U %.3f", Anchor2D.X);
        std::snprintf(VText, sizeof(VText), "V %.3f", Anchor2D.Y);
        std::snprintf(SText, sizeof(SText), "S %.2fx", Session->LatestMetrics.MeanL2Stretch);
        std::snprintf(UdimText, sizeof(UdimText), "UDIM %u", Session->ActiveUDIMNumber);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ReadoutBox.X + 11.0f, ReadoutBox.Y + 5.0f }, 9.0f, ColorRgba{ 207, 154, 147, 255 }, UText);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ReadoutBox.X + 74.0f, ReadoutBox.Y + 5.0f }, 9.0f, ColorRgba{ 158, 195, 166, 255 }, VText);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ReadoutBox.X + 137.0f, ReadoutBox.Y + 5.0f }, 9.0f, ColorRgba{ 145, 170, 206, 255 }, SText);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium,  { ReadoutBox.X + 200.0f, ReadoutBox.Y + 5.0f }, 9.0f, ColorRgba{ 214, 184, 124, 255 }, UdimText);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Bottom Status Bar (.pz-bottom-bar: Surface Telemetry | Ready Status)
    //--------------------------------------------------------------------------------------------------------------------
    const Rectangle2D FootRect{ LeftX, Bounds.Bottom() - BottomBarH, CanvasBounds.Width, BottomBarH };
    {
        Surface.FillCircle({ FootRect.X + 6.0f, FootRect.Y + 21.0f }, 3.2f, Theme.ReadyGreen);
        char LeftStats[128]{};
        std::snprintf(
            LeftStats,
            sizeof(LeftStats),
            "%s verts · %s faces · %u charts",
            FormatCountWithCommas(Session->Asset.Network.Vertices.size()).c_str(),
            FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str(),
            CachedChartCount);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { FootRect.X + 15.0f, FootRect.Y + 14.0f },
            9.5f,
            Theme.TextMuted,
            LeftStats);

        // Right-aligned live status readout
        if (FootRect.Width >= 560.0f)
        {
            char RightStats[128]{};
            std::snprintf(
                RightStats,
                sizeof(RightStats),
                "Ready · Stretch: %.2f · Flips: %u",
                Session->LatestMetrics.MeanL2Stretch,
                Session->LatestMetrics.FlippedFaceCount);
            const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, RightStats);
            if (FootRect.Right() - RW > FootRect.X + 220.0f)
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface,
                    FontWeightClass::Regular,
                    { FootRect.Right() - RW, FootRect.Y + 14.0f },
                    9.5f,
                    Theme.TextMuted,
                    RightStats);
            }
        }
    }

    if (SurfaceDropdownOpen && SurfaceDropdownBounds.Width > 0.0f)
    {
        constexpr std::array<const char*, 6u> SurfaceOptions {{
            "Surface Material",
            "Priority Tint",
            "Stretch Heat",
            "UDIM Checker",
            "MatCap Preview",
            "Material + MatCap",
        }};
        const uint32_t CurrentIndex = static_cast<uint32_t>(Session->RenderOptions3D.DisplayMode);
        uint32_t ChosenIndex = CurrentIndex;
        const ControlContact Choice = DropdownSelector::ConstructOptionSheet(
            Surface,
            SurfaceSheetAnchor,
            SurfaceOptions.data(),
            static_cast<uint32_t>(SurfaceOptions.size()),
            std::min<uint32_t>(CurrentIndex, static_cast<uint32_t>(SurfaceOptions.size() - 1u)),
            Input.PrimaryPointer,
            ChosenIndex);
        if (Choice.Clicked && ChosenIndex < SurfaceOptions.size())
        {
            Session->RenderOptions3D.DisplayMode = static_cast<GeometricRaster::SurfaceDisplayMode>(ChosenIndex);
            Session->FramebuffersDirty           = true;
            SurfaceDropdownOpen                  = false;
        }
        else if (Input.PrimaryPointer.Released &&
                 !SurfaceDropdownBounds.Contains(Cursor) && !SurfaceSheetBounds.Contains(Cursor))
        {
            SurfaceDropdownOpen = false;
        }
    }

    if (CanvasDebugDropdownOpen && CanvasDebugDropdownBounds.Width > 0.0f)
    {
        constexpr std::array<const char*, 3u> DebugOptions {{
            "Debug: Priority",
            "Debug: Stretch",
            "Debug: Overlaps",
        }};
        const uint32_t CurrentIndex = static_cast<uint32_t>(Session->RenderOptions2D.DisplayMode);
        uint32_t ChosenIndex = CurrentIndex;
        const ControlContact Choice = DropdownSelector::ConstructOptionSheet(
            Surface,
            CanvasDebugSheetAnchor,
            DebugOptions.data(),
            static_cast<uint32_t>(DebugOptions.size()),
            std::min<uint32_t>(CurrentIndex, static_cast<uint32_t>(DebugOptions.size() - 1u)),
            Input.PrimaryPointer,
            ChosenIndex);
        if (Choice.Clicked && ChosenIndex < DebugOptions.size())
        {
            Session->RenderOptions2D.DisplayMode = static_cast<GeometricRaster::UVCanvasDisplayMode>(ChosenIndex);
            Session->FramebuffersDirty           = true;
            CanvasDebugDropdownOpen              = false;
        }
        else if (Input.PrimaryPointer.Released &&
                 !CanvasDebugDropdownBounds.Contains(Cursor) && !CanvasDebugSheetBounds.Contains(Cursor))
        {
            CanvasDebugDropdownOpen = false;
        }
    }

    if (ProjectionDropdownOpen && ViewProjectionDropdownBounds.Width > 0.0f)
    {
        constexpr std::array<const char*, 5u> ProjectionOptions {{
            "Perspective", "Orthographic", "Top", "Front", "Right" }};
        const uint32_t CurrentIndex = static_cast<uint32_t>(
            Session->AccessViewportProjection().InspectSpatialCamera().Preset);
        uint32_t ChosenIndex = CurrentIndex;
        const ControlContact Choice = DropdownSelector::ConstructOptionSheet(
            Surface,
            ProjectionSheetAnchor,
            ProjectionOptions.data(),
            static_cast<uint32_t>(ProjectionOptions.size()),
            std::min<uint32_t>(CurrentIndex, static_cast<uint32_t>(ProjectionOptions.size() - 1u)),
            Input.PrimaryPointer,
            ChosenIndex);
        if (Choice.Clicked && ChosenIndex < ProjectionOptions.size())
        {
            Session->AccessViewportProjection().ApplySpatialPreset(
                static_cast<Editor::SpatialProjectionPreset>(ChosenIndex));
            Session->FramebuffersDirty = true;
            ProjectionDropdownOpen = false;
        }
        else if (Input.PrimaryPointer.Released &&
                 !ViewProjectionDropdownBounds.Contains(Cursor) && !ProjectionSheetBounds.Contains(Cursor))
        {
            ProjectionDropdownOpen = false;
        }
    }

    if (ViewportDirectiveSheetOpen && !DirectiveEntries.empty())
    {
        int32_t ChosenIndex = -1;
        const ControlContact SheetHit = DropdownSelector::ConstructDirectiveSheet(
            Surface,
            DirectiveSheetBounds,
            DirectiveEntries,
            Input.PrimaryPointer,
            ChosenIndex);
        if (SheetHit.Clicked && ChosenIndex >= 0 && ChosenIndex < static_cast<int32_t>(DirectiveOps.size()))
        {
            const UVCommandDirective ChosenOp = DirectiveOps[static_cast<std::size_t>(ChosenIndex)];
            if (ChosenOp != UVCommandDirective::None)
            {
                (void)Session->ExecuteDirective(ChosenOp, nullptr);
            }
            ViewportDirectiveSheetOpen = false;
        }
        else if ((Input.PrimaryPointer.Pressed || Input.PrimaryPointer.Released) &&
                 !DirectiveSheetBounds.Contains(Cursor))
        {
            ViewportDirectiveSheetOpen = false;
        }
    }


    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(Surface, Bounds, Input.PrimaryPointer);
}

void UVViewportHost::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##UVViewportHostCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };
    PaneScreenOrigin = { ScreenBounds.X, ScreenBounds.Y };
    PaneScaleFactor  = SafeScale;

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);

    if (Session != nullptr)
    {
        const Point2D PhysicalCursor {
            ScreenBounds.X + Input.PrimaryPointer.X * SafeScale,
            ScreenBounds.Y + Input.PrimaryPointer.Y * SafeScale,
        };
        const bool BlockedByBrowser = [&]() noexcept
        {
            if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
            {
                return Browser->QueryBlocksPointerAt(PhysicalCursor) &&
                    !Interactor.QueryDragCaptureActive() &&
                    !ProjectionPane.QuerySplitterDragActive();
            }
            return false;
        }();
        const bool BlockedByOwner =
            Session->PointerOwnerPane != UVPointerOwnerPane::None &&
            Session->PointerOwnerPane != UVPointerOwnerPane::ViewportHost;
        if (BlockedByBrowser || BlockedByOwner)
        {
            Input.PrimaryPointer.Enabled  = false;
            Input.PrimaryPointer.Pressed  = false;
            Input.PrimaryPointer.Down     = false;
            Input.PrimaryPointer.Released = false;
            Input.SecondaryPressed        = false;
            Input.SecondaryDown           = false;
            Input.MiddlePressed           = false;
            Input.MiddleDown              = false;
            Input.DoubleClicked           = false;
            Input.WheelDeltaY             = 0.0f;
        }
    }

    Render(Surface, LogicalBounds, Input, Δt);

    if (Session != nullptr)
    {
        const bool HostCaptured = Interactor.QueryDragCaptureActive() || ProjectionPane.QuerySplitterDragActive();
        if (HostCaptured)
        {
            Session->PointerOwnerPane = UVPointerOwnerPane::ViewportHost;
        }
        else if (Session->PointerOwnerPane == UVPointerOwnerPane::ViewportHost &&
                 !Input.PrimaryPointer.Down && !Input.MiddleDown && !Input.SecondaryDown)
        {
            Session->PointerOwnerPane = UVPointerOwnerPane::None;
        }
    }

    if (Session != nullptr)
    {
        if (ContentBrowserPanel* Browser = Session->QueryBrowserPanel())
        {
            Rectangle2D BrowserSpatialBounds {};
            if (LastSpatialRectLogical.Width > 8.0f && LastSpatialRectLogical.Height > 8.0f)
            {
                const Rectangle2D PhysicalSpatial {
                    PaneScreenOrigin.X + LastSpatialRectLogical.X * SafeScale,
                    PaneScreenOrigin.Y + LastSpatialRectLogical.Y * SafeScale,
                    LastSpatialRectLogical.Width * SafeScale,
                    LastSpatialRectLogical.Height * SafeScale,
                };
                const float DrawerTopY = Browser->QueryDrawerTopPhysicalY();
                if (PhysicalSpatial.Y < DrawerTopY)
                {
                    Rectangle2D ClippedPhysical = PhysicalSpatial;
                    if (ClippedPhysical.Bottom() > DrawerTopY)
                    {
                        ClippedPhysical.Height = std::max(0.0f, DrawerTopY - ClippedPhysical.Y);
                    }
                    if (ClippedPhysical.Height >= 16.0f)
                    {
                        BrowserSpatialBounds = Browser->MapPhysicalToLogicalBounds(ClippedPhysical);
                    }
                }
            }

            Browser->RegisterMatCapSlot(MatCapSlotRegistration{
                "uv.viewport3d.surface",
                "3D Viewport Surface MatCap",
                BrowserSpatialBounds,
                [Sess = Session](uint8_t SlotOrdinal, const ContentRepositoryEntry&)
                {
                    if (Sess != nullptr)
                    {
                        Sess->ApplyMatCapToActiveSelection(SlotOrdinal, true);
                    }
                },
            });
            Browser->RegisterColourSlot(ColourSlotRegistration{
                "uv.viewport3d.albedo_colour",
                "3D Surface Albedo Colour",
                BrowserSpatialBounds,
                nullptr,
                [Sess = Session](ColorRgba AssignedRgba, const ColourCodexEntry&)
                {
                    if (Sess != nullptr)
                    {
                        Sess->ApplySurfaceAlbedoColour(
                            {
                                static_cast<float>(AssignedRgba.Red) / 255.0f,
                                static_cast<float>(AssignedRgba.Green) / 255.0f,
                                static_cast<float>(AssignedRgba.Blue) / 255.0f,
                            },
                            true);
                    }
                },
            });
        }
    }
}

} // namespace Frontier::Workspaces
