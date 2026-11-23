/*============================================================================================================================================
                                                         FRACTUREVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureViewportHost matching Port 8091 HTML Fracture Editor (2-row vp-bar, chips, callout, Diagnostics, Library, Timeline)

#include "Engine/Workspaces/Fracture/FractureViewportHost.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExecutionModeStrip.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Controls/SimulationTimelineFooter.h"
#include "Engine/Components/Diagnostics/DiagnosticsOverlay.h"
#include "Engine/Components/Panes/StudioPaneAppearance.h"
#include "Engine/Workspaces/Fracture/FractureIcons.h"

#include <algorithm>
#include <array>
#include <cctype>
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

using namespace StudioAppearance;
using GeometrySpace::Fracture::FractureSolverMethod;

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

    // 0: Frame rate / time
    {
        auto [W, F] = SplitDecimalParts(Session.LastFrameTimeMs);
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Frame";
        Ch.Title           = "Frame interval";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = Session.VulkanSpirvReady ? "Vulkan SPIR-V + OpenPBR Studio" : "CPU OpenPBR Studio";
        Ch.Samples         = Session.FrameTimeSamplesMs;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = 33.3f;
        Ch.HasBudgetLine   = true;
        Ch.BudgetThreshold = 16.67f;
        Ch.AccentColor     = { 111u, 207u, 138u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {};
        std::snprintf(R1, sizeof(R1), "%.2f ms", Session.LastRenderTimeMs);
        std::snprintf(R2, sizeof(R2), "%u tris", Session.LastDrawnTriangles);
        std::snprintf(R3, sizeof(R3), "%s", Session.VulkanSpirvReady ? "Vulkan 1.3" : "CPU Direct");
        Ch.Rows = {
            { "Viewport raster", R1 },
            { "Drawn triangles", R2 },
            { "Backend", R3 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 1: Bake timing (matches diagnostics.png!)
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(Session.LastTimings.TotalMs), Session.LastTimings.TotalMs < 100.0 ? "%.1f" : "%.0f");
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Bake";
        Ch.Title           = "Bake timing";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "ms";
        Ch.SourceCaption   = "Wall time of fracture bake on main thread (seeding, Voronoi cells, needle rejection).";
        Ch.Samples         = {
            static_cast<float>(Session.LastTimings.TotalMs * 1.06),
            static_cast<float>(Session.LastTimings.TotalMs * 0.98),
            static_cast<float>(Session.LastTimings.TotalMs)
        };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = std::max(50.0f, static_cast<float>(Session.LastTimings.TotalMs * 1.6));
        Ch.HasBudgetLine   = true;
        Ch.BudgetThreshold = (Session.MethodMode == FractureMethodMode::Realtime) ? static_cast<float>(Session.RealtimeBudgetMs) : 100.0f;
        Ch.AccentColor     = { 232u, 194u, 122u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {}, R4[48] {};
        std::snprintf(R1, sizeof(R1), "%.2f ms", Session.LastTimings.SeedingMs);
        std::snprintf(R2, sizeof(R2), "%.1f ms", Session.LastTimings.CellsMs);
        std::snprintf(R3, sizeof(R3), "%.1f ms", Session.LastTimings.NeedlesMs);
        std::snprintf(R4, sizeof(R4), "%.2f ms", Session.LastTimings.SortMs);
        Ch.Rows = {
            { "Seeding", R1 },
            { "Cells + seams", R2 },
            { "Needle rejection", R3 },
            { "Sort + totals", R4 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 2: Fracture result
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(Session.PartitionResult.Fragments.size()), "%.0f");
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Fracture";
        Ch.Title           = "Fracture result";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = "";
        Ch.PrimaryUnit     = "pieces";
        Ch.SourceCaption   = FractureWorkspaceSession::QuerySolverShortLabel(Session.SolverMethod);
        Ch.Samples         = { static_cast<float>(Session.PartitionResult.Fragments.size()), static_cast<float>(Session.PartitionResult.Fragments.size()) };
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = static_cast<float>(std::max(10u, Session.FragmentCeiling));
        Ch.AccentColor     = { 122u, 167u, 224u, 255u };

        char R1[48] {}, R2[48] {}, R3[48] {};
        std::snprintf(R1, sizeof(R1), "%zu / %u", Session.PartitionResult.Fragments.size(), Session.FragmentCeiling);
        std::snprintf(R2, sizeof(R2), "%.2e", Rec.VolumeDriftRatio);
        std::snprintf(R3, sizeof(R3), "%.2f m2", Rec.TotalCrackAreaM2);
        Ch.Rows = {
            { "Pieces / ceiling", R1 },
            { "Volume drift", R2 },
            { "Crack area", R3 }
        };
        OutChannels.push_back(std::move(Ch));
    }

    // 3: Kinetic energy
    {
        auto [W, F] = SplitDecimalParts(static_cast<float>(SimTelemetry.TotalKineticJ), "%.1f");
        DiagnosticsMetricChannel Ch {};
        Ch.TabCaption      = "Kinetic";
        Ch.Title           = "Kinetic energy";
        Ch.PrimaryWhole    = W;
        Ch.PrimaryFraction = F;
        Ch.PrimaryUnit     = "J";
        Ch.SourceCaption   = "JoltPhysics v5.6.0 · BreakableBonds";
        Ch.Samples         = Session.KineticCurveSamples.empty() ? std::vector<float> { 0.0f } : Session.KineticCurveSamples;
        Ch.PlotMinBound    = 0.0f;
        Ch.PlotMaxBound    = std::max(10.0f, static_cast<float>(Session.ImpactEnergyJ) * 0.5f);
        Ch.AccentColor     = { 255u, 138u, 61u, 255u };

        char R1[48] {}, R2[48] {};
        std::snprintf(R1, sizeof(R1), "%u / %u", SimTelemetry.IntactBondCount, SimTelemetry.TotalBondCount);
        std::snprintf(R2, sizeof(R2), "%u / %u", SimTelemetry.AwakeFragmentCount, SimTelemetry.FragmentCount);
        Ch.Rows = {
            { "Intact bonds", R1 },
            { "Moving shards", R2 }
        };
        OutChannels.push_back(std::move(Ch));
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
        if (!NeedsCreate)
        {
            ViewportTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        }
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

    Surface.FillRectangle(Bounds, ColorRgba { 18u, 18u, 18u, 255u }, 0.0f);

    const auto& Prims = FractureWorkspaceSession::InspectPrimitives();
    const auto& ActivePrim = Prims[static_cast<uint32_t>(Session.ActiveShape) % FractureWorkspaceSession::PrimitiveCount];

    // 1. Port-8091 2-Row Viewport Bar (vp-bar)
    const float Row1Y = Bounds.Y + 4.0f;
    const float Row2Y = Bounds.Y + 52.0f;
    {
        // Row 1 Left: [Primitive SVG] Title / Sub + [LIVE FRACTURE / BAKED / SIMULATED] badge
        FractureIcons::DrawPrimitiveIcon(Surface, { Bounds.X + 14.0f, Row1Y + 11.0f, 26.0f, 26.0f }, Session.ActiveShape, 1.0f);
        Surface.Text({ Bounds.X + 49.0f, Row1Y + 9.0f }, 12.2f, ColorRgba { 226u, 226u, 226u, 255u }, ActivePrim.Title);

        char SubStr[96] {};
        std::snprintf(SubStr, sizeof(SubStr), "%zu fragments · %s · %s",
            Session.PartitionResult.Fragments.size(),
            Session.ActiveMaterialSpec().ShortLabel,
            Session.FormatPrimitiveSubtitle().c_str());
        Surface.Text({ Bounds.X + 49.0f, Row1Y + 25.5f }, 8.8f, ColorRgba { 105u, 105u, 105u, 255u }, SubStr);

        const float SubW   = Surface.MeasureTextWidth(8.8f, SubStr);
        const float TitleW = Surface.MeasureTextWidth(12.2f, ActivePrim.Title);
        const float BadgeX = Bounds.X + 58.0f + std::max(TitleW, SubW);

        const bool IsSim = (Session.ExecutionPhase != ExecutionStripPhase::Edit);
        char BadgeBuf[40] {};
        if (IsSim)
        {
            std::snprintf(BadgeBuf, sizeof(BadgeBuf), "SIMULATED · %.0f MS", Session.LastTimings.TotalMs);
        }
        else if (Session.MethodMode == FractureMethodMode::Baked)
        {
            std::snprintf(BadgeBuf, sizeof(BadgeBuf), "BAKED · LIVE PREVIEW");
        }
        else
        {
            std::snprintf(BadgeBuf, sizeof(BadgeBuf), "LIVE FRACTURE");
        }
        const float BadgeW = Surface.MeasureTextWidth(7.8f, BadgeBuf) + 16.0f;
        const Rectangle2D BadgeRect { BadgeX, Row1Y + 13.0f, BadgeW, 20.0f };
        Surface.FillRectangle(BadgeRect, IsSim ? ColorRgba { 185u, 151u, 86u, 20u } : ColorRgba { 52u, 199u, 89u, 20u }, 5.0f);
        Surface.StrokeRectangle(BadgeRect, IsSim ? ColorRgba { 185u, 151u, 86u, 52u } : ColorRgba { 52u, 199u, 89u, 58u }, 5.0f, 0.9f);
        Surface.TextCentered(BadgeRect, 7.8f, IsSim ? ColorRgba { 191u, 162u, 122u, 255u } : ColorRgba { 91u, 219u, 135u, 255u }, BadgeBuf);

        // Row 1 Right: Edit | Sim | Play segmented strip
        const float StripW = ExecutionModeStrip::PreferredStripWidth(false);
        const Rectangle2D StripRect { Bounds.Right() - StripW - 12.0f, Row1Y + 9.0f, StripW, ExecutionModeStrip::RecommendedHeightPx };
        ExecutionModeStripReading StripRead {};
        StripRead.ActivePhase          = Session.ExecutionPhase;
        StripRead.PlayPaused           = !Session.Playing;
        StripRead.ShowRewindButton     = false;
        StripRead.ShowPauseWhenPlaying = true;
        StripRead.EditCaption          = "Edit";
        StripRead.SimulateCaption      = "Sim";
        StripRead.PlayCaption          = "Play";
        const auto StripRes = ExecutionModeStrip::Construct(Surface, StripRect, StripRead, Pointer);
        if (StripRes.PhaseChanged)
        {
            Session.TransitionExecutionPhase(StripRes.SelectedPhase);
        }
    }

    // Row 2 divider + controls: [Simulate Shift+2] [Frame] [⚡] | [Shading v] [Explode --- 0%] ... [Diag] [Wire] [Seeds] [Grid]
    Surface.StrokeLine({ Bounds.X + 12.0f, Row2Y - 5.0f }, { Bounds.Right() - 12.0f, Row2Y - 5.0f }, ColorRgba { 255u, 255u, 255u, 10u }, 1.0f);

    static constexpr std::array<const char*, 4> ShadingLabels {{
        "Material", "Energy", "Cells", "Normals"
    }};
    const uint32_t ShadingIdx = static_cast<uint32_t>(Session.RenderOptions.RenderMode) % 4u;
    Rectangle2D ShadingDropRect {};
    Rectangle2D DebugDropRect {};
    {
        float Ax = Bounds.X + 12.0f;

        // Primary white pill: [Simulate  Shift+2] or [Play  Shift+3]
        const char* PrimTxt = (Session.ExecutionPhase == ExecutionStripPhase::Edit) ? "Simulate" : (Session.Playing ? "Pause" : "Play");
        const char* KeyTxt  = (Session.ExecutionPhase == ExecutionStripPhase::Edit) ? "Shift+2" : "Shift+3";
        const float PrimW   = 118.0f;
        const Rectangle2D PrimBtn { Ax, Row2Y, PrimW, 31.0f };
        const bool PrimHov = ControlAppearance::QueryPointerOver(PrimBtn, Pointer);
        Surface.FillRectangle(PrimBtn, PrimHov ? ColorRgba { 242u, 242u, 239u, 255u } : ColorRgba { 222u, 222u, 219u, 255u }, 15.5f);
        FractureIcons::DrawSimulateGearPlayIcon(Surface, { PrimBtn.X + 9.0f, PrimBtn.Center().Y - 8.5f, 17.0f, 17.0f });
        Surface.Text({ PrimBtn.X + 31.0f, PrimBtn.Y + 9.5f }, 10.6f, ColorRgba { 34u, 34u, 34u, 255u }, PrimTxt);
        const Rectangle2D KeyPill { PrimBtn.Right() - 40.0f, PrimBtn.Center().Y - 8.0f, 34.0f, 16.0f };
        Surface.FillRectangle(KeyPill, ColorRgba { 0u, 0u, 0u, 20u }, 5.0f);
        Surface.TextCentered(KeyPill, 7.6f, ColorRgba { 88u, 88u, 86u, 255u }, KeyTxt);
        if (PrimHov && Pointer.Pressed)
        {
            Session.TogglePlayPause();
        }
        Ax += PrimW + 7.0f;

        // Frame button
        const Rectangle2D FrameBtn { Ax, Row2Y, 31.0f, 31.0f };
        const bool FrameHov = ControlAppearance::QueryPointerOver(FrameBtn, Pointer);
        Surface.FillRectangle(FrameBtn, FrameHov ? ColorRgba { 34u, 34u, 34u, 255u } : ColorRgba { 26u, 26u, 26u, 255u }, 8.0f);
        FractureIcons::DrawFrameGlyph(Surface, { FrameBtn.Center().X - 7.5f, FrameBtn.Center().Y - 7.5f, 15.0f, 15.0f },
            FrameHov ? ColorRgba { 228u, 228u, 226u, 255u } : ColorRgba { 147u, 147u, 147u, 255u });
        if (FrameHov && Pointer.Pressed)
        {
            Session.ApplyCameraView(0u);
        }
        Ax += 38.0f;

        // Impact tool button
        const Rectangle2D ImpactBtn { Ax, Row2Y, 31.0f, 31.0f };
        const bool ImpHov = ControlAppearance::QueryPointerOver(ImpactBtn, Pointer);
        Surface.FillRectangle(ImpactBtn, Session.AimingImpactTool ? ColorRgba { 191u, 162u, 122u, 28u } : (ImpHov ? ColorRgba { 34u, 34u, 34u, 255u } : ColorRgba { 26u, 26u, 26u, 255u }), 8.0f);
        if (Session.AimingImpactTool)
        {
            Surface.StrokeRectangle(ImpactBtn, ColorRgba { 191u, 162u, 122u, 88u }, 8.0f, 1.0f);
        }
        FractureIcons::DrawLightningIcon(Surface, { ImpactBtn.Center().X - 8.0f, ImpactBtn.Center().Y - 8.0f, 16.0f, 16.0f },
            Session.AimingImpactTool ? ColorRgba { 222u, 193u, 147u, 255u } : ColorRgba { 148u, 160u, 178u, 255u });
        if (ImpHov && Pointer.Pressed)
        {
            Session.AimingImpactTool = !Session.AimingImpactTool;
        }
        Ax += 39.0f;

        Surface.StrokeLine({ Ax, Row2Y + 6.0f }, { Ax, Row2Y + 25.0f }, ColorRgba { 255u, 255u, 255u, 14u }, 1.0f);
        Ax += 9.0f;

        // Shading dropdown [Sphere  Energy  v]
        ShadingDropRect = { Ax, Row2Y, 114.0f, 31.0f };
        const bool ShadeHov = ControlAppearance::QueryPointerOver(ShadingDropRect, Pointer);
        Surface.FillRectangle(ShadingDropRect, ShadeHov ? ColorRgba { 32u, 32u, 33u, 255u } : ColorRgba { 26u, 26u, 26u, 255u }, 9.0f);
        Surface.StrokeRectangle(ShadingDropRect, ColorRgba { 255u, 255u, 255u, 18u }, 9.0f, 0.9f);
        FractureIcons::DrawSphereWireGlyph(Surface, { ShadingDropRect.X + 9.0f, ShadingDropRect.Center().Y - 7.0f, 14.0f, 14.0f }, ColorRgba { 145u, 145u, 143u, 255u });
        Surface.Text({ ShadingDropRect.X + 27.0f, ShadingDropRect.Y + 9.5f }, 10.4f, ColorRgba { 196u, 196u, 194u, 255u }, ShadingLabels[ShadingIdx]);
        FractureIcons::DrawChevronDownGlyph(Surface, { ShadingDropRect.Right() - 18.0f, ShadingDropRect.Center().Y - 6.0f, 12.0f, 12.0f }, ColorRgba { 138u, 138u, 136u, 255u });
        if (ShadeHov && Pointer.Pressed)
        {
            Session.ShadingDropdownOpen = !Session.ShadingDropdownOpen;
            Session.DebugDropdownOpen   = false;
        }
        Ax = ShadingDropRect.Right() + 7.0f;

        // Debug dropdown [Wire  Debug  v] (Collision shapes, Mesh wireframe, Bonds, Wavefronts, Grid, Diagnostics)
        DebugDropRect = { Ax, Row2Y, 96.0f, 31.0f };
        const bool DebugHov    = ControlAppearance::QueryPointerOver(DebugDropRect, Pointer);
        const bool DebugActive = Session.RenderOptions.ShowCollisionShapes || Session.DebugDropdownOpen;
        Surface.FillRectangle(DebugDropRect,
            DebugActive ? ColorRgba { 52u, 199u, 89u, 24u } : (DebugHov ? ColorRgba { 32u, 32u, 33u, 255u } : ColorRgba { 26u, 26u, 26u, 255u }), 9.0f);
        Surface.StrokeRectangle(DebugDropRect,
            DebugActive ? ColorRgba { 72u, 214u, 128u, 96u } : ColorRgba { 255u, 255u, 255u, 18u }, 9.0f, 0.95f);
        FractureIcons::DrawWireCubeGlyph(Surface, { DebugDropRect.X + 9.0f, DebugDropRect.Center().Y - 7.0f, 14.0f, 14.0f },
            Session.RenderOptions.ShowCollisionShapes ? ColorRgba { 91u, 219u, 135u, 255u } : ColorRgba { 145u, 145u, 143u, 255u });
        Surface.Text({ DebugDropRect.X + 27.0f, DebugDropRect.Y + 9.5f }, 10.4f,
            Session.RenderOptions.ShowCollisionShapes ? ColorRgba { 224u, 246u, 232u, 255u } : ColorRgba { 196u, 196u, 194u, 255u }, "Debug");
        FractureIcons::DrawChevronDownGlyph(Surface, { DebugDropRect.Right() - 18.0f, DebugDropRect.Center().Y - 6.0f, 12.0f, 12.0f }, ColorRgba { 138u, 138u, 136u, 255u });
        if (DebugHov && Pointer.Pressed)
        {
            Session.DebugDropdownOpen   = !Session.DebugDropdownOpen;
            Session.ShadingDropdownOpen = false;
        }
        Ax = DebugDropRect.Right() + 8.0f;

        // Explode pill using Frontier native C++ ScalarSlider capsule component
        const float ExpW = std::min(196.0f, Bounds.Right() - Ax - 156.0f);
        if (ExpW > 130.0f)
        {
            Surface.Text({ Ax + 2.0f, Row2Y + 9.0f }, 10.6f, ColorRgba { 154u, 154u, 152u, 255u }, "Explode");
            float UpdatedExplode = Session.RenderOptions.ExplodeFactor * 100.0f;
            const float SliderLeft = Ax + 52.0f;
            const float ReadoutW   = 44.0f;
            const Rectangle2D SliderRect { SliderLeft, Row2Y + 4.0f, std::max(44.0f, ExpW - 52.0f - ReadoutW - 6.0f), 23.0f };
            const ControlContact SlContact = ScalarSlider::Construct(
                Surface, SliderRect, 0.0f, 100.0f, UpdatedExplode, ExplodeSliderDrag, Pointer, UpdatedExplode,
                1.0f, ControlMetrics::SliderHeight * 0.78f, ControlMetrics::SliderThumbDiameter * 0.76f);
            if (Pointer.Pressed && SlContact.Hovered) ExplodeSliderDrag = true;
            if (!Pointer.Down) ExplodeSliderDrag = false;
            if (ExplodeSliderDrag || std::abs(UpdatedExplode - Session.RenderOptions.ExplodeFactor * 100.0f) > 0.05f)
            {
                Session.RenderOptions.ExplodeFactor = std::clamp(UpdatedExplode / 100.0f, 0.0f, 1.0f);
            }
            char ExpPct[16] {};
            std::snprintf(ExpPct, sizeof(ExpPct), "%d %%", static_cast<int>(std::lround(Session.RenderOptions.ExplodeFactor * 100.0f)));
            const Rectangle2D ReadPill { SliderRect.Right() + 6.0f, Row2Y + 4.0f, ReadoutW, 23.0f };
            Surface.FillRectangle(ReadPill, ColorRgba { 24u, 24u, 26u, 255u }, 11.5f);
            Surface.StrokeRectangle(ReadPill, ColorRgba { 255u, 255u, 255u, 22u }, 11.5f, 1.0f);
            Surface.TextCentered(ReadPill, 10.2f, ColorRgba { 214u, 214u, 212u, 255u }, ExpPct);
        }

        // Right 4 Circular Icon Buttons: [Diag pulse] [Wire] [Seeds] [Grid #]
        float Rx = Bounds.Right() - 12.0f;
        auto DrawRoundTool = [&](bool& Active, int GlyphKind)
        {
            Rx -= 31.0f;
            const Rectangle2D Btn { Rx, Row2Y, 31.0f, 31.0f };
            const bool Hov = ControlAppearance::QueryPointerOver(Btn, Pointer);
            Surface.FillCircle(Btn.Center(), 15.5f,
                Active ? ColorRgba { 191u, 162u, 122u, 26u } : (Hov ? ColorRgba { 35u, 35u, 35u, 255u } : ColorRgba { 27u, 27u, 27u, 255u }));
            Surface.StrokeCircle(Btn.Center(), 15.5f,
                Active ? ColorRgba { 191u, 162u, 122u, 90u } : ColorRgba { 255u, 255u, 255u, 18u }, 1.0f);
            const ColorRgba Ink = Active ? ColorRgba { 222u, 193u, 147u, 255u } : ColorRgba { 154u, 154u, 152u, 255u };
            const Rectangle2D GBox { Btn.Center().X - 7.5f, Btn.Center().Y - 7.5f, 15.0f, 15.0f };
            if (GlyphKind == 0) FractureIcons::DrawPulseGlyph(Surface, GBox, Ink);
            else if (GlyphKind == 1) FractureIcons::DrawWireCubeGlyph(Surface, GBox, Ink);
            else if (GlyphKind == 2) FractureIcons::DrawSeedsDotsGlyph(Surface, GBox, Ink);
            else FractureIcons::DrawGridGlyph(Surface, GBox, Ink);

            if (Hov && Pointer.Pressed) Active = !Active;
            Rx -= 6.0f;
        };

        DrawRoundTool(Session.RenderOptions.ShowGroundGrid, 3);
        DrawRoundTool(Session.RenderOptions.ShowWavefronts, 2);
        DrawRoundTool(Session.RenderOptions.ShowWireframe, 1);
        DrawRoundTool(Session.DiagnosticsSession.Visible, 0);
    }

    // 2. Well, 52px Playback Bar & 34px Footer
    constexpr float FootH     = 34.0f;
    constexpr float PlaybackH = 60.0f;
    const Rectangle2D FootRect     { Bounds.X, Bounds.Bottom() - FootH, Bounds.Width, FootH };
    const Rectangle2D PlaybackRect { Bounds.X + 12.0f, FootRect.Y - PlaybackH - 4.0f, Bounds.Width - 24.0f, PlaybackH };
    const Rectangle2D WellRect     { Bounds.X + 12.0f, Row2Y + 42.0f, Bounds.Width - 24.0f, std::max(140.0f, PlaybackRect.Y - (Row2Y + 42.0f) - 9.0f) };

    // 3. Render 3D Viewport Frame via ImTextureData
    Session.RenderOptions.CollisionDebugMode = Session.CollisionShapeMode;
    const uint32_t RenderW = static_cast<uint32_t>(std::clamp(std::lround(WellRect.Width * 0.90f), 240l, 1280l));
    const uint32_t RenderH = static_cast<uint32_t>(std::clamp(std::lround(WellRect.Height * 0.90f), 160l, 900l));
    const uint64_t Sig     = Session.ComputeViewportRenderSignature();

    FractureViewportRenderer& ActiveRenderer = (Renderer != nullptr) ? *Renderer : FallbackRenderer;
    if (Sig != CachedSignature || RenderW != CachedWidthPx || RenderH != CachedHeightPx || ViewportTexture.Pixels == nullptr)
    {
        const auto* SimPtr = (Session.ExecutionPhase != ExecutionStripPhase::Edit && Session.HasSimulatedData)
            ? &Session.SimRunner : nullptr;
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

    Surface.PushClip(WellRect);
    if (ViewportTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(WellRect, &ViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 16.0f);
    }
    Surface.StrokeRectangle(WellRect, ColorRgba { 255u, 255u, 255u, 14u }, 16.0f, 1.0f);

    // 4. Top-Left Floating Status Chips
    {
        float ChipY = WellRect.Y + 13.0f;
        const float ChipX = WellRect.X + 13.0f;

        auto DrawChip = [&](const char* Txt, bool CheckBadge)
        {
            const float W = Surface.MeasureTextWidth(9.8f, Txt) + 38.0f;
            const Rectangle2D Pill { ChipX, ChipY, W, 30.0f };
            Surface.FillRectangle(Pill, ColorRgba { 22u, 22u, 22u, 212u }, 15.0f);
            Surface.StrokeRectangle(Pill, ColorRgba { 255u, 255u, 255u, 22u }, 15.0f, 0.9f);
            const Point2D Bc { Pill.X + 17.0f, Pill.Center().Y };
            if (CheckBadge)
            {
                Surface.FillCircle(Bc, 8.0f, ColorRgba { 52u, 199u, 89u, 255u });
                FractureIcons::DrawCheckGlyph(Surface, { Bc.X - 4.5f, Bc.Y - 4.5f, 9.0f, 9.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.6f);
            }
            else
            {
                Surface.FillCircle(Bc, 8.0f, ColorRgba { 42u, 42u, 42u, 255u });
                Surface.TextCentered({ Bc.X - 6.0f, Bc.Y - 6.0f, 12.0f, 12.0f }, 8.0f, ColorRgba { 180u, 180u, 178u, 255u }, "#");
            }
            Surface.Text({ Pill.X + 31.0f, Pill.Y + 9.5f }, 9.8f, ColorRgba { 222u, 222u, 220u, 255u }, Txt);
            ChipY += 36.0f;
        };

        const double KeptPct = (1.0 - std::min(1.0, Session.PartitionResult.Receipt.VolumeDriftRatio)) * 100.0;
        char KeptBuf[48] {};
        std::snprintf(KeptBuf, sizeof(KeptBuf), "Volume kept  %.3f %%", KeptPct);
        DrawChip(KeptBuf, true);

        char SegBuf[64] {};
        std::snprintf(SegBuf, sizeof(SegBuf), "%u convex segments · %zu seam joins",
            Session.SourceSegmentCount, Session.PartitionResult.Bonds.size());
        DrawChip(SegBuf, false);

        if (Session.ExecutionPhase != ExecutionStripPhase::Edit)
        {
            char RelBuf[48] {};
            std::snprintf(RelBuf, sizeof(RelBuf), "Released  %u of %zu",
                Session.CountReleasedFragments(), Session.PartitionResult.Fragments.size());
            DrawChip(RelBuf, true);
        }

        if (Session.RenderOptions.ShowCollisionShapes)
        {
            const bool ExactShape = (Session.CollisionShapeMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh);
            size_t TotalHulls = 0u;
            for (const auto& F : Session.PartitionResult.Fragments)
            {
                TotalHulls += std::max<size_t>(1u, F.SubConvexHulls.size());
            }
            char ColDbgBuf[72] {};
            if (ExactShape)
            {
                std::snprintf(ColDbgBuf, sizeof(ColDbgBuf), "Collision debug · Exact shape (%zu sub-hulls)", TotalHulls);
            }
            else
            {
                std::snprintf(ColDbgBuf, sizeof(ColDbgBuf), "Collision debug · Convex hull (%zu hulls)", Session.PartitionResult.Fragments.size());
            }
            DrawChip(ColDbgBuf, true);
        }
    }

    // 5. Screen-Projected Impact Reticle & Callout with Leader Line
    if (Session.ShowReticle && Session.RenderOptions.ShowImpactMarker && CachedWidthPx > 0u && CachedHeightPx > 0u)
    {
        const Mathematics::Axis3D WorldImpact = Session.ImpactPointLocal + Session.RenderOptions.SolidWorldOffsetM;
        float ScreenX = 0.0f, ScreenY = 0.0f, Depth = 0.0f;
        if (FractureViewportRenderer::ProjectWorldToViewport(
                Session.Camera, WorldImpact, CachedWidthPx, CachedHeightPx, ScreenX, ScreenY, Depth))
        {
            const float Cx = WellRect.X + (ScreenX / static_cast<float>(CachedWidthPx)) * WellRect.Width;
            const float Cy = WellRect.Y + (ScreenY / static_cast<float>(CachedHeightPx)) * WellRect.Height;
            if (WellRect.Contains({ Cx, Cy }))
            {
                Surface.StrokeCircle({ Cx, Cy }, 16.0f, ColorRgba { 255u, 138u, 61u, 195u }, 1.2f);
                Surface.StrokeCircle({ Cx, Cy }, 6.0f, ColorRgba { 255u, 138u, 61u, 235u }, 1.3f);
                Surface.StrokeLine({ Cx - 22.0f, Cy }, { Cx + 22.0f, Cy }, ColorRgba { 255u, 138u, 61u, 150u }, 1.0f);
                Surface.StrokeLine({ Cx, Cy - 22.0f }, { Cx, Cy + 22.0f }, ColorRgba { 255u, 138u, 61u, 150u }, 1.0f);

                const float CardX = std::min(WellRect.Right() - 226.0f, Cx + 30.0f);
                const float CardY = std::max(WellRect.Y + 60.0f, Cy - 66.0f);
                const Rectangle2D Callout { CardX, CardY, 212.0f, 46.0f };
                Surface.StrokeLine({ Cx + 11.0f, Cy - 11.0f }, { Callout.X, Callout.Bottom() - 8.0f }, ColorRgba { 255u, 138u, 61u, 170u }, 1.1f);
                Surface.FillRectangle(Callout, ColorRgba { 21u, 21u, 22u, 224u }, 14.0f);
                Surface.StrokeRectangle(Callout, ColorRgba { 255u, 138u, 61u, 90u }, 14.0f, 1.0f);

                Surface.FillCircle({ Callout.X + 14.0f, Callout.Y + 15.0f }, 3.5f, ColorRgba { 255u, 138u, 61u, 255u });
                char Line1[48] {}, Line2[72] {};
                if (Session.RenderOptions.SelectedFragmentIndex >= 0 &&
                    static_cast<size_t>(Session.RenderOptions.SelectedFragmentIndex) < Session.PartitionResult.Fragments.size())
                {
                    const auto& SelFrag = Session.PartitionResult.Fragments[static_cast<size_t>(Session.RenderOptions.SelectedFragmentIndex)];
                    std::snprintf(Line1, sizeof(Line1), "%s · %.1f kg", SelFrag.Token.c_str(), SelFrag.MassKg);
                    std::snprintf(Line2, sizeof(Line2), "%.0f J kinetic · %s",
                        Session.QueryFragmentReleaseShareJ(SelFrag.Index),
                        Session.QueryFragmentReleased(SelFrag.Index) ? "released" : "held");
                }
                else
                {
                    std::snprintf(Line1, sizeof(Line1), "Impact · %.2f kJ", Session.ImpactEnergyJ / 1000.0);
                    std::snprintf(Line2, sizeof(Line2), "%.2f, %.2f, %.2f m · %s",
                        Session.ImpactPointLocal.X, Session.ImpactPointLocal.Y, Session.ImpactPointLocal.Z,
                        Session.ActiveMaterialSpec().ShortLabel);
                }
                Surface.Text({ Callout.X + 24.0f, Callout.Y + 8.5f }, 11.2f, ColorRgba { 240u, 240u, 238u, 255u }, Line1);
                Surface.Text({ Callout.X + 13.0f, Callout.Y + 26.5f }, 8.4f, ColorRgba { 157u, 157u, 155u, 255u }, Line2);
            }
        }
    }

    // 6. Bottom-Left Energy LOG Legend
    if (Session.RenderOptions.RenderMode == FractureRenderViewMode::Energy)
    {
        const Rectangle2D Legend { WellRect.X + 13.0f, WellRect.Bottom() - 68.0f, 230.0f, 55.0f };
        Surface.FillRectangle(Legend, ColorRgba { 20u, 20u, 21u, 218u }, 16.0f);
        Surface.StrokeRectangle(Legend, ColorRgba { 255u, 255u, 255u, 24u }, 16.0f, 0.9f);
        Surface.Text({ Legend.X + 13.0f, Legend.Y + 10.0f }, 9.4f, ColorRgba { 169u, 169u, 167u, 255u }, "Energy");
        const Rectangle2D LogPill { Legend.Right() - 42.0f, Legend.Y + 8.0f, 30.0f, 16.0f };
        Surface.FillRectangle(LogPill, ColorRgba { 255u, 255u, 255u, 16u }, 5.0f);
        Surface.TextCentered(LogPill, 7.6f, ColorRgba { 201u, 201u, 199u, 255u }, "LOG");

        constexpr uint32_t Slices = 32u;
        const float RampW = Legend.Width - 26.0f;
        const float SliceW = RampW / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            uint8_t R = 0u, G = 0u, B = 0u;
            FractureViewportRenderer::SampleEnergySwatchSrgb(static_cast<float>(S) / static_cast<float>(Slices - 1u), R, G, B);
            Surface.FillRectangle({ Legend.X + 13.0f + static_cast<float>(S) * SliceW, Legend.Y + 29.0f, SliceW + 0.6f, 8.0f },
                { R, G, B, 255u }, (S == 0u || S + 1u == Slices) ? 4.0f : 0.0f);
        }
        Surface.Text({ Legend.X + 13.0f, Legend.Y + 41.0f }, 8.0f, ColorRgba { 138u, 138u, 136u, 255u }, "<= 0.1 J share");
        char PeakStr[32] {};
        std::snprintf(PeakStr, sizeof(PeakStr), "%.0f J peak share",
            Session.PartitionResult.Fragments.empty() ? 0.0f : Session.QueryFragmentReleaseShareJ(0u));
        Surface.TextRightAligned({ Legend.X, Legend.Y + 41.0f, Legend.Width - 13.0f, 10.0f }, 8.0f, ColorRgba { 138u, 138u, 136u, 255u }, PeakStr);
    }

    // Bottom-right hint
    {
        const char* Hint = "Drag to orbit · right-drag to pan · wheel to zoom · click a fragment to select";
        const float Hw = Surface.MeasureTextWidth(8.4f, Hint);
        Surface.Text({ WellRect.Right() - Hw - 14.0f, WellRect.Bottom() - 18.0f }, 8.4f, ColorRgba { 92u, 92u, 90u, 255u }, Hint);
    }

    // 7. Floating Diagnostics Overlay (F3)
    bool OverlayInteracted = false;
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
            Surface, WellRect, DiagSpec, Session.DiagnosticsSession, Pointer);
        OverlayInteracted = DiagRes.Interacted;
    }

    // 8. Pointer Orbit, Zoom & Surface Pick
    if (ControlAppearance::QueryPointerOver(WellRect, Pointer) &&
        !OverlayInteracted &&
        !ExplodeSliderDrag &&
        !Session.ShadingDropdownOpen &&
        !Session.DebugDropdownOpen)
    {
        if (std::abs(WheelDeltaY) > 0.001f)
        {
            Session.Camera.DistanceM = std::clamp(Session.Camera.DistanceM - WheelDeltaY * 0.25f, 1.3f, 8.5f);
        }
        if (Pointer.Pressed)
        {
            OrbitDragging = true;
            LastDragX = Pointer.X;
            LastDragY = Pointer.Y;
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
                const float NormX = (Pointer.X - WellRect.X) / std::max(1.0f, WellRect.Width);
                const float NormY = (Pointer.Y - WellRect.Y) / std::max(1.0f, WellRect.Height);
                const auto Hit = FractureViewportRenderer::PickViewportSurface(
                    Session.HasFractured ? &Session.PartitionResult : nullptr,
                    &Session.SourceSolid,
                    (Session.ExecutionPhase != ExecutionStripPhase::Edit && Session.HasSimulatedData) ? &Session.SimRunner : nullptr,
                    Session.Camera, Session.RenderOptions, NormX, NormY, CachedWidthPx, CachedHeightPx);
                if (Hit.Hit)
                {
                    if (Session.AimingImpactTool)
                    {
                        Session.TriggerViewportImpact(Hit.HitPointWorld, Hit.HitNormalWorld, Hit.FragmentIndex);
                        Session.AimingImpactTool = false;
                    }
                    else
                    {
                        Session.RenderOptions.SelectedFragmentIndex = Hit.FragmentIndex;
                    }
                }
            }
        }
    }
    if (!Pointer.Down)
    {
        OrbitDragging = false;
    }

    Surface.PopClip();

    if (Session.ShadingDropdownOpen)
    {
        uint32_t Chosen = ShadingIdx;
        if (DropdownSelector::ConstructOptionSheet(
                Surface, ShadingDropRect, ShadingLabels.data(), static_cast<uint32_t>(ShadingLabels.size()),
                ShadingIdx, Pointer, Chosen).Pressed)
        {
            Session.RenderOptions.RenderMode = static_cast<FractureRenderViewMode>(Chosen % 4u);
            Session.ShadingDropdownOpen = false;
        }
    }

    if (Session.DebugDropdownOpen)
    {
        struct DebugOptionEntry
        {
            const char* Label;
            bool*       Flag;
            const char* Badge;
        };
        const bool ExactColMode =
            Session.CollisionShapeMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh;
        std::array<DebugOptionEntry, 6> DebugItems {{
            { "Collision shapes",   &Session.RenderOptions.ShowCollisionShapes, ExactColMode ? "Exact" : "Convex" },
            { "Mesh wireframe",     &Session.RenderOptions.ShowWireframe,       nullptr },
            { "Breakable bonds",    &Session.RenderOptions.ShowBonds,           nullptr },
            { "Wavefronts & seeds", &Session.RenderOptions.ShowWavefronts,      nullptr },
            { "Studio ground grid", &Session.RenderOptions.ShowGroundGrid,      nullptr },
            { "Diagnostics (F3)",   &Session.DiagnosticsSession.Visible,        nullptr }
        }};

        constexpr float RowH = 29.0f;
        const float MenuW    = 236.0f;
        const float MenuH    = 14.0f + static_cast<float>(DebugItems.size()) * RowH;
        const Rectangle2D MenuRect {
            std::min(DebugDropRect.X, Bounds.Right() - MenuW - 8.0f),
            DebugDropRect.Bottom() + 5.0f,
            MenuW,
            MenuH
        };

        Surface.FillRectangle(MenuRect, ColorRgba { 22u, 22u, 24u, 246u }, 12.0f);
        Surface.StrokeRectangle(MenuRect, ColorRgba { 255u, 255u, 255u, 32u }, 12.0f, 1.0f);

        for (size_t Idx = 0u; Idx < DebugItems.size(); ++Idx)
        {
            const auto& Item = DebugItems[Idx];
            const Rectangle2D RowRect {
                MenuRect.X + 6.0f,
                MenuRect.Y + 7.0f + static_cast<float>(Idx) * RowH,
                MenuRect.Width - 12.0f,
                RowH - 2.0f
            };
            const bool RowHov = ControlAppearance::QueryPointerOver(RowRect, Pointer);
            const bool Active = (Item.Flag != nullptr) && (*Item.Flag);
            if (RowHov)
            {
                Surface.FillRectangle(RowRect, ColorRgba { 255u, 255u, 255u, 16u }, 7.0f);
            }
            else if (Active && Idx == 0u)
            {
                Surface.FillRectangle(RowRect, ColorRgba { 52u, 199u, 89u, 18u }, 7.0f);
            }

            const Rectangle2D BoxRect { RowRect.X + 8.0f, RowRect.Center().Y - 7.0f, 14.0f, 14.0f };
            Surface.FillRectangle(BoxRect, Active ? ColorRgba { 56u, 206u, 108u, 255u } : ColorRgba { 34u, 34u, 36u, 255u }, 4.0f);
            Surface.StrokeRectangle(BoxRect, Active ? ColorRgba { 88u, 232u, 136u, 255u } : ColorRgba { 255u, 255u, 255u, 34u }, 4.0f, 0.9f);
            if (Active)
            {
                FractureIcons::DrawCheckGlyph(Surface, { BoxRect.X + 2.5f, BoxRect.Y + 2.5f, 9.0f, 9.0f }, ColorRgba { 11u, 34u, 18u, 255u }, 2.2f);
            }

            Surface.Text({ BoxRect.Right() + 9.0f, RowRect.Y + 7.0f }, 10.2f,
                Active ? ColorRgba { 236u, 238u, 236u, 255u } : ColorRgba { 166u, 166u, 164u, 255u }, Item.Label);

            if (Item.Badge != nullptr)
            {
                const float Bw = Surface.MeasureTextWidth(8.0f, Item.Badge) + 12.0f;
                const Rectangle2D BadgeRect { RowRect.Right() - Bw - 6.0f, RowRect.Center().Y - 8.5f, Bw, 17.0f };
                Surface.FillRectangle(BadgeRect,
                    ExactColMode ? ColorRgba { 56u, 206u, 108u, 26u } : ColorRgba { 64u, 200u, 255u, 26u }, 6.0f);
                Surface.TextCentered(BadgeRect, 8.0f,
                    ExactColMode ? ColorRgba { 98u, 232u, 142u, 255u } : ColorRgba { 116u, 220u, 255u, 255u }, Item.Badge);
            }

            if (RowHov && Pointer.Pressed && Item.Flag != nullptr)
            {
                *Item.Flag = !(*Item.Flag);
            }
        }

        if (Pointer.Pressed &&
            !ControlAppearance::QueryPointerOver(MenuRect, Pointer) &&
            !ControlAppearance::QueryPointerOver(DebugDropRect, Pointer))
        {
            Session.DebugDropdownOpen = false;
        }
    }

    // 10. 60px Playback Footer
    const auto SimTelemetry = Session.SimRunner.InspectTelemetry();
    const bool ShowBaked = (Session.ExecutionPhase != ExecutionStripPhase::Edit) && Session.HasSimulatedData;
    SimulationTimelineFooterReading FooterRead {};
    FooterRead.ElapsedSeconds       = Session.ElapsedSeconds;
    FooterRead.DurationSeconds      = Session.DurationSeconds;
    FooterRead.Playing              = Session.Playing;
    FooterRead.LoopEnabled          = Session.LoopEnabled;
    FooterRead.HasBakedTimeline     = ShowBaked;
    FooterRead.TimeScale            = Session.TimeScale;
    FooterRead.CurrentKineticJoules = static_cast<float>(SimTelemetry.TotalKineticJ);
    FooterRead.MovingBodyCount      = SimTelemetry.AwakeFragmentCount;
    FooterRead.PhaseCaption         = (Session.ExecutionPhase == ExecutionStripPhase::Edit)
                                        ? "EDIT · NOT BAKED"
                                        : (Session.Playing ? "SIMULATING · JOLT" : "PAUSED · SCRUB");
    FooterRead.EmptyPromptText      = "Timeline bakes when you simulate · Shift+2 or Space to play";
    FooterRead.EnergyCurveSamples   = Session.KineticCurveSamples;
    if (ShowBaked)
    {
        FooterRead.EventMarkers = Session.TimelineMarkers;
    }
    const auto FooterRes = SimulationTimelineFooter::Construct(
        Surface, PlaybackRect, FooterRead, Session.TimelineSession, Pointer);
    if (FooterRes.ToStartPressed) Session.RewindToStart();
    if (FooterRes.TogglePlayPressed) Session.TogglePlayPause();
    if (FooterRes.SingleStepPressed) Session.StepSingleFrame();
    if (FooterRes.LoopToggled) Session.LoopEnabled = !Session.LoopEnabled;
    if (FooterRes.TimeScaleChanged) Session.TimeScale = FooterRes.NewTimeScale;
    if (FooterRes.Scrubbed) Session.ScrubToTime(FooterRes.ScrubbedTimeSeconds);

    // 11. 34px Viewport Bottom Footer (vp-foot)
    std::string UpperTitle = ActivePrim.Title;
    std::transform(UpperTitle.begin(), UpperTitle.end(), UpperTitle.begin(), [](unsigned char Ch) { return static_cast<char>(std::toupper(Ch)); });
    char FootLeft[96] {}, FootRight[96] {};
    std::snprintf(FootLeft, sizeof(FootLeft), "ANALYTICAL %s · %s · FACETING 0.00 %%",
        UpperTitle.c_str(), Session.FormatPrimitiveSubtitle().c_str());
    std::snprintf(FootRight, sizeof(FootRight), "%u SEGMENTS · %u TRIS · %zu CLOSED",
        Session.SourceSegmentCount, Session.LastDrawnTriangles, Session.PartitionResult.Fragments.size());
    Surface.Text({ FootRect.X + 16.0f, FootRect.Y + 11.0f }, 8.0f, ColorRgba { 105u, 105u, 103u, 255u }, FootLeft);
    Surface.TextRightAligned({ FootRect.X, FootRect.Y + 11.0f, FootRect.Width - 16.0f, 12.0f }, 8.0f, ColorRgba { 128u, 128u, 126u, 255u }, FootRight);
}

} // namespace Frontier::Workspaces::Fracture
