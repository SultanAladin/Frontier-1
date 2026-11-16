/*============================================================================================================================================
                                                         FRACTUREVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 FractureViewportHost implementation: 1:1 HTML viewport bar, HUD pills, tool rail, projected reticle, ExecutionModeStrip & TimelineFooter

#include "Engine/Workspaces/Fracture/FractureViewportHost.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Controls/SimulationTimelineFooter.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;
using GeometrySpace::Fracture::FractureExecutionMode;
using GeometrySpace::Fracture::FractureSolverMethod;

[[nodiscard]] const char* QuerySolverName(FractureSolverMethod Method) noexcept
{
    switch (Method)
    {
    case FractureSolverMethod::FastRuntimeBoussinesq: return "Boussinesq Fast Runtime";
    case FractureSolverMethod::EngineeringBakedSolid: return "Navier-Cauchy Baked 3D";
    case FractureSolverMethod::GriffithStressBudget:  return "Griffith Energy Budget";
    case FractureSolverMethod::LefmWavefront:         return "LEFM Wavefront";
    case FractureSolverMethod::ClusteredVoronoi:      return "Clustered Voronoi";
    case FractureSolverMethod::DendriticSpallHull:    return "Dendritic Spall";
    }
    return "Fracture Solver";
}

std::pair<std::string, std::string> SplitDecimalParts(float Value, const char* Fmt = "%.2f")
{
    char Buf[32] {};
    std::snprintf(Buf, sizeof(Buf), Fmt, Value);
    std::string Str(Buf);
    const size_t Dot = Str.find('.');
    if (Dot == std::string::npos)
    {
        return { Str, "" };
    }
    return { Str.substr(0u, Dot), Str.substr(Dot) };
}

void PopulateDiagnosticsChannels(
    const FractureWorkspaceSession&        Session,
    std::vector<DiagnosticsMetricChannel>& OutChannels)
{
    OutChannels.clear();
    const auto& Rec = Session.PartitionResult.Receipt;
    const auto SimTelemetry = Session.SimRunner.InspectTelemetry();
    const auto SdfFootprint = Session.QuerySdfFootprint();

    // 0: Frame
    {
        auto [W, F] = SplitDecimalParts(Session.LastFrameTimeMs);
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Frame";
        Ch.Title           = "Frame Time";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = Session.VulkanSpirvReady ? "Vulkan SPIR-V + OpenPBR Studio" : "CPU OpenPBR Direct Studio";
        Ch.Samples         = Session.FrameTimeSamplesMs;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 33.3f;
        Ch.HasBudgetLine   = true;
        Ch.BudgetThreshold = 16.67f;
        Ch.AccentColor     = { 170u, 224u, 152u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {};
        std::snprintf(R1, sizeof(R1), "%.2f ms", Session.LastRenderTimeMs);
        std::snprintf(R2, sizeof(R2), "%u tris", Session.LastDrawnTriangles);
        std::snprintf(R3, sizeof(R3), "%s", Session.VulkanSpirvReady ? "Vulkan 1.3" : "CPU Fallback");
        Ch.Rows = {
            { "Viewport raster", R1 },
            { "Drawn triangles", R2 },
            { "Backend", R3 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 1: Fracture
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(Rec.ComputeDurationMs));
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Fracture";
        Ch.Title           = "Partition Solve";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = QuerySolverName(Session.SolverMethod);
        Ch.Samples         = { static_cast<float>(Rec.ComputeDurationMs), static_cast<float>(Rec.ComputeDurationMs) };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 20.0f;
        Ch.HasBudgetLine   = true;
        Ch.BudgetThreshold = 4.0f;
        Ch.AccentColor     = { 232u, 194u, 122u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {};
        std::snprintf(R1, sizeof(R1), "%zu shards", Session.PartitionResult.Fragments.size());
        std::snprintf(R2, sizeof(R2), "%.3e", Rec.VolumeDriftRatio);
        std::snprintf(R3, sizeof(R3), "%.2f m2", Rec.TotalCrackAreaM2);
        Ch.Rows = {
            { "Fragments", R1 },
            { "Volume drift", R2 },
            { "Crack surface", R3 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 2: Sim
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(SimTelemetry.TotalKineticJ), "%.1f");
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Sim";
        Ch.Title           = "Kinetic Energy";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "J";
        Ch.SourceCaption   = "JoltPhysics v5.6.0 · BreakableBonds";
        Ch.Samples         = Session.KineticCurveSamples.empty() ? std::vector<float> { 0.0f } : Session.KineticCurveSamples;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = std::max(10.0f, static_cast<float>(Session.ImpactEnergyJ) * 0.5f);
        Ch.AccentColor     = { 96u, 188u, 255u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {};
        std::snprintf(R1, sizeof(R1), "%u / %u", SimTelemetry.IntactBondCount, SimTelemetry.TotalBondCount);
        std::snprintf(R2, sizeof(R2), "%u / %u", SimTelemetry.AwakeFragmentCount, SimTelemetry.FragmentCount);
        std::snprintf(R3, sizeof(R3), "%.2f m/s", SimTelemetry.MaxSpeedMps);
        Ch.Rows = {
            { "Intact bonds", R1 },
            { "Moving shards", R2 },
            { "Peak speed", R3 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 3: SDF
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(SdfFootprint.Megabytes));
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "SDF";
        Ch.Title           = "Sparse SDF Bricks";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "MB";
        Ch.SourceCaption   = Session.SdfEnabled ? "Sparse brick atlas active" : "Sparse SDF disabled";
        Ch.Samples         = { static_cast<float>(SdfFootprint.Megabytes), static_cast<float>(SdfFootprint.Megabytes) };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 64.0f;
        Ch.AccentColor     = { 104u, 212u, 178u, 255u };

        char R1[48] {}, R2[48] {};
        std::snprintf(R1, sizeof(R1), "%u^3", Session.SdfResolution);
        std::snprintf(R2, sizeof(R2), "%u bricks", SdfFootprint.OccupiedBricks);
        Ch.Rows = {
            { "Resolution", R1 },
            { "Occupied bricks", R2 }
        };
        OutChannels.push_back(std::move(Ch));
    }
}

void DrawToolIconGlyph(PixelSpace& Surface, Point2D C, FractureViewportTool Tool, ColorRgba Ink) noexcept
{
    switch (Tool)
    {
    case FractureViewportTool::Strike:
        Surface.StrokeLine({ C.X + 2.0f, C.Y - 5.5f }, { C.X - 2.5f, C.Y + 0.5f }, Ink, 1.35f);
        Surface.StrokeLine({ C.X - 2.5f, C.Y + 0.5f }, { C.X + 1.5f, C.Y + 0.5f }, Ink, 1.35f);
        Surface.StrokeLine({ C.X + 1.5f, C.Y + 0.5f }, { C.X - 2.0f, C.Y + 5.5f }, Ink, 1.35f);
        break;
    case FractureViewportTool::Select:
        Surface.StrokeCircle({ C.X - 2.0f, C.Y }, 4.2f, Ink, 1.2f);
        Surface.StrokeCircle({ C.X + 2.0f, C.Y }, 4.2f, Ink, 1.2f);
        break;
    case FractureViewportTool::Locus:
        Surface.StrokeCircle(C, 4.5f, Ink, 1.2f);
        Surface.StrokeLine({ C.X - 6.5f, C.Y }, { C.X + 6.5f, C.Y }, Ink, 1.1f);
        Surface.StrokeLine({ C.X, C.Y - 6.5f }, { C.X, C.Y + 6.5f }, Ink, 1.1f);
        break;
    case FractureViewportTool::Reset:
        Surface.StrokeCircle(C, 4.6f, Ink, 1.2f);
        Surface.FillCircle({ C.X + 4.6f, C.Y }, 1.6f, Ink);
        break;
    }
}

} // namespace

FractureViewportHost::~FractureViewportHost() noexcept
{
    ViewportTexture.DestroyPixels();
}

void FractureViewportHost::AttachRenderer(FractureViewportRenderer* TargetRenderer) noexcept
{
    Renderer = TargetRenderer;
    InvalidateRenderCache();
}

void FractureViewportHost::InvalidateRenderCache() noexcept
{
    CachedSignature = 0ull;
    CachedWidthPx   = 0u;
    CachedHeightPx  = 0u;
}

void FractureViewportHost::UploadRgbaTexture(
    const std::vector<uint8_t>& RgbaPixels,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() < static_cast<size_t>(WidthPx) * HeightPx * 4u)
    {
        return;
    }

    const bool NeedsCreate =
        ViewportTexture.Width != static_cast<int>(WidthPx) ||
        ViewportTexture.Height != static_cast<int>(HeightPx) ||
        ViewportTexture.Pixels == nullptr;

    if (NeedsCreate)
    {
        ViewportTexture.DestroyPixels();
        ViewportTexture.SetStatus(ImTextureStatus_Destroyed);
        ViewportTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }

    if (ViewportTexture.Pixels != nullptr)
    {
        std::memcpy(ViewportTexture.GetPixels(), RgbaPixels.data(), static_cast<size_t>(WidthPx) * HeightPx * 4u);
        ViewportTexture.SetStatus(NeedsCreate ? ImTextureStatus_WantCreate : ImTextureStatus_WantUpdates);
    }
}

void FractureViewportHost::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY) noexcept
{
    if (Bounds.Width < 260.0f || Bounds.Height < 220.0f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    // 1. Top 42px Viewport Bar (Scene view | Camera dropdown | Pipeline dropdown | Lens dropdown | Matrix M | Log | Focus | Diag)
    constexpr float TopBarH = 42.0f;
    const Rectangle2D TopBar { Bounds.X + 2.0f, Bounds.Y + 2.0f, Bounds.Width - 4.0f, TopBarH };
    Surface.FillRectangle(TopBar, ColorRgba { 23u, 23u, 25u, 255u }, 18.0f);
    Surface.StrokeLine({ Bounds.X + 10.0f, TopBar.Bottom() }, { Bounds.Right() - 10.0f, TopBar.Bottom() }, C.Hairline, 1.0f);

    float BarX = TopBar.X + 12.0f;
    const float BarCY = TopBar.Center().Y;

    Surface.StrokeRectangle({ BarX, BarCY - 5.5f, 11.0f, 11.0f }, C.WarmAccent, 2.5f, 1.2f);
    BarX += 16.0f;
    Surface.Text({ BarX, BarCY - 5.5f }, 10.4f, C.TextPrimary, "Scene view");
    BarX += Surface.MeasureTextWidth(10.4f, "Scene view") + 7.0f;
    Surface.StrokeLine({ BarX, BarCY - 7.0f }, { BarX, BarCY + 7.0f }, C.HairlineStrong, 1.0f);
    BarX += 7.0f;

    static constexpr std::array<const char*, 4> CameraLabels {{
        "Perspective", "Top view", "Front face", "Side view"
    }};
    static constexpr std::array<const char*, 3> PipelineLabels {{
        "Pipe · Realtime", "Pipe · Eng Bake", "Pipe · Hybrid"
    }};
    static constexpr std::array<const char*, 4> LensLabels {{
        "Lens · OpenPBR", "Lens · Energy", "Lens · Cells", "Lens · Normals"
    }};

    const uint32_t PipeIdx = static_cast<uint32_t>(Session.ExecutionMode);
    const uint32_t LensIdx = static_cast<uint32_t>(Session.RenderOptions.RenderMode);

    const Rectangle2D CamDropRect { BarX, BarCY - 13.0f, 100.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface, CamDropRect, CameraLabels[Session.CameraViewIndex % 4u],
            Session.CameraDropdownOpen, Pointer, 1.0f).Pressed)
    {
        Session.CameraDropdownOpen   = !Session.CameraDropdownOpen;
        Session.PipelineDropdownOpen = false;
        Session.LensDropdownOpen     = false;
    }
    BarX = CamDropRect.Right() + 4.0f;

    const Rectangle2D PipeDropRect { BarX, BarCY - 13.0f, 118.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface, PipeDropRect, PipelineLabels[PipeIdx % 3u],
            Session.PipelineDropdownOpen, Pointer, 1.0f).Pressed)
    {
        Session.PipelineDropdownOpen = !Session.PipelineDropdownOpen;
        Session.CameraDropdownOpen   = false;
        Session.LensDropdownOpen     = false;
    }
    BarX = PipeDropRect.Right() + 4.0f;

    const Rectangle2D LensDropRect { BarX, BarCY - 13.0f, 108.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface, LensDropRect, LensLabels[LensIdx % 4u],
            Session.LensDropdownOpen, Pointer, 1.0f).Pressed)
    {
        Session.LensDropdownOpen     = !Session.LensDropdownOpen;
        Session.CameraDropdownOpen   = false;
        Session.PipelineDropdownOpen = false;
    }

    // Right end of viewport bar
    float RightX = TopBar.Right() - 10.0f;
    auto DrawBarIconPill = [&](const char* Label, float Width, bool Active, bool Accent) -> bool
    {
        RightX -= Width;
        if (RightX < LensDropRect.Right() + 4.0f) return false;
        const Rectangle2D R { RightX, BarCY - 13.0f, Width, 26.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(R, Pointer);
        const ColorRgba Fill = Active
            ? (Accent ? C.WarmAccent.ScaledAlpha(0.18f) : ColorRgba { 40u, 42u, 46u, 255u })
            : (Hov ? C.CardElevated : C.CardBg);
        const ColorRgba Border = Active
            ? (Accent ? C.WarmAccent.ScaledAlpha(0.65f) : C.HairlineStrong)
            : C.Hairline;
        Surface.FillRectangle(R, Fill, 13.0f);
        Surface.StrokeRectangle(R, Border, 13.0f, 0.9f);
        Surface.TextCentered(R, 8.6f, Active ? (Accent ? C.WarmAccent : C.TextPrimary) : C.TextSoft, Label);
        RightX -= 4.0f;
        return Hov && Pointer.Pressed;
    };

    if (DrawBarIconPill("Max", 32.0f, Session.ViewportMaximized, false))
    {
        Session.ViewportMaximized = !Session.ViewportMaximized;
    }
    if (DrawBarIconPill("Diag F3", 48.0f, Session.DiagnosticsSession.Visible, true))
    {
        Session.DiagnosticsSession.Visible = !Session.DiagnosticsSession.Visible;
    }
    if (DrawBarIconPill("Focus", 40.0f, false, false))
    {
        Session.ApplyCameraView(0u);
    }
    if (DrawBarIconPill("Log", 30.0f, Session.ShowLogDrawer, false))
    {
        Session.ShowLogDrawer = !Session.ShowLogDrawer;
        Session.NavView = Session.ShowLogDrawer ? FractureNavView::LogStream : FractureNavView::Workspace;
    }
    if (DrawBarIconPill("Matrix", 44.0f, Session.ShowSolverMatrix, true))
    {
        Session.ShowSolverMatrix = !Session.ShowSolverMatrix;
        Session.NavView = Session.ShowSolverMatrix ? FractureNavView::Solvers : FractureNavView::Workspace;
    }

    // 2. Canvas & Optional Log Drawer & Bottom SimulationTimelineFooter
    constexpr float TimelineH = 56.0f;
    const float LogH = Session.ShowLogDrawer ? 124.0f : 0.0f;
    const Rectangle2D FooterRect { Bounds.X + 8.0f, Bounds.Bottom() - TimelineH - 6.0f, Bounds.Width - 16.0f, TimelineH };
    const Rectangle2D LogRect    { Bounds.X + 8.0f, FooterRect.Y - LogH - (Session.ShowLogDrawer ? 4.0f : 0.0f), Bounds.Width - 16.0f, LogH };
    const Rectangle2D CanvasRect {
        Bounds.X + 8.0f,
        TopBar.Bottom() + 4.0f,
        Bounds.Width - 16.0f,
        std::max(140.0f, (Session.ShowLogDrawer ? LogRect.Y : FooterRect.Y) - TopBar.Bottom() - 8.0f)
    };

    // 3. Render 3D Viewport Frame via ImTextureData
    const uint32_t RenderW = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Width * 0.85f), 240l, 1280l));
    const uint32_t RenderH = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Height * 0.85f), 160l, 900l));
    const uint64_t Sig     = Session.ComputeViewportRenderSignature();

    FractureViewportRenderer& ActiveRenderer = (Renderer != nullptr) ? *Renderer : FallbackRenderer;
    if (Sig != CachedSignature || RenderW != CachedWidthPx || RenderH != CachedHeightPx || ViewportTexture.Pixels == nullptr)
    {
        const auto* SimPtr = (Session.ExecutionPhase != ExecutionStripPhase::Edit && Session.HasSimulatedData)
            ? &Session.SimRunner
            : nullptr;
        const auto Output = ActiveRenderer.RenderFrame(
            Session.HasFractured ? &Session.PartitionResult : nullptr,
            &Session.SourceSolid,
            Session.ActiveMaterialSpec(),
            SimPtr,
            Session.Camera,
            Session.RenderOptions,
            Session.ImpactPointLocal,
            Session.ImpactNormalLocal,
            RenderW,
            RenderH);
        Session.LastRenderTimeMs   = Output.RenderMillisec;
        Session.LastDrawnTriangles = Output.DrawnTriangles;
        Session.VulkanSpirvReady   = Output.VulkanSpirvReady;
        UploadRgbaTexture(Output.DisplayRgba8, Output.WidthPx, Output.HeightPx);
        CachedSignature = Sig;
        CachedWidthPx   = RenderW;
        CachedHeightPx  = RenderH;
    }

    Surface.PushClip(CanvasRect);
    if (ViewportTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(CanvasRect, &ViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 14.0f);
    }
    Surface.StrokeRectangle(CanvasRect, C.Hairline, 14.0f, 1.0f);

    // 4. Top-Left Caption Pill (LIVE FRACTURE + Specimen + Subtitle)
    {
        float CapX = CanvasRect.X + 14.0f;
        const float CapY = CanvasRect.Y + 12.0f;

        const bool IsSim = (Session.ExecutionPhase != ExecutionStripPhase::Edit);
        const char* LiveStr = IsSim ? (Session.Playing ? "SIMULATING" : "SIMULATED") : "LIVE FRACTURE";
        const float LiveW = Surface.MeasureTextWidth(8.4f, LiveStr) + 28.0f;
        const Rectangle2D LivePill { CapX, CapY, LiveW, 24.0f };
        Surface.FillRectangle(LivePill, ColorRgba { 16u, 16u, 18u, 212u }, 12.0f);
        Surface.StrokeRectangle(LivePill, ColorRgba { 255u, 255u, 255u, 28u }, 12.0f, 0.8f);
        Surface.FillCircle({ LivePill.X + 11.0f, LivePill.Center().Y }, 3.0f, IsSim ? C.WarmAccent : C.LiveGreen);
        Surface.Text({ LivePill.X + 19.0f, LivePill.Y + 6.5f }, 8.4f, C.TextPrimary, LiveStr);
        CapX = LivePill.Right() + 8.0f;

        const auto& Preset = FractureWorkspaceSession::InspectPresets()[Session.ActivePresetIndex];
        const float ObjW = Surface.MeasureTextWidth(9.8f, Preset.Title) + 20.0f;
        const Rectangle2D ObjPill { CapX, CapY, ObjW, 24.0f };
        Surface.FillRectangle(ObjPill, ColorRgba { 18u, 18u, 20u, 200u }, 12.0f);
        Surface.StrokeRectangle(ObjPill, C.Hairline, 12.0f, 0.8f);
        Surface.TextCentered(ObjPill, 9.6f, C.TextPrimary, Preset.Title);
    }

    // 5. Top-Right ExecutionModeStrip (Edit | Simulate | Play/Pause + Rewind)
    const float StripW = ExecutionModeStrip::PreferredStripWidth(true);
    const Rectangle2D StripRect {
        CanvasRect.Right() - StripW - 12.0f,
        CanvasRect.Y + 10.0f,
        StripW,
        ExecutionModeStrip::RecommendedHeightPx
    };
    ExecutionModeStripReading StripRead {};
    StripRead.ActivePhase          = Session.ExecutionPhase;
    StripRead.PlayPaused           = !Session.Playing;
    StripRead.ShowRewindButton     = true;
    StripRead.ShowPauseWhenPlaying = true;
    const auto StripRes = ExecutionModeStrip::Construct(Surface, StripRect, StripRead, Pointer);
    if (StripRes.RewindPressed)
    {
        Session.RewindToStart();
    }
    else if (StripRes.PhaseChanged)
    {
        Session.TransitionExecutionPhase(StripRes.SelectedPhase);
    }

    // 6. Second Row Overlay Bar: Strike button + Explode ScalarSlider + Wire/Bonds/Wave/Grid toggle pills
    {
        const float Row2Y = CanvasRect.Y + 46.0f;
        const Rectangle2D StrikeBtn { CanvasRect.X + 14.0f, Row2Y, 92.0f, 26.0f };
        const bool StrikeHov = ControlAppearance::QueryPointerOver(StrikeBtn, Pointer);
        Surface.FillRectangle(StrikeBtn, StrikeHov ? ColorRgba { 248u, 248u, 244u, 255u } : ColorRgba { 235u, 235u, 232u, 255u }, 13.0f);
        Surface.TextCentered(StrikeBtn, 9.2f, ColorRgba { 16u, 16u, 16u, 255u }, "Strike Solid");
        if (StrikeHov && Pointer.Pressed)
        {
            Session.TriggerViewportImpact(
                Session.ImpactPointLocal + Session.RenderOptions.SolidWorldOffsetM,
                Session.ImpactNormalLocal,
                Session.RenderOptions.SelectedFragmentIndex);
        }

        const Rectangle2D ExplodePanel { StrikeBtn.Right() + 8.0f, Row2Y, 178.0f, 26.0f };
        Surface.FillRectangle(ExplodePanel, ColorRgba { 16u, 16u, 18u, 205u }, 13.0f);
        Surface.StrokeRectangle(ExplodePanel, C.Hairline, 13.0f, 0.8f);
        Surface.Text({ ExplodePanel.X + 10.0f, ExplodePanel.Y + 7.5f }, 8.8f, C.TextMuted, "Explode");
        float UpdatedExplode = Session.RenderOptions.ExplodeFactor * 100.0f;
        const Rectangle2D SliderRect { ExplodePanel.X + 58.0f, ExplodePanel.Y + 5.0f, 76.0f, 16.0f };
        const ControlContact SlContact = ScalarSlider::Construct(
            Surface, SliderRect, 0.0f, 100.0f, UpdatedExplode, ExplodeSliderDrag, Pointer, UpdatedExplode, 1.0f, 5.0f, 12.0f);
        if (Pointer.Pressed && SlContact.Hovered) ExplodeSliderDrag = true;
        if (!Pointer.Down) ExplodeSliderDrag = false;
        if (ExplodeSliderDrag || std::abs(UpdatedExplode - Session.RenderOptions.ExplodeFactor * 100.0f) > 0.05f)
        {
            Session.RenderOptions.ExplodeFactor = std::clamp(UpdatedExplode / 100.0f, 0.0f, 1.0f);
        }
        char ExpPct[16] {};
        std::snprintf(ExpPct, sizeof(ExpPct), "%d%%", static_cast<int>(std::lround(Session.RenderOptions.ExplodeFactor * 100.0f)));
        Surface.TextRightAligned({ ExplodePanel.X, ExplodePanel.Y + 7.5f, ExplodePanel.Width - 10.0f, 14.0f }, 8.8f, C.TextPrimary, ExpPct);

        float Tx = CanvasRect.Right() - 12.0f;
        auto DrawOverlayPill = [&](const char* Txt, float W, bool& Flag, ColorRgba OnTint)
        {
            Tx -= W;
            const Rectangle2D R { Tx, Row2Y, W, 24.0f };
            const bool Hov = ControlAppearance::QueryPointerOver(R, Pointer);
            Surface.FillRectangle(R, Flag ? OnTint.ScaledAlpha(0.18f) : (Hov ? C.CardElevated : ColorRgba { 16u, 16u, 18u, 200u }), 12.0f);
            Surface.StrokeRectangle(R, Flag ? OnTint.ScaledAlpha(0.75f) : C.Hairline, 12.0f, 0.8f);
            Surface.TextCentered(R, 8.6f, Flag ? OnTint : C.TextMuted, Txt);
            if (Hov && Pointer.Pressed) Flag = !Flag;
            Tx -= 5.0f;
        };
        DrawOverlayPill("Grid #", 46.0f, Session.RenderOptions.ShowGroundGrid, C.WarmAccent);
        DrawOverlayPill("Wave", 44.0f, Session.RenderOptions.ShowWavefronts, ColorRgba { 112u, 198u, 255u, 255u });
        DrawOverlayPill("Bonds", 48.0f, Session.RenderOptions.ShowBonds, C.LiveGreen);
        DrawOverlayPill("Wire", 42.0f, Session.RenderOptions.ShowWireframe, C.WarmAccent);
    }

    // 7. Left Status HUD Pills
    {
        float HudY = CanvasRect.Y + 84.0f;
        const float HudX = CanvasRect.X + 14.0f;

        const double KeptPct = (1.0 - std::min(1.0, Session.PartitionResult.Receipt.VolumeDriftRatio)) * 100.0;
        char KeptStr[48] {};
        std::snprintf(KeptStr, sizeof(KeptStr), "Volume kept  %.3f %%", KeptPct);
        const float KeptW = Surface.MeasureTextWidth(9.0f, KeptStr) + 34.0f;
        const Rectangle2D KeptPill { HudX, HudY, KeptW, 25.0f };
        Surface.FillRectangle(KeptPill, ColorRgba { 16u, 16u, 18u, 210u }, 12.5f);
        Surface.StrokeRectangle(KeptPill, C.Hairline, 12.5f, 0.8f);
        RasterizeCheckBadgeGlyph(Surface, { KeptPill.X + 14.0f, KeptPill.Center().Y }, 7.0f, C.LiveGreen, { 10u, 22u, 14u, 255u });
        Surface.Text({ KeptPill.X + 26.0f, KeptPill.Y + 7.0f }, 9.0f, C.TextPrimary, KeptStr);
        HudY += 30.0f;

        char SeamStr[64] {};
        std::snprintf(SeamStr, sizeof(SeamStr), "%zu convex cells · %zu seam bonds",
            Session.PartitionResult.Fragments.size(), Session.PartitionResult.Bonds.size());
        const float SeamW = Surface.MeasureTextWidth(8.8f, SeamStr) + 22.0f;
        const Rectangle2D SeamPill { HudX, HudY, SeamW, 24.0f };
        Surface.FillRectangle(SeamPill, ColorRgba { 16u, 16u, 18u, 200u }, 12.0f);
        Surface.StrokeRectangle(SeamPill, C.Hairline, 12.0f, 0.8f);
        Surface.TextCentered(SeamPill, 8.8f, C.TextSoft, SeamStr);
        HudY += 29.0f;

        if (Session.ExecutionPhase != ExecutionStripPhase::Edit)
        {
            char RelStr[48] {};
            std::snprintf(RelStr, sizeof(RelStr), "Released  %u of %zu",
                Session.CountReleasedFragments(), Session.PartitionResult.Fragments.size());
            const float RelW = Surface.MeasureTextWidth(8.8f, RelStr) + 34.0f;
            const Rectangle2D RelPill { HudX, HudY, RelW, 24.0f };
            Surface.FillRectangle(RelPill, ColorRgba { 16u, 16u, 18u, 210u }, 12.0f);
            Surface.StrokeRectangle(RelPill, C.Hairline, 12.0f, 0.8f);
            RasterizeCheckBadgeGlyph(Surface, { RelPill.X + 13.0f, RelPill.Center().Y }, 6.8f, C.LiveGreen, { 10u, 22u, 14u, 255u });
            Surface.Text({ RelPill.X + 25.0f, RelPill.Y + 6.5f }, 8.8f, C.TextPrimary, RelStr);
        }
    }

    // 8. Left Vertical 4-Tool Rail (Strike, Select, Locus, Reset)
    bool ToolRailHit = false;
    {
        const Rectangle2D RailRect { CanvasRect.X + 14.0f, CanvasRect.Y + 186.0f, 38.0f, 146.0f };
        Surface.FillRectangle(RailRect, ColorRgba { 16u, 16u, 18u, 212u }, 19.0f);
        Surface.StrokeRectangle(RailRect, ColorRgba { 255u, 255u, 255u, 22u }, 19.0f, 0.9f);
        static constexpr std::array<FractureViewportTool, 4> Tools {{
            FractureViewportTool::Strike,
            FractureViewportTool::Select,
            FractureViewportTool::Locus,
            FractureViewportTool::Reset,
        }};
        for (size_t I = 0u; I < Tools.size(); ++I)
        {
            const Rectangle2D Btn { RailRect.X + 4.0f, RailRect.Y + 5.0f + static_cast<float>(I) * 34.0f, 30.0f, 30.0f };
            const bool Active = (Session.ViewportTool == Tools[I]);
            const bool Hov    = ControlAppearance::QueryPointerOver(Btn, Pointer);
            if (Active || Hov)
            {
                Surface.FillCircle(Btn.Center(), 14.0f, Active ? ColorRgba { 44u, 44u, 48u, 255u } : ColorRgba { 30u, 30u, 34u, 255u });
                if (Active) Surface.StrokeCircle(Btn.Center(), 14.0f, C.HairlineStrong, 0.9f);
            }
            DrawToolIconGlyph(Surface, Btn.Center(), Tools[I], Active ? C.WarmAccent : C.TextSoft);
            if (Hov && Pointer.Pressed)
            {
                ToolRailHit = true;
                if (Tools[I] == FractureViewportTool::Reset)
                {
                    Session.RewindToStart();
                    Session.RenderOptions.ExplodeFactor = 0.0f;
                    Session.ApplyCameraView(0u);
                }
                else
                {
                    Session.ViewportTool = Tools[I];
                }
            }
        }
    }

    // 9. Screen-Projected Impact Reticle & Callout Pill
    if (Session.ShowReticle && Session.RenderOptions.ShowImpactMarker && CachedWidthPx > 0u && CachedHeightPx > 0u)
    {
        const Mathematics::Axis3D WorldImpact = Session.ImpactPointLocal + Session.RenderOptions.SolidWorldOffsetM;
        float ScreenX = 0.0f, ScreenY = 0.0f, Depth = 0.0f;
        if (FractureViewportRenderer::ProjectWorldToViewport(
                Session.Camera, WorldImpact, CachedWidthPx, CachedHeightPx, ScreenX, ScreenY, Depth))
        {
            const float Cx = CanvasRect.X + (ScreenX / static_cast<float>(CachedWidthPx)) * CanvasRect.Width;
            const float Cy = CanvasRect.Y + (ScreenY / static_cast<float>(CachedHeightPx)) * CanvasRect.Height;
            if (CanvasRect.Contains({ Cx, Cy }))
            {
                Surface.StrokeCircle({ Cx, Cy }, 15.0f, C.WarmAccent.ScaledAlpha(0.75f), 1.2f);
                Surface.StrokeCircle({ Cx, Cy }, 5.0f, ColorRgba { 255u, 118u, 64u, 240u }, 1.4f);
                Surface.FillCircle({ Cx, Cy }, 2.2f, C.WarmAccent);

                const float CardX = std::min(CanvasRect.Right() - 186.0f, Cx + 26.0f);
                const float CardY = std::max(CanvasRect.Y + 90.0f, Cy - 48.0f);
                const Rectangle2D Callout { CardX, CardY, 174.0f, 40.0f };
                Surface.StrokeLine({ Cx + 10.0f, Cy - 10.0f }, { Callout.X, Callout.Center().Y }, C.WarmAccent.ScaledAlpha(0.65f), 1.0f);
                Surface.FillRectangle(Callout, ColorRgba { 16u, 16u, 18u, 224u }, 11.0f);
                Surface.StrokeRectangle(Callout, ColorRgba { 255u, 255u, 255u, 26u }, 11.0f, 0.8f);

                Surface.FillCircle({ Callout.X + 11.0f, Callout.Y + 13.0f }, 3.2f, ColorRgba { 242u, 148u, 64u, 255u });
                char ImpactLine[48] {}, SubLine[64] {};
                std::snprintf(ImpactLine, sizeof(ImpactLine), "Impact · %.2f kJ", Session.ImpactEnergyJ / 1000.0);
                std::snprintf(SubLine, sizeof(SubLine), "%.2f, %.2f, %.2f m · %s",
                    Session.ImpactPointLocal.X, Session.ImpactPointLocal.Y, Session.ImpactPointLocal.Z,
                    Session.ActiveMaterialSpec().ShortLabel);
                Surface.Text({ Callout.X + 19.0f, Callout.Y + 7.5f }, 9.2f, C.TextPrimary, ImpactLine);
                Surface.Text({ Callout.X + 10.0f, Callout.Y + 23.0f }, 7.8f, C.TextMuted, SubLine);
            }
        }
    }

    // 10. Bottom-Left Energy Colourbar Legend
    if (Session.RenderOptions.RenderMode == FractureRenderViewMode::Energy)
    {
        const Rectangle2D Legend { CanvasRect.X + 14.0f, CanvasRect.Bottom() - 62.0f, 224.0f, 48.0f };
        Surface.FillRectangle(Legend, ColorRgba { 14u, 14u, 16u, 220u }, 12.0f);
        Surface.StrokeRectangle(Legend, ColorRgba { 255u, 255u, 255u, 24u }, 12.0f, 0.8f);
        Surface.Text({ Legend.X + 10.0f, Legend.Y + 7.0f }, 8.6f, C.TextPrimary, "Energy");
        const Rectangle2D LogBadge { Legend.Right() - 36.0f, Legend.Y + 5.0f, 28.0f, 15.0f };
        Surface.FillRectangle(LogBadge, ColorRgba { 32u, 32u, 36u, 255u }, 7.5f);
        Surface.TextCentered(LogBadge, 7.6f, C.TextSoft, "LOG");

        constexpr uint32_t Slices = 30u;
        const float BarW = Legend.Width - 20.0f;
        const float SliceW = BarW / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            uint8_t R = 0u, G = 0u, B = 0u;
            FractureViewportRenderer::SampleEnergySwatchSrgb(static_cast<float>(S) / static_cast<float>(Slices - 1u), R, G, B);
            Surface.FillRectangle({ Legend.X + 10.0f + static_cast<float>(S) * SliceW, Legend.Y + 24.0f, SliceW + 0.6f, 8.0f },
                { R, G, B, 255u }, (S == 0u || S + 1u == Slices) ? 3.5f : 0.0f);
        }
        Surface.Text({ Legend.X + 10.0f, Legend.Y + 35.0f }, 7.6f, C.TextFaint, "<= 0.1 J share");
        char PeakBuf[32] {};
        std::snprintf(PeakBuf, sizeof(PeakBuf), "%.0f J peak", Session.ImpactEnergyJ * 0.09);
        Surface.TextRightAligned({ Legend.X, Legend.Y + 35.0f, Legend.Width - 10.0f, 10.0f }, 7.6f, C.TextFaint, PeakBuf);
    }

    // 11. Bottom-Right Help Hint + World Axis Gizmo
    {
        const char* Hint = "Drag orbit · Scroll zoom · Click solid strike · F3 diag";
        const float HintW = Surface.MeasureTextWidth(8.0f, Hint);
        Surface.Text({ CanvasRect.Right() - HintW - 64.0f, CanvasRect.Bottom() - 16.0f }, 8.0f, C.TextFaint, Hint);

        const Point2D GizmoC { CanvasRect.Right() - 32.0f, CanvasRect.Bottom() - 32.0f };
        Surface.FillCircle(GizmoC, 20.0f, ColorRgba { 16u, 16u, 18u, 180u });
        Surface.StrokeLine(GizmoC, { GizmoC.X - 13.0f, GizmoC.Y + 8.0f }, ColorRgba { 239u, 83u, 80u, 220u }, 1.4f);
        Surface.StrokeLine(GizmoC, { GizmoC.X + 13.0f, GizmoC.Y + 8.0f }, ColorRgba { 105u, 208u, 109u, 220u }, 1.4f);
        Surface.StrokeLine(GizmoC, { GizmoC.X, GizmoC.Y - 14.0f }, ColorRgba { 91u, 140u, 255u, 220u }, 1.4f);
        Surface.Text({ GizmoC.X - 17.0f, GizmoC.Y + 9.0f }, 7.4f, ColorRgba { 239u, 83u, 80u, 240u }, "X");
        Surface.Text({ GizmoC.X + 13.0f, GizmoC.Y + 9.0f }, 7.4f, ColorRgba { 105u, 208u, 109u, 240u }, "Y");
        Surface.Text({ GizmoC.X - 3.0f,  GizmoC.Y - 22.0f }, 7.4f, ColorRgba { 91u, 140u, 255u, 240u }, "Z");
    }

    // 12. Floating DiagnosticsOverlay (F3)
    bool DiagInteracted = false;
    if (Session.DiagnosticsSession.Visible)
    {
        std::vector<DiagnosticsMetricChannel> Channels;
        PopulateDiagnosticsChannels(Session, Channels);
        DiagnosticsOverlaySpec DiagSpec {};
        DiagSpec.Title                = "Diagnostics";
        DiagSpec.ShortcutBadge        = "F3";
        DiagSpec.FooterSampleLabel    = "96 samples";
        DiagSpec.AllowDragResize      = true;
        DiagSpec.ShowViewSpanControls = true;
        DiagSpec.Channels             = Channels;
        const auto DiagRes = DiagnosticsOverlay::Construct(
            Surface, CanvasRect, DiagSpec, Session.DiagnosticsSession, Pointer);
        DiagInteracted = DiagRes.Interacted;
    }

    // 13. Viewport pointer orbit, zoom & click-to-strike/select
    const bool PointerInCanvas = ControlAppearance::QueryPointerOver(CanvasRect, Pointer);
    if (PointerInCanvas && !ToolRailHit && !DiagInteracted && !ExplodeSliderDrag &&
        !Session.CameraDropdownOpen && !Session.PipelineDropdownOpen && !Session.LensDropdownOpen)
    {
        if (std::abs(WheelDeltaY) > 0.001f)
        {
            Session.Camera.DistanceM = std::clamp(Session.Camera.DistanceM - WheelDeltaY * 0.25f, 1.4f, 9.0f);
        }

        if (Pointer.Pressed)
        {
            OrbitDragging = true;
            LastDragX     = Pointer.X;
            LastDragY     = Pointer.Y;
        }
        else if (OrbitDragging && Pointer.Down)
        {
            const float DX = Pointer.X - LastDragX;
            const float DY = Pointer.Y - LastDragY;
            LastDragX = Pointer.X;
            LastDragY = Pointer.Y;
            if (std::abs(DX) > 0.2f || std::abs(DY) > 0.2f)
            {
                Session.Camera.YawRadians   += DX * 0.008f;
                Session.Camera.PitchRadians = std::clamp(Session.Camera.PitchRadians + DY * 0.007f, -0.35f, 1.48f);
            }
        }

        if (OrbitDragging && Pointer.Released)
        {
            const float MoveDist = std::hypot(Pointer.X - LastDragX, Pointer.Y - LastDragY);
            OrbitDragging = false;
            if (MoveDist < 3.0f)
            {
                const float NormX = (Pointer.X - CanvasRect.X) / std::max(1.0f, CanvasRect.Width);
                const float NormY = (Pointer.Y - CanvasRect.Y) / std::max(1.0f, CanvasRect.Height);
                const auto Hit = FractureViewportRenderer::PickViewportSurface(
                    Session.HasFractured ? &Session.PartitionResult : nullptr,
                    &Session.SourceSolid,
                    (Session.ExecutionPhase != ExecutionStripPhase::Edit && Session.HasSimulatedData) ? &Session.SimRunner : nullptr,
                    Session.Camera,
                    Session.RenderOptions,
                    NormX, NormY, CachedWidthPx, CachedHeightPx);
                if (Hit.Hit)
                {
                    if (Session.ViewportTool == FractureViewportTool::Select)
                    {
                        Session.RenderOptions.SelectedFragmentIndex = Hit.FragmentIndex;
                    }
                    else
                    {
                        Session.TriggerViewportImpact(Hit.HitPointWorld, Hit.HitNormalWorld, Hit.FragmentIndex);
                    }
                }
            }
        }
    }
    if (!Pointer.Down)
    {
        OrbitDragging = false;
    }

    // 14. Open Dropdown Option Sheets (drawn over canvas)
    if (Session.CameraDropdownOpen)
    {
        uint32_t Chosen = Session.CameraViewIndex;
        if (DropdownSelector::ConstructOptionSheet(
                Surface, CamDropRect, CameraLabels.data(), static_cast<uint32_t>(CameraLabels.size()),
                Session.CameraViewIndex, Pointer, Chosen).Pressed)
        {
            Session.ApplyCameraView(Chosen);
            Session.CameraDropdownOpen = false;
        }
    }
    if (Session.PipelineDropdownOpen)
    {
        uint32_t Chosen = PipeIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Surface, PipeDropRect, PipelineLabels.data(), static_cast<uint32_t>(PipelineLabels.size()),
                PipeIdx, Pointer, Chosen).Pressed)
        {
            Session.ExecutionMode = static_cast<FractureExecutionMode>(Chosen % 3u);
            if (Session.ExecutionMode == FractureExecutionMode::Realtime)
            {
                Session.SolverMethod = FractureSolverMethod::FastRuntimeBoussinesq;
            }
            else if (Session.ExecutionMode == FractureExecutionMode::Baked &&
                     Session.SolverMethod == FractureSolverMethod::FastRuntimeBoussinesq)
            {
                Session.SolverMethod = FractureSolverMethod::EngineeringBakedSolid;
            }
            Session.RebuildFracturePartition();
            Session.PipelineDropdownOpen = false;
        }
    }
    if (Session.LensDropdownOpen)
    {
        uint32_t Chosen = LensIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Surface, LensDropRect, LensLabels.data(), static_cast<uint32_t>(LensLabels.size()),
                LensIdx, Pointer, Chosen).Pressed)
        {
            Session.RenderOptions.RenderMode = static_cast<FractureRenderViewMode>(Chosen % 4u);
            Session.LensDropdownOpen = false;
        }
    }

    Surface.PopClip();

    // 15. Optional Collapsible Log Stream Drawer
    if (Session.ShowLogDrawer && LogH > 40.0f)
    {
        Surface.FillRectangle(LogRect, ColorRgba { 16u, 16u, 18u, 255u }, 12.0f);
        Surface.StrokeRectangle(LogRect, C.Hairline, 12.0f, 1.0f);
        Surface.Text({ LogRect.X + 12.0f, LogRect.Y + 8.0f }, 9.2f, C.TextPrimary, "Log Stream");
        float Ly = LogRect.Y + 26.0f;
        const size_t StartIdx = (Session.LogEntries.size() > 5u) ? (Session.LogEntries.size() - 5u) : 0u;
        for (size_t I = StartIdx; I < Session.LogEntries.size(); ++I)
        {
            const auto& L = Session.LogEntries[I];
            char LineBuf[160] {};
            std::snprintf(LineBuf, sizeof(LineBuf), "[%.2fs] %-8s  %s  (%s)",
                L.TimeSec, L.Channel.c_str(), L.Message.c_str(), L.Detail.c_str());
            Surface.Text({ LogRect.X + 12.0f, Ly }, 8.6f, L.IsWarmTone ? C.WarmAccent : C.TextSoft, LineBuf);
            Ly += 18.0f;
        }
    }

    // 16. Bottom SimulationTimelineFooter
    const auto SimTelemetry = Session.SimRunner.InspectTelemetry();
    SimulationTimelineFooterReading FooterRead {};
    FooterRead.ElapsedSeconds       = Session.ElapsedSeconds;
    FooterRead.DurationSeconds      = Session.DurationSeconds;
    FooterRead.Playing              = Session.Playing;
    FooterRead.LoopEnabled          = Session.LoopEnabled;
    const bool ShowBakedFooter = (Session.ExecutionPhase != ExecutionStripPhase::Edit) && Session.HasSimulatedData;
    FooterRead.HasBakedTimeline     = ShowBakedFooter;
    FooterRead.TimeScale            = Session.TimeScale;
    FooterRead.CurrentKineticJoules = static_cast<float>(SimTelemetry.TotalKineticJ);
    FooterRead.MovingBodyCount      = SimTelemetry.AwakeFragmentCount;
    FooterRead.PhaseCaption         = (Session.ExecutionPhase == ExecutionStripPhase::Edit)
                                        ? "EDIT · NOT BAKED"
                                        : (Session.Playing ? "SIMULATING · JOLT" : "PAUSED · SCRUB");
    FooterRead.EmptyPromptText      = "Timeline bakes when you simulate · Shift+2 or Space to play";
    FooterRead.EnergyCurveSamples   = Session.KineticCurveSamples;
    if (ShowBakedFooter)
    {
        FooterRead.EventMarkers = Session.TimelineMarkers;
    }

    const auto FooterRes = SimulationTimelineFooter::Construct(
        Surface, FooterRect, FooterRead, Session.TimelineSession, Pointer);
    if (FooterRes.ToStartPressed) Session.RewindToStart();
    if (FooterRes.TogglePlayPressed) Session.TogglePlayPause();
    if (FooterRes.SingleStepPressed) Session.StepSingleFrame();
    if (FooterRes.LoopToggled) Session.LoopEnabled = !Session.LoopEnabled;
    if (FooterRes.TimeScaleChanged) Session.TimeScale = FooterRes.NewTimeScale;
    if (FooterRes.Scrubbed) Session.ScrubToTime(FooterRes.ScrubbedTimeSeconds);
}

} // namespace Frontier::Workspaces::Fracture
