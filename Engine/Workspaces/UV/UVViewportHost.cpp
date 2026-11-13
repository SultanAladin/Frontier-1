/*============================================================================================================================================
                                                           UVVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportHost matching Experimental/UVEditor viewport canvas header, command strip, tool rail & status bar

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportHost.h"

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
            Interactor.ProcessViewportInput(Reading);
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

    SynchronizeProjectionPolicy();
    Session->AdvanceWorkspaceCamera(static_cast<float>(Δt));
    RefreshCachedIslandCount();

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    if (Input.EscapePressed)
    {
        SurfaceDropdownOpen     = false;
        CanvasDebugDropdownOpen = false;
        ProjectionDropdownOpen  = false;
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

        // [ 1 | 2 | 3 | 4 ] Component Selection Mode Capsule (118x31)
        const Rectangle2D CompGroup{ CurX, StripY, 118.0f, 31.0f };
        Surface.FillRectangle(CompGroup, Theme.WellBg, 15.5f);
        Surface.StrokeRectangle(CompGroup, Theme.BorderSubtle, 15.5f, 1.0f);
        constexpr std::array<UVSelectionMode, 4u> SlotModes {{
            UVSelectionMode::Faces,
            UVSelectionMode::Edges,
            UVSelectionMode::Islands,
            UVSelectionMode::Layers,
        }};
        constexpr std::array<const char*, 4u> SelectionModeNames {{ "Faces", "Edges", "Islands", "Layers" }};
        constexpr std::array<const char*, 4u> SelectionModeTips {{
            "Select UV faces.", "Select mesh edges.", "Select complete UV islands.", "Select priority layers." }};
        constexpr std::array<uint32_t, 3u> ComponentGlyphIndices {{ 2u, 1u, 3u }};
        for (uint32_t ModeIdx = 0u; ModeIdx < 4u; ++ModeIdx)
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
                    ComponentGlyphIndices[ModeIdx], Active ? Theme.TextPrimary : Theme.TextMuted);
            }
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Cell, OptionalTooltip::FromTitled(SelectionModeNames[ModeIdx], SelectionModeTips[ModeIdx]));
            }
            if (Hovered && Pressed)
            {
                OverlayCapturedClick       = true;
                Session->SelectionMode     = TargetMode;
                Session->FramebuffersDirty = true;
            }
        }
        CurX = CompGroup.Right() + 8.0f;

        // Right-aligned Wire | Seams | More capsules
        const Rectangle2D MoreBtn { RightX - 58.0f, StripY, 58.0f, 31.0f };
        const Rectangle2D SeamBtn { MoreBtn.X - 6.0f - 58.0f, StripY, 58.0f, 31.0f };
        const Rectangle2D WireBtn   { SeamBtn.X - 6.0f - 52.0f, StripY, 52.0f, 31.0f };
        const Rectangle2D Fit3DBtn  { WireBtn.X - 103.0f, StripY, 40.0f, 31.0f };
        const Rectangle2D ZoomOutBtn{ WireBtn.X - 62.0f, StripY, 28.0f, 31.0f };
        const Rectangle2D ZoomInBtn { WireBtn.X - 31.0f, StripY, 28.0f, 31.0f };

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
        if (ConstructCapsuleButton(
                Surface, WireBtn, "Wire", Theme, Cursor, Pressed, Prefs.WireframeOverlayEnabled, false, 9.8f, -1.0f,
                OptionalTooltip::FromText("Toggle 3D mesh wireframe overlay.")))
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
        if (ConstructCapsuleButton(
                Surface, Fit3DBtn, "Fit", Theme, Cursor, Pressed, false, false, 10.0f, -1.0f,
                OptionalTooltip::FromText("Frame the complete object in the 3D viewport.")))
        {
            OverlayCapturedClick = true;
            const auto& Vertices = Session->Asset.Network.Vertices;
            if (!Vertices.empty())
            {
                Mathematics::Axis3D Min { 1.0e9f, 1.0e9f, 1.0e9f };
                Mathematics::Axis3D Max { -1.0e9f, -1.0e9f, -1.0e9f };
                for (const auto& Vertex : Vertices)
                {
                    Min = { std::min(Min.X, Vertex.Axes.X), std::min(Min.Y, Vertex.Axes.Y), std::min(Min.Z, Vertex.Axes.Z) };
                    Max = { std::max(Max.X, Vertex.Axes.X), std::max(Max.Y, Vertex.Axes.Y), std::max(Max.Z, Vertex.Axes.Z) };
                }
                Session->AccessViewportProjection().FitSpatialExtents(Min, Max);
                Session->FramebuffersDirty = true;
            }
        }
        if (ConstructCapsuleButton(
                Surface, ZoomOutBtn, "-", Theme, Cursor, Pressed, false, false, 14.0f, -1.0f,
                OptionalTooltip::FromText("Zoom the 3D camera out.")))
        {
            OverlayCapturedClick = true;
            Session->AccessViewportProjection().DollySpatialCamera(1.25f);
            Session->FramebuffersDirty = true;
        }
        if (ConstructCapsuleButton(
                Surface, ZoomInBtn, "+", Theme, Cursor, Pressed, false, false, 14.0f, -1.0f,
                OptionalTooltip::FromText("Zoom the 3D camera in.")))
        {
            OverlayCapturedClick = true;
            Session->AccessViewportProjection().DollySpatialCamera(0.80f);
            Session->FramebuffersDirty = true;
        }

        // Middle display/debug/view dropdowns when horizontal width permits
        const float MiddleControlsRight = Fit3DBtn.X - 8.0f;
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
            if (SurfHit.Clicked)
            {
                SurfaceDropdownOpen     = !SurfaceDropdownOpen;
                CanvasDebugDropdownOpen = false;
                ProjectionDropdownOpen  = false;
                OverlayCapturedClick    = true;
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
            if (DbgHit.Clicked)
            {
                CanvasDebugDropdownOpen = !CanvasDebugDropdownOpen;
                SurfaceDropdownOpen     = false;
                ProjectionDropdownOpen  = false;
                OverlayCapturedClick    = true;
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
            if (ViewHit.Clicked)
            {
                ProjectionDropdownOpen  = !ProjectionDropdownOpen;
                SurfaceDropdownOpen     = false;
                CanvasDebugDropdownOpen = false;
                OverlayCapturedClick    = true;
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
        OpX += 72.0f;

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

    // Pre-check overlay overlays so clicks on tool rail or asset drawer do not trigger 3D/2D picking underneath!
    const Rectangle2D RailBounds{
        CanvasBounds.X + 10.0f,
        CanvasBounds.Y + 10.0f,
        38.0f,
        std::min(CanvasBounds.Height - 20.0f, 356.0f)
    };

    const bool PointerOverOverlayControls =
        OverlayCapturedClick ||
        RailBounds.Contains(Cursor);
    if (PointerOverOverlayControls)
    {
        // The interaction callback is intentionally suppressed over overlays; clear its
        // navigation state so a deferred planar refresh cannot remain stuck indefinitely.
        Session->SpatialNavigationActive = false;
    }

    Rectangle2D PlanarRect  {};
    Rectangle2D SpatialRect {};
    ProjectionPane.RenderViewportCanvas(Surface, CanvasBounds, Input, !PointerOverOverlayControls, &PlanarRect, &SpatialRect);

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

    // 3a. overlay Left tool Tool Rail (.pz-tool-rail)
    {
        Surface.FillRectangle(RailBounds, ColorRgba{ 20, 20, 22, 226 }, 15.0f);
        Surface.StrokeRectangle(RailBounds, ColorRgba{ 255, 255, 255, 28 }, 15.0f, 1.0f);

        constexpr std::array<const char*, 12u> RailToolNames {{
            "Select", "Box Select", "Circle Select", "Move", "Rotate", "Scale",
            "Split Island", "Seam Brush", "Stitch Seams", "Pin UVs", "Relax Stretch", "Frame View" }};
        constexpr std::array<const char*, 12u> RailToolTips {{
            "Select UV or mesh components.",
            "Select components inside a rectangular marquee.",
            "Paint-select components with a circular brush.",
            "Move the active UV or spatial selection.",
            "Rotate the active UV or spatial selection.",
            "Scale the active UV or spatial selection.",
            "Split the selected UV island at selected seams.",
            "Toggle seam edges with the seam brush.",
            "Stitch selected seam boundaries together.",
            "Pin selected UV corners in place.",
            "Relax UV stretch using the available solver.",
            "Frame the current selection or complete network." }};
        float ToolY = RailBounds.Y + 5.0f;
        for (uint32_t Slot = 0u; Slot < 12u; ++Slot)
        {
            if (ToolY + 26.0f > RailBounds.Bottom() - 4.0f)
            {
                break;
            }
            if (Slot == 3u || Slot == 6u)
            {
                Surface.StrokeLine({ RailBounds.X + 8.0f, ToolY + 1.0f }, { RailBounds.Right() - 8.0f, ToolY + 1.0f }, ColorRgba{ 255, 255, 255, 24 }, 1.0f);
                ToolY += 5.0f;
            }
            const Rectangle2D Btn{ RailBounds.X + 4.0f, ToolY, 30.0f, 25.0f };
            const bool        IsActive = (ActiveRailSlot == Slot);
            const bool        Hovered  = Btn.Contains(Cursor);
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Btn, OptionalTooltip::FromTitled(RailToolNames[Slot], RailToolTips[Slot]));
            }

            if (IsActive)
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
                { Btn.X + 7.5f, Btn.Y + 5.0f },
                15.0f,
                Slot,
                IsActive ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 168, 168, 172, 255 });

            if (Hovered && Pressed)
            {
                switch (Slot)
                {
                    case 0u: ActiveRailSlot = 0u; Session->SeamBrushActive = false; break;
                    case 1u: ActiveRailSlot = 1u; Session->SeamBrushActive = false; break;
                    case 2u: ActiveRailSlot = 2u; Session->SeamBrushActive = false; break;
                    case 3u: ActiveRailSlot = 3u; Session->SeamBrushActive = false; break;
                    case 4u: ActiveRailSlot = 4u; Session->SeamBrushActive = false; break;
                    case 5u: ActiveRailSlot = 5u; Session->SeamBrushActive = false; break;
                    case 6u: (void)Session->ExecuteDirective(UVCommandDirective::SplitSelectedIsland, nullptr); break;
                    case 7u: ActiveRailSlot = 7u; Session->SeamBrushActive = !Session->SeamBrushActive; break;
                    case 8u: (void)Session->ExecuteDirective(UVCommandDirective::StitchSelectedSeams, nullptr); break;
                    case 9u: (void)Session->ExecuteDirective(UVCommandDirective::PinSelectedUVs, nullptr);      break;
                    case 10u: (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);    break;
                    default: Session->FitSelectionOrNetwork(); break;
                }
                Session->FramebuffersDirty = true;
            }

            ToolY += 27.0f;
        }
    }

    // 3b. overlay Bottom-Left 2D UV Transform Readout (.pz-uv-transform-capsules)
    if (CanvasBounds.Width >= 340.0f)
    {
        const auto Anchor2D = Session->Gizmo2D.QueryPivotOrigin();
        const Rectangle2D ReadoutBox{ CanvasBounds.X + 56.0f, CanvasBounds.Bottom() - 34.0f, 266.0f, 24.0f };
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

    PixelSpace             Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(Surface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
