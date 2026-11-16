/*============================================================================================================================================
                                                         FRACTUREWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 FractureWorkspaceHost implementation: 1:1 HTML 56px Studio Header, slanted construct tabs, 3-panel shell, Solver Matrix & Status Bar

#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;
using GeometrySpace::Fracture::FractureSolverMethod;

} // namespace

FractureWorkspaceHost::FractureWorkspaceHost()
{
    Viewport.AttachRenderer(&Renderer);
}

void FractureWorkspaceHost::AttachVulkanDevice(
    GeometricRaster::VulkanInterchange& TargetDevice,
    const std::filesystem::path&        CompiledShaderDirectory) noexcept
{
    Session.VulkanSpirvReady = Renderer.InitializeVulkanShaders(TargetDevice, CompiledShaderDirectory);
}

void FractureWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Fracture);
    }
}

void FractureWorkspaceHost::RenderStudioHeader(
    PixelSpace&           Surface,
    Rectangle2D           HeaderRect,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(HeaderRect, C.CanvasBg, 0.0f);

    const float CY = HeaderRect.Center().Y;
    float X = HeaderRect.X + 18.0f;

    // 1. Brand mark: "frontier." + FRACTURE STUDIO badge
    Surface.Text({ X, CY - 10.5f }, 19.0f, C.TextPrimary, "frontier");
    const float FrontierW = Surface.MeasureTextWidth(19.0f, "frontier");
    Surface.FillCircle({ X + FrontierW + 3.0f, CY + 4.5f }, 2.2f, C.WarmAccent);
    X += FrontierW + 16.0f;

    const Rectangle2D BadgePill { X, CY - 10.5f, 108.0f, 21.0f };
    Surface.StrokeRectangle(BadgePill, C.HairlineStrong, 10.5f, 1.0f);
    Surface.TextCentered(BadgePill, 8.2f, C.TextMuted, "FRACTURE STUDIO");
    X = BadgePill.Right() + 18.0f;

    // 2. Slanted Trapezoid Construct Dock Tabs + "+" spawn button
    const auto& Presets = FractureWorkspaceSession::InspectPresets();
    static constexpr std::array<const char*, 6> ShortDockNames {{
        "Concrete Column",
        "Baked Fractography",
        "Tempered Curtain",
        "Spruce Timber",
        "Polymer Cone",
        "Dendritic Boulder",
    }};

    const float MaxDockRight = HeaderRect.Right() - 470.0f;
    for (uint32_t I = 0u; I < FractureWorkspaceSession::PresetCount; ++I)
    {
        const bool Active = (Session.ActivePresetIndex == I);
        const float TabW = std::max(84.0f, Surface.MeasureTextWidth(9.2f, ShortDockNames[I]) + 32.0f);
        if (X + TabW > MaxDockRight)
        {
            break;
        }
        const Rectangle2D TabRect { X, HeaderRect.Bottom() - 34.0f, TabW, 33.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(TabRect, Pointer);

        const std::array<Point2D, 4> Trap {{
            { TabRect.X + 9.0f,       TabRect.Y },
            { TabRect.Right() - 9.0f, TabRect.Y },
            { TabRect.Right(),        TabRect.Bottom() },
            { TabRect.X,              TabRect.Bottom() },
        }};
        Surface.FillPolygon(Trap, Active ? ColorRgba { 26u, 26u, 29u, 255u } : (Hov ? ColorRgba { 20u, 20u, 22u, 255u } : ColorRgba { 16u, 16u, 18u, 255u }));
        if (Active)
        {
            Surface.StrokeLine({ TabRect.X + 10.0f, TabRect.Bottom() }, { TabRect.Right() - 10.0f, TabRect.Bottom() }, C.WarmAccent, 1.6f);
        }
        Surface.PushClip({ TabRect.X + 10.0f, TabRect.Y, TabRect.Width - 20.0f, TabRect.Height });
        Surface.TextCentered(TabRect, 9.2f, Active ? C.TextPrimary : C.TextMuted, ShortDockNames[I]);
        Surface.PopClip();

        if (Hov && Pointer.Pressed)
        {
            Session.ApplyPreset(I);
        }
        X += TabW - 4.0f;
    }
    (void)Presets;

    const Rectangle2D SpawnBtn { X + 8.0f, CY - 11.0f, 22.0f, 22.0f };
    const bool SpawnHov = ControlAppearance::QueryPointerOver(SpawnBtn, Pointer);
    Surface.FillCircle(SpawnBtn.Center(), 11.0f, SpawnHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(SpawnBtn.Center(), 11.0f, C.Hairline, 0.9f);
    RasterizePlusGlyph(Surface, SpawnBtn.Center(), 3.8f, SpawnHov ? C.TextPrimary : C.TextMuted);
    if (SpawnHov && Pointer.Pressed)
    {
        Session.ApplyPreset((Session.ActivePresetIndex + 1u) % FractureWorkspaceSession::PresetCount);
    }

    // 3. Right-End Live Engine Tag + Export TOML
    float RightEdge = HeaderRect.Right() - 18.0f;
    const Rectangle2D ExportBtn { RightEdge - 92.0f, CY - 14.0f, 92.0f, 28.0f };
    const ButtonDescriptor ExportDesc {
        .Label    = "Export TOML",
        .Tone     = ButtonTone::Secondary,
        .FontSize = 9.4f,
        .PaddingX = 12.0f,
    };
    if (CapsuleButton::Construct(Surface, ExportBtn, ExportDesc, Pointer).Pressed)
    {
        Session.AppendLog("EXPORT", "Encoded .fracture.toml",
            std::to_string(Session.PartitionResult.Fragments.size()) + " shards", false);
    }
    RightEdge = ExportBtn.X - 10.0f;

    const char* EngineLabel = Session.VulkanSpirvReady ? "VULKAN · LIVE" : "OPENPBR · LIVE";
    const float TagW = Surface.MeasureTextWidth(8.4f, EngineLabel) + 26.0f;
    const Rectangle2D EngineTag { RightEdge - TagW, CY - 13.0f, TagW, 26.0f };
    Surface.FillRectangle(EngineTag, Session.VulkanSpirvReady ? ColorRgba { 20u, 27u, 23u, 255u } : ColorRgba { 28u, 25u, 18u, 255u }, 13.0f);
    Surface.StrokeRectangle(EngineTag, Session.VulkanSpirvReady ? C.LiveGreen.ScaledAlpha(0.35f) : C.WarmAccent.ScaledAlpha(0.35f), 13.0f, 0.9f);
    Surface.FillCircle({ EngineTag.X + 11.0f, EngineTag.Center().Y }, 2.8f, Session.VulkanSpirvReady ? C.LiveGreen : C.WarmAccent);
    Surface.Text({ EngineTag.X + 19.0f, EngineTag.Y + 8.0f }, 8.2f, Session.VulkanSpirvReady ? ColorRgba { 155u, 227u, 181u, 255u } : ColorRgba { 239u, 208u, 151u, 255u }, EngineLabel);
    RightEdge = EngineTag.X - 12.0f;

    // 4. Pill Workspace Navigation (Workspace | Solvers 6 | Log Stream N)
    const float NavW = 248.0f;
    const Rectangle2D NavPill { std::max(SpawnBtn.Right() + 12.0f, RightEdge - NavW), CY - 15.0f, NavW, 30.0f };
    Surface.FillRectangle(NavPill, ColorRgba { 17u, 17u, 19u, 255u }, 15.0f);
    Surface.StrokeRectangle(NavPill, C.Hairline, 15.0f, 0.9f);

    char LogLbl[32];
    std::snprintf(LogLbl, sizeof(LogLbl), "Logs %zu", Session.LogEntries.size());
    const std::array<std::pair<FractureNavView, const char*>, 3> NavItems {{
        { FractureNavView::Workspace, "Workspace" },
        { FractureNavView::Solvers,   "Solvers 6" },
        { FractureNavView::LogStream, LogLbl },
    }};

    const float SegW = (NavPill.Width - 6.0f) / 3.0f;
    for (size_t I = 0u; I < 3u; ++I)
    {
        const Rectangle2D SegRect { NavPill.X + 3.0f + static_cast<float>(I) * SegW, NavPill.Y + 3.0f, SegW, NavPill.Height - 6.0f };
        const bool Active = (Session.NavView == NavItems[I].first);
        const bool Hov    = ControlAppearance::QueryPointerOver(SegRect, Pointer);
        if (Active || Hov)
        {
            Surface.FillRectangle(SegRect, Active ? ColorRgba { 36u, 36u, 38u, 255u } : ColorRgba { 26u, 26u, 28u, 255u }, 12.0f);
            if (Active) Surface.StrokeRectangle(SegRect, C.HairlineStrong, 12.0f, 0.8f);
        }
        Surface.TextCentered(SegRect, 9.0f, Active ? C.TextPrimary : C.TextMuted, NavItems[I].second);
        if (Hov && Pointer.Pressed)
        {
            Session.NavView = NavItems[I].first;
            if (Session.NavView == FractureNavView::Solvers)
            {
                Session.ShowSolverMatrix = !Session.ShowSolverMatrix;
                if (!Session.ShowSolverMatrix) Session.NavView = FractureNavView::Workspace;
            }
            else if (Session.NavView == FractureNavView::LogStream)
            {
                Session.ShowLogDrawer = !Session.ShowLogDrawer;
                Session.ShowSolverMatrix = false;
                if (!Session.ShowLogDrawer) Session.NavView = FractureNavView::Workspace;
            }
            else
            {
                Session.ShowSolverMatrix = false;
                Session.ShowLogDrawer    = false;
            }
        }
    }
}

void FractureWorkspaceHost::RenderSolverMatrixModal(
    PixelSpace&           Surface,
    Rectangle2D           Area,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(Area, ColorRgba { 8u, 8u, 10u, 178u }, 0.0f);

    const float ModalW = std::min(760.0f, Area.Width - 48.0f);
    const float ModalH = std::min(410.0f, Area.Height - 48.0f);
    const Rectangle2D Sheet { Area.Center().X - ModalW * 0.5f, Area.Center().Y - ModalH * 0.5f, ModalW, ModalH };
    Surface.FillRectangle(Sheet, ColorRgba { 20u, 20u, 22u, 252u }, 22.0f);
    Surface.StrokeRectangle(Sheet, C.HairlineStrong, 22.0f, 1.2f);

    Surface.Text({ Sheet.X + 22.0f, Sheet.Y + 18.0f }, 16.0f, C.TextPrimary, "Fracture Solver Comparison Matrix");
    Surface.Text({ Sheet.X + 22.0f, Sheet.Y + 40.0f }, 9.4f, C.TextMuted,
        "Click any solver card to switch the active solid partitioning engine");

    const Rectangle2D CloseBtn { Sheet.Right() - 40.0f, Sheet.Y + 16.0f, 26.0f, 26.0f };
    const bool CloseHov = ControlAppearance::QueryPointerOver(CloseBtn, Pointer);
    Surface.FillCircle(CloseBtn.Center(), 13.0f, CloseHov ? C.CardElevated : C.CardBg);
    Surface.TextCentered(CloseBtn, 10.0f, C.TextSoft, "X");
    if (CloseHov && Pointer.Pressed)
    {
        Session.ShowSolverMatrix = false;
        Session.NavView = FractureNavView::Workspace;
    }

    struct MatrixEntry { FractureSolverMethod Method; const char* Name; const char* Budget; const char* Desc; };
    static constexpr std::array<MatrixEntry, 6> Entries {{
        { FractureSolverMethod::FastRuntimeBoussinesq, "Boussinesq Fast",     "< 1 ms · Realtime",   "Closed-form Hertzian cone + CutToken strain bonds" },
        { FractureSolverMethod::EngineeringBakedSolid, "Navier-Cauchy Baked", "4-12 ms · 3D FEM",    "Elastostatic principal stress + Bouchaud relief" },
        { FractureSolverMethod::GriffithStressBudget,  "Griffith Energy",     "1-3 ms · Energetic",  "Strain energy budget release G_c surface quota" },
        { FractureSolverMethod::LefmWavefront,         "LEFM Wavefront",      "1-4 ms · Dynamic",    "Radial spoke + Wallner ripple rings for glass" },
        { FractureSolverMethod::ClusteredVoronoi,      "Clustered Voronoi",   "1-2 ms · Cellular",   "Multi-tier Voronoi polyhedral fragmentation" },
        { FractureSolverMethod::DendriticSpallHull,    "Dendritic Spall",     "2-5 ms · Spall",      "Hopkinson rear spall shell + branching trunk" },
    }};

    const float Pad = 22.0f;
    const float Gap = 12.0f;
    const float CardW = (Sheet.Width - Pad * 2.0f - Gap * 2.0f) / 3.0f;
    const float CardH = 140.0f;
    const float StartY = Sheet.Y + 72.0f;

    for (size_t I = 0u; I < Entries.size(); ++I)
    {
        const size_t Col = I % 3u;
        const size_t Row = I / 3u;
        const Rectangle2D Card {
            Sheet.X + Pad + static_cast<float>(Col) * (CardW + Gap),
            StartY + static_cast<float>(Row) * (CardH + Gap),
            CardW,
            CardH
        };
        const bool Active = (Session.SolverMethod == Entries[I].Method);
        const bool Hov    = ControlAppearance::QueryPointerOver(Card, Pointer);
        Surface.FillRectangle(Card, Active ? ColorRgba { 32u, 30u, 24u, 255u } : (Hov ? C.CardElevated : C.CardBg), 16.0f);
        Surface.StrokeRectangle(Card, Active ? C.WarmAccent : C.Hairline, 16.0f, Active ? 1.4f : 1.0f);

        Surface.Text({ Card.X + 14.0f, Card.Y + 14.0f }, 12.0f, Active ? C.WarmAccent : C.TextPrimary, Entries[I].Name);
        Surface.Text({ Card.X + 14.0f, Card.Y + 34.0f }, 8.6f, C.LiveGreen, Entries[I].Budget);
        Surface.Text({ Card.X + 14.0f, Card.Y + 58.0f }, 8.8f, C.TextSoft, Entries[I].Desc);
        if (Active)
        {
            Surface.Text({ Card.X + 14.0f, Card.Bottom() - 22.0f }, 8.4f, C.WarmAccent, "ACTIVE SOLVER");
        }
        if (Hov && Pointer.Pressed)
        {
            Session.SolverMethod     = Entries[I].Method;
            Session.ShowSolverMatrix = false;
            Session.NavView          = FractureNavView::Workspace;
            Session.RebuildFracturePartition();
        }
    }
}

void FractureWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(StatusRect, ColorRgba { 11u, 11u, 12u, 255u }, 0.0f);
    Surface.StrokeLine({ StatusRect.X, StatusRect.Y }, { StatusRect.Right(), StatusRect.Y }, C.Hairline, 1.0f);

    const float CY = StatusRect.Center().Y;
    Surface.FillCircle({ StatusRect.X + 16.0f, CY }, 3.0f, C.LiveGreen);

    const auto& Rec = Session.PartitionResult.Receipt;
    const auto& SimTelemetry = Session.SimRunner.InspectTelemetry();
    const uint32_t IntactBonds = Session.HasSimulatedData
        ? SimTelemetry.IntactBondCount
        : static_cast<uint32_t>(Session.PartitionResult.Bonds.size());

    char LeftBuf[160];
    std::snprintf(LeftBuf, sizeof(LeftBuf),
        "Manifold ready  |  %zu shards · %u/%zu bonds  |  SigmaV drift %.2e  |  %.2f ms  |  JoltPhysics v5.6.0",
        Session.PartitionResult.Fragments.size(),
        IntactBonds,
        Session.PartitionResult.Bonds.size(),
        Rec.VolumeDriftRatio,
        Rec.ComputeDurationMs);
    Surface.Text({ StatusRect.X + 26.0f, CY - 5.0f }, 8.8f, C.TextMuted, LeftBuf);

    const char* RightText = Session.VulkanSpirvReady
        ? "Vulkan 1.3 SPIR-V  |  FRONTIER / FRACTURE 2.0"
        : "CPU OpenPBR Direct Studio  |  FRONTIER / FRACTURE 2.0";
    Surface.TextRightAligned({ StatusRect.X, CY - 5.0f, StatusRect.Width - 16.0f, 12.0f }, 8.8f, C.TextMuted, RightText);
}

void FractureWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    Session.AdvanceFrame(DeltaSeconds);

    ControlPointer Pointer {};
    Pointer.X        = PointerInput.CursorX;
    Pointer.Y        = PointerInput.CursorY;
    Pointer.Down     = PointerInput.HeldDown;
    Pointer.Pressed  = PointerInput.Pressed;
    Pointer.Released = PointerInput.Released;
    Pointer.Enabled  = true;

    constexpr float HeaderH = 56.0f;
    constexpr float StatusH = 28.0f;
    constexpr float Pad     = 10.0f;
    constexpr float Gap     = 10.0f;

    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, HeaderH };
    RenderStudioHeader(Surface, HeaderRect, Pointer);

    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    RenderStatusBar(Surface, StatusRect);

    const Rectangle2D WorkArea {
        WindowBounds.X + Pad,
        HeaderRect.Bottom() + 2.0f,
        WindowBounds.Width - Pad * 2.0f,
        std::max(260.0f, StatusRect.Y - HeaderRect.Bottom() - Pad - 2.0f)
    };

    if (Session.ViewportMaximized)
    {
        Viewport.Render(Surface, WorkArea, Session, Pointer, PointerInput.WheelDeltaY);
    }
    else
    {
        const float LeftW  = std::clamp(WorkArea.Width * 0.21f, 260.0f, 312.0f);
        const float RightW = std::clamp(WorkArea.Width * 0.25f, 310.0f, 376.0f);
        const float CenterW = std::max(320.0f, WorkArea.Width - LeftW - RightW - Gap * 2.0f);

        const Rectangle2D OutlinerRect  { WorkArea.X, WorkArea.Y, LeftW, WorkArea.Height };
        const Rectangle2D ViewportRect  { OutlinerRect.Right() + Gap, WorkArea.Y, CenterW, WorkArea.Height };
        const Rectangle2D InspectorRect { ViewportRect.Right() + Gap, WorkArea.Y, RightW, WorkArea.Height };

        Outliner.Render(Surface, OutlinerRect, Session, Pointer, PointerInput.WheelDeltaY);
        Viewport.Render(Surface, ViewportRect, Session, Pointer, PointerInput.WheelDeltaY);
        Inspector.Render(Surface, InspectorRect, Session, Pointer, PointerInput.WheelDeltaY);
    }

    if (Session.ShowSolverMatrix)
    {
        RenderSolverMatrixModal(Surface, WorkArea, Pointer);
    }
}

} // namespace Frontier::Workspaces::Fracture
