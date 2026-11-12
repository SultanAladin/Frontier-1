/*============================================================================================================================================
                                                           UVVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVViewportHost matching Experimental/UVEditor viewport canvas header, command strip, tool rail & status bar

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVViewportHost.h"

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
        case GeometricRaster::SurfaceDisplayMode::StudioLitAndMatCap: return "Surface Material";
        case GeometricRaster::SurfaceDisplayMode::PriorityLayerTint:  return "Priority Tint";
        case GeometricRaster::SurfaceDisplayMode::StretchHeatmap:     return "Stretch Heat";
        case GeometricRaster::SurfaceDisplayMode::UDIMCheckerPreview: return "UDIM Checker";
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

} // namespace

UVViewportHost::UVViewportHost()
{
    ProjectionPane.ConfigurePaneIndicators("2D UDIM UVTILE", "3D FAST LIT + MATCAP");
}

UVViewportHost::~UVViewportHost() = default;

void UVViewportHost::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    RetainedUVRevision = UINT64_MAX;
    Interactor.AttachSession(Session);

    if (Session == nullptr)
    {
        ProjectionPane.AttachDocument(nullptr);
        ProjectionPane.AssignInteractionCallback({});
        ProjectionPane.AssignRasterBufferSource({});
        ProjectionPane.AssignSplitModeCallback({});
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

void UVViewportHost::RefreshRetainedIslandCount() noexcept
{
    if (Session == nullptr)
    {
        return;
    }
    if (RetainedUVRevision != Session->Asset.Network.UVRevision)
    {
        RetainedUVRevision = Session->Asset.Network.UVRevision;
        RetainedChartCount = static_cast<uint32_t>(SeamCutGenerator::PartitionConnectedUVIslands(Session->Asset.Network).size());
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
    RefreshRetainedIslandCount();

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;

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
        ConstructSceneCubeIcon(Surface, { CubeIndicator.X + 5.0f, CubeIndicator.Y + 5.0f }, 16.0f, Theme.TextPrimary);

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
            ConstructPackGridIcon(Surface, { PackBtn.X + 11.0f, PackBtn.Y + 8.5f }, 14.0f, Theme.TextPrimary);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PackBtn.X + 30.0f, PackBtn.Y + 8.5f }, 10.5f, Theme.TextPrimary, "Pack");
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { PackBtn.X + 66.0f, PackBtn.Y + 9.5f }, 8.8f, Theme.TextFaint, "^P");
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
            ConstructComponentModeIcon(Surface, { Cell.X + 6.5f, Cell.Y + 6.0f }, 13.0f, ModeIdx, Active ? Theme.TextPrimary : Theme.TextMuted);
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
        const Rectangle2D WireBtn { SeamBtn.X - 6.0f - 52.0f, StripY, 52.0f, 31.0f };

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();
        if (ConstructCapsuleButton(Surface, WireBtn, "Wire", Theme, Cursor, Pressed, Prefs.WireframeOverlayEnabled, false, 9.8f))
        {
            OverlayCapturedClick          = true;
            Prefs.WireframeOverlayEnabled = !Prefs.WireframeOverlayEnabled;
            Session->FramebuffersDirty    = true;
        }
        if (ConstructCapsuleButton(Surface, SeamBtn, "Seams", Theme, Cursor, Pressed, Prefs.SeamOverlayEnabled, false, 9.8f))
        {
            OverlayCapturedClick       = true;
            Prefs.SeamOverlayEnabled   = !Prefs.SeamOverlayEnabled;
            Session->FramebuffersDirty = true;
        }
        if (ConstructCapsuleButton(Surface, MoreBtn, MoreShelfExpanded ? "More ^" : "More v", Theme, Cursor, Pressed, MoreShelfExpanded, false, 9.8f))
        {
            OverlayCapturedClick = true;
            MoreShelfExpanded    = !MoreShelfExpanded;
        }

        // Middle display/debug/view capsules when horizontal width permits
        if (CurX + 126.0f < WireBtn.X - 8.0f)
        {
            const Rectangle2D SurfBtn{ CurX, StripY, 122.0f, 31.0f };
            if (ConstructCapsuleButton(Surface, SurfBtn, QuerySurfaceDisplayLabel(Session->RenderOptions3D.DisplayMode), Theme, Cursor, Pressed, false, false, 9.6f))
            {
                OverlayCapturedClick = true;
                const uint32_t Next = (static_cast<uint32_t>(Session->RenderOptions3D.DisplayMode) + 1u) % 4u;
                Session->RenderOptions3D.DisplayMode = static_cast<GeometricRaster::SurfaceDisplayMode>(Next);
                Session->FramebuffersDirty           = true;
            }
            CurX = SurfBtn.Right() + 6.0f;
        }
        if (CurX + 112.0f < WireBtn.X - 8.0f)
        {
            const Rectangle2D DbgBtn{ CurX, StripY, 108.0f, 31.0f };
            if (ConstructCapsuleButton(Surface, DbgBtn, QueryCanvasDebugLabel(Session->RenderOptions2D.DisplayMode), Theme, Cursor, Pressed, false, false, 9.6f))
            {
                OverlayCapturedClick = true;
                const uint32_t Next = (static_cast<uint32_t>(Session->RenderOptions2D.DisplayMode) + 1u) % 3u;
                Session->RenderOptions2D.DisplayMode = static_cast<GeometricRaster::UVCanvasDisplayMode>(Next);
                Session->FramebuffersDirty           = true;
            }
            CurX = DbgBtn.Right() + 6.0f;
        }
        if (CurX + 98.0f < WireBtn.X - 8.0f)
        {
            const Rectangle2D ViewBtn{ CurX, StripY, 96.0f, 31.0f };
            if (ConstructCapsuleButton(Surface, ViewBtn, "Perspective", Theme, Cursor, Pressed, false, false, 9.6f))
            {
                OverlayCapturedClick = true;
                Session->FitSelectionOrNetwork();
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

        if (ConstructCapsuleButton(Surface, { OpX, OpY, 66.0f, 24.0f }, "Checker", Theme, Cursor, Pressed, Prefs.CheckerOverlayEnabled, false, 9.2f))
        {
            OverlayCapturedClick        = true;
            Prefs.CheckerOverlayEnabled = !Prefs.CheckerOverlayEnabled;
            Session->FramebuffersDirty  = true;
        }
        OpX += 72.0f;

        const std::array<std::pair<const char*, UVCommandDirective>, 5u> QuickOps {{
            { "Relax GPU",   UVCommandDirective::RelaxStretchGPU       },
            { "Avg Scale",   UVCommandDirective::EqualizeTexelDensity  },
            { "Smart Seams", UVCommandDirective::MarkSharpSeams        },
            { "Stack",       UVCommandDirective::StackSimilarIslands   },
            { "Unstack",     UVCommandDirective::UnstackOverlapping    },
        }};
        for (const auto& [Cap, Dir] : QuickOps)
        {
            if (OpX + 78.0f > ShelfRect.Right() - 120.0f)
            {
                break;
            }
            if (ConstructCapsuleButton(Surface, { OpX, OpY, 76.0f, 24.0f }, Cap, Theme, Cursor, Pressed, false, false, 9.2f))
            {
                OverlayCapturedClick = true;
                (void)Session->ExecuteDirective(Dir, nullptr);
            }
            OpX += 80.0f;
        }

        if (ConstructCapsuleButton(Surface, { ShelfRect.Right() - 108.0f, OpY, 48.0f, 24.0f }, "Undo", Theme, Cursor, Pressed, false, false, 9.2f))
        {
            OverlayCapturedClick = true;
            (void)Session->UndoParameterization();
        }
        if (ConstructCapsuleButton(Surface, { ShelfRect.Right() - 54.0f, OpY, 48.0f, 24.0f }, "Redo", Theme, Cursor, Pressed, false, false, 9.2f))
        {
            OverlayCapturedClick = true;
            (void)Session->RedoParameterization();
        }

        CanvasTopY += 38.0f;
    }

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
            const bool        RailSlotActive = (ActiveRailSlot == Slot);
            const bool        Hovered  = Btn.Contains(Cursor);

            if (RailSlotActive)
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
                RailSlotActive ? ColorRgba{ 244, 244, 240, 255 } : ColorRgba{ 168, 168, 172, 255 });

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
            RetainedChartCount);
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

    Surface.PopClip();
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
