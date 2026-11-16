/*============================================================================================================================================
                                                         FRACTUREVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureViewportHost: ExecutionModeStrip, OpenPBR 3D viewport, Energy LOG legend, DiagnosticsOverlay & TimelineFooter

#include "Engine/Workspaces/Fracture/FractureViewportHost.h"

#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Controls/SimulationTimelineFooter.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;

std::pair<std::string, std::string> SplitWholeAndFraction(float Value, const char* Fmt = "%.2f")
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
    LastSignature = 0ull;
    LastWidthPx   = 0u;
    LastHeightPx  = 0u;
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

void FractureViewportHost::PopulateDiagnosticsChannels(
    const FractureWorkspaceSession&        Session,
    std::vector<DiagnosticsMetricChannel>& OutChannels) const
{
    OutChannels.clear();
    const auto SimTelemetry = Session.SimRunner.InspectTelemetry();
    const auto SdfFootprint = Session.QuerySdfFootprint();

    // Channel 0: Frame Time
    {
        auto [W, F] = SplitWholeAndFraction(Session.LastFrameTimeMs);
        DiagnosticsMetricChannel Ch {};
        Ch.Title           = "Frame Time";
        Ch.TabCaption      = "Frame";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = Session.VulkanSpirvReady ? "FractureSurfaceResolve SPIR-V + OpenPBR" : "CPU OpenPBR Direct Studio";
        Ch.Samples         = Session.FrameTimeSamplesMs;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 33.3f;
        Ch.BudgetThreshold = 16.67f;
        Ch.HasBudgetLine   = true;
        Ch.AccentColor     = { 142u, 198u, 152u, 240u };
        char R0[48] {}, R1[48] {}, R2[48] {};
        std::snprintf(R0, sizeof(R0), "%.2f ms", Session.LastRenderTimeMs);
        std::snprintf(R1, sizeof(R1), "%u tris", Session.LastDrawnTriangles);
        std::snprintf(R2, sizeof(R2), "%s", Session.VulkanSpirvReady ? "SPIR-V Ready" : "CPU Fallback");
        Ch.Rows = {
            { "Viewport raster", R0 },
            { "Drawn triangles", R1 },
            { "Backend", R2 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // Channel 1: Fracture Partition
    {
        auto [W, F] = SplitWholeAndFraction(static_cast<float>(Session.PartitionResult.Receipt.ComputeDurationMs));
        DiagnosticsMetricChannel Ch {};
        Ch.Title           = "Fracture Partition";
        Ch.TabCaption      = "Fracture";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = Session.PartitionResult.MaterialSpec.Label;
        Ch.Samples         = { static_cast<float>(Session.PartitionResult.Receipt.ComputeDurationMs) };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 12.0f;
        Ch.AccentColor     = { 232u, 194u, 122u, 240u };
        char R0[48] {}, R1[48] {}, R2[48] {};
        std::snprintf(R0, sizeof(R0), "%u / %u", Session.PartitionResult.Receipt.FragmentCount, Session.FragmentCeiling);
        std::snprintf(R1, sizeof(R1), "%.2e", Session.PartitionResult.Receipt.VolumeDriftRatio);
        std::snprintf(R2, sizeof(R2), "%.3f m^2", Session.PartitionResult.Receipt.TotalCrackAreaM2);
        Ch.Rows = {
            { "Fragments", R0 },
            { "Volume drift", R1 },
            { "Crack area", R2 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // Channel 2: JoltPhysics Simulation
    {
        auto [W, F] = SplitWholeAndFraction(static_cast<float>(SimTelemetry.TotalKineticJ), "%.1f");
        DiagnosticsMetricChannel Ch {};
        Ch.Title           = "JoltPhysics v5.6";
        Ch.TabCaption      = "Sim";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "J";
        Ch.SourceCaption   = Session.HasSimulatedData ? "ConvexHull + BreakableBond active" : "Edit preview (not stepping)";
        Ch.Samples         = Session.KineticCurveSamples.empty() ? std::vector<float> { 0.0f } : Session.KineticCurveSamples;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = std::max(50.0f, static_cast<float>(Session.ImpactEnergyJ) * 0.25f);
        Ch.AccentColor     = { 74u, 196u, 196u, 240u };
        char R0[48] {}, R1[48] {}, R2[48] {};
        std::snprintf(R0, sizeof(R0), "%u / %u", SimTelemetry.AwakeFragmentCount, SimTelemetry.FragmentCount);
        std::snprintf(R1, sizeof(R1), "%u / %u", SimTelemetry.IntactBondCount, SimTelemetry.TotalBondCount);
        std::snprintf(R2, sizeof(R2), "%.2f m/s", SimTelemetry.MaxSpeedMps);
        Ch.Rows = {
            { "Awake bodies", R0 },
            { "Intact bonds", R1 },
            { "Peak speed", R2 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // Channel 3: SDF Memory Footprint
    {
        auto [W, F] = SplitWholeAndFraction(static_cast<float>(SdfFootprint.Megabytes));
        DiagnosticsMetricChannel Ch {};
        Ch.Title           = "Sparse SDF Volume";
        Ch.TabCaption      = "SDF";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "MB";
        Ch.SourceCaption   = Session.SdfEnabled ? "Narrow-band shard collision SDF" : "SDF disabled";
        Ch.Samples         = { static_cast<float>(SdfFootprint.Megabytes) };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 32.0f;
        Ch.AccentColor     = { 126u, 168u, 255u, 240u };
        char R0[48] {}, R1[48] {};
        std::snprintf(R0, sizeof(R0), "%ux%ux%u", Session.SdfResolution, Session.SdfResolution, Session.SdfResolution);
        std::snprintf(R1, sizeof(R1), "%u bricks", SdfFootprint.OccupiedBricks);
        Ch.Rows = {
            { "Grid resolution", R0 },
            { "Occupied bricks", R1 }
        };
        OutChannels.push_back(std::move(Ch));
    }
}

void FractureViewportHost::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 240.0f || Bounds.Height < 220.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.ViewportBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float TopBarH   = 44.0f;
    const float FooterH   = 56.0f;
    const Rectangle2D TopBarRect   { Bounds.X + 12.0f, Bounds.Y + 8.0f, Bounds.Width - 24.0f, TopBarH - 8.0f };
    const Rectangle2D FooterRect   { Bounds.X + 10.0f, Bounds.Bottom() - FooterH - 8.0f, Bounds.Width - 20.0f, FooterH };
    const Rectangle2D CanvasRect   {
        Bounds.X + 8.0f,
        Bounds.Y + TopBarH + 4.0f,
        Bounds.Width - 16.0f,
        std::max(100.0f, FooterRect.Y - (Bounds.Y + TopBarH + 10.0f))
    };

    // 1. Render 3D viewport buffer (with signature cache)
    const uint32_t TargetW = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Width * 0.75f), 240l, 1280l));
    const uint32_t TargetH = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Height * 0.75f), 160l, 900l));
    const uint64_t Sig     = Session.ComputeViewportRenderSignature();

    if (Renderer != nullptr && (Sig != LastSignature || TargetW != LastWidthPx || TargetH != LastHeightPx))
    {
        const auto Output = Renderer->RenderFrame(
            Session.HasFractured ? &Session.PartitionResult : nullptr,
            &Session.SourceSolid,
            Session.ActiveMaterialSpec(),
            Session.HasSimulatedData ? &Session.SimRunner : nullptr,
            Session.Camera,
            Session.RenderOptions,
            Session.ImpactPointLocal,
            Session.ImpactNormalLocal,
            TargetW,
            TargetH);
        Session.LastRenderTimeMs   = Output.RenderMillisec;
        Session.LastDrawnTriangles = Output.DrawnTriangles;
        Session.VulkanSpirvReady   = Output.VulkanSpirvReady;
        UploadRgbaTexture(Output.DisplayRgba8, Output.WidthPx, Output.HeightPx);
        LastSignature = Sig;
        LastWidthPx   = TargetW;
        LastHeightPx  = TargetH;
    }

    if (ViewportTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(CanvasRect, &ViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 16.0f);
    }
    Surface.StrokeRectangle(CanvasRect, C.Hairline, 16.0f, 1.0f);

    // 2. Top bar: ExecutionModeStrip on left, RenderViewMode & overlay pills on right
    {
        const float StripW = ExecutionModeStrip::PreferredStripWidth(true);
        const Rectangle2D StripRect {
            TopBarRect.X,
            TopBarRect.Y + 2.0f,
            std::min(StripW, TopBarRect.Width * 0.45f),
            ExecutionModeStrip::RecommendedHeightPx
        };
        ExecutionModeStripReading StripRead {};
        StripRead.ActivePhase        = Session.ExecutionPhase;
        StripRead.PlayPaused         = !Session.Playing;
        StripRead.ShowRewindButton   = true;
        StripRead.ShowPauseWhenPlaying = true;
        const auto StripRes = ExecutionModeStrip::Construct(Surface, StripRect, StripRead, Pointer);
        if (StripRes.RewindPressed)
        {
            Session.RewindToStart();
        }
        else if (StripRes.PlayToggleRequested)
        {
            Session.TogglePlayPause();
        }
        else if (StripRes.PhaseChanged)
        {
            Session.TransitionExecutionPhase(StripRes.SelectedPhase);
        }

        // Render view mode capsules + overlay pills on right side
        static constexpr std::array<const char*, 4> ModeCaptions = { "OpenPBR", "Energy", "Cells", "Normals" };
        float CursorX = TopBarRect.Right();
        const auto DrawMiniToggle = [&](const char* Label, bool Active, ColorRgba Accent) -> bool
        {
            const float PillW = std::max(46.0f, Surface.MeasureTextWidth(9.2f, Label) + 16.0f);
            CursorX -= PillW + 4.0f;
            const Rectangle2D Rect { CursorX, TopBarRect.Y + 4.0f, PillW, 26.0f };
            const bool Hov = ControlAppearance::QueryPointerOver(Rect, Pointer);
            Surface.FillRectangle(Rect, Active ? C.CardElevated : (Hov ? C.CardBg : C.WellBg), 13.0f);
            Surface.StrokeRectangle(Rect, Active ? Accent : C.Hairline, 13.0f, 1.0f);
            Surface.TextCentered(Rect, 9.2f, Active ? Accent : C.TextSoft, Label);
            return Hov && Pointer.Pressed;
        };

        if (DrawMiniToggle("Diag", Session.DiagnosticsSession.Visible, C.TealTint))
        {
            Session.DiagnosticsSession.Visible = !Session.DiagnosticsSession.Visible;
        }
        if (DrawMiniToggle("Wave", Session.RenderOptions.ShowWavefronts, C.WarmAccent))
        {
            Session.RenderOptions.ShowWavefronts = !Session.RenderOptions.ShowWavefronts;
        }
        if (DrawMiniToggle("Wire", Session.RenderOptions.ShowWireframe, C.TextPrimary))
        {
            Session.RenderOptions.ShowWireframe = !Session.RenderOptions.ShowWireframe;
        }
        if (DrawMiniToggle("Bonds", Session.RenderOptions.ShowBonds, C.LiveGreen))
        {
            Session.RenderOptions.ShowBonds = !Session.RenderOptions.ShowBonds;
        }

        CursorX -= 8.0f;
        for (int32_t M = 3; M >= 0; --M)
        {
            const auto Mode = static_cast<FractureRenderViewMode>(M);
            if (DrawMiniToggle(ModeCaptions[static_cast<size_t>(M)], Session.RenderOptions.RenderMode == Mode, C.WarmAccent))
            {
                Session.RenderOptions.RenderMode = Mode;
            }
        }
    }

    // 3. Energy heatmap LOG legend when RenderMode == Energy
    if (Session.RenderOptions.RenderMode == FractureRenderViewMode::Energy)
    {
        const Rectangle2D LegendRect { CanvasRect.X + 14.0f, CanvasRect.Bottom() - 48.0f, 224.0f, 36.0f };
        Surface.FillRectangle(LegendRect, { 12u, 12u, 14u, 220u }, 10.0f);
        Surface.StrokeRectangle(LegendRect, C.HairlineStrong, 10.0f, 1.0f);
        Surface.Text({ LegendRect.X + 10.0f, LegendRect.Y + 5.0f }, 8.6f, C.TextSoft, "STRAIN ENERGY (LOG10 J/m3)");

        const Rectangle2D BarRect { LegendRect.X + 10.0f, LegendRect.Y + 19.0f, LegendRect.Width - 20.0f, 8.0f };
        constexpr uint32_t Steps = 24u;
        const float StepW = BarRect.Width / static_cast<float>(Steps);
        for (uint32_t I = 0u; I < Steps; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Steps - 1u);
            ColorRgba Col { 20u, 62u, 235u, 255u };
            if (T < 0.25f)      Col = ColorRgba::Blend({ 20u, 62u, 235u, 255u },  { 16u, 198u, 220u, 255u }, T / 0.25f);
            else if (T < 0.50f) Col = ColorRgba::Blend({ 16u, 198u, 220u, 255u }, { 46u, 220u, 88u, 255u },  (T - 0.25f) / 0.25f);
            else if (T < 0.75f) Col = ColorRgba::Blend({ 46u, 220u, 88u, 255u },  { 250u, 214u, 32u, 255u }, (T - 0.50f) / 0.25f);
            else                Col = ColorRgba::Blend({ 250u, 214u, 32u, 255u }, { 244u, 36u, 26u, 255u },  (T - 0.75f) / 0.25f);
            Surface.FillRectangle({ BarRect.X + static_cast<float>(I) * StepW, BarRect.Y, StepW + 0.6f, BarRect.Height }, Col, 2.0f);
        }
    }

    // 4. Floating DiagnosticsOverlay
    if (Session.DiagnosticsSession.Visible)
    {
        std::vector<DiagnosticsMetricChannel> Channels;
        PopulateDiagnosticsChannels(Session, Channels);
        DiagnosticsOverlaySpec DiagSpec {};
        DiagSpec.Title            = "Fracture Diagnostics";
        DiagSpec.ShortcutBadge    = "F3";
        DiagSpec.FooterSampleLabel = "96 samples";
        DiagSpec.Channels         = Channels;
        const auto DiagRes = DiagnosticsOverlay::Construct(
            Surface, CanvasRect, DiagSpec, Session.DiagnosticsSession, Pointer);
        (void)DiagRes;
    }

    // 5. Viewport orbit, scroll zoom & click-to-target impact interaction
    const bool OverDiag = Session.DiagnosticsSession.Visible &&
                          ControlAppearance::QueryPointerOver(Session.DiagnosticsSession.PanelBounds, Pointer);
    if (ControlAppearance::QueryPointerOver(CanvasRect, Pointer) && !OverDiag)
    {
        if (std::abs(WheelDeltaY) > 1.0e-4f)
        {
            Session.Camera.DistanceM = std::clamp(Session.Camera.DistanceM * (1.0f - WheelDeltaY * 0.09f), 1.1f, 12.0f);
        }

        if (Pointer.Pressed)
        {
            OrbitDragging = true;
            DragStartPos  = { Pointer.X, Pointer.Y };
            LastDragPos   = { Pointer.X, Pointer.Y };
            DragMovedPx   = 0.0f;
        }
    }

    if (OrbitDragging)
    {
        if (Pointer.Down)
        {
            const float Dx = Pointer.X - LastDragPos.X;
            const float Dy = Pointer.Y - LastDragPos.Y;
            LastDragPos    = { Pointer.X, Pointer.Y };
            DragMovedPx   += std::hypot(Dx, Dy);
            Session.Camera.YawRadians   -= Dx * 0.0085f;
            Session.Camera.PitchRadians  = std::clamp(Session.Camera.PitchRadians + Dy * 0.0075f, -0.45f, 1.35f);
        }
        else
        {
            OrbitDragging = false;
            if (DragMovedPx < 5.0f && ControlAppearance::QueryPointerOver(CanvasRect, Pointer))
            {
                const float NormX = (Pointer.X - CanvasRect.X) / std::max(1.0f, CanvasRect.Width);
                const float NormY = (Pointer.Y - CanvasRect.Y) / std::max(1.0f, CanvasRect.Height);
                const auto Hit = FractureViewportRenderer::PickViewportSurface(
                    Session.HasFractured ? &Session.PartitionResult : nullptr,
                    &Session.SourceSolid,
                    Session.HasSimulatedData ? &Session.SimRunner : nullptr,
                    Session.Camera,
                    Session.RenderOptions,
                    NormX,
                    NormY,
                    TargetW,
                    TargetH);
                if (Hit.Hit)
                {
                    Session.TriggerViewportImpact(Hit.HitPointWorld, Hit.HitNormalWorld, Hit.FragmentIndex);
                }
            }
        }
    }

    // 6. SimulationTimelineFooter (56px)
    {
        const auto SimTel = Session.SimRunner.InspectTelemetry();
        SimulationTimelineFooterReading FooterRead {};
        FooterRead.ElapsedSeconds       = Session.ElapsedSeconds;
        FooterRead.DurationSeconds      = Session.DurationSeconds;
        FooterRead.Playing              = Session.Playing;
        FooterRead.LoopEnabled          = Session.LoopEnabled;
        FooterRead.HasBakedTimeline     = Session.HasSimulatedData;
        FooterRead.TimeScale            = Session.TimeScale;
        FooterRead.CurrentKineticJoules = static_cast<float>(SimTel.TotalKineticJ);
        FooterRead.MovingBodyCount      = SimTel.AwakeFragmentCount;
        FooterRead.PhaseCaption         = Session.ExecutionPhase == ExecutionStripPhase::Edit
            ? "EDIT · READY"
            : (Session.Playing ? "SIMULATING · JOLT" : "PAUSED · POSE");
        FooterRead.EnergyCurveSamples   = Session.KineticCurveSamples;
        FooterRead.EventMarkers         = Session.TimelineMarkers;

        const auto FooterRes = SimulationTimelineFooter::Construct(
            Surface, FooterRect, FooterRead, Session.TimelineSession, Pointer);
        if (FooterRes.ToStartPressed)
        {
            Session.RewindToStart();
        }
        else if (FooterRes.TogglePlayPressed)
        {
            Session.TogglePlayPause();
        }
        else if (FooterRes.SingleStepPressed)
        {
            Session.StepSingleFrame();
        }
        else if (FooterRes.Scrubbed)
        {
            Session.ScrubToTime(FooterRes.ScrubbedTimeSeconds);
        }
        else if (FooterRes.TimeScaleChanged)
        {
            Session.TimeScale = FooterRes.NewTimeScale;
        }
        else if (FooterRes.LoopToggled)
        {
            Session.LoopEnabled = !Session.LoopEnabled;
        }
    }
}

} // namespace Frontier::Workspaces::Fracture
